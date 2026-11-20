/*
 * The manual's pictures of the conversation screen. Virtual ][ has no
 * CoGS card, so it cannot show one; this runs the real src/convo.c on the
 * Mac against test_a2.c's model card and gateway, serving a demo
 * conversation, and saves each scene's 24 x 80 screen codes to
 * build/shots/<name>.scr. tools/render.py draws them with the glyphs
 * captured from Virtual ][ (tools/fontgrab.py), so they match the
 * emulator's own screenshots pixel for pixel.
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
#include "config.h"

static uint8_t codes[SCR_ROWS][SCR_COLS];

void scr_init(void) { memset(codes, SC_SPACE, sizeof codes); }
void scr_done(void) {}
void scr_blit(uint8_t row) { memcpy(codes[row], linebuf, SCR_COLS); }

uint8_t key_oa, key_sa;
static const uint8_t *script;
static const uint8_t *script_oa;
static unsigned script_n, script_i;

/* A scene in the middle of a screen: saved when its key is asked for. */
static const char *snap_name;
static unsigned snap_at;
static void save(const char *name);

uint8_t key_get(void)
{
    if (snap_name && script_i == snap_at) {
        save(snap_name);
        snap_name = 0;
    }
    if (script_i >= script_n) {
        key_oa = 1;
        return 'q';
    }
    key_oa = script_oa ? script_oa[script_i] : 0;
    return script[script_i++];
}
#define IDLE        0xFF
static unsigned idle_left;

uint8_t key_ready(void)
{
    if (idle_left) {
        --idle_left;
        return 0;
    }
    if (script_i < script_n && script[script_i] == IDLE) {
        ++script_i;
        idle_left = 400;
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

void net_check(void) {}
uint8_t cfg_load(void) { return 1; }

/* TWOEGRAM.CFG, in memory. */
static char cfg_file[600];
uint16_t cfg_read(char *buf, uint16_t cap)
{
    size_t n = strlen(cfg_file);

    if (n > (size_t)cap - 1) n = (size_t)cap - 1;
    memcpy(buf, cfg_file, n);
    buf[n] = 0;
    return (uint16_t)n;
}
uint8_t cfg_write(const char *text, uint16_t n)
{
    memcpy(cfg_file, text, n);
    cfg_file[n] = 0;
    return 0;
}

static uint8_t kb[400], oa[400];

static void run(unsigned n)
{
    script = kb;
    script_oa = oa;
    script_n = n;
    script_i = 0;
    convo_run();
    memset(oa, 0, sizeof oa);
}

static unsigned type(unsigned n, const char *text)
{
    while (*text) kb[n++] = (uint8_t)*text++;
    return n;
}

static void save(const char *name)
{
    char path[80];
    FILE *f;

    sprintf(path, "build/shots/%s.scr", name);
    f = fopen(path, "wb");
    if (!f || fwrite(codes, 1, sizeof codes, f) != sizeof codes) {
        printf("shots: cannot write %s\n", path);
        exit(1);
    }
    fclose(f);
    printf("  %s\n", path);
}

static const demo_msg_t demo[] = {
    { "08:58", ">Good morning! What's on my calendar today?" },
    { "08:58", "Good morning. Three things today:\n\n"
               "- 10:00  Dentist\n"
               "- 13:30  Call the print shop about the newsletter\n"
               "- 18:00  Apple II user group, at the library\n\n"
               "Tonight is the swap meet, so you may want to bring the spare "
               "Disk II drive." },
    { "09:01", ">Remind me to pack the spare Disk II at 5:30." },
    { "09:01", "Done \x02 I'll remind you at 17:30." },
    { "09:14", ">How do I clear the screen from 6502 assembly?" },
    { "09:14", "Call the Monitor's HOME routine:\n\n"
               "        JSR $FC58     ; HOME\n\n"
               "With the 80-column firmware active, you can also print a "
               "Ctrl-L through COUT:\n\n"
               "        LDA #$8C      ; Ctrl-L, high bit set\n"
               "        JSR $FDED     ; COUT" },
    { "09:16", ">Thanks! Anything new on the user group's mailing list?" },
    { "09:16", "Two new posts since yesterday:\n\n"
               "1. Someone is selling a boxed AppleWorks 2.0, manuals included.\n"
               "2. Next month's meeting moves to the second Saturday.\n\n"
               "Would you like me to answer either one?" },
};

int main(void)
{
    unsigned n;

    strcpy(status_net, "COGS 192.168.1.42");   /* status.c's, set as net.c would */
    conv_demo = demo;
    conv_base = sizeof demo / sizeof demo[0];
    gw_agents = "agent main\nagent coder\nagent research\nname main Juniper\n";
    model_answer = "It starts at 18:00 in the library's community room. "
                   "I'll remind you at 17:30, as you asked.";
    cogs_slot = 4;
    gateway_reset();
    if (ses_hello() || st_init()) {
        printf("shots: the model gateway did not start\n");
        return 1;
    }
    strcpy(ses_id, "4f1c09a2");
    msgs_mode(MSGS_LIVE);
    link_live = 1;
    strcpy(link_sec, SEC_SIGNED);
    has_mousetext = 1;
    scr_init();

    /* The conversation, newest at the foot. */
    run(0);
    save("convo");

    /* Typing a message. */
    n = type(0, "Is the user group meeting still at six?");
    run(n);
    save("compose");

    /* Sent: the agent is thinking. */
    kb[0] = K_RETURN;
    run(1);
    save("thinking");

    /* The answer arrives. */
    kb[0] = IDLE; kb[1] = IDLE;
    run(2);
    save("answer");

    /* Scrolled back, and something new arrives below. */
    kb[0] = '<'; oa[0] = 1;
    run(1);
    save("scrolled");
    conv_append(0, "Reminder: pack the spare Disk II drive for tonight.", 51);
    kb[0] = IDLE;
    run(1);
    save("new-below");

    /* Back at the newest; another agent has news. */
    kb[0] = '>'; oa[0] = 1;
    run(1);
    gw_unread_coder = 2;
    kb[0] = IDLE;
    run(1);
    save("unread");

    /* The agent list has the focus. */
    kb[0] = K_TAB; kb[1] = K_DOWN;
    run(2);
    save("list");
    kb[0] = K_ESC;
    run(1);

    /* The list hidden: the conversation takes all 80 columns. */
    kb[0] = 'b'; oa[0] = 1;
    run(1);
    save("hidden");
    kb[0] = 'b'; oa[0] = 1;
    run(1);

    /* Settings, with three gateways saved. A made-up master on the
     * card, and the gateway knows claw1's key from it. */
    {
        static const uint8_t m[16] = { 0x5A, 0x11, 0xC3, 0x07, 0x9E, 0x42, 0x6B, 0xD0,
                                       0x28, 0x83, 0xF1, 0x3C, 0x74, 0xA9, 0x0E, 0x65 };
        uint8_t k[16], buf[27], dig[32];

        memset(prefs, 0, 15);
        prefs[15] = KS_MASTER;
        memcpy(prefs + 16, m, 16);
        prefs_len = 32;
        memcpy(ks_secret, m, 16);
        ks_have = 1;
        ks_derive(m, "claw1", k);
        memcpy(gw_key, k, 16);
        memcpy(buf, "twoegram-id", 11);
        memcpy(buf + 11, k, 16);
        sha256(buf, 27, dig);
        a2_hex(gw_id, dig, 4);
        memcpy(ses_key, k, 16);
        ses_hello();
    }
    strcpy(cfg_name, "claw1");
    strcpy(ses_base, "http://192.168.1.20:8417");
    strcpy(cfg_file, "GATEWAY=claw1 http://192.168.1.20:8417\r"
                     "GATEWAY=mini http://192.168.1.31:8417\r"
                     "GATEWAY=relay http://192.168.1.40:8417\r");
    kb[0] = 'k'; oa[0] = 1;
    kb[1] = K_ESC;
    snap_name = "settings"; snap_at = 1;
    run(2);

    /* Adding a gateway: Add, after the three. */
    kb[0] = 'k'; oa[0] = 1;
    kb[1] = K_DOWN; kb[2] = K_DOWN; kb[3] = K_DOWN;
    kb[4] = K_RETURN;
    n = type(5, "studio");
    kb[n++] = K_TAB;
    n = type(n, "192.168.1.50");
    snap_name = "add-gateway"; snap_at = n;
    kb[n++] = K_ESC;
    kb[n++] = K_ESC;
    run(n);

    /* Its key: K on claw1 in Settings. */
    kb[0] = 'k'; oa[0] = 1;
    kb[1] = 'k';
    kb[2] = ' ';
    snap_name = "add-key"; snap_at = 2;
    run(3);
    return 0;
}
