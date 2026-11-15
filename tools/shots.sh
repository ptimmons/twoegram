#!/bin/bash
# shots.sh <image.po> <dir> -- capture the screen in each of its states.
#
# Boots the disk in Virtual ][ and walks through milestone 0's made-up
# conversations, saving a PNG at each step: splash, the two panes, the
# agent list with the focus, a code block, the list hidden, a message
# typed and sent, and help.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V="$ROOT/tools/vii.sh"
IMG="${1:?usage: shots.sh <image.po> <dir>}"
OUT="${2:?usage: shots.sh <image.po> <dir>}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

snap() {
    sleep 1
    "$V" snap "$OUT/$1.png" >/dev/null
    echo "  $1.png"
}

"$V" boot "$IMG" >/dev/null
"$V" await "PRESS ANY KEY" 30
snap splash

"$V" text " " >/dev/null
"$V" await "AGENTS" 10
snap chat

# The list, with the focus on it, the bar on coder.
"$V" tab >/dev/null
"$V" key "down arrow" >/dev/null
snap list-focus

"$V" ret >/dev/null
"$V" await "Want a version" 10
snap code

"$V" oa b >/dev/null
"$V" await "new elsewhere" 10
snap list-hidden

"$V" text "And one that clears a range of pages, with the first page and the count in A and X?" >/dev/null
sleep 3
snap compose
"$V" ret >/dev/null
"$V" await "nothing was sent" 10
snap sent

"$V" oa b >/dev/null
"$V" oa "?" >/dev/null
"$V" await "KEYBOARD COMMANDS" 10
snap help
"$V" text " " >/dev/null
