#!/usr/bin/env bash
# run_crosscheck.sh — compile each corpus .sno via scrip, run, diff against .ref
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS="${CORPUS:-$(cd "$TINY/../../corpus/crosscheck" 2>/dev/null && pwd || echo /home/claude/corpus/crosscheck)}"
SCRIP_CC="$TINY/scrip"
RT="$TINY/src/runtime"
SCRIP_CC_INC="$TINY/src/frontend/snobol4"
FILTER="${FILTER:-}"
STOP_ON_FAIL="${STOP_ON_FAIL:-1}"
TIMEOUT=5
TMPDIR_RUN=$(mktemp -d); trap "rm -rf $TMPDIR_RUN" EXIT

BACKEND_C="$TINY/src/backend"

# Precompile runtime into a static archive once — 5x faster per-test gcc link.
RTLIB="$TMPDIR_RUN/libsnobol4rt.a"
_rt_objs=()
for _src in \
    "$RT/x86/snobol4.c" \
    "$RT/mock/mock_includes.c" \
    "$RT/x86/snobol4_pattern.c" \
    "$RT/mock/mock_engine.c" \
    "$BACKEND_C/trampoline_branches.c" \
    "$BACKEND_C/trampoline_hello.c" \
    "$BACKEND_C/trampoline_pattern.c"; do
    _o="$TMPDIR_RUN/$(basename "${_src%.c}").o"
    gcc -O0 -g -c "$_src" -I"$RT/x86" -I"$RT" -I"$RT/x86" -I"$RT/mock" -I"$SCRIP_CC_INC" -I"$BACKEND_C" -w -o "$_o"
    _rt_objs+=("$_o")
done
ar rcs "$RTLIB" "${_rt_objs[@]}"
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

run_test() {
    local sno="$1" ref="${1%.sno}.ref" input="${1%.sno}.input"
    local name; name=$(basename "$sno" .sno)
    [[ -n "$FILTER" && "$name" != *"$FILTER"* ]] && { SKIP=$((SKIP+1)); return 0; }
    [[ ! -f "$ref" ]] && { echo -e "${YELLOW}SKIP${RESET} $name"; SKIP=$((SKIP+1)); return 0; }
    local c="$TMPDIR_RUN/$name.c" bin="$TMPDIR_RUN/$name"
    if ! "$SCRIP_CC" -trampoline "$sno" > "$c" 2>/dev/null; then
        echo -e "${RED}FAIL${RESET} $name  [scrip]"; FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && summary && exit 1; return 1
    fi
    if ! gcc -O0 -g "$c" "$RTLIB" \
        -I"$RT/x86" -I"$RT" -I"$RT/x86" -I"$RT/mock" -I"$SCRIP_CC_INC" -I"$BACKEND_C" \
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

echo "=== one4all crosscheck ==="
echo "scrip: $SCRIP_CC"; echo ""

for dir in output assign concat arith_new control_new patterns capture strings keywords functions data; do
    full="$CORPUS/$dir"; [[ -d "$full" ]] || continue
    echo "── $dir ──"
    for sno in $(ls "$full"/*.sno 2>/dev/null | sort); do run_test "$sno" || true; done
    echo ""
done

summary
[[ $FAIL -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" && exit 0; exit 1
