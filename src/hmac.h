/*
 * SHA-256 and HMAC-SHA256, with the card doing the hashing (HASH, $30).
 *
 * HMAC is two hashes: H((K ^ ipad) || message), then H((K ^ opad) ||
 * inner). The Apple only pads and XORs the key; the message goes to the
 * card in pieces, never copied together. One HASH takes at most 2,045
 * bytes, so a message may be at most 1,981 bytes (2,045 less the 64-byte
 * padded key). Portable C over cogs.c, tested on the Mac against a model
 * card that really hashes.
 */
#ifndef HMAC_H
#define HMAC_H

#include "cogs.h"

#define HASH_MAX        2045        /* bytes in one HASH request */
#define HMAC_MSG_MAX    (HASH_MAX - 64)
#define HMAC_PARTS_MAX  6

/* SHA-256 of the parts, joined. */
uint8_t sha256_parts(const cogs_part_t *parts, uint8_t nparts, uint8_t digest[32]);

/* HMAC-SHA256 of the parts, joined, under a key of at most 64 bytes.
 * COGS_E_TOOBIG if the message is too long for one HASH. */
uint8_t hmac_parts(const uint8_t *key, uint8_t klen,
                   const cogs_part_t *parts, uint8_t nparts, uint8_t mac[32]);

#endif
