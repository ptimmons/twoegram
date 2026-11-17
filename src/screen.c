/*
 * Building rows of screen codes in linebuf. See screen.h.
 */
#include <string.h>
#include "screen.h"

uint8_t linebuf[SCR_COLS];
uint8_t has_mousetext;

/*
 * The screen code for an ASCII character, normal or inverse, with the
 * alternate character set on:
 *   normal   $A0-$FF   the character with bit 7 set
 *   inverse  $00-$1F   @ A-Z [ \ ] ^ _
 *            $20-$3F   space, punctuation, digits
 *            $60-$7F   lowercase
 * $40-$5F is MouseText, so inverse capitals have to move down to $00.
 * The glyph bytes 1-5 (screen.h) are MouseText whatever the attribute.
 */
static const uint8_t glyph_mt[5]   = { MT_OAPPLE, MT_CHECK, MT_DOWN, MT_DIAMOND, MT_UP };
static const char    glyph_flat[5] = { 0, '+', 'v', '*', '^' };

uint8_t __fastcall__ sc_char(char c, uint8_t inverse)
{
    uint8_t b = (uint8_t)c & 0x7F;

    if (b >= 0x01 && b <= 0x05) {
        if (b == 0x01 || has_mousetext) {
            return glyph_mt[b - 1];
        }
        b = (uint8_t)glyph_flat[b - 1];
    } else if (b < 0x20) {
        b = '?';
    }
    if (!inverse) {
        return (uint8_t)(b | 0x80);
    }
    if (b >= 0x40 && b < 0x60) {
        return (uint8_t)(b - 0x40);
    }
    return b;
}

void __fastcall__ lb_clear(uint8_t code)
{
    memset(linebuf, code, SCR_COLS);
}

void __fastcall__ lb_fill(uint8_t col, uint8_t n, uint8_t code)
{
    if (col >= SCR_COLS) {
        return;
    }
    if (n > SCR_COLS - col) {
        n = SCR_COLS - col;
    }
    memset(linebuf + col, code, n);
}

/* Write s at col, clipped at the right edge. Returns the column after it. */
uint8_t lb_text(uint8_t col, const char *s, uint8_t inverse)
{
    while (*s && col < SCR_COLS) {
        linebuf[col++] = sc_char(*s++, inverse);
    }
    return col;
}

/* As lb_text, for n bytes that need not end in a NUL. */
uint8_t lb_textn(uint8_t col, const char *s, uint8_t n, uint8_t inverse)
{
    while (n-- && col < SCR_COLS) {
        linebuf[col++] = sc_char(*s++, inverse);
    }
    return col;
}

uint8_t txt_len(const char *s)
{
    return (uint8_t)strlen(s);
}

uint8_t lb_center(const char *s, uint8_t inverse)
{
    uint8_t n = txt_len(s);

    return lb_text(n >= SCR_COLS ? 0 : (uint8_t)((SCR_COLS - n) / 2), s, inverse);
}

/* Blank rows from..to inclusive. */
void scr_clear(uint8_t from, uint8_t to)
{
    lb_clear(SC_SPACE);
    while (from <= to) {
        scr_blit(from++);
    }
}
