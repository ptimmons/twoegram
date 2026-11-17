/*
 * The session with the gateway. See session.h.
 */
#include <string.h>
#include "cogs_http.h"
#include "hmac.h"
#include "fmt.h"
#include "session.h"

char     ses_base[SES_BASE_MAX + 1];
uint8_t  ses_key[A2_KEY_LEN];
char     ses_id[9];
char     ses_sid[9];
uint8_t  ses_open;
uint8_t  ses_nagents;
char     ses_agents[SES_AGENTS_MAX][SES_AGENT_LEN + 1];
char     ses_server[61];
uint32_t ses_seq;
a2_reply_t ses_reply;
uint8_t  ses_genuine;

static uint8_t  sk[32];
/* The reply buffer lives in LOWBSS, $0800-$1FFF on the Apple (see
 * cfg/twoegram.cfg): never zeroed, and always written before it is read.
 * The set-up overlay loads over it (overlay.h). */
#ifdef __CC65__
#pragma bss-name (push, "LOWBSS")
#endif
static uint8_t  rbuf[2048];
#ifdef __CC65__
#pragma bss-name (pop)
#endif
/* In main memory: the set-up overlay (overlay.h) loads over LOWBSS, and
 * the names must outlast it. */
char     ses_labels[SES_AGENTS_MAX][SES_AGENT_LEN + 1];
static char     url[256];
static char     hdrs[160];
static char     meth[8];
static cogs_part_t parts[6];
static uint8_t  dig[32];

static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

uint8_t ses_device_id(void)
{
    uint8_t rc;

    COGS_PART(parts[0], "twoegram-id", 11);
    COGS_PART(parts[1], ses_key, A2_KEY_LEN);
    rc = sha256_parts(parts, 2, dig);
    if (!rc) {
        a2_hex(ses_id, dig, 4);
    }
    return rc;
}

/* The HTTP exchange and the header line, for every request. */
static uint8_t exchange(const char *method, const uint8_t *body, uint16_t nbody, const char *h)
{
    uint16_t got, status;
    uint8_t rc;

    ses_genuine = 0;
    rc = cogs_http(method, url, h, body, nbody, COGS_HTTP_KEEPALIVE,
                   rbuf, sizeof rbuf, &got, &status);
    if (rc) {
        return rc;
    }
    if (status != 200 || a2_reply(rbuf, got, &ses_reply)) {
        return SES_E_NOTSERVER;
    }
    return 0;
}

uint8_t ses_line(const char *word, const char **val, uint8_t *vlen)
{
    a2_iter_t it;
    a2_item_t item;

    a2_iter(&it, &ses_reply);
    while (a2_next(&it, &item) == 1) {
        if (a2_is(&item, word)) {
            *val = item.val;
            *vlen = item.vlen;
            return 1;
        }
    }
    return 0;
}

static void copy_line(const char *word, char *dst, uint8_t cap)
{
    const char *v;
    uint8_t n;

    dst[0] = 0;
    if (ses_line(word, &v, &n) && n < cap) {
        memcpy(dst, v, n);
        dst[n] = 0;
    }
}

uint8_t ses_agent_at(const char *val, uint8_t vlen, uint8_t *rest)
{
    uint8_t i, n;

    for (n = 0; n < vlen && val[n] != ' '; ++n) {
    }
    *rest = (uint8_t)(n + 1);
    if (n < vlen) {
        for (i = 0; i < ses_nagents; ++i) {
            if (strlen(ses_agents[i]) == n && !memcmp(ses_agents[i], val, n)) {
                return i;
            }
        }
    }
    return SES_AGENTS_MAX;
}

uint8_t ses_hello(void)
{
    static uint8_t cn[16], sn[16];
    a2_iter_t it;
    a2_item_t item;
    char *u;
    uint8_t rc, i, n, rest, have_sn = 0;

    ses_open = 0;
    ses_nagents = 0;
    ses_sid[0] = 0;
    memset(ses_labels, 0, sizeof ses_labels);
    rc = cogs_random(cn, 16);
    if (!rc) {
        rc = ses_device_id();
    }
    if (!rc) {
        COGS_PART(parts[0], "hello", 5);
        COGS_PART(parts[1], cn, 16);
        rc = hmac_parts(ses_key, A2_KEY_LEN, parts, 2, dig);
    }
    if (rc) {
        return rc;
    }
    u = fmt_str(fmt_str(url, ses_base), "/a2/hello?d=");
    u = fmt_str(u, ses_id);
    u = fmt_str(u, "&c=");
    u = a2_hex(u, cn, 16);
    u = fmt_str(u, "&m=");
    a2_hex(u, dig, 32);

    rc = exchange("GET", NULL, 0, "");
    if (rc) {
        return rc;
    }
    copy_line("server", ses_server, sizeof ses_server);
    if (ses_reply.code) {
        return SES_E_REFUSED;
    }

    a2_iter(&it, &ses_reply);
    while ((rc = a2_next(&it, &item)) == 1) {
        if (a2_is(&item, "sid") && item.vlen == 8) {
            memcpy(ses_sid, item.val, 8);
            ses_sid[8] = 0;
        } else if (a2_is(&item, "sn") && item.vlen == 32) {
            have_sn = (uint8_t)!a2_unhex(sn, item.val, 16);
        } else if (a2_is(&item, "agent") && item.vlen && item.vlen <= SES_AGENT_LEN &&
                   ses_nagents < SES_AGENTS_MAX) {
            memcpy(ses_agents[ses_nagents], item.val, item.vlen);
            ses_agents[ses_nagents][item.vlen] = 0;
            ++ses_nagents;
        } else if (a2_is(&item, "name")) {
            /* "name main Pinchy", after the agent lines */
            i = ses_agent_at(item.val, item.vlen, &rest);
            n = (uint8_t)(item.vlen - rest);
            if (i < SES_AGENTS_MAX && n && n <= SES_AGENT_LEN) {
                memcpy(ses_labels[i], item.val + rest, n);
                ses_labels[i][n] = 0;
            }
        }
    }
    if (rc == A2_ITER_BAD || !have_sn || !ses_sid[0]) {
        return SES_E_NOTSERVER;
    }

    /* The session key, and the proof that the gateway holds the key: a
     * reply signed with a key only the two of us can make. */
    COGS_PART(parts[0], "session", 7);
    COGS_PART(parts[1], cn, 16);
    COGS_PART(parts[2], sn, 16);
    rc = hmac_parts(ses_key, A2_KEY_LEN, parts, 3, sk);
    if (!rc) {
        COGS_PART(parts[0], "h", 1);
        COGS_PART(parts[1], ses_reply.payload, ses_reply.plen);
        rc = hmac_parts(sk, 32, parts, 2, dig);
    }
    if (rc) {
        return rc;
    }
    if (!ses_reply.signed_ || memcmp(dig, ses_reply.mac, 32)) {
        ses_nagents = 0;
        return SES_E_FORGED;
    }
    ses_genuine = 1;
    ses_seq = 0;
    ses_open = 1;
    return 0;
}

uint8_t ses_request(const char *method, const char *target,
                    const uint8_t *body, uint16_t nbody,
                    uint32_t seq, uint8_t forge)
{
    static uint8_t sq[4];
    uint8_t rc, n = (uint8_t)strlen(method);
    char *h;

    if (!seq) {
        seq = ++ses_seq;
    }
    be32(sq, seq);
    /* "GET " or "POST ": the method and its space are one piece */
    memcpy(meth, method, n);
    meth[n] = ' ';
    meth[n + 1] = 0;
    COGS_PART(parts[0], "q", 1);
    COGS_PART(parts[1], sq, 4);
    COGS_PART(parts[2], meth, n + 1);
    COGS_PART(parts[3], target, strlen(target));
    COGS_PART(parts[4], "\n", 1);
    COGS_PART(parts[5], body, nbody);
    rc = hmac_parts(sk, 32, parts, 6, dig);
    if (rc) {
        return rc;
    }
    if (forge) {
        dig[0] ^= 1;
    }
    h = fmt_str(hdrs, "X-A2-Session: ");
    h = fmt_str(h, ses_sid);
    h = fmt_str(h, "\r\nX-A2-Seq: ");
    h = fmt_u32(h, seq);
    h = fmt_str(h, "\r\nX-A2-Mac: ");
    a2_hex(h, dig, 32);
    fmt_str(fmt_str(url, ses_base), target);

    rc = exchange(method, body, nbody, hdrs);
    if (rc) {
        return rc;
    }
    if (ses_reply.signed_) {
        COGS_PART(parts[0], "r", 1);
        COGS_PART(parts[1], sq, 4);
        COGS_PART(parts[2], ses_reply.payload, ses_reply.plen);
        rc = hmac_parts(sk, 32, parts, 3, dig);
        if (rc) {
            return rc;
        }
        if (memcmp(dig, ses_reply.mac, 32)) {
            return SES_E_FORGED;
        }
        ses_genuine = 1;
    }
    if (ses_reply.code) {
        return SES_E_REFUSED;
    }
    return ses_genuine ? 0 : SES_E_FORGED;     /* OK must be signed */
}

uint8_t ses_call(const char *method, const char *target,
                 const uint8_t *body, uint16_t nbody)
{
    uint8_t rc;

    if (!ses_open) {
        rc = ses_hello();
        if (rc) {
            return rc;
        }
    }
    rc = ses_request(method, target, body, nbody, 0, 0);
    if (rc == SES_E_REFUSED &&
        (ses_reply.code == A2_NO_SESSION || ses_reply.code == A2_REPLAY)) {
        /* The gateway restarted, or the session aged out: once more. */
        rc = ses_hello();
        if (!rc) {
            rc = ses_request(method, target, body, nbody, 0, 0);
        }
    }
    return rc;
}

const char *ses_strerror(uint8_t rc)
{
    if (rc == SES_E_NOTSERVER) {
        return "NOT A TWOEGRAM GATEWAY AT THAT ADDRESS";
    }
    if (rc == SES_E_FORGED) {
        return "A REPLY WAS NOT SIGNED WITH THIS KEY: STOPPED";
    }
    if (rc != SES_E_REFUSED) {
        return cogs_strerror(rc);
    }
    switch (ses_reply.code) {
    case A2_UNKNOWN_DEVICE: return "THE GATEWAY DOES NOT KNOW THIS KEY";
    case A2_BAD_SIGNATURE:  return "BAD SIGNATURE: IS THE KEY RIGHT?";
    case A2_NO_SESSION:     return "THE SESSION ENDED";
    case A2_LOCKED:         return "LOCKED OUT AFTER BAD TRIES: WAIT 15 MINUTES";
    case A2_UNKNOWN_AGENT:  return "THE GATEWAY HAS NO SUCH AGENT";
    case A2_TOO_LARGE:      return "TOO LONG FOR THE GATEWAY";
    case A2_BUSY:           return "THE GATEWAY IS BUSY";
    }
    return "THE GATEWAY REFUSED";
}

void ses_forget(void)
{
    memset(ses_key, 0, sizeof ses_key);
    memset(sk, 0, sizeof sk);
    ses_open = 0;
}
