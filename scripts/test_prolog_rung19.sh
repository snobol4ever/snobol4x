#!/usr/bin/env bash
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${HERE}/../scrip"
CORPUS=/home/claude/corpus/programs/prolog/rung19
PASS=0; FAIL=0
[ -d "$CORPUS" ] || { echo "SKIP: $CORPUS missing"; exit 0; }
for f in "$CORPUS"/*.pl; do
    ref="${f%.pl}.ref"; [ -f "$ref" ] || continue
    actual=$(timeout 8 "$SCRIP" --ir-run "$f" < /dev/null 2>/dev/null)
    expected=$(cat "$ref")
    if [ "$actual" = "$expected" ]; then echo "  PASS $(basename $f)"; PASS=$((PASS+1))
    else echo "  FAIL $(basename $f)"; FAIL=$((FAIL+1)); fi
done
echo "PASS=$PASS FAIL=$FAIL"; [ "$FAIL" -eq 0 ]
