#!/usr/bin/env bash
# build_scrip.sh — build the scrip compiler from snobol4ever/one4all
# Idempotent. Safe to run multiple times.
# Usage: bash build/build_scrip.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ONE4ALL="$ROOT/one4all"

[ -d "$ONE4ALL/.git" ] || { echo "FAIL clone snobol4ever/one4all to $ONE4ALL first"; exit 1; }

if [ -x "$ONE4ALL/scrip" ]; then
    echo "SKIP scrip already built: $ONE4ALL/scrip"
else
    cd "$ONE4ALL/src" && make -j4 2>&1 | tail -3
    [ -x "$ONE4ALL/scrip" ] || { echo "FAIL scrip not found after build"; exit 1; }
    echo "OK  scrip built"
fi

# scrip_jvm symlink (build_snobol4_jvm.sh expects scrip_jvm in $HOME)
[ -e "$HOME/scrip_jvm" ] || ln -sf "$ONE4ALL/scrip" "$HOME/scrip_jvm"
echo "OK  scrip: $ONE4ALL/scrip"
