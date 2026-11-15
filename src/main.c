/*
 * Twoegram -- a client for OpenClaw agents on the Enhanced Apple //e,
 * with the network done by a CoGS card.
 */
#include <apple2.h>
#include <stdint.h>
#include "screen.h"
#include "kbd.h"
#include "ui.h"
#include "link.h"
#include "session.h"
#include "store.h"

/* Before the 80-column screen is known to exist: a message on the
 * 40-column screen, written straight to page 1. */
static void fail40(const char *msg)
{
    uint8_t *p = (uint8_t *)0x0400;

    while (*msg) {
        *p++ = (uint8_t)(*msg++ | 0x80);
    }
    key_get();
}

int main(void)
{
    uint8_t os = get_ostype();
    const char *why;

    if (os < APPLE_IIE) {
        fail40("TWOEGRAM NEEDS AN APPLE //E");
        return 1;
    }
    has_mousetext = (uint8_t)(os != APPLE_IIE);

    scr_init();
    splash_show();
    why = st_init();               /* 128K, and aux memory for messages */
    if (why) {
        status_msg(why);
        key_get();
        scr_done();
        return 1;
    }
    net_check();                /* while the splash is up */
    status_msg("PRESS ANY KEY");
    key_get();                  /* consumed: it must not act on the screen */

    link_start();
    convo_run();

    ses_forget();               /* no key left in memory after quitting */
    scr_done();
    return 0;
}
