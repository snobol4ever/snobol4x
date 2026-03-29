#!/usr/bin/env bash
# run_icon_x64_rung.sh — Icon x64 corpus ladder driver
#
# Usage:  bash run_icon_x64_rung.sh <corpus-dir> [dir2 ...]
# Env:    SCRIP_CC (default: ./scrip-cc)  STOP_ON_FAIL=1

set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip-cc}"
RT="$ROOT/src/frontend/icon/icon_runtime.c"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

run_test() {
    local icn="$1"
    local base; base=$(basename "$icn" .icn)
    local dir;  dir=$(dirname "$icn")
    local exp="$dir/$base.expected"

    [[ ! -f "$exp" ]] && { echo -e "${YELLOW}SKIP${RESET} $base (no .expected)"; SKIP=$((SKIP+1)); return 0; }

    local asm="$WORK/$base.asm" obj="$WORK/$base.o" bin="$WORK/$base"

    if ! "$SCRIP_CC" -icn "$icn" -o "$asm" 2>"$WORK/$base.emit_err"; then
        echo -e "${RED}FAIL${RESET} $base  [emit error]"
        head -3 "$WORK/$base.emit_err"
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    if ! nasm -f elf64 "$asm" -o "$obj" 2>"$WORK/$base.nasm_err"; then
        echo -e "${RED}FAIL${RESET} $base  [nasm error]"
        head -5 "$WORK/$base.nasm_err"
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    if ! gcc -nostdlib -no-pie -Wl,--no-warn-execstack "$obj" "$RT" -o "$bin" 2>"$WORK/$base.link_err"; then
        echo -e "${RED}FAIL${RESET} $base  [link error]"
        head -3 "$WORK/$base.link_err"
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1; return 0
    fi

    local input="$dir/$base.input"
    local got expected
    if [[ -f "$input" ]]; then
        got=$(timeout 5 "$bin" < "$input" 2>/dev/null || true)
    else
        got=$(timeout 5 "$bin" 2>/dev/null </dev/null || true)
    fi
    expected=$(cat "$exp")

    if [[ "$got" == "$expected" ]]; then
        echo -e "${GREEN}PASS${RESET} $base"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $base"
        diff <(echo "$expected") <(echo "$got") | head -6
        FAIL=$((FAIL+1)); [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

[[ $# -eq 0 ]] && { echo "Usage: $0 <corpus-dir> [dir2 ...]"; exit 1; }

for dir in "$@"; do
    [[ -d "$dir" ]] || { echo "WARNING: not a directory: $dir" >&2; continue; }
    echo "=== $(basename "$dir") ==="
    for icn in "$dir"/*.icn; do
        [[ -f "$icn" ]] || continue
        run_test "$icn"
    done
done

echo ""
echo "============================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[[ $FAIL -eq 0 ]] && echo "ALL PASS" || { echo "FAILURES PRESENT"; exit 1; }
