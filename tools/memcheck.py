#!/usr/bin/env python3
"""memcheck.py map cfg -- fail if a program leaves too little heap.

The heap runs from the end of BSS to the bottom of the C stack. Opening a
file through cc65 takes a 1K ProDOS buffer from it, aligned to a 256-byte
page: up to 1024 + 255 bytes, plus the allocator's own few. With less, the
open fails with no ProDOS error at all, and the feed list silently does not
load. That happened once (0.9, after the truncation notices); this keeps
it from happening again.
"""
import re
import sys

NEED = 1024 + 255 + 16      # the buffer, its alignment, the allocator's header
STACK_TOP = 0xBF00          # the C stack hangs below the ProDOS global page


def main():
    mapfile, cfg = sys.argv[1], sys.argv[2]
    m = re.search(r"^BSS\s+[0-9A-F]+\s+([0-9A-F]+)", open(mapfile).read(), re.M)
    if not m:
        print(f"memcheck: no BSS segment in {mapfile}")
        return 1
    bss_end = int(m.group(1), 16) + 1
    s = re.search(r"__STACKSIZE__:.*value\s*=\s*\$([0-9A-Fa-f]+)", open(cfg).read())
    stack = int(s.group(1), 16)
    heap = STACK_TOP - stack - bss_end
    status = "ok" if heap >= NEED else "TOO SMALL"
    print(f"memcheck: {heap} bytes of heap (BSS ends ${bss_end:04X}, stack at "
          f"${STACK_TOP - stack:04X}); opening a file needs {NEED}: {status}")
    return 0 if heap >= NEED and overlay_ok(mapfile) else 1


# The set-up overlay (src/overlay.h) loads at $0800 over LOWBSS. Only these
# buffers are idle while it is open and rebuilt afterwards; it must end
# before anything else in LOWBSS starts.
OVERLAY_MAY_COVER = ("layout.o", "msgs.o", "session.o")


def overlay_ok(mapfile):
    text = open(mapfile).read()
    m = re.search(r"^OVERLAY1\s+([0-9A-F]+)\s+([0-9A-F]+)", text, re.M)
    if not m:
        return True                     # the probe: no overlay
    end = int(m.group(2), 16)
    mods = text.split("Segment list:")[0]
    module, ok = None, True
    for line in mods.splitlines():
        if line and not line.startswith(" ") and line.endswith(":"):
            module = line[:-1].rsplit("/", 1)[-1]
            continue
        g = re.match(r"\s+LOWBSS\s+Offs=([0-9A-F]+)\s+Size=([0-9A-F]+)", line)
        if not g:
            continue
        start = 0x0800 + int(g.group(1), 16)
        size = int(g.group(2), 16)
        covered = start <= end
        if covered and module not in OVERLAY_MAY_COVER:
            print(f"memcheck: the set-up overlay ends at ${end:04X}, over "
                  f"{module}'s LOWBSS at ${start:04X}: TOO BIG")
            ok = False
    if ok:
        print(f"memcheck: the set-up overlay ends at ${end:04X}, over idle buffers only: ok")
    return ok


if __name__ == "__main__":
    sys.exit(main())
