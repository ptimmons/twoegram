/*
 * Keyboard. The //e has no type-ahead buffer: one latched key at $C000.
 */
#ifndef KBD_H
#define KBD_H

#include <stdint.h>

#define K_LEFT      0x08
#define K_TAB       0x09
#define K_DOWN      0x0A
#define K_UP        0x0B
#define K_RETURN    0x0D
#define K_RIGHT     0x15
#define K_ESC       0x1B
#define K_DELETE    0x7F

/* Set by key_get(): was Open Apple (or Solid Apple) held with the key? */
extern uint8_t key_oa;
extern uint8_t key_sa;

uint8_t key_get(void);      /* wait for a key; 7-bit ASCII */
uint8_t key_ready(void);    /* nonzero if a key is latched */

#endif
