#!/usr/bin/env bash
# run_crosscheck_jvm_rung.sh — JVM backend corpus ladder driver
#
# Compiles each .sno in a given directory via scrip-cc -jvm, assembles with
# jasmin.jar, runs with java, diffs vs .ref oracle.
#
# Usage:
#   bash test/crosscheck/run_crosscheck_jvm_rung.sh <dir> [dir2 ...]
#
# Environment overrides:
#   SCRIP_CC        — path to scrip-cc binary     (default: ./scrip-cc)
#   INC          — SNOBOL4 include dir      (default: demo/inc)
#   JASMIN       — path to jasmin.jar       (default: src/backend/jvm/jasmin.jar)
#   STOP_ON_FAIL — stop at first failure    (default: 0)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$TINY/scrip-cc}"
INC="${INC:-$TINY/demo/inc}"
JASMIN="${JASMIN:-$TINY/src/backend/jvm/jasmin.jar}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
TIMEOUT=10

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <corpus-dir> [dir2 ...]"
    exit 1
fi

if [[ ! -x "$SCRIP_CC" ]]; then
    echo "ERROR: scrip-cc not found at $SCRIP_CC"; exit 1
fi

if [[ ! -f "$JASMIN" ]]; then
    echo "ERROR: jasmin.jar not found at $JASMIN"; exit 1
fi

WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

run_test() {
    local sno="$1"
    local base; base=$(basename "$sno" .sno)
    local dir;  dir=$(dirname "$sno")
    local ref="$dir/$base.ref"
    local xfail="$dir/$base.xfail"
    local input="$dir/$base.input"

    [[ -f "$ref" ]] || return 0

    if [[ -f "$xfail" ]]; then
        echo -e "${YELLOW}SKIP${RESET} $base  [xfail: $(cat "$xfail")]"
        SKIP=$((SKIP+1)); return 0
    fi

    local classdir="$WORK/$base"
    mkdir -p "$classdir"
    local jfile="$classdir/${base}.j"

    # Compile .sno → .j
    if ! "$SCRIP_CC" -jvm -I"$INC" "$sno" > "$jfile" 2>"$WORK/${base}.scrip-cc_err"; then
        echo -e "${RED}FAIL${RESET} $base  [scrip-cc error]"
        head -3 "$WORK/${base}.scrip-cc_err"
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    # Assemble .j → .class
    if ! java -jar "$JASMIN" "$jfile" -d "$classdir" > /dev/null 2>"$WORK/${base}.jasmin_err"; then
        echo -e "${RED}FAIL${RESET} $base  [jasmin error]"
        grep -v "Picked up" "$WORK/${base}.jasmin_err" | head -5
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    # Derive class name from .j file (first .class public line)
    local classname
    classname=$(grep "^\.class public" "$jfile" | head -1 | awk '{print $3}')
    if [[ -z "$classname" ]]; then
        echo -e "${RED}FAIL${RESET} $base  [cannot determine class name]"
        FAIL=$((FAIL+1)); return 0
    fi

    # Run
    local actual
    if [[ -f "$input" ]]; then
        actual=$(timeout "$TIMEOUT" java -cp "$classdir" "$classname" < "$input" 2>/dev/null || true)
    else
        actual=$(timeout "$TIMEOUT" java -cp "$classdir" "$classname" </dev/null 2>/dev/null || true)
    fi
    local expected; expected=$(cat "$ref")

    if [[ "$actual" == "$expected" ]]; then
        echo -e "${GREEN}PASS${RESET} $base"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $base"
        diff <(echo "$expected") <(echo "$actual") | head -8
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

for dir in "$@"; do
    [[ -d "$dir" ]] || { echo "WARNING: not a directory: $dir" >&2; continue; }
    for sno in "$dir"/*.sno; do
        [[ -f "$sno" ]] || continue
        run_test "$sno"
    done
done

echo ""
echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[[ $FAIL -eq 0 ]] && echo "ALL PASS" || { echo "FAILURES PRESENT"; exit 1; }
