#!/usr/bin/env bash
# test_raku_ir_rung_16_for_array.sh — RK-16: for @arr -> $x real array iteration
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP="$ROOT/scrip"
TESTS="$ROOT/test/raku"
pass=0; fail=0

run_test() {
    tname="$1"
    src="$TESTS/${tname}.raku"
    exp="$TESTS/${tname}.expected"
    got=$("$SCRIP" --ir-run "$src" 2>/dev/null) || true
    if [ "$got" = "$(cat "$exp")" ]; then
        echo "  PASS $tname"; pass=$((pass+1))
    else
        echo "  FAIL $tname"
        diff <(echo "$got") "$exp" | head -8
        fail=$((fail+1))
    fi
}

echo "=== RK-16: for @arr -> \$x array iteration ==="
run_test rk_for_array

echo ""
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ]
