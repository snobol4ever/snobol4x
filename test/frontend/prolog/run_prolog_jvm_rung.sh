#!/usr/bin/env bash
# run_prolog_jvm_rung.sh — Prolog JVM backend corpus ladder driver
#
# Compiles each .pro in a given directory via scrip -pl -jvm, assembles
# with jasmin.jar, runs with java, diffs vs .expected oracle.
#
# Usage:
#   bash test/frontend/prolog/run_prolog_jvm_rung.sh <dir> [dir2 ...]
#
# Environment overrides:
#   SCRIP        — path to scrip binary       (default: ./scrip)
#   JASMIN       — path to jasmin.jar          (default: src/backend/jasmin.jar)
#   STOP_ON_FAIL — stop at first failure       (default: 0)
#   TIMEOUT      — per-test timeout in seconds (default: 10)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SCRIP="${SCRIP:-$ROOT/scrip}"
JASMIN="${JASMIN:-$ROOT/src/backend/jasmin.jar}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
TIMEOUT="${TIMEOUT:-10}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <corpus-dir> [dir2 ...]"
    exit 1
fi
if [[ ! -x "$SCRIP" ]]; then echo "ERROR: scrip not found at $SCRIP"; exit 1; fi
if [[ ! -f "$JASMIN" ]]; then echo "ERROR: jasmin.jar not found at $JASMIN"; exit 1; fi

WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

run_test() {
    local pro="$1"
    local base; base=$(basename "$pro" .pro)
    local dir;  dir=$(dirname "$pro")
    local expected="$dir/$base.expected"
    local xfail="$dir/$base.xfail"

    [[ -f "$expected" ]] || return 0

    if [[ -f "$xfail" ]]; then
        echo -e "${YELLOW}SKIP${RESET} $base  [xfail: $(cat "$xfail")]"
        SKIP=$((SKIP+1)); return 0
    fi

    local classdir="$WORK/$base"
    mkdir -p "$classdir"
    local jfile="$classdir/${base}.j"

    # Compile .pro -> .j
    if ! "$SCRIP" -pl -jvm "$pro" > "$jfile" 2>"$WORK/${base}.scrip_err"; then
        echo -e "${RED}FAIL${RESET} $base  [scrip error]"
        head -3 "$WORK/${base}.scrip_err"
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    # Assemble .j -> .class
    if ! java -jar "$JASMIN" "$jfile" -d "$classdir" > /dev/null 2>"$WORK/${base}.jasmin_err"; then
        echo -e "${RED}FAIL${RESET} $base  [jasmin error]"
        grep -v "Picked up" "$WORK/${base}.jasmin_err" | head -5
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    local classname
    classname=$(grep "^\.class public" "$jfile" | head -1 | awk '{print $3}')
    if [[ -z "$classname" ]]; then
        echo -e "${RED}FAIL${RESET} $base  [cannot determine class name]"
        FAIL=$((FAIL+1)); return 0
    fi

    # Run
    local actual
    actual=$(timeout "$TIMEOUT" java -cp "$classdir" "$classname" </dev/null 2>/dev/null || true)
    local exp; exp=$(cat "$expected")

    if [[ "$actual" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET} $base"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $base"
        diff <(echo "$exp") <(echo "$actual") | head -8
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

for dir in "$@"; do
    [[ -d "$dir" ]] || { echo "WARNING: not a directory: $dir" >&2; continue; }
    for pro in "$dir"/*.pro; do
        [[ -f "$pro" ]] || continue
        run_test "$pro"
    done
done

echo ""
echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[[ $FAIL -eq 0 ]] && echo "ALL PASS" || { echo "FAILURES PRESENT"; exit 1; }
