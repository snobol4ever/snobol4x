#!/bin/bash
# run_rung22.sh — rung22_lists corpus runner
set -e
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0
for icn in test/frontend/icon/corpus/rung22_lists/t*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  /tmp/sno2c -jvm "$icn" -o /tmp/t22.j 2>/dev/null
  java -jar src/backend/jvm/jasmin.jar /tmp/t22.j -d /tmp/ 2>/dev/null
  cls=$(grep -m1 '\.class' /tmp/t22.j | awk '{print $NF}')
  got=$(java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; fi
done
echo "rung22: $PASS pass, $FAIL fail"
[ $FAIL -eq 0 ]
