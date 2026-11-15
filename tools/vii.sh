#!/bin/bash
# vii.sh -- drive Virtual ][ from the shell, through its AppleScript dictionary.
#
# Every subcommand targets `last machine`; `boot` creates one if none exists.
# Nothing here closes a machine, so other sessions in the emulator are safe.
#
#   boot <image>          insert into S6D1 and cold-restart
#   insert <dev> <image>  put a disk in a drive, e.g. S6D2
#   screen                the screen as text, blank lines stripped
#   screen-raw            all 24 rows
#   await <text> [secs]   wait for text to appear; exit 1 on timeout
#   key <name>            "up arrow", "down arrow", "left arrow", "right
#                         arrow", esc (quote two-word names)
#   ret                   Return (not a key name Virtual ][ accepts)
#   tab                   Tab (nor is this)
#   text <string>         type characters
#   oa <char>             Open-Apple + character
#   snap <file.png>       screenshot
#   dump <addr> <len> <bank> <file>   read RAM; bank 1 is auxiliary
set -euo pipefail

APP='Virtual ]['

as() {
    osascript -e "tell application \"$APP\"" -e "$1" -e "end tell"
}

ensure_machine() {
    osascript >/dev/null <<EOF
tell application "$APP"
    if (count of machines) = 0 then
        activate
        make new AppleIIe
        delay 2
    end if
end tell
EOF
}

cmd="${1:-}"; shift || true
case "$cmd" in
boot)
    img="${1:?usage: vii.sh boot <image>}"
    img="$(cd "$(dirname "$img")" && pwd)/$(basename "$img")"
    ensure_machine
    osascript -e "tell application \"$APP\" to unfreeze (last machine)" >/dev/null 2>&1 || true
    # A failed eject makes `insert` throw, which would skip the restart and
    # leave the previous session running; retry it.
    osascript >/dev/null <<EOF
tell application "$APP"
    tell last machine
        repeat 5 times
            try
                eject device "S6D1"
            end try
            delay 0.3
        end repeat
        insert "$img" into device "S6D1"
        delay 0.3
        restart
    end tell
end tell
EOF
    # restart resets both to the machine's saved defaults, so set them after.
    # A zero keyboard delay injects keys faster than a program reads them.
    as "set speed of (last machine) to ${VII_SPEED:-maximum}" >/dev/null
    as "set keyboard delay of (last machine) to ${VII_KEYDELAY:-0.2}" >/dev/null
    echo "booted $img"
    ;;
# insert <device> <image> -- e.g. insert S6D2 build/FEEDS.po
insert)
    dev="${1:?usage: vii.sh insert <device> <image>}"
    img="${2:?usage: vii.sh insert <device> <image>}"
    img="$(cd "$(dirname "$img")" && pwd)/$(basename "$img")"
    osascript >/dev/null <<EOF
tell application "$APP"
    tell last machine
        try
            eject device "$dev"
        end try
        delay 0.3
        insert "$img" into device "$dev"
    end tell
end tell
EOF
    echo "inserted $img in $dev"
    ;;
screen)     as 'return (content of (compact screen text of (last machine))) as string' ;;
screen-raw) as 'return (content of (screen text of (last machine))) as string' ;;
await)
    want="${1:?usage: vii.sh await <text> [secs]}"; timeout="${2:-30}"
    deadline=$(( $(date +%s) + timeout ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        if "$0" screen 2>/dev/null | grep -qF -- "$want"; then exit 0; fi
        sleep 0.5
    done
    echo "vii.sh await: timed out after ${timeout}s waiting for: $want" >&2
    "$0" screen >&2 || true
    exit 1
    ;;
key)  as "tell (last machine) to type key $*" ;;
ret)  as 'tell (last machine) to type text (character id 13)' ;;
tab)  as 'tell (last machine) to type text (character id 9)' ;;
text) as "tell (last machine) to type text \"$1\"" ;;
oa)   as "tell (last machine) to type open Apple \"$1\"" ;;
snap)
    out="${1:?usage: vii.sh snap <file.png>}"
    out="$(cd "$(dirname "$out")" && pwd)/$(basename "$out")"
    as "return snap (screen picture of (last machine)) to \"${out%.png}\" format png"
    ;;
dump)
    addr=$(( ${1:?} )); len=$(( ${2:?} )); bank="${3:-0}"; out="${4:?}"
    as "return dump memory (last machine) into \"$out\" address $addr length $len bank $bank"
    ;;
*)
    sed -n '2,17p' "$0"
    exit 1
    ;;
esac
