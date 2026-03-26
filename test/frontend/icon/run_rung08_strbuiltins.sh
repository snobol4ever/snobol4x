#!/bin/bash
# run_rung08_strbuiltins.sh — rung08_strbuiltins corpus runner
cd "$(dirname "$0")/../../.."
DRIVER=${1:-/tmp/icon_driver}
PASS=0; FAIL=0; XFAIL=0
for icn in test/frontend/icon/corpus/rung08_strbuiltins/t*.icn; do
  [ -f "$icn" ] || continue
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  $DRIVER -jvm "$icn" -o /tmp/t08_x.j 2>/dev/null
  java -jar src/backend/jvm/jasmin.jar /tmp/t08_x.j -d /tmp/ >/dev/null 2>&1
  cls=$(grep -m1 '\.class' /tmp/t08_x.j | awk '{print $NF}')
  got=$(java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ "$got" = "$want" ]; then
    PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else
    FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"
    echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"
  fi
done
echo "--- rung08_strbuiltins: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
