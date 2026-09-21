; NTPMUSIC.s - NinjaTrackerPlus (NTP) music driver for the Apple IIgs intro
;
; Loads the NTP player (disk file NTPPLAYER) into bank $0F and the title
; song (disk file TITLE.NTP) into $100000, then starts playback from the
; DOC sound interrupt (NTP engine by Jesse Blue / Ninjaforce).
;
; Called from C (vbcc, +iigs target, native 65816, MX=%00):
;     void ntp_music_start(void);
;     void ntp_music_stop(void);
;
; Disk I/O goes through the ProDOS 8 MLI ($BF00, emulation mode), which is
; available both under ProDOS 8 and under GS/OS. Reads use an 8K staging
; buffer in bank 0; the data is then copied to its final 24-bit destination
; (player @ $0F0000, song @ $100000).
;
; NOTE: all symbol-address references must follow the vbcc/vlink convention
;      lda #<sym  /  ldx #^sym   (low word / bank byte)
; i.e. never  #>sym, or the appleomf link target refuses the relocation.
;
; Requires a IIgs with 2 MB or more of RAM (banks $0F / $10 present).

; ---------------- constants ----------------
P8_OPEN         = $C8           ; ProDOS 8 MLI: OPEN
P8_READ         = $CA           ; ProDOS 8 MLI: READ
P8_CLOSE        = $CC           ; ProDOS 8 MLI: CLOSE
MLI             = $BF00         ; ProDOS 8 MLI (interpreter) entry

NTP_PREPARE     = $0F0000       ; player entry points (player org $0F0000)
NTP_PLAY        = $0F0003
NTP_STOP        = $0F0006
NTP_SETVOL      = $0F0015
NTP_VOL         = 180           ; playback volume cap

SONG_BASE       = $100000       ; title song load address
PLAYER_BASE     = $0F0000       ; NTP player load address
READ_CHUNK      = $2000         ; bytes per MLI READ (staging buffer size)

; zero-page usage (saved/restored by the wrapper; D forced to 0)
Z_PRM           = $D0           ; base of the MLI parameter list at $00D0
Z_DST           = $DC           ; 3 bytes: 24-bit copy destination base
Z_SRC           = $DF           ; 3 bytes: 24-bit copy source base
Z_OFF           = $E2           ; word: running destination offset
Z_BANK          = $E4           ; byte: running destination bank
Z_PAD1          = $E5           ; spare byte (never touched)
Z_REF           = $E6           ; word: OPEN reference number
Z_PATH          = $E8           ; word: pathname pointer (bank 0)
Z_COUNT         = $EA           ; word: bytes returned by last READ
Z_ERR           = $EC           ; word: saved error result
Z_PAD2          = $EE           ; spare bytes (part of the saved range)

        global _ntp_music_start
        global _ntp_music_stop

        section "DONTMERGE_text.far.ntpmusic.0","acrx"

; ---------------------------------------------------------------------------
_ntp_music_start:
        a16
        x16
        tdc
        pha                     ; save D register
        lda #0
        tcd                     ; D = 0  (crisp zero-page addressing)
        ldx #$00
zpsv    lda $D0,x               ; save ZP scratch $D0-$EF (words) on native stack
        pha
        inx
        inx
        cpx #$20
        bne zpsv

        ; base pointer for the MLI parameter list ($00D0, bank 0)
        lda #$00D0
        sta Z_PRM
        stz Z_PRM+2             ; zero filler (harmless)

        ; ---- load the player into bank $0F -----------------------------
        lda #<ntp_path_player
        sta Z_PATH
        stz Z_OFF
        a8
        lda #^PLAYER_BASE
        sta Z_BANK
        a16
        jsr ntp_load_file
        bne ntp_done            ; A != 0 -> error, skip music

        ; ---- load the title song into $100000 --------------------------
        lda #<ntp_path_title
        sta Z_PATH
        stz Z_OFF
        a8
        lda #^SONG_BASE
        sta Z_BANK
        a16
        jsr ntp_load_file
        bne ntp_done

        ; ---- NTPprepare(song_ptr, doubling=0) ---------------------------
        ldx #<SONG_BASE
        ldy #^SONG_BASE
        lda #0
        jsl NTP_PREPARE
        bcc ntp_ok
        bra ntp_done            ; carry set -> prepare failed, error in A

ntp_ok:
        lda #NTP_VOL            ; cap the playback volume
        jsl NTP_SETVOL
        lda #0                  ; 0 = loop the song forever
        jsl NTP_PLAY
        cli                     ; let the DOC sound interrupt run
        lda #0

ntp_done:
        ; restore ZP scratch and D register
        ldx #$1E
zprs    pla
        sta $D0,x
        dex
        dex
        bmi drest
        bra zprs
drest   pla
        tcd
        rtl

; ---------------------------------------------------------------------------
_ntp_music_stop:
        a16
        jsl NTP_STOP            ; shuts down the sound interrupt + oscillators
        rtl

; ---------------------------------------------------------------------------
; Loads a file into banks beyond main memory using MLI READ + a bank-0
; staging buffer.  Uses: Z_PATH (pathname), Z_OFF / Z_BANK (destination).
; Returns A16 = 0 on success, else the MLI error code.
ntp_load_file:
        a16
        stz Z_ERR
        ; -------------------------- OPEN --------------------------------
        a8
        lda #3
        sta Z_PRM                ; pcount
        lda Z_PATH
        sta Z_PRM+1              ; pathname 16-bit pointer (bank 0)
        lda Z_PATH+1
        sta Z_PRM+2
        stz Z_PRM+3              ; io buffer number = 0
        jsr mli_open
        a16
        bne ntp_lf_err           ; error code in A
        lda Z_PRM+4              ; refnum (result appended by MLI)
        sta Z_REF

        ; -------------------------- READ loop ---------------------------
rdlp    a8
        lda #4
        sta Z_PRM                ; pcount
        lda Z_REF
        sta Z_PRM+1
        lda Z_REF+1
        sta Z_PRM+2
        lda #<ntp_buf
        sta Z_PRM+3
        lda #^ntp_buf
        sta Z_PRM+4              ; buffer bank = 0
        stz Z_PRM+5
        lda #<READ_CHUNK
        sta Z_PRM+6
        lda #>READ_CHUNK
        sta Z_PRM+7
        a16
        jsr mli_read
        bne ntp_lf_readerr       ; error code in A
        lda Z_PRM+8              ; transfer count (word result)
        tay
        beq ntp_lf_eof           ; 0 bytes -> end of file
        sta Z_COUNT
        jsr ntp_copy             ; copy transfer bytes to destination
        a16
        lda Z_OFF
        clc
        adc Z_COUNT
        sta Z_OFF
        bcc nobank
        inc Z_BANK
        a16
nobank  lda Z_COUNT
        cmp #READ_CHUNK
        bcc ntp_lf_eof           ; short read -> EOF
        bra rdlp

ntp_lf_readerr:
        sta Z_ERR                ; remember the error

        ; -------------------------- CLOSE -------------------------------
ntp_lf_eof:
        a8
        lda #1
        sta Z_PRM                ; pcount
        lda Z_REF
        sta Z_PRM+1
        lda Z_REF+1
        sta Z_PRM+2
        jsr mli_close            ; ignore any close error
        a8
        lda Z_ERR
        a16
        and #$00FF               ; error (or 0) -> A16
        rts

ntp_lf_err:
        rts                      ; A already holds the error (16-bit)

; ---------------------------------------------------------------------------
; Copies Z_COUNT bytes from the staging buffer (bank 0) to Z_BANK:Z_OFF.
ntp_copy:
        a16
        lda #<ntp_buf
        sta Z_SRC
        a8
        lda #^ntp_buf
        sta Z_SRC+2              ; source bank (byte)
        lda Z_BANK
        sta Z_DST+2              ; destination bank (byte)
        a16
        lda Z_OFF
        sta Z_DST
        lda Z_COUNT
        lsr                      ; bytes -> 16-bit words
        tay
        dey
cpntr   lda [Z_SRC],y
        sta [Z_DST],y
        dey
        bpl cpntr
        rts

; ------------------------- MLI dispatchers -------------------------------
; Each returns in native 16-bit mode with A16 = 0 (ok) or the error code.
mli_open:
        sep #$30
        sec
        xce                     ; -> emulation mode (MLI is 8-bit code)
        jsr MLI
        dfb P8_OPEN
        da $00D0
        bcc mli_o_ok
        clc
        xce                     ; -> native
        rep #$30
        and #$00FF
        rts
mli_o_ok:
        clc
        xce
        rep #$30
        lda #0
        rts

mli_read:
        sep #$30
        sec
        xce
        jsr MLI
        dfb P8_READ
        da $00D0
        bcc mli_r_ok
        clc
        xce
        rep #$30
        and #$00FF
        rts
mli_r_ok:
        clc
        xce
        rep #$30
        lda #0
        rts

mli_close:
        sep #$30
        sec
        xce
        jsr MLI
        dfb P8_CLOSE
        da $00D0
        bcc mli_c_ok
        clc
        xce
        rep #$30
        and #$00FF
        rts
mli_c_ok:
        clc
        xce
        rep #$30
        lda #0
        rts

ntp_path_player:
        byte 24                 ; length-prefixed ProDOS pathname
        byte "/PETSCIIROBOTS/NTPPLAYER"
ntp_path_title:
        byte 24
        byte "/PETSCIIROBOTS/TITLE.NTP"

        ; ----------------------------------------------------------------
        section "DONTMERGE_bss.near.ntpbuf.0","aurw"
ntp_buf:
        reserve READ_CHUNK       ; 8K staging buffer, bank 0