/*
 * Card commands only the probe uses: PING, LOCALTIME, RANDOM and HASH.
 * Kept out of cogs.c so the app does not carry them: its main memory is
 * full (PLAN.md section 5).
 */
#include <stddef.h>
#include "cogs.h"

#define OP_PING         0x00
#define OP_LOCALTIME    0x0F
#define OP_HASH         0x30
#define OP_RANDOM       0x31

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t cogs_ping(cogs_ping_t *p)
{
    uint8_t r[4];
    uint16_t got;
    uint8_t rc = cogs_request(OP_PING, NULL, 0, NULL, 0, r, sizeof r, &got);

    if (rc) {
        return rc;
    }
    if (got < 4) {
        return COGS_E_PROTO;
    }
    p->fw_major = r[0];
    p->fw_minor = r[1];
    p->proto = r[2];
    p->caps = r[3];
    return COGS_OK;
}

uint8_t cogs_localtime(uint8_t *valid, uint32_t *secs_1904)
{
    uint8_t r[6];
    uint16_t got;
    uint8_t rc = cogs_request(OP_LOCALTIME, NULL, 0, NULL, 0, r, sizeof r, &got);

    if (rc) {
        return rc;
    }
    if (got < 6) {
        return COGS_E_PROTO;
    }
    *valid = r[0];
    *secs_1904 = le32(r + 2);
    return COGS_OK;
}

uint8_t cogs_random(uint8_t *dst, uint8_t n)
{
    uint16_t got;
    uint8_t rc = cogs_request(OP_RANDOM, &n, 1, NULL, 0, dst, n, &got);

    if (rc) {
        return rc;
    }
    return got == n ? COGS_OK : COGS_E_PROTO;
}

uint8_t cogs_sha256(const uint8_t *src, uint16_t n, uint8_t digest[32])
{
    uint8_t pre[3];
    uint16_t got;
    uint8_t rc;

    pre[0] = 0;                     /* algo: single SHA-256 */
    pre[1] = (uint8_t)n;
    pre[2] = (uint8_t)(n >> 8);
    rc = cogs_request(OP_HASH, pre, 3, src, n, digest, 32, &got);
    if (rc) {
        return rc;
    }
    return got == 32 ? COGS_OK : COGS_E_PROTO;
}
