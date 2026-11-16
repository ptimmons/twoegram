/*
 * Getting to the gateway. See link.h.
 */
#include <string.h>
#include "cogs.h"
#include "tick.h"
#include "kbd.h"
#include "fmt.h"
#include "session.h"
#include "keystore.h"
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
    msgs_mode(MSGS_NONE);
    fmt_str(link_sec, sec);
    fmt_str(link_err, err);
}

uint8_t link_trouble(uint8_t rc)
{
    uint8_t code = rc == SES_E_REFUSED ? ses_reply.code : A2_OK;

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
    link_fails = 0;
    link_err[0] = 0;
    fmt_str(link_sec, SEC_SIGNED);
    msgs_mode(MSGS_LIVE);
    return 0;
}

/* The gateway said no to this key: set-up again, until it is right or
 * cancelled. */
static void connect_or_setup(void)
{
    uint8_t rc;

    for (;;) {
        rc = link_connect();
        if (rc != SES_E_REFUSED ||
            (ses_reply.code != A2_UNKNOWN_DEVICE && ses_reply.code != A2_BAD_SIGNATURE)) {
            return;
        }
        if (!setup_run("The gateway does not know this key. Pair this Apple on the gateway, then type its key.")) {
            return;
        }
    }
}

void link_start(void)
{
    uint8_t have_gw, have_key;

    if (!cogs_slot) {
        offline("NO COGS CARD", "No CoGS card was found in any slot.");
        return;
    }
    /* The disk before any network use; then the card. */
    have_gw = cfg_load();
    have_key = (uint8_t)!ks_load(ses_key);
    if (!have_gw || !have_key) {
        if (!setup_run(have_gw ? "The card holds no key for Twoegram yet." : 0)) {
            offline("NOT SET UP", "Not set up yet.");
            return;
        }
    }
    connect_or_setup();
}

void link_setup(void)
{
    if (setup_run(0)) {
        connect_or_setup();
    }
}
