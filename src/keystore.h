/*
 * Where the device key lives: the CoGS card's prefs blob, bytes 15-31,
 * which the card keeps in its flash for host programs (reference 4.2.5;
 * milestone 2 found them free on the user's card). Byte 15 marks the
 * blob as Twoegram's, bytes 16-31 are its secret: the master the Apple
 * made itself (KS_MASTER), from which each gateway's key is derived
 * (docs/RELAY.md, part 1). It may be scrambled with a passphrase
 * (lock.h), which nothing stores. A key made by a gateway (KS_OLD, from
 * before b048) is no longer used: it loads as no key, and the next
 * master is written over it. Never on a disk: a network disk is an image on a web server.
 *
 * Bytes 0-14 are the card's own settings (timezone, clock sync, TLS,
 * equaliser) and are always written back exactly as read.
 */
#ifndef KEYSTORE_H
#define KEYSTORE_H

#include "proto.h"

#define KS_OLD      0xA2            /* byte 15: a gateway's key (and $A3, locked): unused */
#define KS_MASTER   0xA4            /* the master the Apple made */
#define KS_MASTER_PASS 0xA5         /* ... scrambled with a passphrase */

#define KS_NONE     0xD0            /* no key stored */
#define KS_NOROOM   0xD1            /* the card has none of its own settings stored yet */
#define KS_INUSE    0xD2            /* bytes 15-31 hold something else's data */
#define KS_LOCKED   0xD3            /* the key is scrambled: a passphrase is needed */
#define KS_NONAME   0xD4            /* the gateway has no key name */

/* The card's master in memory, as ks_load found it and lock.c unlocked
 * it: ks_have says it is there. */
extern uint8_t ks_secret[A2_KEY_LEN];
extern uint8_t ks_have;

/* Wipe ks_secret (at the lock, and at quit). */
void ks_forget(void);

/* The mark to save ks_secret under, plain or with a passphrase. */
#define KS_MARK_FOR(pass) ((uint8_t)((pass) ? KS_MASTER_PASS : KS_MASTER))

/* 0 with the master; KS_LOCKED with it scrambled, to be unscrambled
 * with the passphrase; KS_NONE (also for a gateway's old key); or a
 * card code. */
uint8_t ks_load(uint8_t key[A2_KEY_LEN]);

/* The secret under mark (KS_MARK_FOR: already scrambled if it says
 * so). 0, KS_NOROOM, KS_INUSE, or a card code. Reads back what it
 * wrote. */
uint8_t ks_save(const uint8_t key[A2_KEY_LEN], uint8_t mark);

/* Scramble or unscramble key with a passphrase of n characters (an XOR
 * with a pad the card hashes from it, PASS_ROUNDS times over). 0 or a
 * card code. */
#define PASS_ROUNDS 64
uint8_t ks_scramble(uint8_t key[A2_KEY_LEN], const char *pass, uint8_t n);

/* The key for the gateway named name (1 to KS_NAME_MAX of a-z 0-9 - _,
 * checked by the caller) from the master secret: the first 16 bytes of
 * HMAC(master, "twoegram-gw" || name), docs/RELAY.md part 1. 0 or a card
 * code. key may be master itself. */
#define KS_NAME_MAX 16
uint8_t ks_derive(const uint8_t master[A2_KEY_LEN], const char *name, uint8_t key[A2_KEY_LEN]);

/* Remove the key, leaving the card's own settings as they were. */
uint8_t ks_clear(void);

#endif
