#!/usr/bin/env bash
# run_crosscheck_asm_corpus.sh — compile each corpus .sno via scrip -asm,
#   assemble, link, run, diff against .ref
#
# Mirrors run_crosscheck.sh but uses the ASM backend instead of C.
#
# Usage: bash test/crosscheck/run_crosscheck_asm_corpus.sh
#
# Environment:
#   STOP_ON_FAIL=1   stop after first failure (default: 0)
#   FILTER=pattern   only run tests matching pattern

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS="${CORPUS:-$(cd "$TINY/../corpus/crosscheck" && pwd)}"
SCRIP="$TINY/scrip"
RT="$TINY/src/runtime"
SCRIP_INC="$TINY/src/frontend/snobol4"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
FILTER="${FILTER:-}"
TIMEOUT=5

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'

WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

PASS=0; FAIL=0; SKIP=0; NASM_FAIL=0; TIMEOUT_COUNT=0

# ── Precompile runtime once ───────────────────────────────────────────────────
gcc -O0 -g -c "$RT/x86/snobol4_stmt_rt.c"    -I"$RT/x86" -I"$RT" -I"$SCRIP_INC" -w -o "$WORK/stmt_rt.o"
gcc -O0 -g -c "$RT/x86/snobol4.c"         -I"$RT/x86" -I"$RT" -I"$SCRIP_INC" -w -o "$WORK/snobol4.o"
gcc -O0 -g -c "$RT/mock/mock_includes.c"       -I"$RT/x86" -I"$RT" -I"$SCRIP_INC" -w -o "$WORK/mock_includes.o"
gcc -O0 -g -c "$RT/x86/snobol4_pattern.c" -I"$RT/x86" -I"$RT" -I"$SCRIP_INC" -w -o "$WORK/snobol4_pattern.o"
gcc -O0 -g -c "$RT/mock/mock_engine.c"         -I"$RT/x86" -I"$RT" -I"$SCRIP_INC" -w -o "$WORK/mock_engine.o"
gcc -O0 -g -c "$RT/x86/blk_alloc.c"            -I"$RT/x86"                              -w -o "$WORK/blk_alloc.o"
gcc -O0 -g -c "$RT/x86/blk_reloc.c"            -I"$RT/x86"                              -w -o "$WORK/blk_reloc.o"

RT_OBJS="$WORK/stmt_rt.o $WORK/snobol4.o $WORK/mock_includes.o $WORK/snobol4_pattern.o $WORK/mock_engine.o $WORK/blk_alloc.o $WORK/blk_reloc.o"

run_test() {
    local sno="$1"
    local ref="${sno%.sno}.ref"
    local input="${sno%.sno}.input"
    local name
    name=$(basename "$sno" .sno)

    [[ -n "$FILTER" && "$name" != *"$FILTER"* ]] && return
    [[ ! -f "$ref" ]] && { echo -e "${YELLOW}SKIP${RESET} $name (no .ref)"; SKIP=$((SKIP+1)); return; }

    local s_file="$WORK/${name}.s"
    local o_file="$WORK/${name}.o"
    local bin="$WORK/${name}"

    # Compile SNOBOL4 → ASM
    if ! "$SCRIP" -asm "$sno" > "$s_file" 2>/dev/null; then
        echo -e "${RED}FAIL${RESET} $name  [scrip error]"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return
    fi

    # Assemble
    if ! nasm -f elf64 -I"$RT/x86/" "$s_file" -o "$o_file" 2>/dev/null; then
        echo -e "${RED}NASM_FAIL${RESET} $name"
        NASM_FAIL=$((NASM_FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return
    fi

    # Link
    if ! gcc -no-pie "$o_file" $RT_OBJS -lgc -lm -o "$bin" 2>/dev/null; then
        echo -e "${RED}FAIL${RESET} $name  [link error]"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return
    fi

    # Run (feed .input file to stdin if present, else /dev/null to avoid blocking)
    local got
    local stdin_src="/dev/null"
    [[ -f "$input" ]] && stdin_src="$input"
    if ! got=$(timeout "$TIMEOUT" "$bin" < "$stdin_src" 2>/dev/null); then
        local ec=$?
        if [[ $ec -eq 124 ]]; then
            echo -e "${YELLOW}TIMEOUT${RESET} $name"
            TIMEOUT_COUNT=$((TIMEOUT_COUNT+1))
        else
            echo -e "${RED}FAIL${RESET} $name  [runtime exit $ec]"
            FAIL=$((FAIL+1))
        fi
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return
    fi

    local exp
    exp=$(cat "$ref")

    if [[ "$got" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET} $name"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $name"
        diff <(echo "$exp") <(echo "$got") | head -6 | sed 's/^/      /'
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

DIRS="output assign concat arith_new control_new patterns capture strings functions data keywords"

for dir in $DIRS; do
    full="$CORPUS/$dir"
    [[ -d "$full" ]] || continue
    echo "── $dir ──"
    for sno in "$full"/*.sno; do
        [[ -f "$sno" ]] || continue
        run_test "$sno" || true
    done
    echo ""
done

TOTAL=$((PASS+FAIL+NASM_FAIL+TIMEOUT_COUNT))
echo "============================================"
echo -e "Results: ${GREEN}$PASS passed${RESET}, ${RED}$FAIL failed${RESET}, ${RED}$NASM_FAIL nasm_fail${RESET}, ${YELLOW}$TIMEOUT_COUNT timeout${RESET}, ${YELLOW}$SKIP skipped${RESET}  (of $TOTAL run)"
[[ $((FAIL+NASM_FAIL+TIMEOUT_COUNT)) -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" && exit 0
exit 1
