/*
 * HMAC-SHA256 on the card's hash. See hmac.h.
 */
#include <string.h>
#include "hmac.h"

#define OP_HASH     0x30

static uint8_t pre[3];
static uint8_t pad[64];
static cogs_part_t all[HMAC_PARTS_MAX + 2];

/* Hash pre-set parts all[1..n-1] behind a HASH header in all[0]. */
static uint8_t hash_all(uint8_t n, uint8_t digest[32])
{
    uint16_t len = 0, got;
    uint8_t i, rc;

    for (i = 1; i < n; ++i) {
        len = (uint16_t)(len + all[i].n);
    }
    if (len > HASH_MAX) {
        return COGS_E_TOOBIG;
    }
    pre[0] = 0;                     /* single SHA-256 */
    pre[1] = (uint8_t)len;
    pre[2] = (uint8_t)(len >> 8);
    all[0].p = pre;
    all[0].n = 3;
    rc = cogs_send(OP_HASH, all, n);
    if (!rc) {
        rc = cogs_reply(OP_HASH, digest, 32, &got);
    }
    if (!rc && got != 32) {
        rc = COGS_E_PROTO;
    }
    return rc;
}

uint8_t sha256_parts(const cogs_part_t *parts, uint8_t nparts, uint8_t digest[32])
{
    if (nparts > HMAC_PARTS_MAX + 1) {
        return COGS_E_TOOBIG;
    }
    memcpy(all + 1, parts, nparts * sizeof *parts);
    return hash_all((uint8_t)(nparts + 1), digest);
}

static void make_pad(const uint8_t *key, uint8_t klen, uint8_t x)
{
    uint8_t i;

    memset(pad, x, sizeof pad);
    for (i = 0; i < klen; ++i) {
        pad[i] ^= key[i];
    }
}

uint8_t hmac_parts(const uint8_t *key, uint8_t klen,
                   const cogs_part_t *parts, uint8_t nparts, uint8_t mac[32])
{
    uint8_t rc;

    if (klen > 64 || nparts > HMAC_PARTS_MAX) {
        return COGS_E_TOOBIG;
    }
    make_pad(key, klen, 0x36);
    all[1].p = pad;
    all[1].n = 64;
    memcpy(all + 2, parts, nparts * sizeof *parts);
    rc = hash_all((uint8_t)(nparts + 2), mac);
    if (rc) {
        return rc;
    }
    make_pad(key, klen, 0x5C);
    all[2].p = mac;                 /* the card has read it before replying */
    all[2].n = 32;
    return hash_all(3, mac);
}
