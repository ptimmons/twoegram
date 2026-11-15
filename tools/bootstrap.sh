#!/bin/bash
# bootstrap.sh -- fetch the tools and images the build needs.
#
# None of these are in git: AppleCommander is a large binary, and the ProDOS
# image is not ours to redistribute. cc65 comes from Homebrew.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p tools vendor build

AC_VER=13.2
PRODOS_SHA=398d333cb2ab92df9f8bb2cf64b946f2567116910eb8359cf4bdee5d4194f0fa

echo "==> cc65"
if command -v cl65 >/dev/null; then
    echo "    ok ($(cc65 --version 2>&1 | head -1))"
else
    echo "    missing: brew install cc65" >&2
    exit 1
fi

echo "==> AppleCommander $AC_VER (native arm64, no JRE)"
if [ ! -e tools/ac ]; then
    curl -sL --fail -o tools/ac.zip \
        "https://github.com/AppleCommander/AppleCommander/releases/download/$AC_VER/ac-darwin-arm64.zip"
    unzip -oq tools/ac.zip -d tools/ && rm tools/ac.zip
    ln -sf "ac-mac-aarch64-$AC_VER" tools/ac
fi
echo "    ok"

# releases.prodos8.com is a custom domain on GitHub Pages and serves the
# *.github.io certificate, so HTTPS fails name validation. Fetch over HTTP
# and verify by SHA-256 instead.
echo "==> ProDOS 2.4.3 system disk"
if [ ! -f vendor/ProDOS_2_4_3.po ]; then
    curl -sL --fail -o vendor/ProDOS_2_4_3.po http://releases.prodos8.com/ProDOS_2_4_3.po
fi
got=$(shasum -a 256 vendor/ProDOS_2_4_3.po | cut -d' ' -f1)
if [ "$got" != "$PRODOS_SHA" ]; then
    echo "    SHA-256 mismatch: expected $PRODOS_SHA, got $got" >&2
    exit 1
fi
echo "    ok (sha256 verified)"

echo "==> Virtual ]["
if [ -d "/Applications/Virtual ][.app" ] || [ -d "/Applications/Virtual ][/Virtual ][.app" ]; then
    echo "    ok"
else
    echo "    not found: make run will not work" >&2
fi

echo
echo "bootstrap complete. try: make run"
