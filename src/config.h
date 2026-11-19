/*
 * TWOEGRAM.CFG: the saved gateways, a ProDOS text file beside the
 * program. Each has its key name (docs/RELAY.md, part 1), from which
 * this Apple's key for it is made, then its address. The one in use is
 * first:
 *
 *     # Twoegram: the gateways, the one in use first
 *     GATEWAY=claw1 http://192.168.1.20:8417
 *     GATEWAY=mini http://192.168.1.30:8417
 *
 * Up to CFG_GATEWAYS of them. The form before key names,
 * GATEWAY=http://192.168.1.20:8417, still reads, as a gateway with no
 * name, which set-up then asks for. The program itself reads only the
 * first line, at startup; the whole list is the set-up overlays'
 * (setup_list.h). Written only by them, never while a request is in
 * flight (the card's disk traffic shares the FIFO). No key: that is on
 * the card (keystore.h).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "port.h"

#define CFG_FILE    "TWOEGRAM.CFG"
#define CFG_NAME_MAX 16
#define CFG_GATEWAYS 4

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

/* The whole file into buf, of cap bytes, ended with a 0: its length,
 * 0 if there is none. */
uint16_t cfg_read(char *buf, uint16_t cap);

/* Write n bytes of text as the whole file: 0, or the ProDOS error ($2B:
 * write-protected). */
uint8_t cfg_write(const char *text, uint16_t n);

#endif
