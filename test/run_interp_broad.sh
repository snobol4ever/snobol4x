#!/usr/bin/env bash
# run_interp_broad.sh — scrip regression: crosscheck + beauty drivers + demos
# Usage: bash test/run_interp_broad.sh
# From: /home/claude/one4all/

set -uo pipefail
INTERP="${INTERP:-./scrip}"
CORPUS="${CORPUS:-/home/claude/corpus}"
TIMEOUT="${TIMEOUT:-10}"
INC="${INC:-$CORPUS/programs/snobol4/demo/inc}"
BEAUTY="${BEAUTY:-$CORPUS/programs/snobol4/beauty}"
DEMO="${DEMO:-$CORPUS/programs/snobol4/demo}"

PASS=0; FAIL=0
FAILURES=""

run_test() {
    local label="$1" sno="$2" ref="$3" input="${4:-}" filter="${5:-}"
    [ ! -f "$ref" ] && return
    local got exp
    if [ -n "$input" ] && [ -f "$input" ]; then
        got=$(SNO_LIB="$INC" timeout "$TIMEOUT" $INTERP "$sno" < "$input" 2>/dev/null || true)
    else
        got=$(SNO_LIB="$INC" timeout "$TIMEOUT" $INTERP "$sno" 2>/dev/null || true)
    fi
    if [ -n "$filter" ]; then
        got=$(printf '%s\n' "$got" | grep -v "$filter" || true)
    fi
    exp=$(cat "$ref")
    if [ "$got" = "$exp" ]; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        FAILURES="${FAILURES}  FAIL ${label}\n"
    fi
}

# ── Crosscheck corpus ──────────────────────────────────────────────────────────
while IFS= read -r sno; do
    ref="${sno%.sno}.ref"
    input="${sno%.sno}.input"
    [ ! -f "$ref" ] && continue
    label=$(basename "$sno" .sno)
    run_test "$label" "$sno" "$ref" "$input" ""
done < <(find "$CORPUS/crosscheck" -name "*.sno" | sort)

# ── Beauty library drivers (19 subsystems) ────────────────────────────────────
for sno in "$BEAUTY"/beauty_*_driver.sno; do
    [ ! -f "$sno" ] && continue
    name=$(basename "$sno" .sno)
    ref="$BEAUTY/${name}.ref"
    run_test "$name" "$sno" "$ref" "" ""
done

# ── Demo programs ─────────────────────────────────────────────────────────────
run_test "demo_wordcount" "$DEMO/wordcount.sno" "$DEMO/wordcount.ref" "$DEMO/wordcount.input" ""
run_test "demo_treebank"  "$DEMO/treebank.sno"  "$DEMO/treebank.ref"  "$DEMO/treebank.input"  ""
run_test "demo_claws5"    "$DEMO/claws5.sno"    "$DEMO/claws5.ref"    "$DEMO/CLAWS5inTASA.dat" ""
TIMEOUT=30 \
run_test "demo_roman"     "$DEMO/roman.sno"     "$DEMO/roman.ref"     ""                       "^ms:"

echo "PASS=$PASS FAIL=$FAIL  ($(( PASS + FAIL )) total)"
[ -n "$FAILURES" ] && printf "$FAILURES" | head -40
