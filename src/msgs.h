/*
 * The agents and their conversations, as the screen sees them.
 *
 * Milestone 0 serves made-up text from main memory (demo.c). From
 * milestone 3 the agent list comes from the gateway, and from milestone 4
 * the messages live in aux memory; the screen code does not change, since
 * it reaches the text only through these calls.
 */
#ifndef MSGS_H
#define MSGS_H

#include "port.h"

#define AGENT_NAME_MAX  16
#define MSG_TEXT_MAX    1500        /* one part of a reply */

uint8_t     agent_count(void);
const char *agent_name(uint8_t a);
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

/* Add a message to the current conversation. Returns 0 if there was no
 * room. */
uint8_t     msg_add(uint8_t mine, const char *meta, const char *text, uint16_t n);

#endif
