/*
 * ntpconverter.c - Amiga MOD -> NinjaTrackerPlus (.ntp) converter
 *
 * A C port of Ninjaforce's ntpconverter_lib.php / ntpconverter.php
 * (c) 2018-2025 Jesse Blue / Ninjaforce.
 *
 * Converts a ProTracker MOD file into the version-2 .ntp format used by
 * the NinjaTrackerPlus player on the Apple IIgs. Produces the raw song
 * file only (no ProDOS header); use AppleCommander or CiderPress to put
 * it on disk as type $D5, aux $0008 if desired.
 *
 * Usage: ntpconverter MODFILE [STREAM_FORBIDDEN|STREAM_ALLOW|STREAM_ENFORCE] [OUTFILE]
 *
 * The output defaults to the MOD filename (a leading "mod." dropped)
 * with a ".ntp" suffix.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

/* ------------------------- constants ------------------------- */

#define NTP_MAX_INSTRUMENTS    255
#define NTP_LINES_PER_PATTERN  64
#define NTP_NOTE_BYTES         4

#define MODINST_TYPE_SINGLE       0
#define MODINST_TYPE_LOOPED       1
#define MODINST_TYPE_LOOPWITHHEAD 2

#define NTP_TYPE_SINGLE        0
#define NTP_TYPE_LOOPED        1
#define NTP_TYPE_LOOPHEADER    2
#define NTP_TYPE_LOOP          3
#define NTP_TYPE_STREAM_SINGLE 8
#define NTP_TYPE_STREAM_LOOPED 9

#define CONFIG_STREAM_FORBIDDEN 0
#define CONFIG_STREAM_ALLOW     1
#define CONFIG_STREAM_ENFORCE   2

#define STOPPER_BYTES_COUNT    8
#define OPTIMUM_SMALL_LOOP     512
#define INITIAL_STREAMBUFFER   512
#define STREAMBUFFER_COPY      256
#define STREAMBUFFER_LENGTH    512

/* ------------------------- byte buffer ------------------------- */

typedef struct {
    unsigned char *b;
    size_t n, cap;
} Buf;

static void buf_init(Buf *x)  { x->b = NULL; x->n = 0; x->cap = 0; }
static void buf_free(Buf *x)  { free(x->b); x->b = NULL; x->n = x->cap = 0; }
static void buf_reset(Buf *x) { x->n = 0; }

static void buf_grow(Buf *x, size_t need)
{
    size_t c;
    if (need <= x->cap) return;
    c = x->cap ? x->cap : 256;
    while (c < need) c *= 2;
    x->b = (unsigned char *)realloc(x->b, c);
    if (!x->b) { fprintf(stderr, "out of memory\n"); exit(1); }
    x->cap = c;
}

static void buf_byte(Buf *x, unsigned char v) { buf_grow(x, x->n + 1); x->b[x->n++] = v; }

static void buf_mem(Buf *x, const void *p, size_t n)
{
    if (n == 0) return;
    buf_grow(x, x->n + n);
    memcpy(x->b + x->n, p, n);
    x->n += n;
}

static void buf_str(Buf *x, const char *s) { buf_mem(x, s, strlen(s)); }

static void buf_printf(Buf *x, const char *fmt, ...)
{
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    buf_str(x, tmp);
}

static unsigned char buf_get(const Buf *x, size_t i) { return (i < x->n) ? x->b[i] : 0; }

/* ------------------------- string list (Vec) ------------------------- */

typedef struct {
    void **items;
    size_t n, cap;
} Vec;

static void vec_init(Vec *v) { v->items = NULL; v->n = 0; v->cap = 0; }
static void vec_free(Vec *v) { free(v->items); v->items = NULL; v->n = v->cap = 0; }

static void vec_push(Vec *v, void *p)
{
    size_t c;
    if (v->n == v->cap) {
        c = v->cap ? v->cap * 2 : 16;
        v->items = (void **)realloc(v->items, c * sizeof(void *));
        if (!v->items) { fprintf(stderr, "out of memory\n"); exit(1); }
        v->cap = c;
    }
    v->items[v->n++] = p;
}

static void vec_push_str(Vec *v, const char *s)
{
    char *p = strdup(s);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    vec_push(v, p);
}

/* ------------------------- small int list ------------------------- */

typedef struct {
    int n;
    int v[300];
} IntList;

static int intlist_has(const IntList *l, int value)
{
    int i;
    for (i = 0; i < l->n; i++) if (l->v[i] == value) return 1;
    return 0;
}

static void intlist_add(IntList *l, int value)
{
    if (l->n < 300 && !intlist_has(l, value)) l->v[l->n++] = value;
}

/* ------------------------- MOD instrument ------------------------- */

typedef struct {
    int number;              /* 1..15 or 1..31 */
    char name[23];
    uint32_t length;         /* raw sample length in bytes */
    int finetune;            /* lower nibble 0..15 */
    int volume;              /* 0..64 */
    uint32_t repeat_pointer;
    uint32_t repeat_length;
    Buf data;                /* raw MOD sample bytes */
    int must_stream;
} ModInstrument;

static int mod_instrument_type(const ModInstrument *mi)
{
    if (mi->repeat_length < 3)  return MODINST_TYPE_SINGLE;
    if (mi->repeat_pointer > 2) return MODINST_TYPE_LOOPWITHHEAD;
    return MODINST_TYPE_LOOPED;
}

static int mod_must_be_streamed(const ModInstrument *mi)
{
    if (mi->must_stream) return 1;
    if (mi->length == 32768) return 1;
    return (mi->length > 32768 - STOPPER_BYTES_COUNT);
}

static void mod_force_stream(ModInstrument *mi) { mi->must_stream = 1; }

/* ------------------------- NTP instrument ------------------------- */

typedef struct {
    int type;
    int number;              /* 1..255 ntp instrument number */
    int number_original;     /* original MOD instrument number */
    char name[23];
    uint32_t original_length;
    int volume;
    int finetune;
    uint32_t repeat_pointer;
    uint32_t repeat_length;
    Buf data;                /* processed, ensoniq-shifted bytes */
    uint32_t length;
    int page_size;
    int has_stopper_bytes;
    Buf data_original_converted;
} NtpInstrument;

static int is_optimum(size_t length)
{
    static const size_t opt[] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
    size_t i;
    for (i = 0; i < sizeof opt / sizeof opt[0]; i++) if (opt[i] == length) return 1;
    return 0;
}

static void get_converted_instrument_bytes(const ModInstrument *mi, Buf *out)
{
    size_t i;
    buf_reset(out);
    for (i = 0; i < mi->data.n; i++) {
        int e = (mi->data.b[i] + 128) & 0xff;
        if (e == 0) e = 1;
        buf_byte(out, (unsigned char)e);
    }
}

static void optimize_loop(Buf *data)
{
    size_t length = data->n;
    if (!is_optimum(length) && length < OPTIMUM_SMALL_LOOP) {
        unsigned char src[OPTIMUM_SMALL_LOOP];
        int stopper = STOPPER_BYTES_COUNT;
        int exact = (OPTIMUM_SMALL_LOOP % length == 0); /* loop fits exactly n times */
        if (exact) stopper = 0;
        /* snapshot the original loop so the append below cannot read a stale
           pointer if buf_grow() moves the buffer */
        memcpy(src, data->b, length);
        while (data->n + length + (size_t)stopper <= OPTIMUM_SMALL_LOOP) {
            buf_mem(data, src, length);
        }
    }
}

static int find_closest_zero_crossing(const Buf *data)
{
    size_t i;
    int min_ptr = 0, min_diff = 1000;
    for (i = 0; i < data->n; i++) {
        int diff = abs((int)data->b[i] - 128);
        if (min_diff > diff) { min_diff = diff; min_ptr = (int)i; }
    }
    return min_ptr;
}

/* on return, *head and *loop each own a freshly allocated buffer */
static void split_rotate_loop(Buf *head, Buf *loop, const NtpInstrument *ni, int rotate_loops)
{
    const Buf *orig = &ni->data_original_converted;
    size_t rp = ni->repeat_pointer;
    size_t rl = ni->repeat_length;
    size_t i;
    int pos;

    buf_init(head);
    buf_init(loop);
    for (i = 0; i < rp && i < orig->n; i++) buf_byte(head, orig->b[i]);
    for (i = rp; i < rp + rl && i < orig->n; i++) buf_byte(loop, orig->b[i]);

    pos = find_closest_zero_crossing(loop);

    if (rotate_loops && pos > 0) {
        Buf head_new, loop_new;
        buf_init(&head_new);
        buf_init(&loop_new);
        buf_mem(&head_new, head->b, head->n);
        buf_mem(&head_new, loop->b, (size_t)pos);
        buf_mem(&loop_new, loop->b + pos, loop->n - (size_t)pos);
        buf_mem(&loop_new, loop->b, (size_t)pos);
        buf_free(head);
        buf_free(loop);
        *head = head_new;
        *loop = loop_new;
    }
}

static void get_instrument_data(NtpInstrument *ni, int rotate_loops)
{
    const Buf *orig = &ni->data_original_converted;
    size_t rp = ni->repeat_pointer, rl = ni->repeat_length;
    size_t i;
    int add_stopper;
    Buf data, head, loop;

    buf_init(&data);
    buf_init(&head);
    buf_init(&loop);

    switch (ni->type) {
    case NTP_TYPE_SINGLE:
        buf_mem(&data, orig->b, orig->n);
        add_stopper = !is_optimum(data.n);
        break;

    case NTP_TYPE_LOOPED:
        for (i = rp; i < rp + rl && i < orig->n; i++) buf_byte(&data, orig->b[i]);
        optimize_loop(&data);
        add_stopper = !is_optimum(data.n);
        break;

    case NTP_TYPE_LOOPHEADER:
    case NTP_TYPE_LOOP: {
        Buf hnew, lnew;
        split_rotate_loop(&hnew, &lnew, ni, rotate_loops);
        if (ni->type == NTP_TYPE_LOOPHEADER) {
            buf_mem(&data, hnew.b, hnew.n);
        } else {
            buf_mem(&data, lnew.b, lnew.n);
            optimize_loop(&data);
        }
        buf_free(&hnew);
        buf_free(&lnew);
        add_stopper = !is_optimum(data.n);
        break;
    }

    case NTP_TYPE_STREAM_SINGLE:
        buf_mem(&data, orig->b, orig->n);
        add_stopper = 1;
        break;

    case NTP_TYPE_STREAM_LOOPED: {
        Buf loops;
        unsigned char sbuf[STREAMBUFFER_COPY];
        buf_init(&loops);
        for (i = 0; i < rp && i < orig->n; i++) buf_byte(&head, orig->b[i]);
        for (i = rp; i < rp + rl && i < orig->n; i++) buf_byte(&loop, orig->b[i]);
        /* make sure the loops are larger than 512 bytes */
        buf_mem(&loops, loop.b, loop.n);
        while (loops.n < INITIAL_STREAMBUFFER) buf_mem(&loops, loop.b, loop.n);
        /* always append 256 bytes of the loop, so that we can always copy 256 bytes when streaming */
        /* snapshot the prefix first: appending loops.b to itself could read a stale
           pointer if buf_grow() moves the buffer past its capacity */
        memcpy(sbuf, loops.b, STREAMBUFFER_COPY);
        buf_mem(&loops, sbuf, STREAMBUFFER_COPY);
        buf_mem(&data, head.b, head.n);
        buf_mem(&data, loops.b, loops.n);
        buf_free(&loops);
        add_stopper = 0;
        break;
    }
    }

    ni->has_stopper_bytes = add_stopper;
    if (add_stopper) {
        for (i = 0; i < STOPPER_BYTES_COUNT; i++) buf_byte(&data, 0);
    }

    buf_free(&head);
    buf_free(&loop);
    ni->data = data;
    ni->length = (uint32_t)data.n;
}

static void ntp_instrument_init(NtpInstrument *ni, const ModInstrument *mi,
                                int type, int number, int rotate_loops)
{
    memset(ni, 0, sizeof *ni);
    ni->type = type;
    ni->number = number;
    ni->number_original = mi->number;
    strncpy(ni->name, mi->name, sizeof ni->name - 1);
    ni->name[sizeof ni->name - 1] = '\0';
    ni->volume = mi->volume;
    ni->finetune = mi->finetune;
    ni->repeat_pointer = mi->repeat_pointer;
    ni->repeat_length = mi->repeat_length;

    buf_init(&ni->data);
    buf_init(&ni->data_original_converted);
    get_converted_instrument_bytes(mi, &ni->data_original_converted);
    ni->original_length = (uint32_t)ni->data_original_converted.n;

    get_instrument_data(ni, rotate_loops);
    ni->page_size = (int)((ni->length + 255) / 256);
}

static int ntp_is_first_splitted(const NtpInstrument *ni) { return ni->type == NTP_TYPE_LOOPHEADER; }

static int ntp_is_streamed(const NtpInstrument *ni)
{
    return ni->type == NTP_TYPE_STREAM_SINGLE || ni->type == NTP_TYPE_STREAM_LOOPED;
}

static int ntp_get_type_for_file(const NtpInstrument *ni)
{
    int optimal = is_optimum(ni->length);
    return ni->type + ((optimal && !ni->has_stopper_bytes) ? 4 : 0);
}

static int ntp_get_osc_count(const NtpInstrument *ni)
{
    int optimal = is_optimum(ni->length);
    if (ni->type == NTP_TYPE_SINGLE ||
        (ni->type == NTP_TYPE_LOOPED && optimal && !ni->has_stopper_bytes)) {
        return 1;
    }
    return 2;
}/* ------------------------- instrument container ------------------------- */

typedef struct {
    int type;              /* 1 instrument, 2 streambuffer, 3 interrupt */
    int size;
    int pages;
    int page_start;
    NtpInstrument *instrument;
    int track_number;
} Stuff;

typedef struct {
    int doc_pages[256];
    Stuff stuff[300];
    int stuff_count;
    int number_of_tracks;
    int track_pages[32];
    Vec instrument_errors;
    int instrument_doc_pages[256];
    int instrument_doc_sizes[256];
} Container;

static int container_reserve(Container *ic, int page_size, int type)
{
    static const int boundaries[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    int align = -1, page_start = -1;
    int i;
    for (i = 0; i < 8; i++) {
        if (page_size <= boundaries[i]) { align = boundaries[i]; break; }
    }
    if (align < 0) return -2;   /* larger than 32k, the DOC can't play it */
    for (i = 0; i < 256; i += align) {
        if (ic->doc_pages[i] == 0) { page_start = i; break; }
    }
    if (page_start >= 0) {
        for (i = page_start; i < page_start + page_size; i++) ic->doc_pages[i] = type;
    }
    return page_start;
}

static int container_find_doc_register(int page_size)
{
    static const int boundaries[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    static const int regs[8] = { 0, 9, 18, 27, 36, 45, 54, 63 };
    int i;
    if (page_size == 0) return 0;
    for (i = 0; i < 8; i++) if (page_size <= boundaries[i]) return regs[i];
    return 0;
}

static uint32_t container_get_smaller_length(uint32_t length)
{
    static const int boundaries[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    int i;
    for (i = 7; i >= 0; i--) {
        uint32_t bytes = (uint32_t)boundaries[i] * 256;
        if (bytes < length) return bytes;
    }
    return length;
}

static void container_error_instrument(Container *ic, const NtpInstrument *ni, const char *error)
{
    uint32_t length;
    uint32_t smaller;
    char msg[512];

    switch (ni->type) {
    default:
    case NTP_TYPE_SINGLE:
    case NTP_TYPE_STREAM_SINGLE:
        length = ni->original_length;
        break;
    case NTP_TYPE_LOOPHEADER:
    case NTP_TYPE_LOOPED:
    case NTP_TYPE_LOOP:
    case NTP_TYPE_STREAM_LOOPED:
        length = ni->length;
        break;
    }

    smaller = container_get_smaller_length(length);
    if (smaller < length) {
        if (ni->type == NTP_TYPE_LOOPHEADER) {
            snprintf(msg, sizeof msg,
                     "Instrument %03d: %s - consider reducing the loop header from %u to %u bytes or removing it.",
                     ni->number_original, error, length, smaller);
        } else if (ni->type == NTP_TYPE_LOOPED || ni->type == NTP_TYPE_LOOP ||
                   ni->type == NTP_TYPE_STREAM_LOOPED) {
            snprintf(msg, sizeof msg,
                     "Instrument %03d: %s - consider reducing the loop from %u to %u bytes.",
                     ni->number_original, error, length, smaller);
        } else {
            snprintf(msg, sizeof msg,
                     "Instrument %03d: %s - consider reducing the size from %u to %u bytes.",
                     ni->number_original, error, length, smaller);
        }
    } else {
        snprintf(msg, sizeof msg, "Instrument %03d: %s.", ni->number_original, error);
    }
    vec_push_str(&ic->instrument_errors, msg);
}

static void container_put_instrument(Container *ic, int page_start, NtpInstrument *ni)
{
    if (page_start == -2) {
        container_error_instrument(ic, ni, "Too large (must be < 32k)");
    } else if (page_start == -1) {
        container_error_instrument(ic, ni, "Too large");
    } else {
        ic->instrument_doc_pages[ni->number] = page_start;
        ic->instrument_doc_sizes[ni->number] = container_find_doc_register(ni->page_size);
    }
}

static void container_put_streambuffer(Container *ic, int page_start, int track_number)
{
    if (page_start >= 0) {
        ic->track_pages[track_number] = page_start;
    } else {
        vec_push_str(&ic->instrument_errors, "Unable to allocate doc ram memory for stream buffer.");
    }
}

static void container_put_stuff(Container *ic)
{
    int i;
    for (i = 0; i < 256; i++) ic->doc_pages[i] = 0;
    for (i = 0; i < 32; i++) ic->track_pages[i] = 0;
    for (i = 0; i < 256; i++) { ic->instrument_doc_pages[i] = 0; ic->instrument_doc_sizes[i] = 0; }
    ic->instrument_errors.n = 0;

    for (i = 0; i < ic->stuff_count; i++) {
        Stuff *s = &ic->stuff[i];
        int page_size = (s->size + 255) / 256;
        int page_start = container_reserve(ic, page_size, s->type);
        s->page_start = page_start;
        switch (s->type) {
        case 1: /* instrument */
            container_put_instrument(ic, page_start, s->instrument);
            break;
        case 2: /* streambuffer */
            container_put_streambuffer(ic, page_start, s->track_number);
            break;
        case 3: /* interrupt dummy instrument */
            if (page_start < 0) {
                vec_push_str(&ic->instrument_errors,
                             "Unable to place interrupt dummy instrument in doc ram.");
            }
            break;
        }
    }
}

static void container_get_stuff(Container *ic, const NtpInstrument *const *ntps, int ntp_count,
                                const IntList *track_mod_instruments, int number_of_tracks)
{
    int streaming_tracks[32] = { 0 };
    int i, t;
    ic->stuff_count = 0;

    /* the first 256 bytes of DOC ram must be non-zero for the interrupt timer */
    {
        uint32_t max_len = 0;
        for (i = 0; i < ntp_count; i++) {
            if (!ntp_is_streamed(ntps[i]) && ntps[i]->length > max_len) max_len = ntps[i]->length;
        }
        if (max_len < 256) {
            Stuff *s = &ic->stuff[ic->stuff_count++];
            s->type = 3;
            s->size = 256;
            s->pages = 1;
            s->page_start = -1;
            s->instrument = NULL;
            s->track_number = -1;
        }
    }

    for (i = 0; i < ntp_count; i++) {
        NtpInstrument *ni = (NtpInstrument *)ntps[i];
        if (ntp_is_streamed(ni)) {
            for (t = 0; t < number_of_tracks; t++) {
                if (intlist_has(&track_mod_instruments[t], ni->number_original)) {
                    streaming_tracks[t] = 1;
                }
            }
        } else {
            Stuff *s = &ic->stuff[ic->stuff_count++];
            s->type = 1;
            s->size = (int)ni->length;
            s->pages = ni->page_size;
            s->page_start = -1;
            s->instrument = ni;
            s->track_number = -1;
        }
    }

    for (t = 0; t < number_of_tracks; t++) {
        if (streaming_tracks[t]) {
            Stuff *s = &ic->stuff[ic->stuff_count++];
            s->type = 2;
            s->size = STREAMBUFFER_LENGTH;
            s->pages = STREAMBUFFER_LENGTH / 256;
            s->page_start = -1;
            s->instrument = NULL;
            s->track_number = t;
        }
    }
}

static int stuff_cmp_desc(const Stuff *a, const Stuff *b)
{
    if (a->type == 3) return -1;
    if (b->type == 3) return 1;
    if (a->pages == b->pages) return 0;
    return (a->pages > b->pages) ? -1 : 1;
}

static void container_sort_stuff(Container *ic)
{
    /* stable insertion sort, largest first, interrupt first */
    int i, j;
    for (i = 1; i < ic->stuff_count; i++) {
        Stuff key = ic->stuff[i];
        j = i - 1;
        while (j >= 0 && stuff_cmp_desc(&key, &ic->stuff[j]) < 0) {
            ic->stuff[j + 1] = ic->stuff[j];
            j--;
        }
        ic->stuff[j + 1] = key;
    }
}

static int container_arrange(Container *ic, const NtpInstrument *const *ntps, int ntp_count,
                             const IntList *track_mod_instruments, int number_of_tracks)
{
    container_get_stuff(ic, ntps, ntp_count, track_mod_instruments, number_of_tracks);
    container_sort_stuff(ic);
    container_put_stuff(ic);
    return ic->instrument_errors.n == 0;
}

/* ------------------------- converter ------------------------- */

typedef struct {
    /* config */
    const char *path_source_mod;
    const char *path_output_ntp;
    int stream_config;

    /* mod file */
    Buf mod_data;
    char mod_name[21];

    /* geometry */
    int max_number_instruments;
    int no_of_tracks;
    int max_pat_cnt;
    int ptr_instnumb;
    int mod_headlen;
    int ptr_songp_start;
    int ptr_songp_end;
    int length_of_one_pattern;
    int length_of_all_patterns;
    Buf pattern_order;

    /* analysis */
    IntList needs_stream;
    IntList track_mod_instruments[32];

    /* instruments */
    ModInstrument mod_instruments[32];
    int mod_instrument_count;
    NtpInstrument *ntp_instruments[NTP_MAX_INSTRUMENTS + 5];
    int ntp_instrument_count;

    Container container;

    int doc_ptrs[256];
    int doc_sizes[256];
    int track_stream_pages[32];
    int track_oscillator_byte[32];
    int instrument_positions[256];

    int instrument_map[256];
    Buf ntp_notes;

    int oscillator_usage[32];

    Vec instrument_errors;
    Vec pattern_errors;
    Vec track_errors;
} Converter;

/* MOD format tags -> number of tracks.  "!PM!" is a 4-channel variant tag
 * used by the PETSCII Robots soundtrack. */
static const struct { const char *id; int tracks; } MOD_TYPE_TABLE[] = {
    { "M.K.", 4 }, { "1CHN", 1 }, { "2CHN", 2 }, { "3CHN", 3 }, { "4CHN", 4 },
    { "5CHN", 5 }, { "6CHN", 6 }, { "7CHN", 7 }, { "8CHN", 8 }, { "9CHN", 9 },
    { "01CH", 1 }, { "02CH", 2 }, { "03CH", 3 }, { "04CH", 4 }, { "05CH", 5 },
    { "06CH", 6 }, { "07CH", 7 }, { "08CH", 8 }, { "09CH", 9 }, { "10CH", 10 },
    { "11CH", 11 }, { "12CH", 12 }, { "13CH", 13 }, { "14CH", 14 }, { "15CH", 15 },
    { "16CH", 16 }, { "17CH", 17 }, { "18CH", 18 }, { "19CH", 19 }, { "20CH", 20 },
    { "21CH", 21 }, { "22CH", 22 }, { "23CH", 23 }, { "24CH", 24 }, { "25CH", 25 },
    { "26CH", 26 }, { "27CH", 27 }, { "28CH", 28 }, { "29CH", 29 }, { "30CH", 30 },
    { "31CH", 31 },
    { "01CN", 1 }, { "02CN", 2 }, { "03CN", 3 }, { "04CN", 4 }, { "05CN", 5 },
    { "06CN", 6 }, { "07CN", 7 }, { "08CN", 8 }, { "09CN", 9 }, { "10CN", 10 },
    { "11CN", 11 }, { "12CN", 12 }, { "13CN", 13 }, { "14CN", 14 }, { "15CN", 15 },
    { "16CN", 16 }, { "17CN", 17 }, { "18CN", 18 }, { "19CN", 19 }, { "20CN", 20 },
    { "21CN", 21 }, { "22CN", 22 }, { "23CN", 23 }, { "24CN", 24 }, { "25CN", 25 },
    { "26CN", 26 }, { "27CN", 27 }, { "28CN", 28 }, { "29CN", 29 }, { "30CN", 30 },
    { "31CN", 31 },
    { "FLT4", 4 }, { "FLT8", 8 },
    { "!PM!", 4 },
    { NULL, 0 }
};

static const int MOD_NOTES[72] = {
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  906,
     856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
     428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
     214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
     107,  101,   95,   90,   85,   80,   75,   71,   67,   63,   60,   56,
      53,   50,   47,   45,   42,   40,   37,   35,   33,   31,   30,   28
};

/* stereo defaults for the NTP output channels, 1 per track, 0-based (matches
 * the PHP converter's $doc_stereo_defaults list). */
static int doc_stereo_default(int index) { return (index % 2 == 0) ? 1 : 0; }/* ------------------------- MOD file access helpers ------------------------- */

static int get_mod_byte(const Buf *mod, size_t pos) { return buf_get(mod, pos); }

static long get_amiga_word(const Buf *mod, size_t pos)
{
    return 256L * get_mod_byte(mod, pos) + get_mod_byte(mod, pos + 1);
}

static void get_module_string(const Buf *mod, size_t ptr, int max_length, char *out, size_t outsz)
{
    size_t i;
    int n = 0;
    for (i = 0; i < (size_t)max_length; i++) {
        int byte = get_mod_byte(mod, ptr + i);
        if (byte == 0) break;
        if (n < (int)outsz - 1) out[n++] = (char)byte;
    }
    out[n] = '\0';
}

static int mod_type_tracks(const char *tag4)
{
    int i;
    for (i = 0; MOD_TYPE_TABLE[i].id; i++) {
        if (memcmp(MOD_TYPE_TABLE[i].id, tag4, 4) == 0) return MOD_TYPE_TABLE[i].tracks;
    }
    return -1;
}

/* ------------------------- converter steps ------------------------- */

static void detect_mod_and_number_of_tracks(Converter *c)
{
    char tag[4];
    int tracks;

    get_module_string(&c->mod_data, 0, 20, c->mod_name, sizeof c->mod_name);

    memcpy(tag, c->mod_data.b + 1080, 4);
    tracks = mod_type_tracks(tag);
    if (tracks >= 0) {
        /* normal tracker: up to 31 instruments */
        c->no_of_tracks = tracks;
        c->max_number_instruments = 31;
        c->ptr_instnumb = 950;
        c->mod_headlen = 1084;
        c->ptr_songp_start = 952;
        c->ptr_songp_end = 1080;
    } else {
        /* old tracker: up to 15 instruments */
        c->no_of_tracks = 4;
        c->max_number_instruments = 15;
        c->ptr_instnumb = 470;
        c->mod_headlen = 600;
        c->ptr_songp_start = 472;
        c->ptr_songp_end = 600;
    }
}

static void detect_number_of_patterns(Converter *c)
{
    int max_pat_cnt = 0;
    int pos;
    for (pos = c->ptr_songp_start; pos < c->ptr_songp_end; pos++) {
        int byte = get_mod_byte(&c->mod_data, (size_t)pos);
        if (byte > max_pat_cnt) max_pat_cnt = byte;
    }
    c->max_pat_cnt = max_pat_cnt + 1;
}

static void calc_pattern_length(Converter *c)
{
    c->length_of_one_pattern = c->no_of_tracks * NTP_NOTE_BYTES * NTP_LINES_PER_PATTERN;
    c->length_of_all_patterns = c->length_of_one_pattern * c->max_pat_cnt;
}

static void get_pattern_order(Converter *c)
{
    int length = get_mod_byte(&c->mod_data, (size_t)c->ptr_instnumb);
    int i;
    buf_reset(&c->pattern_order);
    for (i = 0; i < length; i++) {
        buf_byte(&c->pattern_order, (unsigned char)get_mod_byte(&c->mod_data, (size_t)(c->ptr_instnumb + 2 + i)));
    }
}

static void extract_mod_note(const Buf *mod, size_t ptr, int *period, int *instr, int *eff, int *val)
{
    long w0 = get_amiga_word(mod, ptr);
    long w1 = get_amiga_word(mod, ptr + 2);
    *period = (int)(w0 & 0x0fff);
    *instr  = (int)(((w0 & 0xf000) >> 8) + ((w1 & 0xf000) >> 12));
    *eff    = (int)((w1 & 0x0f00) >> 8);
    *val    = (int)(w1 & 0x00ff);
}

static void analyze_pattern(Converter *c, int pattern_number)
{
    int pattern_len = c->no_of_tracks * NTP_LINES_PER_PATTERN * NTP_NOTE_BYTES;
    int pattern_offset = c->mod_headlen + pattern_len * pattern_number;
    int line, track;

    for (line = 0; line < NTP_LINES_PER_PATTERN; line++) {
        int line_offset = line * c->no_of_tracks * NTP_NOTE_BYTES;
        for (track = 0; track < c->no_of_tracks; track++) {
            int track_offset = track * NTP_NOTE_BYTES;
            size_t ptr = (size_t)(pattern_offset + line_offset + track_offset);
            int period, mod_instrument_number, mod_effect_number, mod_effect_value;
            extract_mod_note(&c->mod_data, ptr, &period, &mod_instrument_number,
                             &mod_effect_number, &mod_effect_value);

            if (mod_instrument_number > 0) {
                intlist_add(&c->track_mod_instruments[track], mod_instrument_number);
            }

            /* effect 9 (sample offset) only works for streamed instruments */
            if (mod_effect_number == 9 && mod_effect_value != 0 &&
                !intlist_has(&c->needs_stream, mod_instrument_number)) {
                intlist_add(&c->needs_stream, mod_instrument_number);
            }
        }
    }
}

static void analyze_patterns(Converter *c)
{
    int p;
    int t;
    c->needs_stream.n = 0;
    for (t = 0; t < 32; t++) c->track_mod_instruments[t].n = 0;
    for (p = 0; p < c->max_pat_cnt; p++) analyze_pattern(c, p);
}

/* returns 1 and fills *mi if the instrument exists, else 0 */
static int extract_instrument(Converter *c, int instrument_number, size_t *total_instrument_length)
{
    size_t ptr = 20 + (size_t)(instrument_number - 1) * 30;
    long length_words, rep_ptr_words, rep_len_words;
    size_t dataptr, i;
    ModInstrument *mi;

    mi = &c->mod_instruments[instrument_number];
    memset(mi, 0, sizeof *mi);
    mi->number = instrument_number;

    get_module_string(&c->mod_data, ptr, 22, mi->name, sizeof mi->name);
    length_words    = get_amiga_word(&c->mod_data, ptr + 22);
    mi->finetune    = get_mod_byte(&c->mod_data, ptr + 24) & 15;
    mi->volume      = get_mod_byte(&c->mod_data, ptr + 25);
    rep_ptr_words   = get_amiga_word(&c->mod_data, ptr + 26);
    rep_len_words   = get_amiga_word(&c->mod_data, ptr + 28);
    mi->length          = (uint32_t)(length_words * 2);
    mi->repeat_pointer  = (uint32_t)(rep_ptr_words * 2);
    mi->repeat_length   = (uint32_t)(rep_len_words * 2);

    if (mi->length == 0) return 0; /* no instrument */

    dataptr = (size_t)(c->mod_headlen + c->length_of_all_patterns) + *total_instrument_length;
    buf_init(&mi->data);
    for (i = 0; i < mi->length; i++) {
        buf_byte(&mi->data, (unsigned char)get_mod_byte(&c->mod_data, dataptr + i));
    }

    if (c->stream_config == CONFIG_STREAM_ENFORCE ||
        (c->stream_config == CONFIG_STREAM_ALLOW && intlist_has(&c->needs_stream, instrument_number))) {
        mi->must_stream = 1;
    }

    *total_instrument_length += mi->length;
    return 1;
}

static void extract_instruments(Converter *c)
{
    size_t total_instrument_length = 0;
    int i;
    c->mod_instrument_count = 0;
    for (i = 1; i <= c->max_number_instruments; i++) {
        if (extract_instrument(c, i, &total_instrument_length)) {
            c->mod_instrument_count = i;
        }
    }
}

static void ntp_instrument_add(Converter *c, const ModInstrument *mi, int type,
                               int number, int rotate_loops)
{
    NtpInstrument *ni = (NtpInstrument *)calloc(1, sizeof *ni);
    if (!ni) { fprintf(stderr, "out of memory\n"); exit(1); }
    ntp_instrument_init(ni, mi, type, number, rotate_loops);
    c->ntp_instruments[c->ntp_instrument_count++] = ni;
}

static void convert_instruments(Converter *c, int rotate_loops)
{
    int i;
    int ntp_instrument_number = 1;

    /* drop instruments from any previous attempt */
    for (i = 0; i < c->ntp_instrument_count; i++) {
        if (c->ntp_instruments[i]) {
            buf_free(&c->ntp_instruments[i]->data);
            buf_free(&c->ntp_instruments[i]->data_original_converted);
            free(c->ntp_instruments[i]);
        }
    }
    c->ntp_instrument_count = 0;

    for (i = 1; i <= c->mod_instrument_count; i++) {
        ModInstrument *mi = &c->mod_instruments[i];
        int type;
        if (mi->length == 0) continue;

        if (!mod_must_be_streamed(mi)) {
            switch (mod_instrument_type(mi)) {
            case MODINST_TYPE_SINGLE:
                ntp_instrument_add(c, mi, NTP_TYPE_SINGLE, ntp_instrument_number, rotate_loops);
                break;
            case MODINST_TYPE_LOOPED:
                ntp_instrument_add(c, mi, NTP_TYPE_LOOPED, ntp_instrument_number, rotate_loops);
                break;
            case MODINST_TYPE_LOOPWITHHEAD:
                ntp_instrument_add(c, mi, NTP_TYPE_LOOPHEADER, ntp_instrument_number, rotate_loops);
                ntp_instrument_number++;
                ntp_instrument_add(c, mi, NTP_TYPE_LOOP, ntp_instrument_number, rotate_loops);
                break;
            }
        } else {
            type = (mod_instrument_type(mi) == MODINST_TYPE_SINGLE)
                 ? NTP_TYPE_STREAM_SINGLE : NTP_TYPE_STREAM_LOOPED;
            ntp_instrument_add(c, mi, type, ntp_instrument_number, rotate_loops);
        }
        ntp_instrument_number++;
    }
}

static int count_mod_instruments(const Converter *c)
{
    int n = 0, i;
    for (i = 1; i <= c->mod_instrument_count; i++) {
        if (c->mod_instruments[i].length > 0) n++;
    }
    return n;
}

static void check_instrument_count(Converter *c)
{
    int number_of_mod_instruments;
    int number_splitted = 0;
    int i;

    if (c->ntp_instrument_count <= NTP_MAX_INSTRUMENTS) return;

    vec_push_str(&c->instrument_errors, "Too many instruments:");
    {
        static const char msg1[] = "- Ninjatracker only allows up to 255 instruments.";
        static const char msg2[] = "- The MOD already has %d instruments.";
        static const char msg3[] = "- The MOD has %d instruments.";
        static const char msg4[] = "- Because of looped instruments, the converter creates %d additional instrument%s.";
        static const char msg5[] = "- Consider getting rid of instruments or try removing instrument loop headers.";
        char buf[256];
        vec_push_str(&c->instrument_errors, msg1);
        number_of_mod_instruments = count_mod_instruments(c);
        if (number_of_mod_instruments > NTP_MAX_INSTRUMENTS) {
            snprintf(buf, sizeof buf, msg2, number_of_mod_instruments);
        } else {
            snprintf(buf, sizeof buf, msg3, number_of_mod_instruments);
        }
        vec_push_str(&c->instrument_errors, buf);
        for (i = 0; i < c->ntp_instrument_count; i++) {
            number_splitted += ntp_is_first_splitted(c->ntp_instruments[i]) ? 1 : 0;
        }
        if (number_splitted > 0) {
            snprintf(buf, sizeof buf, msg4, number_splitted,
                     (number_splitted != 1) ? "s" : "");
            vec_push_str(&c->instrument_errors, buf);
        }
        vec_push_str(&c->instrument_errors, msg5);
    }
}

static int switch_instrument_to_stream(Converter *c)
{
    uint32_t max_length = 0;
    int instrument_index = -1;
    int i;
    for (i = 1; i <= c->mod_instrument_count; i++) {
        ModInstrument *mi = &c->mod_instruments[i];
        if (mi->length == 0) continue;
        if (!mi->must_stream && mi->length > max_length) {
            max_length = mi->length;
            instrument_index = i;
        }
    }
    if (instrument_index >= 0) {
        mod_force_stream(&c->mod_instruments[instrument_index]);
        return 1;
    }
    return 0;
}

static int optimize_instruments_sub(Converter *c, int rotate_loops)
{
    while (1) {
        int i;
        convert_instruments(c, rotate_loops);
        check_instrument_count(c);
        if (c->instrument_errors.n > 0) return 0;

        if (container_arrange(&c->container, (const NtpInstrument *const *)c->ntp_instruments,
                              c->ntp_instrument_count, c->track_mod_instruments, c->no_of_tracks)) {
            /* success; copy placement results */
            for (i = 0; i < c->no_of_tracks; i++) c->track_stream_pages[i] = c->container.track_pages[i];
            for (i = 1; i <= c->ntp_instrument_count; i++) {
                c->doc_ptrs[i] = c->container.instrument_doc_pages[i];
                c->doc_sizes[i] = c->container.instrument_doc_sizes[i];
            }
            return 1;
        }

        if (c->stream_config == CONFIG_STREAM_FORBIDDEN) {
            /* merge the container's errors into the converter's error list */
            for (i = 0; i < (int)c->container.instrument_errors.n; i++) {
                vec_push(&c->instrument_errors, c->container.instrument_errors.items[i]);
            }
            c->container.instrument_errors.n = 0;
            return 0;
        }

        /* switch the largest non-streamed instrument to streaming and retry */
        switch_instrument_to_stream(c);
    }
}

static void optimize_instruments(Converter *c)
{
    /* try to convert instruments using rotated loops */
    if (!optimize_instruments_sub(c, 1)) {
        /* retry without rotation */
        optimize_instruments_sub(c, 0);
    }
}

static int convert_period(int period)
{
    int i;
    if (period == 0) return 0;

    for (i = 0; i < 72; i++) if (MOD_NOTES[i] == period) return i + 1;

    /* find best match */
    {
        int delta = -1, bestkey = 0;
        for (i = 0; i < 72; i++) {
            int d = abs(period - MOD_NOTES[i]);
            if (delta < 0 || delta > d) { delta = d; bestkey = i; }
        }
        return bestkey + 1;
    }
}

static int add_note_to_ntp(Converter *c, int note, int inst, int effect, int value, char *err, size_t errsz)
{
    if (note > 255)   { snprintf(err, errsz, "NinjaTracker+ does not allow a note value > 255!"); return 0; }
    if (inst > NTP_MAX_INSTRUMENTS) {
        snprintf(err, errsz, "NinjaTracker+ does not allow more than %d instruments!", NTP_MAX_INSTRUMENTS);
        return 0;
    }
    if (effect > 15)  { snprintf(err, errsz, "NinjaTracker+ does not allow effect numbers > 15!"); return 0; }
    if (value > 255)  { snprintf(err, errsz, "NinjaTracker+ does not allow effect values > 255!"); return 0; }

    buf_byte(&c->ntp_notes, (unsigned char)note);
    buf_byte(&c->ntp_notes, (unsigned char)inst);
    buf_byte(&c->ntp_notes, (unsigned char)effect);
    buf_byte(&c->ntp_notes, (unsigned char)value);
    return 1;
}

static void convert_pattern(Converter *c, int pattern_number)
{
    int pattern_len = c->no_of_tracks * NTP_LINES_PER_PATTERN * NTP_NOTE_BYTES;
    int pattern_offset = c->mod_headlen + pattern_len * pattern_number;
    int line, track;
    int error_message_set = 0;

    for (line = 0; line < NTP_LINES_PER_PATTERN && !error_message_set; line++) {
        int line_offset = line * c->no_of_tracks * NTP_NOTE_BYTES;
        for (track = 0; track < c->no_of_tracks && !error_message_set; track++) {
            int track_offset = track * NTP_NOTE_BYTES;
            size_t ptr = (size_t)(pattern_offset + line_offset + track_offset);
            int period, mod_instrument_number, mod_effect_number, mod_effect_value;
            char err[160];
            int ok = 0;

            extract_mod_note(&c->mod_data, ptr, &period, &mod_instrument_number,
                             &mod_effect_number, &mod_effect_value);

            if (mod_instrument_number != 0 && c->instrument_map[mod_instrument_number] == 0) {
                snprintf(err, sizeof err, "Instrument %d not found!", mod_instrument_number);
            } else {
                int gs_note = convert_period(period);
                int gs_instrument = (mod_instrument_number == 0)
                                  ? 0 : c->instrument_map[mod_instrument_number];
                ok = add_note_to_ntp(c, gs_note, gs_instrument,
                                     mod_effect_number, mod_effect_value, err, sizeof err);
            }

            if (!ok) {
                char full[360];
                snprintf(full, sizeof full, "Error in pattern %02d, line %02d, track %02d: %s",
                         pattern_number, line, track + 1, err);
                vec_push_str(&c->pattern_errors, full);
                error_message_set = 1;
            }
        }
    }
}

static void convert_patterns(Converter *c)
{
    int i, p;
    memset(c->instrument_map, 0, sizeof c->instrument_map);
    for (i = 0; i < c->ntp_instrument_count; i++) {
        NtpInstrument *ni = c->ntp_instruments[i];
        if (c->instrument_map[ni->number_original] == 0) {
            /* save the 1st instrument; it is the loop header */
            c->instrument_map[ni->number_original] = ni->number;
        }
    }
    for (p = 0; p < c->max_pat_cnt; p++) convert_pattern(c, p);
}

static void too_many_osc_message(Converter *c, const int *track_osc_count, Buf *msg)
{
    int total = 0;
    int track, i, k;

    for (track = 1; track <= c->no_of_tracks; track++) total += track_osc_count[track];

    buf_str(msg, "The tracks require too many oscillators:\n");
    for (track = 1; track <= c->no_of_tracks; track++) {
        IntList *l = &c->track_mod_instruments[track - 1];
        buf_printf(msg, "Track %2d: %d oscillator%s, instrument%s: ",
                   track, track_osc_count[track],
                   (track_osc_count[track] == 1) ? "" : "s",
                   (l->n == 1) ? "" : "s");
        for (k = 0; k < l->n; k++) {
            if (k) buf_str(msg, ", ");
            buf_printf(msg, "%d", l->v[k]);
        }
        buf_str(msg, "\n");
    }
    buf_printf(msg, "Total: %d (31 allowed).\n\n", total);

    buf_str(msg, "Module instrument oscillator requirements:\n");
    for (i = 0; i < c->ntp_instrument_count; i++) {
        int original = c->ntp_instruments[i]->number_original;
        int count = ntp_get_osc_count(c->ntp_instruments[i]);
        int already = 0;
        int j;
        for (j = 0; j < i; j++) {
            if (c->ntp_instruments[j]->number_original == original) { already = 1; break; }
        }
        if (!already) {
            buf_printf(msg, "Instrument %3d: %d oscillator%s\n",
                       original, count, (count == 1) ? "" : "s");
        }
    }
}

static void determine_osc_per_track(Converter *c)
{
    int track_osc_count[32] = { 0 };
    int total = 0;
    int oscs[32];
    int track, t, i, k;

    for (track = 1; track <= c->no_of_tracks; track++) track_osc_count[track] = 1;

    for (t = 0; t < c->no_of_tracks; t++) {
        int track = t + 1;
        for (k = 0; k < c->track_mod_instruments[t].n; k++) {
            int mod_instrument_number = c->track_mod_instruments[t].v[k];
            for (i = 0; i < c->ntp_instrument_count; i++) {
                if (c->ntp_instruments[i]->number_original == mod_instrument_number) {
                    int cnt = ntp_get_osc_count(c->ntp_instruments[i]);
                    if (cnt > track_osc_count[track]) track_osc_count[track] = cnt;
                }
            }
        }
    }

    for (track = 1; track <= c->no_of_tracks; track++) total += track_osc_count[track];
    if (total > 31) {
        Buf msg;
        buf_init(&msg);
        too_many_osc_message(c, track_osc_count, &msg);
        buf_byte(&msg, 0);
        vec_push_str(&c->track_errors, (const char *)msg.b);
        buf_free(&msg);
        return;
    }

    for (i = 0; i < 31; i++) oscs[i] = 0;
    oscs[31] = -1; /* oscillator 31 is the music timer */

    for (track = 1; track <= c->no_of_tracks; track++) {
        int osc_needed = track_osc_count[track];
        for (i = 0; i < 32; i += osc_needed) {
            if (osc_needed == 1 && oscs[i] == 0) {
                oscs[i] = track;
                c->track_oscillator_byte[track] = i;
                break;
            } else if (osc_needed == 2 && oscs[i] == 0 && oscs[i + 1] == 0) {
                oscs[i] = track;
                oscs[i + 1] = track;
                c->track_oscillator_byte[track] = 128 + i;
                break;
            }
        }
    }

    memcpy(c->oscillator_usage, oscs, sizeof oscs);
}

/* ------------------------- NTP file output ------------------------- */

static void length_to_gs_str(Buf *out, uint32_t word)
{
    buf_byte(out, (unsigned char)(word & 0xff));
    buf_byte(out, (unsigned char)((word >> 8) & 0xff));
    buf_byte(out, (unsigned char)((word >> 16) & 0xff));
}

static void save_ntp(Converter *c)
{
    Buf out;
    int i, t;

    buf_init(&out);

    /* identifier "nfc!" + version 2 */
    buf_mem(&out, "nfc!", 4);
    buf_byte(&out, 2);
    buf_byte(&out, (unsigned char)c->no_of_tracks);
    buf_byte(&out, (unsigned char)c->ntp_instrument_count);
    buf_byte(&out, (unsigned char)c->max_pat_cnt);
    buf_byte(&out, (unsigned char)c->pattern_order.n);

    /* song name */
    buf_byte(&out, (unsigned char)strlen(c->mod_name));
    buf_mem(&out, c->mod_name, strlen(c->mod_name));

    /* track data: 3 bytes per track */
    for (t = 1; t <= c->no_of_tracks; t++) {
        buf_byte(&out, (unsigned char)doc_stereo_default(t - 1));
        buf_byte(&out, (unsigned char)c->track_stream_pages[t - 1]);
        buf_byte(&out, (unsigned char)c->track_oscillator_byte[t]);
    }

    /* instrument data */
    for (i = 0; i < c->ntp_instrument_count; i++) {
        NtpInstrument *ni = c->ntp_instruments[i];
        buf_byte(&out, (unsigned char)ntp_get_type_for_file(ni));
        length_to_gs_str(&out, ni->length);
        length_to_gs_str(&out, ni->repeat_pointer);
        length_to_gs_str(&out, ni->repeat_length);
        buf_byte(&out, (unsigned char)ni->volume);
        buf_byte(&out, (unsigned char)(ni->finetune & 0xff));
        buf_byte(&out, (unsigned char)c->doc_ptrs[ni->number]);
        buf_byte(&out, (unsigned char)c->doc_sizes[ni->number]);
        buf_byte(&out, (unsigned char)strlen(ni->name));
        buf_mem(&out, ni->name, strlen(ni->name));
    }

    /* pattern order */
    for (i = 0; i < (int)c->pattern_order.n; i++) buf_byte(&out, buf_get(&c->pattern_order, (size_t)i));

    /* pattern data */
    for (i = 0; i < (int)c->ntp_notes.n; i++) buf_byte(&out, buf_get(&c->ntp_notes, (size_t)i));

    /* sample data */
    memset(c->instrument_positions, 0, sizeof c->instrument_positions);
    for (i = 0; i < c->ntp_instrument_count; i++) {
        NtpInstrument *ni = c->ntp_instruments[i];
        c->instrument_positions[ni->number] = (int)out.n;
        buf_mem(&out, ni->data.b, ni->data.n);
    }

    {
        FILE *fp = fopen(c->path_output_ntp, "wb");
        if (!fp) {
            fprintf(stderr, "Unable to open output file %s\n", c->path_output_ntp);
            exit(1);
        }
        fwrite(out.b, 1, out.n, fp);
        fclose(fp);
    }

    buf_free(&out);
}/* ------------------------- conversion info report ------------------------- */

static void append_binary(Buf *msg, unsigned int value, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        buf_byte(msg, (unsigned char)('0' + ((value >> i) & 1)));
    }
}

static void append_instrument_info(Converter *c, Buf *msg)
{
    int i;
    buf_str(msg, "Module name: ");
    buf_str(msg, c->mod_name);
    buf_str(msg, "\n");
    buf_printf(msg, "Number of tracks: %d\n", c->no_of_tracks);
    buf_printf(msg, "Number of patterns: %d\n", c->max_pat_cnt);
    buf_printf(msg, "Number of pattern positions: %d\n", (int)c->pattern_order.n);
    buf_str(msg, "\n");

    /* instrument oscillator requirements */
    buf_str(msg, "Module instrument oscillator requirements:\n");
    for (i = 0; i < c->ntp_instrument_count; i++) {
        int original = c->ntp_instruments[i]->number_original;
        int count = ntp_get_osc_count(c->ntp_instruments[i]);
        int already = 0;
        int j;
        for (j = 0; j < i; j++) {
            if (c->ntp_instruments[j]->number_original == original) { already = 1; break; }
        }
        if (!already) {
            buf_printf(msg, "Instrument %3d: %d oscillator%s\n",
                       original, count, (count == 1) ? "" : "s");
        }
    }
    buf_str(msg, "\n");

    buf_str(msg, "NTP Instruments:\n");
    for (i = 0; i < c->ntp_instrument_count; i++) {
        NtpInstrument *ni = c->ntp_instruments[i];
        buf_printf(msg, "number=%3d", ni->number);
        buf_printf(msg, ", modnumber=%3d", ni->number_original);
        buf_str(msg, ", type=%");
        append_binary(msg, (unsigned int)ntp_get_type_for_file(ni), 4);
        buf_str(msg, ", streamed=");
        buf_str(msg, ntp_is_streamed(ni) ? "yes" : "no ");
        buf_printf(msg, ", length=%5u", ni->length);
        buf_printf(msg, ", volume=%2d", ni->volume);
        buf_printf(msg, ", finetune=%2d", ni->finetune);
        buf_printf(msg, ", docptr=$%04X", c->doc_ptrs[ni->number] * 256);
        buf_str(msg, ", docsiz=%");
        append_binary(msg, (unsigned int)c->doc_sizes[ni->number], 6);
        buf_printf(msg, ", pos=$%06X", c->instrument_positions[ni->number]);
        buf_str(msg, "\n");
    }

    buf_str(msg, "\n");
    buf_str(msg, "modnumber - Instrument number in the original module\n");
    buf_str(msg, "type - Bitmap field, see file format in source code archive\n");
    buf_str(msg, "docptr - Pointer to which page in sound ram the instrument is placed (streamed samples have a 0 here).\n");
    buf_str(msg, "docsiz - Bitmap field, see Apple IIGS hardware reference.\n");
    buf_str(msg, "pos - Position in NTP file where the instrument is stored at.\n");
    buf_str(msg, "\n");

    /* DOC ram usage */
    buf_str(msg, "DOC ram usage:");
    {
        int i2;
        for (i2 = 0; i2 < 256; i2++) {
            if (i2 % 16 == 0) {
                buf_str(msg, "\n");
                buf_printf(msg, "$%04X ", i2 * 256);
            }
            buf_printf(msg, "%d", c->container.doc_pages[i2]);
        }
    }
    buf_str(msg, "\n");
    buf_str(msg, "0=free, 1=instrument, 2=stream buffer, 3=interrupt timer\n");
    buf_str(msg, "\n");

    /* oscillator usage */
    buf_str(msg, "Oscillator usage:\n");
    {
        int osc;
        for (osc = 0; osc < 32; osc++) {
            buf_printf(msg, "Oscillator %02d: ", osc);
            if (c->oscillator_usage[osc] == 0) {
                buf_str(msg, "free");
            } else if (c->oscillator_usage[osc] == -1) {
                buf_str(msg, "Music timer");
            } else {
                buf_printf(msg, "Track %d", c->oscillator_usage[osc]);
            }
            buf_str(msg, "\n");
        }
    }
}

static void print_errors(Converter *c)
{
    size_t i;
    fprintf(stderr, "UNABLE TO CONVERT:\n");

    for (i = 0; i < c->instrument_errors.n; i++) fprintf(stderr, "  %s\n", (const char *)c->instrument_errors.items[i]);
    for (i = 0; i < c->track_errors.n; i++) fprintf(stderr, "  %s\n", (const char *)c->track_errors.items[i]);
    for (i = 0; i < c->pattern_errors.n && i < 10; i++) fprintf(stderr, "  %s\n", (const char *)c->pattern_errors.items[i]);
    if (c->pattern_errors.n > 10) fprintf(stderr, "  ... (%zu more pattern errors)\n", c->pattern_errors.n - 10);
}

/* ------------------------- main / conversion flow ------------------------- */

static int load_mod(Converter *c, const char *path)
{
    FILE *fp;
    long size;
    size_t rd;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Unable to find MOD file %s\n", path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); return 0; }

    buf_reset(&c->mod_data);
    buf_grow(&c->mod_data, (size_t)size);
    rd = fread(c->mod_data.b, 1, (size_t)size, fp);
    c->mod_data.n = rd;
    fclose(fp);
    return 1;
}

static void cleanup(Converter *c)
{
    int i;
    buf_free(&c->mod_data);
    buf_free(&c->pattern_order);
    buf_free(&c->ntp_notes);
    for (i = 0; i < c->ntp_instrument_count; i++) {
        if (c->ntp_instruments[i]) {
            buf_free(&c->ntp_instruments[i]->data);
            buf_free(&c->ntp_instruments[i]->data_original_converted);
            free(c->ntp_instruments[i]);
        }
    }
    for (i = 1; i <= c->mod_instrument_count; i++) buf_free(&c->mod_instruments[i].data);
    vec_free(&c->instrument_errors);
    vec_free(&c->pattern_errors);
    vec_free(&c->track_errors);
    vec_free(&c->container.instrument_errors);
}

static void usage(void)
{
    printf("ntpconverter by ninjaforce (C port)\n\n");
    printf("  help: `ntpconverter mysong.MOD [stream_config] [output]`\n\n");
    printf("  stream_config is optional and is one of:\n");
    printf("    [STREAM_FORBIDDEN, STREAM_ALLOW, STREAM_ENFORCE]\n\n");
    printf("  output is an optional path for the resulting .ntp file\n");
}

int main(int argc, char **argv)
{
    Converter c;
    const char *mod_arg;
    char output_base[1200];
    Buf report;

    if (argc < 2) {
        usage();
        return 1;
    }
    mod_arg = argv[1];
    if (strcmp(mod_arg, "-h") == 0 || strcmp(mod_arg, "--help") == 0) {
        usage();
        return 0;
    }

    memset(&c, 0, sizeof c);
    buf_init(&c.mod_data);
    buf_init(&c.pattern_order);
    buf_init(&c.ntp_notes);
    vec_init(&c.instrument_errors);
    vec_init(&c.pattern_errors);
    vec_init(&c.track_errors);
    vec_init(&c.container.instrument_errors);

    c.stream_config = CONFIG_STREAM_ALLOW;
    if (argc >= 3) {
        if (strcasecmp(argv[2], "STREAM_FORBIDDEN") == 0) c.stream_config = CONFIG_STREAM_FORBIDDEN;
        else if (strcasecmp(argv[2], "STREAM_ALLOW") == 0) c.stream_config = CONFIG_STREAM_ALLOW;
        else if (strcasecmp(argv[2], "STREAM_ENFORCE") == 0) c.stream_config = CONFIG_STREAM_ENFORCE;
    }

    /* derive the output path: optional explicit path in argv[3], otherwise
       the module name with a trailing ".MOD" (case-insensitive) replaced
       by ".ntp", or ".ntp" appended if there is no such suffix; a leading
       "mod." filename prefix is dropped (mod.song -> song.ntp). */
    if (argc >= 4) {
        c.path_output_ntp = argv[3];
    } else {
        size_t mod_len = strlen(mod_arg);
        snprintf(output_base, sizeof output_base, "%s", mod_arg);
        if (mod_len >= 4 && strcasecmp(mod_arg + mod_len - 4, ".mod") == 0) {
            output_base[mod_len - 4] = '\0';
        } else if (mod_len >= 4 && strcasecmp(mod_arg + mod_len - 4, ".ntp") == 0) {
            /* already has .ntp, leave as-is to keep name stable */
        }
        {
            const char *slash = strrchr(output_base, '/');
            char *name = (char *)(slash ? slash + 1 : output_base);
            if (strncmp(name, "mod.", 4) == 0) {
                memmove(name, name + 4, strlen(name + 4) + 1);
            }
        }
        strncat(output_base, ".ntp", sizeof output_base - strlen(output_base) - 1);
        c.path_output_ntp = output_base;
    }

    if (!load_mod(&c, mod_arg)) {
        cleanup(&c);
        return 255;
    }

    detect_mod_and_number_of_tracks(&c);
    detect_number_of_patterns(&c);
    calc_pattern_length(&c);
    get_pattern_order(&c);

    analyze_patterns(&c);
    extract_instruments(&c);
    optimize_instruments(&c);
    convert_patterns(&c);
    determine_osc_per_track(&c);

    if (c.instrument_errors.n || c.pattern_errors.n || c.track_errors.n) {
        print_errors(&c);
        cleanup(&c);
        return 255;
    }

    save_ntp(&c);

    buf_init(&report);
    append_instrument_info(&c, &report);
    buf_str(&report, "\n");
    fwrite(report.b, 1, report.n, stdout);
    buf_free(&report);

    cleanup(&c);
    return 0;
}