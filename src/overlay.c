/*
 * Loading the overlays. See overlay.h.
 */
#include "kbd.h"
#include "ui.h"
#include "fmt.h"
#include "save.h"
#include "overlay.h"

#ifdef __CC65__
#include <fcntl.h>
#include <unistd.h>

extern char _OVERLAY1_LOAD__[], _OVERLAY1_SIZE__[], _OVL1DATA_SIZE__[];
extern char _OVERLAY2_LOAD__[], _OVERLAY2_SIZE__[];
extern char _OVERLAY3_LOAD__[], _OVERLAY3_SIZE__[], _OVL3DATA_SIZE__[];

static uint8_t load(const char *file, char *at, unsigned size)
{
    int fd, n;

    fd = open(file, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    n = read(fd, at, size);
    close(fd);
    return (uint8_t)(n == (int)size);
}
#define LOAD_SETUP() load(SETUP_FILE, _OVERLAY1_LOAD__, (unsigned)_OVERLAY1_SIZE__ + (unsigned)_OVL1DATA_SIZE__)
#define LOAD_SAVE()  load(SAVE_FILE, _OVERLAY2_LOAD__, (unsigned)_OVERLAY2_SIZE__)
#define LOAD_PREFS() load(PREFS_FILE, _OVERLAY3_LOAD__, (unsigned)_OVERLAY3_SIZE__ + (unsigned)_OVL3DATA_SIZE__)
#else
/* On the Mac the overlays' code is linked in. */
#define LOAD_SETUP() 1
#define LOAD_SAVE()  1
#define LOAD_PREFS() 1
#endif

/* An overlay that could not be read: said, and waited for. */
static uint8_t missing(const char *msg)
{
    status_msg(msg);
    key_get();
    return 0;
}

uint8_t save_run(const char *agent, const char *label)
{
    if (!LOAD_SAVE()) {
        fmt_str(save_note, "SAVING, TWOEGRAM.SAVE, COULD NOT BE READ FROM THE DISK");
        return 0;
    }
    return save_main(agent, label);
}

uint8_t setup_run(uint8_t at_key, const char *why)
{
    if (!LOAD_SETUP()) {
        return missing("ADDING A GATEWAY, TWOEGRAM.SETUP, COULD NOT BE READ FROM THE DISK");
    }
    return setup_main(at_key, why);
}

uint8_t prefs_run(const char *why)
{
    if (!LOAD_PREFS()) {
        return missing("SETTINGS, TWOEGRAM.PREFS, COULD NOT BE READ FROM THE DISK");
    }
    return prefs_main(why);
}
