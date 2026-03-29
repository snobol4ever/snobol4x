#!/bin/bash
# run_rung28.sh — rung28_builtins_str corpus runner (M-IJ-BUILTINS-STR)
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0; XFAIL=0
for icn in test/frontend/icon/corpus/rung28_builtins_str/t*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  /tmp/scrip-cc -jvm "$icn" -o /tmp/t28.j 2>/dev/null
  timeout 30 java -jar src/backend/jvm/jasmin.jar /tmp/t28.j -d /tmp/ 2>/dev/null
  cls=$(grep -m1 '\.class' /tmp/t28.j | awk '{print $NF}')
  stdin_f="$base.stdin"
  if [ -f "$stdin_f" ]; then
    got=$(timeout 5 java -cp /tmp/ "$cls" < "$stdin_f" 2>/dev/null)
  else
    got=$(timeout 5 java -cp /tmp/ "$cls" 2>/dev/null)
  fi
  want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"; fi
done
echo "--- rung28: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
