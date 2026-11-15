/*
 * HTTP and PREFS on the card. See cogs_http.h.
 *
 * HTTP ($20) has no reply of its own: it ends with HTTP_DONE ($95), or an
 * ERROR ($93) on its socket, with the body in DATA events ($91) ahead of
 * it. Without the STREAM flag the card sends those only once the response
 * is complete, so they are all queued when the first arrives and the
 * caller's buffer is filled in one pass.
 */
#include <string.h>
#include "cogs_http.h"

#define OP_PREFS_GET    0x06
#define OP_PREFS_SET    0x07
#define OP_HTTP         0x20

#define EV_DATA         0x91
#define EV_ERROR        0x93
#define EV_HTTP_DONE    0x95
#define SOCK_NONE       0xFF

/* Frames that are not ours to wait through before giving up. */
#define MAX_OTHER       64

uint8_t cogs_http(const char *method, const char *url, const char *hdrs,
                  const uint8_t *body, uint16_t nbody, uint8_t flags,
                  uint8_t *buf, uint16_t cap, uint16_t *got, uint16_t *status)
{
    static uint8_t a[3], ul, hl[2], bl[2], ev[7];
    static cogs_part_t parts[8];
    uint16_t len, hn = (uint16_t)strlen(hdrs);
    uint8_t op, rc, other = 0, over = 0;

    *got = 0;
    *status = 0;
    a[0] = A2_SOCK;
    a[1] = flags;
    a[2] = (uint8_t)strlen(method);
    ul = (uint8_t)strlen(url);
    hl[0] = (uint8_t)hn;
    hl[1] = (uint8_t)(hn >> 8);
    bl[0] = (uint8_t)nbody;
    bl[1] = (uint8_t)(nbody >> 8);
    parts[0].p = a;                         parts[0].n = 3;
    parts[1].p = (const uint8_t *)method;   parts[1].n = a[2];
    parts[2].p = &ul;                       parts[2].n = 1;
    parts[3].p = (const uint8_t *)url;      parts[3].n = ul;
    parts[4].p = hl;                        parts[4].n = 2;
    parts[5].p = (const uint8_t *)hdrs;     parts[5].n = hn;
    parts[6].p = bl;                        parts[6].n = 2;
    parts[7].p = body;                      parts[7].n = nbody;
    rc = cogs_send(OP_HTTP, parts, 8);
    if (rc) {
        return rc;
    }

    for (;;) {
        rc = cogs_next(&op, &len);
        if (rc) {
            return rc;
        }
        if (op == EV_DATA && len >= 1) {
            if ((rc = cogs_read(ev, 1)) != 0) {
                return rc;
            }
            --len;
            if (ev[0] != A2_SOCK) {
                rc = cogs_drop(len);
            } else if (over || len > cap - *got) {
                over = 1;
                rc = cogs_drop(len);
            } else {
                rc = cogs_read(buf + *got, len);
                *got = (uint16_t)(*got + len);
            }
            if (rc) {
                return rc;
            }
            continue;
        }
        if (op == EV_HTTP_DONE && len >= 7) {
            if ((rc = cogs_read(ev, 7)) != 0 || (rc = cogs_drop(len - 7)) != 0) {
                return rc;
            }
            if (ev[0] != A2_SOCK) {
                continue;
            }
            *status = (uint16_t)(ev[1] | ((uint16_t)ev[2] << 8));
            return over ? COGS_E_TOOBIG : COGS_OK;
        }
        if (op == EV_ERROR && len >= 2) {
            if ((rc = cogs_read(ev, 2)) != 0 || (rc = cogs_drop(len - 2)) != 0) {
                return rc;
            }
            if (ev[0] == A2_SOCK || ev[0] == SOCK_NONE) {
                return ev[1] ? ev[1] : COGS_E_PROTO;
            }
            continue;
        }
        if ((rc = cogs_drop(len)) != 0) {
            return rc;
        }
        if (++other >= MAX_OTHER) {
            return COGS_E_PROTO;
        }
    }
}

uint8_t cogs_prefs_get(uint8_t blob[COGS_PREFS_MAX], uint8_t *n)
{
    static uint8_t r[1 + COGS_PREFS_MAX];
    uint16_t got;
    uint8_t rc = cogs_request(OP_PREFS_GET, NULL, 0, NULL, 0, r, sizeof r, &got);

    *n = 0;
    if (rc) {
        return rc;
    }
    if (got < 1) {
        return COGS_E_PROTO;
    }
    if (r[0]) {
        return r[0];
    }
    *n = (uint8_t)(got - 1);
    memcpy(blob, r + 1, *n);
    return COGS_OK;
}

uint8_t cogs_prefs_set(const uint8_t *blob, uint8_t n)
{
    uint8_t r[1];
    uint16_t got;
    uint8_t rc = cogs_request(OP_PREFS_SET, NULL, 0, blob, n, r, sizeof r, &got);

    if (rc) {
        return rc;
    }
    return got < 1 ? COGS_E_PROTO : r[0];
}
