/*
 * The device key in the card's prefs blob. See keystore.h.
 */
#include <string.h>
#include "cogs_http.h"
#include "keystore.h"

static uint8_t blob[COGS_PREFS_MAX], back[COGS_PREFS_MAX];

uint8_t ks_load(uint8_t key[A2_KEY_LEN])
{
    uint8_t n, rc = cogs_prefs_get(blob, &n);

    if (rc) {
        return rc;
    }
    if (n != COGS_PREFS_MAX || blob[COGS_PREFS_FREE] != KS_MARK) {
        return KS_NONE;
    }
    memcpy(key, blob + COGS_PREFS_FREE + 1, A2_KEY_LEN);
    return 0;
}

uint8_t ks_save(const uint8_t key[A2_KEY_LEN])
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
    if (n > COGS_PREFS_FREE && blob[COGS_PREFS_FREE] != KS_MARK) {
        for (i = COGS_PREFS_FREE; i < n; ++i) {
            if (blob[i]) {
                return KS_INUSE;
            }
        }
    }
    blob[COGS_PREFS_FREE] = KS_MARK;
    memcpy(blob + COGS_PREFS_FREE + 1, key, A2_KEY_LEN);
    rc = cogs_prefs_set(blob, COGS_PREFS_MAX);
    if (!rc) {
        rc = cogs_prefs_get(back, &n2);
    }
    if (!rc && (n2 != COGS_PREFS_MAX || memcmp(back, blob, COGS_PREFS_MAX))) {
        rc = COGS_E_PROTO;
    }
    memset(blob, 0, sizeof blob);
    memset(back, 0, sizeof back);
    return rc;
}

uint8_t ks_clear(void)
{
    uint8_t n, rc = cogs_prefs_get(blob, &n);

    if (rc) {
        return rc;
    }
    if (n != COGS_PREFS_MAX || blob[COGS_PREFS_FREE] != KS_MARK) {
        return 0;                   /* nothing of ours there */
    }
    /* Back to the card's own fifteen bytes, as before the first save. */
    rc = cogs_prefs_set(blob, COGS_PREFS_FREE);
    memset(blob, 0, sizeof blob);
    return rc;
}
