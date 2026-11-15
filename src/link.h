/*
 * Getting to the gateway: the address from TWOEGRAM.CFG, the key from the
 * card, the set-up screen when either is missing or the gateway does not
 * know the key, and the hello. With no CoGS card (an emulator) the app
 * shows milestone 0's made-up conversations instead.
 */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>

extern uint8_t link_live;           /* a session is up */
extern char    link_sec[24];        /* the status bar's security field */
extern char    link_err[64];        /* why there is no session, if there is none */

/* At startup, after net_check(). Ends with a session, or without one and
 * link_err saying why. */
void link_start(void);

/* Say hello again (Open-Apple-R). 0, or why not. */
uint8_t link_connect(void);

/* The set-up screen, then hello (Open-Apple-K). */
void link_setup(void);

/* setup.c: 1 if the address and key were set, 0 if cancelled. */
uint8_t setup_run(const char *why);

#endif
