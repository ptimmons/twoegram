/*
 * The keyboard, with type-ahead. The //e latches one key at $C000; while
 * a request waits on the network, key_poll() (called from the wait's idle
 * hook) moves keys into a small ring so none is lost, and key_get()
 * takes from the ring first. See kbd.h.
 */
#include "kbd.h"

#define KBD     (*(volatile uint8_t *)0xC000)
#define KBDSTRB (*(volatile uint8_t *)0xC010)
#define BUTN0   (*(volatile uint8_t *)0xC061)   /* Open Apple  */
#define BUTN1   (*(volatile uint8_t *)0xC062)   /* Solid Apple */

#define TA_SIZE 32                              /* a power of two */

uint8_t key_oa;
uint8_t key_sa;

static uint8_t ta_key[TA_SIZE], ta_mod[TA_SIZE];
static uint8_t ta_head, ta_tail;

void key_poll(void)
{
    uint8_t k = KBD, next;

    if (!(k & 0x80)) {
        return;
    }
    next = (uint8_t)((ta_tail + 1) & (TA_SIZE - 1));
    if (next != ta_head) {          /* full: the key is dropped */
        ta_key[ta_tail] = (uint8_t)(k & 0x7F);
        ta_mod[ta_tail] = (uint8_t)((BUTN0 & 0x80) | ((BUTN1 & 0x80) >> 1));
        ta_tail = next;
    }
    KBDSTRB = 0;
}

void key_flush(void)
{
    ta_head = ta_tail;
    KBDSTRB = 0;
}

uint8_t key_ready(void)
{
    return (uint8_t)(ta_head != ta_tail || (KBD & 0x80));
}

uint8_t key_get(void)
{
    uint8_t k;

    if (ta_head != ta_tail) {
        k = ta_key[ta_head];
        key_oa = (uint8_t)(ta_mod[ta_head] & 0x80);
        key_sa = (uint8_t)(ta_mod[ta_head] & 0x40);
        ta_head = (uint8_t)((ta_head + 1) & (TA_SIZE - 1));
        return k;
    }
    while (!((k = KBD) & 0x80)) {
    }
    /* Sample the modifiers with the key, before they can be let go. */
    key_oa = (uint8_t)(BUTN0 & 0x80);
    key_sa = (uint8_t)(BUTN1 & 0x80);
    KBDSTRB = 0;
    return (uint8_t)(k & 0x7F);
}
