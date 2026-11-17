/*
 * The set-up screen and saving, as overlays.
 *
 * Main memory is full, and the set-up screen is open only at the first
 * start and on Open-Apple-K. So its code and text (setup.c, segment
 * OVERLAY1) are not part of TWOEGRAM.SYSTEM: they are the file
 * TWOEGRAM.SETUP, loaded at $0800 each time the screen opens, over
 * buffers that are idle then and rebuilt afterwards: the layout table,
 * the text buffer and the reply buffer (cfg/twoegram.cfg; memcheck.py
 * checks it stops short of ses_labels, which must survive).
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
#define SAVE_FILE   "TWOEGRAM.SAVE"

/* The set-up screen: load it, then run it. 1 if the address and key
 * were set, 0 if cancelled or it could not be loaded (said). */
uint8_t setup_run(const char *why);

/* setup.c, in the overlay. */
uint8_t setup_main(const char *why);

/* Saving (save.h): load it, then run it. 0, or a session.c outcome if
 * the gateway failed partway; save_note says what became of it (and if
 * the overlay could not be loaded). */
uint8_t save_run(const char *agent, const char *label);

#endif
