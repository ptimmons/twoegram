/*
 * What lets the same C build for the Apple (cc65) and the Mac (host tests).
 */
#ifndef PORT_H
#define PORT_H

#include <stdint.h>

#ifndef __CC65__
#define __fastcall__
#endif

#endif
