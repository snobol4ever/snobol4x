#!/bin/bash
# run_rung01.sh — run Rung 1 corpus against a compiled icon binary
# Usage: bash run_rung01.sh <icon-binary>
# The binary must accept an .icn filename as its first argument and
# print output to stdout.
#
# Also usable with the oracle directly:
#   bash run_rung01.sh oracle
# which runs all .icn files through icont+iconx.

set -e
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CORPUS="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon"
ICONT="${ICONT:-/home/claude/icon-master/bin/icont}"
ICONX="${ICONX:-/home/claude/icon-master/bin/iconx}"

TIMEOUT="${TIMEOUT:-5}"
BINARY="${1:-}"
PASS=0
FAIL=0

for icn in "$CORPUS"/icon_rung01_paper__*.icn; do
    base=$(basename "$icn" .icn)
    expected="$CORPUS/${base}.expected"

    if [[ "$BINARY" == "oracle" ]]; then
        cp "$icn" /tmp/_icon_test_${base}.icn
        cd /tmp && "$ICONT" -s "_icon_test_${base}.icn" 2>/dev/null
        actual=$(timeout "$TIMEOUT" "$ICONX" "/tmp/_icon_test_${base}" 2>/dev/null)
    else
        actual=$(timeout "$TIMEOUT" "$BINARY" "$icn" 2>/dev/null)
    fi

    if [[ "$actual" == "$(cat "$expected")" ]]; then
        echo "PASS  $base"
        PASS=$((PASS+1))
    else
        echo "FAIL  $base"
        echo "  expected: $(cat "$expected" | tr '\n' '|')"
        echo "  actual:   $(echo "$actual" | tr '\n' '|')"
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "$PASS PASS  $FAIL FAIL"
[[ $FAIL -eq 0 ]]
