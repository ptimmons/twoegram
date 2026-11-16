/*
 * The agents and their conversations, as the screen sees them.
 *
 * Served by msgs.c from the gateway's agents (session.c) and the messages
 * held in aux memory (store.c). The screen reaches the text only through
 * these calls.
 */
#ifndef MSGS_H
#define MSGS_H

#include "port.h"

#define AGENT_NAME_MAX  16
#define MSG_TEXT_MAX    1500        /* one part of a reply */

/* Where the agents come from. */
#define MSGS_LIVE   1               /* the gateway's agents and their messages */
#define MSGS_NONE   2               /* no session: no agents */
void        msgs_mode(uint8_t m);
extern uint8_t msgs_now;

uint8_t     agent_count(void);
const char *agent_name(uint8_t a);  /* its id, which requests name */
const char *agent_label(uint8_t a); /* what it calls itself, else its id */
uint8_t     agent_unread(uint8_t a);

/* Make agent a's conversation the current one; clears its unread count. */
void        msg_select(uint8_t a);

/* The current conversation. */
uint16_t    msg_count(void);
uint8_t     msg_mine(uint16_t i);
const char *msg_meta(uint16_t i);   /* the time, or a note such as "sending" */

/* Copy len bytes of message i's text, from off, to dst. */
void        msg_copy(uint16_t i, uint16_t off, uint8_t len, char *dst);

/* The whole text of message i, for laying it out: a pointer that is good
 * until the next msg_ call, and its length. */
const char *msg_text(uint16_t i, uint16_t *n);

#endif
