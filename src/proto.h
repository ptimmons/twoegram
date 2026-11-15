/*
 * The Twoegram protocol's bytes (docs/PROTOCOL.md), with no I/O: the
 * device key's written form, hex, and reading a reply. Plain C, tested on
 * the Mac.
 */
#ifndef PROTO_H
#define PROTO_H

#include "port.h"

#define A2_KEY_LEN      16
#define A2_MAC_LEN      32
#define A2_PAYLOAD_MAX  1900

/* Reply codes (PROTOCOL.md section 4). */
#define A2_OK               0
#define A2_BAD_REQUEST      1
#define A2_UNKNOWN_DEVICE   2
#define A2_BAD_SIGNATURE    3
#define A2_NO_SESSION       4
#define A2_REPLAY           5
#define A2_LOCKED           6
#define A2_UNKNOWN_AGENT    7
#define A2_TOO_LARGE        8
#define A2_BUSY             9

/* The written key: 0 if it parsed and checked, A2_KEY_BADCHAR for a
 * wrong length or a character that cannot be in a key, A2_KEY_BADCHECK
 * for a typing slip. Hyphens and spaces are ignored, case does not
 * matter, O reads as 0 and I and L as 1. */
#define A2_KEY_BADCHAR  1
#define A2_KEY_BADCHECK 2
uint8_t a2_key_parse(const char *s, uint8_t key[A2_KEY_LEN]);

/* n bytes as 2n lowercase hex digits, NUL-terminated; returns the NUL. */
char *a2_hex(char *dst, const uint8_t *src, uint8_t n);
/* 2n hex digits to n bytes; 0 if they all were hex. */
uint8_t a2_unhex(uint8_t *dst, const char *src, uint8_t n);

/* A reply's header line. */
typedef struct {
    uint8_t        code;
    uint8_t        signed_;         /* it carried a signature */
    uint8_t        mac[A2_MAC_LEN];
    const uint8_t *payload;
    uint16_t       plen;
} a2_reply_t;

/* 0 if buf holds a well-formed header line ("A2 1 <code> <mac|->"). */
uint8_t a2_reply(const uint8_t *buf, uint16_t n, a2_reply_t *r);

/* One payload item: a "word value" line, or a message record. */
#define A2_ITEM_LINE    1
#define A2_ITEM_MSG     2

typedef struct {
    uint8_t     kind;
    const char *word;               /* A2_ITEM_LINE */
    uint8_t     wlen;
    const char *val;
    uint8_t     vlen;
    uint16_t    n;                  /* A2_ITEM_MSG */
    uint8_t     mine;
    char        time[6];
    const char *text;
    uint16_t    tlen;
} a2_item_t;

typedef struct {
    const uint8_t *p, *end;
} a2_iter_t;

void a2_iter(a2_iter_t *it, const a2_reply_t *r);

/* 1 with the next item, 0 at the end, A2_ITER_BAD if the payload is
 * malformed (a reply that was signed never is). */
#define A2_ITER_BAD     0xFF
uint8_t a2_next(a2_iter_t *it, a2_item_t *item);

/* Does an item's word equal w? */
uint8_t a2_is(const a2_item_t *item, const char *w);

/* A line's value as a number; 0xFFFF if it is not one. */
uint16_t a2_num(const char *s, uint8_t n);

#endif
