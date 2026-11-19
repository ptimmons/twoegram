/*
 * Adding a gateway: its key name and address, then this Apple's key for
 * it, to type there with `a2 add`.
 *
 * Shown at the first start, from Settings (prefs.c), and whenever the
 * gateway in use does not know the key: then it opens at the key, with
 * the name and address already in. The key is made from the master on
 * the card and the key name (docs/RELAY.md, part 1); with no master on
 * the card yet, one is made here, without asking. The key is shown
 * nowhere else, and is wiped from the screen and memory at the next key
 * pressed. The gateway goes first in TWOEGRAM.CFG's list (config.h),
 * as the one in use.
 */
#include <string.h>
#include "cogs.h"
#include "screen.h"
#include "kbd.h"
#include "fmt.h"
#include "hmac.h"
#include "tick.h"
#include "proto.h"
#include "session.h"
#include "keystore.h"
#include "lock.h"
#include "config.h"
#include "ui.h"
#include "build.h"
#include "overlay.h"

/* An overlay (overlay.h): its code, text and variables (segment
 * OVL1DATA, cfg/twoegram.cfg says why) are loaded from TWOEGRAM.SETUP
 * while it is open, and gone when it closes. What must outlast it is in
 * main memory: ses_base, cfg_name, ks_secret. */
#ifdef __CC65__
#pragma code-name ("OVERLAY1")
#pragma rodata-name ("OVERLAY1")
#pragma data-name ("OVL1DATA")
#pragma bss-name ("OVL1DATA")
#endif

#include "setup_list.h"

#define FIELD_COL   KEY_COL
#define FIELD_W     (SCR_COLS - 2 - FIELD_COL)
#define NAME_ROW    3
#define NNOTE_ROW   4
#define ADDR_ROW    6
#define ANOTE_ROW   7
#define HELP_ROW    9
#define KEY_ROW     13              /* the key, while shown */
#define GW_MAX      48
#define HINT        "TAB: NEXT FIELD   RETURN: NEXT, THE KEY   ESC: CANCEL"
#define KEY_HINT    "RETURN: CONNECT, ONCE IT IS ADDED ON THE GATEWAY   ESC: BACK"

static char    nm[CFG_NAME_MAX + 1];
static uint8_t nmn;
static char    gw[GW_MAX + 1];
static uint8_t gwn;
static uint8_t field;              /* 0 key name, 1 address */

static const char *const labels[2] = { "Key name", "Address" };
static const uint8_t rows[2] = { NAME_ROW, ADDR_ROW };
static const char *const help_text[] = {
    "This Apple has one key on the CoGS card, which never leaves it. From it",
    "comes a different key for each gateway, by the key name: the next step",
    "shows it, to type on the gateway. Settings (" OA "-K) keeps up to four.",
};
#define NHELP   (sizeof help_text / sizeof help_text[0])

/* ---- drawing ---- */

static void draw_field(uint8_t f)
{
    const char *text = f ? gw : nm;
    uint8_t n = f ? gwn : nmn, x;

    lb_clear(SC_SPACE);
    lb_text(LABEL_COL, labels[f], field == f);
    lb_fill(FIELD_COL, FIELD_W, SC_UNDER);
    x = lb_textn(FIELD_COL, text, n, 0);
    if (field == f && x < SCR_COLS - 1) {
        linebuf[x] = SC_ISPACE;                 /* the cursor */
    }
    scr_blit(rows[f]);
}

static void note(uint8_t row, const char *s)
{
    lb_clear(SC_SPACE);
    lb_text(FIELD_COL, s, 0);
    scr_blit(row);
}

static void draw(const char *why)
{
    uint8_t i;

    scr_clear(0, BODY_ROWS - 1);
    lb_clear(SC_SPACE);
    lb_text(1, "Twoegram: add a gateway", 0);
    lb_text((uint8_t)(SCR_COLS - 1 - txt_len("build " BUILD_ID)), "build " BUILD_ID, 0);
    scr_blit(0);
    lb_clear(has_mousetext ? MT_RULE : SC_DASH);
    scr_blit(1);
    draw_field(0);
    draw_field(1);
    note(NNOTE_ROW, "1 to 16 of a-z 0-9 - _, such as claw1. Each name has its own key.");
    note(ANOTE_ROW, "Its address, and its port if not 8417: 192.168.1.20:8417.");
    for (i = 0; i < NHELP; ++i) {
        say((uint8_t)(HELP_ROW + i), help_text[i], 0);
    }
    if (why) {
        say(WHY_ROW, why, 1);
    }
    status_msg(HINT);
}

/* ---- the key ---- */

static uint8_t k[34];               /* a derived key while shown; the new master's makings */
static uint8_t dig[32];
static char    head[48];
static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ*~$=U";

/* A new master, from two draws of the card's random numbers and the
 * tick count, hashed together by the card, and kept on the card
 * without a passphrase. 0, or the card's trouble (said). */
static uint8_t make_master(void)
{
    cogs_part_t part;
    uint8_t rc;

    rc = cogs_random(k, 16);
    if (!rc) {
        rc = cogs_random(k + 16, 16);
    }
    k[32] = (uint8_t)ticks;
    k[33] = (uint8_t)(ticks >> 8);
    COGS_PART(part, k, sizeof k);
    if (!rc) {
        rc = sha256_parts(&part, 1, (uint8_t *)line);
    }
    memcpy(ks_secret, line, A2_KEY_LEN);
    memset(k, 0, sizeof k);
    memset(line, 0, sizeof line);
    if (rc) {
        ks_forget();
        status_msg(cogs_strerror(rc));
        return rc;
    }
    ks_have = 1;
    lock_armed = 0;
    card = 0;
    card_trouble(ks_save(ks_secret, KS_MASTER));
    return 0;
}

/* k in the written form (PROTOCOL.md section 1), then "device id ",
 * into line; the id itself goes at line + 46. */
static void write_key(void)
{
    uint8_t i, j, v, pos = 0;
    uint16_t sum = 0;
    char *p = line;

    for (i = 0; i < 26; ++i) {
        v = 0;
        for (j = 0; j < 5; ++j, ++pos) {
            v = (uint8_t)(v << 1);
            if (pos < 128 && (k[pos >> 3] & (0x80 >> (pos & 7)))) {
                v |= 1;
            }
        }
        sum = (uint16_t)(sum + (uint16_t)(i + 1) * v);
        if (i && i % 5 == 0) {
            *p++ = '-';
        }
        *p++ = alphabet[v];
    }
    *p++ = alphabet[sum % 37];
    fmt_str(p, "    device id ");
}

/* This Apple's key for the gateway named name, with its device id, on
 * rows row to row + 3, until a key is pressed; then wiped from the
 * screen and memory. The key pressed, or 0 if the key could not be made
 * (said). The master must be in memory. */
static uint8_t show_key(const char *name, uint8_t row, const char *hint)
{
    cogs_part_t parts[2];
    uint8_t rc, c = 0;

    rc = ks_derive(ks_secret, name, k);
    COGS_PART(parts[0], "twoegram-id", 11);
    COGS_PART(parts[1], k, A2_KEY_LEN);
    if (!rc) {
        rc = sha256_parts(parts, 2, dig);
    }
    if (rc) {
        status_msg(cogs_strerror(rc));
    } else {
        write_key();
        a2_hex(line + 46, dig, 4);
        fmt_str(fmt_str(fmt_str(head, "This Apple's key for "), name), ":");
        say(row, head, 0);
        lb_clear(SC_SPACE);
        lb_text(KEY_COL, line, 1);
        scr_blit((uint8_t)(row + 1));
        say((uint8_t)(row + 2), "Type it on the gateway after openclaw a2 add \"Apple IIe\" (or hermes a2 add).", 0);
        say((uint8_t)(row + 3), "The gateway then shows the device id, which should be the same.", 0);
        status_msg(hint);
        c = key_get();
        scr_clear(row, (uint8_t)(row + 3));
    }
    memset(k, 0, sizeof k);
    memset(line, 0, sizeof line);
    memset(dig, 0, sizeof dig);
    return c;
}

/* ---- the steps ---- */

/* Return on the fields: the gateway first in the list, and in use. 1 to
 * go on to the key. */
static uint8_t next(void)
{
    uint8_t full;

    nm[nmn] = 0;
    gw[gwn] = 0;
    if (!nmn) {
        status_msg("GIVE THE GATEWAY A KEY NAME: 1 TO 16 OF A-Z 0-9 - _");
        return 0;
    }
    if (!cfg_set_gateway(gw)) {
        status_msg("THE ADDRESS IS AN ADDRESS AND PORT, FOR EXAMPLE 192.168.1.20:8417");
        return 0;
    }
    fmt_str(cfg_name, nm);
    warned = 0;
    status_msg("SAVING");
    full = gl_put(cfg_name, ses_base);
    disk_trouble(gl_save());
    if (full) {
        warn(WHY_ROW - 1, "Four gateways at most: the one used longest ago is forgotten.");
    }
    if (warned) {
        status_msg("PRESS A KEY TO GO ON");
        key_get();
    }
    return 1;
}

/* The key for the gateway in use, made first if the card has none. 1 to
 * connect, 0 to go back to the fields. */
static uint8_t key_step(void)
{
    uint8_t c;

    if (!ks_have && card == KS_LOCKED) {
        return 1;                   /* the passphrase comes first (link.c) */
    }
    if (!ks_have) {
        warned = 0;
        status_msg("MAKING THIS APPLE'S KEY");
        if (make_master()) {
            key_get();
            return 0;
        }
        if (warned) {
            status_msg("PRESS A KEY TO GO ON");
            key_get();
        }
    }
    c = show_key(cfg_name, KEY_ROW, KEY_HINT);
    return (uint8_t)(c && c != K_ESC);
}

uint8_t setup_main(uint8_t at_key, const char *why)
{
    uint8_t c, saved = 0;
    const char *g = ses_base;

    /* At the key, the gateway in use; with none named yet, the address
     * from before key names, if any; else a new one, empty. */
    if (!strncmp(g, "http://", 7)) {
        g += 7;
    }
    gwn = nmn = 0;
    if (at_key || !cfg_name[0]) {
        gwn = (uint8_t)strlen(g);
        if (gwn > GW_MAX) {
            gwn = GW_MAX;
        }
        memcpy(gw, g, gwn);
    }
    if (at_key) {
        fmt_str(nm, cfg_name);
        nmn = (uint8_t)strlen(nm);
    }
    field = 0;
    gl_load();
    take_card();
    draw(why);
    if (at_key && nmn && key_step()) {
        return 1;
    }

    for (;;) {
        c = key_get();
        if (key_oa) {
            continue;
        }
        switch (c) {
        case K_ESC:
            /* A gateway saved (now in use) is reported, so the link
             * connects to it. */
            return saved;
        case K_TAB: case K_DOWN: case K_UP:
            field ^= 1;
            draw_field(0);
            draw_field(1);
            break;
        case K_RETURN:
            if (next()) {
                saved = 1;
                say(WHY_ROW, 0, 0);
                if (key_step()) {
                    return 1;
                }
                status_msg(HINT);
            }
            break;
        case K_DELETE: case K_LEFT:
            if (field == 0 && nmn) {
                --nmn;
            } else if (field == 1 && gwn) {
                --gwn;
            }
            draw_field(field);
            break;
        default:
            if (field == 0) {
                if (c >= 'A' && c <= 'Z') {
                    c |= 0x20;
                }
                if (cfg_name_ok((const char *)&c, 1) && nmn < CFG_NAME_MAX) {
                    nm[nmn++] = (char)c;
                }
            } else if (c > ' ' && c < 0x7F && gwn < GW_MAX) {
                gw[gwn++] = (char)c;
            }
            draw_field(field);
            break;
        }
    }
}
