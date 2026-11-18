/*
 * The set-up screen: the gateway's key name and address, this Apple's
 * key, and an optional passphrase (lock.h).
 *
 * Shown at the first start, when the gateway does not know the key, and
 * on Open-Apple-K. The key name and address go to TWOEGRAM.CFG
 * (config.h). The key is made here (Open-Apple-M) from the card's random
 * numbers: a master that never leaves the card, from which each
 * gateway's key is derived by its key name (docs/RELAY.md, part 1). So
 * moving to another gateway is typing its name and address: no key is
 * typed again. Open-Apple-V shows the gateway's key, to type there with
 * `a2 add`; nothing else shows it, and it is wiped from the screen and
 * memory at the next key pressed. A key made by a gateway (`a2 pair`),
 * already on the card, still works, but set-up no longer takes one.
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

/* The set-up screen is an overlay (overlay.h): its code, text and
 * variables (segment OVL1DATA, cfg/twoegram.cfg says why) are loaded
 * from TWOEGRAM.SETUP while it is open, and gone when it closes. What must outlast it is in main memory: ses_base,
 * cfg_name, ks_secret. */
#ifdef __CC65__
#pragma code-name ("OVERLAY1")
#pragma rodata-name ("OVERLAY1")
#pragma data-name ("OVL1DATA")
#pragma bss-name ("OVL1DATA")
#endif

#define LABEL_COL   2
#define FIELD_COL   14
#define FIELD_W     (SCR_COLS - 2 - FIELD_COL)
#define NAME_ROW    3
#define NNOTE_ROW   4
#define ADDR_ROW    6
#define PASS_ROW    8
#define PNOTE_ROW   9
#define HELP_ROW    11
#define KEY_ROW     18              /* the key, while shown */
#define WHY_ROW     22
#define GW_MAX      48
#define HINT        "TAB: NEXT FIELD   RETURN: SAVE AND CONNECT   ESC: CANCEL"

static char    nm[CFG_NAME_MAX + 1];
static uint8_t nmn;
static char    gw[GW_MAX + 1];
static uint8_t gwn;
static uint8_t field;              /* 0 key name, 1 address, 2 passphrase */
static char    pass[PASS_MAX + 1], pass1[PASS_MAX + 1];
static uint8_t pn, p1n;            /* p1n: the first typing, being confirmed */
static uint8_t card;               /* the card: 0 a secret, KS_LOCKED, KS_NONE */
static uint8_t changed;            /* a new key: the session starts again */
static uint8_t warned;
static char    line[96];
static uint8_t k[34];              /* a derived key while shown; the new master's makings */
static const char *labels[3] = { "Key name", "Address", "Passphrase" };

static const char *const help_text[] = {
    "Key name: this Apple's key for the gateway is made from it, so give each",
    "gateway its own. " OA "-V shows that key, to type on the gateway with",
    "openclaw a2 add, or hermes a2 add. " OA "-M makes this Apple's key, kept in",
    "the CoGS card's own memory, never on a disk; it never leaves the card.",
    "The passphrase, if you give one, locks the key on the card. It is asked",
    "for at every start, and after " OA "-L; nothing keeps it.",
    OA "-X removes the key from the card; " OA "-P removes the passphrase.",
};

#define NHELP   (sizeof help_text / sizeof help_text[0])

#ifdef __CC65__

#include <apple2.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

uint8_t cfg_save(void)
{
    int fd, n;
    uint8_t tries, rc;
    char *p;

    p = fmt_str(line, "# Twoegram: the gateway\rGATEWAY=");
    if (cfg_name[0]) {
        p = fmt_str(fmt_str(p, cfg_name), " ");
    }
    p = fmt_str(fmt_str(p, ses_base), "\r");
    n = (int)(p - line);
    /* A block write to a CoGS network disk can fail with $27 (Newsiie's
     * milestone 8 notes): write the whole file again, up to three times. */
    for (tries = 0; tries < 3; ++tries) {
        _filetype = 0x04;           /* TXT */
        _auxtype = 0;
        fd = open(CFG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            rc = _oserror ? _oserror : 0xFF;
        } else {
            rc = write(fd, line, n) == n ? 0 : (_oserror ? _oserror : 0xFF);
            if (close(fd) && !rc) {
                rc = _oserror ? _oserror : 0xFF;
            }
        }
        if (rc != 0x27) {
            return rc;
        }
    }
    return rc;
}

#endif

/* ---- drawing ---- */

static void say(uint8_t row, const char *s, uint8_t inverse)
{
    lb_clear(SC_SPACE);
    if (s) {
        lb_text(LABEL_COL, s, inverse);
    }
    scr_blit(row);
}

static void draw_field(uint8_t f)
{
    static const uint8_t rows[3] = { NAME_ROW, ADDR_ROW, PASS_ROW };
    const char *text = f == 0 ? nm : f == 1 ? gw : line;
    uint8_t n = f == 0 ? nmn : f == 1 ? gwn : pn, x;

    if (f == 2) {
        memset(line, '*', pn);
    }
    lb_clear(SC_SPACE);
    lb_text(LABEL_COL, labels[f], field == f);
    lb_fill(FIELD_COL, FIELD_W, SC_UNDER);
    x = lb_textn(FIELD_COL, text, n, 0);
    if (field == f && x < SCR_COLS - 1) {
        linebuf[x] = SC_ISPACE;                 /* the cursor */
    }
    scr_blit(rows[f]);
}

static void draw_fields(void)
{
    draw_field(0);
    draw_field(1);
    draw_field(2);
}

static void draw_help(void)
{
    uint8_t i;

    for (i = 0; i < NHELP; ++i) {
        say((uint8_t)(HELP_ROW + i), help_text[i], 0);
    }
}

static void note(uint8_t row, const char *s)
{
    lb_clear(SC_SPACE);
    lb_text(FIELD_COL, s, 0);
    scr_blit(row);
}

static void draw(const char *why)
{
    scr_clear(0, BODY_ROWS - 1);
    lb_clear(SC_SPACE);
    lb_text(1, "Twoegram set-up", 0);
    lb_text((uint8_t)(SCR_COLS - 1 - txt_len("build " BUILD_ID)), "build " BUILD_ID, 0);
    scr_blit(0);
    lb_clear(has_mousetext ? MT_RULE : SC_DASH);
    scr_blit(1);
    draw_fields();
    note(NNOTE_ROW, "1 to 16 of a-z 0-9 - _, such as claw1. Each name has its own key.");
    note(PNOTE_ROW, "Optional: 8 to 32 characters, typed twice.");
    draw_help();
    if (why) {
        say(WHY_ROW, why, 1);
    }
    status_msg(HINT);
}

static void warn(uint8_t row, const char *s)
{
    say(row, s, 1);
    warned = 1;
}

static uint8_t yes(const char *q)
{
    uint8_t c;

    status_msg(q);
    c = key_get();
    status_msg(HINT);
    return (uint8_t)(c == 'y' || c == 'Y');
}

static void wipe_pass(void)
{
    memset(pass, 0, sizeof pass);
    memset(pass1, 0, sizeof pass1);
    pn = p1n = 0;
}

/* The card's secret is not in memory: why, on the status bar. 0 if it is. */
static uint8_t no_secret(void)
{
    if (ks_have) {
        return 0;
    }
    status_msg(card == KS_LOCKED ? "THE KEY IS LOCKED: START AGAIN AND TYPE THE PASSPHRASE"
                                 : "THERE IS NO KEY YET: OPEN-APPLE-M MAKES ONE");
    return 1;
}

static void card_trouble(uint8_t rc)
{
    if (rc == KS_NOROOM) {
        warn(WHY_ROW - 1, "The card cannot keep the key yet; it will be made again.");
    } else if (rc == KS_INUSE) {
        warn(WHY_ROW - 1, "The card's spare memory is in use: the key is not kept there.");
    } else if (rc) {
        fmt_str(fmt_str(line, "The key could not be kept on the card: "), cogs_strerror(rc));
        warn(WHY_ROW - 1, line);
    }
}

/* ---- the key ---- */

/* k in the written form (PROTOCOL.md section 1), with the device id it
 * makes, into line. */
static void write_key(void)
{
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ*~$=U";
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

/* Open-Apple-V: the key for the gateway named in the field, until a key
 * is pressed. */
static void show_key(void)
{
    static uint8_t dig[32];
    cogs_part_t parts[2];
    uint8_t rc;

    if (no_secret()) {
        return;
    }
    if (!ks_master) {
        status_msg("THIS KEY CAME FROM A GATEWAY: OPEN-APPLE-M MAKES ONE HERE");
        return;
    }
    nm[nmn] = 0;
    if (!nmn) {
        status_msg("GIVE THE GATEWAY A KEY NAME FIRST");
        return;
    }
    rc = ks_derive(ks_secret, nm, k);
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
        say(KEY_ROW, "This Apple's key for the gateway:", 0);
        lb_clear(SC_SPACE);
        lb_text(FIELD_COL, line, 1);
        scr_blit(KEY_ROW + 1);
        say(KEY_ROW + 2, "Type it on the gateway after openclaw a2 add (or hermes a2 add); the", 0);
        say(KEY_ROW + 3, "gateway then shows the device id, which should be the same.", 0);
        status_msg("PRESS A KEY WHEN IT IS ADDED");
        key_get();
        scr_clear(KEY_ROW, KEY_ROW + 3);
        status_msg(HINT);
    }
    memset(k, 0, sizeof k);
    memset(line, 0, sizeof line);
    memset(dig, 0, sizeof dig);
}

/* Open-Apple-M: a new master, from two draws of the card's random
 * numbers and the tick count, hashed together by the card. */
static void make_master(void)
{
    cogs_part_t part;
    uint8_t rc;

    if (card != KS_NONE &&
        !yes(ks_master ? "A NEW KEY: EVERY GATEWAY MUST HAVE THIS APPLE ADDED AGAIN.  Y OR N"
                       : "THIS REPLACES THE KEY FROM THE GATEWAY: ADD THE NEW ONE THERE.  Y OR N")) {
        return;
    }
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
        return;
    }
    ks_master = 1;
    ks_have = 1;
    lock_armed = 0;
    changed = 1;
    card = 0;
    warned = 0;
    card_trouble(ks_save(ks_secret, KS_MASTER));
    status_msg("THIS APPLE HAS A NEW KEY: OPEN-APPLE-V SHOWS THE GATEWAY'S");
    if (warned) {
        key_get();
    }
}

/* Open-Apple-X. */
static void forget(void)
{
    if (yes("REMOVE THE KEY FROM THE CARD?  Y OR N")) {
        status_msg(ks_clear() ? "COULD NOT REMOVE THE KEY" : "THE KEY IS GONE FROM THE CARD");
        lock_armed = 0;
        ks_master = 0;
        card = KS_NONE;
        ks_forget();
    }
}

/* Open-Apple-P: keep the key on the card without its passphrase. It
 * must have been unlocked at the start. */
static void unlock_card(void)
{
    if (!lock_armed || !ks_have) {
        status_msg("NO PASSPHRASE IS IN USE");
    } else if (yes("REMOVE THE PASSPHRASE?  Y OR N")) {
        status_msg(ks_save(ks_secret, KS_MARK_FOR(0)) ? "COULD NOT REMOVE THE PASSPHRASE"
                                                       : "THE KEY NO LONGER NEEDS A PASSPHRASE");
        if (!ks_load((uint8_t *)pass1)) {       /* scratch: the secret, as kept */
            lock_armed = 0;
        }
        wipe_pass();
    }
}

/* ---- saving ---- */

/* Returns 1 to leave the screen. A problem that does not stop the
 * session (a write-protected disk, a key that cannot be kept on the
 * card) is said and waited for. */
static uint8_t save(void)
{
    uint8_t rc;

    nm[nmn] = 0;
    gw[gwn] = 0;
    if (card == KS_NONE) {
        status_msg("MAKE THIS APPLE'S KEY FIRST: OPEN-APPLE-M");
        return 0;
    }
    if (ks_master && !nmn) {
        status_msg("GIVE THE GATEWAY A KEY NAME: 1 TO 16 OF A-Z 0-9 - _");
        return 0;
    }
    if (!cfg_set_gateway(gw)) {
        status_msg("THE ADDRESS IS AN ADDRESS AND PORT, FOR EXAMPLE 192.168.1.20:8417");
        return 0;
    }
    if (pn) {
        if (no_secret()) {
            return 0;
        }
        if (pn < PASS_MIN) {
            status_msg("A PASSPHRASE IS AT LEAST 8 CHARACTERS");
            return 0;
        }
        if (!p1n) {
            memcpy(pass1, pass, sizeof pass);
            p1n = pn;
            memset(pass, 0, sizeof pass);
            pn = 0;
            field = 2;
            draw_fields();
            status_msg("TYPE THE PASSPHRASE AGAIN, THEN RETURN");
            return 0;
        }
        if (pn != p1n || memcmp(pass, pass1, pn)) {
            wipe_pass();
            draw_field(2);
            status_msg("THE TWO DIFFER: TYPE THE PASSPHRASE AGAIN");
            return 0;
        }
    }
    fmt_str(cfg_name, nm);

    /* The file first, the card second: no network in between. */
    warned = 0;
    status_msg("SAVING");
    rc = cfg_save();
    if (rc == 0x2B) {
        warn(WHY_ROW - 2, "The disk is write-protected: the gateway is not saved.");
    } else if (rc) {
        fmt_hex8(fmt_str(line, "The gateway could not be saved: ProDOS error $"), rc);
        warn(WHY_ROW - 2, line);
    }
    if (pn) {
        memcpy(k, ks_secret, A2_KEY_LEN);
        rc = ks_scramble(k, pass, pn);          /* ks_secret stays plain */
        if (!rc) {
            rc = ks_save(k, KS_MARK_FOR(1));
        }
        memset(k, 0, sizeof k);
        lock_armed = 1;
        card_trouble(rc);
    }
    wipe_pass();
    if (ks_have) {
        lock_use();
    }
    if (warned) {
        status_msg("PRESS A KEY TO CONNECT ANYWAY");
        key_get();
    }
    return 1;
}

/* ---- typing ---- */

uint8_t setup_main(const char *why)
{
    uint8_t c;
    const char *g = ses_base;

    card = ks_load((uint8_t *)pass1);           /* what the card holds */
    if (card && card != KS_LOCKED) {
        card = KS_NONE;
    }
    if (!card && !ks_have) {                    /* plain on the card: take it */
        memcpy(ks_secret, pass1, A2_KEY_LEN);
        ks_have = 1;
    }
    wipe_pass();
    changed = 0;
    if (!strncmp(g, "http://", 7)) {
        g += 7;
    }
    gwn = (uint8_t)strlen(g);
    if (gwn > GW_MAX) {
        gwn = GW_MAX;
    }
    memcpy(gw, g, gwn);
    fmt_str(nm, cfg_name);
    nmn = (uint8_t)strlen(nm);
    field = 0;
    draw(why);

    for (;;) {
        c = key_get();
        if (key_oa) {
            c |= 0x20;                          /* lowercase */
            if (c == 'm') {
                make_master();
            } else if (c == 'v') {
                show_key();
            } else if (c == 'x') {
                forget();
            } else if (c == 'p') {
                unlock_card();
            }
            continue;
        }
        switch (c) {
        case K_ESC:
            wipe_pass();
            if (changed && ks_have) {
                lock_use();
            }
            return changed;
        case K_TAB: case K_DOWN:
            field = (uint8_t)(field == 2 ? 0 : field + 1);
            draw_fields();
            break;
        case K_UP:
            field = (uint8_t)(field ? field - 1 : 2);
            draw_fields();
            break;
        case K_RETURN:
            if (save()) {
                return 1;
            }
            break;
        case K_DELETE: case K_LEFT:
            if (field == 0 && nmn) {
                --nmn;
            } else if (field == 1 && gwn) {
                --gwn;
            } else if (field == 2 && pn) {
                pass[--pn] = 0;
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
            } else if (field == 1) {
                if (c > ' ' && c < 0x7F && gwn < GW_MAX) {
                    gw[gwn++] = (char)c;
                }
            } else if (c >= ' ' && c < 0x7F && pn < PASS_MAX) {
                pass[pn++] = (char)c;
            }
            draw_field(field);
            break;
        }
    }
}
