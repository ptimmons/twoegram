/*
 * The passphrase and the lock.
 *
 * With a passphrase, the card holds the key scrambled with it
 * (KS_MARK_PASS, keystore.h). Twoegram asks for it at the start and
 * unscrambles the key in memory; the passphrase itself is never stored.
 * A wrong one makes a wrong key, which the gateway does not know.
 *
 * Open-Apple-L, or LOCK_IDLE_MIN minutes without a key, locks: the key
 * and session key are wiped from memory, and so are the messages; the
 * screen shows only the passphrase prompt. The passphrase is checked on
 * the Apple, against the device id the key made (no network), before the
 * key is used again.
 */
#ifndef LOCK_H
#define LOCK_H

#include "port.h"

#define PASS_MIN        8
#define PASS_MAX        32
#define LOCK_IDLE_MIN   10

/* The key in memory came from, or was saved with, a passphrase: only
 * then can the screen be locked. */
extern uint8_t lock_armed;

/* At the start, with the card's key scrambled: ask for the passphrase
 * and unscramble the key into ses_key. why, if not 0, is said above the
 * prompt. 1 when unscrambled; 0 if Esc was pressed (or the card could
 * not be read). */
uint8_t lock_open(const char *why);

/* Lock now; returns once the right passphrase is typed, with the key
 * back in ses_key and no session open. */
void lock_now(void);

#endif
