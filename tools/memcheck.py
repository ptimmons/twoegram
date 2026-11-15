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
    return 0 if heap >= NEED else 1


if __name__ == "__main__":
    sys.exit(main())
