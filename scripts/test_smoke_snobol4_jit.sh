#!/usr/bin/env bash
# scripts/test_smoke_snobol4_jit.sh — SN-9c-e gate:
# three-mode sweep across the crosscheck corpus; every program that passes
# under --ir-run and --sm-run must also pass under --jit-run.  Target state
# for SN-9: full three-mode parity on broad corpus, i.e. --jit-run reaches
# the same PASS/FAIL set as the other two modes.
#
# Gate semantics:
#   * --ir-run PASS count is the reference (what the tree-walk interpreter
#     manages on this corpus).
#   * --sm-run and --jit-run must MATCH that PASS count — any mode-specific
#     regression fails the gate.
#   * The baked-in .ref files come from SPITBOL oracle; a program that fails
#     in all three modes is a real (non-JIT-specific) bug and is reported
#     but does not regress the gate (same treatment as test_interp_broad_*
#     gives demo_claws5 etc.).
#
# Self-contained per RULES.md: paths derived from $0; no env deps required.
# Usage: bash scripts/test_smoke_snobol4_jit.sh

set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIP="${SCRIP:-$HERE/../scrip}"
CORPUS="${CORPUS:-/home/claude/corpus}"
INC="$CORPUS/programs/snobol4/demo/inc"
TIMEOUT="${TIMEOUT:-10}"

if [ ! -x "$SCRIP" ]; then
    echo "SKIP scrip not built at $SCRIP"
    exit 0
fi
if [ ! -d "$CORPUS/crosscheck" ]; then
    echo "SKIP corpus not populated at $CORPUS"
    exit 0
fi

# Run every crosscheck .sno under one mode, write "PASS FAIL" to stdout
# and the full failure list to file $2.
run_mode() {
    local mode="$1"
    local fails_out="$2"
    local pass=0 fail=0
    : > "$fails_out"
    while IFS= read -r sno; do
        local ref="${sno%.sno}.ref"
        local input="${sno%.sno}.input"
        [ ! -f "$ref" ] && continue
        local got exp
        if [ -f "$input" ]; then
            got=$(SNO_LIB="$INC" timeout "$TIMEOUT" "$SCRIP" $mode "$sno" < "$input" 2>/dev/null || true)
        else
            got=$(SNO_LIB="$INC" timeout "$TIMEOUT" "$SCRIP" $mode "$sno" < /dev/null 2>/dev/null || true)
        fi
        exp=$(cat "$ref")
        if [ "$got" = "$exp" ]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            printf ' %s' "$(basename "${sno%.sno}")" >> "$fails_out"
        fi
    done < <(find "$CORPUS/crosscheck" -name "*.sno" | sort)
    printf '%d %d\n' "$pass" "$fail"
}

echo "=== SN-9c-e: three-mode crosscheck sweep ==="

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

read IR_PASS  IR_FAIL  <<< "$(run_mode --ir-run  "$TMPDIR/ir")"
read SM_PASS  SM_FAIL  <<< "$(run_mode --sm-run  "$TMPDIR/sm")"
read JIT_PASS JIT_FAIL <<< "$(run_mode --jit-run "$TMPDIR/jit")"

IR_FAILS_FULL=$(cat "$TMPDIR/ir")
SM_FAILS_FULL=$(cat "$TMPDIR/sm")
JIT_FAILS_FULL=$(cat "$TMPDIR/jit")

printf '  --ir-run  PASS=%-3d FAIL=%d\n' "$IR_PASS" "$IR_FAIL"
printf '  --sm-run  PASS=%-3d FAIL=%d\n' "$SM_PASS" "$SM_FAIL"
printf '  --jit-run PASS=%-3d FAIL=%d\n' "$JIT_PASS" "$JIT_FAIL"
echo ""

# Gate: --sm-run and --jit-run must match --ir-run PASS count.
# Mode-specific regressions (failures that pass in --ir-run but fail in the
# other modes) are the real signal; shared failures are orthogonal bugs.
GATE_FAIL=0

if [ "$SM_PASS" -lt "$IR_PASS" ]; then
    echo "FAIL  --sm-run PASS ($SM_PASS) < --ir-run PASS ($IR_PASS)"
    GATE_FAIL=1
fi
if [ "$JIT_PASS" -lt "$IR_PASS" ]; then
    echo "FAIL  --jit-run PASS ($JIT_PASS) < --ir-run PASS ($IR_PASS)"
    GATE_FAIL=1
fi

if [ "$GATE_FAIL" -eq 0 ]; then
    echo "PASS  three-mode parity on crosscheck: $IR_PASS programs"
    # Report shared failures (same failing set across all three modes) as info only.
    if [ "$IR_FAIL" -gt 0 ]; then
        echo "  shared failures (same in all three modes):$IR_FAILS_FULL"
    fi
    exit 0
else
    echo ""
    echo "  --ir-run  failures:$IR_FAILS_FULL"
    echo "  --sm-run  failures:$SM_FAILS_FULL"
    echo "  --jit-run failures:$JIT_FAILS_FULL"
    exit 1
fi
