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
/* A script byte of IDLE means "no key for a while": key_ready() says no
 * for that many calls, and the screen's polling loop runs meanwhile. */
#define IDLE        0xFF
#define IDLE_CALLS  400
static unsigned idle_left;

uint8_t key_ready(void)
{
    if (idle_left) {
        --idle_left;
        return 0;
    }
    if (script_i < script_n && script[script_i] == IDLE) {
        ++script_i;
        idle_left = IDLE_CALLS;
        return 0;
    }
    return 1;
}
void key_flush(void) {}
void key_poll(void) {}

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

/* Type text into the script; returns the count. */
static unsigned type(uint8_t *keys, const char *text)
{
    unsigned n = 0;
    while (*text) keys[n++] = (uint8_t)*text++;
    return n;
}

static void test_send_poll(void)
{
    static uint8_t keys[200], oa[200];
    unsigned n, r, polls_before;
    char rows[SCR_ROWS][SCR_COLS + 1];

    memset(oa, 0, sizeof oa);
    /* At the newest (where test_screen left it). */
    keys[0] = '>'; oa[0] = 1;
    run(keys, oa, 1);
    memset(oa, 0, sizeof oa);

    /* Typed and sent: it joins the conversation at the foot, the compose
     * line empties, and the heading says the agent is thinking. */
    n = type(keys, "Hello from the test");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    CHECK(on_screen("Hello from the test"));
    CHECK(on_screen("thinking..."));
    CHECK(strstr(screen[BOTTOM + 2], "> ") && !strstr(screen[BOTTOM + 2], "Hello"));
    CHECK(conv_total == CONV_N + 1 && extra_mine[0] == 1);
    CHECK(!strncmp(extra_text[0], "Hello from the test", 19));

    /* Left alone, it polls; the answer arrives and thinking ends. */
    polls_before = (unsigned)hashes;
    keys[0] = IDLE; keys[1] = IDLE;
    run(keys, oa, 2);
    CHECK((unsigned)hashes > polls_before);
    CHECK(on_screen("echo: Hello from the test"));
    CHECK(!on_screen("thinking..."));

    /* Scrolled back: a down arrow at the heading's right. */
    CHECK(screen[0][78] != '#');
    for (n = 0; n < 6; ++n) keys[n] = K_UP;
    run(keys, oa, 6);
    CHECK(screen[0][78] == '#' && !on_screen("scrolled back"));
    /* A message arrives: the screen stays still, and a diamond goes
     * beside the agent's name, in the heading and in the list. */
    CHECK(screen[0][27] == ' ' && screen[2][6] == ' ');
    memcpy(rows, screen, sizeof screen);
    conv_append(0, "unprompted news", 15);
    keys[0] = IDLE;
    run(keys, oa, 1);
    for (r = TOP; r <= BOTTOM; ++r) {
        CHECK(!strcmp(screen[r] + 21, rows[r] + 21));
    }
    CHECK(!strncmp(screen[0] + 22, "main #", 6));     /* heading: main, diamond */
    CHECK(!strncmp(screen[2], ">main #", 7));          /* list */
    CHECK(!on_screen("unprompted news"));
    for (n = 0; n < 30; ++n) keys[n] = K_DOWN;
    run(keys, oa, 30);
    CHECK(on_screen("unprompted news"));
    /* At the newest: seen, so the diamonds and the arrow go. */
    CHECK(screen[0][27] == ' ' && screen[2][6] == ' ' && screen[0][78] != '#');

    /* Another agent's news comes with the poll of this one: a diamond
     * and a count beside it in the list. Open-Apple-N goes there, and
     * the marks go. */
    gw_unread_coder = 2;
    keys[0] = IDLE;
    run(keys, oa, 1);
    CHECK(!strncmp(screen[3], " coder #", 8) && screen[3][18] == '2');
    CHECK(screen[0][27] == ' ');                        /* main has nothing new */
    keys[0] = 'n'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(!strncmp(screen[3], ">coder  ", 8) && screen[3][18] == ' ');
    CHECK(!strncmp(screen[0] + 22, "coder", 5));
    keys[0] = IDLE;
    run(keys, oa, 1);
    CHECK(gw_unread_coder == 0 && !strncmp(screen[3], ">coder  ", 8));
    keys[0] = 'n'; oa[0] = 1;
    run(keys, oa, 1);                                   /* nothing new: stays */
    CHECK(!strncmp(screen[0] + 22, "coder", 5));
    /* back to main for the rest */
    keys[0] = K_TAB; keys[1] = K_UP; keys[2] = K_RETURN;
    oa[0] = oa[1] = oa[2] = 0;
    run(keys, oa, 3);
    CHECK(!strncmp(screen[0] + 22, "main", 4));
    keys[0] = '>'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;

    /* Sending from far back goes to the newest end first. */
    keys[0] = '<'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(!on_screen("unprompted news"));
    n = type(keys, "Second one");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    CHECK(on_screen("Second one") && on_screen("unprompted news"));
    keys[0] = IDLE; keys[1] = IDLE;
    run(keys, oa, 2);
    CHECK(on_screen("echo: Second one"));

    /* A long message fills three compose rows and keeps its end in view. */
    for (n = 0; n < 200; ++n) keys[n] = (uint8_t)('a' + n % 26);
    run(keys, oa, n);
    CHECK(strstr(screen[SCR_ROWS - 2], "ijklmnopqr") != NULL);   /* the end typed */
    CHECK(screen[SCR_ROWS - 5][30] == '_');      /* the rule three rows up */
    keys[0] = K_RETURN;
    run(keys, oa, 1);
    CHECK(extra_len[conv_total - CONV_N - 1] == 200 && extra_mine[conv_total - CONV_N - 1]);
    CHECK(screen[SCR_ROWS - 3][30] == '_');      /* and back to one row */
}

/* Idle until n requests have been made, or give up: the count made. */
static unsigned idle_for(unsigned n, unsigned max_idles)
{
    static uint8_t keys[1];
    unsigned start = http_requests, i;

    keys[0] = IDLE;
    for (i = 0; i < max_idles && http_requests - start < n; ++i) {
        run(keys, NULL, 1);
    }
    return http_requests - start;
}

static void test_errors(void)
{
    static uint8_t keys[100], oa[100];
    char rows[SCR_ROWS][SCR_COLS + 1];
    unsigned n, r, before_n;

    memset(oa, 0, sizeof oa);
    /* The last message's answer first, so polls are 5 s apart. */
    keys[0] = keys[1] = keys[2] = IDLE;
    run(keys, oa, 3);
    CHECK(on_screen("SIGNED, NOT ENCRYPTED") && link_fails == 0 && !hist_busy);

    /* The gateway stops answering: the conversation stays on the screen,
     * the status bar says so, and polls come further and further apart. */
    memcpy(rows, screen, sizeof screen);
    http_error = 3;                                 /* connection timed out */
    CHECK(idle_for(1, 5) == 1);
    CHECK(on_screen("GATEWAY NOT ANSWERING") && link_fails == 1 && link_live);
    for (r = TOP; r <= BOTTOM; ++r) {
        CHECK(!strcmp(screen[r] + 21, rows[r] + 21));
    }
    /* Every 5 s while all is well; after failures 5, 10, 20, 40, then
     * 60 s. 30 idles of 400 frames are 200 s: 40 polls when well, but
     * 5 + 10 + 20 + 40 + 60 + 60 s take in only 6 or so now. */
    before_n = http_requests;
    keys[0] = IDLE;
    for (n = 0; n < 30; ++n) run(keys, oa, 1);
    n = http_requests - before_n;
    CHECK(n >= 4 && n <= 7);
    CHECK(link_fails == 1 + n);

    /* Sending meanwhile: refused, said, and the text kept to send again. */
    n = type(keys, "while down");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    CHECK(on_screen("TIMED OUT") && strstr(screen[BOTTOM + 2], "while down"));

    /* It comes back: the next poll finds it, and the bar is put right.
     * The text sends. */
    http_error = 0;
    CHECK(idle_for(1, 30) == 1);
    CHECK(on_screen("SIGNED, NOT ENCRYPTED") && link_fails == 0);
    keys[0] = K_RETURN;
    run(keys, oa, 1);
    CHECK(on_screen("while down") && !strstr(screen[BOTTOM + 2], "while down"));
    idle_for(2, 5);

    /* Busy: said, then gone at the next good poll. */
    gw_refuse = 9; gw_refuse_left = -1;
    idle_for(1, 5);
    CHECK(on_screen("GATEWAY BUSY") && link_live);
    gw_refuse = 0;
    idle_for(1, 30);
    CHECK(on_screen("SIGNED, NOT ENCRYPTED"));

    /* The gateway restarts with its agents in another order: the open
     * agent is followed to its new place. */
    gw_agents = "agent coder\nagent main\n";
    gw_drop_session = 1;
    idle_for(2, 5);
    CHECK(!strncmp(screen[0] + 22, "main", 4) && !strncmp(screen[3], ">main", 5));
    CHECK(!strncmp(screen[2], " coder", 6));

    /* An agent the gateway no longer has: hello again for the new list. */
    gw_agents = "agent research\nagent main\n";
    gw_refuse = 7; gw_refuse_left = 1;
    idle_for(1, 5);
    CHECK(!strncmp(screen[2], " research", 9) && !strncmp(screen[3], ">main", 5));
    CHECK(!on_screen("coder") && link_live);

    /* Revoked: the session ends, and the hello after it is refused.
     * Nothing more is tried until the user acts. */
    strcpy(gw_id, "ffffffff");
    gw_drop_session = 1;
    idle_for(2, 5);
    CHECK(!link_live && !link_waiting);
    CHECK(on_screen("THE GATEWAY DOES NOT KNOW THIS KEY"));
    CHECK(idle_for(1, 10) == 0);
    /* Paired again (here: the gateway knows it again); Open-Apple-R. */
    strcpy(gw_id, "922fadb6");
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(link_live && on_screen("SIGNED, NOT ENCRYPTED") && !strncmp(screen[0] + 22, "main", 4));

    /* Locked out: stopped, and told how long. */
    gw_refuse = 6; gw_refuse_left = -1;
    idle_for(1, 5);
    CHECK(!link_live && on_screen("WAIT 15 MINUTES"));
    CHECK(idle_for(1, 10) == 0);
    gw_refuse = 0;
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(link_live);

    /* A reply that does not check: stopped, never retried by itself. */
    gw_forge_reply = 1;
    idle_for(1, 5);
    CHECK(!link_live && on_screen("NOT SIGNED WITH THIS KEY"));
    CHECK(idle_for(1, 10) == 0);
    gw_forge_reply = 0;

    /* Starting (here: Open-Apple-R) while the gateway is down: no
     * session, but hello is tried again by itself until it answers. */
    http_error = 4;
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(!link_live && link_waiting && on_screen("TRYING AGAIN"));
    CHECK(idle_for(1, 10) == 1 && !link_live);      /* tried again, still down */
    http_error = 0;
    CHECK(idle_for(1, 30) >= 1);
    CHECK(link_live && !link_waiting && on_screen("SIGNED, NOT ENCRYPTED"));
    CHECK(!strncmp(screen[0] + 22, "main", 4) && on_screen("while down"));
    gw_agents = "agent main\nagent coder\n";
}

/* Agents named in OpenClaw: the list, the heading and the status bar
 * show the name; requests still go by id. */
static void test_names(void)
{
    static uint8_t keys[8], oa[8];
    unsigned before_n;

    gw_agents = "agent main\nagent coder\nname main Pinchy\n"
                "name coder A very long name x\nname ghost Nobody\n";
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    CHECK(link_live && !strcmp(ses_labels[0], "Pinchy") && !ses_labels[1][0]);  /* 17: too long */
    CHECK(!strcmp(agent_label(0), "Pinchy") && !strcmp(agent_label(1), "coder"));
    CHECK(!strncmp(screen[0] + 22, "Pinchy", 6) && !strncmp(screen[2], ">Pinchy", 7));
    CHECK(strstr(screen[SCR_ROWS - 1], " PINCHY ") != NULL);
    before_n = http_requests;
    keys[0] = IDLE;
    run(keys, NULL, 1);
    CHECK(http_requests > before_n && strstr(gw_last_target, "a=main&") != NULL);
    gw_agents = "agent main\nagent coder\nname coder Thirteen chars\n";
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    CHECK(!strncmp(screen[3], " Thirteen char ", 15) && !ses_labels[0][0]);   /* cut to 13 */
    gw_agents = "agent main\nagent coder\n";
}

/* The glyph bytes: MouseText, or plain stand-ins without it. */
static void test_glyphs(void)
{
    has_mousetext = 1;
    CHECK(sc_char(GL_CHECK, 0) == MT_CHECK && sc_char(GL_DOWN, 1) == MT_DOWN);
    CHECK(sc_char(GL_DIAMOND, 0) == MT_DIAMOND && sc_char(OA[0], 0) == MT_OAPPLE);
    has_mousetext = 0;
    CHECK(sc_char(GL_CHECK, 0) == ('+' | 0x80) && sc_char(GL_DOWN, 0) == ('v' | 0x80));
    CHECK(sc_char(GL_DIAMOND, 1) == '*' && sc_char(OA[0], 0) == MT_OAPPLE);
    CHECK(sc_char(0x05, 0) == ('?' | 0x80));
    has_mousetext = 1;
}

int main(void)
{
    cogs_slot = 4;
    test_glyphs();
    test_screen();
    test_send_poll();
    test_errors();
    test_names();
    printf("convo: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
