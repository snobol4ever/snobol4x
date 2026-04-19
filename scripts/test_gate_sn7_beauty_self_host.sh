#!/usr/bin/env bash
# scripts/test_gate_sn7_beauty_self_host.sh — SN-7 gate:
# every beauty_*_driver.sno in corpus, under --ir-run, --sm-run, --jit-run,
# diff=0 vs its pre-baked .ref file (SPITBOL ground truth where valid; some
# drivers have .ref files that reflect correct behavior SPITBOL itself fails
# on — see RULES.md on .ref authority).
#
# Self-contained per RULES.md: paths derived from $0; no env deps.

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${SCRIP:-$HERE/../scrip}"
CORPUS="${CORPUS:-/home/claude/corpus}"
BEAUTY="$CORPUS/programs/snobol4/beauty"
TIMEOUT="${TIMEOUT:-30}"

if [ ! -x "$SCRIP" ]; then
    echo "SKIP scrip not built at $SCRIP"
    exit 0
fi
if [ ! -d "$BEAUTY" ]; then
    echo "SKIP corpus not populated at $CORPUS"
    exit 0
fi

PASS=0
FAIL=0
FAILS=""

for sno in "$BEAUTY"/beauty_*_driver.sno; do
    [ ! -f "$sno" ] && continue
    name=$(basename "$sno" .sno)
    ref="$BEAUTY/${name}.ref"
    [ ! -f "$ref" ] && continue
    for mode in --ir-run --sm-run --jit-run; do
        got=$(SNO_LIB="$BEAUTY" timeout "$TIMEOUT" "$SCRIP" $mode "$sno" < /dev/null 2>/dev/null || true)
        if diff <(printf '%s\n' "$got") "$ref" > /dev/null 2>&1; then
            PASS=$((PASS + 1))
        else
            FAIL=$((FAIL + 1))
            FAILS="$FAILS $name($mode)"
        fi
    done
done

echo "PASS=$PASS FAIL=$FAIL"
[ -n "$FAILS" ] && echo "FAILS:$FAILS"
[ "$FAIL" -eq 0 ]
