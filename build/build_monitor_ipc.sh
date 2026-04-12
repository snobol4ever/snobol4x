#!/usr/bin/env bash
# build_monitor_ipc.sh — build monitor_ipc.so for the sync-step monitor harness
# Idempotent. Safe to run multiple times.
# Usage: bash build/build_monitor_ipc.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ONE4ALL="$ROOT/one4all"
MDIR="$ONE4ALL/test/monitor"

[ -d "$MDIR" ] || { echo "FAIL monitor dir not found: $MDIR"; exit 1; }
[ -f "$MDIR/monitor_ipc.c" ] || { echo "FAIL monitor_ipc.c not found"; exit 1; }

if [ -f "$MDIR/monitor_ipc.so" ]; then
    echo "SKIP monitor_ipc.so already built"
else
    gcc -shared -fPIC -o "$MDIR/monitor_ipc.so" "$MDIR/monitor_ipc.c"
    echo "OK  monitor_ipc.so built"
fi
echo "OK  monitor_ipc.so: $MDIR/monitor_ipc.so"
