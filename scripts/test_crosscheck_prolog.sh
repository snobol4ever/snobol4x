#!/usr/bin/env bash
# test_crosscheck_prolog.sh — 3-mode crosscheck for PROLOG (GOAL-LANG-PROLOG)
#
# Runs the prolog test corpus through --ir-run, --sm-run, --jit-run.
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

echo "=== Prolog 3-mode crosscheck ==="

T=$(mktemp /tmp/pl_XXXXXX.pl)
cat > "$T" << 'EOF'
:- initialization(main).
main :- write(hello), nl.
EOF
xcheck "hello" "$T"

cat > "$T" << 'EOF'
:- initialization(main).
fact(a). fact(b). fact(c).
main :- fact(X), write(X), nl, fail ; true.
EOF
xcheck "backtrack" "$T"

cat > "$T" << 'EOF'
:- initialization(main).
main :- X is 2 + 3, write(X), nl.
EOF
xcheck "arith" "$T"

cat > "$T" << 'EOF'
:- initialization(main).
count(0) :- !.
count(N) :- N > 0, write(N), nl, N1 is N - 1, count(N1).
main :- count(3).
EOF
xcheck "recursion" "$T"

rm -f "$T"

# Rung corpus files
RUNGS=/home/claude/corpus/programs/prolog
for rung in rung01 rung02 rung03; do
    dir="$RUNGS/$rung"
    if [ -d "$dir" ]; then
        for f in "$dir"/*.pl; do
            [ -f "$f" ] || continue
            ref="${f%.pl}.ref"
            xcheck "$(basename $f .pl)" "$f" "$ref"
        done
    fi
done

echo ""
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
