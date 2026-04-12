#!/usr/bin/env bash
# run_sc_corpus_rung.sh — SC corpus ladder driver (-sc -asm pipeline)
#
# Compiles each .sc in a given directory via scrip -sc -asm, assembles,
# links against stmt_rt + snobol4 runtime, runs, diffs vs .ref oracle.
#
# Usage:
#   bash test/crosscheck/run_sc_corpus_rung.sh <dir> [dir2 ...]
#
# Examples:
#   bash test/crosscheck/run_sc_corpus_rung.sh $CORPUS/crosscheck/snocone
#   bash test/crosscheck/run_sc_corpus_rung.sh \
#       $CORPUS/crosscheck/snocone \
#       $CORPUS/programs/snocone/corpus
#
# Environment overrides:
#   SCRIP_CC        — path to scrip binary     (default: ./scrip)
#   STOP_ON_FAIL — 1 to stop at first fail  (default: 0)

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TINY="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$TINY/scrip}"
RT="$TINY/src/runtime"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <sc-corpus-dir> [dir2 ...]"
    exit 1
fi

if [[ ! -x "$SCRIP_CC" ]]; then
    echo "ERROR: scrip not found at $SCRIP_CC"
    exit 1
fi

# ── Persistent runtime archive cache ─────────────────────────────────────────
# Build snocone_rt.a once per session (or when sources change) into
# $TINY/out/rt_cache/.  A stamp file records the mtimes of the 7 source files;
# if the stamp is current the archive is reused as-is — skipping ~4-5s of gcc
# per rung invocation (7 compilations × 21 rungs ≈ 100s saved).
WORK=$(mktemp -d); trap "rm -rf $WORK" EXIT

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

# Compute stamp: mtime of each source file
_stamp=$(stat -c '%n:%Y' "${RT_SRCS[@]}" 2>/dev/null | md5sum | cut -c1-16)

_need_rebuild=1
if [[ -f "$RT_STAMP" && -f "$RT_ARCHIVE" ]]; then
  _cached=$(cat "$RT_STAMP" 2>/dev/null)
  [[ "$_cached" == "$_stamp" ]] && _need_rebuild=0
fi

if [[ $_need_rebuild -eq 1 ]]; then
  _objs=()
  _bld=$(mktemp -d); trap "rm -rf $_bld" EXIT
  gcc -O0 -g -c "$RT/x86/snobol4_stmt_rt.c"    -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/stmt_rt.o"
  gcc -O0 -g -c "$RT/x86/snobol4.c"         -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/snobol4.o"
  gcc -O0 -g -c "$RT/mock/mock_includes.c"       -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/mock_includes.o"
  gcc -O0 -g -c "$RT/x86/snobol4_pattern.c" -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/snobol4_pattern.o"
  gcc -O0 -g -c "$RT/mock/mock_engine.c"         -I"$RT/x86" -I"$RT" -I"$TINY/src/frontend/snobol4" -w -o "$_bld/mock_engine.o"
  gcc -O0 -g -c "$RT/x86/blk_alloc.c"           -I"$RT/x86"                                             -w -o "$_bld/blk_alloc.o"
  gcc -O0 -g -c "$RT/x86/blk_reloc.c"           -I"$RT/x86"                                             -w -o "$_bld/blk_reloc.o"
  ar rcs "$RT_ARCHIVE" "$_bld"/*.o
  echo "$_stamp" > "$RT_STAMP"
fi

LINK_OBJS="$RT_ARCHIVE"

# ── Per-test binary cache ──────────────────────────────────────────────────────
# Key: md5 of (.sc content + runtime stamp).  On cache hit: skip scrip,
# nasm, and link entirely — just run the cached binary.  Cuts warm-run cost
# from ~220ms/test to ~10ms/test (run only).
BIN_CACHE_DIR="$RT_CACHE_DIR/bins"
mkdir -p "$BIN_CACHE_DIR"

run_test() {
    local sc="$1"
    local base; base=$(basename "$sc" .sc)
    local dir;  dir=$(dirname "$sc")
    local ref="$dir/$base.ref"
    local input="$dir/$base.input"   # optional stdin

    [[ ! -f "$ref" ]] && { echo -e "${YELLOW}SKIP${RESET} $base (no .ref)"; SKIP=$((SKIP+1)); return 0; }

    # .xfail — emitter gap queued for backend session; skip gracefully
    local xfail="$dir/$base.xfail"
    if [[ -f "$xfail" ]]; then
        local reason; reason=$(cat "$xfail")
        echo -e "${YELLOW}XFAIL${RESET} $base  [$reason]"
        SKIP=$((SKIP+1)); return 0
    fi

    # Binary cache: key = md5(sc_content + runtime_stamp)
    local _sc_md5; _sc_md5=$(md5sum "$sc" 2>/dev/null | cut -c1-16)
    local _cache_key="${_sc_md5}_${_stamp}"
    local _cached_bin="$BIN_CACHE_DIR/${_cache_key}"
    local bin

    if [[ -x "$_cached_bin" ]]; then
        bin="$_cached_bin"
    else
        local s_file="$WORK/${base}.s"
        local o_file="$WORK/${base}.o"
        bin="$WORK/${base}_bin"

        # scrip -sc -asm (timeout guards against hangs on unimplemented constructs)
        if ! timeout 15 "$SCRIP_CC" -sc -asm "$sc" -o "$s_file" 2>"$WORK/${base}.scrip_err"; then
            echo -e "${RED}FAIL${RESET} $base  [scrip error/timeout]"
            cat "$WORK/${base}.scrip_err" | head -3
            FAIL=$((FAIL+1))
            [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
            return 0
        fi

        # nasm
        if ! nasm -f elf64 -I"$RT/x86/" "$s_file" -o "$o_file" 2>"$WORK/${base}.nasm_err"; then
            echo -e "${RED}FAIL${RESET} $base  [nasm error]"
            head -5 "$WORK/${base}.nasm_err"
            FAIL=$((FAIL+1))
            [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
            return 0
        fi

        # link
        if ! gcc -no-pie "$o_file" $LINK_OBJS -lgc -lm -w -no-pie -o "$bin" 2>"$WORK/${base}.link_err"; then
            echo -e "${RED}FAIL${RESET} $base  [link error]"
            head -3 "$WORK/${base}.link_err"
            FAIL=$((FAIL+1))
            [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
            return 0
        fi

        # Promote to cache (atomic via temp+move)
        cp "$bin" "${_cached_bin}.tmp" && mv "${_cached_bin}.tmp" "$_cached_bin" || true
    fi

    # run
    local got exp
    if [[ -f "$input" ]]; then
        got=$(timeout 10 "$bin" < "$input" 2>/dev/null) || true
    else
        got=$(timeout 10 "$bin" 2>/dev/null) || true
    fi
    exp=$(cat "$ref")

    if [[ "$got" == "$exp" ]]; then
        echo -e "${GREEN}PASS${RESET} $base"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${RESET} $base"
        echo "  expected: $(echo "$exp" | head -3)"
        echo "  got:      $(echo "$got" | head -3)"
        FAIL=$((FAIL+1))
        [[ "$STOP_ON_FAIL" == "1" ]] && exit 1
    fi
}

for dir in "$@"; do
    for sc in "$dir"/*.sc; do
        [[ -f "$sc" ]] || continue
        run_test "$sc"
    done
done

echo "============================================"
echo "Results: ${GREEN}${PASS} passed${RESET}, ${RED}${FAIL} failed${RESET}, ${YELLOW}${SKIP} skipped${RESET}"
[[ $FAIL -eq 0 ]] && echo -e "${GREEN}ALL PASS${RESET}" || echo -e "${RED}FAILURES${RESET}"
