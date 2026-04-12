#!/bin/bash
# run_rung24.sh — rung24_records corpus runner
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0; XFAIL=0
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNG_DIR="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon"
for icn in "$RUNG_DIR"/rung24_records_*.icn; do
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  [ -f "$base.xfail" ] && { XFAIL=$((XFAIL+1)); echo "XFAIL: $(basename $icn)"; continue; }
  ${1:-/tmp/scrip} -jvm "$icn" -o /tmp/t24.j 2>/dev/null
  timeout 30 java -jar src/backend/jasmin.jar /tmp/t24.j -d /tmp/ 2>/dev/null
  for rj in /tmp/*\$*.j; do [ -f "$rj" ] && timeout 30 java -jar src/backend/jasmin.jar "$rj" -d /tmp/ 2>/dev/null; done
  cls=$(grep -m1 '\.class' /tmp/t24.j | awk '{print $NF}')
  got=$(timeout 5 java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"; fi
done
echo "--- rung24: $PASS pass, $FAIL fail, $XFAIL xfail ---"
[ $FAIL -eq 0 ]
