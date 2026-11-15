/*
 * Host tests for the protocol's C side: src/proto.c (key, hex, replies),
 * src/hmac.c (on a model card that really hashes), and src/cogs_http.c
 * (HTTP and PREFS on that model). Known answers come from the plugin's
 * TypeScript (plugin/src/protocol.ts) and RFC 4231, so the two
 * implementations check each other.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cogs.h"
#include "cogs_http.h"
#include "hmac.h"
#include "proto.h"
#include "sha256.h"

static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

/* ---- the model card: HASH, HTTP, PREFS ---- */

static uint8_t  q[65536];
static unsigned qh, qt;
static uint8_t  fr[4096];
static unsigned frn;
static unsigned hashes;

/* What the next HTTP request is answered with. */
static uint8_t     http_body[8192];
static unsigned    http_len;
static uint16_t    http_status = 200;
static uint8_t     http_error;        /* nonzero: an ERROR event on socket 0 instead */
static int         http_noise;        /* events for other sockets around ours */

/* What the last HTTP request carried. */
static uint8_t  req_sock, req_flags;
static char     req_method[16], req_url[256], req_hdrs[512];
static uint8_t  req_body[2048];
static unsigned req_blen;

static uint8_t  prefs[32];
static unsigned prefs_len;

static void put(uint8_t op, const uint8_t *p, unsigned n)
{
    if (qh == qt) {
        qh = qt = 0;
    }
    q[qt++] = op;
    q[qt++] = (uint8_t)n;
    q[qt++] = (uint8_t)(n >> 8);
    memcpy(q + qt, p, n);
    qt += n;
}

static void http_frame(const uint8_t *p, unsigned len)
{
    unsigned i = 0, n, sent;
    uint8_t d[1025], done[7], err[5];

    req_sock = p[i++];
    req_flags = p[i++];
    n = p[i++]; memcpy(req_method, p + i, n); req_method[n] = 0; i += n;
    n = p[i++]; memcpy(req_url, p + i, n); req_url[n] = 0; i += n;
    n = p[i] | (p[i + 1] << 8); i += 2; memcpy(req_hdrs, p + i, n); req_hdrs[n] = 0; i += n;
    n = p[i] | (p[i + 1] << 8); i += 2; memcpy(req_body, p + i, n); req_blen = n; i += n;
    if (i != len) {
        printf("model: HTTP frame length %u, fields %u\n", len, i);
        exit(2);
    }
    if (http_noise) {
        d[0] = 2; memset(d + 1, 'x', 40);
        put(0x91, d, 41);                       /* DATA for socket 2 */
        err[0] = 1; err[1] = 3; err[2] = 2; err[3] = 'n'; err[4] = 'o';
        put(0x93, err, 5);                      /* ERROR for socket 1 */
    }
    if (http_error) {
        err[0] = 0; err[1] = http_error; err[2] = 2; err[3] = 'n'; err[4] = 'o';
        put(0x93, err, 5);
        return;
    }
    for (sent = 0; sent < http_len; sent += n) {
        n = http_len - sent > 1024 ? 1024 : http_len - sent;
        d[0] = 0;
        memcpy(d + 1, http_body + sent, n);
        put(0x91, d, n + 1);
        if (http_noise) {
            uint8_t ws = 2;
            put(0x94, &ws, 1);                  /* WIFI_STATE between */
        }
    }
    if (http_noise) {
        done[0] = 3; memset(done + 1, 0, 6);
        put(0x95, done, 7);                     /* another socket's DONE */
    }
    done[0] = 0;
    done[1] = (uint8_t)http_status; done[2] = (uint8_t)(http_status >> 8);
    done[3] = (uint8_t)http_len; done[4] = (uint8_t)(http_len >> 8); done[5] = 0; done[6] = 0;
    put(0x95, done, 7);
}

static void card_frame(void)
{
    unsigned len = fr[1] | (fr[2] << 8), n;
    const uint8_t *p = fr + 3;
    uint8_t r[40];

    switch (fr[0]) {
    case 0x30:                                  /* HASH */
        n = p[1] | (p[2] << 8);
        if (n != len - 3 || n > 2045) {
            uint8_t e[3] = { 0xFF, 10, 0 };
            put(0x93, e, 3);
            break;
        }
        sha256(p + 3, n, r);
        ++hashes;
        put(0xB0, r, 32);
        break;
    case 0x20:
        http_frame(p, len);
        break;
    case 0x06:                                  /* PREFS_GET */
        r[0] = 0;
        memcpy(r + 1, prefs, prefs_len);
        put(0x86, r, 1 + prefs_len);
        break;
    case 0x07:                                  /* PREFS_SET */
        r[0] = len > 32 ? 9 : 0;
        if (len <= 32) {
            memcpy(prefs, p, len);
            prefs_len = len;
        }
        put(0x87, r, 1);
        break;
    default: {
        uint8_t e[3] = { 0xFF, 11, 0 };
        put(0x93, e, 3);
    }
    }
}

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
uint8_t cogs_resync(void) { qh = qt = frn = 0; return 0; }
void cogs_irq_off(void) {}

/* ---- helpers ---- */

static void unhex(uint8_t *dst, const char *hex)
{
    CHECK(a2_unhex(dst, hex, (uint8_t)(strlen(hex) / 2)) == 0);
}

static cogs_part_t part(const void *p, uint16_t n)
{
    cogs_part_t x;
    x.p = (const uint8_t *)p;
    x.n = n;
    return x;
}

/* ---- tests ---- */

static const struct { const char *written, *hex, *id; } keys[] = {
    { "00000-00000-00000-00000-00000-00", "00000000000000000000000000000000", "bf211603" },
    { "ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ-WU", "ffffffffffffffffffffffffffffffff", "410a1288" },
    { "008J4-CT4AN-K7F24-SNAXW-SQFEZ-WX", "00112233445566778899aabbccddeeff", "922fadb6" },
    { "1WF2T-F2BB9-MQH1W-PMPTC-7MQ1Y-08", "0f1e2d3c4b5a69788796a5b4c3d2e1f0", "9dc217a6" },
    { "HEPZ0-3EYNP-ZEZJQ-YQAZ1-4D2PF-0C", "8badf00ddeadbeefcafebabe12345678", "8ebbd877" },
};

static void test_keys(void)
{
    uint8_t key[16], want[16], dig[32];
    char hex[40], w[40];
    cogs_part_t p[2];
    unsigned i, pos;
    const char *alpha = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    for (i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        CHECK(a2_key_parse(keys[i].written, key) == 0);
        unhex(want, keys[i].hex);
        CHECK(memcmp(key, want, 16) == 0);
        /* the device id, hashed by the card */
        p[0] = part("twoegram-id", 11);
        p[1] = part(key, 16);
        CHECK(sha256_parts(p, 2, dig) == 0);
        a2_hex(hex, dig, 4);
        CHECK(strcmp(hex, keys[i].id) == 0);
    }
    /* Sloppy typing is forgiven. */
    CHECK(a2_key_parse("008j4 ct4an k7f24 snaxw sqfez wx", key) == 0);
    CHECK(a2_key_parse("oo8J4CT4ANK7F24SNAXWSQFEZWX", key) == 0);
    CHECK(a2_key_parse("0O8J4-CT4AN-K7F24-SNAXW-SQFEZ-WX", key) == 0);
    /* Every one-character slip in the key part is caught. */
    strcpy(w, "008J4CT4ANK7F24SNAXWSQFEZWX");
    for (pos = 0; pos < 26; ++pos) {
        char was = w[pos];
        const char *c;
        for (c = alpha; *c; ++c) {
            if (*c == was) continue;
            w[pos] = *c;
            CHECK(a2_key_parse(w, key) == A2_KEY_BADCHECK);
        }
        w[pos] = was;
    }
    CHECK(a2_key_parse("008J4CT4ANK7F24SNAXWSQFEZWX", key) == 0);
    CHECK(a2_key_parse("008J4CT4ANK7F24SNAXWSQFEZW", key) == A2_KEY_BADCHAR);
    CHECK(a2_key_parse("008J4CT4ANK7F24SNAXWSQFEZWXX", key) == A2_KEY_BADCHAR);
    CHECK(a2_key_parse("U08J4CT4ANK7F24SNAXWSQFEZWX", key) == A2_KEY_BADCHAR);
    CHECK(a2_key_parse("008J4CT4ANK7F24SNAXWSQFEZW!", key) == A2_KEY_BADCHAR);
    CHECK(a2_key_parse("", key) == A2_KEY_BADCHAR);
}

static void test_hmac(void)
{
    uint8_t key[16], cn[16], sn[16], sk[32], mac[32], want[32], big[2100];
    cogs_part_t p[6];
    uint8_t seq[4] = { 0, 0, 0, 7 };
    const char *body = "Hello from the //e";

    /* RFC 4231 test case 2 */
    p[0] = part("what do ya want for nothing?", 28);
    CHECK(hmac_parts((const uint8_t *)"Jefe", 4, p, 1, mac) == 0);
    unhex(want, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    CHECK(memcmp(mac, want, 32) == 0);
    /* The same message in pieces gives the same answer. */
    p[0] = part("what do ya ", 11);
    p[1] = part("want for not", 12);
    p[2] = part("hing?", 5);
    CHECK(hmac_parts((const uint8_t *)"Jefe", 4, p, 3, mac) == 0);
    CHECK(memcmp(mac, want, 32) == 0);
    /* RFC 4231 test case 6: a key longer than the block is hashed first by
     * the caller; here only check a full 64-byte key is accepted. */
    memset(big, 0xaa, 64);
    p[0] = part("x", 1);
    CHECK(hmac_parts(big, 64, p, 1, mac) == 0);
    CHECK(hmac_parts(big, 65, p, 1, mac) == COGS_E_TOOBIG);

    /* The protocol's signatures, against plugin/src/protocol.ts. */
    unhex(key, "00112233445566778899aabbccddeeff");
    unhex(cn, "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    unhex(sn, "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf");
    p[0] = part("hello", 5);
    p[1] = part(cn, 16);
    CHECK(hmac_parts(key, 16, p, 2, mac) == 0);
    unhex(want, "f844558d60b5a46fe81b5bd67e8bd54b21d125e97d804cb8dee78d2be3960428");
    CHECK(memcmp(mac, want, 32) == 0);

    p[0] = part("session", 7);
    p[1] = part(cn, 16);
    p[2] = part(sn, 16);
    CHECK(hmac_parts(key, 16, p, 3, sk) == 0);
    unhex(want, "43fa398d261277a3093738ac35f4807b25a51b3cc17c32c03ac57d3a81e5ce1a");
    CHECK(memcmp(sk, want, 32) == 0);

    p[0] = part("q", 1);
    p[1] = part(seq, 4);
    p[2] = part("POST /a2/send?a=main", 20);
    p[3] = part("\n", 1);
    p[4] = part(body, (uint16_t)strlen(body));
    CHECK(hmac_parts(sk, 32, p, 5, mac) == 0);
    unhex(want, "463804e74d33feb97785f32b11c7b8e07f1ac4c598271ee9f250b0731451bb2b");
    CHECK(memcmp(mac, want, 32) == 0);

    p[0] = part("h", 1);
    p[1] = part("sid 01234567\n", 13);
    CHECK(hmac_parts(sk, 32, p, 2, mac) == 0);
    unhex(want, "33f5362e02f68f3c7a1706e796cb2b1e0b4a7b0496163a2baa569c9302156dba");
    CHECK(memcmp(mac, want, 32) == 0);

    /* The largest message one HASH holds, and one byte over. */
    memset(big, 'z', sizeof big);
    p[0] = part(big, HMAC_MSG_MAX);
    CHECK(hmac_parts(sk, 32, p, 1, mac) == 0);
    p[0] = part(big, HMAC_MSG_MAX + 1);
    CHECK(hmac_parts(sk, 32, p, 1, mac) == COGS_E_TOOBIG);
}

static const char signed_reply[] =
    "A2 1 0 81fbdbb3d6f6f9f69f85a1634de326930c6dfc36ececba89a4366c4e901c0107\n"
    "more 0\nbusy 1\nunread coder 2\nmsg 3 0 14:01 9\ntwo\nlines\nmsg 4 1 14:02 0\n\n";

static void test_reply(void)
{
    a2_reply_t r;
    a2_iter_t it;
    a2_item_t item;
    uint8_t sk[32], mac[32];
    uint8_t seq[4] = { 0, 0, 0, 7 };
    cogs_part_t p[3];
    const uint8_t *b = (const uint8_t *)signed_reply;
    uint16_t n = (uint16_t)strlen(signed_reply);

    CHECK(a2_reply(b, n, &r) == 0);
    CHECK(r.code == 0 && r.signed_);
    /* Verify it as the Apple will. */
    unhex(sk, "43fa398d261277a3093738ac35f4807b25a51b3cc17c32c03ac57d3a81e5ce1a");
    p[0] = part("r", 1);
    p[1] = part(seq, 4);
    p[2] = part(r.payload, r.plen);
    CHECK(hmac_parts(sk, 32, p, 3, mac) == 0);
    CHECK(memcmp(mac, r.mac, 32) == 0);

    a2_iter(&it, &r);
    CHECK(a2_next(&it, &item) == 1 && a2_is(&item, "more") && item.vlen == 1 && item.val[0] == '0');
    CHECK(a2_next(&it, &item) == 1 && a2_is(&item, "busy") && a2_num(item.val, item.vlen) == 1);
    CHECK(a2_next(&it, &item) == 1 && a2_is(&item, "unread") && item.vlen == 7 && !memcmp(item.val, "coder 2", 7));
    CHECK(a2_next(&it, &item) == 1 && item.kind == A2_ITEM_MSG && item.n == 3 && !item.mine &&
          !strcmp(item.time, "14:01") && item.tlen == 9 && !memcmp(item.text, "two\nlines", 9));
    CHECK(a2_next(&it, &item) == 1 && item.kind == A2_ITEM_MSG && item.n == 4 && item.mine && item.tlen == 0);
    CHECK(a2_next(&it, &item) == 0);

    /* Unsigned, with a reason. */
    CHECK(a2_reply((const uint8_t *)"A2 1 4 -\nwhy no such session\n", 29, &r) == 0);
    CHECK(r.code == 4 && !r.signed_);
    /* Malformed headers. */
    CHECK(a2_reply((const uint8_t *)"A2 1 0 abc\n", 11, &r) != 0);
    CHECK(a2_reply((const uint8_t *)"A2 2 0 -\n", 9, &r) != 0);
    CHECK(a2_reply((const uint8_t *)"HTTP/1.1 200\n", 13, &r) != 0);
    CHECK(a2_reply((const uint8_t *)"A2 1 0 -", 8, &r) != 0);
    /* A record whose length runs past the end. */
    CHECK(a2_reply((const uint8_t *)"A2 1 0 -\nmsg 1 0 10:00 50\nabc\n", 30, &r) == 0);
    a2_iter(&it, &r);
    CHECK(a2_next(&it, &item) == A2_ITER_BAD);
    CHECK(a2_reply((const uint8_t *)"A2 1 0 -\nmsg 1 2 10:00 0\n\n", 26, &r) == 0);
    a2_iter(&it, &r);
    CHECK(a2_next(&it, &item) == A2_ITER_BAD);
    /* A line without a newline. */
    CHECK(a2_reply((const uint8_t *)"A2 1 0 -\nmore 0", 15, &r) == 0);
    a2_iter(&it, &r);
    CHECK(a2_next(&it, &item) == A2_ITER_BAD);

    CHECK(a2_num("65534", 5) == 65534 && a2_num("65535", 5) == 0xFFFF && a2_num("1x", 2) == 0xFFFF);
}

static void test_http(void)
{
    static uint8_t buf[1900];
    uint16_t got, status;
    unsigned i;
    const char *hdrs = "X-A2-Session: 01234567\r\nX-A2-Seq: 7\r\nX-A2-Mac: ab";

    /* A reply in two DATA events, with others' events around them. */
    qh = qt = 0;
    http_len = 1500;
    for (i = 0; i < http_len; ++i) http_body[i] = (uint8_t)(i * 7 + 3);
    http_noise = 1;
    http_error = 0;
    http_status = 200;
    CHECK(cogs_http("POST", "http://10.0.0.2:8417/a2/send?a=main", hdrs,
                    (const uint8_t *)"hi", 2, COGS_HTTP_KEEPALIVE,
                    buf, sizeof buf, &got, &status) == 0);
    CHECK(got == 1500 && status == 200 && !memcmp(buf, http_body, 1500));
    CHECK(req_sock == A2_SOCK && req_flags == COGS_HTTP_KEEPALIVE);
    CHECK(!strcmp(req_method, "POST") && !strcmp(req_url, "http://10.0.0.2:8417/a2/send?a=main"));
    CHECK(!strcmp(req_hdrs, hdrs) && req_blen == 2 && !memcmp(req_body, "hi", 2));
    CHECK(qh == qt);

    /* A GET with no body and no headers. */
    http_noise = 0;
    http_len = 0;
    CHECK(cogs_http("GET", "http://h/a2/hello", "", NULL, 0, 0, buf, sizeof buf, &got, &status) == 0);
    CHECK(got == 0 && status == 200 && req_blen == 0 && req_hdrs[0] == 0);

    /* Longer than the buffer: everything read, nothing overrun. */
    http_len = 3000;
    memset(buf, 0, sizeof buf);
    CHECK(cogs_http("GET", "http://h/a2/poll", "", NULL, 0, 0, buf, sizeof buf, &got, &status) == COGS_E_TOOBIG);
    CHECK(qh == qt);

    /* The card's own failure, on our socket. */
    http_error = 4;
    CHECK(cogs_http("GET", "http://h/a2/poll", "", NULL, 0, 0, buf, sizeof buf, &got, &status) == 4);
    CHECK(qh == qt);
    http_error = 0;

    /* Another status is passed back for the caller to judge. */
    http_len = 3;
    memcpy(http_body, "abc", 3);
    http_status = 404;
    CHECK(cogs_http("GET", "http://h/x", "", NULL, 0, 0, buf, sizeof buf, &got, &status) == 0);
    CHECK(status == 404 && got == 3);
    http_status = 200;
}

static void test_prefs(void)
{
    uint8_t blob[32], n, orig[32];
    unsigned i;

    prefs_len = 0;
    CHECK(cogs_prefs_get(blob, &n) == 0 && n == 0);
    for (i = 0; i < 32; ++i) orig[i] = (uint8_t)i;
    CHECK(cogs_prefs_set(orig, 32) == 0);
    CHECK(cogs_prefs_get(blob, &n) == 0 && n == 32 && !memcmp(blob, orig, 32));
    prefs_len = 15;
    CHECK(cogs_prefs_get(blob, &n) == 0 && n == 15);
}

int main(void)
{
    test_keys();
    test_hmac();
    test_reply();
    test_http();
    test_prefs();
    printf("a2: %d checks, %d failed (%u hashes on the model card)\n", checks, fails, hashes);
    return fails != 0;
}
