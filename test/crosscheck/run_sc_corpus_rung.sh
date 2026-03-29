#!/usr/bin/env bash
# run_sc_corpus_rung.sh — SC corpus ladder driver (-sc -asm pipeline)
#
# Compiles each .sc in a given directory via scrip-cc -sc -asm, assembles,
# links against stmt_rt + snobol4 runtime, runs, diffs vs .ref oracle.
#
# Usage:
#   bash test/crosscheck/run_sc_corpus_rung.sh <dir> [dir2 ...]
#
# Examples:
#   bash test/crosscheck/run_sc_corpus_rung.sh test/crosscheck/sc_corpus/hello
#   bash test/crosscheck/run_sc_corpus_rung.sh \
#       test/crosscheck/sc_corpus/hello \
#       test/crosscheck/sc_corpus/output \
#       test/crosscheck/sc_corpus/assign
#
# Environment overrides:
#   SCRIP_CC        — path to scrip-cc binary     (default: ./scrip-cc)
#   STOP_ON_FAIL — 1 to stop at first fail  (default: 0)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$TINY/scrip-cc}"
RT="$TINY/src/runtime"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <sc-corpus-dir> [dir2 ...]"
    exit 1
fi

if [[ ! -x "$SCRIP_CC" ]]; then
    echo "ERROR: scrip-cc not found at $SCRIP_CC"
    exit 1
fi

# Build shared runtime objects once
WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

gcc -O0 -g -c "$RT/asm/snobol4_stmt_rt.c"       -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/stmt_rt.o"
gcc -O0 -g -c "$RT/snobol4/snobol4.c"            -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/snobol4.o"
gcc -O0 -g -c "$RT/mock/mock_includes.c"          -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/mock_includes.o"
gcc -O0 -g -c "$RT/snobol4/snobol4_pattern.c"    -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/snobol4_pattern.o"
gcc -O0 -g -c "$RT/mock/mock_engine.c"            -I"$RT/snobol4" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/mock_engine.o"

LINK_OBJS="$WORK/stmt_rt.o $WORK/snobol4.o $WORK/mock_includes.o $WORK/snobol4_pattern.o $WORK/mock_engine.o"

run_test() {
    local sc="$1"
    local base; base=$(basename "$sc" .sc)
    local dir;  dir=$(dirname "$sc")
    local ref="$dir/$base.ref"
    local input="$dir/$base.input"   # optional stdin

    [[ ! -f "$ref" ]] && { echo -e "${YELLOW}SKIP${RESET} $base (no .ref)"; SKIP=$((SKIP+1)); return 0; }

    # .xfail — emitter gap queued for backend session; skip gracefully
    local xfail="$dir/$base.xfail"
    if [[ -f "$xfail" ]]; then
        local reason; reason=$(cat "$xfail")
        echo -e "${YELLOW}XFAIL${RESET} $base  [$reason]"
        SKIP=$((SKIP+1)); return 0
    fi

    local s_file="$WORK/${base}.s"
    local o_file="$WORK/${base}.o"
    local bin="$WORK/${base}_bin"

    # scrip-cc -sc -asm
    if ! "$SCRIP_CC" -sc -asm "$sc" > "$s_file" 2>"$WORK/${base}.scrip-cc_err"; then
        echo -e "${RED}FAIL${RESET} $base  [scrip-cc error]"
        cat "$WORK/${base}.scrip-cc_err" | head -3
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return 0
    fi

    # nasm
    if ! nasm -f elf64 -I"$RT/asm/" "$s_file" -o "$o_file" 2>"$WORK/${base}.nasm_err"; then
        echo -e "${RED}FAIL${RESET} $base  [nasm error]"
        head -5 "$WORK/${base}.nasm_err"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return 0
    fi

    # link
    if ! gcc -no-pie "$o_file" $LINK_OBJS -lgc -lm -w -no-pie -o "$bin" 2>"$WORK/${base}.link_err"; then
        echo -e "${RED}FAIL${RESET} $base  [link error]"
        head -3 "$WORK/${base}.link_err"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return 0
    fi

    # run
    local got exp
    if [[ -f "$input" ]]; then
        got=$(timeout 10 "$bin" < "$input" 2>/dev/null) || true
    else
        got=$(timeout 10 "$bin" 2>/dev/null) || true
    fi
    exp=$(cat "$ref")

    if [[ "$got" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET} $base"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $base"
        echo "  expected: $(echo "$exp" | head -3)"
        echo "  got:      $(echo "$got" | head -3)"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

for dir in "$@"; do
    for sc in "$dir"/*.sc; do
        [[ -f "$sc" ]] || continue
        run_test "$sc"
    done
done

echo "============================================"
echo "Results: ${GREEN}${PASS} passed${RESET}, ${RED}${FAIL} failed${RESET}, ${YELLOW}${SKIP} skipped${RESET}"
[[ $FAIL -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" || echo -e "${RED}FAILURES${RESET}"
