#!/bin/bash
# run_rung29.sh — rung29_builtins_type corpus runner (M-IJ-BUILTINS-TYPE)
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0; XFAIL=0
for icn in test/frontend/icon/corpus/rung29_builtins_type/t*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  /tmp/sno2c -jvm "$icn" -o /tmp/t29.j 2>/dev/null
  java -jar src/backend/jvm/jasmin.jar /tmp/t29.j -d /tmp/ 2>/dev/null
  cls=$(grep -m1 '\.class' /tmp/t29.j | awk '{print $NF}')
  stdin_f="$base.stdin"
  if [ -f "$stdin_f" ]; then
    got=$(java -cp /tmp/ "$cls" < "$stdin_f" 2>/dev/null)
  else
    got=$(java -cp /tmp/ "$cls" 2>/dev/null)
  fi
  want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"; fi
done
echo "--- rung29: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
