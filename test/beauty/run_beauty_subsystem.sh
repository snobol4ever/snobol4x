#!/bin/bash
# run_beauty_subsystem.sh <subsystem>
#
# Run one beauty.sno subsystem driver through the 3-way monitor.
# Usage: bash test/beauty/run_beauty_subsystem.sh global
#
# Exits 0 = all 3 participants agree with oracle. 1 = divergence. 2 = error.

set -uo pipefail

SUB=${1:?Usage: run_beauty_subsystem.sh <subsystem>}
SDIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SDIR/../.." && pwd)
DRIVER="$SDIR/$SUB/driver.sno"
REF="$SDIR/$SUB/driver.ref"
MONITOR="$REPO/test/monitor/run_monitor_3way.sh"
# Per-subsystem conf overrides default if present
_SUB_CONF="$SDIR/$SUB/tracepoints.conf"
if [[ -f "$_SUB_CONF" ]]; then
    CONF="${2:-$_SUB_CONF}"
else
    CONF="${2:-$REPO/test/monitor/tracepoints.conf}"
fi
INC="${INC:-/home/claude/snobol4corpus/programs/inc}"
X64_DIR="${X64_DIR:-/home/claude/x64}"
MONITOR_TIMEOUT="${MONITOR_TIMEOUT:-30}"

[[ -f "$DRIVER" ]] || { echo "ERROR: no driver at $DRIVER"; exit 2; }
[[ -f "$REF"    ]] || { echo "ERROR: no ref at $REF";    exit 2; }

echo "═══════════════════════════════════════════════════"
echo " M-BEAUTY-$(echo $SUB | tr a-z A-Z)"
echo " driver: $DRIVER"
echo "═══════════════════════════════════════════════════"

# --- Sanity: CSNOBOL4 output must match ref (CSNOBOL4 is primary oracle) ---
TMP=$(mktemp -d /tmp/beauty_sub_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

INC="$INC" snobol4 -f -P256k -I"$INC" "$DRIVER" > "$TMP/csn.out" 2>"$TMP/csn.err"
if ! diff -q "$REF" "$TMP/csn.out" > /dev/null 2>&1; then
    echo "FAIL: CSNOBOL4 output does not match ref"
    echo "--- ref ---"; cat "$REF"
    echo "--- got ---"; cat "$TMP/csn.out"
    exit 2
fi
echo "PASS: CSNOBOL4 oracle matches ref ($(wc -l < "$REF") lines)"

# --- 3-way monitor run ---
INC="$INC" X64_DIR="$X64_DIR" MONITOR_TIMEOUT="$MONITOR_TIMEOUT" \
    bash "$MONITOR" "$DRIVER" "$CONF"
