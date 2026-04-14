#!/usr/bin/env bash
# test_raku_smoke.sh — Rung 0-5 smoke tests for the Tiny-Raku frontend
# Gate: all cases PASS before any commit touching src/frontend/raku/
# Self-contained. Idempotent. Safe to run multiple times.
#
# AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [[ ! -x "$SCRIP" ]]; then
    echo "SKIP  scrip binary not found at $SCRIP — run build_scrip.sh first"
    exit 0
fi

PASS=0; FAIL=0

check() {
    local name="$1" input="$2" expected="$3"
    local f="$TMP/t.raku"
    printf '%s' "$input" > "$f"
    local got
    got=$(timeout 8 "$SCRIP" "$f" 2>/dev/null || true)
    if [[ "$got" == "$expected" ]]; then
        echo "  PASS  $name"
        PASS=$((PASS+1))
    else
        echo "  FAIL  $name"
        echo "        expected: $(printf '%s' "$expected" | head -3)"
        echo "        got:      $(printf '%s' "$got"      | head -3)"
        FAIL=$((FAIL+1))
    fi
}

echo "=== test_raku_smoke ==="

# Rung 0 — hello world
check "say string literal"    'say "hello world";'                        "hello world"
check "say single-quoted"     "say 'hello raku';"                         "hello raku"
check "print no newline"      'print "hi";'                               "hi"

# Rung 3 — arithmetic + my
check "my scalar + say"       'my $x = 3 + 4; say $x;'                   "7"
check "subtraction"           'my $x = 10 - 3; say $x;'                  "7"
check "multiplication"        'my $x = 3 * 4; say $x;'                   "12"
check "division float"        'my $x = 7 / 2; say $x;'                   "3.5"
check "modulo"                'my $x = 10 % 3; say $x;'                  "1"
check "nested arithmetic"     'my $x = (2 + 3) * 4; say $x;'            "20"
check "unary negation"        'my $x = -5; say $x;'                      "-5"

# Rung 4 — string concat
check "string concat ~"       'say "Hello" ~ " " ~ "world";'             "Hello world"
check "concat with var"       'my $n = "Raku"; say "Hello " ~ $n ~ "!";' "Hello Raku!"

# Rung 5 — range + for loop
check "for 1..5"              'for 1..5 -> $i { say $i; }'               "$(printf '1\n2\n3\n4\n5')"
check "for 1..3 sum"          'my $s = 0; for 1..3 -> $i { $s = $s + $i; } say $s;' "6"

# if/else
check "if true"               'my $x = 10; if ($x > 5) { say "big"; } else { say "small"; }' "big"
check "if false"              'my $x = 3;  if ($x > 5) { say "big"; } else { say "small"; }' "small"

# while
check "while loop"            'my $i = 1; while ($i <= 3) { say $i; $i = $i + 1; }' "$(printf '1\n2\n3')"

echo ""
echo "=== RESULTS: $PASS PASS  $FAIL FAIL ==="
[[ $FAIL -eq 0 ]]
