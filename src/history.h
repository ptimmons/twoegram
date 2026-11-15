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

#define HIST_PAGE   20              /* messages asked for at a time */

extern uint8_t hist_added;

uint8_t hist_load(const char *agent);
uint8_t hist_older(const char *agent);
uint8_t hist_newer(const char *agent);

#endif
