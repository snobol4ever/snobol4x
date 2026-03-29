#!/bin/bash
# run_rung27.sh — rung27_read corpus runner (M-IJ-READ)
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0; XFAIL=0
for icn in test/frontend/icon/corpus/rung27_read/t*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  /tmp/scrip-cc -jvm "$icn" -o /tmp/t27.j 2>/dev/null
  timeout 30 java -jar src/backend/jvm/jasmin.jar /tmp/t27.j -d /tmp/ 2>/dev/null
  cls=$(grep -m1 '\.class' /tmp/t27.j | awk '{print $NF}')
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
echo "--- rung27: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
