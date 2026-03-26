#!/usr/bin/env bash
# run_demo.sh — run one SCRIP demo through all available backends
#
# Usage:
#   bash demo/scrip/run_demo.sh DEMO_DIR [EXPECTED_FILE]
#
# DEMO_DIR      path to a demoN/ directory containing *.scrip + *.expected
# EXPECTED_FILE optional override (default: DEMO_DIR/*.expected)
#
# Exit codes:
#   0  all available backends PASS (missing backends are SKIP, not FAIL)
#   1  at least one backend FAIL
#
# Environment overrides:
#   SNOBOL4   path to csnobol4 binary  (default: snobol4)
#   SWIPL     path to swipl binary     (default: swipl)
#   ICONT     path to icont binary     (default: icont)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SNOBOL4X="$(cd "$SCRIPT_DIR/../.." && pwd)"

GREEN='\033[0;32m'; YELLOW='\033[0;33m'; RED='\033[0;31m'; RESET='\033[0m'
pass()  { echo -e "  ${GREEN}PASS${RESET}  $*"; }
skip()  { echo -e "  ${YELLOW}SKIP${RESET}  $*"; }
fail()  { echo -e "  ${RED}FAIL${RESET}  $*"; }

DEMO_DIR="${1:-}"
if [ -z "$DEMO_DIR" ]; then
    echo "Usage: $0 DEMO_DIR [EXPECTED_FILE]" >&2
    exit 1
fi
DEMO_DIR="$(cd "$DEMO_DIR" && pwd)"

# Locate .md source
SCRIP_FILE="$(ls "$DEMO_DIR"/*.md 2>/dev/null | head -1)"
if [ -z "$SCRIP_FILE" ]; then
    echo "ERROR: no .md file found in $DEMO_DIR" >&2
    exit 1
fi

# Locate .expected file
if [ -n "${2:-}" ]; then
    EXPECTED="$2"
else
    EXPECTED="$(ls "$DEMO_DIR"/*.expected 2>/dev/null | head -1)"
fi
if [ -z "$EXPECTED" ] || [ ! -f "$EXPECTED" ]; then
    echo "ERROR: no .expected file found (tried: ${EXPECTED:-$DEMO_DIR/*.expected})" >&2
    exit 1
fi

DEMO_NAME="$(basename "$DEMO_DIR")"
echo ""
echo "═══════════════════════════════════════════════"
echo "  SCRIP demo: $DEMO_NAME  ($(basename "$SCRIP_FILE"))"
echo "═══════════════════════════════════════════════"

# ── Split .scrip into per-language files ──────────────────────────────────────
TMP="$(mktemp -d /tmp/scrip_demo_XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

python3 "$SCRIPT_DIR/scrip_split.py" "$SCRIP_FILE" "$TMP" > "$TMP/manifest.txt"
echo ""
echo "Split blocks:"
cat "$TMP/manifest.txt" | while read lang cls file; do
    echo "  $lang  →  $(basename $file)"
done

# ── Backend runners ───────────────────────────────────────────────────────────
SNOBOL4="${SNOBOL4:-snobol4}"
SWIPL="${SWIPL:-swipl}"
ICONT="${ICONT:-icont}"

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

run_backend() {
    local name="$1"
    local outfile="$2"
    local actual="$TMP/${name}.out"

    if [ ! -f "$outfile" ]; then
        skip "$name  (no source block in .scrip)"
        SKIP_COUNT=$((SKIP_COUNT+1))
        return
    fi

    if ! command -v "${!name}" &>/dev/null; then
        skip "$name  ($(basename ${!name}) not found in PATH)"
        SKIP_COUNT=$((SKIP_COUNT+1))
        return
    fi

    # Run backend
    case "$name" in
        SNOBOL4)
            "$SNOBOL4" "$outfile" > "$actual" 2>/dev/null
            ;;
        SWIPL)
            "$SWIPL" -q -f "$outfile" -t halt > "$actual" 2>/dev/null
            ;;
        ICONT)
            local exe="$TMP/icon_demo"
            "$ICONT" -o "$exe" "$outfile" > /dev/null 2>&1 && \
            "$exe" > "$actual" 2>/dev/null
            ;;
    esac

    if diff -q "$EXPECTED" "$actual" > /dev/null 2>&1; then
        pass "$name"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        fail "$name"
        echo "    expected: $(cat $EXPECTED)"
        echo "    actual:   $(cat $actual 2>/dev/null || echo '<no output>')"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi
}

echo ""
echo "Backends:"
run_backend SNOBOL4 "$TMP/snobol4.sno"
run_backend SWIPL   "$TMP/prolog.pro"
run_backend ICONT   "$TMP/icon.icn"

echo ""
echo "───────────────────────────────────────────────"
echo "  PASS: $PASS_COUNT   SKIP: $SKIP_COUNT   FAIL: $FAIL_COUNT"
echo "───────────────────────────────────────────────"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
exit 0
