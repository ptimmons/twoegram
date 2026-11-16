/*
 * Fetching a conversation's messages from the gateway into the store:
 * the newest page when an agent is opened, older ones when scrolling up
 * past the first held, newer ones when scrolling down past the last.
 * Each returns 0 or a session.c outcome; hist_added says how many
 * messages came in.
 */
#ifndef HISTORY_H
#define HISTORY_H

#include "port.h"
#include "session.h"

#define HIST_PAGE   20              /* messages asked for at a time */

extern uint8_t  hist_added;
extern uint8_t  hist_busy;          /* the agent is working on an answer */
extern uint16_t hist_latest;        /* the newest message number known */
extern uint8_t  hist_unseen;        /* newer messages than the window holds */
/* Each other agent's messages not yet fetched, by its index in
 * ses_agents: from the gateway's unread lines, every poll. */
extern uint8_t  hist_unread[SES_AGENTS_MAX];

uint8_t hist_load(const char *agent);
uint8_t hist_older(const char *agent);
uint8_t hist_newer(const char *agent);

/* Send n bytes of text. The message as stored comes back and joins the
 * window, which must hold the newest (the caller loads it first). */
uint8_t hist_send(const char *agent, const char *text, uint8_t n);

/* What is new since the newest message known: added to the window if it
 * holds the newest, else counted in hist_unseen. Sets hist_busy. */
uint8_t hist_poll(const char *agent);

#endif
