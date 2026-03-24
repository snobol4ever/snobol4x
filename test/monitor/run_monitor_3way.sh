#!/bin/bash
# run_monitor_3way.sh <sno_file> [tracepoints_conf]
#
# Sync-step 3-way monitor: CSNOBOL4 + SPITBOL + snobol4x ASM backend.
# JVM and NET excluded. Identical logic to run_monitor_sync.sh minus
# those two participants. NAMES="spl csn asm" (SPITBOL is oracle[0]).
#
# Exit 0 = all agree. Exit 1 = divergence. Exit 2 = timeout/error.

set -uo pipefail

SNO=${1:?Usage: run_monitor_3way.sh <sno_file> [tracepoints_conf]}
CONF=${2:-$(dirname "$0")/tracepoints.conf}
MDIR=$(cd "$(dirname "$0")" && pwd)
DIR=$(cd "$MDIR/../.." && pwd)
X64_DIR="${X64_DIR:-/home/claude/x64}"
RT=$DIR/src/runtime
INC="${INC:-/home/claude/snobol4corpus/programs/inc}"
[[ -d "$INC" ]] || INC="$DIR/demo/inc"
SO="$MDIR/monitor_ipc_sync.so"
TIMEOUT="${MONITOR_TIMEOUT:-10}"
TMP=$(mktemp -d /tmp/monitor_3way_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

base="$(basename "$SNO" .sno)"
STDIN_SRC="/dev/null"
[[ -f "${SNO%.sno}.input" ]] && STDIN_SRC="${SNO%.sno}.input"

echo "[3way] program: $base"

# ── Step 1: inject traces ────────────────────────────────────────────────
python3 "$MDIR/inject_traces.py" "$SNO" "$CONF" > "$TMP/instr.sno"
python3 - "$TMP/instr.sno" << 'PYEOF'
import sys, re
path = sys.argv[1]
with open(path) as f:
    src = f.read()
src = src.replace(
    "        MON_READY_PIPE_       =  HOST(4,'MONITOR_READY_PIPE')\n",
    "        MON_READY_PIPE_       =  HOST(4,'MONITOR_READY_PIPE')\n"
    "        MON_GO_PIPE_   =  HOST(4,'MONITOR_GO_PIPE')\n"
)
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

# ── Step 3: create FIFOs ─────────────────────────────────────────────────
NAMES="spl csn asm"
for p in $NAMES; do mkfifo "$TMP/$p.ready"; mkfifo "$TMP/$p.go"; done
READY_PATHS=$(echo $NAMES | tr ' ' '\n' | sed "s|.*|$TMP/&.ready|" | tr '\n' ',' | sed 's/,$//')
GO_PATHS=$(echo $NAMES    | tr ' ' '\n' | sed "s|.*|$TMP/&.go|"    | tr '\n' ',' | sed 's/,$//')

# ── Step 4: launch 3 participants ────────────────────────────────────────
# SPITBOL is participant 0 (primary oracle) — snobol4x targets SPITBOL semantics (D-001/D-005).
# CSNOBOL4 is secondary; its quirks (FENCE, DATATYPE case) are ignore-points.
(cd "$INC" && SNOLIB="$X64_DIR:$INC" MONITOR_READY_PIPE="$TMP/spl.ready" \
    MONITOR_GO_PIPE="$TMP/spl.go" MONITOR_SO="$X64_DIR/monitor_ipc_spitbol.so" \
    "$X64_DIR/bootsbl" "$TMP/instr.sno" \
    < "$STDIN_SRC" > "$TMP/spl.out" 2>"$TMP/spl.err") &
SPL_PID=$!

MONITOR_READY_PIPE="$TMP/csn.ready" MONITOR_GO_PIPE="$TMP/csn.go" MONITOR_SO="$SO" \
    snobol4 -P256k -I"$INC" "$TMP/instr.sno" \
    < "$STDIN_SRC" > "$TMP/csn.out" 2>"$TMP/csn.err" &
CSN_PID=$!

MONITOR_READY_PIPE="$TMP/asm.ready" MONITOR_GO_PIPE="$TMP/asm.go" \
    "$TMP/prog_asm" < "$STDIN_SRC" > "$TMP/asm.out" 2>"$TMP/asm.err" &
ASM_PID=$!

# ── Step 5: controller ───────────────────────────────────────────────────
python3 "$MDIR/monitor_sync.py" \
    "$TIMEOUT" "$(echo $NAMES | tr ' ' ',')" "$READY_PATHS" "$GO_PATHS" > "$TMP/ctrl.out" 2>&1 &
CTRL_PID=$!

wait $CTRL_PID; CTRL_RC=$?
for pid in $CSN_PID $SPL_PID $ASM_PID; do kill "$pid" 2>/dev/null || true; done
wait 2>/dev/null || true

cat "$TMP/ctrl.out"
exit $CTRL_RC
