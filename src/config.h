/*
 * TWOEGRAM.CFG: the gateway's address, a ProDOS text file beside the
 * program. Read once at startup and written only after the set-up screen,
 * never while a request is in flight (the card's disk traffic shares the
 * FIFO). No key: that is on the card (keystore.h).
 *
 *     # Twoegram: the gateway's address
 *     GATEWAY=http://192.168.1.20:8417
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "port.h"

#define CFG_FILE    "TWOEGRAM.CFG"

/* Parse the file's text into ses_base: 1 if it named a gateway. */
uint8_t cfg_parse(char *text);

/* Normalise what was typed into ses_base: add http:// and the default
 * port when missing. 0 if it cannot be an address. */
#define CFG_PORT    "8417"
uint8_t cfg_set_gateway(const char *typed);

/* Read the file: 1 if it named a gateway. */
uint8_t cfg_load(void);

/* Write the file: 0, or the ProDOS error ($2B: write-protected). */
uint8_t cfg_save(void);

#endif
