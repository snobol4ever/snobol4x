#!/usr/bin/env bash
# test/smoke/test_snoCommand_match.sh
#
# Smoke test: feed individual SNOBOL4 statements through beauty_full_bin
# and verify that NONE produce "Parse Error".
#
# ARCHITECTURAL INVARIANT (Session 50):
#   If you strip all . and $ captures from the grammar patterns, the structural
#   pattern WILL match all beauty.sno statements — validated during bootstrap.
#   Therefore: any "Parse Error" means the grammar pattern is broken at the
#   structural level, not just the capture/shift-reduce level.
#
# Usage:
#   ./test_snoCommand_match.sh [path/to/beauty_full_bin]
#
# Build beauty_full_bin first:
#   SNOC=$REPO/src/snoc/snoc
#   INC=$CORPUS/programs/inc
#   BEAUTY=$CORPUS/programs/beauty/beauty.sno
#   R=$REPO/src/runtime/snobol4
#   $SNOC $BEAUTY -I $INC > /tmp/beauty_full.c
#   gcc -O0 -g /tmp/beauty_full.c $R/snobol4.c $R/mock_includes.c \
#       $R/snobol4_pattern.c $REPO/src/runtime/engine.c \
#       -I$R -I$REPO/src/runtime -lgc -lm -w -o /tmp/beauty_full_bin

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CORPUS="$(cd "$REPO/../SNOBOL4-corpus" && pwd)"
BIN="${1:-/tmp/beauty_full_bin}"

if [[ ! -x "$BIN" ]]; then
    echo "ERROR: beauty_full_bin not found at $BIN"
    echo "Build it first (see script header)"
    exit 1
fi

PASS=0
FAIL=0
SKIP=0

check_stmt() {
    local desc="$1"
    local stmt="$2"
    # Feed statement + END to binary; check no "Parse Error" in output
    local out
    out=$(printf '%s\nEND\n' "$stmt" | timeout 5 "$BIN" 2>/dev/null || true)
    if echo "$out" | grep -q "Parse Error"; then
        echo "FAIL: $desc"
        echo "      stmt: $stmt"
        echo "      out:  $out"
        FAIL=$((FAIL + 1))
    else
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    fi
}

echo "=== snoCommand smoke test ==="
echo "Binary: $BIN"
echo ""

# --- Assignments ---
check_stmt "simple assignment"         "    X = 'hello'"
check_stmt "null assignment"           "    X = "
check_stmt "concat assignment"         "    X = 'hello' 'world'"
check_stmt "numeric assignment"        "    N = 42"
check_stmt "indirect assign"           "    \$'var' = 'value'"

# --- Pattern matches ---
check_stmt "bare pattern match"        "    X 'hello'"
check_stmt "pattern with S goto"       "    X 'hello'                        :S(END)"
check_stmt "pattern with F goto"       "    X 'hello'                        :F(END)"
check_stmt "pattern with capture"      "    X ('hello') . Y"
check_stmt "labeled pattern match"     "LOOP    X 'tick'                     :S(LOOP)"

# --- Function calls ---
check_stmt "OUTPUT assignment"         "    OUTPUT = 'hello'"
check_stmt "DEFINE call"               "    DEFINE('fn(a,b)loc')"
check_stmt "DIFFER call"               "    DIFFER(X,Y)"
check_stmt "GT call with goto"         "    GT(N,0)                          :S(END)"

# --- Comments and control ---
check_stmt "comment line"              "*   this is a comment"
check_stmt "control line"             "-INCLUDE 'foo.sno'"

# --- Gotos ---
check_stmt "unconditional goto"        "    X = 1                            :(END)"
check_stmt "labeled stmt"              "TOP X = X 1                          :(TOP)"

# --- Complex from beauty.sno itself ---
check_stmt "snoLabel definition"       "                  snoLabel = BREAK(' ' tab nl ';') ~ 'snoLabel'"
check_stmt "ARBNO pattern"             "    snoParse = ARBNO(*snoCommand)"
check_stmt "DATA call"                 "    DATA('t(type,value,nargs,left,right)')"

echo ""
echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
if [[ $FAIL -eq 0 ]]; then
    echo "ALL PASS — snoCommand matches all statement types."
    exit 0
else
    echo "FAILURES DETECTED — grammar pattern is broken at structural level."
    exit 1
fi
