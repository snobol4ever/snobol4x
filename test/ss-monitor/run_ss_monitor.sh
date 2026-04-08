#!/bin/bash
# run_ss_monitor.sh — launch sync-step monitor for Silly vs CSNOBOL4
#
# Usage:
#   ./run_ss_monitor.sh <sno_file> [--timeout N] [--ping]
#
# Modes:
#   --ping       M-SS-MON-1: run ping_test×2 instead of real interpreters
#   (default)    Run CSNOBOL4 (csn) vs Silly (sly) on <sno_file>
#
# Environment (override defaults):
#   CSNOBOL4     path to csnobol4 binary
#   SILLY        path to silly-snobol4 binary
#   TIMEOUT      inter-event timeout in seconds (default 10)

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

CSNOBOL4="${CSNOBOL4:-/home/claude/work/snobol4-2.3.3/snobol4-mon}"
SILLY="${SILLY:-/tmp/silly-mon}"
TIMEOUT="${TIMEOUT:-10}"
SNO_FILE=""
PING_MODE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ping)    PING_MODE=1; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        *.sno)     SNO_FILE="$1"; shift ;;
        *)         echo "Unknown arg: $1"; exit 1 ;;
    esac
done

TMP=$(mktemp -d /tmp/ss-monitor-XXXXXX)
trap 'rm -rf "$TMP"' EXIT

CSN_EVT="$TMP/csn.evt"
CSN_ACK="$TMP/csn.ack"
SLY_EVT="$TMP/sly.evt"
SLY_ACK="$TMP/sly.ack"

mkfifo "$CSN_EVT" "$CSN_ACK" "$SLY_EVT" "$SLY_ACK"

echo "[monitor] FIFOs created in $TMP"
echo "[monitor] Starting controller..."

# Launch controller in background (opens evt FIFOs for reading first,
# then ack FIFOs for writing — unblocks participant open() calls in order)
FILTER_ARG=""
if [ "$PING_MODE" = "0" ] && [ -f "$DIR/sly_fns.txt" ]; then
    FILTER_ARG="--filter-fns $DIR/sly_fns.txt"
fi
python3 "$DIR/monitor_sync.py" \
    "$CSN_EVT" "$CSN_ACK" "$SLY_EVT" "$SLY_ACK" \
    --timeout "$TIMEOUT" $FILTER_ARG &
CTRL_PID=$!

if [ "$PING_MODE" = "1" ]; then
    echo "[monitor] Ping mode — launching two ping_test processes"
    MON_EVT="$CSN_EVT" MON_ACK="$CSN_ACK" "$DIR/ping_test" csn &
    MON_EVT="$SLY_EVT" MON_ACK="$SLY_ACK" "$DIR/ping_test" sly &
else
    if [ -z "$SNO_FILE" ]; then
        echo "Usage: $0 <file.sno> [--timeout N]"
        kill $CTRL_PID 2>/dev/null
        exit 1
    fi
    echo "[monitor] Running: $SNO_FILE"
    echo "[monitor]   csn: $CSNOBOL4"
    echo "[monitor]   sly: $SILLY"
    MON_EVT="$CSN_EVT" MON_ACK="$CSN_ACK" "$CSNOBOL4" "$SNO_FILE" </dev/null &
    CSN_PID=$!
    MON_EVT="$SLY_EVT" MON_ACK="$SLY_ACK" "$SILLY" "$SNO_FILE" </dev/null &
    SLY_PID=$!
fi

wait $CTRL_PID
RC=$?
echo "[monitor] exit $RC"
exit $RC
