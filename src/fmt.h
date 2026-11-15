/*
 * Formatting numbers, addresses and dates into a char buffer, without
 * printf. Each function writes at p, NUL-terminates, and returns a pointer
 * to the NUL, so calls chain:
 *
 *     p = fmt_str(buf, "IP ");
 *     p = fmt_ip(p, ip);
 */
#ifndef FMT_H
#define FMT_H

#include "port.h"

/* Seconds from 1904-01-01 to 1970-01-01: Unix time plus this is the
 * 1904-based time LOCALTIME uses. */
#define SECS_1904_TO_1970   2082844800UL

char *fmt_str(char *p, const char *s);
char *fmt_u32(char *p, uint32_t v);
char *fmt_ip(char *p, const uint8_t ip[4]);
char *fmt_hex8(char *p, uint8_t v);         /* two digits, no '$' */

/* fmt_extra.c: only the probe links these. */
char *fmt_i8(char *p, int8_t v);
char *fmt_dur(char *p, uint32_t secs);      /* "3d 4h 12m", "4h 12m", "12m 5s" */

/* "YYYY-MM-DD HH:MM:SS" from seconds since 1904-01-01 00:00. Every fourth
 * year is a leap year, which holds from 1904 to 2099. */
char *fmt_date1904(char *p, uint32_t secs);

#endif
