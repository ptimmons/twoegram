/*
 * The set-up screen as an overlay.
 *
 * Main memory is full, and the set-up screen is open only at the first
 * start and on Open-Apple-K. So its code and text (setup.c, segment
 * OVERLAY1) are not part of TWOEGRAM.SYSTEM: they are the file
 * TWOEGRAM.SETUP, loaded at $0800 each time the screen opens, over
 * buffers that are idle then and rebuilt afterwards: the layout table,
 * the text buffer and the reply buffer (cfg/twoegram.cfg; memcheck.py
 * checks it stops short of ses_labels, which must survive).
 *
 * Never loaded during a request (the network disk is the card too).
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include "port.h"

#define SETUP_FILE  "TWOEGRAM.SETUP"

/* The set-up screen: load it, then run it. 1 if the address and key
 * were set, 0 if cancelled or it could not be loaded (said). */
uint8_t setup_run(const char *why);

/* setup.c, in the overlay. */
uint8_t setup_main(const char *why);

#endif
