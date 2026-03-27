#!/bin/bash
# run_bench_rung36.sh — Icon JVM benchmark/oracle harness for rung36_jcon
# Usage: bash run_bench_rung36.sh [/path/to/icon_driver]
#
# Runs all 75 rung36_jcon tests. .xfail tests are run but shown as XFAIL/XPASS.
# [B] marks benchmark-class tests (non-trivial programs from IPL/JCON):
#   t01 primes, t27 queens, t28 genqueen, t39 concord,
#   t54 sieve, t66 cxprimes, t70 sorting

DRIVER="${1:-/tmp/icon_driver}"
JASMIN="$(dirname $0)/../../../src/backend/jvm/jasmin.jar"
CORPUS="$(dirname $0)/corpus/rung36_jcon"
TMPJ=/tmp/icon_bench_$$.j

BENCH_TESTS="t01 t27 t28 t39 t54 t66 t70"

pass=0; wo=0; ve=0; ce=0; xpass=0; xfail=0; total=0

for icn in "$CORPUS"/t*.icn; do
    base="${icn%.icn}"
    name=$(basename "$base")
    [ -f "${base}.expected" ] || continue
    total=$((total+1))

    is_xfail=0; [ -f "${base}.xfail" ] && is_xfail=1

    tag=""
    for b in $BENCH_TESTS; do
        [[ "$name" == ${b}_* ]] && tag=" [B]" && break
    done

    "$DRIVER" -jvm "$icn" -o "$TMPJ" 2>/dev/null
    cls=$(grep -m1 '\.class' "$TMPJ" 2>/dev/null | awk '{print $NF}')
    if [ -z "$cls" ]; then
        [ $is_xfail -eq 1 ] && { printf "XFAIL%-26s (CE)%s\n" " $name" "$tag"; xfail=$((xfail+1)); } || { printf "CE   %-26s%s\n" "$name" "$tag"; ce=$((ce+1)); }
        continue
    fi
    asm_err=$(java -jar "$JASMIN" "$TMPJ" -d /tmp/ 2>&1 | grep -v "Generated\|JAVA_TOOL")
    if [ -n "$asm_err" ]; then
        [ $is_xfail -eq 1 ] && { printf "XFAIL%-26s (CE)%s\n" " $name" "$tag"; xfail=$((xfail+1)); } || { printf "CE   %-26s%s\n" "$name" "$tag"; ce=$((ce+1)); }
        continue
    fi
    stdin_f="${base}.stdin"
    if [ -f "$stdin_f" ]; then out=$(timeout 15 java -cp /tmp "$cls" < "$stdin_f" 2>&1 | grep -v JAVA_TOOL)
    else out=$(timeout 15 java -cp /tmp "$cls" 2>&1 | grep -v JAVA_TOOL); fi

    if echo "$out" | grep -q "VerifyError\|LinkageError\|ClassFormatError\|Unable to initialize"; then
        [ $is_xfail -eq 1 ] && { printf "XFAIL%-26s (VE)%s\n" " $name" "$tag"; xfail=$((xfail+1)); } || { printf "VE   %-26s%s\n" "$name" "$tag"; ve=$((ve+1)); }
    elif [ "$out" = "$(cat "${base}.expected")" ]; then
        [ $is_xfail -eq 1 ] && { printf "XPASS%-26s%s\n" " $name" "$tag"; xpass=$((xpass+1)); } || { printf "PASS %-26s%s\n" "$name" "$tag"; pass=$((pass+1)); }
    else
        [ $is_xfail -eq 1 ] && { printf "XFAIL%-26s (WO)%s\n" " $name" "$tag"; xfail=$((xfail+1)); } || { printf "WO   %-26s%s\n" "$name" "$tag"; wo=$((wo+1)); }
    fi
done

rm -f "$TMPJ"
echo ""
echo "--- rung36_jcon ($total tests): PASS=$pass WO=$wo VE=$ve CE=$ce | XPASS=$xpass XFAIL=$xfail"
echo "--- [B]=benchmark-class  XPASS=unexpected pass (remove .xfail)"
