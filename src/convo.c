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

/* ---- the conversation's lines ---- */

static void relayout(void)
{
    uint16_t i, n, cnt = msg_count();
    const char *p;

    lay_reset(text_w());
    for (i = 0; i < cnt; ++i) {
        p = msg_text(i, &n);
        lay_msg(i, p, n, msg_mine(i), txt_len(msg_meta(i)));
    }
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
    lb_text(text_x(), agent_name(cur), 0);
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
    status_main(agent_name(cur), SEC_DEMO);
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

static void open_agent(uint8_t a)
{
    cur = a;
    sel = a;
    msg_select(a);
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
    case 'n': case 'N':
        next_unread();
        break;
    case K_UP:
        scroll_by((int16_t)(msg_rows() - 1));
        break;
    case K_DOWN:
        scroll_by((int16_t)-(msg_rows() - 1));
        break;
    case '<': case ',':
        scroll_by(0x7FFF);
        break;
    case '>': case '.':
        scroll_by(-0x7FFF);
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
        scroll_by(1);
        break;
    case K_DOWN:
        scroll_by(-1);
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
    uint8_t k;

    msg_select(cur);
    relayout();
    draw_all();
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
