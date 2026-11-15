/*
 * Host tests for src/store.c: the window of messages in aux memory.
 */
#include <stdio.h>
#include <string.h>
#include "store.h"

static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

extern unsigned long aux_bytes_moved;

/* Message n's text: its length set by the test, its bytes from n. */
static unsigned (*len_of)(unsigned n);
static char text[ST_TEXT_MAX + 1];

static const char *make(unsigned n)
{
    unsigned i, len = len_of(n);

    for (i = 0; i < len; ++i) {
        text[i] = (char)('A' + (n * 7 + i) % 26);
    }
    return text;
}

static uint8_t add(unsigned n, uint8_t older)
{
    return st_add((uint16_t)n, (uint8_t)(n & 1), "12:34", make(n), (uint16_t)len_of(n), older);
}

/* Every held message is what was added, in order, and contiguous. */
static int intact(void)
{
    static char got[ST_TEXT_MAX + 1];
    uint8_t i;

    for (i = 0; i < st_count; ++i) {
        unsigned n = st[i].n;
        if (i && n != st[i - 1].n + 1u) return 0;
        if (st[i].len != len_of(n) || st[i].mine != (n & 1)) return 0;
        if (strcmp(st[i].time, "12:34")) return 0;
        make(n);
        st_text(i, 0, st[i].len, got);
        if (memcmp(got, text, st[i].len)) return 0;
    }
    return 1;
}

static unsigned short_len(unsigned n) { return 20 + n % 40; }
static unsigned big_len(unsigned n) { return n % 3 ? 1500 : 3 + n % 50; }
static unsigned empty_len(unsigned n) { return n % 2 ? 0 : 10; }

static void test_basics(void)
{
    unsigned n;

    len_of = short_len;
    st_init();
    CHECK(st_count == 0 && !st_older && st_newest);
    for (n = 101; n <= 120; ++n) CHECK(add(n, 0));
    CHECK(st_count == 20 && intact());
    /* Only next to an end: not a gap, not one already held. */
    CHECK(!add(125, 0) && !add(110, 0) && !add(99, 1) && st_count == 20);
    /* Older at the front: existing messages move along. */
    st_shift = 0;
    CHECK(add(100, 1) && st[0].n == 100 && st_shift == 1 && intact());
    /* A part of a message. */
    {
        char part[8];
        st_text(0, 3, 5, part);
        make(100);
        CHECK(!memcmp(part, text + 3, 5));
    }
    st_reset();
    CHECK(st_count == 0 && st_free() == ST_END - ST_BASE);
}

static void test_full_index(void)
{
    unsigned n;

    len_of = short_len;
    st_reset();
    /* Newer ones beyond ST_MAX push the oldest out. */
    st_shift = 0;
    for (n = 1; n <= 100; ++n) CHECK(add(n, 0));
    CHECK(st_count == ST_MAX && st[0].n == 100 - ST_MAX + 1 && st[ST_MAX - 1].n == 100);
    CHECK(st_older && st_newest && st_shift == -(100 - ST_MAX) && intact());
    /* Older ones push the newest out. */
    st_shift = 0;
    for (n = 100 - ST_MAX; n >= 1; --n) CHECK(add(n, 1));
    CHECK(st_count == ST_MAX && st[0].n == 1 && st[ST_MAX - 1].n == ST_MAX);
    CHECK(!st_newest && intact());
}

static void test_aux_full(void)
{
    unsigned n, moved;

    /* Mostly 1,500-byte messages: aux fills long before the index, and
     * room is made by compaction and by dropping from the far end. */
    len_of = big_len;
    st_reset();
    for (n = 1; n <= 200; ++n) {
        CHECK(add(n, 0));
        CHECK(st_free() <= ST_END - ST_BASE);
    }
    CHECK(st[st_count - 1].n == 200 && st_older && intact());
    CHECK(st_count < ST_MAX && st_count > 20);
    /* Back the other way, all the way to the start. */
    moved = (unsigned)aux_bytes_moved;
    for (n = st[0].n - 1; n >= 1; --n) CHECK(add(n, 1));
    CHECK(st[0].n == 1 && !st_newest && intact());
    CHECK(aux_bytes_moved > moved);         /* compaction did happen */
}

static void test_empty_texts(void)
{
    unsigned n;

    len_of = empty_len;
    st_reset();
    for (n = 1; n <= 30; ++n) CHECK(add(n, 0));
    CHECK(st_count == 30 && intact());
}

int main(void)
{
    test_basics();
    test_full_index();
    test_aux_full();
    test_empty_texts();
    printf("store: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
