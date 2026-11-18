/*
 * TWOEGRAM.CFG: the gateway, a ProDOS text file beside the program. Its
 * key name (docs/RELAY.md, part 1), from which this Apple's key for it
 * is made, then its address:
 *
 *     # Twoegram: the gateway
 *     GATEWAY=claw1 http://192.168.1.20:8417
 *
 * The form before key names, GATEWAY=http://192.168.1.20:8417, still
 * reads, as a gateway with no name. Read once at startup and written
 * only by the set-up screen, never while a request is in flight (the
 * card's disk traffic shares the FIFO). No key: that is on the card
 * (keystore.h).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "port.h"

#define CFG_FILE    "TWOEGRAM.CFG"
#define CFG_NAME_MAX 16

/* The key name of the gateway in use; empty if it has none. */
extern char cfg_name[CFG_NAME_MAX + 1];

/* Whether s, of n characters, can be a key name: 1 to 16 of a-z 0-9 - _. */
uint8_t cfg_name_ok(const char *s, uint8_t n);

/* Parse the file's text into ses_base and cfg_name, from its first
 * GATEWAY line: 1 if there was one. */
uint8_t cfg_parse(char *text);

/* Normalise what was typed into ses_base: add http:// and the default
 * port when missing. 0 if it cannot be an address. */
#define CFG_PORT    "8417"
uint8_t cfg_set_gateway(const char *typed);

/* Read the file: 1 if it named a gateway. */
uint8_t cfg_load(void);

/* Write ses_base and cfg_name as the file: 0, or the ProDOS error ($2B:
 * write-protected). In the set-up overlay (setup.c), its only user. */
uint8_t cfg_save(void);

#endif
