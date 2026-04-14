#!/usr/bin/env bash
# test_crosscheck_icon.sh — 3-mode crosscheck for ICON (GOAL-LANG-ICON)
#
# Runs the icon test corpus through --ir-run, --sm-run, --jit-run.
# Run on every major push. Mode-consistency check, not regression.
# If .ref present alongside test file: diffs vs oracle too.
# Exits 0 only if all three modes agree on every test.
#
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6  DATE: 2026-04-14

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
TIMEOUT=30
PASS=0; FAIL=0; SKIP=0

xcheck() {
    local label="$1" file="$2" ref="${3:-}"
    if [ ! -f "$file" ]; then echo "  SKIP $label (no file)"; SKIP=$((SKIP+1)); return; fi
    local ir sm jit
    ir=$(timeout  $TIMEOUT "$SCRIP" --ir-run  "$file" </dev/null 2>/dev/null)
    sm=$(timeout  $TIMEOUT "$SCRIP" --sm-run  "$file" </dev/null 2>/dev/null)
    jit=$(timeout $TIMEOUT "$SCRIP" --jit-run "$file" </dev/null 2>/dev/null)
    local ok=1
    if [ -n "$ref" ] && [ -f "$ref" ]; then
        local exp; exp=$(cat "$ref")
        [ "$ir"  != "$exp" ] && { echo "  FAIL $label ir-run  vs oracle"; diff <(echo "$exp") <(echo "$ir")  | head -5 | sed 's/^/    /'; ok=0; }
        [ "$sm"  != "$exp" ] && { echo "  FAIL $label sm-run  vs oracle"; diff <(echo "$exp") <(echo "$sm")  | head -5 | sed 's/^/    /'; ok=0; }
        [ "$jit" != "$exp" ] && { echo "  FAIL $label jit-run vs oracle"; diff <(echo "$exp") <(echo "$jit") | head -5 | sed 's/^/    /'; ok=0; }
    else
        [ "$sm"  != "$ir" ] && { echo "  FAIL $label sm-run  vs ir-run";  diff <(echo "$ir") <(echo "$sm")  | head -5 | sed 's/^/    /'; ok=0; }
        [ "$jit" != "$ir" ] && { echo "  FAIL $label jit-run vs ir-run";  diff <(echo "$ir") <(echo "$jit") | head -5 | sed 's/^/    /'; ok=0; }
    fi
    if [ "$ok" -eq 1 ]; then echo "  PASS $label"; PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
}

echo "=== Icon 3-mode crosscheck ==="

# Inline tests
T=$(mktemp /tmp/icn_XXXXXX.icn)
cat > "$T" << 'EOF'
procedure main()
  write(2 + 3);
end
EOF
xcheck "arith" "$T"

cat > "$T" << 'EOF'
procedure main()
  write("ab" || "cd");
end
EOF
xcheck "concat" "$T"

cat > "$T" << 'EOF'
procedure main()
  x := 10;
  if x > 5 then write("big"); else write("small");
end
EOF
xcheck "if_expr" "$T"

cat > "$T" << 'EOF'
procedure main()
  every write(1 to 3);
end
EOF
xcheck "every_to" "$T"

rm -f "$T"

# Rung files — if present
RUNGS=/home/claude/one4all/test/icon
for rung in rung01 rung02 rung03; do
    f="$RUNGS/${rung}.icn"
    ref="$RUNGS/${rung}.ref"
    [ -f "$f" ] && xcheck "rung_${rung}" "$f" "$ref"
done

echo ""
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
