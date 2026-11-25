/*
 * Host tests for src/cogs.c and src/fmt.c.
 *
 * The register primitives of src/cogs_io.s are replaced by a model of the
 * card: a frame parser on the host-to-card side and a FIFO on the
 * card-to-host side. A primitive that would wait for a byte that never
 * comes returns its timeout at once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cogs.h"
#include "fmt.h"

static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

/* ---- the card model ---- */

static uint8_t  q[65536];           /* card -> host FIFO */
static unsigned qh, qt;
static uint8_t  fr[4096];           /* host -> card frame being assembled */
static unsigned frn;
static unsigned nresync;

/* Behaviour switches, reset by reset_model(). */
static int events_first;            /* queue events ahead of the next reply */
static int silent;                  /* answer nothing */
static int truncate_reply;          /* promise a 4-byte PING reply, send 2 */
static int short_reply;             /* PING reply of 2 complete bytes */
static int garbage_first;           /* a stray non-reply byte ahead of the reply */
static int status_len = 17;
static uint8_t last_payload[4096];
static unsigned last_len;

/* The network stream. */
static uint8_t  body[70000];
static unsigned body_len, body_pos;
static int      stream_open;
static uint8_t  open_result;        /* what the next STREAM_OPEN answers */
static int      open_delay;         /* idle calls before the open reply lands */
static int      open_pending;
static char     open_url[256];
static int      read_toggle;        /* every other read finds the ring empty */
static int      idle_calls, idle_limit;

static void put(uint8_t op, const uint8_t *p, unsigned n);
static void read_reply(unsigned max);
static int      read_delay;         /* idle calls before a read reply lands */
static unsigned read_pending;       /* max of a read waiting to be answered */
static int      read_idle0;

static void reset_model(void)
{
    qh = qt = frn = 0;
    events_first = silent = truncate_reply = short_reply = garbage_first = 0;
    status_len = 17;
    stream_open = open_pending = 0;
    open_result = 0;
    open_delay = 0;
    read_toggle = 0;
    idle_calls = 0;
    idle_limit = 0;
    read_delay = 0;
    read_pending = 0;
}

static void open_reply(void)
{
    uint8_t r[2] = { 0, open_result };

    stream_open = !open_result;
    body_pos = 0;
    put(0xF0, r, 2);
}

/* The idle hook cogs.c calls while a reply has not started. */
static uint8_t idle(void)
{
    ++idle_calls;
    if (open_pending && idle_calls >= open_delay) {
        open_pending = 0;
        open_reply();
    }
    if (read_pending && idle_calls - read_idle0 >= read_delay) {
        read_reply(read_pending);
        read_pending = 0;
    }
    if (idle_limit && idle_calls >= idle_limit) {
        return COGS_E_TIMEOUT;
    }
    return 0;
}

static void put(uint8_t op, const uint8_t *p, unsigned n)
{
    /* The FIFO never wraps; rewind it whenever the host has drained it,
     * which it always has by the time a new command arrives. */
    if (qh == qt) {
        qh = qt = 0;
    }
    if (qt + 3 + n > sizeof q) {
        printf("model FIFO overflow\n");
        exit(2);
    }
    q[qt++] = op;
    q[qt++] = (uint8_t)n;
    q[qt++] = (uint8_t)(n >> 8);
    memcpy(q + qt, p, n);
    qt += n;
}

static uint8_t pat(unsigned i) { return (uint8_t)(i * 7 + 3); }

/* The card's answer to STREAM_READ for up to max bytes. */
static void read_reply(unsigned max)
{
    static uint8_t rr[6 + 4096];
    unsigned n = 0;

    rr[0] = 0;
    rr[1] = 0;
    if (!stream_open) {
        rr[1] = 18;
        memset(rr + 2, 0, 4);
        put(0xF1, rr, 6);
        return;
    }
    /* With read_toggle set, every other read finds the ring empty. */
    if (!read_toggle || !(++read_toggle & 1)) {
        n = body_len - body_pos;
        if (n > max) n = max;
    }
    memcpy(rr + 6, body + body_pos, n);
    body_pos += n;
    rr[2] = body_pos < body_len ? 1 : 2;        /* FLOWING, or END */
    if (!n && body_pos < body_len) rr[2] |= 4;  /* STALLED */
    rr[3] = 128;
    rr[4] = (uint8_t)n;
    rr[5] = (uint8_t)(n >> 8);
    put(0xF1, rr, 6 + n);
}

static void card_frame(void)
{
    unsigned len = fr[1] | (fr[2] << 8);
    const uint8_t *p = fr + 3;
    uint8_t r[300];
    unsigned i;

    memcpy(last_payload, p, len);
    last_len = len;
    if (silent) {
        return;
    }
    if (garbage_first) {
        q[qt++] = 0x42;
    }
    if (events_first) {
        uint8_t ws = 2, data[301], err[6] = { 0, 3, 3, 'b', 'a', 'd' };

        put(0x94, &ws, 1);                  /* WIFI_STATE */
        data[0] = 2;                        /* DATA on another socket */
        for (i = 1; i < sizeof data; ++i) data[i] = pat(i);
        put(0x91, data, sizeof data);
        put(0x93, err, sizeof err);         /* ERROR tied to socket 0 */
    }

    switch (fr[0]) {
    case 0x00:                              /* PING */
        r[0] = 1; r[1] = 39; r[2] = 4; r[3] = 0xFF;
        if (truncate_reply) {
            q[qt++] = 0x80; q[qt++] = 4; q[qt++] = 0;
            q[qt++] = 1; q[qt++] = 39;
        } else {
            put(0x80, r, short_reply ? 2 : 4);
        }
        break;
    case 0x01:                              /* STATUS */
        memset(r, 0, 25);
        r[0] = 2; r[1] = 192; r[2] = 168; r[3] = 1; r[4] = 60;
        r[5] = (uint8_t)-54;
        r[6] = 0x34; r[7] = 0x12;           /* heap 0x1234 KB */
        r[8] = 1;
        r[13] = 0x78; r[14] = 0x56; r[15] = 0x34; r[16] = 0x12;
        put(0x81, r, (unsigned)status_len);
        break;
    case 0x05:                              /* TIME */
        r[0] = 0x00; r[1] = 0xF1; r[2] = 0x53; r[3] = 0x65;    /* 1700000000 */
        put(0x85, r, 4);
        break;
    case 0x0F:                              /* LOCALTIME */
        r[0] = 1; r[1] = 1;
        r[2] = 0xDD; r[3] = 0x3E; r[4] = 0x1E; r[5] = 0xE7;    /* 3877519069 */
        put(0x8F, r, 6);
        break;
    case 0x30:                              /* HASH: echo a fixed digest */
        for (i = 0; i < 32; ++i) r[i] = (uint8_t)(0xA0 + i);
        put(0xB0, r, 32);
        break;
    case 0x31:                              /* RANDOM */
        for (i = 0; i < p[0]; ++i) r[i] = pat(i);
        put(0xB1, r, p[0]);
        break;
    case 0x70:                              /* STREAM_OPEN, deferred */
        memcpy(open_url, p + 1, p[0]);
        open_url[p[0]] = 0;
        if (open_delay) {
            open_pending = 1;
        } else {
            open_reply();
        }
        break;
    case 0x71:                              /* STREAM_READ */
        if (read_delay) {
            read_pending = p[1] | (p[2] << 8);
            read_idle0 = idle_calls;
        } else {
            read_reply(p[1] | (p[2] << 8));
        }
        break;
    case 0x72: {                            /* STREAM_CLOSE */
        uint8_t cr[2] = { 0, (uint8_t)(stream_open ? 0 : 18) };
        stream_open = 0;
        put(0xF2, cr, 2);
        break;
    }
    default: {                              /* UNSUPPORTED, from the parser */
        uint8_t e[8] = { 0xFF, 11, 5, 'n', 'o', 'p', 'e', '!' };
        put(0x93, e, sizeof e);
    }
    }
}

/* ---- the primitives cogs.c is built on ---- */

void cogs_set_slot(uint8_t slot) { (void)slot; }
uint8_t cogs_probe(uint8_t slot) { return slot == 4; }
uint8_t cogs_status(void) { return (uint8_t)(qh != qt ? 0x80 : 0) | 0x40 | 0x10; }
int cogs_get(void) { return qh != qt ? q[qh++] : -1; }
uint8_t cogs_getn(uint8_t *dst, uint8_t n)
{
    while (n--) {
        if (qh == qt) return 1;
        *dst++ = q[qh++];
    }
    return 0;
}
uint8_t cogs_skip(uint16_t n)
{
    while (n--) {
        if (qh == qt) return 1;
        ++qh;
    }
    return 0;
}
uint8_t cogs_putn(const uint8_t *src, uint16_t n)
{
    while (n--) {
        fr[frn++] = *src++;
        if (frn >= 3 && frn == 3u + (fr[1] | (fr[2] << 8))) {
            card_frame();
            frn = 0;
        }
    }
    return 0;
}
uint8_t cogs_resync(void) { qh = qt = frn = 0; ++nresync; return 0; }
void cogs_irq_off(void) {}

/* kbd.c's type-ahead: count the times cogs.c looks for a key. */
static unsigned nkeypoll;
void key_poll(void) { ++nkeypoll; }

/* ---- tests ---- */

static void test_commands(void)
{
    cogs_ping_t p;
    cogs_netstat_t s;
    uint32_t t;
    uint8_t valid, buf[256], dig[32], data[1000];
    unsigned i;

    reset_model();
    nresync = 0;
    CHECK(cogs_open(4) == 0 && cogs_slot == 4 && nresync == 1);

    memset(&p, 0, sizeof p);
    CHECK(cogs_ping(&p) == 0);
    CHECK(p.fw_major == 1 && p.fw_minor == 39 && p.proto == 4 && p.caps == 0xFF);
    CHECK(qh == qt);

    CHECK(cogs_netstat(&s) == 0);
    CHECK(s.link == 2 && s.ip[0] == 192 && s.ip[3] == 60 && s.rssi == -54);
    CHECK(s.heap_kb == 0x1234 && s.sockets == 1);
    CHECK(s.has_uptime && s.uptime == 0x12345678UL);

    status_len = 9;                         /* oldest firmware's short form */
    CHECK(cogs_netstat(&s) == 0 && s.link == 2 && !s.has_uptime && s.uptime == 0);
    status_len = 25;                        /* a future, longer form */
    CHECK(cogs_netstat(&s) == 0 && s.has_uptime && qh == qt);
    status_len = 17;

    CHECK(cogs_time(&t) == 0 && t == 1700000000UL);
    CHECK(cogs_localtime(&valid, &t) == 0 && valid == 1 && t == 3877519069UL);

    CHECK(cogs_random(buf, 255) == 0);
    for (i = 0; i < 255 && buf[i] == pat(i); ++i) {}
    CHECK(i == 255);
    CHECK(last_len == 1 && last_payload[0] == 255);

    for (i = 0; i < sizeof data; ++i) data[i] = pat(i);
    CHECK(cogs_sha256(data, sizeof data, dig) == 0);
    CHECK(dig[0] == 0xA0 && dig[31] == 0xBF);
    CHECK(last_len == 1003 && last_payload[0] == 0 &&
          last_payload[1] == (1000 & 255) && last_payload[2] == (1000 >> 8) &&
          !memcmp(last_payload + 3, data, 1000));
    CHECK(qh == qt);
}

static void test_events_and_errors(void)
{
    cogs_ping_t p;
    uint8_t buf[8];

    /* Events (one with a 300-byte payload) and a socket ERROR ahead of
     * the reply are skipped, and the FIFO ends empty. */
    reset_model();
    events_first = 1;
    CHECK(cogs_ping(&p) == 0 && p.fw_minor == 39 && qh == qt);

    /* An opcode the card does not know: ERROR with socket $FF. */
    reset_model();
    CHECK(cogs_request(0x7E, NULL, 0, NULL, 0, buf, sizeof buf, NULL) == COGS_E_UNSUPPORTED);
    CHECK(qh == qt);

    /* A reply that stops partway: resynchronise and report it. */
    reset_model();
    nresync = 0;
    truncate_reply = 1;
    CHECK(cogs_ping(&p) == COGS_E_TIMEOUT && nresync == 1);
    CHECK(cogs_lost_why == COGS_LOST_PAYLOAD && cogs_lost_op == 0x80);

    /* No reply at all. */
    reset_model();
    nresync = 0;
    silent = 1;
    CHECK(cogs_ping(&p) == COGS_E_TIMEOUT && nresync == 1);
    CHECK(cogs_lost_why == COGS_LOST_HEADER && cogs_lost_op == 0x00);

    /* A stray byte that cannot start a card frame. */
    reset_model();
    nresync = 0;
    garbage_first = 1;
    CHECK(cogs_ping(&p) == COGS_E_TIMEOUT && nresync == 1);
    CHECK(cogs_lost_why == COGS_LOST_OPCODE && cogs_lost_op == 0x42);

    /* A complete reply that is too short to parse. */
    reset_model();
    short_reply = 1;
    CHECK(cogs_ping(&p) == COGS_E_PROTO && qh == qt);

    CHECK(!strcmp(cogs_strerror(1), "NO NETWORK"));
    CHECK(!strcmp(cogs_strerror(20), "ALREADY MOUNTED"));
    CHECK(!strcmp(cogs_strerror(COGS_E_TIMEOUT), "CARD STOPPED ANSWERING"));
    CHECK(!strcmp(cogs_strerror(99), "CARD ERROR"));
}

static void test_streams(void)
{
    static uint8_t buf[6 + 4096], got[70000];
    const char *url = "https://hnrss.org/frontpage";
    cogs_chunk_t c;
    unsigned i, total, reads, empties;
    uint8_t rc;

    reset_model();
    body_len = 65000;
    for (i = 0; i < body_len; ++i) body[i] = pat(i);

    /* The open reply comes only after the card has done its network work:
     * cogs.c must wait through the idle hook, not the byte timeout. */
    cogs_idle = idle;
    open_delay = 5;
    CHECK(cogs_stream_open(url, (uint8_t)strlen(url)) == 0);
    CHECK(!strcmp(open_url, url) && idle_calls >= 5 && stream_open);

    /* Read the whole body in 512-byte chunks, with the ring found empty on
     * every other read, until END. */
    read_toggle = 1;
    total = reads = empties = 0;
    for (;;) {
        rc = cogs_stream_read(buf, 512, &c);
        CHECK(rc == 0);
        if (rc) break;
        CHECK(c.len <= 512 && c.data == buf + 6);
        if (!c.len) {
            ++empties;
            if (c.flags & COGS_SF_END) break;
            CHECK(c.flags & COGS_SF_STALLED);
            continue;
        }
        memcpy(got + total, c.data, c.len);
        total += c.len;
        ++reads;
        if (c.flags & COGS_SF_END) break;
        if (reads > 1000) break;
    }
    CHECK(total == body_len && !memcmp(got, body, body_len));
    CHECK(reads == (body_len + 511) / 512 && empties > 0);
    CHECK(qh == qt);

    /* Bigger reads, up to the card's 4096 cap. */
    CHECK(cogs_stream_close() == 0 && !stream_open);
    read_toggle = 0;
    open_delay = 0;
    CHECK(cogs_stream_open(url, (uint8_t)strlen(url)) == 0);
    total = 0;
    do {
        CHECK(cogs_stream_read(buf, 4096, &c) == 0);
        total += c.len;
    } while (!(c.flags & COGS_SF_END) && c.len);
    CHECK(total == body_len && qh == qt);

    /* Closing again: no stream open. */
    CHECK(cogs_stream_close() == 0);
    CHECK(cogs_stream_close() == COGS_E_STREAM_NONE);
    CHECK(cogs_stream_read(buf, 512, &c) == COGS_E_STREAM_NONE && c.len == 0);

    /* A 404: the open is refused, nothing is open. */
    open_result = COGS_E_REFUSED;
    CHECK(cogs_stream_open(url, (uint8_t)strlen(url)) == COGS_E_REFUSED && !stream_open);
    open_result = 0;

    /* A read the card holds back until the server answers (seen on a real
     * card with slow servers): the idle hook covers reads too. */
    reset_model();
    cogs_idle = idle;
    CHECK(cogs_stream_open(url, (uint8_t)strlen(url)) == 0);
    read_delay = 40;
    CHECK(cogs_stream_read(buf, 512, &c) == 0 && c.len == 512);
    CHECK(idle_calls >= 40 && !memcmp(c.data, body, 512));
    read_delay = 0;
    CHECK(cogs_stream_close() == 0);

    /* Without the hook, the same read gives up at the byte timeout. */
    cogs_idle = NULL;
    CHECK(cogs_stream_open(url, (uint8_t)strlen(url)) == 0);
    read_delay = 40;
    cogs_lost_why = 0;
    CHECK(cogs_stream_read(buf, 512, &c) == COGS_E_TIMEOUT);
    CHECK(cogs_lost_why == COGS_LOST_HEADER && cogs_lost_op == 0x71);
    cogs_idle = idle;

    /* An open the card never answers: the idle hook gives up, and the
     * FIFOs are flushed so a late reply cannot be mistaken for the next. */
    reset_model();
    cogs_idle = idle;
    silent = 1;
    idle_limit = 50;
    nresync = 0;
    CHECK(cogs_stream_open(url, (uint8_t)strlen(url)) == COGS_E_TIMEOUT);
    CHECK(idle_calls == 50 && nresync == 1);
    CHECK(cogs_lost_why == COGS_LOST_IDLE && cogs_lost_op == 0x70);

    cogs_idle = NULL;
}

/* A reply longer than one cogs_getn() piece is read without the idle
 * hook, so cogs.c must look for a key between pieces, or keys typed
 * meanwhile overwrite each other in the //e's one-key latch. */
static void test_type_ahead(void)
{
    static uint8_t big[600], got[600];
    unsigned i;
    uint8_t op;
    uint16_t len;

    reset_model();
    for (i = 0; i < sizeof big; ++i) {
        big[i] = (uint8_t)i;
    }
    put(0x85, big, sizeof big);
    nkeypoll = 0;
    CHECK(cogs_next(&op, &len) == 0 && op == 0x85 && len == sizeof big);
    CHECK(cogs_read(got, len) == 0 && memcmp(got, big, sizeof big) == 0);
    CHECK(nkeypoll >= 3);                   /* 255 + 255 + 90 */
}

static void test_fmt(void)
{
    char b[64];
    uint8_t ip[4] = { 192, 168, 1, 60 };

    fmt_u32(b, 0);                  CHECK(!strcmp(b, "0"));
    fmt_u32(b, 4294967295UL);       CHECK(!strcmp(b, "4294967295"));
    fmt_i8(b, -54);                 CHECK(!strcmp(b, "-54"));
    fmt_i8(b, -128);                CHECK(!strcmp(b, "-128"));
    fmt_i8(b, 7);                   CHECK(!strcmp(b, "7"));
    fmt_hex8(b, 0xA2);              CHECK(!strcmp(b, "A2"));
    fmt_ip(b, ip);                  CHECK(!strcmp(b, "192.168.1.60"));
    fmt_dur(b, 65);                 CHECK(!strcmp(b, "1m 5s"));
    fmt_dur(b, 3 * 3600 + 120);     CHECK(!strcmp(b, "3h 2m"));
    fmt_dur(b, 2 * 86400UL + 5);    CHECK(!strcmp(b, "2d 0h 0m"));

    /* Vectors from Python's datetime. */
    fmt_date1904(b, 0);             CHECK(!strcmp(b, "1904-01-01 00:00:00"));
    fmt_date1904(b, 3877519325UL);  CHECK(!strcmp(b, "2026-11-14 16:42:05"));
    fmt_date1904(b, 3034713599UL);  CHECK(!strcmp(b, "2000-02-29 23:59:59"));
    fmt_date1904(b, 4294967295UL);  CHECK(!strcmp(b, "2040-02-06 06:28:15"));
    fmt_date1904(b, 1700000000UL + SECS_1904_TO_1970);
    CHECK(!strcmp(b, "2023-11-14 22:13:20"));

    {
        char *p = fmt_str(b, "IP ");
        p = fmt_ip(p, ip);
        CHECK(!strcmp(b, "IP 192.168.1.60") && *p == 0 && p == b + 15);
    }
}

int main(void)
{
    test_commands();
    test_events_and_errors();
    test_streams();
    test_type_ahead();
    test_fmt();
    if (fails) {
        printf("%d of %d checks FAILED\n", fails, checks);
        return 1;
    }
    printf("all %d checks passed\n", checks);
    return 0;
}
