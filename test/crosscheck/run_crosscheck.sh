#!/usr/bin/env bash
# run_crosscheck.sh — compile each corpus .sno via sno2c, run, diff against .ref
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS="$(cd "$TINY/../../SNOBOL4-corpus/crosscheck" && pwd)"
SNO2C="$TINY/src/sno2c/sno2c"
RT="$TINY/src/runtime"
SNO2C_INC="$TINY/src/sno2c"
FILTER="${FILTER:-}"
STOP_ON_FAIL="${STOP_ON_FAIL:-1}"
TIMEOUT=5
TMPDIR_RUN=$(mktemp -d); trap "rm -rf $TMPDIR_RUN" EXIT
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

run_test() {
    local sno="$1" ref="${1%.sno}.ref" input="${1%.sno}.input"
    local name; name=$(basename "$sno" .sno)
    [[ -n "$FILTER" && "$name" != *"$FILTER"* ]] && { SKIP=$((SKIP+1)); return 0; }
    [[ ! -f "$ref" ]] && { echo -e "${YELLOW}SKIP${RESET} $name"; SKIP=$((SKIP+1)); return 0; }
    local c="$TMPDIR_RUN/$name.c" bin="$TMPDIR_RUN/$name"
    if ! "$SNO2C" -trampoline "$sno" > "$c" 2>/dev/null; then
        echo -e "${RED}FAIL${RESET} $name  [sno2c]"; FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && summary && exit 1; return 1
    fi
    if ! gcc -O0 -g "$c" \
        "$RT/snobol4/snobol4.c" "$RT/snobol4/mock_includes.c" \
        "$RT/snobol4/snobol4_pattern.c" "$RT/mock_engine.c" \
        -I"$RT/snobol4" -I"$RT" -I"$SNO2C_INC" \
        -lgc -lm -w -o "$bin" 2>/dev/null; then
        echo -e "${RED}FAIL${RESET} $name  [gcc]"; FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && summary && exit 1; return 1
    fi
    local got exp
    if [[ -f "$input" ]]; then
        got=$(timeout $TIMEOUT "$bin" < "$input" 2>/dev/null || true)
    else
        got=$(timeout $TIMEOUT "$bin" </dev/null 2>/dev/null || true)
    fi
    exp=$(cat "$ref")
    if [[ "$got" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET} $name"; PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $name"
        diff <(echo "$exp") <(echo "$got") | head -6 | sed 's/^/    /'
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && summary && exit 1
    fi
}

summary() {
    echo ""; echo "============================================"
    echo -e "Results: ${GREEN}$PASS passed${RESET}, ${RED}$FAIL failed${RESET}, ${YELLOW}$SKIP skipped${RESET}"
}

echo "=== SNOBOL4-tiny crosscheck ==="
echo "sno2c: $SNO2C"; echo ""

for dir in output assign concat arith_new control_new patterns capture strings keywords functions data; do
    full="$CORPUS/$dir"; [[ -d "$full" ]] || continue
    echo "── $dir ──"
    for sno in $(ls "$full"/*.sno 2>/dev/null | sort); do run_test "$sno" || true; done
    echo ""
done

summary
[[ $FAIL -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" && exit 0; exit 1
