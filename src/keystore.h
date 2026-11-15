/*
 * Where the device key lives: the CoGS card's prefs blob, bytes 15-31,
 * which the card keeps in its flash for host programs (reference 4.2.5;
 * milestone 2 found them free on the user's card). Byte 15 marks the
 * blob as Twoegram's, bytes 16-31 are the key. Never on a disk: a network
 * disk is an image on a web server.
 *
 * Bytes 0-14 are the card's own settings (timezone, clock sync, TLS,
 * equaliser) and are always written back exactly as read.
 */
#ifndef KEYSTORE_H
#define KEYSTORE_H

#include "proto.h"

#define KS_MARK     0xA2            /* byte 15: the key that follows is ours */

#define KS_NONE     0xD0            /* no key stored */
#define KS_NOROOM   0xD1            /* the card has none of its own settings stored yet */
#define KS_INUSE    0xD2            /* bytes 15-31 hold something else's data */

/* 0 with the key, KS_NONE, or a card code. */
uint8_t ks_load(uint8_t key[A2_KEY_LEN]);

/* 0, KS_NOROOM, KS_INUSE, or a card code. Reads back what it wrote. */
uint8_t ks_save(const uint8_t key[A2_KEY_LEN]);

/* Remove the key, leaving the card's own settings as they were. */
uint8_t ks_clear(void);

#endif
