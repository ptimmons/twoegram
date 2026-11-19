/*
 * The help screen: a MouseText box over the body rows, two columns of keys.
 *
 * MouseText has no corners, so the box closes by placement (Newsiie's
 * box, measured there):
 *   top     normal '_' along the FOOT of the row above the box, so it meets
 *           the verticals where they start
 *   sides   $5A on the left, a stroke down the RIGHT edge of its cell, and
 *           $5F on the right, a stroke down the LEFT edge, so both land on
 *           the edges of the interior and every rule inside meets them
 *   rules   $4C, a stroke along the TOP of its cell, under the title and
 *           as the bottom edge
 * Without MouseText (an unenhanced //e) the sides are '|' and the rules '-'.
 */
#include "screen.h"
#include "ui.h"
#include "build.h"

/* This file's code runs from the language card ($D400-$DFFF, bank 2),
 * which cc65 fills at startup: main memory is full (PLAN.md, milestone 4
 * notes). Code there must not call ProDOS or the ROM; ProDOS restores the
 * language card when it returns to the main code that calls it. */
#ifdef __CC65__
#pragma code-name ("LC")
#endif

#define BOX_L       7           /* column of the left side */
#define BOX_R       72          /* column of the right side */
#define TOP_ROW     0           /* the '_' row */
#define TITLE_ROW   1
#define SEP_ROW     2
#define FIRST_ROW   3

#define IN_L        (BOX_L + 1) /* first interior column */
#define IN_W        (BOX_R - BOX_L - 1)

#define COL1_KEY    (BOX_L + 3)
#define COL1_DESC   (BOX_L + 14)
#define COL2_KEY    (BOX_L + 34)
#define COL2_DESC   (BOX_L + 45)

/* One row of the two columns. A NULL description marks a heading. */
struct help_row {
    const char *lkey, *ldesc, *rkey, *rdesc;
};

static const struct help_row rows[] = {
    { "CONVERSATION", 0,               "AGENTS",     0 },
    { "Type",       "write a message", "Up Down",    "choose" },
    { "Return",     "send it",         "Return",     "open" },
    { "Delete",     "erase",           "Tab Esc",    "back to the chat" },
    { "Up Down",    "scroll",          "",           "" },
    { OA "-Up Dn",  "page",            "ANYWHERE",   0 },
    { OA "-< >",    "start, newest",   OA "-B",      "hide, show agents" },
    { "Tab",        "to the agents",   OA "-N",      "next with news" },
    { OA "-S",      "save to disk",    OA "-K",      "settings" },
    { "",           "",                OA "-R",      "connect again" },
    { "",           "",                OA "-L",      "lock" },
    { "",           "",                OA "-?",      "this help" },
    { "",           "",                OA "-Q",      "quit" },
};

#define NROWS   (sizeof rows / sizeof rows[0])

static void sides(void)
{
    lb_clear(SC_SPACE);
    linebuf[BOX_L] = has_mousetext ? MT_VERT_R : SC_BAR;
    linebuf[BOX_R] = has_mousetext ? MT_VERT : SC_BAR;
}

static void column(uint8_t kcol, uint8_t dcol, const char *k, const char *d)
{
    if (!d) {
        lb_text(kcol, k, 0);        /* heading */
        return;
    }
    lb_text(kcol + 1, k, 0);
    lb_text(dcol, d, 0);
}

void help_show(void)
{
    uint8_t rule = has_mousetext ? MT_RULE : SC_DASH;
    uint8_t row, i;

    lb_clear(SC_SPACE);
    lb_fill(IN_L, IN_W, SC_UNDER);
    scr_blit(TOP_ROW);

    sides();
    lb_center("TWOEGRAM " TWOEGRAM_VERSION " " BUILD_ID "  --  KEYBOARD COMMANDS", 0);
    scr_blit(TITLE_ROW);

    sides();
    lb_fill(IN_L, IN_W, rule);
    scr_blit(SEP_ROW);

    row = FIRST_ROW;
    for (i = 0; i < NROWS; ++i) {
        sides();
        column(COL1_KEY, COL1_DESC, rows[i].lkey, rows[i].ldesc);
        column(COL2_KEY, COL2_DESC, rows[i].rkey, rows[i].rdesc);
        scr_blit(row++);
    }

    sides();
    scr_blit(row++);
    sides();
    lb_text(COL1_KEY, "\x04 something new there      \x05 \x03 more above, more below", 0);
    scr_blit(row++);
    sides();
    lb_text(COL1_KEY, "press any key to return", 0);
    scr_blit(row++);

    lb_clear(SC_SPACE);
    lb_fill(IN_L, IN_W, rule);
    scr_blit(row++);

    scr_clear(row, BODY_ROWS - 1);
}
