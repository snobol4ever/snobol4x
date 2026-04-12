#!/usr/bin/env bash
# run_beauty_sc_subsystem.sh — run Snocone BEAUTY subsystem tests via scrip --ir-run
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${SCRIP:-./scrip}"
TIMEOUT="${TIMEOUT:-10}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
if [[ $# -eq 0 ]]; then echo "Usage: bash test/beauty-sc/run_beauty_sc_subsystem.sh <subsystem> ..."; exit 1; fi
if [[ ! -x "$SCRIP" ]]; then echo "ERROR: scrip not found at $SCRIP" >&2; exit 1; fi
PASS=0; FAIL=0; SKIP=0
run_subsystem() {
    local subsys="$1"
    local driver_sc="$SCRIPT_DIR/$subsys/driver.sc"
    local driver_ref="$SCRIPT_DIR/$subsys/driver.ref"
    echo "=== SCB: $subsys ==="
    if [[ ! -f "$driver_sc" ]]; then echo -e "${YELLOW}SKIP${RESET}  $subsys  (no driver.sc)"; SKIP=$((SKIP+1)); return; fi
    if [[ ! -f "$driver_ref" ]]; then echo -e "${YELLOW}SKIP${RESET}  $subsys  (no driver.ref)"; SKIP=$((SKIP+1)); return; fi
    local err; err=$(mktemp)
    local got; got=$(timeout "$TIMEOUT" "$SCRIP" --ir-run "$driver_sc" 2>"$err") || true
    local exp; exp=$(cat "$driver_ref")
    if [[ "$got" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET}  $subsys"; PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET}  $subsys"
        [[ -s "$err" ]] && head -5 "$err" | sed 's/^/  stderr: /'
        diff <(echo "$exp") <(echo "$got") | head -15
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
    rm -f "$err"
}
for subsys in "$@"; do run_subsystem "$subsys"; done
echo ""
echo "============================================"
echo -e "Results: ${GREEN}${PASS} passed${RESET} / ${RED}${FAIL} failed${RESET} / ${YELLOW}${SKIP} skipped${RESET}"
[[ $FAIL -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" || { echo -e "${RED}FAILURES${RESET}"; exit 1; }
