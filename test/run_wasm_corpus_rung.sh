#!/usr/bin/env bash
# test/run_wasm_corpus_rung.sh — WASM corpus rung runner
#
# Usage:
#   CORPUS=/home/claude/corpus bash test/run_wasm_corpus_rung.sh <rung_dir_name>
#
# Example:
#   CORPUS=/home/claude/corpus bash test/run_wasm_corpus_rung.sh output
#   CORPUS=/home/claude/corpus bash test/run_wasm_corpus_rung.sh rungW01
#
# Pipeline per test:
#   scrip -wasm <stem>.sno  > <work>/<stem>.wat
#   wat2wasm --enable-tail-call <work>/<stem>.wat -o <work>/<stem>.wasm
#   node test/wasm/run_wasm.js <work>/<stem>.wasm  > <work>/<stem>.got
#   diff <stem>.ref <work>/<stem>.got
#
# Exit: 0 = all pass, 1 = any failure

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP="${SCRIP:-$ROOT/scrip}"
CORPUS="${CORPUS:-$(cd "$ROOT/../corpus" 2>/dev/null && pwd || echo "")}"
RUNNER="$ROOT/test/wasm/run_wasm.js"
WORK="${WORK:-/tmp/wasm_rung_$$}"
TIMEOUT="${TIMEOUT_WASM:-10}"

GREEN='\033[0;32m'; RED='\033[0;31m'; BOLD='\033[1m'; RESET='\033[0m'

# ── Preflight ────────────────────────────────────────────────────────────────
die() { echo -e "${RED}ERROR${RESET}: $*" >&2; exit 2; }

[[ $# -ge 1 ]] || die "usage: $0 <rung_dir_name>"
RUNG_NAME="$1"

command -v "$SCRIP"  &>/dev/null || die "scrip not found at $SCRIP — run SESSION_SETUP.sh"
command -v wat2wasm     &>/dev/null || die "wat2wasm not found — run SESSION_SETUP.sh"
command -v node         &>/dev/null || die "node not found — run SESSION_SETUP.sh"
[[ -f "$RUNNER" ]]                  || die "run_wasm.js not found at $RUNNER"
[[ -n "$CORPUS" && -d "$CORPUS/crosscheck" ]] || die "CORPUS not set or crosscheck dir missing"

RUNG_DIR="$CORPUS/crosscheck/$RUNG_NAME"
[[ -d "$RUNG_DIR" ]] || die "rung dir not found: $RUNG_DIR"

mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

# ── Run tests ────────────────────────────────────────────────────────────────
pass=0; fail=0; skip=0

for sno in "$RUNG_DIR"/*.sno; do
    [[ -f "$sno" ]] || continue
    base=$(basename "$sno" .sno)
    ref="${sno%.sno}.ref"

    if [[ ! -f "$ref" ]]; then
        echo -e "  ${RED}SKIP${RESET}  $base  (no .ref)"
        (( skip++ )) || true
        continue
    fi

    wat="$WORK/${base}.wat"
    wasm="$WORK/${base}.wasm"
    got="$WORK/${base}.got"
    err="$WORK/${base}.err"

    # Step 1: compile to WAT (explicit -o → work dir; never writes alongside corpus source)
    if ! "$SCRIP" -wasm -o "$wat" "$sno" 2>"$err"; then
        echo -e "  ${RED}FAIL${RESET}  $base  (scrip error)"
        [[ -s "$err" ]] && sed 's/^/        /' "$err"
        (( fail++ )) || true
        continue
    fi

    # Step 2: assemble WAT → WASM
    if ! wat2wasm --enable-tail-call "$wat" -o "$wasm" 2>"$err"; then
        echo -e "  ${RED}FAIL${RESET}  $base  (wat2wasm error)"
        [[ -s "$err" ]] && sed 's/^/        /' "$err"
        (( fail++ )) || true
        continue
    fi

    # Step 3: run under node with timeout
    if ! timeout "$TIMEOUT" node "$RUNNER" "$wasm" > "$got" 2>"$err"; then
        echo -e "  ${RED}FAIL${RESET}  $base  (node runner error/timeout)"
        [[ -s "$err" ]] && sed 's/^/        /' "$err"
        (( fail++ )) || true
        continue
    fi

    # Step 4: diff against oracle
    if diff -q "$ref" "$got" &>/dev/null; then
        echo -e "  ${GREEN}PASS${RESET}  $base"
        (( pass++ )) || true
    else
        echo -e "  ${RED}FAIL${RESET}  $base  (output mismatch)"
        diff "$ref" "$got" | head -10 | sed 's/^/        /'
        (( fail++ )) || true
    fi
done

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
total=$(( pass + fail + skip ))
echo -e "${BOLD}WASM rung ${RUNG_NAME}: ${GREEN}${pass} pass${RESET} / ${RED}${fail} fail${RESET} / ${skip} skip  (${total} total)${RESET}"

[[ $fail -eq 0 ]]
