#!/bin/bash
# run_monitor.sh <sno_file> [tracepoints_conf]
#
# Two-participant monitor: CSNOBOL4 (oracle) + snobol4x ASM backend.
#
# CSNOBOL4:  runs instrumented .sno (TRACE callbacks → TERMINAL → stderr)
# ASM:       compiles + runs original .sno with MONITOR=1 (comm_var → stderr)
#
# Both streams are normalized and diffed. Exit 0 = PASS. Exit 1 = FAIL.
#
# Expands to 3-way (+SPITBOL) in M-MONITOR-3WAY.
# Expands to 5-way (+JVM+NET) in M-MONITOR-5WAY.

set -euo pipefail

SNO=${1:?Usage: run_monitor.sh <sno_file> [tracepoints_conf]}
CONF=${2:-$(dirname "$0")/tracepoints.conf}

DIR=$(cd "$(dirname "$0")/../.." && pwd)   # snobol4x root
RT=$DIR/src/runtime
INC=/home/claude/snobol4corpus/programs/inc
TMP=$(mktemp -d /tmp/monitor_XXXXXX)
trap 'rm -rf "$TMP"' EXIT

SNO_BASE=$(basename "$SNO" .sno)

# ------------------------------------------------------------------
# Step 1: Inject TRACE() callbacks → instrumented .sno for CSNOBOL4
# ------------------------------------------------------------------
python3 "$(dirname "$0")/inject_traces.py" "$SNO" "$CONF" > "$TMP/instr.sno"

# ------------------------------------------------------------------
# Step 2: Run CSNOBOL4 on instrumented .sno
#   stdout = program output (discarded for trace comparison)
#   stderr = TRACE events (VALUE/CALL/RETURN lines from callbacks)
# ------------------------------------------------------------------
snobol4 -f -P256k -I"$INC" "$TMP/instr.sno" \
    < /dev/null \
    > "$TMP/csn.out" 2>"$TMP/csn.trace" || true

# ------------------------------------------------------------------
# Step 3: Compile original .sno via ASM backend and run with MONITOR=1
#   MONITOR=1 activates comm_var telemetry → VAR/STNO lines on stderr
#   (No TRACE injection needed — comm_var covers all variable assignments)
# ------------------------------------------------------------------

# Build runtime objects (cached in TMP)
gcc -O0 -g -c "$RT/asm/snobol4_stmt_rt.c" \
    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w \
    -o "$TMP/stmt_rt.o"
gcc -O0 -g -c "$RT/snobol4/snobol4.c" \
    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w \
    -o "$TMP/snobol4.o"
gcc -O0 -g -c "$RT/mock/mock_includes.c" \
    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w \
    -o "$TMP/mock.o"
gcc -O0 -g -c "$RT/snobol4/snobol4_pattern.c" \
    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w \
    -o "$TMP/pat.o"
gcc -O0 -g -c "$RT/engine/engine.c" \
    -I"$RT/snobol4" -I"$RT" -I"$DIR/src/frontend/snobol4" -w \
    -o "$TMP/eng.o"

# Compile .sno → .s
"$DIR/sno2c" -asm "$SNO" > "$TMP/prog.s" 2>"$TMP/compile.err" || {
    echo "FAIL [ASM compile] $SNO_BASE"
    cat "$TMP/compile.err" | head -10
    exit 1
}

# Assemble .s → .o
nasm -f elf64 -I"$RT/asm/" "$TMP/prog.s" -o "$TMP/prog.o" 2>"$TMP/nasm.err" || {
    echo "FAIL [ASM assemble] $SNO_BASE"
    cat "$TMP/nasm.err" | head -10
    exit 1
}

# Link
gcc -no-pie "$TMP/prog.o" \
    "$TMP/stmt_rt.o" "$TMP/snobol4.o" "$TMP/mock.o" "$TMP/pat.o" "$TMP/eng.o" \
    -lgc -lm -o "$TMP/prog_asm" 2>"$TMP/link.err" || {
    echo "FAIL [ASM link] $SNO_BASE"
    cat "$TMP/link.err" | head -10
    exit 1
}

# Run with MONITOR=1; capture stderr (VAR/STNO events)
MONITOR=1 "$TMP/prog_asm" \
    < /dev/null \
    > "$TMP/asm.out" 2>"$TMP/asm.trace" || true

# ------------------------------------------------------------------
# Step 4: Normalize both streams
# ------------------------------------------------------------------
python3 "$(dirname "$0")/normalize_trace.py" "$CONF" \
    "$TMP/csn.trace" "$TMP/asm.trace" \
    "$TMP/csn.norm"  "$TMP/asm.norm"

# ------------------------------------------------------------------
# Step 5: Sanity check — CSNOBOL4 stream must be non-empty
# ------------------------------------------------------------------
if [ ! -s "$TMP/csn.norm" ]; then
    echo "WARN [csnobol4] empty trace stream for $SNO_BASE (no traceable vars/funcs?)"
fi

# ------------------------------------------------------------------
# Step 6: Diff
# ------------------------------------------------------------------
FAIL=0
DIFF=$(diff "$TMP/csn.norm" "$TMP/asm.norm" || true)
if [ -z "$DIFF" ]; then
    echo "PASS [ASM] $SNO_BASE"
else
    echo "FAIL [ASM] $SNO_BASE"
    echo "$DIFF" | head -10
    FAIL=1
fi

exit $FAIL
