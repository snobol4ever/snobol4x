#!/bin/bash
# test/linker/net/ancestor/run.sh — M-LINK-NET-5 acceptance test
# Cross-language: SNOBOL4 → Prolog ANCESTOR via .NET Byrd-box ABI.
# Uses GENERATED ancestor.il (from scrip -pl -net ancestor.pl).
# Expected output: ann
set -e

SCRIP=../../../../src/driver/scrip
RUNTIME=../../../../src/runtime/net
OUT=./out
mkdir -p "$OUT"

cp "$RUNTIME"/snobol4lib.dll "$OUT"/
cp "$RUNTIME"/snobol4run.dll "$OUT"/

echo "--- generating ancestor.il from ancestor.pl ---"
"$SCRIP" -pl -net ancestor.pl > "$OUT"/ancestor.il

echo "--- assembling ancestor.dll ---"
ilasm "$OUT"/ancestor.il /dll /output:"$OUT"/ancestor.dll

echo "--- compiling ancestor_main.sno ---"
"$SCRIP" -net ancestor_main.sno > "$OUT"/ancestor_main.il

echo "--- assembling ancestor_main.exe ---"
ilasm "$OUT"/ancestor_main.il /exe /output:"$OUT"/ancestor_main.exe

echo "--- running ---"
RESULT=$(cd "$OUT" && mono ancestor_main.exe)

if [ "$RESULT" = "ann" ]; then
    echo "M-LINK-NET-5 ✅  $RESULT"
else
    echo "M-LINK-NET-5 ❌  got: '$RESULT'"
    exit 1
fi
