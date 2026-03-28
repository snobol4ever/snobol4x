#!/bin/bash
# run_monitor_sync.sh <sno_file> [tracepoints_conf]
#
# Sync-step 5-way monitor. Each participant blocks after every trace event
# waiting for a GO/STOP ack from the controller. First divergence stops
# all 5 immediately and reports exactly who diverged and what they said.
#
# Two FIFOs per participant:
#   <name>.ready  — participant writes events, controller reads
#   <name>.go  — controller writes GO/STOP, participant reads
#
# Exit 0 = all agree. Exit 1 = divergence. Exit 2 = timeout/error.

set -uo pipefail

SNO=${1:?Usage: run_monitor_sync.sh <sno_file> [tracepoints_conf]}
CONF=${2:-$(dirname "$0")/tracepoints.conf}
MDIR=$(cd "$(dirname "$0")" && pwd)
DIR=$(cd "$MDIR/../.." && pwd)
X64_DIR="${X64_DIR:-/home/claude/x64}"
RT=$DIR/src/runtime
INC="${INC:-/home/claude/corpus/programs/inc}"
# Fallback INC to demo/inc if corpus not present
[[ -d "$INC" ]] || INC="$DIR/demo/inc"
SO="$MDIR/monitor_ipc_sync.so"
NET_RT="$DIR/src/runtime/net"
TIMEOUT="${MONITOR_TIMEOUT:-10}"
TMP=$(mktemp -d /tmp/monitor_sync_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

SNO_BASE=$(basename "$SNO" .sno)
base="$(basename "$SNO" .sno)"
dh="$(echo "$SNO" | md5sum | cut -c1-8)"

STDIN_SRC="/dev/null"
[[ -f "${SNO%.sno}.input" ]] && STDIN_SRC="${SNO%.sno}.input"

echo "[sync] program: $SNO_BASE"

# ── Step 1: inject traces ────────────────────────────────────────────────
python3 "$MDIR/inject_traces.py" "$SNO" "$CONF" > "$TMP/instr.sno"
# Patch preamble: MON_OPEN now takes TWO args (ready_pipe, go_pipe)
# inject_traces.py emits: MON_OPEN(MON_READY_PIPE_)
# We need:               MON_OPEN(MON_EVT_FIFO_, MON_GO_PIPE_)
# Also add MON_GO_PIPE_ read from env MONITOR_GO_PIPE
python3 - "$TMP/instr.sno" << 'PYEOF'
import sys, re
path = sys.argv[1]
with open(path) as f:
    src = f.read()
# Insert MON_GO_PIPE_ read after MON_READY_PIPE_ read
src = src.replace(
    "        MON_READY_PIPE_       =  HOST(4,'MONITOR_READY_PIPE')\n",
    "        MON_READY_PIPE_       =  HOST(4,'MONITOR_READY_PIPE')\n"
    "        MON_GO_PIPE_   =  HOST(4,'MONITOR_GO_PIPE')\n"
)
# Patch MON_OPEN call to pass both FIFOs
src = src.replace(
    "        MON_OPEN(MON_READY_PIPE_)",
    "        MON_OPEN(MON_READY_PIPE_, MON_GO_PIPE_)"
)
with open(path, 'w') as f:
    f.write(src)
PYEOF

# ── Step 2: compile ASM ──────────────────────────────────────────────────
for src in "$RT/asm/snobol4_stmt_rt.c" "$RT/snobol4/snobol4.c" \
           "$RT/mock/mock_includes.c"   "$RT/snobol4/snobol4_pattern.c" \
           "$RT/engine/engine.c" \
           "$RT/asm/blk_alloc.c" "$RT/asm/blk_reloc.c"; do
    gcc -O0 -g -c "$src" -I"$RT/snobol4" -I"$RT" -I"$RT/asm" \
        -I"$DIR/src/frontend/snobol4" -w \
        -o "$TMP/$(basename "$src" .c).o" 2>/dev/null
done
"$DIR/sno2c" -asm -I"$INC" "$TMP/instr.sno" > "$TMP/prog.s" 2>/dev/null
nasm -f elf64 -I"$RT/asm/" "$TMP/prog.s" -o "$TMP/prog.o" 2>/dev/null
gcc -no-pie "$TMP/prog.o" \
    "$TMP/snobol4_stmt_rt.o" "$TMP/snobol4.o" "$TMP/mock_includes.o" \
    "$TMP/snobol4_pattern.o" "$TMP/engine.o" \
    "$TMP/blk_alloc.o" "$TMP/blk_reloc.o" \
    -lgc -lm -o "$TMP/prog_asm" 2>/dev/null

# ── Step 3: compile NET ──────────────────────────────────────────────────
NET_CACHE="${NET_CACHE:-/tmp/snobol4x_net_cache}"
mkdir -p "$NET_CACHE"
for dll in snobol4lib.dll snobol4run.dll; do
    [[ -f "$NET_RT/$dll" ]] && cp "$NET_RT/$dll" "$NET_CACHE/" 2>/dev/null || true
done
il="$NET_CACHE/${base}_${dh}.il"; exe="$NET_CACHE/${base}_${dh}.exe"
stamp="$NET_CACHE/${base}_${dh}.stamp"
"$DIR/sno2c" -net -I"$INC" "$TMP/instr.sno" > "$il" 2>/dev/null
il_md5="$(md5sum "$il" | cut -d' ' -f1)"
if [[ "$(cat "$stamp" 2>/dev/null)" != "$il_md5" ]] || [[ ! -f "$exe" ]]; then
    ilasm "$il" /output:"$exe" >/dev/null 2>&1 && echo "$il_md5" > "$stamp"
fi

# ── Step 4: compile JVM ──────────────────────────────────────────────────
JASMIN="${JASMIN:-$DIR/src/backend/jvm/jasmin.jar}"
JVM_CACHE="${JVM_CACHE:-/tmp/snobol4x_jvm_cache}"
mkdir -p "$JVM_CACHE"
jfile="$JVM_CACHE/${base}_${dh}.j"; jstamp="$JVM_CACHE/${base}_${dh}.jstamp"
"$DIR/sno2c" -jvm -I"$INC" "$SNO" > "$jfile" 2>/dev/null
classname=$(grep '\.class' "$jfile" | head -1 | awk '{print $NF}')
j_md5="$(md5sum "$jfile" | cut -d' ' -f1)"
if [[ "$(cat "$jstamp" 2>/dev/null)" != "$j_md5" ]] || \
   [[ ! -f "$JVM_CACHE/${classname}.class" ]]; then
    java -jar "$JASMIN" "$jfile" -d "$JVM_CACHE" >/dev/null 2>&1 \
        && echo "$j_md5" > "$jstamp"
fi

# ── Step 5: create FIFOs (two per participant) ───────────────────────────
NAMES="csn spl asm jvm net"
for p in $NAMES; do
    mkfifo "$TMP/$p.ready"
    mkfifo "$TMP/$p.go"
done

READY_PATHS=$(echo $NAMES | tr ' ' '\n' | sed "s|.*|$TMP/&.ready|" | tr '\n' ',' | sed 's/,$//')
GO_PATHS=$(echo $NAMES | tr ' ' '\n' | sed "s|.*|$TMP/&.go|" | tr '\n' ',' | sed 's/,$//')

# ── Step 6: launch all 5 participants (background) ───────────────────────
# Must start before controller so both FIFO ends open simultaneously.
# Case policy: run both oracles with DEFAULT case settings (fold-on).
#   CSNOBOL4 default = fold ON (uppercase). Do NOT pass -f (that toggles fold OFF).
#   SPITBOL   default = -F  (fold ON). Do NOT pass -f (that turns fold OFF).
# All .sno sources must use uppercase identifiers (DIFFER not differ, etc.).
# monitor_sync.py normalises trace names to uppercase before comparing.
MONITOR_READY_PIPE="$TMP/csn.ready" MONITOR_GO_PIPE="$TMP/csn.go" MONITOR_SO="$SO" \
    snobol4 -P256k -I"$INC" "$TMP/instr.sno" \
    < "$STDIN_SRC" > "$TMP/csn.out" 2>"$TMP/csn.err" &
CSN_PID=$!

SNOLIB="$X64_DIR" MONITOR_READY_PIPE="$TMP/spl.ready" MONITOR_GO_PIPE="$TMP/spl.go" \
    MONITOR_SO="$X64_DIR/monitor_ipc_spitbol.so" \
    "$X64_DIR/bootsbl" "$TMP/instr.sno" \
    < "$STDIN_SRC" > "$TMP/spl.out" 2>"$TMP/spl.err" &
SPL_PID=$!

MONITOR_READY_PIPE="$TMP/asm.ready" MONITOR_GO_PIPE="$TMP/asm.go" \
    "$TMP/prog_asm" < "$STDIN_SRC" > "$TMP/asm.out" 2>"$TMP/asm.err" &
ASM_PID=$!

MONITOR_READY_PIPE="$TMP/jvm.ready" MONITOR_GO_PIPE="$TMP/jvm.go" \
    java -cp "$JVM_CACHE" "$classname" \
    < "$STDIN_SRC" > "$TMP/jvm.out" 2>"$TMP/jvm.err" &
JVM_PID=$!

MONITOR_READY_PIPE="$TMP/net.ready" MONITOR_GO_PIPE="$TMP/net.go" \
    mono "$exe" < "$STDIN_SRC" > "$TMP/net.out" 2>"$TMP/net.err" &
NET_PID=$!

# ── Step 7: launch sync controller (opens FIFOs after participants are ready) ─
python3 "$MDIR/monitor_sync.py" \
    "$TIMEOUT" \
    "csn,spl,asm,jvm,net" \
    "$READY_PATHS" \
    "$GO_PATHS" > "$TMP/ctrl.out" 2>&1 &
CTRL_PID=$!

# ── Step 8: wait for controller ──────────────────────────────────────────
wait $CTRL_PID
CTRL_RC=$?

# Kill any remaining participants
for pid in $CSN_PID $SPL_PID $ASM_PID $JVM_PID $NET_PID; do
    kill "$pid" 2>/dev/null || true
done
wait 2>/dev/null || true

# ── Step 9: report ────────────────────────────────────────────────────────
cat "$TMP/ctrl.out"
exit $CTRL_RC
