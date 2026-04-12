#!/bin/bash
# run_rung35_table_str.sh — rung35_table_str corpus runner
cd "$(dirname "$0")/../../.."
DRIVER=${1:-/tmp/scrip}
PASS=0; FAIL=0; XFAIL=0
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNG_DIR="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon"
for icn in "$RUNG_DIR"/rung35_table_str_*.icn; do
  [ -f "$icn" ] || continue
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  xfail_file="$base.xfail"
  $DRIVER -jvm "$icn" -o /tmp/t35_table_.j 2>/dev/null
  timeout 30 java -jar src/backend/jasmin.jar /tmp/t35_table_.j -d /tmp/ >/dev/null 2>&1
  cls=$(grep -m1 '\.class' /tmp/t35_table_.j | awk '{print $NF}')
  got=$(timeout 5 java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ -f "$xfail_file" ]; then
    XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"
  elif [ "$got" = "$want" ]; then
    PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else
    FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"
    echo "  want: $(echo "$want"|tr '\n' '|')"
    echo "  got:  $(echo "$got"|tr '\n' '|')"
  fi
done
echo "--- rung35_table_str: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
