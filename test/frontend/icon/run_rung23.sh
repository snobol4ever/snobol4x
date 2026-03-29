#!/bin/bash
# run_rung23.sh — rung23_table corpus runner
cd "$(dirname "$0")/../../.."
PASS=0; FAIL=0
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNG_DIR="${CORPUS_REPO:-$(cd "$SCRIPT_DIR/../../.." && pwd)/corpus}/programs/icon"
for icn in "$RUNG_DIR"/icon_rung23_table__t*.icn; do
  [ -f "$icn" ] || continue
  base="${icn%.icn}"; exp="$base.expected"; [ -f "$exp" ] || continue
  /tmp/scrip-cc -jvm "$icn" -o /tmp/t23.j 2>/dev/null
  timeout 30 java -jar src/backend/jvm/jasmin.jar /tmp/t23.j -d /tmp/ >/dev/null 2>&1
  cls=$(grep -m1 '\.class' /tmp/t23.j | awk '{print $NF}')
  got=$(timeout 5 java -cp /tmp/ "$cls" 2>/dev/null); want=$(cat "$exp")
  if [ "$got" = "$want" ]; then PASS=$((PASS+1)); echo "PASS: $(basename $icn)"
  else FAIL=$((FAIL+1)); echo "FAIL: $(basename $icn)"; echo "  want: $(echo "$want"|tr '\n' '|')"; echo "  got:  $(echo "$got"|tr '\n' '|')"; fi
done
echo "--- rung23: $PASS pass, $FAIL fail ---"
[ $FAIL -eq 0 ]
