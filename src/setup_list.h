/*
 * What the two set-up overlays share: adding a gateway (setup.c) and
 * Settings (prefs.c). The saved gateways (TWOEGRAM.CFG, config.h), and
 * a few pieces of the screen.
 *
 * Included by each of them, after its segment pragmas, so that its code
 * and data land in that overlay: one overlay cannot call into the other,
 * which is not loaded while it runs. So everything here is static, and
 * what only Settings uses is left out of the other (SETUP_LIST_PREFS). Its
 * constants are at file scope: with code and constants in one segment,
 * a function's own static const table would sit at the function's entry
 * and run as code (tools/cc65check.py).
 */
#ifndef SETUP_LIST_H
#define SETUP_LIST_H

#define LABEL_COL   2
#define KEY_COL     14
#define WHY_ROW     22

/* The saved gateways, the one in use first. */
typedef struct {
    char name[CFG_NAME_MAX + 1];
    char addr[SES_BASE_MAX + 1];        /* http://host:port */
} gw_t;

static gw_t    gws[CFG_GATEWAYS];
static uint8_t ngw;
/* The file's text; also scratch for moving an entry. */
static char    ftext[CFG_GATEWAYS * (8 + CFG_NAME_MAX + 1 + SES_BASE_MAX + 1) + 48];

static char    line[96];                /* scratch: never kept across calls */
static uint8_t card;                    /* the card: 0 a master, KS_LOCKED, KS_NONE */
static uint8_t warned;

/* ---- the screen ---- */

static void say(uint8_t row, const char *s, uint8_t inverse)
{
    lb_clear(SC_SPACE);
    if (s) {
        lb_text(LABEL_COL, s, inverse);
    }
    scr_blit(row);
}

static void warn(uint8_t row, const char *s)
{
    say(row, s, 1);
    warned = 1;
}

#ifdef SETUP_LIST_PREFS
/* A question on the status bar: 1 for Y. */
static uint8_t yes(const char *q)
{
    uint8_t c;

    status_msg(q);
    c = key_get();
    return (uint8_t)(c == 'y' || c == 'Y');
}

#endif

static void card_trouble(uint8_t rc)
{
#ifndef SETUP_LIST_PREFS
    /* Only when a master is first kept: Settings changes one already
     * there. */
    if (rc == KS_NOROOM) {
        warn(WHY_ROW - 1, "The card cannot keep the key yet; it will be made again.");
    } else if (rc == KS_INUSE) {
        warn(WHY_ROW - 1, "The card's spare memory is in use: the key is not kept there.");
    } else
#endif
    if (rc) {
        fmt_str(fmt_str(line, "The key could not be kept on the card: "), cogs_strerror(rc));
        warn(WHY_ROW - 1, line);
    }
}

static void disk_trouble(uint8_t rc)
{
    if (rc == 0x2B) {
        warn(WHY_ROW - 2, "The disk is write-protected: the gateways are not saved.");
    } else if (rc) {
        fmt_hex8(fmt_str(line, "The gateways could not be saved: ProDOS error $"), rc);
        warn(WHY_ROW - 2, line);
    }
}

/* ---- the list ---- */

static int8_t gl_find(const char *name)
{
    uint8_t i;

    for (i = 0; i < ngw; ++i) {
        if (!strcmp(gws[i].name, name)) {
            return (int8_t)i;
        }
    }
    return -1;
}

static void gl_drop(uint8_t i)
{
    --ngw;
    memmove(&gws[i], &gws[i + 1], (ngw - i) * sizeof(gw_t));
}

/* name, at addr, first: moved there if it was saved, else added, the
 * last one dropped if the list was full. 1 if one was dropped. */
static uint8_t gl_put(const char *name, const char *addr)
{
    int8_t i = gl_find(name);
    uint8_t full = 0;

    if (i >= 0) {
        gl_drop((uint8_t)i);
    } else if (ngw == CFG_GATEWAYS) {
        --ngw;
        full = 1;
    }
    memmove(&gws[1], &gws[0], ngw * sizeof(gw_t));
    ++ngw;
    fmt_str(gws[0].name, name);
    fmt_str(gws[0].addr, addr);
    return full;
}

#ifdef SETUP_LIST_PREFS
static void gl_front(uint8_t i)
{
    memcpy(ftext, &gws[i], sizeof(gw_t));
    memmove(&gws[1], &gws[0], i * sizeof(gw_t));
    memcpy(&gws[0], ftext, sizeof(gw_t));
}

/* The first is the one in use: cfg_name and ses_base from it. */
static void gl_apply(void)
{
    if (ngw) {
        fmt_str(cfg_name, gws[0].name);
        fmt_str(ses_base, gws[0].addr);
    } else {
        cfg_name[0] = 0;
    }
}

#endif

/* The file's gateways, with names (one without, from before key names,
 * is only an address for set-up to start from). Then the one in use,
 * first, in case the file could not be written last time. */
static void gl_load(void)
{
    char *s, *e, *v, *sp;

    ngw = 0;
    fmt_str(line, ses_base);            /* cfg_set_gateway writes it */
    if (cfg_read(ftext, sizeof ftext)) {
        for (s = ftext; *s; s = e) {
            for (e = s; *e && *e != '\r' && *e != '\n'; ++e) {
            }
            if (*e) {
                *e++ = 0;
            }
            if (strncmp(s, "GATEWAY=", 8)) {
                continue;
            }
            v = s + 8;
            sp = strchr(v, ' ');
            if (!sp || !cfg_name_ok(v, (uint8_t)(sp - v))) {
                continue;
            }
            *sp = 0;
            if (ngw < CFG_GATEWAYS && gl_find(v) < 0 && cfg_set_gateway(sp + 1)) {
                fmt_str(gws[ngw].name, v);
                fmt_str(gws[ngw].addr, ses_base);
                ++ngw;
            }
        }
    }
    fmt_str(ses_base, line);
    if (cfg_name[0]) {
        gl_put(cfg_name, ses_base);
    }
}

/* The list as the file: 0, or the ProDOS error. */
static uint8_t gl_save(void)
{
    char *p = fmt_str(ftext, "# Twoegram: the gateways, the one in use first\r");
    uint8_t i;

    for (i = 0; i < ngw; ++i) {
        p = fmt_str(fmt_str(fmt_str(fmt_str(fmt_str(p, "GATEWAY="), gws[i].name), " "),
                            gws[i].addr), "\r");
    }
    return cfg_write(ftext, (uint16_t)(p - ftext));
}

/* ---- the key ---- */

/* What the card holds, and its master into memory if it is there plain. */
static void take_card(void)
{
    card = ks_load((uint8_t *)line);
    if (card && card != KS_LOCKED) {
        card = KS_NONE;
    }
    if (!card && !ks_have) {
        memcpy(ks_secret, line, A2_KEY_LEN);
        ks_have = 1;
    }
    memset(line, 0, sizeof line);
}

#endif
