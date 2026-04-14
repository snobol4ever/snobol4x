#!/usr/bin/env bash
# test_crosscheck_raku.sh — 3-mode crosscheck for RAKU (GOAL-LANG-RAKU)
#
# Runs the raku test corpus through --ir-run, --sm-run, --jit-run.
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

echo "=== Raku 3-mode crosscheck ==="

T=$(mktemp /tmp/rk_XXXXXX.raku)
cat > "$T" << 'EOF'
sub main() {
    say('hello world');
}
EOF
xcheck "hello" "$T"

cat > "$T" << 'EOF'
sub main() {
    my $x = 6 * 7;
    say($x);
}
EOF
xcheck "arith" "$T"

cat > "$T" << 'EOF'
sub main() {
    my $i = 1;
    while ($i <= 3) {
        say($i);
        $i = $i + 1;
    }
}
EOF
xcheck "while_loop" "$T"

cat > "$T" << 'EOF'
sub main() {
    my $s = 'ab' ~ 'cd';
    say($s);
}
EOF
xcheck "concat" "$T"

rm -f "$T"

# Raku corpus rung files
RUNGS=/home/claude/one4all/test/raku
for f in "$RUNGS"/*.raku; do
    [ -f "$f" ] || continue
    ref="${f%.raku}.ref"
    xcheck "$(basename $f .raku)" "$f" "$ref"
done

echo ""
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
