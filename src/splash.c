#include "screen.h"
#include "ui.h"
#include "build.h"

static void row_center(uint8_t row, const char *s, uint8_t inverse)
{
    lb_clear(SC_SPACE);
    lb_center(s, inverse);
    scr_blit(row);
}

void splash_show(void)
{
    scr_clear(0, SCR_ROWS - 1);

    row_center(7, "          ", 1);
    row_center(8, " TWOEGRAM ", 1);
    row_center(9, "          ", 1);
    row_center(11, "Authenticated chat for your Apple //e", 0);
    row_center(17, "Version " TWOEGRAM_VERSION ", build " BUILD_ID, 0);
    status_msg("LOOKING FOR THE COGS CARD");
}
