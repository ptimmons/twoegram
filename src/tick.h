/*
 * A coarse time base from the vertical blank: 60 frames a second on an
 * NTSC //e. There is no interrupt; callers poll tick_poll() in their wait
 * loops, and it reports each new frame once.
 */
#ifndef TICK_H
#define TICK_H

#include "port.h"

#define TICKS_PER_SEC   60

extern uint16_t ticks;              /* frames seen by tick_poll() */

void tick_reset(void);
uint8_t tick_poll(void);            /* 1 if a new frame began since the last call */

/* Spin until n frames pass, or a key is pressed (returns 1). If the
 * vertical-blank signal never changes (an emulator that does not model
 * it), a spin cap ends the wait anyway. */
uint8_t tick_wait(uint16_t n);

#endif
