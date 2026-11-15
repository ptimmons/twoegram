/*
 * The set-up screen: the gateway's address and the device key.
 *
 * Shown at the first start, when the gateway does not know the key, and
 * on Open-Apple-K. The address goes to TWOEGRAM.CFG, the key to the
 * card's prefs blob (keystore.h); the key is never shown back, and the
 * typed copy is wiped once parsed.
 */
#include <string.h>
#include "screen.h"
#include "kbd.h"
#include "fmt.h"
#include "proto.h"
#include "session.h"
#include "keystore.h"
#include "config.h"
#include "ui.h"
#include "build.h"

#define LABEL_COL   2
#define FIELD_COL   12
#define FIELD_W     (SCR_COLS - 2 - FIELD_COL)
#define GW_ROW      3
#define KEY_ROW     5
#define NOTE_ROW    6
#define WHY_ROW     20
#define GW_MAX      48
#define KEY_MAX     27

static char    gw[GW_MAX + 1];
static uint8_t gwn;
static char    kraw[KEY_MAX + 1];  /* as typed, without dashes */
static uint8_t kn;
static uint8_t field;              /* 0 gateway, 1 key */

static const char *const help_text[] = {
    "The gateway is the machine running OpenClaw with the a2 channel,",
    "as an address and port, for example 192.168.1.20:8417.",
    "",
    "The key comes from pairing this Apple on the gateway:",
    "      openclaw a2 pair",
    "(until the channel is in OpenClaw: npm run pair, in its plugin folder).",
    "Type it as shown; dashes are optional. A slip is caught before",
    "anything is sent.",
    "",
    "The key is kept in the CoGS card's own memory, never on a disk.",
    OA "-X removes it from the card.",
};

#define NHELP   (sizeof help_text / sizeof help_text[0])

static void draw_field(uint8_t row, const char *label, uint8_t focused,
                       const char *text, uint8_t n)
{
    uint8_t x;

    lb_clear(SC_SPACE);
    lb_text(LABEL_COL, label, focused);
    lb_fill(FIELD_COL, FIELD_W, SC_UNDER);
    x = lb_textn(FIELD_COL, text, n, 0);
    if (focused && x < SCR_COLS - 1) {
        linebuf[x] = SC_ISPACE;                 /* the cursor */
    }
    scr_blit(row);
}

static void draw_key(void)
{
    static char shown[KEY_MAX + 6];
    uint8_t i, j = 0;

    for (i = 0; i < kn; ++i) {
        if (i && i % 5 == 0) {
            shown[j++] = '-';
        }
        shown[j++] = kraw[i];
    }
    draw_field(KEY_ROW, "Key", field == 1, shown, j);
}

static void draw(const char *why)
{
    uint8_t i;

    scr_clear(0, BODY_ROWS - 1);
    lb_clear(SC_SPACE);
    lb_text(1, "Twoegram set-up", 0);
    lb_text((uint8_t)(SCR_COLS - 1 - txt_len("build " BUILD_ID)), "build " BUILD_ID, 0);
    scr_blit(0);
    lb_clear(has_mousetext ? MT_RULE : SC_DASH);
    scr_blit(1);

    draw_field(GW_ROW, "Gateway", field == 0, gw, gwn);
    draw_key();
    lb_clear(SC_SPACE);
    lb_text(FIELD_COL, "Leave the key empty to keep the one on the card, if there is one.", 0);
    scr_blit(NOTE_ROW);
    for (i = 0; i < NHELP; ++i) {
        lb_clear(SC_SPACE);
        lb_text(LABEL_COL, help_text[i], 0);
        scr_blit((uint8_t)(8 + i));
    }
    if (why) {
        lb_clear(SC_SPACE);
        lb_text(LABEL_COL, why, 1);
        scr_blit(WHY_ROW);
    }
    status_msg("TAB: NEXT FIELD   RETURN: SAVE AND CONNECT   ESC: CANCEL");
}

/* What a typed character is in a key, or 0 if it cannot be in one. */
static char key_char(uint8_t k)
{
    if (k >= 'a' && k <= 'z') {
        k = (uint8_t)(k - 32);
    }
    if ((k >= '0' && k <= '9') || (k >= 'A' && k <= 'Z') ||
        k == '*' || k == '~' || k == '$' || k == '=') {
        return (char)k;
    }
    return 0;
}

static uint8_t warned;

static void warn(uint8_t row, const char *s)
{
    lb_clear(SC_SPACE);
    lb_text(LABEL_COL, s, 1);
    scr_blit(row);
    warned = 1;
}

/* Saving. Returns 1 to leave the screen. A problem that does not stop
 * the session (a write-protected disk, a key that cannot be kept on the
 * card) is said and waited for. */
static uint8_t save(void)
{
    static uint8_t key[A2_KEY_LEN];
    static char note[64];
    uint8_t rc, have = 0;

    gw[gwn] = 0;
    kraw[kn] = 0;
    if (!cfg_set_gateway(gw)) {
        status_msg("THE GATEWAY IS AN ADDRESS AND PORT, FOR EXAMPLE 192.168.1.20:8417");
        return 0;
    }
    if (kn) {
        rc = a2_key_parse(kraw, key);
        if (rc == A2_KEY_BADCHAR) {
            status_msg(kn != KEY_MAX ? "A KEY IS 27 CHARACTERS: 26, THEN ONE TO CHECK THEM"
                                     : "THAT IS NOT A KEY: CHECK THE LAST CHARACTER");
            return 0;
        }
        if (rc == A2_KEY_BADCHECK) {
            status_msg("THE KEY DOES NOT CHECK: A TYPING SLIP? COMPARE IT AGAIN");
            return 0;
        }
        have = 1;
    } else if (ks_load(key)) {
        status_msg("TYPE THE KEY: THE CARD DOES NOT HOLD ONE");
        return 0;
    }
    memset(kraw, 0, sizeof kraw);
    kn = 0;
    memcpy(ses_key, key, A2_KEY_LEN);

    /* The file first, the card second: no network in between. */
    warned = 0;
    status_msg("SAVING");
    rc = cfg_save();
    if (rc == 0x2B) {
        warn(WHY_ROW - 1, "The disk is write-protected: the address is not saved.");
    } else if (rc) {
        fmt_hex8(fmt_str(note, "The address could not be saved: ProDOS error $"), rc);
        warn(WHY_ROW - 1, note);
    }
    if (have) {
        rc = ks_save(key);
        if (rc == KS_NOROOM) {
            warn(WHY_ROW, "The card cannot keep the key yet; it will be asked for again.");
        } else if (rc == KS_INUSE) {
            warn(WHY_ROW, "The card's spare memory is in use: the key is not kept there.");
        } else if (rc) {
            warn(WHY_ROW, "The key could not be kept on the card.");
        }
    }
    memset(key, 0, sizeof key);
    if (warned) {
        status_msg("PRESS A KEY TO CONNECT ANYWAY");
        key_get();
    }
    return 1;
}

static void forget(void)
{
    uint8_t k;

    status_msg("REMOVE THE KEY FROM THE CARD?  Y OR N");
    k = key_get();
    if (k != 'y' && k != 'Y') {
        status_msg("TAB: NEXT FIELD   RETURN: SAVE AND CONNECT   ESC: CANCEL");
        return;
    }
    status_msg(ks_clear() ? "COULD NOT REMOVE THE KEY" : "THE KEY IS GONE FROM THE CARD");
}

uint8_t setup_run(const char *why)
{
    uint8_t k;
    const char *g = ses_base;

    if (!strncmp(g, "http://", 7)) {
        g += 7;
    }
    gwn = (uint8_t)strlen(g);
    if (gwn > GW_MAX) {
        gwn = GW_MAX;
    }
    memcpy(gw, g, gwn);
    kn = 0;
    field = gwn ? 1 : 0;
    draw(why);

    for (;;) {
        k = key_get();
        if (key_oa) {
            if (k == 'x' || k == 'X') {
                forget();
            }
            continue;
        }
        switch (k) {
        case K_ESC:
            memset(kraw, 0, sizeof kraw);
            kn = 0;
            return 0;
        case K_TAB: case K_UP: case K_DOWN:
            field ^= 1;
            draw_field(GW_ROW, "Gateway", field == 0, gw, gwn);
            draw_key();
            break;
        case K_RETURN:
            if (save()) {
                return 1;
            }
            break;
        case K_DELETE: case K_LEFT:
            if (field == 0 && gwn) {
                --gwn;
                draw_field(GW_ROW, "Gateway", 1, gw, gwn);
            } else if (field == 1 && kn) {
                kraw[--kn] = 0;
                draw_key();
            }
            break;
        default:
            if (field == 0) {
                if (k > ' ' && k < 0x7F && gwn < GW_MAX) {
                    gw[gwn++] = (char)k;
                    draw_field(GW_ROW, "Gateway", 1, gw, gwn);
                }
            } else if (key_char(k) && kn < KEY_MAX) {
                kraw[kn++] = key_char(k);
                draw_key();
            }
            break;
        }
    }
}
