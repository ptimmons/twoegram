/*
 * Adding a gateway, Settings, and saving, as overlays.
 *
 * Main memory is full, and adding a gateway (setup.c, segment OVERLAY1)
 * happens only at the first start, from Settings, and when the gateway
 * does not know the key. So its code and text are not part of
 * TWOEGRAM.SYSTEM: they are the file TWOEGRAM.SETUP, loaded at $0800
 * each time it opens, over buffers that are idle then and rebuilt
 * afterwards: the layout table, the text buffer and the reply buffer
 * (cfg/twoegram.cfg; memcheck.py checks it stops short of ses_labels,
 * which must survive). Settings (prefs.c, segment OVERLAY3, Open-Apple-K)
 * is TWOEGRAM.PREFS, loaded the same way.
 *
 * Saving the conversation (save.h, segment OVERLAY2) is the second:
 * TWOEGRAM.SAVE, loaded at $0800 too, but over the layout table and
 * the text buffer only, since it fetches and so needs the reply buffer.
 *
 * Never loaded during a request (the network disk is the card too).
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include "port.h"

#define SETUP_FILE  "TWOEGRAM.SETUP"
#define PREFS_FILE  "TWOEGRAM.PREFS"
#define SAVE_FILE   "TWOEGRAM.SAVE"

/* Adding a gateway: load it, then run it. at_key: open at the key for
 * the gateway in use (it has a name), as when the gateway does not know
 * it. why, if not 0, is said at the foot. 1 if a gateway was saved or
 * its key added (connect to it), 0 if cancelled with nothing changed or
 * it could not be loaded (said). */
uint8_t setup_run(uint8_t at_key, const char *why);

/* setup.c, in the overlay. */
uint8_t setup_main(uint8_t at_key, const char *why);

/* Settings: load it, then run it. What to do next: */
#define PREFS_NONE  0               /* nothing changed that needs the link */
#define PREFS_USE   1               /* connect: another gateway, or a new key */
#define PREFS_ADD   2               /* add a gateway (setup_run) */
#define PREFS_GONE  3               /* the key is gone from the card */
#define PREFS_KEY   4               /* show the gateway in use's key (setup_run), then connect */
uint8_t prefs_run(const char *why);

/* prefs.c, in the overlay. */
uint8_t prefs_main(const char *why);

/* Saving (save.h): load it, then run it. 0, or a session.c outcome if
 * the gateway failed partway; save_note says what became of it (and if
 * the overlay could not be loaded). */
uint8_t save_run(const char *agent, const char *label);

#endif
