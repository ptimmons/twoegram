/*
 * The screen's shared pieces: splash, help, the status bar, and the card's
 * state as a status-bar field. The conversation screen is convo.c.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>

#define TWOEGRAM_VERSION "0.1"

void splash_show(void);         /* draws the splash; caller waits for a key */
void help_show(void);           /* draws the help box over the body rows */
void convo_run(void);           /* the conversation screen, until quit */

/* The status bar. status_main() is the resting state: the agent, the
 * link's security, the card's address, and the help hint. status_msg()
 * takes the whole bar for a message until the next status_main(). */
void status_main(const char *agent, const char *security);
void status_msg(const char *msg);

/* What the security field says with a session. */
#define SEC_SIGNED      "SIGNED, NOT ENCRYPTED"

/* The card's state, shown in status_main()'s network field: "COGS
 * 192.168.1.60", "NO COGS CARD", ... Set by net_check(). */
extern char status_net[24];

/* Find the card and read its link state into status_net. */
void net_check(void);

#endif
