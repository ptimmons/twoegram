/*
 * The open conversation's messages, their text in auxiliary memory.
 *
 * A window of up to ST_MAX messages, in order of their numbers, always
 * contiguous in the gateway's numbering. History is fetched at either
 * end (history.c), so the window can slide: adding older messages when
 * it is full drops the newest, and adding newer ones drops the oldest.
 * st_older and st_newest say whether the gateway has more beyond each
 * end. The index is in main memory; each message's text is in aux
 * $0800-$BEFF, packed, and compacted when an addition does not fit.
 * Plain C over aux_put/aux_get, tested on the Mac with an array for aux.
 */
#ifndef STORE_H
#define STORE_H

#include "port.h"

#define ST_MAX      64
#define ST_TEXT_MAX 1500            /* the protocol's largest message */
#define ST_BASE     0x0800          /* aux memory the store owns */
#define ST_END      0xBF00          /* below $C000 (I/O), with a page to spare */

typedef struct {
    uint16_t n;                     /* the gateway's number for it */
    uint16_t off;                   /* where its text is in aux */
    uint16_t len;
    uint8_t  mine;
    char     time[6];               /* hh:mm */
} st_msg_t;

extern st_msg_t st[ST_MAX];
extern uint8_t  st_count;
extern uint8_t  st_older;           /* the gateway has messages before st[0] */
extern uint8_t  st_newest;          /* st[st_count - 1] is the newest it had */
extern int8_t   st_shift;           /* how far existing messages moved in st[]
                                       since st_shift was last zeroed */

/* Check for 128K and claim aux memory, disconnecting ProDOS's /RAM disk,
 * which lives there. 0, or a message saying why not. */
const char *st_init(void);

void st_reset(void);

/* Add message n. Returns 1 if it was added, 0 if it was already held or
 * does not join the window (it must be next to one end, or the window
 * empty). older says which end to drop from when full: adding older
 * messages drops the newest, and the other way round. */
uint8_t st_add(uint16_t n, uint8_t mine, const char *time,
               const char *text, uint16_t len, uint8_t older);

/* Copy len bytes of message i's text, from off, to dst. */
void st_text(uint8_t i, uint16_t off, uint16_t len, char *dst);

/* aux bytes not yet used (after compaction, more may be free) */
uint16_t st_free(void);

#endif
