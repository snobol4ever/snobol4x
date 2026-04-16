#!/usr/bin/env bash
# test_icon_ir_rung_13_alt.sh — rung13 Icon alternation generator tests (IC-6)
# Two fixes: E_ASSIGN generative box (alt_filter) + E_CAT cross-product (alt_nested)
# Gate: PASS=2 FAIL=0 (no corpus required — tests are inline)
# Authors: LCherryholmes · Claude Sonnet 4.6   DATE: 2026-04-16
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${SCRIP:-$HERE/../scrip}"
PASS=0; FAIL=0

if [ ! -x "$SCRIP" ]; then echo "SKIP scrip not found at $SCRIP"; exit 0; fi

icon() {
    local label="$1" expected="$2"
    local tmp; tmp=$(mktemp /tmp/icn_XXXXXX.icn)
    cat > "$tmp"
    local actual; actual=$(timeout 8 "$SCRIP" --ir-run "$tmp" < /dev/null 2>/dev/null) || true
    rm -f "$tmp"
    if [ "$actual" = "$expected" ]; then
        echo "  PASS $label"; PASS=$((PASS+1))
    else
        echo "  FAIL $label"
        echo "    want: $(echo "$expected" | tr '\n' '|')"
        echo "    got:  $(echo "$actual"   | tr '\n' '|')"
        FAIL=$((FAIL+1))
    fi
}

echo "=== rung13: Icon alternation generators ==="

# IC-6 fix 1: E_ASSIGN with generative RHS inside relop filter
# icn_bb_assign_gen pumps RHS alt on each tick, writes to var, returns value to binop_gen
icon "rung13_alt_filter" "$(printf '3\n4')" << 'EOF'
procedure main()
  every (x := (1|2|3|4)) > 2 & write(x);
end
EOF

# IC-6 fix 2: E_CAT with both children generative → cross-product via icn_bb_binop_gen
# ("a"|"b") || ("x"|"y") was intercepted by E_CAT one-side pump; now routes to cross-product
icon "rung13_alt_nested" "$(printf 'ax\nay\nbx\nby')" << 'EOF'
procedure main()
  every write(("a"|"b") || ("x"|"y"));
end
EOF

echo ""
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
