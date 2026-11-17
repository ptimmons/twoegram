/*
 * Where the device key lives: the CoGS card's prefs blob, bytes 15-31,
 * which the card keeps in its flash for host programs (reference 4.2.5;
 * milestone 2 found them free on the user's card). Byte 15 marks the
 * blob as Twoegram's, bytes 16-31 are the key: as it is (KS_MARK), or
 * scrambled with a passphrase (KS_MARK_PASS, lock.h), which nothing
 * stores. Never on a disk: a network disk is an image on a web server.
 *
 * Bytes 0-14 are the card's own settings (timezone, clock sync, TLS,
 * equaliser) and are always written back exactly as read.
 */
#ifndef KEYSTORE_H
#define KEYSTORE_H

#include "proto.h"

#define KS_MARK     0xA2            /* byte 15: the key that follows is ours */
#define KS_MARK_PASS 0xA3           /* ... and scrambled with a passphrase */

#define KS_NONE     0xD0            /* no key stored */
#define KS_NOROOM   0xD1            /* the card has none of its own settings stored yet */
#define KS_INUSE    0xD2            /* bytes 15-31 hold something else's data */
#define KS_LOCKED   0xD3            /* the key is scrambled: a passphrase is needed */

/* 0 with the key; KS_LOCKED with the scrambled key, to be unscrambled
 * with the passphrase; KS_NONE; or a card code. */
uint8_t ks_load(uint8_t key[A2_KEY_LEN]);

/* The key under mark (KS_MARK, or KS_MARK_PASS for one already
 * scrambled). 0, KS_NOROOM, KS_INUSE, or a card code. Reads back what it
 * wrote. */
uint8_t ks_save(const uint8_t key[A2_KEY_LEN], uint8_t mark);

/* Scramble or unscramble key with a passphrase of n characters (an XOR
 * with a pad the card hashes from it, PASS_ROUNDS times over). 0 or a
 * card code. */
#define PASS_ROUNDS 64
uint8_t ks_scramble(uint8_t key[A2_KEY_LEN], const char *pass, uint8_t n);

/* Remove the key, leaving the card's own settings as they were. */
uint8_t ks_clear(void);

#endif
