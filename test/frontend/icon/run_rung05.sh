#!/bin/bash
# run_rung05.sh — rung05_scan JVM corpus runner
# Usage: ./run_rung05.sh [icon_driver_path]
# Oracle: JVM backend (icon_emit_jvm.c) vs expected files

set -euo pipefail

DRIVER="${1:-/tmp/icon_driver}"
JASMIN="$(dirname "$0")/../../../src/backend/jvm/jasmin.jar"
CORPUS="$(dirname "$0")/corpus/rung05_scan"
TMPDIR_OUT="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_OUT"' EXIT

PASS=0; FAIL=0

run_test() {
    local t="$1"
    local icn="$CORPUS/${t}.icn"
    local exp="$CORPUS/${t}.expected"
    local jfile="$TMPDIR_OUT/${t}.j"

    "$DRIVER" -jvm "$icn" -o "$jfile" 2>/dev/null
    java -jar "$JASMIN" "$jfile" -d "$TMPDIR_OUT/" 2>/dev/null
    local cls
    cls=$(grep '\.class' "$jfile" | awk '{print $NF}')
    local got
    got=$(java -cp "$TMPDIR_OUT/" "$cls" 2>/dev/null)
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

run_test t01_scan_subject
run_test t02_scan_var
run_test t03_scan_restores
run_test t04_scan_concat_subject
run_test t05_scan_nested

echo "--- rung05: $PASS/5 PASS, $FAIL FAIL ---"
[ "$FAIL" -eq 0 ]
