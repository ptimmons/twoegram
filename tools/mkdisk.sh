#!/bin/bash
# mkdisk.sh <image.po> <FILE.SYSTEM>... -- build a bootable ProDOS 8 disk.
#
# Clones the verified ProDOS 2.4.3 image so the boot blocks are known-good,
# strips everything but PRODOS, and adds our SYS files, each under its own
# file name. ProDOS launches the first .SYSTEM file in the volume
# directory, so the first one given auto-runs; the rest are reached from
# the quit selector.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AC="$ROOT/tools/ac"
BASE="$ROOT/vendor/ProDOS_2_4_3.po"
OUT="${1:?usage: mkdisk.sh <image.po> <FILE.SYSTEM>...}"
shift
[ $# -ge 1 ] || { echo "usage: mkdisk.sh <image.po> <FILE.SYSTEM>..." >&2; exit 1; }
VOL="${VOL:-TWOEGRAM}"

STRIP=(VIEW.README BITSY.BOOT QUIT.SYSTEM BASIC.SYSTEM COPYIIPLUS.8.4
       BLOCKWARDEN CAT.DOCTOR UNSHRINK CD.EXT FASTDSK FASTDSK.CONF
       FASTDSK.SYSTEM MAKE.SMALL.P8 MINIBAS MR.FIXIT.Y2K README)

[ -x "$AC" ]   || { echo "mkdisk: missing $AC (run tools/bootstrap.sh)" >&2; exit 1; }
[ -f "$BASE" ] || { echo "mkdisk: missing $BASE (run tools/bootstrap.sh)" >&2; exit 1; }
for BIN in "$@"; do
    SYS="$(basename "$BIN")"
    [ -f "$BIN" ] || { echo "mkdisk: missing $BIN (run make)" >&2; exit 1; }
    # AppleCommander truncates a long name silently, and a truncated
    # .SYSTEM name is no longer recognised as bootable.
    if [ ${#SYS} -gt 15 ]; then
        echo "mkdisk: '$SYS' is ${#SYS} characters; ProDOS allows 15" >&2
        exit 1
    fi
done

OUT_ABS="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"

# Virtual ][ buffers writes to a mounted image and flushes them on eject, so
# an image rebuilt while mounted is overwritten by the stale copy at the next
# boot. Eject it first, and give the flush a moment to land.
if pgrep -xq "Virtual \]\["; then
    osascript >/dev/null 2>&1 <<EJECT || true
tell application "Virtual ]["
    repeat with m in machines
        tell m
            try
                if (disk image of device "S6D1") is "$OUT_ABS" then eject device "S6D1"
            end try
        end tell
    end repeat
end tell
EJECT
    sleep 1
fi

cp "$BASE" "$OUT"
for f in "${STRIP[@]}"; do
    "$AC" -d "$OUT" "$f" 2>/dev/null || true
done
"$AC" -n "$OUT" "$VOL"

# cc65 writes an AppleSingle file; -as takes the type ($FF) and the load
# address ($2000) from its header.
# Anything else (the set-up overlay, TWOEGRAM.SETUP) is a plain binary
# file, type BIN, loaded by the program itself at $0800.
for BIN in "$@"; do
    case "$BIN" in
    *.SYSTEM) "$AC" -as "$OUT" "$(basename "$BIN")" < "$BIN" ;;
    *)        "$AC" -p "$OUT" "$(basename "$BIN")" BIN 0x0800 < "$BIN" ;;
    esac
done
xattr -c "$OUT" 2>/dev/null || true

# Prove the image carries the binaries just built: extract each and compare
# with the build output minus its 58-byte AppleSingle header.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for BIN in "$@"; do
    "$AC" -g "$OUT" "$(basename "$BIN")" > "$TMP/got"
    case "$BIN" in
    *.SYSTEM) tail -c +59 "$BIN" > "$TMP/want" ;;
    *)        cp "$BIN" "$TMP/want" ;;
    esac
    if ! cmp -s "$TMP/got" "$TMP/want"; then
        echo "mkdisk: $OUT does not carry $BIN" >&2
        exit 1
    fi
done

echo "built $OUT"
"$AC" -l "$OUT"
