/* main.c
   Attack of the PETSCII Robots - Apple IIgs intro screen loader.

   Displays introscreen.png on the Apple IIgs Super Hi-Res screen using
   shadowed memory for a fast refresh (see shr.c / shr.s).

   Build with vbcc for the 65816 backend plus the IIgs system headers.
*/

#include "shr.h"
#include "image_data.h"

extern void StartUp(void);   /* GS/OS startup stub if used */

void ntp_music_start(void);   /* NTPMUSIC.s: load NTP player + title song */
void ntp_music_stop(void);    /* NTPMUSIC.s: shut the music down */

int main(void)
{
    /* The image was pre-converted to 320x200, 16 colors, and stored as
       screen_data[] (pixel bytes, 2 px/byte) and palette_data[] (IIgs $0RGB
       color words).  See convert_png.py.
       screen_data holds only the 32000 pixel bytes; SCB and palette are
       written separately by the loader.  */

    shr_enable_shadowing();

    /* Load the pixel data and the 16-color IIgs palette into the fast back
       buffer, then mirror everything to the SHR display at next VBL. */
    shr_load_pixels_palette(
        screen_data, 32000,            /* 320 x 200, 2 px per byte */
        palette_data, 16);             /* 16 palette entries */

    shr_show();

    /* Start the NinjaTrackerPlus music (loads NTPPLAYER + TITLE.NTP from
   /PETSCIIROBOTS and plays the song through the DOC sound interrupt). */
    ntp_music_start();

    /* Hold the title screen until a key is pressed (simplified). */
    while ((*(volatile unsigned char *)0x00E0C000L & 0x80) == 0)
        ;

    ntp_music_stop();

    return 0;
}
