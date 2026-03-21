#!/usr/bin/env bash
# test/smoke/test_self_beautify.sh
#
# Milestone 0 validation: beauty_full_bin self-beautifies beauty.sno
# and the output matches the CSNOBOL4 oracle.
#
# Usage: ./test_self_beautify.sh [path/to/beauty_full_bin]

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BEAUTY="$REPO/demo/beauty.sno"
INC="$REPO/demo/inc"
BIN="${1:-/tmp/beauty_full_bin}"

echo "=== Milestone 0: self-beautify smoke test ==="
echo "Binary: $BIN"

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: binary not found — run build_beauty.sh first"
    exit 1
fi

# 1. Oracle
echo -n "Running CSNOBOL4 oracle ... "
if snobol4 -f -P256k -I "$INC" "$BEAUTY" < "$BEAUTY" > /tmp/beauty_oracle.sno 2>/dev/null; then
    ORACLE_LINES=$(wc -l < /tmp/beauty_oracle.sno)
    echo "OK ($ORACLE_LINES lines)"
else
    echo "FAIL: oracle crashed"
    exit 1
fi

# 2. Compiled binary
echo -n "Running compiled binary ... "
if timeout 30 "$BIN" < "$BEAUTY" > /tmp/beauty_compiled.sno 2>/tmp/beauty_stderr.txt; then
    COMPILED_LINES=$(wc -l < /tmp/beauty_compiled.sno)
    echo "OK ($COMPILED_LINES lines)"
else
    RC=$?
    echo "FAIL (rc=$RC)"
    head -5 /tmp/beauty_stderr.txt
    head -5 /tmp/beauty_compiled.sno
    exit 1
fi

# 3. Diff
echo -n "Diffing oracle vs compiled ... "
if diff /tmp/beauty_oracle.sno /tmp/beauty_compiled.sno > /tmp/beauty_diff.txt 2>&1; then
    echo "EMPTY DIFF"
    echo ""
    echo "============================================"
    echo "MILESTONE 0 ACHIEVED — self-beautification matches oracle."
    exit 0
else
    DIFF_LINES=$(wc -l < /tmp/beauty_diff.txt)
    echo "DIFF ($DIFF_LINES lines)"
    echo ""
    head -40 /tmp/beauty_diff.txt
    echo ""
    echo "============================================"
    echo "MILESTONE 0 NOT YET ACHIEVED."
    exit 1
fi
