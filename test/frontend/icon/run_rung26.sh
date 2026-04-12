#!/bin/bash
# run_rung26.sh — rung26_pow corpus runner (M-IJ-POW)
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0; XFAIL=0
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNG_DIR="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon"
for icn in "$RUNG_DIR"/rung26_pow_*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  ${1:-/tmp/scrip} -jvm "$icn" -o /tmp/t26.j 2>/dev/null
  timeout 30 java -jar src/backend/jasmin.jar /tmp/t26.j -d /tmp/ 2>/dev/null
  cls=$(grep -m1 '\.class' /tmp/t26.j | awk '{print $NF}')
  got=$(timeout 5 java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"; fi
done
echo "--- rung26: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
