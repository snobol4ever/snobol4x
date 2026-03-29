#!/bin/bash
# run_rung06.sh — rung06_cset JVM corpus runner
# Usage: ./run_rung06.sh [scrip-cc_path]

set -euo pipefail

DRIVER="${1:-/tmp/scrip-cc}"
JASMIN="$(dirname "$0")/../../../src/backend/jvm/jasmin.jar"
CORPUS="$(dirname "$0")/corpus/rung06_cset"
TMPDIR_OUT="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_OUT"' EXIT

PASS=0; FAIL=0

run_test() {
    local t="$1"
    local icn="$CORPUS/${t}.icn"
    local exp="$CORPUS/${t}.expected"
    local jfile="$TMPDIR_OUT/${t}.j"

    "$DRIVER" -jvm "$icn" -o "$jfile" 2>/dev/null
    timeout 30 java -jar "$JASMIN" "$jfile" -d "$TMPDIR_OUT/" 2>/dev/null
    local cls
    cls=$(grep '\.class' "$jfile" | awk '{print $NF}')
    local got
    got=$(timeout 5 java -cp "$TMPDIR_OUT/" "$cls" 2>/dev/null)
    local expected
    expected=$(cat "$exp")

    if [ "$got" = "$expected" ]; then
        echo "PASS $t"
        PASS=$((PASS+1))
    else
        echo "FAIL $t"
        echo "  expected: $(echo "$expected" | tr '\n' '|')"
        echo "  got:      $(echo "$got"      | tr '\n' '|')"
        FAIL=$((FAIL+1))
    fi
}

run_test t01_any_basic
run_test t02_any_fail
run_test t03_many_basic
run_test t04_upto_basic
run_test t05_cset_var

echo "--- rung06: $PASS/5 PASS, $FAIL FAIL ---"
[ "$FAIL" -eq 0 ]
