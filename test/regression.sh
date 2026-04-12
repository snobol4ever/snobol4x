#!/usr/bin/env bash
# test/regression.sh — scrip regression: full corpus vs .ref, all modes
# Usage: CORPUS=/home/claude/corpus bash test/regression.sh [--mode MODE]
# From:  /home/claude/one4all/
#
# Modes: sm-run (default interpreter), ir-run, x86, jvm, net, wasm
# With no --mode flag runs sm-run only. Specify --mode to test other backends.
#
# Sections (sm-run mode):
#   1. crosscheck corpus (patterns, capture, assign, arith, control, etc.)
#   2. beauty library drivers (19 subsystems)
#   3. demo programs
#   4. CSNOBOL4 Budne suite (116 tests)
#   5. FENCE crosscheck tests (10 tests)

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP="${SCRIP:-$ROOT/scrip}"
CORPUS="${CORPUS:-/home/claude/corpus}"
JASMIN="${JASMIN:-$ROOT/src/backend/jasmin.jar}"
TIMEOUT="${TIMEOUT:-15}"
INC="${INC:-$CORPUS/programs/snobol4/demo/inc}"
BEAUTY="${BEAUTY:-$CORPUS/programs/snobol4/beauty}"
DEMO="${DEMO:-$CORPUS/programs/snobol4/demo}"
SUITE="${SUITE:-$CORPUS/programs/csnobol4-suite}"
FENCE="${FENCE:-$CORPUS/crosscheck/patterns}"
MODE="${MODE:-sm-run}"

# Parse --mode flag
while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) MODE="$2"; shift 2 ;;
        *) shift ;;
    esac
done

PASS=0; FAIL=0
FAILURES=""

SKIP_LIST="bench breakline genc k ndbm sleep time line2"
STDIN_TESTS="atn crlf longrec rewind1 sudoku trim0 trim1 uneval2"

is_excluded() { for s in $SKIP_LIST; do [ "$1" = "$s" ] && return 0; done; return 1; }
is_stdin_test() { for s in $STDIN_TESTS; do [ "$1" = "$s" ] && return 0; done; return 1; }

split_at_end() {
    python3 - "$1" "$2" "$3" << 'PY'
import sys, re
src = open(sys.argv[1], 'r', errors='replace').read()
lines = src.split('\n')
end_idx = next((i for i, l in enumerate(lines) if re.match(r'^END\s*$', l)), None)
if end_idx is not None:
    open(sys.argv[2], 'w').write('\n'.join(lines[:end_idx+1]) + '\n')
    open(sys.argv[3], 'w').write('\n'.join(lines[end_idx+1:]))
else:
    open(sys.argv[2], 'w').write(src)
    open(sys.argv[3], 'w').write('')
PY
}

# Run a .sno file under the selected mode, capture stdout
run_sno() {
    local sno="$1" stdin_data="${2:-}"
    case "$MODE" in
        sm-run)
            if [ -n "$stdin_data" ]; then
                SNO_LIB="$INC" timeout "$TIMEOUT" "$SCRIP" "$sno" <<< "$stdin_data" 2>/dev/null || true
            else
                SNO_LIB="$INC" timeout "$TIMEOUT" "$SCRIP" "$sno" 2>/dev/null || true
            fi ;;
        ir-run)
            SNO_LIB="$INC" timeout "$TIMEOUT" "$SCRIP" --ir-run "$sno" 2>/dev/null || true ;;
        x86)
            local t; t=$(mktemp -d)
            "$SCRIP" --jit-emit --x64 "$sno" > "$t/p.s" 2>/dev/null &&
            nasm -f elf64 "$t/p.s" -o "$t/p.o" 2>/dev/null &&
            gcc "$t/p.o" -lgc -lm -o "$t/p" 2>/dev/null &&
            timeout "$TIMEOUT" "$t/p" 2>/dev/null || true
            rm -rf "$t" ;;
        jvm)
            local t; t=$(mktemp -d)
            "$SCRIP" --jit-emit --jvm "$sno" > "$t/p.j" 2>/dev/null
            if grep -q "^.class" "$t/p.j" 2>/dev/null; then
                java -jar "$JASMIN" -d "$t" "$t/p.j" 2>/dev/null &&
                timeout "$TIMEOUT" java -cp "$t" Main 2>/dev/null || true
            fi
            rm -rf "$t" ;;
        net)
            local t; t=$(mktemp -d)
            "$SCRIP" --jit-emit --net "$sno" > "$t/p.il" 2>/dev/null &&
            ilasm "$t/p.il" /output:"$t/p.exe" 2>/dev/null &&
            timeout "$TIMEOUT" mono "$t/p.exe" 2>/dev/null || true
            rm -rf "$t" ;;
        *) echo "Unknown mode: $MODE" >&2; exit 1 ;;
    esac
}

run_test() {
    local label="$1" sno="$2" ref="$3"
    [ -f "$ref" ] || return
    local got exp name
    name=$(basename "$sno" .sno)
    if is_stdin_test "$name" && [ "$MODE" = "sm-run" ]; then
        local pt st
        pt=$(mktemp /tmp/scrip_prog_XXXXXX.sno); st=$(mktemp /tmp/scrip_stdin_XXXXXX)
        split_at_end "$sno" "$pt" "$st"
        got=$(run_sno "$pt" "$(cat "$st")")
        rm -f "$pt" "$st"
    else
        got=$(run_sno "$sno")
    fi
    exp=$(cat "$ref")
    if [ "$got" = "$exp" ]; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        FAILURES="${FAILURES}  FAIL ${label}\n"
    fi
}

echo "=== scrip regression (mode: $MODE) ==="
echo ""

# 1. crosscheck corpus
echo "── crosscheck corpus ──"
while IFS= read -r sno; do
    ref="${sno%.sno}.ref"; [ -f "$ref" ] || continue
    run_test "$(basename "$sno" .sno)" "$sno" "$ref"
done < <(find "$CORPUS/crosscheck" -name "*.sno" | sort)

# 2. beauty drivers
echo "── beauty drivers ──"
for sno in "$BEAUTY"/beauty_*_driver.sno; do
    [ -f "$sno" ] || continue
    name=$(basename "$sno" .sno)
    run_test "$name" "$sno" "$BEAUTY/${name}.ref"
done

# 3. demo programs
echo "── demos ──"
run_test "demo_wordcount" "$DEMO/wordcount.sno" "$DEMO/wordcount.ref"
run_test "demo_treebank"  "$DEMO/treebank.sno"  "$DEMO/treebank.ref"
run_test "demo_claws5"    "$DEMO/claws5.sno"    "$DEMO/claws5.ref"
TIMEOUT=30 run_test "demo_roman" "$DEMO/roman.sno" "$DEMO/roman.ref"

# 4. CSNOBOL4 Budne suite
echo "── csnobol4 suite ──"
for sno in "$SUITE"/*.sno; do
    [ -f "$sno" ] || continue
    name=$(basename "$sno" .sno)
    is_excluded "$name" && continue
    run_test "$name" "$sno" "${sno%.sno}.ref"
done

# 5. FENCE tests
echo "── fence tests ──"
for sno in "$FENCE"/*_pat_fence*.sno; do
    [ -f "$sno" ] || continue
    run_test "$(basename "$sno" .sno)" "$sno" "${sno%.sno}.ref"
done

echo ""
echo "PASS=$PASS FAIL=$FAIL  ($(( PASS + FAIL )) total)"
[ -n "$FAILURES" ] && printf "$FAILURES" | head -40
