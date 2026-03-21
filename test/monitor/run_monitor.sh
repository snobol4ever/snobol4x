#!/bin/bash
# run_monitor.sh <sno_file> [tracepoints_conf]
# Five-way IPC monitor: CSNOBOL4 + SPITBOL + ASM + JVM + NET
# Exit 0 = all backends match oracle. Exit 1 = divergence.
set -euo pipefail

SNO=${1:?Usage: run_monitor.sh <sno_file> [tracepoints_conf]}
CONF=${2:-$(dirname "$0")/tracepoints.conf}
MDIR=$(cd "$(dirname "$0")" && pwd)
DIR=$(cd "$MDIR/../.." && pwd)
X64_DIR="${X64_DIR:-/home/claude/x64}"
RT=$DIR/src/runtime
INC=/home/claude/snobol4corpus/programs/inc
SO=$MDIR/monitor_ipc.so
TMP=$(mktemp -d /tmp/monitor_XXXXXX)
trap 'rm -rf "$TMP"' EXIT
SNO_BASE=$(basename "$SNO" .sno)

# Step 1: inject traces
python3 "$MDIR/inject_traces.py" "$SNO" "$CONF" > "$TMP/instr.sno"

# Step 2: FIFOs + collectors
for p in csn spl asm jvm net; do
    mkfifo "$TMP/$p.fifo"
    cat "$TMP/$p.fifo" > "$TMP/$p.trace" &
done

# Step 3: CSNOBOL4
MONITOR_FIFO="$TMP/csn.fifo" MONITOR_SO="$SO" \
    snobol4 -f -P256k -I"$INC" "$TMP/instr.sno" \
    < /dev/null > "$TMP/csn.out" 2>"$TMP/csn.stderr" || true

# Step 4: SPITBOL
SNOLIB="$X64_DIR" MONITOR_FIFO="$TMP/spl.fifo" \
    "$X64_DIR/bootsbl" "$TMP/instr.sno" \
    < /dev/null > "$TMP/spl.out" 2>"$TMP/spl.stderr" || true

# Step 5: ASM
gcc -O0 -g -c "$RT/asm/snobol4_stmt_rt.c"    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/stmt_rt.o"
gcc -O0 -g -c "$RT/snobol4/snobol4.c"         -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/snobol4.o"
gcc -O0 -g -c "$RT/mock/mock_includes.c"       -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/mock.o"
gcc -O0 -g -c "$RT/snobol4/snobol4_pattern.c" -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/pat.o"
gcc -O0 -g -c "$RT/engine/engine.c"            -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w -o "$TMP/eng.o"
"$DIR/sno2c" -asm "$SNO" > "$TMP/prog.s" 2>/dev/null
nasm -f elf64 -I"$RT/asm/" "$TMP/prog.s" -o "$TMP/prog.o" 2>/dev/null
gcc -no-pie "$TMP/prog.o" "$TMP/stmt_rt.o" "$TMP/snobol4.o" "$TMP/mock.o" \
    "$TMP/pat.o" "$TMP/eng.o" -lgc -lm -o "$TMP/prog_asm" 2>/dev/null
MONITOR_FIFO="$TMP/asm.fifo" \
    "$TMP/prog_asm" < /dev/null > "$TMP/asm.out" 2>"$TMP/asm.stderr" || true

# Step 6: NET
SNO2C_NET="${SNO2C_NET:-/home/claude/sno2c_net}"
NET_CACHE="${NET_CACHE:-/tmp/snobol4x_net_cache}"
mkdir -p "$NET_CACHE"
for dll in snobol4lib.dll snobol4run.dll; do
    [[ -f "$DIR/src/runtime/net/$dll" ]] && cp "$DIR/src/runtime/net/$dll" "$NET_CACHE/" 2>/dev/null || true
done
base="$(basename "$SNO" .sno)"; dh="$(echo "$SNO"|md5sum|cut -c1-8)"
il="$NET_CACHE/${base}_${dh}.il"; exe="$NET_CACHE/${base}_${dh}.exe"
stamp="$NET_CACHE/${base}_${dh}.stamp"
"$SNO2C_NET" -net "$SNO" > "$il" 2>/dev/null
il_md5="$(md5sum "$il"|cut -d' ' -f1)"
if [[ "$(cat "$stamp" 2>/dev/null)" != "$il_md5" ]] || [[ ! -f "$exe" ]]; then
    ilasm "$il" /output:"$exe" >/dev/null 2>&1 && echo "$il_md5" > "$stamp"
fi
MONITOR_FIFO="$TMP/net.fifo" \
    mono "$exe" < /dev/null > "$TMP/net.out" 2>"$TMP/net.stderr" || true

# Step 7: JVM
SNO2C_JVM="${SNO2C_JVM:-/home/claude/sno2c_jvm}"
JASMIN="${JASMIN:-$DIR/src/backend/jvm/jasmin.jar}"
JVM_CACHE="${JVM_CACHE:-/tmp/snobol4x_jvm_cache}"
mkdir -p "$JVM_CACHE"
jfile="$JVM_CACHE/${base}_${dh}.j"; jstamp="$JVM_CACHE/${base}_${dh}.jstamp"
"$SNO2C_JVM" -jvm "$SNO" > "$jfile" 2>/dev/null
classname=$(grep '\.class' "$jfile" | head -1 | awk '{print $NF}')
j_md5="$(md5sum "$jfile"|cut -d' ' -f1)"
if [[ "$(cat "$jstamp" 2>/dev/null)" != "$j_md5" ]] || [[ ! -f "$JVM_CACHE/${classname}.class" ]]; then
    java -jar "$JASMIN" "$jfile" -d "$JVM_CACHE" >/dev/null 2>&1 && echo "$j_md5" > "$jstamp"
fi
MONITOR_FIFO="$TMP/jvm.fifo" \
    java -cp "$JVM_CACHE" "$classname" < /dev/null > "$TMP/jvm.out" 2>"$TMP/jvm.stderr" || true

# Step 8: wait for collectors
wait 2>/dev/null || true

# Step 9: normalize
python3 "$MDIR/normalize_trace.py" "$CONF" \
    "$TMP/csn.trace" "$TMP/spl.trace" \
    "$TMP/asm.trace" "$TMP/jvm.trace" "$TMP/net.trace" \
    "$TMP/csn.norm"  "$TMP/spl.norm"  \
    "$TMP/asm.norm"  "$TMP/jvm.norm"  "$TMP/net.norm"

[ ! -s "$TMP/csn.norm" ] && echo "WARN [csnobol4] empty trace for $SNO_BASE"

# Step 10: diff
FAIL=0
for B in asm jvm net; do
    DIFF=$(diff "$TMP/csn.norm" "$TMP/$B.norm" || true)
    if [ -z "$DIFF" ]; then echo "PASS [$B] $SNO_BASE"
    else echo "FAIL [$B] $SNO_BASE"; echo "$DIFF" | head -10; FAIL=1; fi
done
ODIFF=$(diff "$TMP/csn.norm" "$TMP/spl.norm" || true)
[ -n "$ODIFF" ] && echo "ORACLE-DIFF [csn vs spl] $SNO_BASE" && echo "$ODIFF" | head -5

exit $FAIL
