/*
 * Saving a conversation to a text file (Open-Apple-S).
 *
 * The whole conversation, as the gateway keeps it (its newest 500
 * messages), not only what is on the screen: fetched a page at a time
 * from the start and written as it comes, the disk and the card taking
 * turns (no ProDOS call while a request is out). A ProDOS TXT file with
 * CR line ends, for any Apple editor:
 *
 *     Twoegram: the conversation with Pinchy
 *
 *     08:40 You
 *     Is the office light on?
 *
 *     08:41 Pinchy
 *     It is off.
 *
 * save.c is the second overlay (overlay.h): its code and text load from
 * TWOEGRAM.SAVE at $0800, over the layout table and the text buffer,
 * which are rebuilt afterwards. The reply buffer above them stays in
 * use, for the fetching.
 */
#ifndef SAVE_H
#define SAVE_H

#include "port.h"

#define SAVE_NAME_MAX   40          /* a ProDOS path, as typed */

/* What became of it, for the status bar once the screen is drawn again:
 * "SAVED 120 MESSAGES AS PINCHY.TXT", "NOT SAVED", ... In main memory,
 * since the overlay is gone by then. */
extern char save_note[80];

/* Ask for a name, then fetch and write agent's conversation (label is
 * what the file and the default name call it). 0, with save_note set;
 * or a session.c outcome if the gateway failed partway, the file kept
 * with what had come (save_note says how much). Either way the message
 * store is left holding the newest end it reached. */
uint8_t save_main(const char *agent, const char *label);

/* From the file's name: the label's letters and digits, uppercase, a
 * letter first, at most 11, then ".TXT"; "TWOEGRAM.TXT" if none. */
void save_default(const char *label, char *name);

#endif
