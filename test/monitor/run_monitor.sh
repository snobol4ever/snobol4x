#!/bin/bash
# run_monitor.sh <sno_file> [tracepoints_conf]
# Five-way IPC monitor: CSNOBOL4 + SPITBOL + ASM + JVM + NET
# Zero-race startup via --ready-fd pipe handshake in monitor_collect.py.
# Exit 0 = all backends match oracle. Exit 1 = divergence or timeout.
set -uo pipefail

SNO=${1:?Usage: run_monitor.sh <sno_file> [tracepoints_conf]}
CONF=${2:-$(dirname "$0")/tracepoints.conf}
MDIR=$(cd "$(dirname "$0")" && pwd)
DIR=$(cd "$MDIR/../.." && pwd)
X64_DIR="${X64_DIR:-/home/claude/x64}"
RT=$DIR/src/runtime
INC=/home/claude/corpus/programs/inc
SO=$MDIR/monitor_ipc.so
NET_RT="$DIR/src/runtime/net"
TIMEOUT="${MONITOR_TIMEOUT:-10}"
TMP=$(mktemp -d /tmp/monitor_XXXXXX)
trap 'rm -rf "$TMP"' EXIT
SNO_BASE=$(basename "$SNO" .sno)
base="$(basename "$SNO" .sno)"; dh="$(echo "$SNO"|md5sum|cut -c1-8)"
# Feed .input file to stdin if present (mirrors crosscheck harness behaviour)
SNO_INPUT="${SNO%.sno}.input"
STDIN_SRC="/dev/null"
[[ -f "$SNO_INPUT" ]] && STDIN_SRC="$SNO_INPUT"

# ── Step 1: inject traces ────────────────────────────────────────────────
python3 "$MDIR/inject_traces.py" "$SNO" "$CONF" > "$TMP/instr.sno"

# ── Step 2: compile ASM ──────────────────────────────────────────────────
for src in "$RT/x86/snobol4_stmt_rt.c" "$RT/x86/snobol4.c" \
           "$RT/mock/mock_includes.c"   "$RT/x86/snobol4_pattern.c" \
           "$RT/x86/engine.c"; do
    gcc -O0 -g -c "$src" -I"$RT/x86" -I"$RT" \
        -I"$DIR/src/frontend/snobol4" -w \
        -o "$TMP/$(basename "$src" .c).o" 2>/dev/null
done
"$DIR/scrip" -x86 "$SNO" > "$TMP/prog.s" 2>/dev/null
nasm -f elf64 -I"$RT/x86/" "$TMP/prog.s" -o "$TMP/prog.o" 2>/dev/null
gcc -no-pie "$TMP/prog.o" \
    "$TMP/snobol4_stmt_rt.o" "$TMP/snobol4.o" "$TMP/mock_includes.o" \
    "$TMP/snobol4_pattern.o" "$TMP/engine.o" \
    -lgc -lm -o "$TMP/prog_asm" 2>/dev/null

# ── Step 3: compile NET ──────────────────────────────────────────────────
SCRIP_CC_NET="${SCRIP_CC_NET:-$DIR/scrip}"
NET_CACHE="${NET_CACHE:-/tmp/one4all_net_cache}"
mkdir -p "$NET_CACHE"
for dll in snobol4lib.dll snobol4run.dll; do
    [[ -f "$NET_RT/$dll" ]] && cp "$NET_RT/$dll" "$NET_CACHE/" 2>/dev/null || true
done
il="$NET_CACHE/${base}_${dh}.il"; exe="$NET_CACHE/${base}_${dh}.exe"
stamp="$NET_CACHE/${base}_${dh}.stamp"
"$SCRIP_CC_NET" -net "$SNO" > "$il" 2>/dev/null
il_md5="$(md5sum "$il"|cut -d' ' -f1)"
if [[ "$(cat "$stamp" 2>/dev/null)" != "$il_md5" ]] || [[ ! -f "$exe" ]]; then
    ilasm "$il" /output:"$exe" >/dev/null 2>&1 && echo "$il_md5" > "$stamp"
fi

# ── Step 4: compile JVM ──────────────────────────────────────────────────
SCRIP_CC_JVM="${SCRIP_CC_JVM:-$DIR/scrip}"
JASMIN="${JASMIN:-$DIR/src/backend/jasmin.jar}"
JVM_CACHE="${JVM_CACHE:-/tmp/one4all_jvm_cache}"
mkdir -p "$JVM_CACHE"
jfile="$JVM_CACHE/${base}_${dh}.j"; jstamp="$JVM_CACHE/${base}_${dh}.jstamp"
"$SCRIP_CC_JVM" -jvm "$SNO" > "$jfile" 2>/dev/null
classname=$(grep '\.class' "$jfile" | head -1 | awk '{print $NF}')
j_md5="$(md5sum "$jfile"|cut -d' ' -f1)"
if [[ "$(cat "$jstamp" 2>/dev/null)" != "$j_md5" ]] || \
   [[ ! -f "$JVM_CACHE/${classname}.class" ]]; then
    java -jar "$JASMIN" "$jfile" -d "$JVM_CACHE" >/dev/null 2>&1 \
        && echo "$j_md5" > "$jstamp"
fi

# ── Step 5: create FIFOs + ready-signal pipe ─────────────────────────────
for p in csn spl asm jvm net; do mkfifo "$TMP/$p.fifo"; done

# ready-signal: a small named FIFO just for the 1-byte 'R' handshake.
# Using a named FIFO (not an anonymous pipe) avoids fd-inheritance issues
# across bash exec boundaries.
mkfifo "$TMP/ready.fifo"

# ── Step 6: launch collector — it opens all 5 FIFOs then writes 'R' ──────
# We pass the write-end of ready.fifo as a path; collector opens it itself.
# But monitor_collect.py uses --ready-fd (an integer fd).
# Solution: open ready.fifo for writing in a subshell, pass the fd number.
# The cleanest bash idiom: open fd 9 as write-end of ready.fifo in background,
# then pass fd 9 to the collector via exec.

set +e

# Launch collector: it will open ready.fifo write-end, open all 5 FIFOs,
# write 'R', then proceed with collection.
# We pass the ready.fifo PATH and let Python open it.
python3 "$MDIR/monitor_collect.py" \
    --timeout  "$TIMEOUT" \
    --ready-fd-path "$TMP/ready.fifo" \
    "$TMP/csn.fifo" "$TMP/spl.fifo" "$TMP/asm.fifo" \
    "$TMP/jvm.fifo" "$TMP/net.fifo" \
    "$TMP/csn.trace" "$TMP/spl.trace" "$TMP/asm.trace" \
    "$TMP/jvm.trace" "$TMP/net.trace" &
COLLECTOR_PID=$!

# Block on read-end of ready.fifo until collector signals 'R'
# (read from named FIFO blocks until writer opens it, then until data arrives)
READY_BYTE=$(dd if="$TMP/ready.fifo" bs=1 count=1 2>/dev/null | cat)
if [[ "$READY_BYTE" != "R" ]]; then
    echo "ERROR: collector did not signal readiness (got: '$READY_BYTE')" >&2
    kill "$COLLECTOR_PID" 2>/dev/null
    exit 2
fi

# ── Step 7: all 5 FIFOs have readers — launch participants in parallel ────
MONITOR_FIFO="$TMP/csn.fifo" MONITOR_SO="$SO" \
    snobol4 -f -P256k -I"$INC" "$TMP/instr.sno" \
    < "$STDIN_SRC" > "$TMP/csn.out" 2>"$TMP/csn.stderr" &
CSN_PID=$!

SNOLIB="$X64_DIR" MONITOR_FIFO="$TMP/spl.fifo" \
    MONITOR_SO="$X64_DIR/monitor_ipc_spitbol.so" \
    "$X64_DIR/bootsbl" "$TMP/instr.sno" \
    < "$STDIN_SRC" > "$TMP/spl.out" 2>"$TMP/spl.stderr" &
SPL_PID=$!

MONITOR_FIFO="$TMP/asm.fifo" \
    "$TMP/prog_asm" < "$STDIN_SRC" > "$TMP/asm.out" 2>"$TMP/asm.stderr" &
ASM_PID=$!

MONITOR_FIFO="$TMP/net.fifo" \
    mono "$exe" < "$STDIN_SRC" > "$TMP/net.out" 2>"$TMP/net.stderr" &
NET_PID=$!

MONITOR_FIFO="$TMP/jvm.fifo" \
    java -cp "$JVM_CACHE" "$classname" \
    < "$STDIN_SRC" > "$TMP/jvm.out" 2>"$TMP/jvm.stderr" &
JVM_PID=$!

# Write PID map to sidecar so collector can kill hung participants
printf "csn:%d,spl:%d,asm:%d,net:%d,jvm:%d\n" \
    "$CSN_PID" "$SPL_PID" "$ASM_PID" "$NET_PID" "$JVM_PID" \
    > "$TMP/pids"

# ── Step 8: wait for collector, then reap participants ───────────────────
wait "$COLLECTOR_PID"
COLLECT_RC=$?
# Kill any participants still running (timed-out or slow to exit)
for pid in "$CSN_PID" "$SPL_PID" "$ASM_PID" "$NET_PID" "$JVM_PID"; do
    kill "$pid" 2>/dev/null || true
done
wait 2>/dev/null || true

# ── Step 9: normalize ────────────────────────────────────────────────────
python3 "$MDIR/normalize_trace.py" "$CONF" \
    "$TMP/csn.trace" "$TMP/spl.trace" \
    "$TMP/asm.trace" "$TMP/jvm.trace" "$TMP/net.trace" \
    "$TMP/csn.norm"  "$TMP/spl.norm"  \
    "$TMP/asm.norm"  "$TMP/jvm.norm"  "$TMP/net.norm"

[ ! -s "$TMP/csn.norm" ] && echo "WARN [csnobol4] empty trace for $SNO_BASE"

# ── Step 10: diff ────────────────────────────────────────────────────────
FAIL=$COLLECT_RC
for B in asm jvm net; do
    DIFF=$(diff "$TMP/csn.norm" "$TMP/$B.norm" || true)
    if [ -z "$DIFF" ]; then echo "PASS [$B] $SNO_BASE"
    else echo "FAIL [$B] $SNO_BASE"; echo "$DIFF" | head -10; FAIL=1; fi
done
ODIFF=$(diff "$TMP/csn.norm" "$TMP/spl.norm" || true)
[ -n "$ODIFF" ] \
    && echo "ORACLE-DIFF [csn vs spl] $SNO_BASE" \
    && echo "$ODIFF" | head -5

exit $FAIL
