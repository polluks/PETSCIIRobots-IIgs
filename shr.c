/* shr.c
   Apple IIgs Super Hi-Res (SHR) intro-screen loader using shadowed memory.

   The image is rendered into a fast back buffer in bank $01, then the buffer
   is mirrored onto the SHR display in bank $E1 via shadowed memory (see the
   assembly routine shr.s, and the technique described at
   https://retrocomputing.stackexchange.com/questions/52).

   Layout of the 32 KB SHR back buffer (offset within bank $01):
     +$2000  32000 bytes   pixel data (320 x 200, 2 pixels per byte)
     +$9D00    200 bytes   scan-line control bytes (SCB)
     +$9E00    512 bytes   color palettes (16 x 16 x 2 bytes)

   The screen is shown in 320-mode with a single 16-color palette (palette 0),
   so every SCB entry is 0 and the whole image uses palette 0.

   Target: vbcc for the 65816 backend (IIgs flavour).  `__far` gives 24-bit
   pointers for addressing bank $01 / bank $E1.
*/

typedef unsigned char  u8;
typedef unsigned int   u16;

/* Far pointers into bank $01 (the shadowed back buffer). */
/* $01:[$2000..$9FFF] -> 24-bit address $01_2000 */
#define BACKBUF_ADDR   0x012000UL

/* Offsets of the SHR fields within the bank. */
#define PIX_OFF        0x0000   /* + backbuf, pixel data 32000 bytes   */
#define SCB_OFF        0x7D00   /* + backbuf, scan-line control bytes  */
#define PAL_OFF        0x7E00   /* + backbuf, palette (16 x 16 x 2)    */

#define SHR_SIZE       32768    /* pixels + SCB + palette (32 KB)      */
#define SHADOW_REG     0x00C035L

/* Assembly primitive that mirrors [BACKBUF+src, +src+count) onto the screen
   (bank $E1) via shadowed memory (MVN self-copy in bank $01). */
extern void shr_slam(unsigned src, unsigned count);

/* Far base pointer to the back buffer in bank $01. */
static u8 __far *backbuf = (u8 __far *)BACKBUF_ADDR;

void shr_wait_vbl(void)
{
    /* Bit 7 of $E0C029 is set while the vertical blanking occurs. */
    while (!((*(volatile u8 *)(u8 __far *)0x00E0C029L) & 0x80))
        ;
}

void shr_enable_shadowing(void)
{
    /* Clear bit 3 of the SHADOW register to enable SHR shadowing.
       Do a read/modify/write to avoid disturbing other bits. */
    u8 v = *(volatile u8 __far *)SHADOW_REG;
    v &= ~0x08u;
    *(volatile u8 __far *)SHADOW_REG = v;
}

void shr_disable_shadowing(void)
{
    u8 v = *(volatile u8 __far *)SHADOW_REG;
    v |= 0x08u;
    *(volatile u8 __far *)SHADOW_REG = v;
}

void shr_set_scb_all_palette0(void)
{
    /* 320-mode (SCB = 0) using palette 0 for all 200 scan lines. */
    unsigned i;
    u8 __far *scb = backbuf + SCB_OFF;
    for (i = 0; i < 200; i++)
        scb[i] = 0x00;
}

void shr_load_pixels_palette(const u8 *pixels, unsigned pixelCount,
                             const u16 *palette, unsigned colorCount)
{
    unsigned i;
    u8 __far *dst;

    /* Copy the pixel data into the back buffer. */
    dst = backbuf + PIX_OFF;
    for (i = 0; i < pixelCount; i++)
        dst[i] = pixels[i];

    /* Copy the palette into palette 0. */
    dst = backbuf + PAL_OFF;
    for (i = 0; i < colorCount; i++)
    {
        u16 c = palette[i];
        dst[i * 2 + 0] = (u8)(c & 0xFF);
        dst[i * 2 + 1] = (u8)(c >> 8);
    }

    shr_set_scb_all_palette0();
}

void shr_show(void)
{
    /* Mirror the whole 32 KB back buffer (pixels + SCB + palette) onto the
       display in a single fast shadowed copy. */
    shr_wait_vbl();                     /* avoid visible tearing */
    shr_slam(PIX_OFF, SHR_SIZE);
}
