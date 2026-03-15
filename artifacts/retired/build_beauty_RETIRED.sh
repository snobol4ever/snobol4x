#!/usr/bin/env bash
# test/smoke/build_beauty.sh
#
# Smoke test: compile beauty.sno through snoc → C → gcc with 0 errors.
# Milestone 1+2 validation.
#
# Usage: ./build_beauty.sh
# Outputs: /tmp/beauty_full_bin

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CORPUS="$(cd "$REPO/../SNOBOL4-corpus" && pwd)"
SNOC="$REPO/src/snoc/snoc"
INC="$CORPUS/programs/inc"
BEAUTY="$CORPUS/programs/beauty/beauty.sno"
R="$REPO/src/runtime/snobol4"
RT="$REPO/src/runtime"

echo "=== build_beauty smoke test ==="

# 1. snoc must exist
if [[ ! -x "$SNOC" ]]; then
    echo "FAIL: snoc not built at $SNOC"
    echo "  Run: cd $REPO/src/snoc && make"
    exit 1
fi
echo "PASS: snoc exists"

# 2. Compile beauty.sno → C
echo -n "Compiling beauty.sno → C ... "
if "$SNOC" "$BEAUTY" -I "$INC" > /tmp/beauty_full.c 2>/tmp/snoc_errors.txt; then
    LINES=$(wc -l < /tmp/beauty_full.c)
    echo "OK ($LINES lines)"
    echo "PASS: snoc produced C"
else
    echo "FAIL"
    cat /tmp/snoc_errors.txt | head -20
    exit 1
fi

# 3. gcc → binary
echo -n "Compiling C → binary (gcc) ... "
if gcc -O0 -g /tmp/beauty_full.c \
       "$R/snobol4.c" "$R/mock_includes.c" \
       "$R/snobol4_pattern.c" "$RT/engine.c" \
       -I"$R" -I"$RT" -lgc -lm -w \
       -o /tmp/beauty_full_bin 2>/tmp/gcc_errors.txt; then
    echo "OK"
    echo "PASS: gcc compiled with 0 errors"
else
    echo "FAIL"
    cat /tmp/gcc_errors.txt | head -30
    exit 1
fi

echo ""
echo "============================================"
echo "ALL PASS — beauty_full_bin at /tmp/beauty_full_bin"
echo "Run test_snoCommand_match.sh for grammar smoke test."
echo "Run test_self_beautify.sh for Milestone 0 validation."
