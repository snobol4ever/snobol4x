#!/bin/bash
# run_rung20.sh — rung20_section_seqexpr corpus runner
# Tests: ICN_SECTION s[i:j] + ICN_SEQ_EXPR (E1;E2;...En)
set -e
DRIVER=${1:-/tmp/sno2c}
JASMIN=$(dirname "$0")/../../../../src/backend/jvm/jasmin.jar
CORPUS=$(dirname "$0")
PASS=0; FAIL=0
for icn in "$CORPUS"/*.icn; do
    base=$(basename "$icn" .icn)
    expected="${icn%.icn}.expected"
    [ -f "$expected" ] || continue
    "$DRIVER" -jvm "$icn" -o /tmp/r20_run.j 2>/dev/null || { FAIL=$((FAIL+1)); echo "FAIL(compile) $base"; continue; }
    clsname=$(grep -m1 '\.class' /tmp/r20_run.j | awk '{print $NF}')
    java -jar "$JASMIN" /tmp/r20_run.j -d /tmp/ 2>/dev/null || { FAIL=$((FAIL+1)); echo "FAIL(jasmin) $base"; continue; }
    actual=$(java -cp /tmp/ "$clsname" 2>/dev/null)
    if [ "$actual" = "$(cat $expected)" ]; then PASS=$((PASS+1)); echo "PASS $base"
    else FAIL=$((FAIL+1)); echo "FAIL $base | EXP=$(cat $expected) GOT=$actual"; fi
done
echo "=== rung20: $PASS PASS $FAIL FAIL ==="
[ $FAIL -eq 0 ]
