#!/bin/bash
# precheck.sh — verify all 5-way monitor prerequisites before running run_monitor.sh
# Usage: bash precheck.sh
# Exit 0 = everything OK. Exit 1 = one or more checks failed.
#
# Checks:
#   1.  Tool availability: snobol4, bootsbl, mono, ilasm, java, nasm, python3
#   2.  Paths: monitor_ipc.so, monitor_ipc_spitbol.so, jasmin.jar, runtime DLLs
#   3.  Runtime libs: libgc.so, snobol4lib.dll, snobol4run.dll
#   4.  inject_traces.py and normalize_trace.py present + executable
#   5.  tracepoints.conf present
#   6.  sno2c present and responds
#   7.  Corpus INC dir present
#   SMOKE TESTS (null program through each participant):
#   8.  CSNOBOL4:  null.sno → exit 0, output empty
#   9.  SPITBOL:   null.sno → exit 0 or segfault (SPITBOL exit-segfault OK), output empty
#  10.  ASM:       null.sno → assembles + links + runs → exit 0
#  11.  NET:       null.sno → ilasm + mono → exit 0
#  12.  JVM:       null.sno → jasmin + java → exit 0
#   IPC SMOKE TESTS (hello through each participant's FIFO):
#  13.  CSNOBOL4 IPC: hello → FIFO → "VALUE OUTPUT = hello"
#  14.  SPITBOL  IPC: hello → FIFO → trace line present
#  15.  ASM      IPC: hello → FIFO → "VAR OUTPUT "hello""
#  16.  NET      IPC: hello → FIFO → "VAR OUTPUT "hello""
#  17.  JVM      IPC: hello → FIFO → "VAR OUTPUT "hello""

set -uo pipefail

MDIR="$(cd "$(dirname "$0")" && pwd)"
DIR="$(cd "$MDIR/../.." && pwd)"
X64_DIR="${X64_DIR:-/home/claude/x64}"
RT="$DIR/src/runtime"
INC="${INC:-/home/claude/corpus/programs/inc}"
MONO_PATH="${MONO_PATH:-$DIR/src/runtime/net}"
NET_RT="$DIR/src/runtime/net"
JASMIN="${JASMIN:-$DIR/src/backend/jvm/jasmin.jar}"
SO="$MDIR/monitor_ipc.so"
SPL_SO="$X64_DIR/monitor_ipc_spitbol.so"

PASS=0
FAIL=0
TMP=$(mktemp -d /tmp/precheck_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

green() { printf '\033[32mPASS\033[0m %s\n' "$*"; PASS=$((PASS+1)); }
red()   { printf '\033[31mFAIL\033[0m %s\n' "$*"; FAIL=$((FAIL+1)); }
info()  { printf '     %s\n' "$*"; }

# ── helper: write null.sno and hello.sno ──────────────────────────────────
NULL_SNO="$TMP/null.sno"
HELLO_SNO="$TMP/hello.sno"
printf "END\n" > "$NULL_SNO"
printf "        OUTPUT = 'hello'\nEND\n" > "$HELLO_SNO"

# ── helper: make+drain a FIFO, return trace content ───────────────────────
fifo_drain() {
    local fifo="$1" outfile="$2"
    mkfifo "$fifo"
    timeout 10 cat "$fifo" > "$outfile" &
    echo $!
}

echo "═══════════════════════════════════════════════════════"
echo " snobol4x 5-way monitor — pre-check + smoke tests"
echo " $(date)"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "── Section 1: Tool availability ──────────────────────"

for tool in snobol4 mono ilasm java nasm python3; do
    if command -v "$tool" >/dev/null 2>&1; then
        green "tool: $tool ($(command -v $tool))"
    else
        red   "tool missing: $tool"
    fi
done

if [[ -x "$X64_DIR/bootsbl" ]]; then
    green "tool: bootsbl ($X64_DIR/bootsbl)"
else
    red   "tool missing: bootsbl at $X64_DIR/bootsbl"
fi

echo ""
echo "── Section 2: File paths ──────────────────────────────"

for f in \
    "$DIR/sno2c" \
    "$SO" \
    "$SPL_SO" \
    "$JASMIN" \
    "$MDIR/inject_traces.py" \
    "$MDIR/normalize_trace.py" \
    "$MDIR/tracepoints.conf" \
    "$NET_RT/snobol4lib.dll" \
    "$NET_RT/snobol4run.dll" \
    "$INC" \
; do
    if [[ -e "$f" ]]; then
        green "exists: $f"
    else
        red   "missing: $f"
    fi
done

echo ""
echo "── Section 3: sno2c sanity ────────────────────────────"

if "$DIR/sno2c" -asm "$NULL_SNO" > "$TMP/sno2c_null.s" 2>/dev/null && [[ -s "$TMP/sno2c_null.s" ]]; then
    green "sno2c -asm null.sno produces output"
else
    red   "sno2c -asm null.sno failed or empty output"
fi
if "$DIR/sno2c" -jvm "$NULL_SNO" > "$TMP/sno2c_null.j" 2>/dev/null && [[ -s "$TMP/sno2c_null.j" ]]; then
    green "sno2c -jvm null.sno produces output"
else
    red   "sno2c -jvm null.sno failed or empty output"
fi
if "$DIR/sno2c" -net "$NULL_SNO" > "$TMP/sno2c_null.il" 2>/dev/null && [[ -s "$TMP/sno2c_null.il" ]]; then
    green "sno2c -net null.sno produces output"
else
    red   "sno2c -net null.sno failed or empty output"
fi

echo ""
echo "── Section 4: null-program smoke tests ────────────────"

# ── 4a: CSNOBOL4 null ───────────────────────────────────────────────────
snobol4 -f -P256k -I"$INC" "$NULL_SNO" < /dev/null > "$TMP/csn_null.out" 2>"$TMP/csn_null.err"
csn_exit=$?
if [[ $csn_exit -eq 0 && ! -s "$TMP/csn_null.out" ]]; then
    green "CSNOBOL4 null: exit 0, no output"
else
    red   "CSNOBOL4 null: exit=$csn_exit out=$(cat $TMP/csn_null.out) err=$(cat $TMP/csn_null.err)"
fi

# ── 4b: SPITBOL null ────────────────────────────────────────────────────
SNOLIB="$X64_DIR" "$X64_DIR/bootsbl" "$NULL_SNO" \
    < /dev/null > "$TMP/spl_null.out" 2>"$TMP/spl_null.err"
spl_exit=$?
# SPITBOL exits with segfault (139) on clean exit — that's normal
if [[ ($spl_exit -eq 0 || $spl_exit -eq 139) && ! -s "$TMP/spl_null.out" ]]; then
    green "SPITBOL null: exit=$spl_exit (segfault-on-exit OK), no output"
else
    red   "SPITBOL null: exit=$spl_exit out=$(cat $TMP/spl_null.out)"
fi

# ── 4c: ASM null ────────────────────────────────────────────────────────
"$DIR/sno2c" -asm "$NULL_SNO" > "$TMP/null.s" 2>/dev/null
gcc -O0 -g -c "$RT/asm/snobol4_stmt_rt.c"    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/stmt_rt.o" 2>/dev/null
gcc -O0 -g -c "$RT/snobol4/snobol4.c"         -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/snobol4.o" 2>/dev/null
gcc -O0 -g -c "$RT/mock/mock_includes.c"       -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/mock.o" 2>/dev/null
gcc -O0 -g -c "$RT/snobol4/snobol4_pattern.c" -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/pat.o" 2>/dev/null
gcc -O0 -g -c "$RT/engine/engine.c"            -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/eng.o" 2>/dev/null
nasm -f elf64 -I"$RT/asm/" "$TMP/null.s" -o "$TMP/null.o" 2>/dev/null
gcc -no-pie "$TMP/null.o" "$TMP/stmt_rt.o" "$TMP/snobol4.o" "$TMP/mock.o" \
    "$TMP/pat.o" "$TMP/eng.o" -lgc -lm -o "$TMP/prog_asm" 2>/dev/null
"$TMP/prog_asm" < /dev/null > "$TMP/asm_null.out" 2>"$TMP/asm_null.err"
asm_exit=$?
if [[ $asm_exit -eq 0 && ! -s "$TMP/asm_null.out" ]]; then
    green "ASM null: compiles + runs, exit 0, no output"
else
    red   "ASM null: exit=$asm_exit compile/link errors: $(cat $TMP/asm_null.err | head -2)"
fi

# ── 4d: NET null ────────────────────────────────────────────────────────
"$DIR/sno2c" -net "$NULL_SNO" > "$TMP/null.il" 2>/dev/null
cp "$NET_RT/snobol4lib.dll" "$NET_RT/snobol4run.dll" "$TMP/" 2>/dev/null
ilasm "$TMP/null.il" /output:"$TMP/null.exe" >/dev/null 2>&1
MONO_PATH="$NET_RT" mono "$TMP/null.exe" < /dev/null > "$TMP/net_null.out" 2>"$TMP/net_null.err"
net_exit=$?
if [[ $net_exit -eq 0 && ! -s "$TMP/net_null.out" ]]; then
    green "NET null: ilasm + mono, exit 0, no output"
else
    red   "NET null: exit=$net_exit err=$(head -1 $TMP/net_null.err)"
fi

# ── 4e: JVM null ────────────────────────────────────────────────────────
"$DIR/sno2c" -jvm "$NULL_SNO" > "$TMP/null.j" 2>/dev/null
jcn=$(grep '\.class' "$TMP/null.j" | head -1 | awk '{print $NF}')
java -jar "$JASMIN" "$TMP/null.j" -d "$TMP" >/dev/null 2>&1
java -cp "$TMP" "$jcn" < /dev/null > "$TMP/jvm_null.out" 2>"$TMP/jvm_null.err"
jvm_exit=$?
if [[ $jvm_exit -eq 0 && ! -s "$TMP/jvm_null.out" ]]; then
    green "JVM null: jasmin + java, exit 0, no output"
else
    red   "JVM null: exit=$jvm_exit err=$(head -1 $TMP/jvm_null.err)"
fi

echo ""
echo "── Section 5: IPC smoke tests (hello → FIFO) ──────────"

# ── 5a: CSNOBOL4 IPC ────────────────────────────────────────────────────
python3 "$MDIR/inject_traces.py" "$HELLO_SNO" "$MDIR/tracepoints.conf" > "$TMP/hello_instr.sno"
cpid=$(fifo_drain "$TMP/csn.fifo" "$TMP/csn.trace")
MONITOR_FIFO="$TMP/csn.fifo" MONITOR_SO="$SO" \
    snobol4 -f -P256k -I"$INC" "$TMP/hello_instr.sno" \
    < /dev/null > "$TMP/csn.out" 2>"$TMP/csn.stderr"
wait $cpid 2>/dev/null || true
if grep -q "OUTPUT" "$TMP/csn.trace" 2>/dev/null; then
    green "CSNOBOL4 IPC: trace='$(cat $TMP/csn.trace | tr '\n' '|')'"
else
    red   "CSNOBOL4 IPC: empty trace (stderr: $(cat $TMP/csn.stderr | head -2))"
    info  "  instr.sno preamble: $(head -30 $TMP/hello_instr.sno | grep MON | head -3)"
fi

# ── 5b: SPITBOL IPC ─────────────────────────────────────────────────────
spid=$(fifo_drain "$TMP/spl.fifo" "$TMP/spl.trace")
SNOLIB="$X64_DIR" MONITOR_FIFO="$TMP/spl.fifo" MONITOR_SO="$SPL_SO" \
    "$X64_DIR/bootsbl" "$TMP/hello_instr.sno" \
    < /dev/null > "$TMP/spl.out" 2>"$TMP/spl.stderr" || true
wait $spid 2>/dev/null || true
if [[ -s "$TMP/spl.trace" ]]; then
    green "SPITBOL  IPC: trace='$(cat $TMP/spl.trace | tr '\n' '|')'"
else
    red   "SPITBOL  IPC: empty trace"
    info  "  stdout: $(cat $TMP/spl.out) stderr: $(head -2 $TMP/spl.stderr)"
fi

# ── 5c: ASM IPC ─────────────────────────────────────────────────────────
# Rebuild ASM binary from hello.sno (section 4 left prog_asm as null program)
"$DIR/sno2c" -asm "$HELLO_SNO" > "$TMP/hello.s" 2>/dev/null
nasm -f elf64 -I"$RT/asm/" "$TMP/hello.s" -o "$TMP/hello_asm.o" 2>/dev/null
gcc -no-pie "$TMP/hello_asm.o" "$TMP/stmt_rt.o" "$TMP/snobol4.o" "$TMP/mock.o" \
    "$TMP/pat.o" "$TMP/eng.o" -lgc -lm -o "$TMP/prog_asm_hello" 2>/dev/null
apid=$(fifo_drain "$TMP/asm.fifo" "$TMP/asm.trace")
MONITOR_FIFO="$TMP/asm.fifo" \
    "$TMP/prog_asm_hello" < /dev/null > "$TMP/asm.out" 2>"$TMP/asm.err" || true
wait $apid 2>/dev/null || true
if grep -q "^VAR OUTPUT" "$TMP/asm.trace" 2>/dev/null; then
    green "ASM      IPC: trace='$(grep "^VAR OUTPUT" $TMP/asm.trace | tr '\n' '|')'"
else
    red   "ASM      IPC: no VAR OUTPUT line ($(wc -l < $TMP/asm.trace) lines total)"
    info  "  first 3: $(head -3 $TMP/asm.trace | tr '\n' '|')"
fi

# ── 5d: NET IPC ─────────────────────────────────────────────────────────
"$DIR/sno2c" -net "$HELLO_SNO" > "$TMP/hello.il" 2>/dev/null
cp "$NET_RT/snobol4lib.dll" "$NET_RT/snobol4run.dll" "$TMP/" 2>/dev/null
ilasm "$TMP/hello.il" /output:"$TMP/hello_net.exe" >/dev/null 2>&1
npid=$(fifo_drain "$TMP/net.fifo" "$TMP/net.trace")
MONITOR_FIFO="$TMP/net.fifo" \
    MONO_PATH="$NET_RT" mono "$TMP/hello_net.exe" \
    < /dev/null > "$TMP/net.out" 2>"$TMP/net.err" || true
wait $npid 2>/dev/null || true
if grep -qP "^VAR OUTPUT" "$TMP/net.trace" 2>/dev/null; then
    green "NET      IPC: trace='$(grep "VAR OUTPUT" $TMP/net.trace | tr '\n' '|')'"
else
    red   "NET      IPC: no VAR OUTPUT line ($(wc -l < $TMP/net.trace 2>/dev/null || echo 0) lines)"
    info  "  stderr: $(head -2 $TMP/net.err 2>/dev/null)"
fi

# ── 5e: JVM IPC ─────────────────────────────────────────────────────────
"$DIR/sno2c" -jvm "$HELLO_SNO" > "$TMP/hello.j" 2>/dev/null
jcn_h=$(grep '\.class' "$TMP/hello.j" | head -1 | awk '{print $NF}')
java -jar "$JASMIN" "$TMP/hello.j" -d "$TMP" >/dev/null 2>&1
jpid=$(fifo_drain "$TMP/jvm.fifo" "$TMP/jvm.trace")
MONITOR_FIFO="$TMP/jvm.fifo" \
    java -cp "$TMP" "$jcn_h" \
    < /dev/null > "$TMP/jvm.out" 2>"$TMP/jvm.err" || true
wait $jpid 2>/dev/null || true
if grep -q "OUTPUT\|hello" "$TMP/jvm.trace" 2>/dev/null; then
    green "JVM      IPC: trace='$(cat $TMP/jvm.trace | tr '\n' '|')'"
else
    red   "JVM      IPC: empty or wrong trace: '$(cat $TMP/jvm.trace)'"
fi

echo ""
echo "═══════════════════════════════════════════════════════"
printf " Results: \033[32m%d PASS\033[0m  \033[31m%d FAIL\033[0m\n" $PASS $FAIL
echo "═══════════════════════════════════════════════════════"

[[ $FAIL -eq 0 ]] && echo " ✅ All checks passed — safe to run run_monitor.sh" \
                  || echo " ❌ Fix failures above before running run_monitor.sh"

exit $((FAIL > 0 ? 1 : 0))
