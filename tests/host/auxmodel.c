/*
 * Auxiliary memory for the host tests: a 64K array, and a failure for any
 * access outside the store's region ($0800-$BEFF), which on the Apple
 * would reach ProDOS's buffers or I/O.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store.h"

static unsigned char aux[65536];
unsigned long aux_bytes_moved;

static void check(unsigned at, unsigned n)
{
    if (at < ST_BASE || at + n > ST_END) {
        printf("aux access out of bounds: $%04X, %u bytes\n", at, n);
        exit(3);
    }
}

void aux_init(void)
{
    memset(aux, 0xEE, sizeof aux);
}

void aux_put(uint16_t at, const void *src, uint16_t n)
{
    check(at, n);
    memcpy(aux + at, src, n);
    aux_bytes_moved += n;
}

void aux_get(void *dst, uint16_t at, uint16_t n)
{
    check(at, n);
    memcpy(dst, aux + at, n);
    aux_bytes_moved += n;
}
