/*
 * The passphrase prompt and the lock. See lock.h.
 *
 * In the language card: nothing here calls ProDOS, only the card (to
 * read the key and to hash) and the screen.
 */
#include <string.h>
#include "screen.h"
#include "kbd.h"
#include "ui.h"
#include "proto.h"
#include "session.h"
#include "keystore.h"
#include "config.h"
#include "lock.h"

#ifdef __CC65__
#pragma code-name ("LC")
#endif

#define PROMPT_ROW  10
#define FIELD_ROW   12
#define WHY_ROW     14
#define FIELD_COL   20

static char    pass[PASS_MAX + 1];
static uint8_t pn;
static char    was_id[9];

uint8_t lock_armed;

static void line(uint8_t row, const char *s, uint8_t inverse)
{
    lb_clear(SC_SPACE);
    if (s) {
        lb_text(FIELD_COL, s, inverse);
    }
    scr_blit(row);
}

/* One star a character, and the cursor. */
static void draw_field(void)
{
    uint8_t i;

    lb_clear(SC_SPACE);
    lb_fill(FIELD_COL, PASS_MAX + 1, SC_UNDER);
    for (i = 0; i < pn; ++i) {
        linebuf[FIELD_COL + i] = (uint8_t)('*' | 0x80);
    }
    linebuf[FIELD_COL + pn] = SC_ISPACE;
    scr_blit(FIELD_ROW);
}

/* The prompt, on an otherwise empty screen; the passphrase in pass, pn
 * long. 0 if Esc was pressed (when esc is allowed). */
static uint8_t ask(const char *head, const char *why, uint8_t esc)
{
    uint8_t k;

    scr_clear(0, BODY_ROWS - 1);
    line(PROMPT_ROW, head, 0);
    pn = 0;
    draw_field();
    line(WHY_ROW, why, 1);
    status_msg(esc ? "TYPE THE PASSPHRASE, THEN RETURN   ESC: SETTINGS"
                   : "TYPE THE PASSPHRASE, THEN RETURN");
    for (;;) {
        k = key_get();
        if (key_oa) {
            continue;
        }
        if (k == K_RETURN && pn) {
            status_msg("UNLOCKING");
            return 1;
        }
        if (k == K_ESC && esc) {
            pn = 0;
            return 0;
        }
        if ((k == K_DELETE || k == K_LEFT) && pn) {
            --pn;
        } else if (k >= ' ' && k < 0x7F && pn < PASS_MAX) {
            pass[pn++] = (char)k;
        }
        draw_field();
    }
}

uint8_t lock_use(void)
{
    if (!ks_have) {
        return KS_NONE;
    }
    if (!cfg_name[0]) {
        return KS_NONAME;
    }
    return ks_derive(ks_secret, cfg_name, ses_key);
}

/* ks_secret = the card's scrambled secret, unscrambled with pass, and
 * ses_key from it. */
static uint8_t unscramble(void)
{
    uint8_t rc = ks_load(ks_secret);

    if (rc == KS_LOCKED) {
        rc = ks_scramble(ks_secret, pass, pn);
    }
    memset(pass, 0, sizeof pass);
    pn = 0;
    if (!rc) {
        ks_have = 1;
        rc = lock_use();
    }
    return rc;
}

uint8_t lock_open(const char *why)
{
    /* link.c sees to a key name before asking (KS_NONAME is a failure
     * here). */
    if (!ask("This Apple's key is locked with a passphrase.", why, 1) || unscramble()) {
        ks_forget();
        memset(ses_key, 0, A2_KEY_LEN);
        return 0;
    }
    lock_armed = 1;
    return 1;
}

void lock_now(void)
{
    const char *why = 0;

    memcpy(was_id, ses_id, sizeof was_id);
    ses_forget();
    ks_forget();
    for (;;) {
        ask("Twoegram is locked.", why, 0);
        /* The right passphrase makes the key that made this device id. */
        if (!unscramble() && !ses_device_id() && !memcmp(ses_id, was_id, 8)) {
            return;
        }
        memset(ses_key, 0, A2_KEY_LEN);
        ks_forget();
        memcpy(ses_id, was_id, sizeof was_id);
        why = "That is not the passphrase.";
    }
}
