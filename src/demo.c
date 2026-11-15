/*
 * Milestone 0's made-up agents and conversations, so the screen can be
 * judged before anything talks to a gateway. Messages typed are kept in a
 * small pool and answered with a canned reply. Replaced at milestones 3
 * and 4. See msgs.h.
 */
#include <string.h>
#include "msgs.h"
#include "session.h"

typedef struct {
    uint8_t     mine;
    const char *meta;
    const char *text;
} demo_msg_t;

static const demo_msg_t main_msgs[] = {
    { 1, "09:12", "Good morning. What's on the calendar today?" },
    { 0, "09:12", "Good morning! Two things today:\n"
                  "- 10:30 Call with the plumber\n"
                  "- 15:00 Pick up the parcel at the post office\n"
                  "Tomorrow is clear." },
    { 1, "09:14", "Remind me about the parcel at 14:30." },
    { 0, "09:14", "Done. I'll remind you at 14:30, and again at 14:50 if you "
                  "haven't marked it done." },
    { 1, "09:20", "Thanks. Also, what does the OpenClaw page say about "
                  "pairing a new device?" },
    { 0, "09:21", "Unknown senders get an eight-character code and are "
                  "ignored until you approve it on the gateway with "
                  "openclaw pairing approve. The details are at "
                  "https://docs.openclaw.ai/channels/pairing-and-allowlists-for-every-channel "
                  "(a long address, to show how a word wider than the "
                  "bubble is split)." },
};

static const demo_msg_t coder_msgs[] = {
    { 1, "11:02", "Can you write a 6502 routine that clears page 2?" },
    { 0, "11:03", "Here is one. Y is the index, and every byte gets zero:\n"
                  "\n"
                  "        lda #$00\n"
                  "        ldy #$00\n"
                  "loop:   sta $0200,y\n"
                  "        iny\n"
                  "        bne loop\n"
                  "        rts\n"
                  "\n"
                  "That is about 2,300 cycles at 1 MHz." },
    { 0, "11:03", "Want a version that clears any page you pass in?" },
};

static const demo_msg_t home_msgs[] = {
    { 0, "07:00", "Good morning. The house is at 19 degrees and the front "
                  "door is locked. Rain is expected after 16:00, so the "
                  "garden watering is skipped today." },
};

typedef struct {
    const char       *name;
    const demo_msg_t *msgs;
    uint8_t           nmsgs;
    uint8_t           unread;
} demo_agent_t;

static demo_agent_t agents[] = {
    { "main",  main_msgs,  sizeof main_msgs / sizeof main_msgs[0],   0 },
    { "coder", coder_msgs, sizeof coder_msgs / sizeof coder_msgs[0], 2 },
    { "home",  home_msgs,  sizeof home_msgs / sizeof home_msgs[0],   1 },
};

#define NAGENTS (sizeof agents / sizeof agents[0])

/* Messages typed this session, for the current agent only: switching
 * agents empties the pool. */
#define POOL_BYTES  2048
#define POOL_MSGS   24

/* In LOWBSS ($0800-$1FFF): only the first pool_used bytes are ever read. */
#ifdef __CC65__
#pragma bss-name (push, "LOWBSS")
#endif
static char     pool[POOL_BYTES];
#ifdef __CC65__
#pragma bss-name (pop)
#endif
static uint16_t pool_used;
static uint8_t  pool_n;
static uint16_t pool_off[POOL_MSGS];
static uint16_t pool_len[POOL_MSGS];
static uint8_t  pool_mine[POOL_MSGS];
static char     pool_meta[POOL_MSGS][9];

static uint8_t cur;
uint8_t msgs_now = MSGS_DEMO;

void msgs_mode(uint8_t m)
{
    msgs_now = m;
    cur = 0;
    pool_used = 0;
    pool_n = 0;
}

/* The made-up messages of the current agent: none unless in demo mode. */
static uint8_t base_n(void)
{
    return msgs_now == MSGS_DEMO ? agents[cur].nmsgs : 0;
}

uint8_t agent_count(void)
{
    if (msgs_now == MSGS_DEMO) {
        return NAGENTS;
    }
    return msgs_now == MSGS_LIVE ? ses_nagents : 0;
}

const char *agent_name(uint8_t a)
{
    if (msgs_now == MSGS_DEMO) {
        return agents[a].name;
    }
    return ses_agents[a];
}

uint8_t agent_unread(uint8_t a)
{
    return msgs_now == MSGS_DEMO ? agents[a].unread : 0;
}

void msg_select(uint8_t a)
{
    if (a != cur) {
        pool_used = 0;
        pool_n = 0;
    }
    cur = a;
    if (msgs_now == MSGS_DEMO) {
        agents[a].unread = 0;
    }
}

uint16_t msg_count(void)
{
    return (uint16_t)(base_n() + pool_n);
}

uint8_t msg_mine(uint16_t i)
{
    uint8_t n = base_n();

    return i < n ? agents[cur].msgs[i].mine : pool_mine[i - n];
}

const char *msg_meta(uint16_t i)
{
    uint8_t n = base_n();

    if (i < n) {
        return agents[cur].msgs[i].meta;
    }
    return pool_meta[i - n];
}

const char *msg_text(uint16_t i, uint16_t *len)
{
    uint8_t n = base_n();

    if (i < n) {
        *len = (uint16_t)strlen(agents[cur].msgs[i].text);
        return agents[cur].msgs[i].text;
    }
    *len = pool_len[i - n];
    return pool + pool_off[i - n];
}

void msg_copy(uint16_t i, uint16_t off, uint8_t len, char *dst)
{
    uint16_t n;

    memcpy(dst, msg_text(i, &n) + off, len);
}

uint8_t msg_add(uint8_t mine, const char *meta, const char *text, uint16_t n)
{
    if (pool_n >= POOL_MSGS || n > POOL_BYTES - pool_used) {
        return 0;
    }
    memcpy(pool + pool_used, text, n);
    pool_off[pool_n] = pool_used;
    pool_len[pool_n] = n;
    pool_mine[pool_n] = mine;
    strncpy(pool_meta[pool_n], meta, 8);
    pool_meta[pool_n][8] = 0;
    pool_used += n;
    ++pool_n;
    return 1;
}
