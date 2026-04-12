#!/usr/bin/env bash
# run_demo.sh — run one SCRIP demo through all available backends
#
# Usage:
#   bash demo/scrip/run_demo.sh DEMO_DIR [EXPECTED_FILE]
#
# DEMO_DIR      path to a demoN/ directory containing *.md + *.expected
# EXPECTED_FILE optional override (default: DEMO_DIR/*.expected)
#
# Exit codes:
#   0  all available backends PASS (missing backends are SKIP, not FAIL)
#   1  at least one backend FAIL
#
# Environment overrides (reference interpreters):
#   SNOBOL4   path to csnobol4 binary  (default: snobol4)
#   SWIPL     path to swipl binary     (default: swipl)
#   ICONT     path to icont binary     (default: icont)
#
# Environment overrides (snobol4ever JVM frontends):
#   SCRIP         path to scrip binary       (default: auto-detect)
#   SCRIP   path to scrip binary  (default: auto-detect)
#   JASMIN        path to jasmin.jar          (default: auto-detect)
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

# ── Split .md into per-language files ─────────────────────────────────────────
TMP="$(mktemp -d /tmp/scrip_demo_XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

python3 "$SCRIPT_DIR/scrip_split.py" "$SCRIP_FILE" "$TMP" > "$TMP/manifest.txt"
echo ""
echo "Split blocks:"
cat "$TMP/manifest.txt" | while read lang cls file; do
    echo "  $lang  →  $(basename $file)"
done

# ── Auto-detect snobol4ever tools ─────────────────────────────────────────────
SCRIP="${SCRIP:-}"
if [ -z "$SCRIP" ]; then
    if [ -x "$SNOBOL4X/scrip" ]; then SCRIP="$SNOBOL4X/scrip"
    elif command -v scrip &>/dev/null; then SCRIP="scrip"; fi
fi

SCRIP="${SCRIP:-}"
if [ -z "$SCRIP" ]; then
    if [ -x "/tmp/scrip" ]; then SCRIP="/tmp/scrip"
    elif command -v scrip &>/dev/null; then SCRIP="scrip"; fi
fi

JASMIN="${JASMIN:-}"
if [ -z "$JASMIN" ]; then
    if [ -f "$SNOBOL4X/src/backend/jasmin.jar" ]; then
        JASMIN="$SNOBOL4X/src/backend/jasmin.jar"
    fi
fi

# ── Reference interpreter runners ─────────────────────────────────────────────
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
        skip "$name  (no source block in .md)"
        SKIP_COUNT=$((SKIP_COUNT+1))
        return
    fi

    if ! command -v "${!name}" &>/dev/null; then
        skip "$name  ($(basename ${!name}) not found in PATH)"
        SKIP_COUNT=$((SKIP_COUNT+1))
        return
    fi

    case "$name" in
        SNOBOL4)
            "$SNOBOL4" "$outfile" > "$actual" 2>/dev/null
            ;;
        SWIPL)
            "$SWIPL" -q -f "$outfile" -t halt > "$actual" 2>/dev/null
            ;;
        ICONT)
            local exe="$TMP/icon_demo"
            "$ICONT" -s -o "$exe" "$outfile" 2>/dev/null && \
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

# ── snobol4ever JVM runner ─────────────────────────────────────────────────────
run_jvm_backend() {
    local label="$1"    # display name, e.g. "SCRIP-JVM"
    local src="$2"      # source file to compile

    local actual="$TMP/${label}.out"
    local jfile="$TMP/${label}.j"
    local classdir="$TMP/${label}_classes"

    if [ ! -f "$src" ]; then
        skip "$label  (no source block in .md)"
        SKIP_COUNT=$((SKIP_COUNT+1))
        return
    fi

    if [ -z "$JASMIN" ] || [ ! -f "$JASMIN" ]; then
        skip "$label  (jasmin.jar not found)"
        SKIP_COUNT=$((SKIP_COUNT+1))
        return
    fi

    # Compile to Jasmin .j
    case "$label" in
        SCRIP-JVM)
            if [ -z "$SCRIP" ]; then
                skip "$label  (scrip not found)"
                SKIP_COUNT=$((SKIP_COUNT+1))
                return
            fi
            "$SCRIP" -jvm "$src" -o "$jfile" 2>/dev/null || true
            ;;
        ICON-JVM)
            if [ -z "$SCRIP" ]; then
                skip "$label  (scrip not found)"
                SKIP_COUNT=$((SKIP_COUNT+1))
                return
            fi
            "$SCRIP" -icn -jvm "$src" -o "$jfile" 2>/dev/null || true
            ;;
        PROLOG-JVM)
            if [ -z "$SCRIP" ]; then
                skip "$label  (scrip not found)"
                SKIP_COUNT=$((SKIP_COUNT+1))
                return
            fi
            "$SCRIP" -pl -jvm "$src" -o "$jfile" 2>/dev/null || true
            ;;
    esac

    if [ ! -f "$jfile" ]; then
        fail "$label  (compiler produced no output)"
        FAIL_COUNT=$((FAIL_COUNT+1))
        return
    fi

    # Extract actual classname from .j file
    classname="$(grep '^\.class' "$jfile" | head -1 | awk '{print $NF}')"
    if [ -z "$classname" ]; then
        fail "$label  (cannot determine class name from .j)"
        FAIL_COUNT=$((FAIL_COUNT+1))
        return
    fi

    # Assemble with Jasmin
    mkdir -p "$classdir"
    java -jar "$JASMIN" "$jfile" -d "$classdir" > /dev/null 2>&1 || true
    if [ ! -f "$classdir/${classname}.class" ]; then
        fail "$label  (jasmin assembly failed)"
        FAIL_COUNT=$((FAIL_COUNT+1))
        return
    fi

    # Run
    java -cp "$classdir" "$classname" > "$actual" 2>/dev/null || true

    if diff -q "$EXPECTED" "$actual" > /dev/null 2>&1; then
        pass "$label"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        fail "$label"
        echo "    expected: $(cat $EXPECTED)"
        echo "    actual:   $(cat $actual 2>/dev/null || echo '<no output>')"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi
}

# ── Run all backends ───────────────────────────────────────────────────────────
echo ""
echo "Reference interpreters:"
run_backend SNOBOL4 "$TMP/snobol4.sno"
run_backend SWIPL   "$TMP/prolog.pro"
run_backend ICONT   "$TMP/icon.icn"

echo ""
echo "snobol4ever JVM frontends:"
run_jvm_backend SCRIP-JVM   "$TMP/snobol4.sno"
run_jvm_backend ICON-JVM    "$TMP/icon.icn"
run_jvm_backend PROLOG-JVM  "$TMP/prolog.pro"

echo ""
echo "───────────────────────────────────────────────"
echo "  PASS: $PASS_COUNT   SKIP: $SKIP_COUNT   FAIL: $FAIL_COUNT"
echo "───────────────────────────────────────────────"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
exit 0
