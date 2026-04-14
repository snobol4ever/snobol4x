#!/usr/bin/env bash
# test_crosscheck_rebus.sh — 3-mode crosscheck for REBUS (GOAL-LANG-REBUS)
#
# Runs the rebus test corpus through --ir-run, --sm-run, --jit-run.
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

echo "=== Rebus 3-mode crosscheck ==="

T=$(mktemp /tmp/reb_XXXXXX.reb)
cat > "$T" << 'EOF'
function main()
  OUTPUT := "hello"
end
EOF
xcheck "output" "$T"

cat > "$T" << 'EOF'
function main()
  OUTPUT := 3 + 4
end
EOF
xcheck "arith" "$T"

cat > "$T" << 'EOF'
function main()
  x := 42
  OUTPUT := x
end
EOF
xcheck "var" "$T"

cat > "$T" << 'EOF'
function main()
  OUTPUT := "ab" || "cd"
end
EOF
xcheck "concat" "$T"

rm -f "$T"

# Rebus corpus files
RUNGS=/home/claude/one4all/test/rebus
for f in "$RUNGS"/*.reb; do
    [ -f "$f" ] || continue
    ref="${f%.reb}.ref"
    xcheck "$(basename $f .reb)" "$f" "$ref"
done

echo ""
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
