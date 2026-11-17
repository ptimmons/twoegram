/*
 * The conversation screen: the agent list at the left, the conversation
 * at the right, and a compose line under it.
 *
 *   row  0      headings: AGENTS, and the agent's name
 *   row  1      a rule, '_' along the foot of the row, so it meets the
 *               divider where the divider starts
 *   rows 2..    messages: the agent's at the left, yours at the right
 *   then        a rule over the compose area, in the pane only
 *   ..row 22    the compose area, one to three rows
 *   row 23      the status bar
 *
 * The divider is column 20, a MouseText stroke down the RIGHT edge of its
 * cell, so it is the left edge of the pane and the compose rule (starting
 * at column 21) meets it. Open-Apple-B hides the list, and the
 * conversation, re-wrapped, takes all 80 columns.
 */
#include <string.h>
#include "screen.h"
#include "kbd.h"
#include "fmt.h"
#include "layout.h"
#include "msgs.h"
#include "ui.h"
#include "link.h"
#include "session.h"
#include "store.h"
#include "history.h"
#include "tick.h"
#include "cogs.h"
#include "lock.h"

#define LIST_W      18              /* columns 0..17: >, the name, the diamond */
#define DIV_COL     18
#define HEAD_ROW    0
#define RULE_ROW    1
#define FIRST_ROW   2
#define LAST_ROW    (BODY_ROWS - 1) /* 22 */
#define NAME_MAX    AGENT_NAME_MAX  /* 16: columns 1..16 of the list */
#define DIAMOND_COL (NAME_MAX + 1)  /* 17 */

#define COMPOSE_MAX 255
#define COMPOSE_ROWS_MAX 3

#define F_CHAT      0
#define F_LIST      1

static uint8_t  list_on = 1;
static uint8_t  focus = F_CHAT;
static uint8_t  sel;                /* the list's bar */
static uint8_t  cur;                /* the conversation shown */
static uint8_t  crows = 1;          /* rows the compose area has */
static uint16_t scroll;             /* lines back from the newest */
static uint8_t  clen;
static char     cbuf[COMPOSE_MAX + 1];
static char     tmp[SCR_COLS + 1];

/* ---- geometry ---- */

static uint8_t pane_x(void)
{
    return list_on ? DIV_COL + 1 : 0;
}

/* The text area runs from one column inside the pane to column 77
 * (counting from 0); 78 is left blank, and 79 holds the scroll arrows. */
static uint8_t text_x(void)
{
    return (uint8_t)(pane_x() + 1);
}

static uint8_t text_w(void)
{
    return (uint8_t)(SCR_COLS - 3 - pane_x());
}

static uint8_t compose_w(void)
{
    return (uint8_t)(text_w() - 2);         /* after the "> " */
}

static uint8_t compose_rows(void)
{
    uint8_t n = (uint8_t)(clen / compose_w() + 1);

    return n > COMPOSE_ROWS_MAX ? COMPOSE_ROWS_MAX : n;
}

static uint8_t msg_rows(void)
{
    return (uint8_t)(LAST_ROW - crows - FIRST_ROW);     /* above the rule */
}

static uint16_t max_scroll(void)
{
    uint8_t n = msg_rows();

    return lay_count > n ? (uint16_t)(lay_count - n) : 0;
}

static const char *cur_name(void)
{
    if (!agent_count()) {
        return "(no agents)";
    }
    return agent_label(cur);
}

/* ---- the conversation's lines ---- */

/*
 * The layout is a window on the held messages: it ends at win_end (-1:
 * the newest held) and reaches back as far as LAY_MAX lines allow. Up
 * past its top brings older messages into it, fetching them from the
 * gateway when none older are held; down past its bottom does the same
 * the other way (go_older, go_newer).
 */
#define WIN_BACK    40              /* messages laid out at most */
#define WIN_STEP    8               /* messages a window moves forward */

static int16_t win_end = -1;
static uint16_t since_poll;         /* frames since the last poll */
static uint16_t idle_frames;        /* since the last key, up to a minute */
static uint8_t  idle_min;           /* minutes since the last key */
static uint8_t  news_below;         /* messages came in below the screen */

static void lay_one(uint16_t i)
{
    uint16_t n;
    const char *p = msg_text(i, &n);

    lay_msg(i, p, n, msg_mine(i), txt_len(msg_meta(i)));
}

static void relayout(void)
{
    uint16_t i, cnt = msg_count(), end, from;

    end = (win_end < 0 || (uint16_t)win_end >= cnt) ? cnt : (uint16_t)(win_end + 1);
    if (end == cnt) {
        win_end = -1;
    }
    from = end > WIN_BACK ? (uint16_t)(end - WIN_BACK) : 0;
    lay_reset(text_w());
    for (i = from; i < end; ++i) {
        lay_one(i);
    }
}

/* The window from the first held message, as far as it fits. */
static void relayout_from_start(void)
{
    uint16_t i, cnt = msg_count();

    lay_reset(text_w());
    for (i = 0; i < cnt; ++i) {
        lay_one(i);
        if (lay_dropped) {
            break;
        }
    }
    win_end = (int16_t)(i ? i - 1 : 0);
    relayout();
}

/* ---- drawing ---- */

/* Whether the open conversation has messages below what is on the screen
 * that have not been seen: the diamond beside its name. Reaching the
 * newest end clears it. */
static uint8_t cur_news(void)
{
    if (news_below && !scroll && win_end < 0) {
        news_below = 0;
    }
    return (uint8_t)(msgs_now == MSGS_LIVE && (hist_unseen || news_below));
}

/* Start a body row: clear it, and draw the list's cell and the divider. */
static void row_begin(uint8_t row)
{
    uint8_t a = (uint8_t)(row - FIRST_ROW), bar, n;

    lb_clear(SC_SPACE);
    if (!list_on || row < FIRST_ROW) {
        return;
    }
    linebuf[DIV_COL] = has_mousetext ? MT_VERT_R : SC_BAR;
    if (a >= agent_count()) {
        return;
    }
    bar = (uint8_t)(focus == F_LIST && a == sel);
    if (bar) {
        lb_fill(0, LIST_W, SC_ISPACE);
    }
    if (a == cur) {
        linebuf[0] = sc_char('>', bar);
    }
    n = txt_len(agent_label(a));
    if (n > NAME_MAX) {
        n = NAME_MAX;
    }
    lb_textn(1, agent_label(a), n, bar);
    /* The diamond, in its own column: something new, below the screen
     * here, or from that agent. (The count went to make room for
     * 16-character names; the heading still counts them when the list
     * is hidden.) */
    if (a == cur ? cur_news() : agent_unread(a)) {
        linebuf[DIAMOND_COL] = sc_char(GL_DIAMOND, bar);
    }
}

/* What the heading says at the right of the pane, before the arrow. */
static const char *head_note(void)
{
    static char note[24];
    uint8_t a, u = 0;

    if (!list_on) {
        /* No list to put the diamonds in: the heading counts them. */
        for (a = 0; a < agent_count(); ++a) {
            if (a != cur) {
                u = (uint8_t)(u + agent_unread(a));
            }
        }
        if (u) {
            note[0] = GL_DIAMOND;
            note[1] = ' ';
            fmt_str(fmt_u32(note + 2, u), " new elsewhere");
            return note;
        }
    }
    if (msgs_now == MSGS_LIVE && hist_busy) {
        return "thinking...";
    }
    if (lay_dropped && !scroll && win_end < 0) {
        return "older not shown";
    }
    return "";
}

static void draw_head(void)
{
    const char *note = head_note();
    uint8_t x;

    lb_clear(SC_SPACE);
    if (list_on) {
        lb_text(1, "AGENTS", 0);
    }
    x = lb_text(text_x(), cur_name(), 0);
    if (cur_news()) {
        linebuf[x + 1] = sc_char(GL_DIAMOND, 0);
    }
    lb_text((uint8_t)(SCR_COLS - 2 - txt_len(note)), note, 0);
    scr_blit(HEAD_ROW);

    lb_clear(SC_SPACE);
    lb_fill(0, SCR_COLS, SC_UNDER);
    scr_blit(RULE_ROW);
}

static void draw_line(const lay_line_t *l)
{
    uint8_t x = (uint8_t)(text_x() + l->col);

    /* Yours are told apart by alignment alone: plain text, right-aligned
     * (decided 2026-11-15 from the milestone 0 screenshots). */
    if (l->kind == LK_TEXT) {
        msg_copy(l->msg, l->off, l->len, tmp);
        lb_textn(x, tmp, l->len, 0);
    } else if (l->kind == LK_META) {
        lb_textn(x, msg_meta(l->msg), l->len, 0);
    }
}

/* What the pane says when there is nothing to show: line r of it. */
static const char *pane_note(uint8_t r)
{
    static char l1[80];

    if (msgs_now == MSGS_LIVE) {
        switch (r) {
        case 1:
            fmt_str(fmt_str(fmt_str(l1, "Signed in as device "), ses_id), ".");
            return l1;
        case 2: return "Every request and reply is signed; nothing is encrypted.";
        case 4: return "No messages with this agent yet: type one below.";
        }
        return "";
    }
    if (msgs_now == MSGS_NONE) {
        switch (r) {
        case 1: return link_err;
        case 3: return OA "-K  set the gateway and key";
        case 4: return OA "-R  try to connect again";
        }
    }
    return "";
}

static void draw_msgs(void)
{
    uint8_t n = msg_rows(), r, up, down, live = (uint8_t)(msgs_now == MSGS_LIVE);
    uint16_t first, i;

    if (scroll > max_scroll()) {
        scroll = max_scroll();
    }
    /* Newest at the foot once the conversation fills the pane; until
     * then, from the top. */
    first = lay_count > n ? (uint16_t)(lay_count - n - scroll) : 0;
    /* The 80th column, a scroll bar of sorts: an up arrow at the pane's
     * top when there is more above what is shown (on the screen's lines,
     * held, or at the gateway), a down arrow at its foot when there is
     * more below. */
    up = (uint8_t)(lay_count && (first || lay_lines[0].msg || (live && st_older)));
    down = (uint8_t)(lay_count && (scroll || win_end >= 0 || (live && !st_newest)));
    for (r = 0; r < n; ++r) {
        row_begin((uint8_t)(FIRST_ROW + r));
        i = (uint16_t)(first + r);
        if (i < lay_count) {
            draw_line(&lay_lines[i]);
        } else if (!lay_count) {
            lb_text(text_x(), pane_note(r), 0);
        }
        if (r == 0 && up) {
            linebuf[SCR_COLS - 1] = sc_char(GL_UP, 0);
        }
        if (r == n - 1 && down) {
            linebuf[SCR_COLS - 1] = sc_char(GL_DOWN, 0);
        }
        scr_blit((uint8_t)(FIRST_ROW + r));
    }
}

static void draw_compose(void)
{
    uint8_t cw = compose_w(), x = text_x(), row, j, cnt;
    uint8_t total = (uint8_t)(clen / cw + 1), start, line;
    uint16_t off;

    row = (uint8_t)(LAST_ROW - crows);
    row_begin(row);
    lb_fill(pane_x(), (uint8_t)(SCR_COLS - pane_x()), SC_UNDER);
    scr_blit(row);

    /* The end being typed stays in view. */
    start = total > crows ? (uint8_t)(total - crows) : 0;
    for (j = 0; j < crows; ++j) {
        row = (uint8_t)(LAST_ROW - crows + 1 + j);
        line = (uint8_t)(start + j);
        row_begin(row);
        if (line == 0) {
            lb_text(x, ">", 0);
        }
        off = (uint16_t)line * cw;
        if (off < clen) {
            cnt = (uint8_t)(clen - off > cw ? cw : clen - off);
            lb_textn((uint8_t)(x + 2), cbuf + off, cnt, 0);
        }
        if (focus == F_CHAT && line == clen / cw) {
            linebuf[x + 2 + clen % cw] = SC_ISPACE;     /* the cursor */
        }
        scr_blit(row);
    }
}

static void draw_status(void)
{
    status_main(cur_name(), link_sec);
}

static void draw_body(void)
{
    draw_msgs();
    draw_compose();
}

static void draw_all(void)
{
    draw_head();
    draw_body();
    draw_status();
}

/* ---- actions ---- */

/* The open agent's name: a new hello may bring a new list, in which it
 * has another place, or none. */
static char cur_agent[SES_AGENT_LEN + 1];
static uint8_t restarting;

static void restart(void);

/* After a request: 0, with the status bar put right if that ended a spell
 * of trouble; or nonzero, the failure said and acted on (link.h), and the
 * caller stops. A failure the user asked for (loud) is said in full; a
 * poll's only on the status bar. */
static uint8_t done(uint8_t rc, uint8_t loud)
{
    if (!rc) {
        if (link_ok()) {
            draw_status();
        }
        /* A hello on the way (the gateway restarted) brought a list in
         * which the open agent has moved: follow it. */
        if (!restarting && cur_agent[0] && strcmp(agent_name(cur), cur_agent)) {
            restart();
            return 1;
        }
        return 0;
    }
    switch (link_trouble(rc)) {
    case LT_RETRY:
        draw_status();
        if (!loud) {
            return rc;
        }
        break;
    case LT_AGENTS:
        if (!restarting) {
            link_connect();
            restart();
            return rc;
        }
        break;
    case LT_OFFLINE:
        restart();
        return rc;
    }
    status_msg(ses_strerror(rc));
    return rc;
}

/* Fetch the newest page of the current agent's conversation, or with
 * first its first page. */
static void load_current(uint8_t first)
{
    if (msgs_now != MSGS_LIVE || !agent_count()) {
        return;
    }
    status_msg("FETCHING MESSAGES");
    link_begin();
    done(hist_load(agent_name(cur), first), 1);
}

static void open_agent(uint8_t a)
{
    cur = a;
    sel = a;
    if (agent_count()) {
        fmt_str(cur_agent, agent_name(a));
    }
    msg_select(a);
    load_current(0);
    win_end = -1;
    scroll = 0;
    relayout();
    draw_all();
}

/* After a new hello, or losing the session: the same agent if it is
 * still there, else the first, from the newest message. */
static void restart(void)
{
    uint8_t a;

    for (a = agent_count(); a && strcmp(agent_name(a - 1), cur_agent); --a) {
    }
    restarting = 1;
    open_agent(a ? a - 1 : 0);
    restarting = 0;
    if (!link_live) {
        status_msg(link_err);
    }
}

static void next_unread(void)
{
    uint8_t n = agent_count(), i, a;

    for (i = 1; i < n; ++i) {
        a = (uint8_t)((cur + i) % n);
        if (agent_unread(a)) {
            open_agent(a);
            return;
        }
    }
    status_msg("NOTHING NEW ELSEWHERE");
}

/* The compose area changed: redraw it, or everything above it too if it
 * grew or shrank. */
static void compose_changed(void)
{
    uint8_t n = compose_rows();

    if (n != crows) {
        crows = n;
        draw_body();
    } else {
        draw_compose();
    }
}

static void send(void)
{
    if (!clen) {
        return;
    }
    if (msgs_now != MSGS_LIVE || !agent_count()) {
        status_msg("NOT CONNECTED: NOTHING CAN BE SENT");
        return;
    }
    /* A message joins the newest end: go there first if scrolled away. */
    if (!st_newest) {
        load_current(0);
    }
    status_msg("SENDING");
    link_begin();
    if (done(hist_send(agent_name(cur), cbuf, clen), 1)) {
        return;                     /* the text stays, to send again */
    }
    clen = 0;
    cbuf[0] = 0;
    crows = compose_rows();
    win_end = -1;
    scroll = 0;
    since_poll = 0;
    relayout();
    draw_all();
}

/* Line index of message m's first (last 0) or last (last 1) line in
 * the layout, or -1. */
static int16_t line_of(uint16_t m, uint8_t last)
{
    int16_t i, found = -1;

    /* Not the blank before it, which it has only when it is not the
     * layout's first message. */
    for (i = 0; i < (int16_t)lay_count; ++i) {
        if (lay_lines[i].msg == m && lay_lines[i].kind != LK_BLANK) {
            found = i;
            if (!last) {
                break;
            }
        }
    }
    return found;
}

/* Scroll so layout line k is on view row r (0 the top). */
static void put_line(int16_t k, uint8_t r)
{
    int32_t s = (int32_t)lay_count - msg_rows() - (k - r);

    if (s < 0) {
        s = 0;
    }
    if (s > (int32_t)max_scroll()) {
        s = max_scroll();
    }
    scroll = (uint16_t)s;
}

/* The message whose line is on view row r. */
static uint16_t msg_on_row(uint8_t r)
{
    uint8_t n = msg_rows();
    int16_t first = lay_count > n ? (int16_t)(lay_count - n - scroll) : 0;
    int16_t i = (int16_t)(first + r);

    if (i >= (int16_t)lay_count) {
        i = (int16_t)(lay_count - 1);
    }
    return lay_lines[i < 0 ? 0 : i].msg;
}

/* At the top of the layout and asked to go up by step lines. */
static void go_older(uint8_t step)
{
    uint16_t top, last;
    int16_t k;

    if (!lay_count) {
        return;
    }
    top = lay_lines[0].msg;
    last = msg_on_row((uint8_t)(msg_rows() - 1));
    if (top == 0) {
        if (msgs_now != MSGS_LIVE || !st_older) {
            return;                 /* the start: no up arrow says so */
        }
        status_msg("FETCHING OLDER MESSAGES");
        st_shift = 0;
        link_begin();
        if (done(hist_older(agent_name(cur)), 1)) {
            return;
        }
        if (!hist_added) {
            draw_all();             /* the start after all: the arrow goes */
            return;
        }
        top = (uint16_t)(top + st_shift);
        last = (uint16_t)(last + st_shift);
        if (last >= msg_count()) {
            last = (uint16_t)(msg_count() - 1);  /* the newest were dropped */
        }
    }
    /* The window ends with what is on the screen's last row, so all of
     * the screen stays in it and what is before it comes in above; the
     * top line then moves down by step rows. */
    win_end = (int16_t)last;
    relayout();
    k = line_of(top, 0);
    put_line(k < 0 ? 0 : k, step);
    draw_all();
}

/* At the bottom of the layout and asked to go down by step lines. */
static void go_newer(uint8_t step)
{
    uint16_t bottom, first, cnt, ahead;
    int16_t k, r;

    if (!lay_count) {
        return;
    }
    bottom = lay_lines[lay_count - 1].msg;
    first = msg_on_row(0);
    cnt = msg_count();
    if (bottom + 1 >= cnt) {
        if (msgs_now != MSGS_LIVE || st_newest) {
            return;                 /* the newest: nothing below */
        }
        status_msg("FETCHING NEWER MESSAGES");
        st_shift = 0;
        link_begin();
        if (done(hist_newer(agent_name(cur)), 1)) {
            return;
        }
        cnt = msg_count();
        if (!hist_added) {
            draw_status();
            return;
        }
        bottom = (uint16_t)(bottom + st_shift);
        first = (int16_t)(first + st_shift) < 0 ? 0 : (uint16_t)(first + st_shift);
    }
    /* The window reaches a few messages further on, but never so far that
     * what is on the screen's top row falls out of the layout's lines. */
    for (ahead = WIN_STEP; ; ahead = (uint16_t)(ahead / 2)) {
        win_end = (int16_t)(bottom + ahead);
        if ((uint16_t)win_end >= cnt - 1) {
            win_end = -1;
        }
        relayout();
        if (ahead <= 1 || line_of(first, 0) >= 0) {
            break;
        }
    }
    k = line_of(bottom, 1);
    r = (int16_t)msg_rows() - 1 - step;
    put_line(k < 0 ? 0 : k, (uint8_t)(r < 0 ? 0 : r));
    draw_all();
}

static void scroll_by(int16_t d)
{
    int32_t s = (int32_t)scroll + d;

    if (s < 0) {
        s = 0;
    }
    if (s > (int32_t)max_scroll()) {
        s = max_scroll();
    }
    if ((uint16_t)s != scroll) {
        scroll = (uint16_t)s;
        draw_head();
        draw_msgs();
    }
}

/* ---- polling ---- */

#define POLL_IDLE   (5 * TICKS_PER_SEC)     /* nothing happening */
#define POLL_BUSY   (1 * TICKS_PER_SEC)     /* the agent is answering */
#define POLL_SLOW   60                      /* seconds, at most, after failures */

/* How long to the next poll: after a failure 5 s, then 10, 20, 40 and
 * 60, so a gateway that is down is not hammered (nor the Apple frozen
 * waiting on it), and one that comes back is found within a minute. */
static uint16_t poll_gap(void)
{
    uint8_t secs;

    if (!link_fails) {
        return hist_busy ? POLL_BUSY : POLL_IDLE;
    }
    secs = link_fails > 4 ? POLL_SLOW : (uint8_t)(5 << (link_fails - 1));
    return (uint16_t)(secs * TICKS_PER_SEC);
}

/* Something new: show it if at the newest, else keep the screen still
 * and say there is more below. */
static void poll_now(void)
{
    uint16_t anchor = 0;
    int16_t ofs = 0, k, top_line;
    uint8_t at_bottom = (uint8_t)(!scroll && win_end < 0);
    uint8_t was_busy = hist_busy, was_unseen = hist_unseen;
    uint8_t was_unread[SES_AGENTS_MAX];

    memcpy(was_unread, hist_unread, sizeof was_unread);

    if (lay_count && !at_bottom) {
        /* where the screen's top row is, to put it back there */
        anchor = msg_on_row(0);
        top_line = lay_count > msg_rows() ? (int16_t)(lay_count - msg_rows() - scroll) : 0;
        ofs = (int16_t)(top_line - line_of(anchor, 0));
    }
    st_shift = 0;
    link_begin();
    if (done(hist_poll(agent_name(cur)), 0)) {
        return;
    }
    if (hist_added) {
        if (at_bottom) {
            relayout();
            scroll = 0;
        } else {
            anchor = (uint16_t)(anchor + st_shift);
            if (win_end >= 0) {
                win_end = (int16_t)(win_end + st_shift);
            }
            relayout();
            k = line_of(anchor, 0);
            put_line((int16_t)((k < 0 ? 0 : k) + ofs), 0);
            news_below = 1;
        }
        draw_msgs();
    } else if (list_on && (was_unseen != hist_unseen ||
                           memcmp(was_unread, hist_unread, sizeof was_unread))) {
        draw_msgs();                /* the list's diamonds */
    }
    if (hist_added || was_busy != hist_busy || was_unseen != hist_unseen ||
        (!list_on && memcmp(was_unread, hist_unread, sizeof was_unread))) {
        draw_head();
    }
}

/* Lock: the key, the session key and the messages leave memory until
 * the passphrase is typed again; then sign in afresh. */
static void lock_screen(void)
{
    if (!lock_armed) {
        status_msg("NO PASSPHRASE IS SET: GIVE ONE ON THE SET-UP SCREEN, OPEN-APPLE-K");
        return;
    }
    st_reset();
    lock_now();
    idle_frames = 0;
    idle_min = 0;
    link_connect();
    restart();
}

/* Wait for a key, polling the gateway meanwhile; lock after
 * LOCK_IDLE_MIN minutes without one. */
static uint8_t wait_key(void)
{
    while (!key_ready()) {
        if (tick_poll()) {
            ++since_poll;
            if (++idle_frames >= TICKS_PER_SEC * 60) {
                idle_frames = 0;
                if (++idle_min >= LOCK_IDLE_MIN && lock_armed) {
                    lock_screen();
                }
            }
        }
        if (since_poll < poll_gap()) {
            continue;
        }
        since_poll = 0;
        if (msgs_now == MSGS_LIVE && agent_count()) {
            poll_now();
        } else if (link_waiting) {
            /* Not signed in, for a passing reason: try again. */
            link_connect();
            restart();
        }
    }
    idle_frames = 0;
    idle_min = 0;
    return key_get();
}

/* Open-Apple keys: the same whichever side has the focus. Returns 1 to
 * quit. */
static uint8_t oa_key(uint8_t k)
{
    switch (k) {
    case '?': case '/': case 'h': case 'H':
        help_show();
        key_get();
        draw_all();
        break;
    case 'q': case 'Q':
        return 1;
    case 'b': case 'B':
        list_on ^= 1;
        if (!list_on) {
            focus = F_CHAT;
        }
        crows = compose_rows();
        relayout();
        draw_all();
        break;
    case 'k': case 'K':
        link_setup();
        restart();
        break;
    case 'l': case 'L':
        lock_screen();
        break;
    case 'r': case 'R':
        if (!cogs_slot) {
            status_msg("NO COGS CARD: NOTHING TO CONNECT TO");
            break;
        }
        link_connect();
        restart();
        break;
    case 'n': case 'N':
        next_unread();
        break;
    case K_UP:
        if (scroll >= max_scroll()) {
            go_older((uint8_t)(msg_rows() - 1));
        } else {
            scroll_by((int16_t)(msg_rows() - 1));
        }
        break;
    case K_DOWN:
        if (!scroll) {
            go_newer((uint8_t)(msg_rows() - 1));
        } else {
            scroll_by((int16_t)-(msg_rows() - 1));
        }
        break;
    case '<': case ',':
        /* the start: from the gateway if it has older than held */
        if (msgs_now == MSGS_LIVE && st_older) {
            load_current(1);
        }
        relayout_from_start();
        scroll = max_scroll();
        draw_all();
        break;
    case '>': case '.':
        /* the newest: from the gateway if it has newer than held */
        if (msgs_now == MSGS_LIVE && !st_newest) {
            load_current(0);
        }
        win_end = -1;
        relayout();
        scroll = 0;
        draw_all();
        break;
    }
    return 0;
}

static void list_key(uint8_t k)
{
    switch (k) {
    case K_UP:
        if (sel) {
            --sel;
            draw_body();
        }
        break;
    case K_DOWN:
        if (sel + 1 < agent_count()) {
            ++sel;
            draw_body();
        }
        break;
    case K_RETURN:
        focus = F_CHAT;
        if (sel != cur) {
            open_agent(sel);
        } else {
            draw_body();
        }
        break;
    case K_TAB: case K_ESC:
        focus = F_CHAT;
        sel = cur;
        draw_body();
        break;
    }
}

static void chat_key(uint8_t k)
{
    switch (k) {
    case K_TAB: case K_ESC:
        if (list_on) {
            focus = F_LIST;
            sel = cur;
            draw_body();
        }
        break;
    case K_UP:
        if (scroll >= max_scroll()) {
            go_older(1);
        } else {
            scroll_by(1);
        }
        break;
    case K_DOWN:
        if (!scroll) {
            go_newer(1);
        } else {
            scroll_by(-1);
        }
        break;
    case K_RETURN:
        send();
        break;
    case K_DELETE: case K_LEFT:
        if (clen) {
            cbuf[--clen] = 0;
            compose_changed();
        }
        break;
    default:
        if (k >= 0x20 && k < 0x7F && clen < COMPOSE_MAX) {
            cbuf[clen++] = (char)k;
            cbuf[clen] = 0;
            compose_changed();
        }
        break;
    }
}

void convo_run(void)
{
    static uint8_t entered;
    uint8_t k;

    /* The first time only: the host tests come back in where they left. */
    if (!entered) {
        entered = 1;
        restart();
    } else {
        draw_all();
        if (!link_live && link_err[0]) {
            status_msg(link_err);
        }
    }
    for (;;) {
        k = wait_key();
        if (key_oa) {
            if (oa_key(k)) {
                return;
            }
        } else if (focus == F_LIST) {
            list_key(k);
        } else {
            chat_key(k);
        }
    }
}
