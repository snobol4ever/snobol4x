#!/usr/bin/env bash
# run_crosscheck_asm_prog.sh — Sprint A10: compile beauty.sno via -asm, run on corpus
# Usage: bash test/crosscheck/run_crosscheck_asm_prog.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS="${CORPUS:-/home/claude/corpus/crosscheck/beauty}"
SCRIP_CC="$TINY/scrip-cc"
RT="$TINY/src/runtime"
INC="${INC:-$TINY/demo/inc}"
BEAUTY="${BEAUTY:-$TINY/demo/beauty.sno}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

# Compile beauty.sno via -asm
echo "Compiling beauty.sno via scrip-cc -asm ..."
"$SCRIP_CC" -asm -I"$INC" "$BEAUTY" > "$WORK/beauty.s" 2>&1 || {
    echo -e "${RED}FAIL${RESET} scrip-cc -asm beauty.sno failed"
    cat "$WORK/beauty.s" | head -20
    exit 1
}

# Assemble
nasm -f elf64 -I"$RT/asm/" "$WORK/beauty.s" -o "$WORK/beauty.o" 2>&1 || {
    echo -e "${RED}FAIL${RESET} nasm failed"
    head -20 "$WORK/beauty.s"
    exit 1
}

# Compile runtime
gcc -O0 -g -c "$RT/asm/snobol4_stmt_rt.c"       -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/stmt_rt.o"
gcc -O0 -g -c "$RT/snobol4/snobol4.c"            -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/snobol4.o"
gcc -O0 -g -c "$RT/mock/mock_includes.c"          -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/mock_includes.o"
gcc -O0 -g -c "$RT/snobol4/snobol4_pattern.c"    -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/snobol4_pattern.o"
gcc -O0 -g -c "$RT/engine/engine.c"               -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/engine.o"

# Link beauty_asm_bin
gcc -no-pie "$WORK/beauty.o" \
    "$WORK/stmt_rt.o" "$WORK/snobol4.o" "$WORK/mock_includes.o" \
    "$WORK/snobol4_pattern.o" "$WORK/engine.o" \
    -lgc -lm -o "$WORK/beauty_asm_bin" 2>&1 || {
    echo -e "${RED}FAIL${RESET} link failed"
    exit 1
}
echo "beauty_asm_bin built OK"

# Run each beauty crosscheck test
for INPUT in "$CORPUS"/*.input; do
    NAME=$(basename "$INPUT" .input)
    REF="$CORPUS/$NAME.ref"
    [[ ! -f "$REF" ]] && { echo -e "${YELLOW}SKIP${RESET} $NAME"; SKIP=$((SKIP+1)); continue; }

    ACTUAL=$(timeout 5 "$WORK/beauty_asm_bin" < "$INPUT" 2>/dev/null || true)
    EXPECTED=$(cat "$REF")

    if [[ "$ACTUAL" == "$EXPECTED" ]]; then
        echo -e "${GREEN}PASS${RESET} $NAME"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $NAME"
        diff <(echo "$EXPECTED") <(echo "$ACTUAL") | head -6
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[[ $FAIL -eq 0 ]] && echo "ALL PASS" || { echo "FAILURES PRESENT"; exit 1; }
