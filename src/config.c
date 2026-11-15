/*
 * TWOEGRAM.CFG. See config.h.
 */
#include <string.h>
#include "fmt.h"
#include "session.h"
#include "config.h"

uint8_t cfg_set_gateway(const char *s)
{
    static char t[SES_BASE_MAX + 1];
    const char *host;
    char *p;
    uint8_t n;

    while (*s == ' ') {
        ++s;
    }
    host = s;
    if (!strncmp(s, "http://", 7) || !strncmp(s, "HTTP://", 7)) {
        host = s + 7;
    } else if (strstr(s, "://")) {
        return 0;                   /* https and the rest: not on the LAN */
    }
    n = (uint8_t)strlen(host);
    while (n && (host[n - 1] == ' ' || host[n - 1] == '/')) {
        --n;
    }
    if (!n || n > SES_BASE_MAX - 7 - 5 || memchr(host, '/', n) || memchr(host, ' ', n)) {
        return 0;
    }
    p = fmt_str(t, "http://");
    memcpy(p, host, n);
    p[n] = 0;
    for (; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p + 32);    /* host names are not case-sensitive */
        }
    }
    if (!strchr(t + 7, ':')) {
        fmt_str(fmt_str(p, ":"), CFG_PORT);
    }
    fmt_str(ses_base, t);
    return 1;
}

uint8_t cfg_parse(char *text)
{
    char *s, *e, *v;
    uint8_t found = 0;

    for (s = text; *s; s = e) {
        for (e = s; *e && *e != '\r' && *e != '\n'; ++e) {
        }
        if (*e) {
            *e++ = 0;
        }
        if (*s == '#') {
            continue;
        }
        v = strchr(s, '=');
        if (v) {
            *v++ = 0;
            if (!strcmp(s, "GATEWAY") && cfg_set_gateway(v)) {
                found = 1;
            }
        }
    }
    return found;
}

#ifdef __CC65__

#include <apple2.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static char text[160];

uint8_t cfg_load(void)
{
    int fd, n;

    fd = open(CFG_FILE, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    n = read(fd, text, sizeof text - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    text[n] = 0;
    return cfg_parse(text);
}

uint8_t cfg_save(void)
{
    int fd, n;
    uint8_t tries, rc;
    char *p;

    p = fmt_str(text, "# Twoegram: the gateway's address\rGATEWAY=");
    p = fmt_str(fmt_str(p, ses_base), "\r");
    n = (int)(p - text);
    /* A block write to a CoGS network disk can fail with $27 (Newsiie's
     * milestone 8 notes): write the whole file again, up to three times. */
    for (tries = 0; tries < 3; ++tries) {
        _filetype = 0x04;           /* TXT */
        _auxtype = 0;
        fd = open(CFG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            rc = _oserror ? _oserror : 0xFF;
        } else {
            rc = write(fd, text, n) == n ? 0 : (_oserror ? _oserror : 0xFF);
            if (close(fd) && !rc) {
                rc = _oserror ? _oserror : 0xFF;
            }
        }
        if (rc != 0x27) {
            return rc;
        }
    }
    return rc;
}

#endif
