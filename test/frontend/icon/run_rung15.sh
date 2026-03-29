#!/bin/bash
# run_rung15.sh — rung15_real_swap JVM corpus runner
set -euo pipefail
DRIVER="${1:-/tmp/scrip-cc}"
JASMIN="$(dirname "$0")/../../../src/backend/jvm/jasmin.jar"
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CORPUS="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon/rung15_real_swap"
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
    local cls; cls=$(grep '\.class' "$jfile" | awk '{print $NF}')
    local got; got=$(timeout 5 java -cp "$TMPDIR_OUT/" "$cls" 2>/dev/null)
    local expected; expected=$(cat "$exp")
    if [ "$got" = "$expected" ]; then echo "PASS $t"; PASS=$((PASS+1))
    else echo "FAIL $t"; echo "  expected: $(echo "$expected"|tr '\n' '|')"; echo "  got:      $(echo "$got"|tr '\n' '|')"; FAIL=$((FAIL+1)); fi
}
run_test t01_real_literal
run_test t02_real_var
run_test t03_swap_basic
run_test t04_swap_str
run_test t05_lconcat
echo "--- rung15: $PASS/5 PASS, $FAIL FAIL ---"
