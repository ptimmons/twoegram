/*
 * Formatters only the probe uses: signed numbers, durations and dates.
 * Kept out of fmt.c so the app does not carry them.
 */
#include "fmt.h"

static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

char *fmt_i8(char *p, int8_t v)
{
    if (v < 0) {
        *p++ = '-';
        return fmt_u32(p, (uint32_t)(uint8_t)(-(int16_t)v));
    }
    return fmt_u32(p, (uint32_t)(uint8_t)v);
}

/* Two digits, zero padded. */
static char *two(char *p, uint8_t v)
{
    *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    *p = 0;
    return p;
}

char *fmt_dur(char *p, uint32_t secs)
{
    uint16_t days = (uint16_t)(secs / 86400UL);
    uint32_t rem = secs % 86400UL;
    uint8_t h = (uint8_t)(rem / 3600);
    uint8_t m = (uint8_t)((rem % 3600) / 60);
    uint8_t s = (uint8_t)(rem % 60);

    if (days) {
        p = fmt_u32(p, days);
        p = fmt_str(p, "d ");
    }
    if (days || h) {
        p = fmt_u32(p, h);
        p = fmt_str(p, "h ");
        p = fmt_u32(p, m);
        return fmt_str(p, "m");
    }
    p = fmt_u32(p, m);
    p = fmt_str(p, "m ");
    p = fmt_u32(p, s);
    return fmt_str(p, "s");
}

char *fmt_date1904(char *p, uint32_t secs)
{
    uint16_t days = (uint16_t)(secs / 86400UL);
    uint32_t rem = secs % 86400UL;
    uint16_t year = 1904, ylen;
    uint8_t month = 0, mlen, leap;

    for (;;) {
        leap = (uint8_t)((year & 3) == 0);
        ylen = leap ? 366 : 365;
        if (days < ylen) {
            break;
        }
        days -= ylen;
        ++year;
    }
    for (;;) {
        mlen = mdays[month];
        if (month == 1 && leap) {
            ++mlen;
        }
        if (days < mlen) {
            break;
        }
        days -= mlen;
        ++month;
    }

    p = fmt_u32(p, year);
    *p++ = '-';
    p = two(p, (uint8_t)(month + 1));
    *p++ = '-';
    p = two(p, (uint8_t)(days + 1));
    *p++ = ' ';
    p = two(p, (uint8_t)(rem / 3600));
    *p++ = ':';
    p = two(p, (uint8_t)((rem % 3600) / 60));
    *p++ = ':';
    return two(p, (uint8_t)(rem % 60));
}
