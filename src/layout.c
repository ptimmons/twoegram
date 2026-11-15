/*
 * Laying out a conversation. See layout.h.
 */
#include <string.h>
#include "layout.h"

lay_line_t lay_lines[LAY_MAX];
uint16_t   lay_count;
uint8_t    lay_width;
uint8_t    lay_dropped;
uint8_t    lay_cut;

/* The message being laid out, for wrap()'s emitting pass. */
static const char *base;
static uint16_t    cur_msg;
static uint8_t     cur_mine;
static uint8_t     emit;            /* 0: count lines only */
static uint8_t     blockw;          /* the widest line emitted */
static uint16_t    nlines;

void lay_reset(uint8_t width)
{
    lay_count = 0;
    lay_width = width;
    lay_dropped = 0;
    lay_cut = 0;
}

uint8_t lay_maxw(uint8_t mine)
{
    /* Yours take two thirds of the pane; the agent's nearly all of it,
     * since its answers are long and may hold code. */
    return mine ? (uint8_t)(lay_width * 2 / 3) : (uint8_t)(lay_width - 3);
}

static void add(uint16_t off, uint8_t len, uint8_t kind)
{
    lay_line_t *l;

    ++nlines;
    if (!emit) {
        return;
    }
    if (lay_count >= LAY_MAX - 1) {     /* keep a line for the time */
        if (kind == LK_TEXT) {
            lay_cut = 1;
        }
        if (kind != LK_META || lay_count >= LAY_MAX) {
            return;
        }
    }
    l = &lay_lines[lay_count++];
    l->msg = cur_msg;
    l->off = off;
    l->len = len;
    l->col = 0;
    l->kind = kind;
    l->mine = cur_mine;
    if (kind == LK_TEXT && len > blockw) {
        blockw = len;
    }
}

/* A line of text, without its trailing spaces. */
static void text_line(const char *s, uint8_t len)
{
    while (len && s[len - 1] == ' ') {
        --len;
    }
    add((uint16_t)(s - base), len, LK_TEXT);
}

/* One hard line (no '\n' inside), wrapped to maxw. */
static void wrap_line(const char *s, uint16_t n, uint8_t maxw)
{
    uint16_t k, indent;

    if (!n) {
        text_line(s, 0);
        return;
    }
    for (indent = 0; indent < n && s[indent] == ' '; ++indent) {
    }
    while (n > maxw) {
        /* The last space at or before the width, past any indent: a
         * space exactly at the width still lets the line use every
         * column. */
        for (k = maxw; k > indent && s[k] != ' '; --k) {
        }
        if (k > indent) {
            text_line(s, (uint8_t)k);
            s += k + 1;
            n -= k + 1;
        } else {
            text_line(s, maxw);         /* a word wider than the bubble */
            s += maxw;
            n -= maxw;
        }
        indent = 0;
    }
    if (n) {
        text_line(s, (uint8_t)n);
    }
}

static void wrap(const char *s, uint16_t n, uint8_t maxw)
{
    uint16_t k;

    for (;;) {
        for (k = 0; k < n && s[k] != '\n'; ++k) {
        }
        wrap_line(s, k, maxw);
        if (k >= n) {
            return;
        }
        s += k + 1;
        n -= k + 1;
    }
}

/* Drop the oldest message's lines, and the blank after them. */
static void drop_oldest(void)
{
    uint16_t m = lay_lines[0].msg, k = 0;

    while (k < lay_count && lay_lines[k].msg == m) {
        ++k;
    }
    while (k < lay_count && lay_lines[k].kind == LK_BLANK) {
        ++k;
    }
    lay_count -= k;
    memmove(lay_lines, lay_lines + k, lay_count * sizeof lay_lines[0]);
    lay_dropped = 1;
}

void lay_msg(uint16_t msg, const char *text, uint16_t n, uint8_t mine, uint8_t metalen)
{
    uint8_t maxw = lay_maxw(mine);
    uint16_t need, start, i;
    uint8_t col;

    if (metalen > LAY_META) {
        metalen = LAY_META;
    }
    base = text;
    cur_msg = msg;
    cur_mine = mine;

    /* First count, to make room by dropping whole old messages. */
    emit = 0;
    nlines = 0;
    wrap(text, n, maxw);
    need = (uint16_t)(nlines + 1 + (lay_count ? 1 : 0));
    while (lay_count && LAY_MAX - lay_count < need) {
        drop_oldest();
    }

    emit = 1;
    blockw = 0;
    if (lay_count) {
        add(0, 0, LK_BLANK);
    }
    start = lay_count;
    wrap(text, n, maxw);
    if (metalen > blockw) {
        blockw = metalen;
    }
    /* Yours: the bubble's lines start in one column, so the block sits
     * against the right edge and its text still reads from the left. */
    col = mine ? (uint8_t)(lay_width - blockw) : 0;
    for (i = start; i < lay_count; ++i) {
        lay_lines[i].col = col;
    }
    i = lay_count;
    add(0, metalen, LK_META);
    if (mine && lay_count > i) {
        lay_lines[i].col = (uint8_t)(lay_width - metalen);
    }
}
