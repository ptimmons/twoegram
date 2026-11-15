/*
 * The card commands Twoegram adds to Newsiie's: one HTTP request with its
 * whole reply (reference 4.6), and the prefs blob (4.2.5).
 */
#ifndef COGS_HTTP_H
#define COGS_HTTP_H

#include "cogs.h"

#define COGS_HTTP_KEEPALIVE 0x08    /* park the connection for the next request */

#define A2_SOCK             0       /* the socket Twoegram uses, as the reference's examples do */

/*
 * Make one HTTP request on socket A2_SOCK and collect the body from the
 * DATA events into buf. hdrs is extra header lines, separated by CR LF
 * (none after the last), or "". Returns 0 with the HTTP status in
 * *status and the body's length in *got, or a card code (the card ends a
 * failed request with an ERROR event), COGS_E_TOOBIG if the body did not
 * fit cap (the rest is read and dropped), or a COGS_E_* code. The card
 * delivers the body only after the response is complete, so no disk call
 * can come between.
 */
uint8_t cogs_http(const char *method, const char *url, const char *hdrs,
                  const uint8_t *body, uint16_t nbody, uint8_t flags,
                  uint8_t *buf, uint16_t cap, uint16_t *got, uint16_t *status);

/* The 32-byte prefs blob the card keeps in flash for host software. The
 * card reads bytes 0..14; 15..31 are free for programs. */
#define COGS_PREFS_MAX      32
#define COGS_PREFS_FREE     15

/* *n is how many bytes are stored: 0 when nothing ever was. */
uint8_t cogs_prefs_get(uint8_t blob[COGS_PREFS_MAX], uint8_t *n);
uint8_t cogs_prefs_set(const uint8_t *blob, uint8_t n);

#endif
