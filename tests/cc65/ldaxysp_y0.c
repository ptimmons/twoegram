/*
 * A reproduction of the cc65 miscompile tools/cc65check.py guards against:
 * store_head() as it was before the workaround in src/store.c.
 *
 * With the cc65 from Homebrew's 2.19 bottle (it reports V2.18), compiled as
 *
 *     cl65 -t apple2enh -Or -Cl -S -o /dev/stdout tests/cc65/ldaxysp_y0.c
 *
 * the reload of h for the first memcpy() is "ldy #$00 ... jsr ldaxysp".
 * ldaxysp takes the high byte at offset Y and the low byte at Y - 1, so with
 * Y = 0 the pointer is built from the wrong bytes. To test another cc65:
 * compile this to assembly and run tools/cc65check.py on it; it exits 0 if
 * that compiler gets it right.
 */
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t  flags;
    uint16_t hash;
    char     title[161];
    char     date[17];
} head_t;

uint8_t b[183];

void front(uint16_t i);

void store_head(uint16_t i, head_t *h)
{
    uint8_t tl, dl;

    front(i);
    h->flags = b[2];
    h->hash = (uint16_t)(b[3] | (b[4] << 8));
    tl = b[5];
    memcpy(h->title, b + 6, tl);
    h->title[tl] = 0;
    dl = b[6 + tl];
    memcpy(h->date, b + 7 + tl, dl);
    h->date[dl] = 0;
}
