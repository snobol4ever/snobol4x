#!/usr/bin/env bash
# run_corpus_icon.sh -- run icon_parser and icon_recognizer on all .icn files
# Usage: bash run_corpus_icon.sh [dir ...]
# Prints pass/empty/crash counts. Exits 1 if crash rate > 5%.

TIMEOUT=${TIMEOUT:-10}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PARSER_SRC="$REPO_ROOT/demo/scrip/icon_parser.icn"
RECOG_SRC="$REPO_ROOT/demo/scrip/icon_recognizer.icn"

TMP=$(mktemp -d)
icont -s -o "$TMP/icon_parser"     "$PARSER_SRC"  2>/dev/null || { echo "ERROR: icon_parser compile failed"; exit 2; }
icont -s -o "$TMP/icon_recognizer" "$RECOG_SRC"   2>/dev/null || { echo "ERROR: icon_recognizer compile failed"; exit 2; }

DIRS=("$@")
if [ ${#DIRS[@]} -eq 0 ]; then
  DIRS=(
    "$REPO_ROOT/test/frontend/icon"
    /home/claude/corpus/programs/icon
  )
fi

mapfile -t FILES < <(find "${DIRS[@]}" -name "*.icn" 2>/dev/null | sort)
TOTAL=${#FILES[@]}
P_PASS=0; P_EMPTY=0; P_CRASH=0
R_PASS=0; R_EMPTY=0; R_CRASH=0

for f in "${FILES[@]}"; do
  OUT=$(timeout "$TIMEOUT" "$TMP/icon_parser" < "$f" 2>/dev/null); code=$?
  if [ $code -eq 124 ] || [ $code -ne 0 ]; then
    ((P_CRASH++))
  elif [ -z "$OUT" ] || [ "$OUT" = '(compiland "")' ]; then
    ((P_EMPTY++))
  else
    ((P_PASS++))
  fi

  OUT=$(timeout "$TIMEOUT" "$TMP/icon_recognizer" < "$f" 2>/dev/null); code=$?
  if [ $code -eq 124 ] || [ $code -ne 0 ]; then
    ((R_CRASH++))
  elif [ -z "$OUT" ] || [ "$OUT" = '(compiland "")' ]; then
    ((R_EMPTY++))
  else
    ((R_PASS++))
  fi
done

rm -rf "$TMP"

echo "=== Icon corpus: parser + recognizer ==="
echo "Files:          $TOTAL"
echo "Parser:         pass=$P_PASS  empty=$P_EMPTY  crash/timeout=$P_CRASH"
echo "Recognizer:     pass=$R_PASS  empty=$R_EMPTY  crash/timeout=$R_CRASH"

MAX_CRASH=$(( TOTAL * 5 / 100 + 1 ))
if [ "$P_CRASH" -gt "$MAX_CRASH" ] || [ "$R_CRASH" -gt "$MAX_CRASH" ]; then
  echo "RESULT: FAIL (crash rate > 5%)"
  exit 1
fi
echo "RESULT: PASS"
