/*
 * The open conversation's messages. See store.h.
 */
#include <string.h>
#include "store.h"

/* auxmem.s on the Apple; a plain array in the host tests. */
void aux_init(void);
void __fastcall__ aux_put(uint16_t aux_dst, const void *src, uint16_t n);
void __fastcall__ aux_get(void *dst, uint16_t aux_src, uint16_t n);

st_msg_t st[ST_MAX];
uint8_t  st_count;
uint8_t  st_older;
uint8_t  st_newest;
int8_t   st_shift;

static uint16_t next;               /* the first free aux byte */
static uint8_t  chunk[128];         /* for moving text in aux */

#ifdef __CC65__
/* ProDOS 8's global page. */
#define MACHID      (*(volatile uint8_t *)0xBF98)
#define DEVCNT      (*(volatile uint8_t *)0xBF31)
#define DEVLST      ((volatile uint8_t *)0xBF32)
#define DEVADR32    (*(volatile uint16_t *)0xBF26)  /* slot 3, drive 2 */
#define DEVADR01    (*(volatile uint16_t *)0xBF10)  /* slot 0: "no device" */
#define RAM_DRIVER  0xFF00                          /* ProDOS's /RAM */
#endif

const char *st_init(void)
{
#ifdef __CC65__
    uint8_t i, n;

    if ((MACHID & 0x30) != 0x30) {
        return "TWOEGRAM NEEDS 128K";
    }
    /* On a 128K machine ProDOS puts /RAM in the auxiliary memory the store
     * uses (Newsiie's milestone 4). Disconnect it: point slot 3 drive 2 at
     * the no-device routine and take its entry out of the device list.
     * Something other than /RAM there is left alone, and refused. */
    if (DEVADR32 != DEVADR01) {
        if (DEVADR32 != RAM_DRIVER) {
            return "AUX MEMORY IS IN USE BY A RAM DISK";
        }
        n = DEVCNT;                 /* the count less one */
        for (i = 0; i <= n; ++i) {
            if ((DEVLST[i] & 0xF0) == 0xB0) {
                for (; i < n; ++i) {
                    DEVLST[i] = DEVLST[i + 1];
                }
                DEVLST[n] = 0;
                DEVCNT = (uint8_t)(n - 1);
                break;
            }
        }
        DEVADR32 = DEVADR01;
    }
#endif
    aux_init();
    st_reset();
    return 0;
}

void st_reset(void)
{
    st_count = 0;
    st_older = 0;
    st_newest = 1;
    st_shift = 0;
    next = ST_BASE;
}

uint16_t st_free(void)
{
    return (uint16_t)(ST_END - next);
}

void st_text(uint8_t i, uint16_t off, uint16_t len, char *dst)
{
    if (len) {
        aux_get(dst, (uint16_t)(st[i].off + off), len);
    }
}

/* Drop the message at index i from the window. */
static void drop(uint8_t i)
{
    memmove(st + i, st + i + 1, (uint8_t)(st_count - i - 1) * sizeof st[0]);
    --st_count;
}

/* Move every message's text down to the start of aux, in order of where
 * it is, so moving one never overwrites another still to move. */
static void compact(void)
{
    uint16_t at = ST_BASE, src, dst, left, n;
    uint8_t i, best, done[ST_MAX];

    memset(done, 0, st_count);
    for (;;) {
        best = 0xFF;
        for (i = 0; i < st_count; ++i) {
            if (!done[i] && (best == 0xFF || st[i].off < st[best].off)) {
                best = i;
            }
        }
        if (best == 0xFF) {
            break;
        }
        done[best] = 1;
        src = st[best].off;
        dst = at;
        if (src != dst) {
            for (left = st[best].len; left; left = (uint16_t)(left - n)) {
                n = left > sizeof chunk ? (uint16_t)sizeof chunk : left;
                aux_get(chunk, src, n);
                aux_put(dst, chunk, n);
                src = (uint16_t)(src + n);
                dst = (uint16_t)(dst + n);
            }
            st[best].off = at;
        }
        at = (uint16_t)(at + st[best].len);
    }
    next = at;
}

/* Drop one message from the end that is not being added to. */
static void make_room(uint8_t older)
{
    if (older) {
        drop((uint8_t)(st_count - 1));
        st_newest = 0;
    } else {
        drop(0);
        st_older = 1;
        --st_shift;
    }
}

uint8_t st_add(uint16_t n, uint8_t mine, const char *time,
               const char *text, uint16_t len, uint8_t older)
{
    uint8_t i;

    if (len > ST_TEXT_MAX) {
        len = ST_TEXT_MAX;
    }
    if (st_count) {
        /* Only next to an end: the window stays contiguous. */
        if (n + 1 == st[0].n) {
            older = 1;
        } else if (n == st[st_count - 1].n + 1) {
            older = 0;
        } else {
            return 0;
        }
    }
    /* Room in the index, then in aux. */
    if (st_count >= ST_MAX) {
        make_room(older);
    }
    if (len > (uint16_t)(ST_END - next)) {
        compact();
        while (len > (uint16_t)(ST_END - next) && st_count) {
            make_room(older);
            compact();
        }
    }

    if (older && st_count) {
        memmove(st + 1, st, st_count * sizeof st[0]);
        i = 0;
        ++st_shift;
    } else {
        i = st_count;
    }
    ++st_count;
    st[i].n = n;
    st[i].mine = mine;
    st[i].len = len;
    st[i].off = next;
    strncpy(st[i].time, time, 5);
    st[i].time[5] = 0;
    if (len) {
        aux_put(next, text, len);
    }
    next = (uint16_t)(next + len);
    return 1;
}
