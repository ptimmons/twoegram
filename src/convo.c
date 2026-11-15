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

#define LIST_W      20              /* columns 0..19 */
#define DIV_COL     20
#define HEAD_ROW    0
#define RULE_ROW    1
#define FIRST_ROW   2
#define LAST_ROW    (BODY_ROWS - 1) /* 22 */
#define NAME_MAX    13              /* of an agent's name in the list */

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

/* The text area runs from one column inside the pane to column 78. */
static uint8_t text_x(void)
{
    return (uint8_t)(pane_x() + 1);
}

static uint8_t text_w(void)
{
    return (uint8_t)(SCR_COLS - 2 - pane_x());
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
    return agent_name(cur);
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

/* Start a body row: clear it, and draw the list's cell and the divider. */
static void row_begin(uint8_t row)
{
    uint8_t a = (uint8_t)(row - FIRST_ROW), bar, n;
    char num[6];

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
    } else if (agent_unread(a)) {
        linebuf[0] = has_mousetext ? MT_DIAMOND : sc_char('*', bar);
    }
    n = txt_len(agent_name(a));
    lb_textn(1, agent_name(a), n > NAME_MAX ? NAME_MAX : n, bar);
    if ((n = agent_unread(a)) != 0 && a != cur) {
        fmt_u32(num, n);
        lb_text((uint8_t)(LIST_W - 1 - txt_len(num)), num, bar);
    }
}

/* What the heading says at the right of the pane. */
static const char *head_note(void)
{
    static char note[24];
    uint8_t a, u = 0;

    if (!list_on) {
        for (a = 0; a < agent_count(); ++a) {
            if (a != cur) {
                u = (uint8_t)(u + agent_unread(a));
            }
        }
        if (u) {
            fmt_str(fmt_u32(note, u), " new elsewhere");
            return note;
        }
    }
    if (scroll) {
        return "scrolled back";
    }
    if (lay_dropped) {
        return "older not shown";
    }
    return "";
}

static void draw_head(void)
{
    const char *note = head_note();

    lb_clear(SC_SPACE);
    if (list_on) {
        lb_text(1, "AGENTS", 0);
    }
    lb_text(text_x(), cur_name(), 0);
    lb_text((uint8_t)(SCR_COLS - 1 - txt_len(note)), note, 0);
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
        case 4: return "No messages with this agent yet. Sending comes in milestone 5.";
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
    uint8_t n = msg_rows(), r;
    uint16_t first, i;

    if (scroll > max_scroll()) {
        scroll = max_scroll();
    }
    /* Newest at the foot once the conversation fills the pane; until
     * then, from the top. */
    first = lay_count > n ? (uint16_t)(lay_count - n - scroll) : 0;
    for (r = 0; r < n; ++r) {
        row_begin((uint8_t)(FIRST_ROW + r));
        i = (uint16_t)(first + r);
        if (i < lay_count) {
            draw_line(&lay_lines[i]);
        } else if (!lay_count) {
            lb_text(text_x(), pane_note(r), 0);
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

/* Fetch the newest page of the current agent's conversation. */
static void load_current(void)
{
    uint8_t rc;

    if (msgs_now != MSGS_LIVE || !agent_count()) {
        return;
    }
    status_msg("FETCHING MESSAGES");
    link_begin();
    rc = hist_load(agent_name(cur));
    if (rc) {
        status_msg(ses_strerror(rc));
    }
}

static void open_agent(uint8_t a)
{
    cur = a;
    sel = a;
    msg_select(a);
    load_current();
    win_end = -1;
    scroll = 0;
    relayout();
    draw_all();
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
    static char reply[96];
    uint16_t i = msg_count();
    const char *p;
    uint16_t n;

    if (!clen) {
        return;
    }
    if (msgs_now != MSGS_DEMO) {
        status_msg("SENDING COMES IN MILESTONE 5");
        return;
    }
    fmt_str(fmt_u32(fmt_str(reply,
        "Milestone 0 has no gateway yet, so nothing was sent. You wrote "),
        clen), " characters.");
    if (!msg_add(1, "now", cbuf, clen) || !msg_add(0, "demo", reply, txt_len(reply))) {
        status_msg("NO ROOM: MILESTONE 0 KEEPS ONLY A FEW MESSAGES");
        return;
    }
    for (; i < msg_count(); ++i) {
        p = msg_text(i, &n);
        lay_msg(i, p, n, msg_mine(i), txt_len(msg_meta(i)));
    }
    clen = 0;
    cbuf[0] = 0;
    scroll = 0;
    crows = compose_rows();
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
    uint8_t rc;

    if (!lay_count) {
        return;
    }
    top = lay_lines[0].msg;
    last = msg_on_row((uint8_t)(msg_rows() - 1));
    if (top == 0) {
        if (msgs_now != MSGS_LIVE || !st_older) {
            status_msg("THE START OF THE CONVERSATION");
            return;
        }
        status_msg("FETCHING OLDER MESSAGES");
        st_shift = 0;
        link_begin();
        rc = hist_older(agent_name(cur));
        if (rc) {
            status_msg(ses_strerror(rc));
            return;
        }
        if (!hist_added) {
            status_msg("THE START OF THE CONVERSATION");
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
    uint8_t rc;

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
        rc = hist_newer(agent_name(cur));
        if (rc) {
            status_msg(ses_strerror(rc));
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
        cur = sel = 0;
        msg_select(0);
        load_current();
        win_end = -1;
        scroll = 0;
        relayout();
        draw_all();
        if (!link_live && msgs_now != MSGS_DEMO) {
            status_msg(link_err);
        }
        break;
    case 'r': case 'R':
        if (msgs_now == MSGS_DEMO) {
            status_msg("NO COGS CARD: NOTHING TO CONNECT TO");
            break;
        }
        link_connect();
        cur = sel = 0;
        msg_select(0);
        load_current();
        win_end = -1;
        scroll = 0;
        relayout();
        draw_all();
        if (!link_live) {
            status_msg(link_err);
        }
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
        /* the oldest held message (older ones: keep going up) */
        relayout_from_start();
        scroll = max_scroll();
        draw_all();
        break;
    case '>': case '.':
        /* the newest: from the gateway if it has newer than held */
        if (msgs_now == MSGS_LIVE && !st_newest) {
            load_current();
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
        msg_select(cur);
        load_current();
        relayout();
    }
    draw_all();
    if (!link_live && link_err[0]) {
        status_msg(link_err);
    }
    for (;;) {
        k = key_get();
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
