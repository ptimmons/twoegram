/*
 * A2PROBE.SYSTEM -- milestone 2's real-card test: the Twoegram protocol
 * from end to end against the plugin's dev server, on one screen to be
 * photographed. See docs/PROBE.md.
 *
 *   1. find the card, PING, wait for a link
 *   2. read A2PROBE.CFG: the server's address and a test key
 *   3. hello: a session, proved both ways
 *   4. a signed send, and the echo agent's answer by long poll
 *   5. 20 polls back to back, timed by the card's clock
 *   6. a poll after 6 s idle (inside the card's 8 s keep-alive) and after
 *      10 s (outside it); the dev server logs each new connection
 *   7. a replayed request and a forged one, both refused
 *   8. 20 HMACs of 1,900 bytes, timed
 *   9. the card's prefs blob: are bytes 15-31 free? optionally a write
 *      and restore, only if you press W
 *  10. a ProDOS block read of the boot volume, after all of the above
 */
#include <apple2.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "screen.h"
#include "kbd.h"
#include "tick.h"
#include "cogs.h"
#include "cogs_http.h"
#include "hmac.h"
#include "proto.h"
#include "fmt.h"
#include "ui.h"
#include "build.h"

#define FIRST_ROW   2
#define LAST_ROW    22
#define VAL_COL     12
#define BACKSTOP    (30 * TICKS_PER_SEC)

uint8_t __fastcall__ disk_read_block(uint8_t unit, uint16_t block, void *buf);
uint8_t disk_unit(void);

static uint8_t row = FIRST_ROW;
static uint8_t failures;
static char    buf[96];

static char    base[80];            /* http://host:port */
static char    agent[17];
static uint8_t key[A2_KEY_LEN];
static uint8_t sk[32];
static char    sid[9];
static uint32_t seq;

static uint8_t  rbuf[2048];
static char     url[256];
static char     target[160];
static char     hdrs[160];
static a2_reply_t reply;
static uint16_t waited;             /* frames the last request waited */
static char     srv[61];            /* the server's own name and build */

/* ---- the report ---- */

static void line(const char *label, const char *value, uint8_t inverse)
{
    lb_clear(SC_SPACE);
    lb_text(1, label, 0);
    lb_text(VAL_COL, value, inverse);
    scr_blit(row);
}

static void say(const char *label, const char *value)
{
    line(label, value, 0);
    if (row < LAST_ROW) {
        ++row;
    }
}

static void fail(const char *label, const char *what, uint8_t rc)
{
    char *p = fmt_str(buf, what);

    if (rc) {
        p = fmt_str(p, ": ");
        fmt_str(p, cogs_strerror(rc));
    }
    line(label, buf, 1);
    if (row < LAST_ROW) {
        ++row;
    }
    ++failures;
}

/* ---- waiting ---- */

static uint16_t start;

static uint8_t idle(void)
{
    tick_poll();
    return (uint16_t)(ticks - start) > BACKSTOP ? COGS_E_TIMEOUT : 0;
}

/* ---- the protocol ---- */

static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

#define R_GENUINE   1
#define R_UNSIGNED  2
#define R_FORGED    3               /* signed, but not by our session key */

/*
 * One signed request with sequence number s (0: the next one). rc is a
 * card or COGS_E_* code; *how says whether the reply could be trusted.
 * With forge set, the request's signature is spoiled first.
 */
static uint8_t request(const char *method, const char *tgt, const char *body,
                       uint32_t s, uint8_t forge, uint8_t *how)
{
    uint8_t mac[32], sq[4], rc;
    uint16_t got, status, nbody = (uint16_t)strlen(body);
    cogs_part_t p[6];
    char *h;

    if (!s) {
        s = ++seq;
    }
    be32(sq, s);
    COGS_PART(p[0], "q", 1);
    COGS_PART(p[1], sq, 4);
    COGS_PART(p[2], method, (uint16_t)strlen(method));     /* "GET " or "POST " */
    COGS_PART(p[3], tgt, (uint16_t)strlen(tgt));
    COGS_PART(p[4], "\n", 1);
    COGS_PART(p[5], body, nbody);
    rc = hmac_parts(sk, 32, p, 6, mac);
    if (rc) {
        return rc;
    }
    if (forge) {
        mac[0] ^= 1;
    }
    h = fmt_str(hdrs, "X-A2-Session: ");
    h = fmt_str(h, sid);
    h = fmt_str(h, "\r\nX-A2-Seq: ");
    h = fmt_u32(h, s);
    h = fmt_str(h, "\r\nX-A2-Mac: ");
    a2_hex(h, mac, 32);
    fmt_str(fmt_str(url, base), tgt);

    /* the method without its trailing space */
    memcpy(buf, method, strlen(method) - 1);
    buf[strlen(method) - 1] = 0;
    tick_reset();
    start = ticks;
    rc = cogs_http(buf, url, hdrs, (const uint8_t *)body, nbody, COGS_HTTP_KEEPALIVE,
                   rbuf, sizeof rbuf, &got, &status);
    waited = ticks;
    if (rc) {
        return rc;
    }
    if (status != 200 || a2_reply(rbuf, got, &reply)) {
        return COGS_E_PROTO;
    }
    *how = R_UNSIGNED;
    if (reply.signed_) {
        COGS_PART(p[0], "r", 1);
        COGS_PART(p[1], sq, 4);
        COGS_PART(p[2], reply.payload, reply.plen);
        rc = hmac_parts(sk, 32, p, 3, mac);
        if (rc) {
            return rc;
        }
        *how = memcmp(mac, reply.mac, 32) ? R_FORGED : R_GENUINE;
    }
    return COGS_OK;
}

static const char *code_name(uint8_t c)
{
    static const char *const names[] = {
        "OK", "BAD_REQUEST", "UNKNOWN_DEVICE", "BAD_SIGNATURE", "NO_SESSION",
        "REPLAY", "LOCKED", "UNKNOWN_AGENT", "TOO_LARGE", "BUSY"
    };
    /* cc65 will not mix an array element and a literal in ?: */
    if (c < sizeof names / sizeof names[0]) {
        return names[c];
    }
    return "?";
}

static const char *trust(uint8_t how)
{
    if (how == R_GENUINE) {
        return ", signed";
    }
    if (how == R_UNSIGNED) {
        return ", unsigned";
    }
    return ", FORGED SIGNATURE";
}

/* "code 3 BAD_SIGNATURE, unsigned" */
static char *describe(char *p, uint8_t how)
{
    p = fmt_str(p, "code ");
    p = fmt_u32(p, reply.code);
    p = fmt_str(p, " ");
    p = fmt_str(p, code_name(reply.code));
    return fmt_str(p, trust(how));
}

/* Wait secs seconds whatever the keyboard does. tick_wait() ends at a
 * keypress, and in run 3 a W pressed early, ready for the prefs prompt,
 * cut both idle waits to nothing. */
static void wait_secs(uint8_t secs)
{
    tick_reset();
    while (ticks < (uint16_t)secs * TICKS_PER_SEC) {
        tick_poll();
    }
}

/* Which connection the dev server says this reply came on: 0 if it did
 * not say (a server that does not number them). */
static uint16_t reply_conn(void)
{
    a2_iter_t it;
    a2_item_t item;

    a2_iter(&it, &reply);
    while (a2_next(&it, &item) == 1) {
        if (a2_is(&item, "conn")) {
            return a2_num(item.val, item.vlen);
        }
    }
    return 0;
}

static uint16_t last_conn;

static char *conn_note(char *p, uint16_t c)
{
    if (!c) {
        return fmt_str(p, "; server does not number connections");
    }
    p = fmt_u32(fmt_str(p, "; connection "), c);
    if (c == last_conn) {
        return fmt_str(p, " (the same)");
    }
    return fmt_str(p, " (a NEW one)");
}

/* ---- the steps ---- */

static uint8_t read_config(void)
{
    static char text[400];
    int fd, n;
    char *s, *e, *v;
    uint8_t got_key = 0, rc;

    fd = open("A2PROBE.CFG", O_RDONLY);
    if (fd < 0) {
        fail("Config", "no A2PROBE.CFG on the disk (see docs/PROBE.md)", 0);
        return 0;
    }
    n = read(fd, text, sizeof text - 1);
    close(fd);
    if (n <= 0) {
        fail("Config", "A2PROBE.CFG is empty", 0);
        return 0;
    }
    text[n] = 0;
    fmt_str(agent, "main");
    for (s = text; *s; s = e) {
        for (e = s; *e && *e != '\r' && *e != '\n'; ++e) {
        }
        if (*e) {
            *e++ = 0;
        }
        v = strchr(s, '=');
        if (!v) {
            continue;
        }
        *v++ = 0;
        if (!strcmp(s, "URL") && strlen(v) < sizeof base) {
            fmt_str(base, v);
            /* the targets start with '/' */
            if (base[0] && base[strlen(base) - 1] == '/') {
                base[strlen(base) - 1] = 0;
            }
        } else if (!strcmp(s, "KEY")) {
            rc = a2_key_parse(v, key);
            if (rc) {
                fail("Config", rc == A2_KEY_BADCHECK ? "KEY does not check: a typing slip?" : "KEY is not a key", 0);
                return 0;
            }
            got_key = 1;
        } else if (!strcmp(s, "AGENT") && strlen(v) < sizeof agent) {
            fmt_str(agent, v);
        }
    }
    if (!base[0] || !got_key) {
        fail("Config", "A2PROBE.CFG needs URL= and KEY= lines", 0);
        return 0;
    }
    /* A fingerprint of the parsed key, computed without the card, to
     * compare with the Mac's (docs/PROBE.md): it shows whether the 6502
     * read the key as the Mac does, without showing the key. */
    {
        uint16_t kc = 0;
        uint8_t i;
        char *p;

        for (i = 0; i < A2_KEY_LEN; ++i) {
            kc = (uint16_t)(kc * 31 + key[i]);
        }
        p = fmt_str(buf, base);
        p = fmt_str(p, "  key check ");
        p = fmt_hex8(p, (uint8_t)(kc >> 8));
        fmt_hex8(p, (uint8_t)kc);
    }
    say("Server", buf);
    return 1;
}

static uint8_t hello(void)
{
    uint8_t cn[16], sn[16], dig[32], id[4], rc;
    uint16_t got, status;
    cogs_part_t p[3];
    a2_iter_t it;
    a2_item_t item;
    char *u, *agents;
    static char alist[40];
    uint8_t have_sn = 0;

    rc = cogs_random(cn, 16);
    if (rc) {
        fail("Hello", "RANDOM failed", rc);
        return 0;
    }
    COGS_PART(p[0], "twoegram-id", 11);
    COGS_PART(p[1], key, 16);
    rc = sha256_parts(p, 2, dig);
    if (rc) {
        fail("Hello", "HASH failed", rc);
        return 0;
    }
    memcpy(id, dig, 4);
    COGS_PART(p[0], "hello", 5);
    COGS_PART(p[1], cn, 16);
    rc = hmac_parts(key, 16, p, 2, dig);
    if (rc) {
        fail("Hello", "HMAC failed", rc);
        return 0;
    }
    u = fmt_str(fmt_str(url, base), "/a2/hello?d=");
    u = a2_hex(u, id, 4);
    u = fmt_str(u, "&c=");
    u = a2_hex(u, cn, 16);
    u = fmt_str(u, "&m=");
    a2_hex(u, dig, 32);

    tick_reset();
    start = ticks;
    rc = cogs_http("GET", url, "", NULL, 0, COGS_HTTP_KEEPALIVE, rbuf, sizeof rbuf, &got, &status);
    waited = ticks;
    if (rc) {
        fail("Hello", "no answer from the server", rc);
        return 0;
    }
    if (status != 200 || a2_reply(rbuf, got, &reply)) {
        fmt_u32(fmt_str(buf, "not a Twoegram server: HTTP status "), status);
        fail("Hello", buf, 0);
        return 0;
    }
    /* Which server answered, if it says (refusals say too). */
    srv[0] = 0;
    a2_iter(&it, &reply);
    while (a2_next(&it, &item) == 1) {
        if (a2_is(&item, "server") && item.vlen < sizeof srv) {
            memcpy(srv, item.val, item.vlen);
            srv[item.vlen] = 0;
        }
    }
    if (reply.code) {
        u = describe(fmt_str(buf, "refused, "), reply.signed_ ? R_GENUINE : R_UNSIGNED);
        u = fmt_str(u, ", device ");
        a2_hex(u, id, 4);
        fail("Hello", buf, 0);
        say("Server is", srv[0] ? srv : "(it did not say)");
        return 0;
    }

    alist[0] = 0;
    agents = alist;
    a2_iter(&it, &reply);
    while ((rc = a2_next(&it, &item)) == 1) {
        if (a2_is(&item, "sid") && item.vlen == 8) {
            memcpy(sid, item.val, 8);
            sid[8] = 0;
        } else if (a2_is(&item, "sn") && item.vlen == 32) {
            have_sn = !a2_unhex(sn, item.val, 16);
        } else if (a2_is(&item, "agent") && agents + item.vlen + 2 < alist + sizeof alist) {
            if (agents != alist) {
                *agents++ = ' ';
            }
            memcpy(agents, item.val, item.vlen);
            agents += item.vlen;
            *agents = 0;
        }
    }
    if (rc == A2_ITER_BAD || !have_sn || !sid[0]) {
        fail("Hello", "reply malformed", 0);
        return 0;
    }
    /* The session key, and the proof that the server holds the key. */
    COGS_PART(p[0], "session", 7);
    COGS_PART(p[1], cn, 16);
    COGS_PART(p[2], sn, 16);
    rc = hmac_parts(key, 16, p, 3, sk);
    if (!rc) {
        COGS_PART(p[0], "h", 1);
        COGS_PART(p[1], reply.payload, reply.plen);
        rc = hmac_parts(sk, 32, p, 2, dig);
    }
    if (rc) {
        fail("Hello", "HMAC failed", rc);
        return 0;
    }
    if (!reply.signed_ || memcmp(dig, reply.mac, 32)) {
        fail("Hello", "the reply is NOT signed with our key: not our server", 0);
        return 0;
    }
    u = fmt_str(buf, "session ");
    u = fmt_str(u, sid);
    u = fmt_str(u, ", device ");
    u = a2_hex(u, id, 4);
    u = fmt_str(u, ", agents: ");
    u = fmt_str(u, alist);
    u = fmt_str(u, ", ");
    u = fmt_u32(u, waited);
    u = fmt_str(u, " frames");
    last_conn = reply_conn();
    if (last_conn) {
        fmt_u32(fmt_str(u, ", conn "), last_conn);
    }
    say("Hello", buf);
    say("Server is", srv[0] ? srv : "(it did not say)");
    return 1;
}

static uint32_t uptime(void)
{
    cogs_netstat_t ns;

    return cogs_netstat(&ns) || !ns.has_uptime ? 0 : ns.uptime;
}

/* Send a line and wait for the echo agent's answer. */
static uint8_t converse(void)
{
    uint8_t rc, how, tries;
    uint16_t mine = 0, frames;
    a2_iter_t it;
    a2_item_t item;
    char *p;

    fmt_str(fmt_str(target, "/a2/send?a="), agent);
    rc = request("POST ", target, "Hello from A2PROBE on the //e", 0, 0, &how);
    if (rc) {
        fail("Send", "no answer", rc);
        return 0;
    }
    if (how != R_GENUINE || reply.code) {
        describe(fmt_str(buf, "refused, "), how);
        fail("Send", buf, 0);
        return 0;
    }
    a2_iter(&it, &reply);
    while (a2_next(&it, &item) == 1) {
        if (item.kind == A2_ITEM_MSG) {
            mine = item.n;
        }
    }
    p = fmt_str(buf, "signed reply, stored as message ");
    p = fmt_u32(p, mine);
    p = fmt_str(p, ", ");
    p = fmt_u32(p, waited);
    fmt_str(p, " frames");
    say("Send", buf);

    /* Long polls of up to 4 s until the agent says something. */
    frames = 0;
    for (tries = 0; tries < 5; ++tries) {
        p = fmt_str(target, "/a2/poll?a=");
        p = fmt_str(p, agent);
        p = fmt_str(p, "&after=");
        p = fmt_u32(p, mine);
        fmt_str(p, "&w=4000");
        rc = request("GET ", target, "", 0, 0, &how);
        frames = (uint16_t)(frames + waited);
        if (rc) {
            fail("Answer", "poll failed", rc);
            return 0;
        }
        if (how != R_GENUINE || reply.code) {
            describe(fmt_str(buf, "poll refused, "), how);
            fail("Answer", buf, 0);
            return 0;
        }
        a2_iter(&it, &reply);
        while (a2_next(&it, &item) == 1) {
            if (item.kind == A2_ITEM_MSG && !item.mine) {
                p = fmt_str(buf, "\"");
                memcpy(p, item.text, item.tlen > 40 ? 40 : item.tlen);
                p += item.tlen > 40 ? 40 : item.tlen;
                p = fmt_str(p, item.tlen > 40 ? "...\" " : "\" ");
                p = fmt_u32(p, frames);
                fmt_str(p, " fr");
                for (p = buf; *p; ++p) {
                    if (*p == '\n') {
                        *p = ' ';
                    }
                }
                say("Answer", buf);
                return 1;
            }
        }
    }
    fail("Answer", "no answer from the agent in 20 s", 0);
    return 0;
}

static void polls(void)
{
    uint8_t i, rc, how, conns = 0;
    uint32_t t0, t1;
    uint16_t frames = 0, c;
    char *p;

    p = fmt_str(target, "/a2/poll?a=");
    fmt_str(fmt_str(p, agent), "&after=99999");
    t0 = uptime();
    for (i = 0; i < 20; ++i) {
        rc = request("GET ", target, "", 0, 0, &how);
        frames = (uint16_t)(frames + waited);
        if (rc || how != R_GENUINE || reply.code) {
            fmt_u32(fmt_str(buf, "poll failed at number "), i + 1);
            fail("20 polls", buf, rc);
            return;
        }
        c = reply_conn();
        if (c != last_conn) {
            ++conns;            /* a connection the run had not used */
            last_conn = c;
        }
    }
    t1 = uptime();
    p = fmt_str(buf, "all genuine; ");
    p = fmt_u32(p, t1 - t0);
    p = fmt_str(p, " s by the card's clock, ");
    p = fmt_u32(p, frames);
    p = fmt_str(p, " fr waiting; ");
    p = fmt_u32(p, conns);
    fmt_str(p, " new conn");
    say("20 polls", buf);
}

static void idle_poll(const char *label, uint8_t secs)
{
    uint8_t rc, how;
    uint32_t t0;
    uint16_t c;
    char *p;

    p = fmt_str(buf, "waiting ");
    p = fmt_u32(p, secs);
    fmt_str(p, " s, keys ignored...");
    line(label, buf, 0);
    t0 = uptime();
    wait_secs(secs);
    rc = request("GET ", target, "", 0, 0, &how);
    if (rc || how != R_GENUINE || reply.code) {
        fail(label, "poll failed", rc);
        return;
    }
    c = reply_conn();
    p = fmt_u32(fmt_str(buf, "after "), uptime() - t0);
    p = fmt_str(p, " s: ");
    p = fmt_u32(p, waited);
    p = fmt_str(p, " frames");
    conn_note(p, c);
    last_conn = c;
    say(label, buf);
}

static void attacks(void)
{
    uint8_t rc, how;

    /* The last request again, signature and all: refused, signed. */
    rc = request("GET ", target, "", seq, 0, &how);
    if (rc) {
        fail("Replay", "no answer", rc);
    } else if (reply.code == A2_REPLAY && how == R_GENUINE) {
        describe(fmt_str(buf, "refused as it should be, "), how);
        say("Replay", buf);
    } else {
        describe(fmt_str(buf, "NOT refused properly: "), how);
        fail("Replay", buf, 0);
    }

    /* A fresh request with its signature spoiled: refused, unsigned. */
    rc = request("GET ", target, "", 0, 1, &how);
    if (rc) {
        fail("Forgery", "no answer", rc);
    } else if (reply.code == A2_BAD_SIGNATURE && how == R_UNSIGNED) {
        describe(fmt_str(buf, "refused as it should be, "), how);
        say("Forgery", buf);
    } else {
        describe(fmt_str(buf, "NOT refused properly: "), how);
        fail("Forgery", buf, 0);
    }
}

static void hmac_speed(void)
{
    static uint8_t big[1900];
    uint8_t mac[32], i, rc;
    cogs_part_t p[1];
    uint32_t t0, t1;
    char *s;

    /* Timed by the card's clock in whole seconds: frames cannot be
     * counted while bytes are being moved (the first run said 6 frames
     * for 20, far too few). 50 of them, for a usable figure. */
    memset(big, 'x', sizeof big);
    COGS_PART(p[0], big, sizeof big);
    t0 = uptime();
    for (i = 0; i < 50; ++i) {
        rc = hmac_parts(sk, 32, p, 1, mac);
        if (rc) {
            fail("HMAC", "failed", rc);
            return;
        }
    }
    t1 = uptime();
    s = fmt_str(buf, "50 x 1,900 bytes in ");
    s = fmt_u32(s, t1 - t0);
    fmt_str(s, " s by the card's clock");
    say("HMAC", buf);
}

static void prefs(void)
{
    static uint8_t orig[COGS_PREFS_MAX], blob[COGS_PREFS_MAX], back[COGS_PREFS_MAX];
    uint8_t n, n2, i, rc, used = 0;
    char *p;

    rc = cogs_prefs_get(orig, &n);
    if (rc) {
        fail("Prefs", "PREFS_GET failed", rc);
        return;
    }
    for (i = COGS_PREFS_FREE; i < n; ++i) {
        used |= orig[i];
    }
    p = fmt_u32(buf, n);
    p = fmt_str(p, " bytes stored; ");
    if (n <= COGS_PREFS_FREE) {
        p = fmt_str(p, "15-31 not stored yet (free)");
    } else {
        p = fmt_str(p, used ? "15-31 IN USE by some program" : "15-31 all zero (free)");
    }
    say("Prefs", buf);
    if (n < COGS_PREFS_FREE) {
        say("Prefs W", "skipped: the card's own settings are not stored yet");
        return;
    }

    line("Prefs W", "W: write bytes 15-31, check, restore. Other keys skip.", 0);
    status_msg("PRESS W TO TEST THE CARD'S SPARE SETTINGS, ANY OTHER KEY TO SKIP");
    key_flush();            /* only a key pressed after the question counts */
    n2 = key_get();
    status_msg("A2PROBE RUNNING");
    if (n2 != 'w' && n2 != 'W') {
        say("Prefs W", "skipped");
        return;
    }
    memcpy(blob, orig, COGS_PREFS_FREE);
    for (i = COGS_PREFS_FREE; i < COGS_PREFS_MAX; ++i) {
        blob[i] = (uint8_t)(0xA5 ^ i);
    }
    rc = cogs_prefs_set(blob, COGS_PREFS_MAX);
    if (!rc) {
        rc = cogs_prefs_get(back, &n2);
    }
    if (rc) {
        fail("Prefs W", "write or read back failed", rc);
    } else if (n2 != COGS_PREFS_MAX || memcmp(back, blob, COGS_PREFS_MAX)) {
        fail("Prefs W", "read back DIFFERENT from what was written", 0);
    }
    /* Put back exactly what was there. */
    rc = cogs_prefs_set(orig, n);
    if (!rc) {
        rc = cogs_prefs_get(back, &n2);
    }
    if (rc || n2 != n || memcmp(back, orig, n)) {
        fail("Prefs W", "COULD NOT RESTORE the original bytes", rc);
        return;
    }
    say("Prefs W", "written, read back, and restored");
}

static void disk(void)
{
    static uint8_t block[512];
    uint8_t unit = disk_unit(), rc;
    char *p;

    rc = disk_read_block(unit, 2, block);
    p = fmt_str(buf, "block 2 of unit $");
    p = fmt_hex8(p, unit);
    if (rc) {
        p = fmt_str(p, " FAILED, ProDOS error $");
        fmt_hex8(p, rc);
        fail("Disk", buf, 0);
        return;
    }
    fmt_str(p, " read after all the requests");
    say("Disk", buf);
}

int main(void)
{
    uint8_t os = get_ostype();
    uint8_t slot, rc, secs;
    cogs_ping_t pg;
    cogs_netstat_t ns;
    char *p;

    if (os < APPLE_IIE) {
        return 1;
    }
    has_mousetext = (uint8_t)(os != APPLE_IIE);
    scr_init();
    scr_clear(0, SCR_ROWS - 1);
    lb_clear(SC_SPACE);
    lb_text(1, "Twoegram protocol probe", 0);
    lb_text((uint8_t)(SCR_COLS - 1 - txt_len("Twoegram " TWOEGRAM_VERSION " build " BUILD_ID)),
            "Twoegram " TWOEGRAM_VERSION " build " BUILD_ID, 0);
    scr_blit(0);
    lb_clear(has_mousetext ? MT_RULE : SC_DASH);
    scr_blit(1);
    status_msg("A2PROBE RUNNING");

    /* The file first: no disk call once the network is in use. */
    if (!read_config()) {
        goto done;
    }

    slot = cogs_find();
    if (!slot) {
        fail("Card", "no CoGS card found", 0);
        goto done;
    }
    rc = cogs_open(slot);
    if (!rc) {
        rc = cogs_ping(&pg);
    }
    if (rc) {
        fail("Card", "the card does not answer", rc);
        goto done;
    }
    p = fmt_str(buf, "slot ");
    p = fmt_u32(p, slot);
    p = fmt_str(p, ", firmware ");
    p = fmt_u32(p, pg.fw_major);
    *p++ = '.';
    if (pg.fw_minor < 10) {
        *p++ = '0';
    }
    fmt_u32(p, pg.fw_minor);
    say("Card", buf);

    for (secs = 0; !(cogs_status() & COGS_ST_LINK_UP) && secs < 20; ++secs) {
        line("Network", "waiting for a link...", 0);
        tick_reset();
        tick_wait(TICKS_PER_SEC);
    }
    rc = cogs_netstat(&ns);
    if (rc || ns.link != COGS_LINK_UP) {
        fail("Network", "no link", rc);
        goto done;
    }
    p = fmt_str(buf, "up, ");
    p = fmt_ip(p, ns.ip);
    p = fmt_str(p, ", ");
    p = fmt_u32(p, ns.sockets);
    fmt_str(p, ns.sockets == 1 ? " socket open before we start" : " sockets open before we start");
    say("Network", buf);

    cogs_idle = idle;
    if (hello() && converse()) {
        polls();
        idle_poll("Idle 6 s", 6);
        idle_poll("Idle 10 s", 10);
        attacks();
    }
    hmac_speed();
    prefs();
    cogs_idle = NULL;
    disk();

done:
    if (row < LAST_ROW) {
        ++row;
    }
    if (failures) {
        fmt_str(fmt_str(fmt_u32(buf, failures), failures == 1 ? " check failed" : " checks failed"),
                ", probe build " BUILD_ID);
        line("", buf, 1);
    } else {
        line("", "All checks passed, probe build " BUILD_ID ". Please photograph this screen.", 0);
    }
    status_msg(failures ? "A2PROBE FAILED -- ANY KEY QUITS" : "A2PROBE PASSED -- ANY KEY QUITS");
    key_get();
    scr_done();
    return 0;
}
