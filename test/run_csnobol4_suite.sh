#!/usr/bin/env bash
# run_csnobol4_suite.sh — CSNOBOL4 test suite regression for scrip
#
# Runs 116 Budne-suite tests + 10 FENCE crosscheck tests = 126 total.
# Usage: CORPUS=/home/claude/corpus bash test/run_csnobol4_suite.sh
# From:  /home/claude/one4all/
#
# Stdin tests (data embedded below END in .sno):
#   atn crlf longrec rewind1 sudoku trim0 trim1 uneval2
#   — split at END line, pipe tail as stdin to scrip.
#
# Excluded (8): bench breakline genc k ndbm sleep time line2

set -uo pipefail
INTERP="${INTERP:-./scrip}"
CORPUS="${CORPUS:-/home/claude/corpus}"
TIMEOUT="${TIMEOUT:-15}"
SUITE="${SUITE:-$CORPUS/programs/csnobol4-suite}"
FENCE="${FENCE:-$CORPUS/crosscheck/patterns}"

PASS=0; FAIL=0; SKIP=0
FAILURES=""

SKIP_LIST="bench breakline genc k ndbm sleep time line2"
STDIN_TESTS="atn crlf longrec rewind1 sudoku trim0 trim1 uneval2"

# ── helpers ───────────────────────────────────────────────────────────────────

is_excluded() {
    local name="$1"
    for s in $SKIP_LIST; do [ "$name" = "$s" ] && return 0; done
    return 1
}

is_stdin_test() {
    local name="$1"
    for s in $STDIN_TESTS; do [ "$name" = "$s" ] && return 0; done
    return 1
}

# Split a .sno at the bare END line; write prog part to $2, stdin part to $3.
split_at_end() {
    local file="$1" prog_out="$2" stdin_out="$3"
    python3 - "$file" "$prog_out" "$stdin_out" << 'PY'
import sys, re
src = open(sys.argv[1], 'r', errors='replace').read()
lines = src.split('\n')
end_idx = None
for i, l in enumerate(lines):
    if re.match(r'^END\s*$', l):
        end_idx = i
        break
if end_idx is not None:
    open(sys.argv[2], 'w').write('\n'.join(lines[:end_idx+1]) + '\n')
    open(sys.argv[3], 'w').write('\n'.join(lines[end_idx+1:]))
else:
    open(sys.argv[2], 'w').write(src)
    open(sys.argv[3], 'w').write('')
PY
}

run_test() {
    local label="$1" sno="$2" ref="$3"

    if [ ! -f "$ref" ]; then
        SKIP=$((SKIP+1))
        return
    fi

    local got exp name
    name=$(basename "$sno" .sno)

    if is_stdin_test "$name"; then
        local prog_tmp stdin_tmp
        prog_tmp=$(mktemp /tmp/scrip_prog_XXXXXX.sno)
        stdin_tmp=$(mktemp /tmp/scrip_stdin_XXXXXX)
        split_at_end "$sno" "$prog_tmp" "$stdin_tmp"
        got=$(timeout "$TIMEOUT" $INTERP "$prog_tmp" < "$stdin_tmp" 2>/dev/null || true)
        rm -f "$prog_tmp" "$stdin_tmp"
    else
        got=$(timeout "$TIMEOUT" $INTERP "$sno" 2>/dev/null || true)
    fi

    exp=$(cat "$ref")

    if [ "$got" = "$exp" ]; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        FAILURES="${FAILURES}  FAIL ${label}\n"
    fi
}

# ── FENCE crosscheck tests (058–067) ─────────────────────────────────────────
for sno in "$FENCE"/*_pat_fence*.sno; do
    [ -f "$sno" ] || continue
    ref="${sno%.sno}.ref"
    label=$(basename "$sno" .sno)
    run_test "$label" "$sno" "$ref"
done

# ── Budne suite (116 tests) ───────────────────────────────────────────────────
for sno in "$SUITE"/*.sno; do
    [ -f "$sno" ] || continue
    name=$(basename "$sno" .sno)
    is_excluded "$name" && { SKIP=$((SKIP+1)); continue; }
    ref="${sno%.sno}.ref"
    run_test "$name" "$sno" "$ref"
done

echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP  ($(( PASS + FAIL )) run)"
[ -n "$FAILURES" ] && printf "$FAILURES" | head -40
