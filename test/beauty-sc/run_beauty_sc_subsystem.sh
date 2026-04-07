#!/usr/bin/env bash
# run_beauty_sc_subsystem.sh — run one Snocone BEAUTY subsystem test
#
# Compiles driver.sc via scrip-cc -sc -asm, assembles, links, runs,
# diffs against driver.ref (SNOBOL4 golden output).
#
# Usage:
#   CORPUS=/home/claude/corpus bash test/beauty-sc/run_beauty_sc_subsystem.sh <subsystem>
#
# Examples:
#   CORPUS=/home/claude/corpus bash test/beauty-sc/run_beauty_sc_subsystem.sh assign
#   CORPUS=/home/claude/corpus bash test/beauty-sc/run_beauty_sc_subsystem.sh match
#
# Environment:
#   CORPUS       — path to corpus root (required)
#   SCRIP_CC     — path to scrip-cc binary (default: ./scrip-cc)
#   STOP_ON_FAIL — 1 to stop at first fail (default: 0)
#
# Must be run from one4all root.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$TINY/scrip-cc}"
RT="$TINY/src/runtime"
CORPUS="${CORPUS:-}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'

if [[ $# -eq 0 ]]; then
    echo "Usage: bash test/beauty-sc/run_beauty_sc_subsystem.sh <subsystem> [subsystem2 ...]"
    exit 1
fi

if [[ -z "$CORPUS" ]]; then
    echo "ERROR: CORPUS env var not set" >&2
    exit 1
fi

if [[ ! -x "$SCRIP_CC" ]]; then
    echo "ERROR: scrip-cc not found at $SCRIP_CC" >&2
    exit 1
fi

# ── Build runtime archive ─────────────────────────────────────────────────────
RT_CACHE_DIR="$TINY/out/rt_cache"
mkdir -p "$RT_CACHE_DIR"

RT_SRCS=(
  "$RT/x86/snobol4_stmt_rt.c"
  "$RT/x86/snobol4.c"
  "$RT/mock/mock_includes.c"
  "$RT/x86/snobol4_pattern.c"
  "$RT/mock/mock_engine.c"
  "$RT/x86/blk_alloc.c"
  "$RT/x86/blk_reloc.c"
)
RT_ARCHIVE="$RT_CACHE_DIR/snocone_rt.a"
RT_STAMP="$RT_CACHE_DIR/snocone_rt.stamp"

_stamp=$(stat -c '%n:%Y' "${RT_SRCS[@]}" 2>/dev/null | md5sum | cut -c1-16)
_need_rebuild=1
if [[ -f "$RT_STAMP" && -f "$RT_ARCHIVE" ]]; then
    _cached=$(cat "$RT_STAMP" 2>/dev/null)
    [[ "$_cached" == "$_stamp" ]] && _need_rebuild=0
fi

if [[ $_need_rebuild -eq 1 ]]; then
    echo "Building runtime archive..."
    _bld=$(mktemp -d); trap "rm -rf $_bld" EXIT
    gcc -O0 -g -c "$RT/x86/snobol4_stmt_rt.c"    -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/stmt_rt.o"
    gcc -O0 -g -c "$RT/x86/snobol4.c"         -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/snobol4.o"
    gcc -O0 -g -c "$RT/mock/mock_includes.c"       -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/mock_includes.o"
    gcc -O0 -g -c "$RT/x86/snobol4_pattern.c" -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/snobol4_pattern.o"
    gcc -O0 -g -c "$RT/mock/mock_engine.c"         -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/mock_engine.o"
    gcc -O0 -g -c "$RT/x86/blk_alloc.c"           -I"$RT/x86"                                              -w -o "$_bld/blk_alloc.o"
    gcc -O0 -g -c "$RT/x86/blk_reloc.c"           -I"$RT/x86"                                              -w -o "$_bld/blk_reloc.o"
    ar rcs "$RT_ARCHIVE" "$_bld"/*.o
    echo "$_stamp" > "$RT_STAMP"
fi

WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

PASS=0; FAIL=0; SKIP=0

run_subsystem() {
    local subsys="$1"
    local driver_sc="$SCRIPT_DIR/$subsys/driver.sc"
    local driver_ref="$SCRIPT_DIR/$subsys/driver.ref"

    echo "=== SCB: $subsys ==="

    if [[ ! -f "$driver_sc" ]]; then
        echo -e "${YELLOW}SKIP${RESET}  $subsys  (no driver.sc at $driver_sc)"
        SKIP=$((SKIP+1)); return
    fi
    if [[ ! -f "$driver_ref" ]]; then
        echo -e "${YELLOW}SKIP${RESET}  $subsys  (no driver.ref)"
        SKIP=$((SKIP+1)); return
    fi

    local s_file="$WORK/${subsys}.s"
    local o_file="$WORK/${subsys}.o"
    local bin="$WORK/${subsys}_bin"

    # Compile
    if ! timeout 15 "$SCRIP_CC" -sc -asm "$driver_sc" -o "$s_file" 2>"$WORK/${subsys}.scrip_err"; then
        echo -e "${RED}FAIL${RESET}  $subsys  [scrip-cc error/timeout]"
        cat "$WORK/${subsys}.scrip_err" | head -5
        FAIL=$((FAIL+1)); return
    fi

    # Assemble
    if ! nasm -f elf64 -I"$RT/x86/" "$s_file" -o "$o_file" 2>"$WORK/${subsys}.nasm_err"; then
        echo -e "${RED}FAIL${RESET}  $subsys  [nasm error]"
        head -5 "$WORK/${subsys}.nasm_err"
        FAIL=$((FAIL+1)); return
    fi

    # Link
    if ! gcc -no-pie "$o_file" "$RT_ARCHIVE" -lgc -lm -w -o "$bin" 2>"$WORK/${subsys}.link_err"; then
        echo -e "${RED}FAIL${RESET}  $subsys  [link error]"
        head -3 "$WORK/${subsys}.link_err"
        FAIL=$((FAIL+1)); return
    fi

    # Run
    local got exp
    got=$(timeout 10 "$bin" 2>/dev/null) || true
    exp=$(cat "$driver_ref")

    if [[ "$got" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET}  $subsys"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET}  $subsys  [output mismatch]"
        diff <(echo "$exp") <(echo "$got") | head -15
        FAIL=$((FAIL+1))
    fi
}

for subsys in "$@"; do
    run_subsystem "$subsys"
done

echo ""
echo "============================================"
echo -e "Results: ${GREEN}${PASS} passed${RESET} / ${RED}${FAIL} failed${RESET} / ${YELLOW}${SKIP} skipped${RESET}"
[[ $FAIL -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" || { echo -e "${RED}FAILURES${RESET}"; exit 1; }
