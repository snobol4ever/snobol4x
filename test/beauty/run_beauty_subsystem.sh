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

# --- Sanity: SPITBOL output must match ref (SPITBOL is primary oracle per D-001/D-005) ---
# Fall back to CSNOBOL4 if SPITBOL fails (e.g. FENCE edge case).
TMP=$(mktemp -d /tmp/beauty_sub_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

# Normalise DATATYPE case: uppercase both ref and output before compare (D-002/D-003)
normalise_dt() { sed 's/\b\(pattern\|string\|integer\|real\|code\|array\|table\|name\)\b/\U&/gI'; }

(cd "$INC" && SNOLIB="$X64_DIR:$INC" \
    "$X64_DIR/bootsbl" -P256k "$DRIVER" < /dev/null 2>/dev/null) \
    | normalise_dt > "$TMP/spl.out" 2>/dev/null
cat "$REF" | normalise_dt > "$TMP/ref.norm"

if diff -q "$TMP/ref.norm" "$TMP/spl.out" > /dev/null 2>&1; then
    echo "PASS: SPITBOL oracle matches ref ($(wc -l < "$REF") lines)"
else
    # SPITBOL failed (FENCE or other quirk) — try CSNOBOL4 as fallback
    INC="$INC" snobol4 -f -P256k -I"$INC" "$DRIVER" | normalise_dt > "$TMP/csn.out" 2>/dev/null
    if diff -q "$TMP/ref.norm" "$TMP/csn.out" > /dev/null 2>&1; then
        echo "PASS: CSNOBOL4 oracle matches ref (SPITBOL diverged — known quirk)"
    else
        echo "FAIL: neither SPITBOL nor CSNOBOL4 output matches ref"
        echo "--- ref ---"; cat "$REF"
        echo "--- spl ---"; cat "$TMP/spl.out"
        echo "--- csn ---"; cat "$TMP/csn.out"
        exit 2
    fi
fi

# --- 3-way monitor run ---
INC="$INC" X64_DIR="$X64_DIR" MONITOR_TIMEOUT="$MONITOR_TIMEOUT" \
    bash "$MONITOR" "$DRIVER" "$CONF"
