#!/bin/bash
# run_rung36.sh — rung36_jcon JCON oracle corpus runner
# 75 tests from JCON test suite, ordered easy→hard.
# Corpus .icn files are pre-converted to semicolon-explicit form.
#


DRIVER="${1:-/tmp/scrip-cc}"
JASMIN="$(dirname "$0")/../../../src/backend/jvm/jasmin.jar"
CORPUS="$(dirname "$0")/corpus/rung36_jcon"
PASS=0; FAIL=0; XFAIL=0

for icn in "$CORPUS"/t*.icn; do
  base="${icn%.icn}"
  exp="$base.expected"
  [ -f "$exp" ] || continue

  if [ -f "$base.xfail" ]; then
    XFAIL=$((XFAIL+1))
    echo "XFAIL: $(basename $icn)"
    continue
  fi

  "$DRIVER" -jvm "$icn" -o /tmp/t36.j 2>/dev/null
  timeout 30 java -jar "$JASMIN" /tmp/t36.j -d /tmp/ 2>/dev/null
  cls=$(grep -m1 '\.class' /tmp/t36.j 2>/dev/null | awk '{print $NF}')

  if [ -z "$cls" ]; then
    FAIL=$((FAIL+1))
    echo "FAIL: $(basename $icn) [compile error]"
    continue
  fi

  stdin_f="$base.stdin"
  if [ -f "$stdin_f" ]; then
    got=$(timeout 10 java -cp /tmp/ "$cls" < "$stdin_f" 2>/dev/null)
  else
    got=$(timeout 10 java -cp /tmp/ "$cls" 2>/dev/null)
  fi

  want=$(cat "$exp")
  if [ "$got" = "$want" ]; then
    PASS=$((PASS+1))
    echo "PASS: $(basename $icn)"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $(basename $icn)"
    echo "  want: $(echo "$want" | head -3 | tr '\n' '|')"
    echo "  got:  $(echo "$got"  | head -3 | tr '\n' '|')"
  fi
done

echo "--- rung36: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
