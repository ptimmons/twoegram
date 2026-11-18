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
#include "keystore.h"
#include "lock.h"
#include "save.h"
#include <unistd.h>

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
        else if (c == MT_UP) c = '^';           /* MouseText: the arrows, */
        else if (c == MT_DOWN) c = 'v';
        else if (c >= 0x40 && c < 0x60) c = '#'; /* the rest */
        screen[row][i] = (char)c;
    }
    screen[row][SCR_COLS] = 0;
}

uint8_t key_oa, key_sa;
static const uint8_t *script;
static const uint8_t *script_oa;
static unsigned script_n, script_i;

/* Called at every key: what the screen showed when it was asked for. */
static void (*on_key)(void);
static unsigned past_end;

uint8_t key_get(void)
{
    if (on_key) on_key();
    if (script_i >= script_n) {
        /* Open-Apple-Q ends the conversation screen; a screen that ignores
         * it (the passphrase prompt) would wait for ever. */
        if (++past_end > 50) {
            printf("FAIL: the script ran out on a screen that does not quit\n");
            exit(1);
        }
        key_oa = 1;
        return 'q';
    }
    past_end = 0;
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
static char saved_cfg[200];
uint8_t cfg_save(void)
{
    char *p = saved_cfg;
    p += sprintf(p, "# Twoegram: the gateway\rGATEWAY=");
    if (cfg_name[0]) p += sprintf(p, "%s ", cfg_name);
    sprintf(p, "%s\r", ses_base);
    return 0;
}

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
        /* columns 19 to 77, the pane: the 80th holds the arrows, which stay put */
        if (strncmp(screen[r] + 19, before[from] + 19, 59)) {
            printf("row %d: [%s]\n   was [%s] on row %d\n", r, screen[r] + 19, before[from] + 19, from);
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

    /* At the start: no up arrow, the down arrow at the pane's foot. Up
     * moves nothing and says nothing. */
    CHECK(screen[TOP][79] == ' ' && screen[BOTTOM][79] == 'v');
    memcpy(before, screen, sizeof screen);
    keys[0] = K_UP;
    run(keys, NULL, 1);
    CHECK(!memcmp(before, screen, sizeof screen));
    CHECK(!on_screen("THE START OF THE CONVERSATION"));
    /* A line down: the start is above now, so the up arrow comes. */
    keys[0] = K_DOWN;
    run(keys, NULL, 1);
    CHECK(screen[TOP][79] == '^' && screen[BOTTOM][79] == 'v');
    keys[0] = K_UP;
    run(keys, NULL, 1);
    CHECK(screen[TOP][79] == ' ');

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
    /* Column 78 stays blank between the text and the arrows. */
    for (i = TOP; i <= BOTTOM; ++i) CHECK(screen[i][78] == ' ');
    /* At the newest: the up arrow only; and none in the heading. */
    CHECK(screen[TOP][79] == '^' && screen[BOTTOM][79] == ' ');
    CHECK(screen[0][78] != 'v' && screen[0][79] == ' ');

    /* Pages: Open-Apple-Up and -Down, a pane less one row at a time. */
    for (i = 0; i < 40; ++i) { keys[i] = K_UP; oa[i] = 1; }
    run(keys, oa, 40);
    CHECK(!on_screen("m100 "));
    for (i = 0; i < 80; ++i) { keys[i] = K_DOWN; oa[i] = 1; }
    run(keys, oa, 80);
    CHECK(on_screen("m100 "));

    /* Open-Apple-< : the very start, in one request, however much is
     * older than held; Open-Apple-> : the newest, the same way. */
    CHECK(st_older && st[0].n > 1);
    hellos = (unsigned long)hashes;
    keys[0] = '<'; oa[0] = 1;
    run(keys, oa, 1);
    CHECK(on_screen("m1 ") && st[0].n == 1 && !st_older && !st_newest);
    CHECK(strstr(gw_last_target, "/a2/hist?a=main&after=0&k=") == gw_last_target);
    CHECK(!on_screen("m100 "));
    /* Down from the start fetches newer, as after paging up. */
    for (i = 0; i < 12; ++i) { keys[i] = K_DOWN; oa[i] = 1; }
    run(keys, oa, 12);
    CHECK(!on_screen("m1 ") && st[0].n >= 1);
    keys[0] = '>'; oa[0] = 1;
    run(keys, oa, 1);
    CHECK(on_screen("m100 ") && st_newest);
    /* At the start already, with nothing older: no request. */
    keys[0] = '<'; keys[1] = '<'; oa[0] = oa[1] = 1;
    run(keys, oa, 2);
    CHECK(on_screen("m1 "));
    hellos = (unsigned long)hashes;
    keys[0] = '<'; oa[0] = 1;
    run(keys, oa, 1);
    CHECK((unsigned long)hashes == hellos && on_screen("m1 "));
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

    /* Scrolled back: a down arrow at the pane's foot, in column 80. */
    CHECK(screen[BOTTOM][79] == ' ');
    for (n = 0; n < 6; ++n) keys[n] = K_UP;
    run(keys, oa, 6);
    CHECK(screen[BOTTOM][79] == 'v' && !on_screen("scrolled back"));
    /* A message arrives: the screen stays still, and a diamond goes
     * beside the agent's name, in the heading and in the list. */
    CHECK(screen[0][25] == ' ' && screen[2][17] == ' ');
    memcpy(rows, screen, sizeof screen);
    conv_append(0, "unprompted news", 15);
    keys[0] = IDLE;
    run(keys, oa, 1);
    for (r = TOP; r <= BOTTOM; ++r) {
        CHECK(!strcmp(screen[r] + 19, rows[r] + 19));
    }
    CHECK(!strncmp(screen[0] + 20, "main #", 6));     /* heading: main, diamond */
    CHECK(!strncmp(screen[2], ">main ", 6) && screen[2][17] == '#');  /* list */
    CHECK(!on_screen("unprompted news"));
    for (n = 0; n < 30; ++n) keys[n] = K_DOWN;
    run(keys, oa, 30);
    CHECK(on_screen("unprompted news"));
    /* At the newest: seen, so the diamonds and the arrow go. */
    CHECK(screen[0][25] == ' ' && screen[2][17] == ' ' && screen[BOTTOM][79] == ' ');

    /* Another agent's news comes with the poll of this one: a diamond
     * in the list's last column, and no count. Open-Apple-N goes there,
     * and the mark goes. */
    gw_unread_coder = 2;
    keys[0] = IDLE;
    run(keys, oa, 1);
    CHECK(!strncmp(screen[3], " coder ", 7) && screen[3][17] == '#' && screen[3][16] == ' ');
    CHECK(screen[0][25] == ' ');                        /* main has nothing new */
    keys[0] = 'n'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(!strncmp(screen[3], ">coder ", 7) && screen[3][17] == ' ');
    CHECK(!strncmp(screen[0] + 20, "coder", 5));
    keys[0] = IDLE;
    run(keys, oa, 1);
    CHECK(gw_unread_coder == 0 && !strncmp(screen[3], ">coder  ", 8));
    keys[0] = 'n'; oa[0] = 1;
    run(keys, oa, 1);                                   /* nothing new: stays */
    CHECK(!strncmp(screen[0] + 20, "coder", 5));
    /* back to main for the rest */
    keys[0] = K_TAB; keys[1] = K_UP; keys[2] = K_RETURN;
    oa[0] = oa[1] = oa[2] = 0;
    run(keys, oa, 3);
    CHECK(!strncmp(screen[0] + 20, "main", 4));
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
        CHECK(!strcmp(screen[r] + 19, rows[r] + 19));
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
    CHECK(!strncmp(screen[0] + 20, "main", 4) && !strncmp(screen[3], ">main", 5));
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
    CHECK(link_badkey && on_screen("Add this Apple's key on the gateway again"));
    CHECK(idle_for(1, 10) == 0);
    /* Paired again (here: the gateway knows it again); Open-Apple-R. */
    strcpy(gw_id, "922fadb6");
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(link_live && on_screen("SIGNED, NOT ENCRYPTED") && !strncmp(screen[0] + 20, "main", 4));

    /* Locked out: stopped, and told how long. */
    gw_refuse = 6; gw_refuse_left = -1;
    idle_for(1, 5);
    CHECK(!link_live && on_screen("WAIT 15 MINUTES"));
    CHECK(!link_badkey && !on_screen("Add this Apple's key on the gateway"));
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
    CHECK(!strncmp(screen[0] + 20, "main", 4) && on_screen("while down"));
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
    CHECK(!strncmp(screen[0] + 20, "Pinchy", 6) && !strncmp(screen[2], ">Pinchy", 7));
    CHECK(strstr(screen[SCR_ROWS - 1], " PINCHY ") != NULL);
    before_n = http_requests;
    keys[0] = IDLE;
    run(keys, NULL, 1);
    CHECK(http_requests > before_n && strstr(gw_last_target, "a=main&") != NULL);
    gw_agents = "agent main\nagent coder\nname coder Exactly sixteen!\n";
    keys[0] = 'r'; oa[0] = 1;
    run(keys, oa, 1);
    /* All 16 characters in the list, then the diamond's column, then the
     * divider; and in full in the heading. */
    CHECK(!strncmp(screen[3], " Exactly sixteen! ", 18) && !ses_labels[0][0]);
    CHECK(screen[3][18] != ' ');
    gw_agents = "agent main\nagent coder\n";
}

/* ---- the passphrase and the lock ---- */

static const char *watch[6];
static uint8_t seen[6];
static uint8_t leaked;          /* messages or the key while locked */

static int key_is_zero(void)
{
    unsigned i;
    for (i = 0; i < 16; ++i) if (ses_key[i]) return 0;
    return 1;
}

static void watcher(void)
{
    unsigned i;

    for (i = 0; i < 6 && watch[i]; ++i) {
        if (on_screen(watch[i])) seen[i] = 1;
    }
    if (on_screen("Twoegram is locked.") && (on_screen("while down") || !key_is_zero())) {
        leaked = 1;
    }
}

static void watch_for(const char *a, const char *b, const char *c, const char *d)
{
    watch[0] = a; watch[1] = b; watch[2] = c; watch[3] = d; watch[4] = 0;
    memset(seen, 0, sizeof seen);
    leaked = 0;
    on_key = watcher;
}

static void test_passphrase(void)
{
    static uint8_t keys[300], oa[300];
    static const uint8_t card[15] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    uint8_t plain[16], got[16];
    unsigned n, i, hellos;

    memset(oa, 0, sizeof oa);
    memcpy(plain, gw_key, 16);
    memcpy(prefs, card, 15);
    prefs[15] = KS_MARK;
    memcpy(prefs + 16, plain, 16);
    prefs_len = 32;
    lock_armed = 0;

    /* Without a passphrase, Open-Apple-L says how to give one. */
    keys[0] = 'l'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(on_screen("NO PASSPHRASE IS SET"));

    /* Set-up: too short; typed twice differently; then twice the same. */
    watch_for("AT LEAST 8", "TYPE THE PASSPHRASE AGAIN", "THE TWO DIFFER", "****");
    n = 0;
    keys[n] = 'k'; oa[n++] = 1;
    keys[n++] = K_TAB;                          /* from the key name ... */
    keys[n++] = K_TAB;                          /* ... past the address, to the passphrase */
    n += type(keys + n, "short");
    keys[n++] = K_RETURN;
    for (i = 0; i < 5; ++i) keys[n++] = K_DELETE;
    n += type(keys + n, "correct horse");
    keys[n++] = K_RETURN;
    n += type(keys + n, "correct horsf");
    keys[n++] = K_RETURN;
    n += type(keys + n, "correct horse");
    keys[n++] = K_RETURN;
    n += type(keys + n, "correct horse");
    keys[n++] = K_RETURN;
    hellos = (unsigned)gw_hellos;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    CHECK(!on_screen("correct"));               /* never shown */
    /* The card: its own settings as they were, our mark, the key
     * scrambled; the passphrase unscrambles it. */
    CHECK(prefs_len == 32 && !memcmp(prefs, card, 15) && prefs[15] == KS_MARK_PASS);
    CHECK(memcmp(prefs + 16, plain, 16) != 0);
    CHECK(ks_load(got) == KS_LOCKED && ks_scramble(got, "correct horse", 13) == 0);
    CHECK(!memcmp(got, plain, 16));
    CHECK(ks_load(got) == KS_LOCKED && ks_scramble(got, "correct horsf", 13) == 0);
    CHECK(memcmp(got, plain, 16) != 0);
    CHECK(lock_armed && link_live && (unsigned)gw_hellos > hellos);
    CHECK(!memcmp(ses_key, plain, 16));

    /* Open-Apple-L: the screen, the key and the messages go; a wrong
     * passphrase is refused on the Apple (no request); the right one
     * signs in again. */
    keys[0] = '>'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(on_screen("echo: while down"));
    watch_for("Twoegram is locked.", "That is not the passphrase.", 0, 0);
    n = 0;
    keys[n] = 'l'; oa[n++] = 1;
    n += type(keys + n, "not my passphrase");
    keys[n++] = K_RETURN;
    n += type(keys + n, "correct horse");
    keys[n++] = K_RETURN;
    hellos = (unsigned)gw_hellos;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(seen[0] && seen[1] && !leaked);
    CHECK((unsigned)gw_hellos == hellos + 1);   /* only the unlocked one */
    CHECK(link_live && on_screen("echo: while down") && !memcmp(ses_key, plain, 16));

    /* Ten minutes without a key lock it too. */
    watch_for("Twoegram is locked.", 0, 0, 0);
    n = 0;
    for (i = 0; i < 89; ++i) keys[n++] = IDLE;
    run(keys, oa, n);
    CHECK(!seen[0] && on_screen("echo: while down"));      /* nine minutes and more: not yet */
    n = 0;
    for (i = 0; i < 91; ++i) keys[n++] = IDLE;
    n += type(keys + n, "correct horse");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    CHECK(seen[0] && !leaked && link_live && on_screen("echo: while down"));
    /* ... and the count starts again: another five minutes, no lock. */
    watch_for("Twoegram is locked.", 0, 0, 0);
    n = 0;
    for (i = 0; i < 45; ++i) keys[n++] = IDLE;
    run(keys, oa, n);
    CHECK(!seen[0] && on_screen("echo: while down"));

    /* A fresh start with the key locked: the passphrase first. A wrong
     * one makes a key the gateway does not know; it says so and asks
     * again. */
    ses_forget();
    lock_armed = 0;
    link_live = 0;
    watch_for("This Apple's key is locked with a passphrase.",
              "The gateway does not know the key that passphrase makes", 0, 0);
    n = 0;
    n += type(keys + n, "wrong guess!");
    keys[n++] = K_RETURN;
    n += type(keys + n, "correct horse");
    keys[n++] = K_RETURN;
    script = keys; script_oa = oa; script_n = n; script_i = 0;
    link_start();
    CHECK(seen[0] && seen[1] && script_i == n);
    CHECK(link_live && lock_armed && !memcmp(ses_key, plain, 16));

    /* Open-Apple-P on set-up takes the passphrase off: the key as it is
     * on the card again. */
    n = 0;
    keys[n] = 'k'; oa[n++] = 1;
    keys[n] = 'p'; oa[n++] = 1;
    keys[n++] = 'y';
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(prefs[15] == KS_MARK && !memcmp(prefs + 16, plain, 16) && !lock_armed);
    CHECK(!memcmp(prefs, card, 15) && link_live);
    on_key = 0;
}

/* ---- keys made on the Apple (docs/RELAY.md, part 1) ---- */

/* What the gateway does with `a2 add`: the key from the screen, as the
 * user would type it. */
static char shown_key[40];
static void gateway_adds(void)
{
    uint8_t buf[27], dig[32];

    CHECK(a2_key_parse(shown_key, gw_key) == 0);
    memcpy(buf, "twoegram-id", 11);
    memcpy(buf + 11, gw_key, 16);
    sha256(buf, 27, dig);
    a2_hex(gw_id, dig, 4);
}

/* Called on every key while watching: a key on the screen is added at
 * the gateway at once, as the user would. */
static void read_shown_key(void)
{
    int r;
    char *p;

    watcher();
    for (r = 0; r < SCR_ROWS; ++r) {
        p = strstr(screen[r], "device id ");
        if (p && p - screen[r] > 36 && strncmp(shown_key, p - 36, 32)) {
            memcpy(shown_key, p - 36, 32);
            shown_key[32] = 0;
            gateway_adds();
        }
    }
}

static void test_master(void)
{
    static uint8_t keys[300], oa[300];
    static const uint8_t own[15] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5 };
    uint8_t want[16], got[16];
    unsigned n, hellos;

    memset(oa, 0, sizeof oa);
    ses_forget();
    ks_forget();
    lock_armed = 0;
    link_live = 0;
    memcpy(prefs, own, 15);
    prefs_len = 15;                             /* nothing of ours on the card */
    cfg_name[0] = 0;

    /* A fresh card: set-up first. Return is refused until there is a key;
     * Open-Apple-M makes one, Open-Apple-V shows the gateway's, and with
     * a key name Return saves and connects. */
    watch_for("Make this Apple's key", "MAKE THIS APPLE'S KEY FIRST", "This Apple's key for the gateway:",
              "GIVE THE GATEWAY A KEY NAME");
    on_key = read_shown_key;
    shown_key[0] = 0;
    n = 0;
    keys[n++] = K_RETURN;                       /* no key yet */
    keys[n] = 'm'; oa[n++] = 1;
    keys[n++] = K_RETURN;                       /* a key, but no key name */
    n += type(keys + n, "Claw1");               /* lowercased as typed */
    keys[n] = 'v'; oa[n++] = 1;                 /* shown, and added at the gateway */
    keys[n++] = ' ';                            /* the key is put away */
    keys[n++] = K_RETURN;
    hellos = (unsigned)gw_hellos;
    script = keys; script_oa = oa; script_n = n; script_i = 0;
    link_start();
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    on_key = watcher;
    CHECK(prefs_len == 32 && !memcmp(prefs, own, 15) && prefs[15] == KS_MASTER);
    CHECK(ks_master && ks_have && !memcmp(prefs + 16, ks_secret, 16));
    CHECK(strlen(shown_key) == 32 && !on_screen(shown_key));    /* gone once a key was pressed */
    CHECK(ks_derive(ks_secret, "claw1", want) == 0);
    CHECK(a2_key_parse(shown_key, got) == 0 && !memcmp(got, want, 16));
    memset(oa, 0, sizeof oa);
    CHECK(script_i == n && link_live && (unsigned)gw_hellos > hellos && !memcmp(ses_key, want, 16));
    CHECK(!strcmp(cfg_name, "claw1"));
    CHECK(!strcmp(saved_cfg, "# Twoegram: the gateway\rGATEWAY=claw1 http://10.0.0.2:8417\r"));

    /* Another gateway: its name and address, no key typed. This gateway
     * does not know the new key, so set-up says how to add it. */
    watch_for("The gateway does not know this Apple's key", 0, 0, 0);
    n = 0;
    keys[n] = 'k'; oa[n++] = 1;
    keys[n++] = K_DELETE; keys[n++] = K_DELETE; keys[n++] = K_DELETE;
    keys[n++] = K_DELETE; keys[n++] = K_DELETE;
    n += type(keys + n, "mini");
    keys[n++] = K_RETURN;
    keys[n++] = K_ESC;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(seen[0] && !strcmp(cfg_name, "mini"));
    CHECK(ks_derive(ks_secret, "mini", want) == 0 && !memcmp(ses_key, want, 16));
    /* Back to claw1: its key again, with nothing more on the gateway. */
    n = 0;
    keys[n] = 'k'; oa[n++] = 1;
    keys[n++] = K_DELETE; keys[n++] = K_DELETE; keys[n++] = K_DELETE; keys[n++] = K_DELETE;
    n += type(keys + n, "claw1");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(link_live && ks_derive(ks_secret, "claw1", want) == 0 && !memcmp(ses_key, want, 16));

    /* A passphrase locks the master ($A5); the lock and a fresh start
     * unlock it, and the key made is claw1's again. */
    n = 0;
    keys[n] = 'k'; oa[n++] = 1;
    keys[n++] = K_TAB; keys[n++] = K_TAB;
    n += type(keys + n, "a long passphrase");
    keys[n++] = K_RETURN;
    n += type(keys + n, "a long passphrase");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(prefs[15] == KS_MASTER_PASS && memcmp(prefs + 16, ks_secret, 16) != 0 && lock_armed);
    CHECK(link_live && !memcmp(ses_key, want, 16));
    watch_for("Twoegram is locked.", "That is not the passphrase.", 0, 0);
    n = 0;
    keys[n] = 'l'; oa[n++] = 1;
    n += type(keys + n, "a wrong passphrase");
    keys[n++] = K_RETURN;
    n += type(keys + n, "a long passphrase");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(seen[0] && seen[1] && !leaked && link_live && !memcmp(ses_key, want, 16));
    ses_forget();
    ks_forget();
    lock_armed = 0;
    link_live = 0;
    n = 0;
    n += type(keys + n, "a long passphrase");
    keys[n++] = K_RETURN;
    script = keys; script_oa = oa; script_n = n; script_i = 0;
    link_start();
    CHECK(link_live && lock_armed && ks_master && !memcmp(ses_key, want, 16));

    /* Esc on set-up changes nothing; and a second Open-Apple-M asks first. */
    watch_for("EVERY GATEWAY MUST HAVE THIS APPLE ADDED AGAIN", 0, 0, 0);
    memcpy(got, prefs + 16, 16);
    n = 0;
    keys[n] = 'k'; oa[n++] = 1;
    keys[n] = 'm'; oa[n++] = 1;
    keys[n++] = 'n';
    keys[n++] = K_ESC;
    run(keys, oa, n);
    memset(oa, 0, sizeof oa);
    CHECK(seen[0] && !memcmp(prefs + 16, got, 16) && prefs[15] == KS_MASTER_PASS && link_live);
    on_key = 0;
    cfg_name[0] = 0;
    gateway_reset();
}

/* ---- saving the conversation ---- */

static char saved_text[400000];
static size_t saved_len;

/* The file, read whole; 0 if it is not there. */
static int read_saved(const char *name)
{
    FILE *f = fopen(name, "rb");

    saved_len = 0;
    if (!f) return 0;
    saved_len = fread(saved_text, 1, sizeof saved_text - 1, f);
    saved_text[saved_len] = 0;
    fclose(f);
    return 1;
}

/* Messages in the file: a line "10:mm You" or "10:mm <label>" each. */
static unsigned saved_msgs(const char *label)
{
    unsigned n = 0;
    size_t i, w = strlen(label);

    for (i = 0; i + 6 < saved_len; ++i) {
        if ((i == 0 || saved_text[i - 1] == '\r') && saved_text[i + 2] == ':' &&
            saved_text[i + 5] == ' ' &&
            (!strncmp(saved_text + i + 6, "You\r", 4) ||
             (!strncmp(saved_text + i + 6, label, w) && saved_text[i + 6 + w] == '\r'))) {
            ++n;
        }
    }
    return n;
}

/* A down arrow in the 80th column: more below what is shown. */
static int down_arrow(void)
{
    int r;
    for (r = TOP; r <= BOTTOM; ++r) if (screen[r][SCR_COLS - 1] == 'v') return 1;
    return 0;
}

static unsigned save_fail_at;       /* the key at which the gateway goes */

static void fail_gateway(void)
{
    if (save_fail_at && script_i == save_fail_at) http_error = 3;
}

static void test_save(void)
{
    static uint8_t keys[100], oa[100];
    char here[1024], dir[] = "/tmp/twoegram-save-XXXXXX", want[80], label[20], name[20];
    unsigned n, i, total;

    memset(oa, 0, sizeof oa);
    CHECK(getcwd(here, sizeof here) && mkdtemp(dir) && chdir(dir) == 0);
    strcpy(label, agent_label(0));
    save_default(label, name);
    total = conv_total;

    /* The name it offers, from the agent's: letters and digits, a letter
     * first, at most 11, ".TXT". */
    save_default("Pinchy", want);
    CHECK(!strcmp(want, "PINCHY.TXT"));
    save_default("2nd agent-of-mine!", want);
    CHECK(!strcmp(want, "NDAGENTOFMI.TXT"));
    save_default("", want);
    CHECK(!strcmp(want, "TWOEGRAM.TXT"));

    /* Esc at the name: nothing written. */
    keys[0] = 's'; oa[0] = 1;
    keys[1] = K_ESC;
    run(keys, oa, 2);
    CHECK(on_screen("NOT SAVED") && !read_saved(name));

    /* Open-Apple-S, Return: the whole conversation, from its first
     * message, in pages, to the name offered. */
    keys[0] = 's'; oa[0] = 1;
    keys[1] = K_RETURN;
    run(keys, oa, 2);
    oa[0] = 0;
    sprintf(want, "SAVED %u MESSAGES AS %s", total, name);
    CHECK(on_screen(want));
    CHECK(read_saved(name));
    CHECK(!strncmp(saved_text, "Twoegram: the conversation with ", 32));
    CHECK(saved_msgs(label) == total);
    CHECK(strstr(saved_text, "\r\r10:01 You\rm1 ") != NULL);
    CHECK(strstr(saved_text, "m100 ") != NULL && strstr(saved_text, "m7 ") != NULL);
    CHECK(!memchr(saved_text, '\n', saved_len));        /* CR line ends only */
    /* Message 7, at 1,500 bytes, is there whole: its text with the line
     * breaks (every 61st) made CRs. */
    {
        char m7[1600];
        conv_text(7, m7);
        for (i = 0; i < 1500; ++i) if (m7[i] == '\n') m7[i] = '\r';
        CHECK(memmem(saved_text, saved_len, m7, 1500) != NULL);
    }
    /* After it, the newest on the screen, ready to type. */
    CHECK(st_newest && st[st_count - 1].n == total && !down_arrow() && link_live);

    /* Again: it asks before replacing. N keeps the file; Y replaces it. */
    {
        FILE *f = fopen(name, "wb");
        fputs("keep me", f);
        fclose(f);
    }
    keys[0] = 's'; oa[0] = 1;
    keys[1] = K_RETURN;
    keys[2] = 'n';
    run(keys, oa, 3);
    CHECK(on_screen("NOT SAVED") && read_saved(name) && !strcmp(saved_text, "keep me"));
    keys[2] = 'y';
    run(keys, oa, 3);
    CHECK(on_screen(want) && read_saved(name) && saved_msgs(label) == total);

    /* A name typed over the one offered; lowercase becomes uppercase. */
    n = 0;
    keys[n] = 's'; oa[n++] = 1;
    for (i = 0; i < strlen(name); ++i) keys[n++] = K_DELETE;
    n += type(keys + n, "chat.txt");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    oa[0] = 0;
    CHECK(read_saved("CHAT.TXT") && saved_msgs(label) == total);
    CHECK(on_screen("AS CHAT.TXT"));

    /* Esc while it works: the file keeps what had come. */
    n = 0;
    keys[n] = 's'; oa[n++] = 1;
    for (i = 0; i < strlen(name); ++i) keys[n++] = K_DELETE;
    n += type(keys + n, "PART.TXT");
    keys[n++] = K_RETURN;
    keys[n++] = K_ESC;
    run(keys, oa, n);
    oa[0] = 0;
    CHECK(on_screen("SAVING STOPPED: THE FILE HAS THE FIRST "));
    CHECK(read_saved("PART.TXT") && saved_msgs(label) > 0 && saved_msgs(label) < total);
    CHECK(st_newest && st[st_count - 1].n == total);  /* the newest again */

    /* The gateway goes partway: said, with what the file has. */
    n = 0;
    keys[n] = 's'; oa[n++] = 1;
    for (i = 0; i < strlen(name); ++i) keys[n++] = K_DELETE;
    n += type(keys + n, "GONE.TXT");
    keys[n++] = K_RETURN;
    save_fail_at = n + 1;               /* as it reads the next key */
    keys[n++] = 'x';
    on_key = fail_gateway;
    run(keys, oa, n);
    on_key = 0;
    save_fail_at = 0;
    oa[0] = 0;
    CHECK(on_screen("THE GATEWAY STOPPED ANSWERING: THE FILE HAS THE FIRST "));
    CHECK(read_saved("GONE.TXT") && saved_msgs(label) > 0 && saved_msgs(label) < total);
    http_error = 0;
    keys[0] = IDLE;
    for (i = 0; i < 20 && !link_ok(); ++i) run(keys, oa, 1);

    /* The gateway gone before the first page: no file, said; once it is
     * back, the conversation is there again. */
    n = 0;
    keys[n] = 's'; oa[n++] = 1;
    for (i = 0; i < strlen(name); ++i) keys[n++] = K_DELETE;
    n += type(keys + n, "FIRST.TXT");
    save_fail_at = n;                   /* as it reads the Return */
    keys[n++] = K_RETURN;
    on_key = fail_gateway;
    run(keys, oa, n);
    on_key = 0;
    save_fail_at = 0;
    oa[0] = 0;
    CHECK(on_screen("NOT SAVED: THE GATEWAY DID NOT GIVE THE CONVERSATION"));
    CHECK(!read_saved("FIRST.TXT"));
    http_error = 0;
    keys[0] = IDLE;
    for (i = 0; i < 20 && !link_ok(); ++i) run(keys, oa, 1);
    keys[0] = '>'; oa[0] = 1;
    run(keys, oa, 1);
    oa[0] = 0;
    CHECK(link_live && st_newest && st_count && st[st_count - 1].n == total);

    /* A disk that will not take it: said. */
    n = 0;
    keys[n] = 's'; oa[n++] = 1;
    for (i = 0; i < strlen(name); ++i) keys[n++] = K_DELETE;
    n += type(keys + n, "nowhere/x.txt");
    keys[n++] = K_RETURN;
    run(keys, oa, n);
    oa[0] = 0;
    CHECK(on_screen("COULD NOT SAVE, PRODOS ERROR $FF"));

    unlink(name);
    unlink("CHAT.TXT");
    unlink("PART.TXT");
    unlink("GONE.TXT");
    CHECK(chdir(here) == 0);
    CHECK(rmdir(dir) == 0);             /* nothing else was written */
}

/* The glyph bytes: MouseText, or plain stand-ins without it. */
static void test_glyphs(void)
{
    has_mousetext = 1;
    CHECK(sc_char(GL_CHECK, 0) == MT_CHECK && sc_char(GL_DOWN, 1) == MT_DOWN && sc_char(GL_UP, 0) == MT_UP);
    CHECK(sc_char(GL_DIAMOND, 0) == MT_DIAMOND && sc_char(OA[0], 0) == MT_OAPPLE);
    has_mousetext = 0;
    CHECK(sc_char(GL_CHECK, 0) == ('+' | 0x80) && sc_char(GL_DOWN, 0) == ('v' | 0x80) && sc_char(GL_UP, 0) == ('^' | 0x80));
    CHECK(sc_char(GL_DIAMOND, 1) == '*' && sc_char(OA[0], 0) == MT_OAPPLE);
    CHECK(sc_char(0x06, 0) == ('?' | 0x80));
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
    test_passphrase();
    test_master();
    test_save();
    printf("convo: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
