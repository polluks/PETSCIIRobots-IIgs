/* shr.h - Apple IIgs Super Hi-Res loader (shadowed memory) public API. */

#ifndef SHR_H
#define SHR_H

typedef unsigned char  u8;
typedef unsigned int   u16;

/* Prepare shadowed memory for SHR rendering. */
void shr_enable_shadowing(void);
void shr_disable_shadowing(void);

/* Wait for the vertical blanking interval. */
void shr_wait_vbl(void);

/* Fill the scan-line control bytes with 320-mode / palette 0. */
void shr_set_scb_all_palette0(void);

/* Load pixel data and a 16-color palette into the shadowed back buffer. */
void shr_load_pixels_palette(const u8 *pixels, unsigned pixelCount,
                             const u16 *palette, unsigned colorCount);

/* Refresh the display from the back buffer via the shadowed fast copy. */
void shr_show(void);

#endif
