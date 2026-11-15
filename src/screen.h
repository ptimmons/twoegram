/*
 * The 80-column text screen, written directly rather than through COUT.
 *
 * Everything is drawn a row at a time: C fills linebuf with 80 screen codes,
 * then scr_blit() copies it to a screen row, splitting even columns into the
 * auxiliary half and odd columns into the main half.
 */
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include "port.h"

#define SCR_COLS    80
#define SCR_ROWS    24
#define STATUS_ROW  23          /* the inverse status bar */
#define BODY_ROWS   23          /* rows 0..22 belong to the current view */

/* Screen codes, with the alternate character set switched on. */
#define SC_SPACE    0xA0        /* normal space */
#define SC_ISPACE   0x20        /* inverse space: the status bar's fill */
#define SC_UNDER    0xDF        /* normal '_', a stroke along the cell's foot */
#define SC_DASH     0xAD        /* normal '-' */
#define SC_BAR      0xFC        /* normal '|' */

/* MouseText, Enhanced //e and later only. Codes probed on ZipEdit, not
 * recalled: there are no corner glyphs, so boxes meet by placement. */
#define MT_OAPPLE   0x41        /* open apple; inverse 'A' on an older //e */
#define MT_RULE     0x4C        /* stroke along the TOP of the cell */
#define MT_VERT     0x5F        /* stroke down the LEFT edge of the cell */
#define MT_VERT_R   0x5A        /* stroke down the RIGHT edge of the cell */
#define MT_DIAMOND  0x5B

/* In strings, this byte draws the Open Apple glyph. */
#define OA          "\x01"

extern uint8_t linebuf[SCR_COLS];
extern uint8_t has_mousetext;

void scr_init(void);                    /* blit.s */
void scr_done(void);                    /* blit.s */
void __fastcall__ scr_blit(uint8_t row);/* blit.s */

uint8_t __fastcall__ sc_char(char c, uint8_t inverse);
void __fastcall__ lb_clear(uint8_t code);
void __fastcall__ lb_fill(uint8_t col, uint8_t n, uint8_t code);
uint8_t lb_text(uint8_t col, const char *s, uint8_t inverse);
uint8_t lb_textn(uint8_t col, const char *s, uint8_t n, uint8_t inverse);
uint8_t lb_center(const char *s, uint8_t inverse);
uint8_t txt_len(const char *s);
void scr_clear(uint8_t from, uint8_t to);

#endif
