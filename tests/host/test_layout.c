/*
 * Host tests for src/layout.c: wrapping, placing each side, the time
 * line, blank lines, code kept as it is, re-wrapping at both pane widths,
 * and dropping old messages when the table fills.
 */
#include <stdio.h>
#include <string.h>
#include "layout.h"

static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

/* The texts laid out, by message number, so lines can be read back. */
static const char *texts[400];

static void add(uint16_t msg, const char *t, uint8_t mine, uint8_t meta)
{
    texts[msg] = t;
    lay_msg(msg, t, (uint16_t)strlen(t), mine, meta);
}

/* Line i's text, as the screen would draw it. */
static const char *line(uint16_t i)
{
    static char buf[100];
    const lay_line_t *l = &lay_lines[i];

    if (l->kind != LK_TEXT) {
        return l->kind == LK_META ? "<meta>" : "<blank>";
    }
    memcpy(buf, texts[l->msg] + l->off, l->len);
    buf[l->len] = 0;
    return buf;
}

/* Every line inside the text area, and none of yours wider than yours
 * may be. */
static void check_bounds(void)
{
    uint16_t i;

    for (i = 0; i < lay_count; ++i) {
        const lay_line_t *l = &lay_lines[i];
        CHECK(l->col + l->len <= lay_width);
        if (l->kind == LK_TEXT) {
            CHECK(l->len <= lay_maxw(l->mine));
        }
    }
}

static void test_widths(void)
{
    lay_reset(57);
    CHECK(lay_maxw(1) == 38);
    CHECK(lay_maxw(0) == 54);
    lay_reset(78);
    CHECK(lay_maxw(1) == 52);
    CHECK(lay_maxw(0) == 75);
}

static void test_theirs_short(void)
{
    lay_reset(57);
    add(0, "Hello there.", 0, 5);
    CHECK(lay_count == 2);
    CHECK(lay_lines[0].kind == LK_TEXT && lay_lines[0].col == 0);
    CHECK(strcmp(line(0), "Hello there.") == 0);
    CHECK(lay_lines[1].kind == LK_META && lay_lines[1].col == 0 && lay_lines[1].len == 5);
    check_bounds();
}

static void test_mine_right(void)
{
    lay_reset(57);
    add(0, "Hi", 1, 5);
    /* The block is as wide as the time, against the right edge. */
    CHECK(lay_count == 2);
    CHECK(lay_lines[0].col == 57 - 5);
    CHECK(lay_lines[1].col == 57 - 5);
    CHECK(lay_lines[0].mine && lay_lines[1].mine);

    lay_reset(57);
    add(0, "Remind me about the parcel at 14:30.", 1, 5);
    CHECK(lay_count == 2);
    CHECK(lay_lines[0].col == 57 - 36);
    CHECK(lay_lines[1].col == 57 - 5);
    check_bounds();
}

static void test_mine_wrapped_block(void)
{
    uint16_t i;

    lay_reset(57);
    add(0, "Thanks. Also, what does the OpenClaw page say about pairing a new device?", 1, 5);
    /* Two lines of at most 38, one column for both, the block's right
     * edge at the pane's. */
    CHECK(lay_count == 3);
    CHECK(lay_lines[0].col == lay_lines[1].col);
    for (i = 0; i < 2; ++i) {
        CHECK(lay_lines[i].len <= 38);
    }
    CHECK(lay_lines[0].col + (lay_lines[0].len > lay_lines[1].len ? lay_lines[0].len : lay_lines[1].len) == 57);
    CHECK(strcmp(line(0), "Thanks. Also, what does the OpenClaw") == 0);
    CHECK(strcmp(line(1), "page say about pairing a new device?") == 0);
    check_bounds();
}

static void test_blank_between(void)
{
    lay_reset(57);
    add(0, "one", 0, 5);
    add(1, "two", 1, 5);
    add(2, "three", 0, 5);
    CHECK(lay_count == 8);
    CHECK(lay_lines[2].kind == LK_BLANK && lay_lines[2].msg == 1);
    CHECK(lay_lines[5].kind == LK_BLANK && lay_lines[5].msg == 2);
    CHECK(strcmp(line(6), "three") == 0);
}

static void test_code_kept(void)
{
    const char *t = "Here:\n\n        lda #$00\nloop:   sta $0200,y\n        bne loop";

    lay_reset(57);
    add(0, t, 0, 5);
    CHECK(lay_count == 6);
    CHECK(strcmp(line(0), "Here:") == 0);
    CHECK(lay_lines[1].kind == LK_TEXT && lay_lines[1].len == 0);
    CHECK(strcmp(line(2), "        lda #$00") == 0);
    CHECK(strcmp(line(3), "loop:   sta $0200,y") == 0);
    CHECK(strcmp(line(4), "        bne loop") == 0);
}

static void test_long_word_split(void)
{
    char t[200];
    uint16_t i;

    lay_reset(57);
    strcpy(t, "see https://docs.openclaw.ai/channels/pairing-and-allowlists-for-every-channel-and-more ok");
    add(0, t, 0, 5);
    check_bounds();
    CHECK(strcmp(line(0), "see") == 0);
    CHECK(lay_lines[1].len == 54);
    /* Nothing lost: the lines, joined, are the text less its breaks. */
    {
        char joined[200] = "";
        for (i = 0; i < lay_count && lay_lines[i].kind == LK_TEXT; ++i) {
            strcat(joined, line(i));
        }
        CHECK(strcmp(joined, "seehttps://docs.openclaw.ai/channels/pairing-and-allowlists-for-every-channel-and-more ok") == 0);
    }
}

static void test_indent_not_a_break(void)
{
    /* A long code line with only its indent to break at is split, not
     * turned into a line of spaces. */
    char t[100];

    memset(t, 'x', 70);
    memset(t, ' ', 4);
    t[70] = 0;
    lay_reset(57);
    add(0, t, 0, 0);
    CHECK(lay_lines[0].len == 54);
    CHECK(memcmp(line(0), "    xxxx", 8) == 0);
}

static void test_space_at_width(void)
{
    /* A space exactly at the width lets the line fill it. */
    char t[100];

    memset(t, 'a', 54);
    t[54] = ' ';
    strcpy(t + 55, "bb");
    lay_reset(57);
    add(0, t, 0, 0);
    CHECK(lay_lines[0].len == 54);
    CHECK(strcmp(line(1), "bb") == 0);
}

static void test_rewrap(void)
{
    const char *t = "Good morning. The house is at 19 degrees and the front door is "
                    "locked. Rain is expected after 16:00, so the garden watering is "
                    "skipped today.";
    uint16_t narrow, wide;

    lay_reset(57);
    add(0, t, 0, 5);
    narrow = lay_count;
    check_bounds();
    lay_reset(78);
    add(0, t, 0, 5);
    wide = lay_count;
    check_bounds();
    CHECK(narrow == 4);
    CHECK(wide == 3);
}

static void test_drop_oldest(void)
{
    uint16_t i;

    lay_reset(57);
    for (i = 0; i < 300; ++i) {
        add(i, (i & 1) ? "a message of mine" : "one from the agent", (uint8_t)(i & 1), 5);
    }
    CHECK(lay_dropped);
    CHECK(lay_count <= LAY_MAX);
    /* Whole messages only: the table starts with a message's first line
     * and ends with the newest message's time. */
    CHECK(lay_lines[0].kind == LK_TEXT);
    CHECK(lay_lines[lay_count - 1].kind == LK_META && lay_lines[lay_count - 1].msg == 299);
    CHECK(!lay_cut);
    check_bounds();
}

static void test_too_long(void)
{
    static char t[4000];
    uint16_t i;

    for (i = 0; i < 300; ++i) {
        t[i * 2] = 'w';
        t[i * 2 + 1] = '\n';
    }
    t[600] = 0;
    lay_reset(57);
    add(0, "earlier", 0, 5);
    add(1, t, 0, 5);
    CHECK(lay_cut);
    CHECK(lay_count == LAY_MAX);
    CHECK(lay_lines[LAY_MAX - 1].kind == LK_META);
    CHECK(lay_lines[0].msg == 1);
}

static void test_empty(void)
{
    lay_reset(57);
    add(0, "", 1, 3);
    CHECK(lay_count == 2);
    CHECK(lay_lines[0].kind == LK_TEXT && lay_lines[0].len == 0);
    CHECK(lay_lines[1].col == 57 - 3);
}

int main(void)
{
    test_widths();
    test_theirs_short();
    test_mine_right();
    test_mine_wrapped_block();
    test_blank_between();
    test_code_kept();
    test_long_word_split();
    test_indent_not_a_break();
    test_space_at_width();
    test_rewrap();
    test_drop_oldest();
    test_too_long();
    test_empty();
    printf("layout: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
