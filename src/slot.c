/*
 * Finding the card. Apple only: it reads slot ROM and I/O space directly.
 */
#include "cogs.h"

/* A Disk II controller's boot ROM begins $A2 $20 $A0 $00 $A2 $03 $86 $3C
 * and ends with $00. Its $C0nX switches step the head and run the motor,
 * so not even the read-only CoGS ID test may touch that slot. */
uint8_t slot_is_disk2(uint8_t s)
{
    const volatile uint8_t *rom = (const volatile uint8_t *)(0xC000 | ((uint16_t)s << 8));

    return (uint8_t)(rom[1] == 0x20 && rom[3] == 0x00 && rom[5] == 0x03 &&
                     rom[0xFF] == 0x00);
}

uint8_t cogs_find(void)
{
    uint8_t s;

    for (s = 7; s >= 1; --s) {
        if (!slot_is_disk2(s) && cogs_probe(s)) {
            return s;
        }
    }
    return 0;
}
