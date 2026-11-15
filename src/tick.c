#include "tick.h"
#include "kbd.h"

/* $C019 bit 7 changes at the start and end of each vertical blank. Its
 * sense is inverted between the //e and the IIgs, so count one particular
 * change rather than relying on which level means "in blank". */
#define RDVBLBAR    (*(volatile uint8_t *)0xC019)

/* About 20 cycles per spin at 1 MHz: 1500 spins is a little over a frame. */
#define SPINS_PER_FRAME 1500

uint16_t ticks;
static uint8_t last;

void tick_reset(void)
{
    ticks = 0;
    last = (uint8_t)(RDVBLBAR & 0x80);
}

uint8_t tick_poll(void)
{
    uint8_t v = (uint8_t)(RDVBLBAR & 0x80);

    if (v == last) {
        return 0;
    }
    last = v;
    if (v) {
        return 0;
    }
    ++ticks;
    return 1;
}

uint8_t tick_wait(uint16_t n)
{
    uint16_t start = ticks;
    uint32_t spins = (uint32_t)n * SPINS_PER_FRAME * 2;

    while ((uint16_t)(ticks - start) < n && spins--) {
        tick_poll();
        if (key_ready()) {
            return 1;
        }
    }
    return 0;
}
