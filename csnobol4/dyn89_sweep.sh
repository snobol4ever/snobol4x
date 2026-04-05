#!/usr/bin/env bash
# dyn89_sweep.sh — sno4parse parse-error sweep across corpus/programs/snobol4
#
# For each .sno file: run sno4parse, report OK / ERR / HANG.
# Output is one line per file — safe for context windows.
# Errors include the first error message only.
#
# Usage: bash dyn89_sweep.sh [corpus_dir]
# Default: ~/corpus/programs/snobol4
#
# Placed in one4all/csnobol4/ alongside the CSNOBOL4 patch files.

set -uo pipefail

SNO4=/home/claude/one4all/sno4parse
CORPUS="${1:-/home/claude/corpus/programs/snobol4}"
TIMEOUT=10

OK=0; ERR=0; HANG=0

find "$CORPUS" -name "*.sno" | sort | while read -r sno; do
    rel="${sno#$CORPUS/}"
    result=$(timeout "$TIMEOUT" "$SNO4" "$sno" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}

    if [[ $exit_code -eq 124 ]]; then
        echo "HANG $rel"
        ((HANG++)) || true
        continue
    fi

    first_err=$(echo "$result" | grep -m1 "ELEMNT:\|sil_error\|illegal char\|expected )" || true)
    if [[ -n "$first_err" ]]; then
        # Strip noisy IR dump prefix — keep only the error line
        clean=$(echo "$first_err" | sed 's/^[[:space:]]*(ST([0-9]*)[^l]*//')
        echo "ERR  $rel  -- $clean"
        ((ERR++)) || true
    else
        stmts=$(echo "$result" | grep -o "[0-9]* statements" | tail -1)
        echo "OK   $rel  ($stmts)"
        ((OK++)) || true
    fi
done

echo ""
echo "=== SUMMARY: OK=$OK  ERR=$ERR  HANG=$HANG ==="
