/*
 * Getting to the gateway. See link.h.
 */
#include <string.h>
#include "cogs.h"
#include "screen.h"
#include "tick.h"
#include "kbd.h"
#include "fmt.h"
#include "session.h"
#include "keystore.h"
#include "lock.h"
#include "config.h"
#include "msgs.h"
#include "ui.h"
#include "link.h"

#define BACKSTOP    (30 * TICKS_PER_SEC)    /* the card times out first */
#define LINK_WAIT   20                      /* seconds for a network link */

uint8_t link_live;
char    link_sec[24];
char    link_err[64];
uint8_t link_fails;
uint8_t link_waiting;
uint8_t link_badkey;

static uint16_t start;

/* While a request waits on the network: keep a clock, and give up only
 * if the card itself goes silent. */
static uint8_t idle(void)
{
    tick_poll();
    key_poll();                     /* type-ahead: no key lost while waiting */
    return (uint16_t)(ticks - start) > BACKSTOP ? COGS_E_TIMEOUT : 0;
}

static void offline(const char *sec, const char *err)
{
    link_live = 0;
    link_waiting = 0;
    link_badkey = 0;
    msgs_mode(MSGS_NONE);
    fmt_str(link_sec, sec);
    fmt_str(link_err, err);
}

uint8_t link_trouble(uint8_t rc)
{
    uint8_t code = rc == SES_E_REFUSED ? ses_reply.code : A2_OK;

    link_badkey = 0;
    if (code == A2_UNKNOWN_AGENT) {
        return LT_AGENTS;
    }
    if (code == A2_BAD_REQUEST || code == A2_TOO_LARGE || rc == COGS_E_TOOBIG) {
        return LT_NOTE;
    }
    /* Never retried by itself: a key the gateway does not know or has
     * locked out, a signature it refused, a reply that did not check,
     * something that is not a gateway, no card. */
    if (code == A2_UNKNOWN_DEVICE || code == A2_BAD_SIGNATURE || code == A2_LOCKED ||
        rc == SES_E_FORGED || rc == SES_E_NOTSERVER || rc == COGS_E_NOCARD) {
        offline("NOT CONNECTED", ses_strerror(rc));
        /* Revoked, or the card's key bytes changed (a firmware upgrade,
         * a reset): the key itself is the trouble, and the pane says how
         * to put it right. */
        link_badkey = (uint8_t)(code == A2_UNKNOWN_DEVICE || code == A2_BAD_SIGNATURE);
        return LT_OFFLINE;
    }
    /* Passing: busy, a session lost twice over (the gateway in the middle
     * of restarting), and the card's network errors. */
    if (link_fails < 255) {
        ++link_fails;
    }
    fmt_str(link_sec, code == A2_BUSY ? "GATEWAY BUSY" :
                      rc == COGS_E_NOT_JOINED ? "NO NETWORK" : "GATEWAY NOT ANSWERING");
    return LT_RETRY;
}

uint8_t link_ok(void)
{
    if (!link_fails) {
        return 0;
    }
    link_fails = 0;
    fmt_str(link_sec, SEC_SIGNED);
    return 1;
}

/* Up to 20 s for the card to have a network, as after power-on. */
static uint8_t wait_for_network(void)
{
    uint8_t secs;
    char msg[40];

    for (secs = 0; !(cogs_status() & COGS_ST_LINK_UP); ++secs) {
        if (secs >= LINK_WAIT) {
            return 0;
        }
        fmt_str(fmt_u32(fmt_str(msg, "WAITING FOR THE NETWORK... "), secs), " S");
        status_msg(msg);
        tick_reset();
        tick_wait(TICKS_PER_SEC);
    }
    return 1;
}

void link_begin(void)
{
    tick_reset();
    start = ticks;
    cogs_idle = idle;
}

uint8_t link_connect(void)
{
    char msg[80];
    uint8_t rc;

    if (!cogs_slot) {
        return COGS_E_NOCARD;
    }
    /* A retry by itself does not stop for 20 s to wait for a network. */
    if (link_waiting ? !(cogs_status() & COGS_ST_LINK_UP) : !wait_for_network()) {
        rc = COGS_E_NOT_JOINED;
    } else {
        fmt_str(fmt_str(msg, "SAYING HELLO TO "), ses_base + 7);
        status_msg(msg);
        link_begin();
        rc = ses_hello();
        net_check();                /* the status bar's address, fresh */
    }
    if (rc) {
        link_live = 0;
        msgs_mode(MSGS_NONE);
        if (link_trouble(rc) != LT_OFFLINE) {
            /* Passing (no network, the gateway not up yet): keep trying. */
            link_waiting = 1;
            fmt_str(fmt_str(link_err, ses_strerror(rc)), ": TRYING AGAIN");
        }
        return rc;
    }
    link_live = 1;
    link_waiting = 0;
    link_badkey = 0;
    link_fails = 0;
    link_err[0] = 0;
    fmt_str(link_sec, SEC_SIGNED);
    msgs_mode(MSGS_LIVE);
    return 0;
}

static void open_locked(void);
static void after_prefs(uint8_t act, uint8_t locked);

/* Whether the card's key is locked with a passphrase and not yet
 * unlocked: then the passphrase comes before connecting. */
static uint8_t card_locked(void)
{
    static uint8_t scrambled[A2_KEY_LEN];
    uint8_t rc = (uint8_t)(!ks_have && !lock_armed && ks_load(scrambled) == KS_LOCKED);

    memset(scrambled, 0, sizeof scrambled);
    return rc;
}

/* The gateway refused the key itself: it does not know it, or the
 * signature did not check. */
static uint8_t refused(uint8_t rc)
{
    return (uint8_t)(rc == SES_E_REFUSED &&
                     (ses_reply.code == A2_UNKNOWN_DEVICE || ses_reply.code == A2_BAD_SIGNATURE));
}

/* Connect to the gateway in use. Without one, add one; without a key
 * for it (no master on the card yet), or while the gateway does not
 * know it, show it to be added there: until it is right, or cancelled. */
static void connect_or_setup(void)
{
    const char *why = "The gateway does not know this Apple's key yet: add it there.";

    if (!cogs_slot) {
        offline("NO COGS CARD", "No CoGS card was found in any slot.");
        return;
    }
    for (;;) {
        if (!cfg_name[0]) {
            if (!setup_run(0, 0)) {
                offline("NOT SET UP", "No gateway yet: " OA "-K, then Add a gateway.");
                return;
            }
            continue;
        }
        if (card_locked()) {
            open_locked();
            return;
        }
        if (lock_use()) {
            /* No master on the card: adding the key makes one. */
            if (!setup_run(1, "This Apple needs its key on the gateway.")) {
                offline("NO KEY", "This Apple's key is not added on the gateway: " OA "-R.");
                return;
            }
            continue;
        }
        if (!refused(link_connect())) {
            return;
        }
        if (!setup_run(1, why)) {
            return;
        }
        why = "The gateway still does not know this key: is it added there?";
    }
}

/* The card's key is locked with a passphrase: ask for it, until the
 * gateway knows the key it makes, or Esc goes to Settings. */
static void open_locked(void)
{
    const char *why = 0;
    uint8_t rc;

    for (;;) {
        if (!lock_open(why)) {
            after_prefs(prefs_run("Locked: a new key, without the passphrase, is Start over below."), 1);
            return;
        }
        rc = link_connect();
        if (rc != SES_E_REFUSED || ses_reply.code != A2_UNKNOWN_DEVICE) {
            return;
        }
        why = "The gateway does not know the key that passphrase makes: is it right?";
    }
}

/* After Settings: what it asked for. */
static void after_prefs(uint8_t act, uint8_t locked)
{
    if (act == PREFS_KEY) {
        setup_run(1, 0);
    }
    if (act == PREFS_USE || act == PREFS_KEY || (act == PREFS_ADD && setup_run(0, 0))) {
        connect_or_setup();
    } else if (act == PREFS_GONE) {
        ses_forget();
        memset(ses_key, 0, A2_KEY_LEN);
        offline("NO KEY", "The key is gone from the card: " OA "-R makes a new one, to add there.");
    } else if (locked) {
        offline("LOCKED", "The key on the card is locked with a passphrase: " OA "-K for settings.");
    }
}

void link_start(void)
{
    uint8_t rc;

    /* The disk before any network use; then the card (with none, both
     * come to nothing, and connect_or_setup says so). */
    cfg_load();
    rc = ks_load(ks_secret);
    if (rc) {
        ks_forget();                            /* locked: lock_open reads it again */
    } else {
        ks_have = 1;
    }
    connect_or_setup();
}

void link_setup(void)
{
    after_prefs(prefs_run(0), 0);
}

void link_again(void)
{
    /* With no key in memory (locked, or none on the card), a hello would
     * only count as a bad try at the gateway. */
    if (!ks_have || refused(link_connect())) {
        connect_or_setup();
    }
}
