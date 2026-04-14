#!/usr/bin/env bash
# test_smoke_prolog.sh — per-frontend smoke for Prolog  (FI-9)
# Gate: exits 0 in < 2s on a clean build.
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6  DATE: 2026-04-14
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
PASS=0; FAIL=0

run_file() {
    local label="$1" src="$2" expected="$3"
    local tmp; tmp=$(mktemp /tmp/pl_XXXXXX.pl)
    printf '%s' "$src" > "$tmp"
    local actual; actual=$(timeout 8 "$SCRIP" --ir-run "$tmp" 2>/dev/null)
    rm -f "$tmp"
    if [ "$actual" = "$expected" ]; then echo "  PASS $label"; PASS=$((PASS+1))
    else echo "  FAIL $label (got: $(echo "$actual"|head -1))"; FAIL=$((FAIL+1)); fi
}

echo "=== Prolog smoke ==="
run_file "write_atom" \
':- initialization(main).
main :- write(hello), nl.' \
"hello"

run_file "unify" \
':- initialization(main).
main :- X = world, write(X), nl.' \
"world"

run_file "arith" \
':- initialization(main).
main :- X is 2 + 3, write(X), nl.' \
"5"

run_file "clause" \
':- initialization(main).
fact(a). fact(b). fact(c).
main :- fact(X), write(X), nl, fail ; true.' \
"a
b
c"

run_file "recursion" \
':- initialization(main).
count(0) :- !.
count(N) :- N > 0, write(N), nl, N1 is N - 1, count(N1).
main :- count(3).' \
"3
2
1"

echo ""; echo "PASS=$PASS FAIL=$FAIL"; [ "$FAIL" -eq 0 ]
