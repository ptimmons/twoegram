/*
 * The CoGS frame protocol (Technical Reference chapters 3 to 5).
 *
 *   frame = OPCODE LEN_LO LEN_HI PAYLOAD[LEN]
 *
 * Commands are $00-$7F. A reply is the command's opcode with bit 7 set.
 * $90-$9F are events, which can arrive ahead of any reply and are skipped
 * here; nothing Newsiie did has a socket open, so none matter.
 * A command the card cannot parse gets no reply of its own: it gets an
 * ERROR event ($93) with socket $FF and a code.
 *
 * The card never interleaves two frames, so the reader handles one whole
 * frame at a time. If a frame stops arriving partway (a byte never comes),
 * our view of the stream is lost: reset the parser and both FIFOs.
 *
 * Plain C over the cogs_io primitives, so the host tests can run it against
 * a model of the card.
 */
#include <stddef.h>
#include "cogs.h"

#define OP_STATUS       0x01
#define OP_TIME         0x05
#define OP_STREAM_OPEN  0x70
#define OP_STREAM_READ  0x71
#define OP_STREAM_CLOSE 0x72

#define STREAM_NET      0       /* the network stream's handle */

#define EV_ERROR        0x93
#define SOCK_NONE       0xFF

/* Frames to skip while waiting for a reply before deciding it is not
 * coming. Events are rare while no socket is open. */
#define MAX_SKIPPED     64

uint8_t cogs_slot;
uint8_t (*cogs_idle)(void);

static uint8_t hdr[3];

static const char *const card_err[COGS_E_LAST_CARD + 1] = {
    "OK",
    "NO NETWORK",               /* 1  NOT_JOINED */
    "DNS FAILED",               /* 2  DNS_FAIL */
    "TIMED OUT",                /* 3  CONN_TIMEOUT */
    "CONNECTION REFUSED",       /* 4  CONN_REFUSED */
    "TLS FAILED",               /* 5  TLS_HANDSHAKE */
    "BAD CERTIFICATE",          /* 6  CERT_VERIFY */
    "NO FREE SOCKET",           /* 7  NO_FREE_SOCK */
    "SOCKET STATE",             /* 8  SOCKET_STATE */
    "CARD BUFFER OVERRUN",      /* 9  BUF_OVERRUN */
    "BAD FRAME",                /* 10 BAD_FRAME */
    "NOT SUPPORTED BY CARD",    /* 11 UNSUPPORTED */
    "CARD INTERNAL ERROR",      /* 12 INTERNAL */
    "NOTHING RETAINED",         /* 13 NO_RETAINED */
    "NO SUCH JSON PATH",        /* 14 JSON_NOPATH */
    "CARD BUSY",                /* 15 BUSY */
    "NO VOLUME",                /* 16 VOL_NOMOUNT */
    "BLOCK OUT OF RANGE",       /* 17 VOL_RANGE */
    "NO STREAM OPEN",           /* 18 STREAM_NONE */
    "WINDOW SHORT",             /* 19 WIN_SHORT */
    "ALREADY MOUNTED",          /* 20 VOL_DUP */
};

const char *cogs_strerror(uint8_t code)
{
    if (code <= COGS_E_LAST_CARD) {
        return card_err[code];
    }
    switch (code) {
    case COGS_E_TIMEOUT: return "CARD STOPPED ANSWERING";
    case COGS_E_PROTO:   return "CARD PROTOCOL ERROR";
    case COGS_E_NOCARD:  return "NO COGS CARD";
    case COGS_E_ABORT:   return "STOPPED";
    }
    return "CARD ERROR";
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

uint8_t cogs_lost_why;
uint8_t cogs_lost_op;

/* Lost frame alignment: start again from a clean parser and empty FIFOs,
 * and remember why, for the diagnostics. */
static uint8_t lost(uint8_t why, uint8_t op)
{
    cogs_lost_why = why;
    cogs_lost_op = op;
    cogs_resync();
    return COGS_E_TIMEOUT;
}

uint8_t cogs_open(uint8_t s)
{
    cogs_slot = 0;
    cogs_set_slot(s);
    cogs_irq_off();
    if (cogs_resync()) {
        return COGS_E_TIMEOUT;
    }
    cogs_slot = s;
    return COGS_OK;
}

/* Read n bytes in pieces cogs_getn can take. Nonzero on timeout. */
static uint8_t get_bytes(uint8_t *dst, uint16_t n)
{
    uint8_t piece;

    while (n) {
        piece = n > 255 ? 255 : (uint8_t)n;
        if (cogs_getn(dst, piece)) {
            return 1;
        }
        dst += piece;
        n -= piece;
    }
    return 0;
}

uint8_t cogs_request(uint8_t op, const uint8_t *pre, uint8_t npre,
                     const uint8_t *body, uint16_t nbody,
                     uint8_t *buf, uint16_t cap, uint16_t *got)
{
    uint16_t len, n;
    uint8_t rop, skipped, err[2];

    if (got) {
        *got = 0;
    }
    len = (uint16_t)(npre + nbody);
    hdr[0] = op;
    hdr[1] = (uint8_t)len;
    hdr[2] = (uint8_t)(len >> 8);
    if (cogs_putn(hdr, 3) ||
        (npre && cogs_putn(pre, npre)) ||
        (nbody && cogs_putn(body, nbody))) {
        return lost(COGS_LOST_SEND, op);
    }

    for (skipped = 0; skipped < MAX_SKIPPED; ++skipped) {
        if (cogs_idle) {
            while (!(cogs_status() & COGS_ST_RX_AVAIL)) {
                rop = cogs_idle();
                if (rop) {
                    /* A reply may still come later. Flush what there is;
                     * a late one is skipped by the next request, which
                     * waits for its own opcode. */
                    cogs_lost_why = COGS_LOST_IDLE;
                    cogs_lost_op = op;
                    cogs_resync();
                    return rop;
                }
            }
        }
        if (get_bytes(hdr, 3)) {
            /* Nothing at all, or a header cut short. Either way the card
             * is not where we think it is. */
            return lost(COGS_LOST_HEADER, op);
        }
        rop = hdr[0];
        len = le16(hdr + 1);

        if (rop == (uint8_t)(op | 0x80)) {
            n = len < cap ? len : cap;
            if (get_bytes(buf, n) || (len > n && cogs_skip(len - n))) {
                return lost(COGS_LOST_PAYLOAD, rop);
            }
            if (got) {
                *got = n;
            }
            return COGS_OK;
        }

        if (rop == EV_ERROR && len >= 2) {
            if (get_bytes(err, 2) || cogs_skip(len - 2)) {
                return lost(COGS_LOST_PAYLOAD, rop);
            }
            if (err[0] == SOCK_NONE) {
                /* The card refused the command we just sent. */
                return err[1] ? err[1] : COGS_E_PROTO;
            }
            continue;
        }

        /* Every card-to-host opcode has bit 7 set. Anything else means we
         * are reading the middle of a frame. */
        if (!(rop & 0x80)) {
            return lost(COGS_LOST_OPCODE, rop);
        }
        if (len && cogs_skip(len)) {
            return lost(COGS_LOST_PAYLOAD, rop);
        }
    }
    return COGS_E_PROTO;
}

uint8_t cogs_netstat(cogs_netstat_t *s)
{
    uint8_t r[17];
    uint16_t got;
    uint8_t rc = cogs_request(OP_STATUS, NULL, 0, NULL, 0, r, sizeof r, &got);

    if (rc) {
        return rc;
    }
    if (got < 9) {
        return COGS_E_PROTO;
    }
    s->link = r[0];
    s->ip[0] = r[1];
    s->ip[1] = r[2];
    s->ip[2] = r[3];
    s->ip[3] = r[4];
    s->rssi = (int8_t)r[5];
    s->heap_kb = le16(r + 6);
    s->sockets = r[8];
    s->has_uptime = (uint8_t)(got >= 17);
    s->uptime = s->has_uptime ? le32(r + 13) : 0;
    return COGS_OK;
}

uint8_t cogs_time(uint32_t *unix_utc)
{
    uint8_t r[4];
    uint16_t got;
    uint8_t rc = cogs_request(OP_TIME, NULL, 0, NULL, 0, r, sizeof r, &got);

    if (rc) {
        return rc;
    }
    if (got < 4) {
        return COGS_E_PROTO;
    }
    *unix_utc = le32(r);
    return COGS_OK;
}

/* Replies to the stream commands start handle, result. */
static uint8_t stream_result(uint8_t rc, const uint8_t *r, uint16_t got)
{
    if (rc) {
        return rc;
    }
    if (got < 2 || r[0] != STREAM_NET) {
        return COGS_E_PROTO;
    }
    return r[1];
}

uint8_t cogs_stream_open(const char *url, uint8_t len)
{
    uint8_t r[2];
    uint16_t got;
    uint8_t rc = cogs_request(OP_STREAM_OPEN, &len, 1, (const uint8_t *)url, len,
                              r, sizeof r, &got);

    return stream_result(rc, r, got);
}

uint8_t cogs_stream_read(uint8_t *buf, uint16_t max, cogs_chunk_t *c)
{
    uint8_t pre[3];
    uint16_t got;
    uint8_t rc;

    pre[0] = STREAM_NET;
    pre[1] = (uint8_t)max;
    pre[2] = (uint8_t)(max >> 8);
    c->len = 0;
    c->flags = 0;
    c->data = buf + COGS_STREAM_HDR;
    rc = cogs_request(OP_STREAM_READ, pre, 3, NULL, 0, buf,
                      (uint16_t)(COGS_STREAM_HDR + max), &got);
    rc = stream_result(rc, buf, got);
    if (rc) {
        return rc;
    }
    if (got < COGS_STREAM_HDR) {
        return COGS_E_PROTO;
    }
    c->flags = buf[2];
    c->level = buf[3];
    c->len = le16(buf + 4);
    /* A reply longer than we asked for was cut by cogs_request; that is
     * data lost, not a short read. */
    if (c->len > max || got != (uint16_t)(COGS_STREAM_HDR + c->len)) {
        return COGS_E_PROTO;
    }
    return COGS_OK;
}

uint8_t cogs_stream_close(void)
{
    uint8_t r[2], h = STREAM_NET;
    uint16_t got;
    uint8_t rc = cogs_request(OP_STREAM_CLOSE, &h, 1, NULL, 0, r, sizeof r, &got);

    return stream_result(rc, r, got);
}
