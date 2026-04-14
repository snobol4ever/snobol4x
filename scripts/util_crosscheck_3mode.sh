#!/usr/bin/env bash
# util_crosscheck_3mode.sh — run a test file through all 3 modes, diff vs oracle
#
# Usage: bash util_crosscheck_3mode.sh <file> [oracle_ref]
#
# Runs <file> under --ir-run, --sm-run, --jit-run.
# If oracle_ref given: diffs each mode vs ref.
# If no ref: diffs sm-run and jit-run against ir-run (ir-run is authoritative).
# Prints PASS/FAIL per mode. Exits 0 if all agree, 1 if any diverge.
#
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6  DATE: 2026-04-14

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
TIMEOUT=30

if [ $# -lt 1 ]; then
    echo "Usage: $0 <file> [oracle.ref]" >&2; exit 1
fi
FILE="$1"
REF="${2:-}"
PASS=0; FAIL=0

run_mode() {
    local mode="$1" label="$2"
    timeout "$TIMEOUT" "$SCRIP" "$mode" "$FILE" < /dev/null 2>/dev/null
}

IR=$(run_mode --ir-run  "ir-run")
SM=$(run_mode --sm-run  "sm-run")
JIT=$(run_mode --jit-run "jit-run")

check() {
    local label="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        echo "  PASS $label"; PASS=$((PASS+1))
    else
        echo "  FAIL $label"
        diff <(echo "$want") <(echo "$got") | head -8 | sed 's/^/       /'
        FAIL=$((FAIL+1))
    fi
}

if [ -n "$REF" ] && [ -f "$REF" ]; then
    EXPECTED=$(cat "$REF")
    check "ir-run  vs oracle" "$IR"  "$EXPECTED"
    check "sm-run  vs oracle" "$SM"  "$EXPECTED"
    check "jit-run vs oracle" "$JIT" "$EXPECTED"
else
    # No oracle: sm and jit must agree with ir
    check "sm-run  vs ir-run"  "$SM"  "$IR"
    check "jit-run vs ir-run"  "$JIT" "$IR"
fi

echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
