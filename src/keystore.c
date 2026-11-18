/*
 * The device key in the card's prefs blob. See keystore.h.
 */
#include <string.h>
#include "cogs_http.h"
#include "hmac.h"
#include "keystore.h"

static uint8_t blob[COGS_PREFS_MAX], back[COGS_PREFS_MAX];

/* Whether byte 15 is one of ours. */
static uint8_t ours(uint8_t n)
{
    return (uint8_t)(n == COGS_PREFS_MAX &&
                     (blob[COGS_PREFS_FREE] | 1) == KS_MARK_PASS);
}

uint8_t ks_load(uint8_t key[A2_KEY_LEN])
{
    uint8_t n, rc = cogs_prefs_get(blob, &n);

    if (rc) {
        return rc;
    }
    if (!ours(n)) {
        return KS_NONE;
    }
    memcpy(key, blob + COGS_PREFS_FREE + 1, A2_KEY_LEN);
    rc = (uint8_t)(blob[COGS_PREFS_FREE] == KS_MARK_PASS ? KS_LOCKED : 0);
    memset(blob, 0, sizeof blob);
    return rc;
}

uint8_t ks_save(const uint8_t key[A2_KEY_LEN], uint8_t mark)
{
    uint8_t n, n2, i, rc = cogs_prefs_get(blob, &n);

    if (rc) {
        return rc;
    }
    if (n < COGS_PREFS_FREE) {
        /* Writing a longer blob would set the card's own settings to
         * values we would have to invent. */
        return KS_NOROOM;
    }
    if (n > COGS_PREFS_FREE && !ours(n)) {
        for (i = COGS_PREFS_FREE; i < n; ++i) {
            if (blob[i]) {
                return KS_INUSE;
            }
        }
    }
    blob[COGS_PREFS_FREE] = mark;
    memcpy(blob + COGS_PREFS_FREE + 1, key, A2_KEY_LEN);
    rc = cogs_prefs_set(blob, COGS_PREFS_MAX);
    if (!rc) {
        rc = cogs_prefs_get(back, &n2);
    }
    /* Only our part is checked: the card may tidy its own settings
     * (bytes 0-14) as it stores them. */
    if (!rc && (n2 != COGS_PREFS_MAX ||
                memcmp(back + COGS_PREFS_FREE, blob + COGS_PREFS_FREE, COGS_PREFS_MAX - COGS_PREFS_FREE))) {
        rc = COGS_E_PROTO;
    }
    memset(blob, 0, sizeof blob);
    memset(back, 0, sizeof back);
    return rc;
}

/* In the language card, with the lock (lock.c): it runs only at the
 * start, on a gateway chosen in set-up, and after unlocking. Its parts
 * are written as it runs, so they stay in main memory's DATA. */
#ifdef __CC65__
#pragma code-name (push, "LC")
#pragma rodata-name (push, "LC")
#endif
uint8_t ks_derive(const uint8_t master[A2_KEY_LEN], const char *name, uint8_t key[A2_KEY_LEN])
{
    static cogs_part_t p[2] = { { (const uint8_t *)"twoegram-gw", 11 } };
    uint8_t rc;

    COGS_PART(p[1], name, strlen(name));
    /* blob is idle here (only ks_load and ks_save use it, and wipe it). */
    rc = hmac_parts(master, A2_KEY_LEN, p, 2, blob);
    memcpy(key, blob, A2_KEY_LEN);
    memset(blob, 0, 32);
    return rc;
}
#ifdef __CC65__
#pragma rodata-name (pop)
#pragma code-name (pop)
#endif

uint8_t ks_clear(void)
{
    uint8_t n, rc = cogs_prefs_get(blob, &n);

    if (rc) {
        return rc;
    }
    if (!ours(n)) {
        return 0;                   /* nothing of ours there */
    }
    /* Back to the card's own fifteen bytes, as before the first save. */
    rc = cogs_prefs_set(blob, COGS_PREFS_FREE);
    memset(blob, 0, sizeof blob);
    return rc;
}

uint8_t ks_scramble(uint8_t key[A2_KEY_LEN], const char *pass, uint8_t n)
{
    static uint8_t pad[32];
    cogs_part_t parts[2];
    uint8_t i, rc = 0;

    /* pad = H("twoegram-pass" || pass), then H(pad || pass) again and
     * again: a little work per guess, though the passphrase's length is
     * what really protects it. */
    COGS_PART(parts[0], "twoegram-pass", 13);
    COGS_PART(parts[1], pass, n);
    for (i = 0; i < PASS_ROUNDS; ++i) {
        rc = sha256_parts(parts, 2, pad);
        if (rc) {
            break;
        }
        COGS_PART(parts[0], pad, 32);
    }
    if (!rc) {
        for (i = 0; i < A2_KEY_LEN; ++i) {
            key[i] ^= pad[i];
        }
    }
    memset(pad, 0, sizeof pad);
    return rc;
}

