#!/bin/bash
# run_rung12_strrelop_size.sh — rung12_strrelop_size corpus runner
cd "$(dirname "$0")/../../.."
DRIVER=${1:-/tmp/icon_driver}
PASS=0; FAIL=0; XFAIL=0
for icn in test/frontend/icon/corpus/rung12_strrelop_size/t*.icn; do
  [ -f "$icn" ] || continue
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  $DRIVER -jvm "$icn" -o /tmp/t12_x.j 2>/dev/null
  java -jar src/backend/jvm/jasmin.jar /tmp/t12_x.j -d /tmp/ >/dev/null 2>&1
  cls=$(grep -m1 '\.class' /tmp/t12_x.j | awk '{print $NF}')
  got=$(java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ "$got" = "$want" ]; then
    PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else
    FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"
    echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"
  fi
done
echo "--- rung12_strrelop_size: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
