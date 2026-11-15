/*
 * The conversation screen (src/convo.c) on the Mac: the real code, with a
 * text screen in place of the Apple's, scripted keys, and test_a2.c's
 * model card and gateway serving a 100-message conversation.
 *
 * The property checked most: one Up (or Down) moves every line on the
 * screen exactly one row, whether the window slides over held messages or
 * fetches more from the gateway; so a reader never loses their place.
 */
#define A2_NO_MAIN
#include "test_a2.c"

#include "screen.h"
#include "kbd.h"
#include "tick.h"
#include "msgs.h"
#include "layout.h"
#include "link.h"
#include "ui.h"

/* ---- the stubs: screen, keyboard, clock, disk ---- */

static char screen[SCR_ROWS][SCR_COLS + 1];

void scr_init(void) { memset(screen, ' ', sizeof screen); }
void scr_done(void) {}

void scr_blit(uint8_t row)
{
    uint8_t i, c;

    for (i = 0; i < SCR_COLS; ++i) {
        c = linebuf[i];
        if (c >= 0x80) c &= 0x7F;               /* normal */
        else if (c < 0x20) c = (uint8_t)(c + 0x40); /* inverse capitals */
        else if (c >= 0x40 && c < 0x60) c = '#'; /* MouseText */
        screen[row][i] = (char)c;
    }
    screen[row][SCR_COLS] = 0;
}

uint8_t key_oa, key_sa;
static const uint8_t *script;
static const uint8_t *script_oa;
static unsigned script_n, script_i;

uint8_t key_get(void)
{
    if (script_i >= script_n) {
        key_oa = 1;
        return 'q';
    }
    key_oa = script_oa ? script_oa[script_i] : 0;
    return script[script_i++];
}
uint8_t key_ready(void) { return 0; }
void key_flush(void) {}

uint16_t ticks;
void tick_reset(void) { ticks = 0; }
uint8_t tick_poll(void) { ++ticks; return 1; }
uint8_t tick_wait(uint16_t n) { (void)n; return 0; }

char status_net[24] = "COGS 10.0.0.9";
void net_check(void) {}
uint8_t cfg_load(void) { return 1; }
uint8_t cfg_save(void) { return 0; }

/* ---- driving it ---- */

/* Run the screen on keys, stopping (Open-Apple-Q) after the last. */
static void run(const uint8_t *keys, const uint8_t *oa, unsigned n)
{
    script = keys;
    script_oa = oa;
    script_n = n;
    script_i = 0;
    convo_run();
}

static void dump(void)
{
    int r;
    for (r = 0; r < SCR_ROWS; ++r) printf("|%s|\n", screen[r]);
    printf("store %u..%u (%u held), older %u newest %u; layout %u lines, msgs %u..%u, dropped %u\n",
           st_count ? st[0].n : 0, st_count ? st[st_count - 1].n : 0, st_count, st_older, st_newest,
           lay_count, lay_count ? lay_lines[0].msg : 0, lay_count ? lay_lines[lay_count - 1].msg : 0,
           lay_dropped);
}

static int on_screen(const char *s)
{
    int r;
    for (r = 0; r < SCR_ROWS; ++r) if (strstr(screen[r], s)) return 1;
    return 0;
}

/* The message rows: 2 to 20 with a one-row compose area. */
#define TOP     2
#define BOTTOM  20

static char before[SCR_ROWS][SCR_COLS + 1];

/* After one Up, each row shows what the row above it showed (dir 1);
 * after one Down, what the row below showed (dir -1). Only the pane
 * (from column 21) is compared: the list does not scroll. */
static int moved_one(int dir)
{
    int r;

    for (r = TOP; r <= BOTTOM; ++r) {
        int from = r - dir;
        if (from < TOP || from > BOTTOM) continue;
        if (strcmp(screen[r] + 21, before[from] + 21)) {
            printf("row %d: [%s]\n   was [%s] on row %d\n", r, screen[r] + 21, before[from] + 21, from);
            return 0;
        }
    }
    return 1;
}

static void test_screen(void)
{
    static uint8_t keys[4000], oa[4000];
    unsigned i, steps, fetches = 0;
    unsigned long hellos;

    gateway_reset();
    CHECK(ses_hello() == 0);
    CHECK(st_init() == 0);
    msgs_mode(MSGS_LIVE);
    link_live = 1;
    strcpy(link_sec, SEC_SIGNED);
    has_mousetext = 1;
    scr_init();

    /* Open: the newest page, the newest message at the foot. */
    run(keys, NULL, 0);
    CHECK(on_screen("m100 ") && on_screen("SIGNED, NOT ENCRYPTED") && on_screen("main"));
    CHECK(st[st_count - 1].n == CONV_N);

    /* Up, one line at a time, all the way to message 1: every step moves
     * the pane by exactly one row, fetches and window slides included. */
    for (steps = 0; steps < 3000 && !(st[0].n == 1 && on_screen("m1 ") && !st_older); ++steps) {
        memcpy(before, screen, sizeof screen);
        hellos = (unsigned long)gw_hellos;
        keys[0] = K_UP;
        i = st_count ? st[0].n : 0;
        run(keys, NULL, 1);
        if (st_count && st[0].n != i) ++fetches;
        if (!moved_one(1)) {
            CHECK(!"Up moved the pane by other than one row");
            dump();
            break;
        }
        CHECK((unsigned long)gw_hellos == hellos);
    }
    CHECK(on_screen("m1 ") && st[0].n == 1);
    CHECK(fetches > 3);
    printf("convo: %u steps up, %u fetches\n", steps, fetches);

    /* Past the start: it says so, and nothing moves. */
    memcpy(before, screen, sizeof screen);
    keys[0] = K_UP;
    run(keys, NULL, 1);
    CHECK(on_screen("THE START OF THE CONVERSATION"));

    /* Down, one line at a time, back to message 100. */
    fetches = 0;
    for (steps = 0; steps < 3000; ++steps) {
        memcpy(before, screen, sizeof screen);
        keys[0] = K_DOWN;
        i = st_count ? st[st_count - 1].n : 0;
        run(keys, NULL, 1);
        if (st[st_count - 1].n != i) ++fetches;
        if (!memcmp(before, screen, sizeof screen)) {
            break;                  /* the bottom: nothing moved */
        }
        if (!moved_one(-1)) {
            CHECK(!"Down moved the pane by other than one row");
            dump();
            break;
        }
    }
    CHECK(on_screen("m100 ") && st[st_count - 1].n == CONV_N && st_newest);
    CHECK(fetches > 3);
    printf("convo: %u steps down, %u fetches\n", steps, fetches);

    /* Pages: Open-Apple-Up and -Down, a pane less one row at a time. */
    for (i = 0; i < 40; ++i) { keys[i] = K_UP; oa[i] = 1; }
    run(keys, oa, 40);
    CHECK(!on_screen("m100 "));
    for (i = 0; i < 80; ++i) { keys[i] = K_DOWN; oa[i] = 1; }
    run(keys, oa, 80);
    CHECK(on_screen("m100 "));

    /* Open-Apple-< : the oldest held; Open-Apple-> : the newest. */
    keys[0] = '<'; oa[0] = 1;
    run(keys, oa, 1);
    {
        char tag[12];
        sprintf(tag, "m%u ", st[0].n);
        CHECK(on_screen(tag));
    }
    keys[0] = '>'; oa[0] = 1;
    run(keys, oa, 1);
    CHECK(on_screen("m100 ") && st_newest);

    /* The list hidden and shown re-wraps and keeps the newest in view. */
    keys[0] = 'b'; oa[0] = 1;
    keys[1] = 'b'; oa[1] = 1;
    run(keys, oa, 2);
    CHECK(on_screen("m100 ") && on_screen("AGENTS"));
}

int main(void)
{
    cogs_slot = 4;
    test_screen();
    printf("convo: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
