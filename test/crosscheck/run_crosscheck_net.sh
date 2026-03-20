#!/usr/bin/env bash
# test/crosscheck/run_crosscheck_net.sh — NET backend full corpus runner
#
# Runs snobol4corpus crosscheck rungs against the sno2c -net backend via
# the snobol4harness adapter (adapters/tiny_net/run.sh).
#
# Usage:
#   bash test/crosscheck/run_crosscheck_net.sh [rung ...]
#
# With no arguments runs all standard rungs. With rung names runs only those:
#   bash test/crosscheck/run_crosscheck_net.sh hello output assign
#
# Environment overrides:
#   CORPUS       — crosscheck corpus dir (default: /home/claude/snobol4corpus/crosscheck)
#   HARNESS_REPO — path to snobol4harness  (default: /home/claude/snobol4harness)
#   STOP_ON_FAIL — 1 = stop at first failure (default: 0)
#   NET_CACHE    — ilasm cache dir (default: /tmp/snobol4x_net_cache)
#
# Speed: first run is slow (~400ms/test for ilasm). Repeat runs skip unchanged
# .exe files via md5 cache — only mono startup (~110ms) per test.
#
# Timeout: each test is limited to 5 seconds via the harness adapter.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS="${CORPUS:-$(cd "$TINY/../snobol4corpus/crosscheck" && pwd)}"
HARNESS_REPO="${HARNESS_REPO:-$TINY/../snobol4harness}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
export NET_CACHE="${NET_CACHE:-/tmp/snobol4x_net_cache}"
export TINY_REPO="$TINY"

ADAPTER="$HARNESS_REPO/adapters/tiny_net/run.sh"
if [[ ! -x "$ADAPTER" ]]; then
    echo "ERROR: tiny_net adapter not found at $ADAPTER"
    echo "Clone snobol4harness to $HARNESS_REPO"
    exit 2
fi

# Default rung list mirrors what crosscheck.sh covers
DEFAULT_RUNGS=(hello output assign concat arith_new control_new patterns capture strings functions data keywords)

if [[ $# -gt 0 ]]; then
    RUNGS=("$@")
else
    RUNGS=("${DEFAULT_RUNGS[@]}")
fi

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
pass=0; fail=0; skip=0

for rung in "${RUNGS[@]}"; do
    dir="$CORPUS/$rung"
    [[ -d "$dir" ]] || { echo -e "${YELLOW}SKIP${RESET} rung '$rung' (not found)"; continue; }
    echo "── $rung ──"
    for sno in "$dir"/*.sno; do
        [[ -f "$sno" ]] || continue
        ref="${sno%.sno}.ref"
        input="${sno%.sno}.input"
        base="$(basename "$sno" .sno)"
        [[ -f "$ref" ]] || { echo -e "  ${YELLOW}SKIP${RESET} $base (no .ref)"; skip=$((skip+1)); continue; }

        if [[ -f "$input" ]]; then
            actual=$(timeout 5 bash "$ADAPTER" "$sno" < "$input" 2>/dev/null || true)
        else
            actual=$(timeout 5 bash "$ADAPTER" "$sno" 2>/dev/null || true)
        fi
        expected=$(cat "$ref")

        if [[ "$actual" = "$expected" ]]; then
            echo -e "  ${GREEN}PASS${RESET} $base"
            pass=$((pass+1))
        else
            echo -e "  ${RED}FAIL${RESET} $base"
            diff <(echo "$expected") <(echo "$actual") | head -4 | sed 's/^/    /'
            fail=$((fail+1))
            [[ "$STOP_ON_FAIL" = "1" ]] && { echo "Stopping at first failure."; break 2; }
        fi
    done
done

echo ""
echo "============================================"
echo -e "Results: ${GREEN}${pass} passed${RESET}, ${RED}${fail} failed${RESET}, ${YELLOW}${skip} skipped${RESET}"
[[ $fail -eq 0 ]] && [[ $skip -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}"
exit "$fail"
