#!/bin/bash
# shots.sh <image.po> <dir> -- capture the screens Virtual ][ can show.
#
# Virtual ][ has no CoGS card, so it shows what Twoegram shows without
# one: the splash, the conversation screen saying there is no card, and
# help. Settings and adding a gateway are drawn on the Mac, by
# tests/host/shots.c (make convo-shots), with example addresses: here
# they would show the disk's real gateways, from probe.cfg. The conversation itself is tested on the Mac
# (tests/host/test_convo.c) and photographed on the real card.
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
snap no-card

"$V" oa "?" >/dev/null
"$V" await "KEYBOARD COMMANDS" 10
snap help
"$V" text " " >/dev/null
