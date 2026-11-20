/*
 * Settings (Open-Apple-K): the saved gateways, and this Apple's key.
 *
 *   Gateways           switch to one, show its key again, forget one,
 *                      or add one (the wizard, setup.c)
 *   This Apple's key   the passphrase (set, change, remove), starting
 *                      over with a new key, removing the key
 *
 * One list to move through with the arrows; the status bar and the lines
 * below say what each row's keys do. The passphrase locks the master on
 * the card, so it covers every gateway (lock.h).
 *
 * The third overlay (overlay.h): TWOEGRAM.PREFS, at $0800 like the
 * others. Adding a gateway is the first overlay, which cannot be loaded
 * over this one while it runs: so for anything that shows a key (adding
 * a gateway, showing one's key again, starting over) Settings returns,
 * and link.c opens it. Starting over is clearing the card: adding the
 * key then makes a new master, as on a new card.
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

#ifdef __CC65__
#pragma code-name ("OVERLAY3")
#pragma rodata-name ("OVERLAY3")
#pragma data-name ("OVL3DATA")
#pragma bss-name ("OVL3DATA")
#endif

#define SETUP_LIST_PREFS
#include "setup_list.h"

#define ITEM_COL    4
#define VALUE_COL   24
#define USE_COL     64
#define GW_HEAD_ROW 3               /* gateways from the row below it */
#define KEY_HEAD_ROW 10             /* then the passphrase, new key, remove */
#define TELL_ROW    15              /* two lines about the row chosen */

/* The rows after the gateways, in order. */
#define IT_ADD      0
#define IT_PASS     1
#define IT_NEW      2
#define IT_GONE     3

static uint8_t sel;                 /* the row chosen */
static uint8_t used;                /* the gateway in use changed */
static char    pass[PASS_MAX + 1];  /* the first typing is kept in line */
static uint8_t pn, p1n;

static const char locked[] = "THE KEY IS LOCKED: START AGAIN AND TYPE THE PASSPHRASE";

static const char *const tell_gw[2] = {
    "K shows this Apple's key for it, to add there again, and connects.",
    "Forgetting it here leaves the key on the gateway, until a2 revoke there.",
};
static const char *const tell_add[2] = {
    "Create a new gateway configuration. Can be an individual Twoegram client",
    "or a Twoegram relay server.",
};
static const char *const tell_pass[2] = {
    "Locks the key on the card and enables a passphrase protected screensaver",
    "after 10 minutes. Use " OA "-L to lock the screen manually.",
};
static const char *const tell_new[2] = {
    "Regenerates this machine's key and removes the passphrase. All gateways",
    "will need to add the new key.",
};
static const char *const tell_gone[2] = {
    "Removes the key from the card. (A new key is made when a gateway is added.)",
    0,
};

/* ---- drawing ---- */

/* The row's item: a gateway below ngw, else IT_ADD and on. */
static uint8_t row_of(uint8_t it)
{
    return it < ngw ? (uint8_t)(GW_HEAD_ROW + 1 + it)
         : it == ngw + IT_ADD ? (uint8_t)(GW_HEAD_ROW + 1 + ngw)
         : (uint8_t)(KEY_HEAD_ROW + it - ngw - IT_ADD);
}

static void draw_item(uint8_t it)
{
    uint8_t on = (uint8_t)(it == sel), x = (uint8_t)(it - ngw);
    const char *s;

    lb_clear(SC_SPACE);
    if (it < ngw) {
        lb_text(ITEM_COL, gws[it].name, on);
        lb_text(VALUE_COL, gws[it].addr + 7, 0);
        if (!it) {
            lb_text(USE_COL, "in use", 0);
        }
    } else if (x == IT_ADD) {
        lb_text(ITEM_COL, "Add a gateway", on);
    } else if (x == IT_PASS) {
        lb_text(ITEM_COL, "Passphrase", on);
        s = lock_armed ? "set" : card == KS_LOCKED && !ks_have ? "set, and locked" : "none";
        lb_text(VALUE_COL, s, 0);
    } else {
        lb_text(ITEM_COL, x == IT_NEW ? "Start over with a new key" : "Remove the key from the card", on);
    }
    scr_blit(row_of(it));
}

/* What the chosen row is for, below the list and on the status bar. */
static void tell(void)
{
    const char *const *t;
    const char *hint;
    uint8_t i, x = (uint8_t)(sel - ngw);

    if (sel < ngw) {
        t = tell_gw;
        hint = "RETURN: CONNECT   K: ITS KEY   DELETE: FORGET IT   ESC: BACK";
    } else if (x == IT_ADD) {
        t = tell_add;
        hint = "RETURN: ADD A GATEWAY   ESC: BACK";
    } else if (x == IT_PASS) {
        t = tell_pass;
        hint = lock_armed ? "RETURN: CHANGE IT   DELETE: REMOVE IT   ESC: BACK" : "RETURN: SET ONE   ESC: BACK";
    } else if (x == IT_NEW) {
        t = tell_new;
        hint = "RETURN: START OVER   ESC: BACK";
    } else {
        t = tell_gone;
        hint = "RETURN: REMOVE THE KEY   ESC: BACK";
    }
    for (i = 0; i < 2; ++i) {
        say((uint8_t)(TELL_ROW + i), t[i], 0);
    }
    status_msg(hint);
}

static void draw_list(void)
{
    uint8_t i;

    scr_clear(GW_HEAD_ROW, (uint8_t)(KEY_HEAD_ROW + IT_GONE));
    say(GW_HEAD_ROW, "Gateways", 0);
    say(KEY_HEAD_ROW, "This Apple's key", 0);
    for (i = 0; i < ngw + 4; ++i) {
        draw_item(i);
    }
    tell();
}

static void draw(const char *why)
{
    scr_clear(0, BODY_ROWS - 1);
    lb_clear(SC_SPACE);
    lb_text(1, "Twoegram settings", 0);
    lb_text((uint8_t)(SCR_COLS - 1 - txt_len("build " BUILD_ID)), "build " BUILD_ID, 0);
    scr_blit(0);
    lb_clear(has_mousetext ? MT_RULE : SC_DASH);
    scr_blit(1);
    if (why) {
        say(WHY_ROW, why, 1);
    }
    draw_list();
}

/* Said, and waited for if anything went wrong. */
static void done(const char *msg)
{
    status_msg(msg);
    if (warned) {
        key_get();
    }
    tell();
}

/* ---- the passphrase ---- */

static void wipe_pass(void)
{
    memset(pass, 0, sizeof pass);
    memset(line, 0, sizeof line);
    pn = p1n = 0;
}

/* Typed into pass on the passphrase's row, one star a character. 1 on
 * Return, 0 on Esc. */
static uint8_t type_pass(const char *hint)
{
    uint8_t c, i, row = row_of((uint8_t)(ngw + IT_PASS));

    pn = 0;
    status_msg(hint);
    for (;;) {
        lb_clear(SC_SPACE);
        lb_text(ITEM_COL, "Passphrase", 1);
        lb_fill(VALUE_COL, PASS_MAX + 1, SC_UNDER);
        for (i = 0; i < pn; ++i) {
            linebuf[VALUE_COL + i] = (uint8_t)('*' | 0x80);
        }
        linebuf[VALUE_COL + pn] = SC_ISPACE;
        scr_blit(row);
        c = key_get();
        if (key_oa) {
            continue;
        }
        if (c == K_RETURN && pn) {
            return 1;
        }
        if (c == K_ESC) {
            return 0;
        }
        if ((c == K_DELETE || c == K_LEFT) && pn) {
            pass[--pn] = 0;
        } else if (c >= ' ' && c < 0x7F && pn < PASS_MAX) {
            pass[pn++] = (char)c;
        }
    }
}

/* Return on the passphrase: a new one, typed twice, locks the key. */
static void set_pass(void)
{
    uint8_t rc;

    if (!ks_have) {
        status_msg(card == KS_LOCKED ? (const char *)locked
                                     : (const char *)"THERE IS NO KEY YET: ADD A GATEWAY, AND ONE IS MADE");
        return;
    }
    say(WHY_ROW, 0, 0);
    for (;;) {
        if (!type_pass("TYPE THE PASSPHRASE, 8 TO 32 CHARACTERS, THEN RETURN   ESC: CANCEL")) {
            break;
        }
        if (pn < PASS_MIN) {
            say(WHY_ROW, "A passphrase is at least 8 characters.", 1);
            continue;
        }
        memcpy(line, pass, sizeof pass);
        p1n = pn;
        if (!type_pass("TYPE IT AGAIN, THEN RETURN   ESC: CANCEL")) {
            break;
        }
        if (pn != p1n || memcmp(pass, line, pn)) {
            say(WHY_ROW, "The two differ: type the passphrase again.", 1);
            continue;
        }
        say(WHY_ROW, 0, 0);
        warned = 0;
        status_msg("LOCKING THE KEY");
        memcpy(line, ks_secret, A2_KEY_LEN);
        rc = ks_scramble((uint8_t *)line, pass, pn);  /* ks_secret stays plain */
        if (!rc) {
            rc = ks_save((uint8_t *)line, KS_MARK_FOR(1));
        }
        memset(line, 0, sizeof line);
        card_trouble(rc);
        if (!rc) {
            lock_armed = 1;
        }
        wipe_pass();
        draw_item(sel);
        done(rc ? "THE PASSPHRASE IS NOT SET" : "THE KEY IS LOCKED WITH THE PASSPHRASE");
        return;
    }
    wipe_pass();
    say(WHY_ROW, 0, 0);
    draw_item(sel);
    tell();
}

/* Delete on the passphrase: the key kept plain again. It must have been
 * unlocked at the start. */
static void remove_pass(void)
{
    if (!lock_armed || !ks_have) {
        status_msg("NO PASSPHRASE IS IN USE");
        return;
    }
    if (yes("REMOVE THE PASSPHRASE?  Y OR N")) {
        warned = 0;
        card_trouble(ks_save(ks_secret, KS_MARK_FOR(0)));
        take_card();
        if (!card) {
            lock_armed = 0;
        }
        draw_item(sel);
        done(lock_armed ? "COULD NOT REMOVE THE PASSPHRASE" : "THE KEY NO LONGER NEEDS A PASSPHRASE");
        return;
    }
    tell();
}

/* ---- the gateways ---- */

static void forget(void)
{
    fmt_str(fmt_str(fmt_str(line, "FORGET "), gws[sel].name), " HERE?  Y OR N");
    if (!yes(line)) {
        tell();
        return;
    }
    gl_drop(sel);
    if (!sel) {
        gl_apply();                 /* the next is in use now */
        used = 1;
    }
    warned = 0;
    disk_trouble(gl_save());
    if (sel && sel >= ngw) {
        --sel;
    }
    draw_list();
    done("FORGOTTEN HERE");
}

/* The chosen gateway in use, first in the list: then act. */
static uint8_t use(uint8_t act)
{
    if (sel) {
        gl_front(sel);
        gl_apply();
        warned = 0;
        disk_trouble(gl_save());
        if (warned) {
            status_msg("PRESS A KEY TO GO ON");
            key_get();
        }
    }
    return act;
}

uint8_t prefs_main(const char *why)
{
    uint8_t c, x;

    gl_load();
    take_card();
    sel = 0;
    used = 0;
    wipe_pass();
    draw(why);

    for (;;) {
        c = key_get();
        if (key_oa) {
            continue;
        }
        x = (uint8_t)(sel - ngw);
        switch (c) {
        case K_ESC:
            return used ? PREFS_USE : PREFS_NONE;
        case K_UP: case K_DOWN: case K_TAB:
            x = sel;
            sel = c == K_UP ? (uint8_t)(sel ? sel - 1 : ngw + IT_GONE)
                            : (uint8_t)(sel == ngw + IT_GONE ? 0 : sel + 1);
            draw_item(x);
            draw_item(sel);
            tell();
            break;
        case K_RETURN:
            if (sel < ngw) {
                return use(PREFS_USE);
            }
            if (x == IT_ADD) {
                return PREFS_ADD;
            }
            if (x == IT_PASS) {
                set_pass();
            } else if (yes(x == IT_NEW ? "A NEW KEY: EVERY GATEWAY MUST HAVE THIS APPLE ADDED AGAIN.  Y OR N"
                                       : "REMOVE THE KEY FROM THE CARD?  Y OR N")) {
                if (ks_clear()) {
                    status_msg("COULD NOT REMOVE THE KEY");
                    key_get();
                    tell();
                } else {
                    lock_armed = 0;
                    ks_forget();
                    /* Starting over: connecting finds no key, and adding
                     * it makes one. */
                    return x == IT_NEW ? PREFS_USE : PREFS_GONE;
                }
            } else {
                tell();
            }
            break;
        case 'k': case 'K':
            if (sel < ngw) {
                if (!ks_have && card == KS_LOCKED) {
                    status_msg(locked);
                } else {
                    return use(PREFS_KEY);
                }
            }
            break;
        case K_DELETE: case K_LEFT:
            if (sel < ngw) {
                forget();
            } else if (x == IT_PASS) {
                remove_pass();
            }
            break;
        }
    }
}
