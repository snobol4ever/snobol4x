#!/usr/bin/env bash
# util_bisect_beauty_hang.sh — binary-search for the line in beauty/driver.sc
# that causes a hang under scrip --ir-run.
#
# Usage:
#   bash scripts/util_bisect_beauty_hang.sh [OPTIONS]
#
# Options:
#   --driver PATH   path to assembled driver.sc (default: test/beauty-sc/beauty/driver.sc)
#   --lines N       test only at this line count (skip bisect, single probe)
#   --timeout N     seconds per probe (default: 5)
#   --mode MODE     scrip mode: --ir-run | --sm-run | --jit-run (default: --ir-run)
#   --low N         bisect lower bound (default: 1)
#   --high N        bisect upper bound (default: line count of driver)
#
# Exit codes:
#   0  bisect completed — prints "HANG STARTS AFTER LINE N"
#   1  error
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SCRIP="${SCRIP:-$ROOT/scrip}"

DRIVER="$ROOT/test/beauty-sc/beauty/driver.sc"
TIMEOUT=5
MODE="--ir-run"
FIXED_LINES=""
LOW=1
HIGH=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --driver)  DRIVER="$2";       shift 2 ;;
        --lines)   FIXED_LINES="$2";  shift 2 ;;
        --timeout) TIMEOUT="$2";      shift 2 ;;
        --mode)    MODE="$2";         shift 2 ;;
        --low)     LOW="$2";          shift 2 ;;
        --high)    HIGH="$2";         shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -f "$DRIVER" ]]  || { echo "FAIL driver not found: $DRIVER" >&2; exit 1; }
[[ -x "$SCRIP"  ]]  || { echo "FAIL scrip not found: $SCRIP"   >&2; exit 1; }

TOTAL=$(wc -l < "$DRIVER")
[[ -z "$HIGH" ]] && HIGH="$TOTAL"

TMPFILE=$(mktemp /tmp/bisect_beauty_XXXXXX.sc)
trap 'rm -f "$TMPFILE"' EXIT

# probe LINE  →  0=ok  1=hang/error
probe() {
    local n="$1"
    head -"$n" "$DRIVER" > "$TMPFILE"
    printf 'OUTPUT = "PROBE_%d_OK";\n' "$n" >> "$TMPFILE"
    local out
    out=$(timeout "$TIMEOUT" "$SCRIP" "$MODE" "$TMPFILE" < /dev/null 2>/dev/null || true)
    if echo "$out" | grep -q "PROBE_${n}_OK"; then
        return 0
    else
        return 1
    fi
}

if [[ -n "$FIXED_LINES" ]]; then
    if probe "$FIXED_LINES"; then
        echo "LINE $FIXED_LINES: OK"
    else
        echo "LINE $FIXED_LINES: HANG"
    fi
    exit 0
fi

echo "Bisecting $DRIVER ($TOTAL lines) between $LOW and $HIGH, timeout=${TIMEOUT}s mode=$MODE"

# Quick sanity checks
if probe "$HIGH"; then
    echo "LINE $HIGH: OK — no hang found in range [$LOW,$HIGH]"
    exit 0
fi
if ! probe "$LOW"; then
    echo "LINE $LOW: HANG already — hang is before line $LOW"
    exit 0
fi

lo="$LOW"
hi="$HIGH"
while [[ $((hi - lo)) -gt 1 ]]; do
    mid=$(( (lo + hi) / 2 ))
    printf "  probe %d ... " "$mid"
    if probe "$mid"; then
        echo "OK"
        lo="$mid"
    else
        echo "HANG"
        hi="$mid"
    fi
done

echo ""
echo "HANG STARTS AFTER LINE $lo  (first bad line: $hi)"
sed -n "${hi}p" "$DRIVER" | head -c 120
echo ""
