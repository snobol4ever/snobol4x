#!/bin/bash
# run_rung31.sh — rung31_sort corpus runner (M-IJ-SORT)
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0; XFAIL=0
for icn in test/frontend/icon/corpus/rung31_sort/t*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  /tmp/scrip-cc -jvm "$icn" -o /tmp/t31.j 2>/dev/null
  # compile main class
  timeout 30 java -jar src/backend/jvm/jasmin.jar /tmp/t31.j -d /tmp/ 2>/dev/null
  # compile any record inner classes
  for rj in /tmp/*\$*.j; do [ -f "$rj" ] && timeout 30 java -jar src/backend/jvm/jasmin.jar "$rj" -d /tmp/ 2>/dev/null; done
  cls=$(grep -m1 '\.class' /tmp/t31.j | awk '{print $NF}')
  got=$(timeout 5 java -cp /tmp/ "$cls" 2>/dev/null)
  want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"; fi
done
echo "--- rung31: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
