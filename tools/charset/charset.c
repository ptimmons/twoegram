/*
 * CHARSET.SYSTEM: every screen code, for tools/render.py to cut glyphs
 * from a Virtual ][ screenshot (tools/fontgrab.py). Codes 0-255 sit 64 to
 * a row, on rows 0, 2, 4 and 6, from column 8; row 8 says it is ready.
 * Any key quits.
 */
#include <stdint.h>
#include "screen.h"

int main(void)
{
    uint8_t r, c;

    scr_init();
    has_mousetext = 1;
    for (r = 0; r < 4; ++r) {
        lb_clear(SC_SPACE);
        for (c = 0; c < 64; ++c) {
            linebuf[8 + c] = (uint8_t)(r * 64 + c);
        }
        scr_blit((uint8_t)(r * 2));
    }
    lb_clear(SC_SPACE);
    lb_text(8, "CHARSET READY", 0);
    scr_blit(8);
    while (!(*(volatile uint8_t *)0xC000 & 0x80)) {
    }
    *(volatile uint8_t *)0xC010 = 0;
    scr_done();
    return 0;
}
