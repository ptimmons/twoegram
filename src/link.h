/*
 * Getting to the gateway: the address from TWOEGRAM.CFG, the key from the
 * card, adding a gateway when there is none or it does not know the key,
 * and the hello. With no CoGS card (an emulator) the app
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

/*
 * After a request fails: what it means, with link_sec and link_err set to
 * match (docs/PROTOCOL.md section 4 says what each refusal asks for).
 *
 *   LT_NOTE     this request only (too long, malformed): say it, go on
 *   LT_RETRY    passing (no network, gateway down or busy): the session,
 *               if any, is kept and the status bar says what is wrong;
 *               try again later, waiting longer each time (link_fails)
 *   LT_AGENTS   the agent has gone: say hello again for the new list
 *   LT_OFFLINE  nothing more until the user acts: link_err says what
 */
#define LT_NOTE     0
#define LT_RETRY    1
#define LT_AGENTS   2
#define LT_OFFLINE  3
uint8_t link_trouble(uint8_t rc);

/* After a request succeeds: 1 if that ends a spell of trouble (redraw the
 * status bar). */
uint8_t link_ok(void);

extern uint8_t link_fails;          /* passing failures in a row */
extern uint8_t link_waiting;        /* no session, but hello is retried by itself */
extern uint8_t link_badkey;         /* no session: the gateway refused the key */

/* Before each request: restart the backstop clock the card's waits run
 * against (30 s; the card's own timeouts come first). */
void link_begin(void);

/* Say hello again. 0, or why not. */
uint8_t link_connect(void);

/* Open-Apple-R: hello again, and if the gateway refuses the key (or
 * there is none), show it to be added there. */
void link_again(void);

/* Settings, then whatever it asks for (Open-Apple-K). */
void link_setup(void);

#include "overlay.h"

#endif
