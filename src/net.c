/*
 * The app's view of the card: found or not, and the link state, as a
 * status-bar field.
 *
 * The manual's screenshots come from an emulator with no CoGS card, where
 * every picture would say NO COGS CARD. Built with TWOEGRAM_SHOTS (make
 * shotsdisk), the field shows a placeholder address instead; the manual
 * says so. Nothing else differs.
 */
#include "cogs.h"
#include "fmt.h"
#include "ui.h"

void net_check(void)
{
    cogs_netstat_t ns;
    uint8_t slot, rc;
    char *p;

    slot = cogs_slot ? cogs_slot : cogs_find();
    if (!slot) {
#ifdef TWOEGRAM_SHOTS
        fmt_str(status_net, "COGS 192.168.1.60");
#else
        fmt_str(status_net, "NO COGS CARD");
#endif
        return;
    }
    rc = cogs_slot ? 0 : cogs_open(slot);
    if (!rc) {
        rc = cogs_netstat(&ns);
    }
    if (rc) {
        p = fmt_str(status_net, "COGS ");
        fmt_str(p, rc == COGS_E_TIMEOUT ? "NOT ANSWERING" : "ERROR");
        return;
    }
    p = fmt_str(status_net, "COGS ");
    if (ns.link == COGS_LINK_UP) {
        fmt_ip(p, ns.ip);
    } else {
        fmt_str(p, ns.link == COGS_LINK_JOINING ? "JOINING" : "NO NETWORK");
    }
}
