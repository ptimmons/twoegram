#include "fmt.h"

char *fmt_str(char *p, const char *s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = 0;
    return p;
}

char *fmt_u32(char *p, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0;

    do {
        tmp[n++] = (char)('0' + (uint8_t)(v % 10));
        v /= 10;
    } while (v);
    while (n) {
        *p++ = tmp[--n];
    }
    *p = 0;
    return p;
}

char *fmt_hex8(char *p, uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";

    *p++ = hex[v >> 4];
    *p++ = hex[v & 15];
    *p = 0;
    return p;
}

char *fmt_ip(char *p, const uint8_t ip[4])
{
    uint8_t i;

    for (i = 0; i < 4; ++i) {
        if (i) {
            *p++ = '.';
        }
        p = fmt_u32(p, ip[i]);
    }
    return p;
}

