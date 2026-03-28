#!/bin/bash
# run_rung19.sh — rung19_pow_toby JVM corpus runner
set -euo pipefail
DRIVER="${1:-/tmp/sno2c}"
JASMIN="$(dirname "$0")/../../../src/backend/jvm/jasmin.jar"
CORPUS="$(dirname "$0")/corpus/rung19_pow_toby"
TMPDIR_OUT="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_OUT"' EXIT
PASS=0; FAIL=0
run_test() {
    local t="$1"
    local icn="$CORPUS/${t}.icn"; local exp="$CORPUS/${t}.expected"
    local jfile="$TMPDIR_OUT/${t}.j"
    "$DRIVER" -jvm "$icn" -o "$jfile" 2>/dev/null
    java -jar "$JASMIN" "$jfile" -d "$TMPDIR_OUT/" 2>/dev/null
    local cls; cls=$(grep '\.class' "$jfile" | awk '{print $NF}')
    local got; got=$(timeout 5 java -cp "$TMPDIR_OUT/" "$cls" 2>/dev/null)
    local expected; expected=$(cat "$exp")
    if [ "$got" = "$expected" ]; then echo "PASS $t"; PASS=$((PASS+1))
    else echo "FAIL $t"; echo "  expected: $(echo "$expected"|tr '\n' '|')"; echo "  got:      $(echo "$got"|tr '\n' '|')"; FAIL=$((FAIL+1)); fi
}
run_test t01_pow_int
run_test t02_pow_real
run_test t03_real_toby_pos
run_test t04_real_toby_neg
run_test t05_pow_var
echo "--- rung19: $PASS/5 PASS, $FAIL FAIL ---"
