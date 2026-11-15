/*
 * Laying out a conversation: messages in, a table of screen lines out.
 *
 * Each message is word-wrapped to its side's width and placed: the
 * agent's at the left of the pane, yours at the right, with the time on
 * a line of its own under it and a blank line between messages. Lines
 * hold a message number and an offset rather than a pointer, so the text
 * can live anywhere (aux memory, from milestone 4) and is copied out only
 * when a line is drawn.
 *
 * In the text, '\n' ends a line. Leading spaces are kept (code), a line
 * breaks at the last space that fits, and a word wider than the bubble is
 * split. When the table fills, the oldest messages are dropped from it.
 * Plain C, tested on the Mac.
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include "port.h"

#define LAY_MAX     200             /* lines in the table */
#define LAY_META    8               /* longest time or note under a message */

#define LK_TEXT     0
#define LK_META     1               /* the time under a message */
#define LK_BLANK    2               /* between messages */

typedef struct {
    uint16_t msg;                   /* the message's number */
    uint16_t off;                   /* LK_TEXT: where the line starts in it */
    uint8_t  len;
    uint8_t  col;                   /* from the left of the text area */
    uint8_t  kind;
    uint8_t  mine;
} lay_line_t;

extern lay_line_t lay_lines[LAY_MAX];
extern uint16_t   lay_count;
extern uint8_t    lay_width;        /* columns in the text area */
extern uint8_t    lay_dropped;      /* older messages were dropped */
extern uint8_t    lay_cut;          /* one message was too long to show whole */

/* Empty the table and set the text area's width (20..78). */
void lay_reset(uint8_t width);

/* How wide a bubble may be on each side, at the current width. */
uint8_t lay_maxw(uint8_t mine);

/* Add message msg: n bytes of text, from you (mine) or the agent, with a
 * time or note of metalen characters (0..LAY_META) under it. */
void lay_msg(uint16_t msg, const char *text, uint16_t n, uint8_t mine, uint8_t metalen);

#endif
