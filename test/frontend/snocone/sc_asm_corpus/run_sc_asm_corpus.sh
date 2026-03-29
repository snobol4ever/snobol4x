#!/bin/bash
# run_sc_asm_corpus.sh — SC corpus runner for -sc -asm pipeline
# Usage: bash run_sc_asm_corpus.sh [stop_on_fail=0]
# Run from one4all root.

PASS=0; FAIL=0; SKIP=0
STOP_ON_FAIL=${STOP_ON_FAIL:-0}
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../.." && pwd)"
SCRIP_CC="$ROOT/scrip-cc"
RT="$ROOT/src/runtime"

compile_link() {
    local sc=$1 base
    base=$(basename "$sc" .sc)
    local s=/tmp/sc_corpus_${base}.s
    local o=/tmp/sc_corpus_${base}.o
    local bin=/tmp/sc_corpus_${base}_bin
    "$SCRIP_CC" -sc -asm "$sc" > "$s" 2>/dev/null || return 1
    nasm -f elf64 -I"$RT/asm/" "$s" -o "$o" 2>/dev/null || return 1
    gcc -no-pie "$o" \
        "$RT/asm/snobol4_stmt_rt.c" \
        "$RT/snobol4/snobol4.c" \
        "$RT/mock/mock_includes.c" \
        "$RT/snobol4/snobol4_pattern.c" \
        "$RT/mock/mock_engine.c" \
        -I"$RT/snobol4" -I"$RT" -I"$ROOT/src/frontend/snobol4" \
        -lgc -lm -w -no-pie -o "$bin" 2>/dev/null || return 1
    echo "$bin"
}

run_test() {
    local sc=$1 ref=$2
    local name base
    base=$(basename "$sc" .sc)
    name=$base
    local bin
    bin=$(compile_link "$sc") || { echo "NASM_FAIL  $name"; FAIL=$((FAIL+1)); return; }
    local got
    got=$(timeout 5 "$bin" 2>/dev/null)
    local exp
    exp=$(cat "$ref")
    if [ "$got" = "$exp" ]; then
        echo "PASS       $name"
        PASS=$((PASS+1))
    else
        echo "FAIL       $name"
        echo "  expected: $(echo "$exp" | head -3)"
        echo "  got:      $(echo "$got" | head -3)"
        FAIL=$((FAIL+1))
        if [ "$STOP_ON_FAIL" = "1" ]; then exit 1; fi
    fi
}

for sc in "$DIR"/*.sc; do
    ref="${sc%.sc}.ref"
    [ -f "$ref" ] || { echo "SKIP       $(basename "$sc" .sc) (no .ref)"; SKIP=$((SKIP+1)); continue; }
    run_test "$sc" "$ref"
done

echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ] && echo "ALL PASS" || echo "FAILURES"
