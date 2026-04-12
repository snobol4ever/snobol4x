#!/usr/bin/env bash
# run_crosscheck_x86_rung.sh — ASM backend corpus ladder driver
#
# Compiles each .sno in a given directory via scrip -x86, assembles,
# links against stmt_rt + snobol4 runtime, runs, diffs vs .ref oracle.
#
# Usage:
#   bash test/crosscheck/run_crosscheck_x86_rung.sh <dir> [dir2 ...]
#
# Examples:
#   bash test/crosscheck/run_crosscheck_x86_rung.sh /home/claude/corpus/crosscheck/hello
#   bash test/crosscheck/run_crosscheck_x86_rung.sh \
#       /home/claude/corpus/crosscheck/hello \
#       /home/claude/corpus/crosscheck/output
#
# Environment overrides:
#   SCRIP_CC   — path to scrip binary     (default: ./scrip)
#   INC     — SNOBOL4 include dir      (default: demo/inc)
#   STOP_ON_FAIL=1  — stop at first failure (default: 0 = keep going)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$TINY/scrip}"
RT="$TINY/src/runtime"
INC="${INC:-$TINY/demo/inc}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <corpus-dir> [dir2 ...]"
    exit 1
fi

if [[ ! -x "$SCRIP_CC" ]]; then
    echo "ERROR: scrip not found at $SCRIP_CC"
    exit 1
fi

# Build shared runtime objects once into WORK
WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

gcc -O0 -g -c "$RT/x86/snobol4_stmt_rt.c"       -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/stmt_rt.o"
gcc -O0 -g -c "$RT/x86/snobol4.c"            -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/snobol4.o"
gcc -O0 -g -c "$RT/mock/mock_includes.c"          -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/mock_includes.o"
gcc -O0 -g -c "$RT/x86/snobol4_pattern.c"    -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/snobol4_pattern.o"
gcc -O0 -g -c "$RT/x86/engine.c"               -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$WORK/engine.o"

LINK_OBJS="$WORK/stmt_rt.o $WORK/snobol4.o $WORK/mock_includes.o $WORK/snobol4_pattern.o $WORK/engine.o"

run_test() {
    local sno="$1"
    local base; base=$(basename "$sno" .sno)
    local dir;  dir=$(dirname "$sno")
    local ref="$dir/$base.ref"
    local input="$dir/$base.input"   # optional stdin

    [[ ! -f "$ref" ]] && { echo -e "${YELLOW}SKIP${RESET} $base (no .ref)"; SKIP=$((SKIP+1)); return 0; }

    # .xfail: test is known-failing (deferred feature); treat as SKIP not FAIL
    local xfail_file="$dir/$base.xfail"
    if [[ -f "$xfail_file" ]]; then
        local reason; reason=$(cat "$xfail_file")
        echo -e "${YELLOW}XFAIL${RESET} $base ($reason)"
        SKIP=$((SKIP+1)); return 0
    fi

    local s_file="$WORK/${base}.s"
    local o_file="$WORK/${base}.o"
    local bin="$WORK/${base}_bin"

    # scrip -x86
    if ! "$SCRIP_CC" -x86 -I"$INC" "$sno" > "$s_file" 2>"$WORK/${base}.scrip_err"; then
        echo -e "${RED}FAIL${RESET} $base  [scrip error]"
        cat "$WORK/${base}.scrip_err" | head -3
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return 0
    fi

    # nasm
    if ! nasm -f elf64 -I"$RT/x86/" "$s_file" -o "$o_file" 2>"$WORK/${base}.nasm_err"; then
        echo -e "${RED}FAIL${RESET} $base  [nasm error]"
        head -5 "$WORK/${base}.nasm_err"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return 0
    fi

    # link
    if ! gcc -no-pie "$o_file" $LINK_OBJS -lgc -lm -o "$bin" 2>"$WORK/${base}.link_err"; then
        echo -e "${RED}FAIL${RESET} $base  [link error]"
        head -3 "$WORK/${base}.link_err"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
        return 0
    fi

    # run (with optional stdin)
    local actual
    if [[ -f "$input" ]]; then
        actual=$(timeout 5 "$bin" < "$input" 2>/dev/null || true)
    else
        actual=$(timeout 5 "$bin" 2>/dev/null </dev/null || true)
    fi
    local expected; expected=$(cat "$ref")

    if [[ "$actual" == "$expected" ]]; then
        echo -e "${GREEN}PASS${RESET} $base"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $base"
        diff <(echo "$expected") <(echo "$actual") | head -8
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

# Walk all given directories
for dir in "$@"; do
    if [[ ! -d "$dir" ]]; then
        echo "WARNING: not a directory: $dir" >&2
        continue
    fi
    for sno in "$dir"/*.sno; do
        [[ -f "$sno" ]] || continue
        run_test "$sno"
    done
done

echo ""
echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[[ $FAIL -eq 0 ]] && echo "ALL PASS" || { echo "FAILURES PRESENT"; exit 1; }
