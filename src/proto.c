/*
 * The protocol's bytes. See proto.h.
 */
#include <string.h>
#include "proto.h"

static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static const char checksyms[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ*~$=U";

static int8_t value_of(char c)
{
    const char *p;

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 32);
    }
    if (c == 'O') {
        c = '0';
    } else if (c == 'I' || c == 'L') {
        c = '1';
    }
    p = strchr(alphabet, c);
    return (int8_t)(p && c ? p - alphabet : -1);
}

uint8_t a2_key_parse(const char *s, uint8_t key[A2_KEY_LEN])
{
    uint8_t vals[26], n = 0, bits = 0, k = 0, i;
    uint16_t sum = 0, acc = 0;
    const char *p;
    char c;
    int8_t v;

    for (; *s; ++s) {
        c = *s;
        if (c == '-' || c == ' ') {
            continue;
        }
        if (n == 26) {
            /* the check character */
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 32);
            }
            if (c == 'O') {
                c = '0';
            } else if (c == 'I' || c == 'L') {
                c = '1';
            }
            p = strchr(checksyms, c);
            if (!p || !c) {
                return A2_KEY_BADCHAR;
            }
            for (i = 0; i < 26; ++i) {
                sum = (uint16_t)((sum + (uint16_t)(i + 1) * vals[i]) % 37);
            }
            if ((uint8_t)(p - checksyms) != sum) {
                return A2_KEY_BADCHECK;
            }
            ++n;
            continue;
        }
        if (n > 26 || (v = value_of(c)) < 0) {
            return A2_KEY_BADCHAR;
        }
        vals[n++] = (uint8_t)v;
    }
    if (n != 27) {
        return A2_KEY_BADCHAR;
    }
    if (vals[25] & 3) {
        return A2_KEY_BADCHECK;         /* the padding bits must be zero */
    }
    for (i = 0; i < 26; ++i) {
        acc = (uint16_t)((acc << 5) | vals[i]);
        bits = (uint8_t)(bits + 5);
        if (bits >= 8) {
            if (k < A2_KEY_LEN) {
                key[k++] = (uint8_t)(acc >> (bits - 8));
            }
            bits = (uint8_t)(bits - 8);
        }
        acc &= (uint16_t)((1u << bits) - 1);
    }
    return 0;
}

char *a2_hex(char *dst, const uint8_t *src, uint8_t n)
{
    static const char digits[] = "0123456789abcdef";

    while (n--) {
        *dst++ = digits[*src >> 4];
        *dst++ = digits[*src++ & 15];
    }
    *dst = 0;
    return dst;
}

static int8_t nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return (int8_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int8_t)(c - 'a' + 10);
    }
    return -1;
}

uint8_t a2_unhex(uint8_t *dst, const char *src, uint8_t n)
{
    int8_t hi, lo;

    while (n--) {
        hi = nibble(*src++);
        lo = nibble(*src++);
        if (hi < 0 || lo < 0) {
            return 1;
        }
        *dst++ = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

uint8_t a2_reply(const uint8_t *buf, uint16_t n, a2_reply_t *r)
{
    const uint8_t *p = buf, *nl = memchr(buf, '\n', n);

    if (!nl || n < 9 || memcmp(p, "A2 1 ", 5)) {
        return 1;
    }
    p += 5;
    if (*p < '0' || *p > '9') {
        return 1;
    }
    r->code = 0;
    while (*p >= '0' && *p <= '9' && r->code < 26) {
        r->code = (uint8_t)(r->code * 10 + (*p++ - '0'));
    }
    if (*p++ != ' ') {
        return 1;
    }
    if (*p == '-' && p + 1 == nl) {
        r->signed_ = 0;
    } else if (nl - p == 2 * A2_MAC_LEN && !a2_unhex(r->mac, (const char *)p, A2_MAC_LEN)) {
        r->signed_ = 1;
    } else {
        return 1;
    }
    r->payload = nl + 1;
    r->plen = (uint16_t)(buf + n - r->payload);
    return 0;
}

void a2_iter(a2_iter_t *it, const a2_reply_t *r)
{
    it->p = r->payload;
    it->end = r->payload + r->plen;
}

uint16_t a2_num(const char *s, uint8_t n)
{
    uint32_t v = 0;

    if (!n || n > 5) {
        return 0xFFFF;
    }
    while (n--) {
        if (*s < '0' || *s > '9') {
            return 0xFFFF;
        }
        v = v * 10 + (uint8_t)(*s++ - '0');
    }
    return v > 0xFFFE ? 0xFFFF : (uint16_t)v;
}

uint8_t a2_is(const a2_item_t *item, const char *w)
{
    return (uint8_t)(item->kind == A2_ITEM_LINE && strlen(w) == item->wlen &&
                     !memcmp(item->word, w, item->wlen));
}

/* The next space-separated field of [*p, end), as pointer and length. */
static uint8_t field(const char **p, const char *end, const char **f)
{
    const char *s = *p;

    *f = s;
    while (s < end && *s != ' ') {
        ++s;
    }
    *p = s < end ? s + 1 : s;
    return (uint8_t)(s - *f);
}

uint8_t a2_next(a2_iter_t *it, a2_item_t *item)
{
    const char *line = (const char *)it->p, *end, *q, *f;
    const uint8_t *nl;
    uint8_t fl;
    uint16_t len;

    if (it->p >= it->end) {
        return 0;
    }
    nl = memchr(it->p, '\n', (size_t)(it->end - it->p));
    if (!nl || nl - it->p > 255) {
        return A2_ITER_BAD;
    }
    end = (const char *)nl;
    it->p = nl + 1;
    q = line;
    item->wlen = field(&q, end, &item->word);
    item->val = q;
    item->vlen = (uint8_t)(end - q);

    if (item->wlen != 3 || memcmp(item->word, "msg", 3)) {
        item->kind = A2_ITEM_LINE;
        return 1;
    }
    /* msg <n> <mine> <hh:mm> <len>, then len bytes and a newline */
    item->kind = A2_ITEM_MSG;
    fl = field(&q, end, &f);
    if ((item->n = a2_num(f, fl)) == 0xFFFF) {
        return A2_ITER_BAD;
    }
    fl = field(&q, end, &f);
    if (fl != 1 || (*f != '0' && *f != '1')) {
        return A2_ITER_BAD;
    }
    item->mine = (uint8_t)(*f == '1');
    fl = field(&q, end, &f);
    if (fl > 5) {
        return A2_ITER_BAD;
    }
    memcpy(item->time, f, fl);
    item->time[fl] = 0;
    fl = field(&q, end, &f);
    len = a2_num(f, fl);
    if (len == 0xFFFF || q != end || (uint16_t)(it->end - it->p) < len + 1u || it->p[len] != '\n') {
        return A2_ITER_BAD;
    }
    item->text = (const char *)it->p;
    item->tlen = len;
    it->p += len + 1;
    return 1;
}
