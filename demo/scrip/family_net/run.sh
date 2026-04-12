#!/bin/bash
# run.sh -- M-SCRIP-DEMO: SNOBOL4 parses CSV -> Prolog infers -> Icon formats
# Three languages, real EXPORT/IMPORT linkage, no funny linkage.
#
# Usage: bash run.sh
# Requires: scrip, jasmin.jar, java, ByrdBoxLinkage.j
set -e

REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$REPO/scrip}"
JASMIN="${JASMIN:-$REPO/src/backend/jasmin.jar}"
BYRD="${BYRD:-$REPO/src/runtime/jvm/ByrdBoxLinkage.j}"
OUT=./out ; mkdir -p "$OUT"

# ── Compile ──────────────────────────────────────────────────────────────────
echo "Compiling family_snobol4.sno..."
"$SCRIP_CC" -jvm family_snobol4.sno  > "$OUT/family_snobol4.j"

echo "Compiling family_prolog.pro..."
"$SCRIP_CC" -pl -jvm family_prolog.pro > "$OUT/family_prolog.j"

echo "Compiling family_icon.icn..."
"$SCRIP_CC" -jvm family_icon.icn     > "$OUT/family_icon.j"

# ── Assemble ─────────────────────────────────────────────────────────────────
echo "Assembling..."
java -jar "$JASMIN" "$BYRD"                  -d "$OUT" 2>&1 | grep -v "Picked up\|proxy\|jwt"
java -jar "$JASMIN" "$OUT/family_prolog.j"   -d "$OUT" 2>&1 | grep -v "Picked up\|proxy\|jwt"
java -jar "$JASMIN" "$OUT/family_snobol4.j"  -d "$OUT" 2>&1 | grep -v "Picked up\|proxy\|jwt"
java -jar "$JASMIN" "$OUT/family_icon.j"     -d "$OUT" 2>&1 | grep -v "Picked up\|proxy\|jwt"

# ── Run ───────────────────────────────────────────────────────────────────────
echo "Running..."
# Single JVM: family_icon.main() calls PARSE_CSV (SNOBOL4, reads stdin),
# then queries Prolog, then formats output. All three classes on same classpath.
RESULT=$(java -cp "$OUT" family_icon < family.csv 2>/dev/null)

if [ -f family.expected ]; then
    EXPECTED=$(cat family.expected)
    if [ "$RESULT" = "$EXPECTED" ]; then
        echo "M-SCRIP-DEMO ✅  output matches family.expected"
    else
        echo "M-SCRIP-DEMO ❌  output differs"
        diff <(echo "$EXPECTED") <(echo "$RESULT") | head -20
        exit 1
    fi
else
    echo "$RESULT"
    echo "(no family.expected to diff against -- output above)"
fi
