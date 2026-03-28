#!/usr/bin/env bash
# run_corpus_prolog.sh -- run prolog_parser and prolog_recognizer on all .pro/.pl files
# Usage: bash run_corpus_prolog.sh [dir ...]
# Prints pass/empty/crash counts. Exits 1 if crash rate > 5%.

TIMEOUT=${TIMEOUT:-10}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PARSER_SRC="$REPO_ROOT/demo/scrip/prolog_parser.pro"
RECOG_SRC="$REPO_ROOT/demo/scrip/prolog_recognizer.pro"

DIRS=("$@")
if [ ${#DIRS[@]} -eq 0 ]; then
  DIRS=(
    "$REPO_ROOT/test/frontend/prolog"
    /home/claude/snobol4corpus/programs/prolog
  )
fi

mapfile -t FILES < <(find "${DIRS[@]}" -name "*.pro" -o -name "*.pl" 2>/dev/null | sort)
TOTAL=${#FILES[@]}
if [ "$TOTAL" -eq 0 ]; then
  echo "No Prolog files found in: ${DIRS[*]}"
  exit 0
fi

P_PASS=0; P_EMPTY=0; P_CRASH=0
R_PASS=0; R_EMPTY=0; R_CRASH=0

for f in "${FILES[@]}"; do
  OUT=$(timeout "$TIMEOUT" swipl -q -f "$PARSER_SRC" -t halt < "$f" 2>/dev/null); code=$?
  if [ $code -eq 124 ] || [ $code -ne 0 ]; then
    ((P_CRASH++))
  elif [ -z "$OUT" ]; then
    ((P_EMPTY++))
  else
    ((P_PASS++))
  fi

  OUT=$(timeout "$TIMEOUT" swipl -q -f "$RECOG_SRC" -t halt < "$f" 2>/dev/null); code=$?
  if [ $code -eq 124 ] || [ $code -ne 0 ]; then
    ((R_CRASH++))
  elif [ -z "$OUT" ] || [ "$OUT" = '(compiland)' ]; then
    ((R_EMPTY++))
  else
    ((R_PASS++))
  fi
done

echo "=== Prolog corpus: parser + recognizer ==="
echo "Files:          $TOTAL"
echo "Parser:         pass=$P_PASS  empty=$P_EMPTY  crash/timeout=$P_CRASH"
echo "Recognizer:     pass=$R_PASS  empty=$R_EMPTY  crash/timeout=$R_CRASH"

MAX_CRASH=$(( TOTAL * 5 / 100 + 1 ))
if [ "$P_CRASH" -gt "$MAX_CRASH" ] || [ "$R_CRASH" -gt "$MAX_CRASH" ]; then
  echo "RESULT: FAIL (crash rate > 5%)"
  exit 1
fi
echo "RESULT: PASS"
