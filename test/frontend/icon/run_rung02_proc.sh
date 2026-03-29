#!/bin/bash
# run_rung02_proc.sh — rung02_proc corpus runner
cd "$(dirname "$0")/../../.."
DRIVER=${1:-/tmp/scrip-cc}
PASS=0; FAIL=0; XFAIL=0
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNG_DIR="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon/rung02_proc"
for icn in "$RUNG_DIR"/t*.icn; do
  [ -f "$icn" ] || continue
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  xfail_file="$base.xfail"
  $DRIVER -jvm "$icn" -o /tmp/t02_proc.j 2>/dev/null
  timeout 30 java -jar src/backend/jvm/jasmin.jar /tmp/t02_proc.j -d /tmp/ >/dev/null 2>&1
  cls=$(grep -m1 '\.class' /tmp/t02_proc.j | awk '{print $NF}')
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
echo "--- rung02_proc: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
