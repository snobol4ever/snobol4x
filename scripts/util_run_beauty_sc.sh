#!/usr/bin/env bash
# util_run_beauty_sc.sh — run beauty/driver.sc via scrip and optionally diff against oracle.
#
# Usage:
#   bash scripts/util_run_beauty_sc.sh [OPTIONS]
#
# Options:
#   --input  FILE   SNOBOL4 source to beautify (required, or - for stdin)
#   --driver PATH   path to assembled driver.sc
#                   (default: test/beauty-sc/beauty/driver.sc)
#   --mode   MODE   scrip mode: --ir-run | --sm-run | --jit-run (default: --ir-run)
#   --timeout N     seconds (default: 15)
#   --compare       also run oracle and diff; print PASS or FAIL
#   --ref    FILE   diff against this .ref file instead of running oracle
#   --corpus PATH   corpus root (used when --compare and no --ref)
#   --oracle PATH   sbl binary path
#
# Exit codes:
#   0  success (or PASS when --compare)
#   1  FAIL or error
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SCRIP="${SCRIP:-$ROOT/scrip}"

INPUT_FILE=""
DRIVER="$ROOT/test/beauty-sc/beauty/driver.sc"
MODE="--ir-run"
TIMEOUT=15
COMPARE=0
REF_FILE=""
CORPUS="${CORPUS:-/home/claude/corpus}"
ORACLE="${ORACLE:-/home/claude/x64/bin/sbl}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)   INPUT_FILE="$2";  shift 2 ;;
        --driver)  DRIVER="$2";      shift 2 ;;
        --mode)    MODE="$2";        shift 2 ;;
        --timeout) TIMEOUT="$2";     shift 2 ;;
        --compare) COMPARE=1;        shift   ;;
        --ref)     REF_FILE="$2";    shift 2 ;;
        --corpus)  CORPUS="$2";      shift 2 ;;
        --oracle)  ORACLE="$2";      shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -x "$SCRIP"  ]] || { echo "FAIL scrip not found: $SCRIP"   >&2; exit 1; }
[[ -f "$DRIVER" ]] || { echo "FAIL driver not found: $DRIVER" >&2; exit 1; }

# Run scrip on driver.sc with given input
run_scrip() {
    if [[ -z "$INPUT_FILE" || "$INPUT_FILE" == "-" ]]; then
        timeout "$TIMEOUT" "$SCRIP" "$MODE" "$DRIVER" < /dev/stdin 2>/dev/null || true
    else
        timeout "$TIMEOUT" "$SCRIP" "$MODE" "$DRIVER" < "$INPUT_FILE" 2>/dev/null || true
    fi
}

if [[ "$COMPARE" -eq 0 ]]; then
    run_scrip
    exit 0
fi

# --compare mode: diff scrip output against oracle or --ref
GOT=$(run_scrip)

if [[ -n "$REF_FILE" ]]; then
    [[ -f "$REF_FILE" ]] || { echo "FAIL ref not found: $REF_FILE" >&2; exit 1; }
    EXP=$(cat "$REF_FILE")
else
    # Generate oracle output on the fly
    if [[ -z "$INPUT_FILE" || "$INPUT_FILE" == "-" ]]; then
        echo "FAIL --compare requires --input FILE (not stdin) when no --ref given" >&2; exit 1
    fi
    EXP=$(bash "$HERE/util_run_beauty_oracle.sh" \
              --input "$INPUT_FILE" \
              --corpus "$CORPUS" \
              --oracle "$ORACLE" 2>/dev/null) || {
        echo "SKIP oracle unavailable"
        exit 0
    }
fi

if [[ "$GOT" == "$EXP" ]]; then
    echo "PASS"
    exit 0
else
    echo "FAIL"
    diff <(echo "$EXP") <(echo "$GOT") | head -20
    exit 1
fi
