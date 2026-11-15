#include "kbd.h"

#define KBD     (*(volatile uint8_t *)0xC000)
#define KBDSTRB (*(volatile uint8_t *)0xC010)
#define BUTN0   (*(volatile uint8_t *)0xC061)   /* Open Apple  */
#define BUTN1   (*(volatile uint8_t *)0xC062)   /* Solid Apple */

uint8_t key_oa;
uint8_t key_sa;

void key_flush(void)
{
    KBDSTRB = 0;
}

uint8_t key_ready(void)
{
    return (uint8_t)(KBD & 0x80);
}

uint8_t key_get(void)
{
    uint8_t k;

    while (!((k = KBD) & 0x80)) {
    }
    /* Sample the modifiers with the key, before they can be let go. */
    key_oa = (uint8_t)(BUTN0 & 0x80);
    key_sa = (uint8_t)(BUTN1 & 0x80);
    KBDSTRB = 0;
    return (uint8_t)(k & 0x7F);
}
