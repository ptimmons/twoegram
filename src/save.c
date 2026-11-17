/*
 * Saving the conversation to a text file. See save.h.
 *
 * The second overlay: code and text from TWOEGRAM.SAVE, loaded at $0800
 * while it runs (overlay.h). Its few variables are in main memory.
 */
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "screen.h"
#include "kbd.h"
#include "fmt.h"
#include "ui.h"
#include "store.h"
#include "history.h"
#include "link.h"
#include "save.h"

#ifdef __CC65__
#include <apple2.h>                 /* _filetype, _auxtype */
#include <errno.h>                  /* _oserror */
void lc_ram(void);                  /* blit.s */
#pragma code-name ("OVERLAY2")
#pragma rodata-name ("OVERLAY2")
#else
#define lc_ram()
#endif

#define CHUNK       128             /* text copied out of aux at a time */
#define HINT        "   RETURN: SAVE   ESC: CANCEL"

/* ProDOS errors said in words. */
#define E_IO        0x27
#define E_WPROT     0x2B
#define E_BADPATH   0x40
#define E_NOPATH    0x44
#define E_NOVOL     0x45
#define E_FULL      0x48
#define E_DIRFULL   0x49

/* Its working variables load with it, to spare main memory; only the
 * note outlives it. */
#ifdef __CC65__
#pragma bss-name (push, "OVERLAY2")
#endif
static char     name[SAVE_NAME_MAX + 1];
static uint8_t  nn;
static char     chunk[CHUNK];
static int      fd;
static uint8_t  failed;             /* 0, or the first ProDOS error */
static uint16_t saved;              /* messages written */
#ifdef __CC65__
#pragma bss-name (pop)
#endif

char save_note[80];

/* The error behind a failed call; $FF if ProDOS gave none. */
static uint8_t os_err(void)
{
#ifdef __CC65__
    return _oserror ? _oserror : 0xFF;
#else
    return 0xFF;
#endif
}

static void put(const char *s, uint16_t n)
{
    if (!failed && n && write(fd, s, n) != (int)n) {
        failed = os_err();
    }
}

static void put_s(const char *s)
{
    put(s, (uint16_t)strlen(s));
}

void save_default(const char *label, char *out)
{
    uint8_t n = 0;
    char c;

    for (; *label && n < 11; ++label) {
        c = *label;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if ((c >= 'A' && c <= 'Z') || (n && c >= '0' && c <= '9')) {
            out[n++] = c;
        }
    }
    if (!n) {
        memcpy(out, "TWOEGRAM", 8);
        n = 8;
    }
    memcpy(out + n, ".TXT", 5);
}

/* The status bar as a one-line field: the prompt, the name, the cursor. */
static void draw_prompt(void)
{
    uint8_t x;

    lb_clear(SC_ISPACE);
    x = lb_text(1, "SAVE AS: ", 1);
    x = lb_textn(x, name, nn, 1);
    linebuf[x] = SC_SPACE;          /* the cursor, normal in the inverse bar */
    lb_text((uint8_t)(x + 1), HINT, 1);
    scr_blit(STATUS_ROW);
}

/* The name, typed or kept: 0 if Esc. Uppercase, as ProDOS 8 wants. */
static uint8_t ask_name(void)
{
    uint8_t k;

    for (;;) {
        draw_prompt();
        k = key_get();
        if (key_oa) {
            continue;
        }
        if (k == K_ESC) {
            return 0;
        }
        if (k == K_RETURN && nn) {
            name[nn] = 0;
            return 1;
        }
        if ((k == K_DELETE || k == K_LEFT) && nn) {
            --nn;
        } else if (k > ' ' && k < 0x7F && nn < SAVE_NAME_MAX) {
            if (k >= 'a' && k <= 'z') {
                k = (uint8_t)(k - 32);
            }
            name[nn++] = (char)k;
        }
    }
}

static uint8_t exists(void)
{
    int f = open(name, O_RDONLY);

    if (f >= 0) {
        close(f);
    }
    lc_ram();
    return (uint8_t)(f >= 0);
}

/* Y, or anything else for no. */
static uint8_t replace_ok(void)
{
    uint8_t k;

    fmt_str(fmt_str(fmt_str(save_note, "REPLACE "), name), "?   Y: YES   N: NO");
    status_msg(save_note);
    do {
        k = key_get();
    } while (key_oa);
    return (uint8_t)(k == 'y' || k == 'Y');
}

/* "<what>: THE FILE HAS THE FIRST <n> MESSAGES", or "<what>" if none.
 * At most 42 + 35 characters: the bar's 80, without the name. */
static void partial(const char *what)
{
    char *p = fmt_str(save_note, what);

    if (saved) {
        p = fmt_str(p, ": THE FILE HAS THE FIRST ");
        fmt_str(fmt_u32(p, saved), saved == 1 ? " MESSAGE" : " MESSAGES");
    }
}

/* A ProDOS error, said. */
static void disk_note(uint8_t rc)
{
    switch (rc) {
    case E_WPROT:
        partial("THE DISK IS WRITE-PROTECTED");
        break;
    case E_IO:
        partial("THE DISK FAILED (I/O ERROR $27): TRY AGAIN");
        break;
    case E_FULL: case E_DIRFULL:
        partial("THE DISK IS FULL");
        break;
    case E_BADPATH:
        fmt_str(save_note, "NOT A PRODOS NAME: LETTERS, DIGITS AND PERIODS, A LETTER FIRST");
        break;
    case E_NOPATH: case E_NOVOL:
        fmt_str(save_note, "NO SUCH DISK OR FOLDER: TYPE /DISK/NAME, OR JUST A NAME");
        break;
    default:
        fmt_hex8(fmt_str(chunk, "COULD NOT SAVE, PRODOS ERROR $"), rc);
        partial(chunk);
        break;
    }
}

/* Message i of the store: its time and who, then its text, a line for
 * each '\n', the check mark as '+', and a blank line after. */
static void put_msg(uint8_t i, const char *label)
{
    uint16_t off, n;
    uint8_t j;

    put(st[i].time, (uint16_t)strlen(st[i].time));
    put(" ", 1);
    put_s(st[i].mine ? (const char *)"You" : label);
    put("\r", 1);
    for (off = 0; off < st[i].len; off = (uint16_t)(off + n)) {
        n = (uint16_t)(st[i].len - off);
        if (n > CHUNK) {
            n = CHUNK;
        }
        st_text(i, off, n, chunk);
        for (j = 0; j < (uint8_t)n; ++j) {
            if (chunk[j] == '\n') {
                chunk[j] = '\r';
            } else if (chunk[j] == GL_CHECK) {
                chunk[j] = '+';
            }
        }
        put(chunk, n);
    }
    put("\r\r", 2);
}

/* The status bar while it works. */
static void progress(void)
{
    char *p = fmt_str(save_note, "SAVING: ");

    p = fmt_u32(p, saved);
    fmt_str(p, " MESSAGES SO FAR   ESC: STOP");
    status_msg(save_note);
}

uint8_t save_main(const char *agent, const char *label)
{
    uint16_t last = 0;
    uint8_t i, rc, stopped = 0;

    save_note[0] = 0;
    saved = 0;
    save_default(label, name);
    nn = (uint8_t)strlen(name);
    if (!ask_name()) {
        fmt_str(save_note, "NOT SAVED");
        return 0;
    }
    if (exists() && !replace_ok()) {
        fmt_str(save_note, "NOT SAVED");
        return 0;
    }

    /* The start first: an empty conversation makes no file. */
    status_msg("SAVING: FETCHING THE START");
    link_begin();
    rc = hist_load(agent, 1);
    if (rc) {
        fmt_str(save_note, "NOT SAVED: THE GATEWAY DID NOT GIVE THE CONVERSATION");
        return rc;
    }
    if (!st_count) {
        fmt_str(fmt_str(save_note, "NOTHING TO SAVE: NO MESSAGES WITH "), label);
        return 0;
    }

#ifdef __CC65__
    _filetype = 0x04;               /* TXT */
    _auxtype = 0;
#endif
    failed = 0;
    fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        disk_note(os_err());
        lc_ram();
        return 0;
    }
    put_s("Twoegram: the conversation with ");
    put_s(label);
    put("\r\r", 2);

    /* Each page as it comes: what is new in the store since the last,
     * by the gateway's numbers (the store drops the oldest as it fills,
     * and those are written already). */
    for (;;) {
        for (i = 0; i < st_count && !failed; ++i) {
            if (st[i].n > last) {
                put_msg(i, label);
                last = st[i].n;
                ++saved;
            }
        }
        if (failed || st_newest) {
            break;
        }
        if (key_ready() && key_get() == K_ESC) {
            stopped = 1;
            break;
        }
        progress();
        link_begin();
        rc = hist_newer(agent);
        if (rc || !hist_added) {
            break;                  /* a failure; or nothing newer after all */
        }
    }

    if (close(fd) && !failed) {
        failed = os_err();
    }
    lc_ram();
    if (failed) {
        disk_note(failed);
    } else if (rc) {
        partial("THE GATEWAY STOPPED ANSWERING");
    } else if (stopped) {
        partial("SAVING STOPPED");
    } else {
        fmt_str(fmt_u32(fmt_str(save_note, "SAVED "), saved),
                saved == 1 ? " MESSAGE AS " : " MESSAGES AS ");
        fmt_str(save_note + strlen(save_note), name);
    }
    return rc;
}
