#!/bin/bash
# test/linker/net/run.sh — LP-4 acceptance test: M-LINK-NET-3
# Expected output: Hello, World
set -e

SCRIP=../../../src/driver/scrip
RUNTIME=../../../src/runtime/net
OUT=./out
mkdir -p "$OUT"

cp "$RUNTIME"/snobol4lib.dll "$OUT"/
cp "$RUNTIME"/snobol4run.dll "$OUT"/

echo "--- compiling greet_lib.sno ---"
"$SCRIP" -net greet_lib.sno > "$OUT"/greet_lib.il

echo "--- compiling greet_main.sno ---"
"$SCRIP" -net greet_main.sno > "$OUT"/greet_main.il

echo "--- assembling greet_lib.dll ---"
ilasm "$OUT"/greet_lib.il /dll /output:"$OUT"/greet_lib.dll

echo "--- assembling greet_main.exe ---"
ilasm "$OUT"/greet_main.il /exe /output:"$OUT"/greet_main.exe

echo "--- running ---"
RESULT=$(cd "$OUT" && mono greet_main.exe)

if [ "$RESULT" = "Hello, World" ]; then
    echo "M-LINK-NET-3 ✅  $RESULT"
else
    echo "M-LINK-NET-3 ❌  got: '$RESULT'"
    exit 1
fi
