/*
 * The agents and the open conversation, as the screen sees them: the
 * gateway's agents (session.c) and the messages held in aux memory
 * (store.c). See msgs.h.
 *
 * Milestone 0's made-up conversations were here as demo.c until
 * milestone 5 needed their memory; with no card, there is now nothing to
 * show but the reason.
 */
#include <string.h>
#include "msgs.h"
#include "session.h"
#include "store.h"
#include "history.h"

uint8_t msgs_now = MSGS_NONE;

/* A message's text comes out of aux memory into this buffer to be laid
 * out. In LOWBSS ($0800-$1FFF): always written before it is read. */
#ifdef __CC65__
#pragma bss-name (push, "LOWBSS")
#endif
static char tbuf[ST_TEXT_MAX];
#ifdef __CC65__
#pragma bss-name (pop)
#endif

void msgs_mode(uint8_t m)
{
    msgs_now = m;
    st_reset();
}

uint8_t agent_count(void)
{
    return msgs_now == MSGS_LIVE ? ses_nagents : 0;
}

const char *agent_name(uint8_t a)
{
    return ses_agents[a];
}

const char *agent_label(uint8_t a)
{
    return ses_labels[a][0] ? ses_labels[a] : ses_agents[a];
}

uint8_t agent_unread(uint8_t a)
{
    return msgs_now == MSGS_LIVE && a < SES_AGENTS_MAX ? hist_unread[a] : 0;
}

void msg_select(uint8_t a)
{
    if (a < SES_AGENTS_MAX) {
        hist_unread[a] = 0;         /* about to be read */
    }
    st_reset();
}

uint16_t msg_count(void)
{
    return msgs_now == MSGS_LIVE ? st_count : 0;
}

uint8_t msg_mine(uint16_t i)
{
    return st[i].mine;
}

const char *msg_meta(uint16_t i)
{
    return st[i].time;
}

const char *msg_text(uint16_t i, uint16_t *len)
{
    *len = st[i].len;
    st_text((uint8_t)i, 0, st[i].len, tbuf);
    return tbuf;
}

void msg_copy(uint16_t i, uint16_t off, uint8_t len, char *dst)
{
    st_text((uint8_t)i, off, len, dst);
}
