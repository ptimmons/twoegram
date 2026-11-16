/*
 * Fetching history. See history.h.
 */
#include <string.h>
#include "fmt.h"
#include "proto.h"
#include "session.h"
#include "store.h"
#include "history.h"

uint8_t  hist_added;
uint8_t  hist_busy;
uint16_t hist_latest;
uint8_t  hist_unseen;
uint8_t  hist_unread[SES_AGENTS_MAX];

/* "unread coder 2": the count for that agent. */
static void take_unread(const a2_item_t *item)
{
    uint8_t rest, i = ses_agent_at(item->val, item->vlen, &rest);

    if (i < SES_AGENTS_MAX && rest < item->vlen) {
        hist_unread[i] = (uint8_t)a2_num(item->val + rest, (uint8_t)(item->vlen - rest));
    }
}

static void note_latest(void)
{
    if (st_count && st_newest && st[st_count - 1].n > hist_latest) {
        hist_latest = st[st_count - 1].n;
    }
}

static char target[80];

/* Put the reply's messages into the store; returns the "more" flag. */
static uint8_t take(uint8_t older)
{
    a2_iter_t it;
    a2_item_t item;
    uint8_t more = 0, rc;

    a2_iter(&it, &ses_reply);
    while ((rc = a2_next(&it, &item)) == 1) {
        if (item.kind == A2_ITEM_MSG) {
            hist_added = (uint8_t)(hist_added +
                st_add(item.n, item.mine, item.time, item.text, item.tlen, older));
        } else if (a2_is(&item, "more") && item.vlen == 1) {
            more = (uint8_t)(item.val[0] == '1');
        }
    }
    return more;
}

/* hist and poll replies come oldest first; adding older messages one at
 * a time at the front needs them newest first, so a page of older
 * messages is added in two passes: the reply is walked once to find
 * them, then backwards through a small list of where they are. */
#define PAGE_MAX    40
static const char *ptext[PAGE_MAX];
static uint16_t    plen[PAGE_MAX], pn[PAGE_MAX];
static uint8_t     pmine[PAGE_MAX];
static char        ptime[PAGE_MAX][6];

static uint8_t take_older(void)
{
    a2_iter_t it;
    a2_item_t item;
    uint8_t more = 0, k = 0;

    a2_iter(&it, &ses_reply);
    while (a2_next(&it, &item) == 1) {
        if (item.kind == A2_ITEM_MSG && k < PAGE_MAX) {
            ptext[k] = item.text;
            plen[k] = item.tlen;
            pn[k] = item.n;
            pmine[k] = item.mine;
            memcpy(ptime[k], item.time, 6);
            ++k;
        } else if (a2_is(&item, "more") && item.vlen == 1) {
            more = (uint8_t)(item.val[0] == '1');
        }
    }
    while (k--) {
        hist_added = (uint8_t)(hist_added + st_add(pn[k], pmine[k], ptime[k], ptext[k], plen[k], 1));
    }
    return more;
}

static char *agent_param(char *p, const char *agent)
{
    return fmt_str(p, agent);       /* names are a-z 0-9 - _: nothing to escape */
}

uint8_t hist_load(const char *agent)
{
    char *p;
    uint8_t rc;

    hist_added = 0;
    st_reset();
    p = fmt_str(target, "/a2/hist?a=");
    p = agent_param(p, agent);
    fmt_u32(fmt_str(p, "&k="), HIST_PAGE);
    rc = ses_call("GET", target, NULL, 0);
    if (rc) {
        return rc;
    }
    /* The newest page: everything up to now, so this end is the newest. */
    st_older = take(0);
    st_newest = 1;
    hist_latest = st_count ? st[st_count - 1].n : 0;
    hist_unseen = 0;
    hist_busy = 0;                  /* the next poll says if it is */
    return 0;
}

uint8_t hist_older(const char *agent)
{
    char *p;
    uint8_t rc;

    hist_added = 0;
    if (!st_count) {
        return hist_load(agent);
    }
    p = fmt_str(target, "/a2/hist?a=");
    p = agent_param(p, agent);
    p = fmt_u32(fmt_str(p, "&before="), st[0].n);
    fmt_u32(fmt_str(p, "&k="), HIST_PAGE);
    rc = ses_call("GET", target, NULL, 0);
    if (rc) {
        return rc;
    }
    /* Adding older ones may have dropped the newest; st_older is the
     * gateway's word, read after. */
    rc = take_older();
    st_older = rc;
    return 0;
}

uint8_t hist_newer(const char *agent)
{
    char *p;
    uint8_t rc, more, was_older;

    hist_added = 0;
    if (!st_count) {
        return hist_load(agent);
    }
    p = fmt_str(target, "/a2/poll?a=");
    p = agent_param(p, agent);
    fmt_u32(fmt_str(p, "&after="), st[st_count - 1].n);
    rc = ses_call("GET", target, NULL, 0);
    if (rc) {
        return rc;
    }
    was_older = st_older;
    more = take(0);
    st_newest = (uint8_t)!more;
    st_older = (uint8_t)(st_older | was_older);
    note_latest();
    if (st_newest) {
        hist_unseen = 0;
    }
    return 0;
}

uint8_t hist_send(const char *agent, const char *text, uint8_t n)
{
    char *p;
    uint8_t rc;

    hist_added = 0;
    p = fmt_str(target, "/a2/send?a=");
    agent_param(p, agent);
    rc = ses_call("POST", target, (const uint8_t *)text, n);
    if (rc) {
        return rc;
    }
    take(0);
    note_latest();
    hist_busy = 1;                  /* until a poll says otherwise */
    return 0;
}

uint8_t hist_poll(const char *agent)
{
    a2_iter_t it;
    a2_item_t item;
    char *p;
    uint8_t rc, more = 0, at_end = (uint8_t)(st_newest && st_count);

    hist_added = 0;
    p = fmt_str(target, "/a2/poll?a=");
    p = agent_param(p, agent);
    fmt_u32(fmt_str(p, "&after="), at_end ? st[st_count - 1].n : hist_latest);
    rc = ses_call("GET", target, NULL, 0);
    if (rc) {
        return rc;
    }
    memset(hist_unread, 0, sizeof hist_unread);     /* the reply lists the others */
    a2_iter(&it, &ses_reply);
    while (a2_next(&it, &item) == 1) {
        if (item.kind == A2_ITEM_MSG) {
            if (at_end) {
                hist_added = (uint8_t)(hist_added +
                    st_add(item.n, item.mine, item.time, item.text, item.tlen, 0));
            } else if (item.n > hist_latest) {
                ++hist_unseen;
            }
            if (item.n > hist_latest) {
                hist_latest = item.n;
            }
        } else if (a2_is(&item, "busy") && item.vlen == 1) {
            hist_busy = (uint8_t)(item.val[0] == '1');
        } else if (a2_is(&item, "unread")) {
            take_unread(&item);
        } else if (a2_is(&item, "more") && item.vlen == 1) {
            more = (uint8_t)(item.val[0] == '1');
        }
    }
    if (at_end && more) {
        st_newest = 0;              /* more than fitted: fetched as scrolled to */
        ++hist_unseen;
    }
    return 0;
}
