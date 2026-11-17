/*
 * Loading the set-up overlay. See overlay.h.
 */
#include "kbd.h"
#include "ui.h"
#include "overlay.h"

#ifdef __CC65__
#include <fcntl.h>
#include <unistd.h>

extern char _OVERLAY1_LOAD__[], _OVERLAY1_SIZE__[];

static uint8_t load(void)
{
    int fd, n;

    fd = open(SETUP_FILE, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    n = read(fd, _OVERLAY1_LOAD__, (unsigned)_OVERLAY1_SIZE__);
    close(fd);
    return (uint8_t)(n == (int)(unsigned)_OVERLAY1_SIZE__);
}
#else
/* On the Mac the set-up code is linked in. */
static uint8_t load(void)
{
    return 1;
}
#endif

uint8_t setup_run(const char *why)
{
    if (!load()) {
        status_msg("THE SET-UP SCREEN, TWOEGRAM.SETUP, COULD NOT BE READ FROM THE DISK");
        key_get();
        return 0;
    }
    return setup_main(why);
}
