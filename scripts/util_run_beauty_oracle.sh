#!/usr/bin/env bash
# util_run_beauty_oracle.sh — run SPITBOL oracle on beauty.sno for a given input.
#
# Must be run from corpus/programs/snobol4/beauty/ so that -INCLUDE files resolve.
# Outputs beautified SNOBOL4 to stdout.
#
# Usage:
#   bash scripts/util_run_beauty_oracle.sh [OPTIONS]
#
# Options:
#   --input  FILE   SNOBOL4 source to beautify (required, or use stdin with -)
#   --beauty PATH   path to beauty.sno (default: corpus/programs/snobol4/demo/beauty.sno)
#   --corpus PATH   path to corpus root (default: /home/claude/corpus)
#   --oracle PATH   path to sbl binary (default: /home/claude/x64/bin/sbl)
#   --timeout N     seconds (default: 30)
#   --output FILE   write ref output to FILE instead of stdout
#
# Exit codes:
#   0  oracle ran successfully
#   1  oracle not found (SKIP)
#   2  oracle error
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT_FILE=""
CORPUS="${CORPUS:-/home/claude/corpus}"
ORACLE="${ORACLE:-/home/claude/x64/bin/sbl}"
BEAUTY_SRC=""
TIMEOUT=30
OUTPUT_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)   INPUT_FILE="$2";  shift 2 ;;
        --beauty)  BEAUTY_SRC="$2";  shift 2 ;;
        --corpus)  CORPUS="$2";      shift 2 ;;
        --oracle)  ORACLE="$2";      shift 2 ;;
        --timeout) TIMEOUT="$2";     shift 2 ;;
        --output)  OUTPUT_FILE="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$BEAUTY_SRC" ]] && BEAUTY_SRC="$CORPUS/programs/snobol4/demo/beauty.sno"
BEAUTY_INC="$CORPUS/programs/snobol4/beauty"

if [[ ! -x "$ORACLE" ]]; then
    echo "SKIP oracle not found: $ORACLE" >&2; exit 1
fi
if [[ ! -f "$BEAUTY_SRC" ]]; then
    echo "SKIP beauty.sno not found: $BEAUTY_SRC" >&2; exit 1
fi
if [[ ! -d "$BEAUTY_INC" ]]; then
    echo "SKIP beauty include dir not found: $BEAUTY_INC" >&2; exit 1
fi

run_oracle() {
    # Must cd to the include dir so -INCLUDE 'global.sno' etc. resolve
    (cd "$BEAUTY_INC" && timeout "$TIMEOUT" "$ORACLE" "$BEAUTY_SRC")
}

if [[ -z "$INPUT_FILE" || "$INPUT_FILE" == "-" ]]; then
    # read from stdin
    if [[ -n "$OUTPUT_FILE" ]]; then
        run_oracle < /dev/stdin > "$OUTPUT_FILE"
    else
        run_oracle < /dev/stdin
    fi
elif [[ -f "$INPUT_FILE" ]]; then
    if [[ -n "$OUTPUT_FILE" ]]; then
        run_oracle < "$INPUT_FILE" > "$OUTPUT_FILE"
    else
        run_oracle < "$INPUT_FILE"
    fi
else
    echo "FAIL input not found: $INPUT_FILE" >&2; exit 2
fi
