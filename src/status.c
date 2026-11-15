/*
 * The status bar: row 24, inverse, uppercase, as fixed fields.
 *
 *   col  1   the agent
 *   col 15   the link's security: SIGNED, NOT ENCRYPTED
 *   col 39   the card and its address
 *   col 69   the help hint, ending one column short of the edge
 */
#include "screen.h"
#include "ui.h"

#define COL_LEFT    1
#define COL_SEC     15
#define COL_NET     39
#define COL_HINT    69

char status_net[24];

void status_main(const char *agent, const char *security)
{
    char up[COL_SEC - COL_LEFT - 1];
    uint8_t i;

    /* The bar is uppercase, and a long name stops short of the next
     * field. */
    for (i = 0; agent[i] && i < sizeof up - 1; ++i) {
        up[i] = (char)(agent[i] >= 'a' && agent[i] <= 'z' ? agent[i] - 32 : agent[i]);
    }
    up[i] = 0;
    lb_clear(SC_ISPACE);
    lb_text(COL_LEFT, up, 1);
    lb_text(COL_SEC, security, 1);
    lb_text(COL_NET, status_net, 1);
    lb_text(COL_HINT, OA "-? HELP", 1);
    scr_blit(STATUS_ROW);
}

void status_msg(const char *msg)
{
    lb_clear(SC_ISPACE);
    lb_center(msg, 1);
    scr_blit(STATUS_ROW);
}
