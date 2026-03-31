#!/usr/bin/env bash
# test/run_invariants.sh — Fast 3×3 invariant harness (M-G-INV / M-G-INV-FAST)
#
# Optimizations (M-G-INV-FAST):
#   - Persistent runtime .a cache (out/rt_cache/) — never rebuilt if stamp matches
#   - Batch jasmin: all .j files assembled in ONE java -jar jasmin invocation
#   - Parallel nasm across all tests (xargs -P)
#   - SnoHarness: one JVM process runs all SNOBOL4+Prolog JVM tests
#   - START / FINISH / ELAPSED printed at top and bottom of every run
#
# Usage:
#   bash test/run_invariants.sh [--serial] [--verbose]
#
# Environment overrides:
#   SCRIP_CC      path to scrip-cc binary           (default: <root>/scrip-cc)
#   CORPUS        path to corpus root               (default: <root>/../corpus)
#   JASMIN        path to jasmin.jar                (default: <root>/src/backend/jvm/jasmin.jar)
#   RT_CACHE      path to persistent archive cache  (default: <root>/out/rt_cache)
#   TIMEOUT_X86   per-test timeout x86 (s)          (default: 5)
#   TIMEOUT_JVM   per-test timeout JVM (s)          (default: 10)
#   JOBS          max parallel jobs                 (default: nproc)
#
# Exit: 0 = all active cells pass, 1 = any failure
#
# Authors: Claude Sonnet 4.6 (G-7 2026-03-28 M-G-INV; G-9 2026-03-29 M-G-INV-FAST)

set -uo pipefail

# ── Config ────────────────────────────────────────────────────────────────────
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip-cc}"
CORPUS="${CORPUS:-$(cd "$ROOT/../corpus" 2>/dev/null && pwd || echo "")}"
export CORPUS_REPO="$CORPUS"   # rung scripts use CORPUS_REPO; M-G-INV-FAST-X86-FIX
JASMIN="${JASMIN:-$ROOT/src/backend/jvm/jasmin.jar}"
RT_CACHE="${RT_CACHE:-$ROOT/out/rt_cache}"
RT="$ROOT/src/runtime"
SCRIP_CC_INC="$ROOT/src/frontend/snobol4"
TIMEOUT_X86="${TIMEOUT_X86:-5}"
TIMEOUT_JVM="${TIMEOUT_JVM:-10}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
VERBOSE=0
for arg in "$@"; do
  [[ "$arg" == "--verbose" ]] && VERBOSE=1
done

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'
BOLD='\033[1m'; RESET='\033[0m'

# ── Preflight — verify tools present ────────────────────────────────────────────
# This script does NOT install tools. Run SESSION_SETUP.sh first:
#   TOKEN=ghp_xxx bash /home/claude/.github/SESSION_SETUP.sh
_need() {
  local label="$1" ok="$2"
  if [[ "$ok" == "1" ]]; then
    echo -e "${GREEN}  [ok]${RESET}  $label"
  else
    echo -e "${RED}  [MISSING]${RESET}  $label — run: TOKEN=... bash /home/claude/.github/SESSION_SETUP.sh" >&2
    exit 2
  fi
}
_need "scrip-cc"    "$([[ -x "$SCRIP_CC" && -s "$SCRIP_CC" ]] && echo 1 || echo 0)"

# Determine which tool families are needed based on requested cells.
# If no cells given, all cells run → all tools required.
_cells_arg="$*"
_needs_x86=0; _needs_jvm=0; _needs_wasm=0
if [[ -z "$_cells_arg" ]]; then
  _needs_x86=1; _needs_jvm=1
else
  echo "$_cells_arg" | grep -qE '(x86|x64|net)' && _needs_x86=1
  echo "$_cells_arg" | grep -qE 'jvm'            && _needs_jvm=1
  echo "$_cells_arg" | grep -qE 'wasm'           && _needs_wasm=1
fi

if [[ $_needs_x86 -eq 1 ]]; then
  _need "nasm"  "$(command -v nasm &>/dev/null && echo 1 || echo 0)"
fi
if [[ $_needs_jvm -eq 1 ]]; then
  _need "java"        "$(command -v java  &>/dev/null && echo 1 || echo 0)"
  _need "javac"       "$(command -v javac &>/dev/null && echo 1 || echo 0)"
  _need "SnoHarness"  "$([[ -f "$ROOT/test/jvm/SnoHarness.class" ]] && echo 1 || echo 0)"
fi
if [[ $_needs_wasm -eq 1 ]]; then
  _need "wat2wasm"  "$(command -v wat2wasm &>/dev/null && echo 1 || echo 0)"
  _need "node"      "$(command -v node    &>/dev/null && echo 1 || echo 0)"
fi
echo -e "${GREEN}  [tools] all required tools present ✓${RESET}"

trap 'rm -rf "$WORK"' EXIT

WORK=$(mktemp -d /tmp/inv_XXXXXX)
RESULTS="$WORK/results"
mkdir -p "$RESULTS"

# ── Persistent CSV report ─────────────────────────────────────────────────────
CSV_DIR="$ROOT/test-results"
mkdir -p "$CSV_DIR"
TS=$(date '+%Y%m%d_%H%M%S')
CSV="$CSV_DIR/invariants_${TS}.csv"
printf 'status,cell,test,detail,timestamp\n' > "$CSV"
export CSV

START_TIME=$(date +%s%N 2>/dev/null || date +%s)
START_HUMAN=$(date '+%Y-%m-%d %H:%M:%S')
echo -e "${BOLD}START   $START_HUMAN  run_invariants.sh${RESET}"

# ── Persistent runtime archive cache ─────────────────────────────────────────
# Rebuilt only when source stamp changes. Survives across sessions.

ensure_sno4_archive() {
  local out="$RT_CACHE/libsno4rt_asm.a"
  local stamp_file="$RT_CACHE/stamp"
  local cur_stamp; cur_stamp=$(md5sum "$RT/asm/snobol4_stmt_rt.c" 2>/dev/null | cut -d' ' -f1 || echo "x")
  if [[ -f "$out" && -f "$stamp_file" && "$(cat "$stamp_file" 2>/dev/null)" == "$cur_stamp" ]]; then
    return 0  # cache hit
  fi
  echo -e "${YELLOW}  [cache] Building libsno4rt_asm.a...${RESET}"
  mkdir -p "$RT_CACHE" /tmp/rtbuild_sno_$$
  gcc -O2 -c "$RT/asm/snobol4_stmt_rt.c"    -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o /tmp/rtbuild_sno_$$/stmt_rt.o    || return 1
  gcc -O2 -c "$RT/snobol4/snobol4.c"         -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o /tmp/rtbuild_sno_$$/snobol4.o    || return 1
  gcc -O2 -c "$RT/mock/mock_includes.c"       -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o /tmp/rtbuild_sno_$$/mock_inc.o   || return 1
  gcc -O2 -c "$RT/snobol4/snobol4_pattern.c" -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o /tmp/rtbuild_sno_$$/pat.o        || return 1
  gcc -O2 -c "$RT/mock/mock_engine.c"         -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o /tmp/rtbuild_sno_$$/mock_eng.o   || return 1
  gcc -O2 -c "$RT/asm/blk_alloc.c"            -I"$RT/asm"                               -w -o /tmp/rtbuild_sno_$$/blk_alloc.o  || return 1
  gcc -O2 -c "$RT/asm/blk_reloc.c"            -I"$RT/asm"                               -w -o /tmp/rtbuild_sno_$$/blk_reloc.o  || return 1
  ar rcs "$out" /tmp/rtbuild_sno_$$/*.o
  echo "$cur_stamp" > "$stamp_file"
  rm -rf /tmp/rtbuild_sno_$$
  echo -e "${GREEN}  [cache] libsno4rt_asm.a ready${RESET}"
}

ensure_prolog_archive() {
  local out="$RT_CACHE/libsno4rt_pl.a"
  local stamp_file="$RT_CACHE/stamp_pl"
  local cur_stamp; cur_stamp=$(md5sum "$ROOT/src/frontend/prolog/prolog_atom.c" 2>/dev/null | cut -d' ' -f1 || echo "x")
  if [[ -f "$out" && -f "$stamp_file" && "$(cat "$stamp_file" 2>/dev/null)" == "$cur_stamp" ]]; then
    return 0  # cache hit
  fi
  echo -e "${YELLOW}  [cache] Building libsno4rt_pl.a...${RESET}"
  mkdir -p "$RT_CACHE" /tmp/rtbuild_pl_$$
  gcc -O2 -c "$ROOT/src/frontend/prolog/prolog_atom.c"    -I"$ROOT/src/frontend/prolog" -w -o /tmp/rtbuild_pl_$$/atom.o    || return 1
  gcc -O2 -c "$ROOT/src/frontend/prolog/prolog_unify.c"   -I"$ROOT/src/frontend/prolog" -w -o /tmp/rtbuild_pl_$$/unify.o   || return 1
  gcc -O2 -c "$ROOT/src/frontend/prolog/prolog_builtin.c" -I"$ROOT/src/frontend/prolog" -w -o /tmp/rtbuild_pl_$$/builtin.o || return 1
  ar rcs "$out" /tmp/rtbuild_pl_$$/*.o
  echo "$cur_stamp" > "$stamp_file"
  rm -rf /tmp/rtbuild_pl_$$
  echo -e "${GREEN}  [cache] libsno4rt_pl.a ready${RESET}"
}

# Compile harness classes once per session
ensure_sno_harness() {
  local HARNESS_DIR="$ROOT/test/jvm"
  local W="$1"
  if [[ ! -f "$HARNESS_DIR/SnoHarness.class" ]]; then
    javac "$HARNESS_DIR/SnoRuntime.java" "$HARNESS_DIR/SnoHarness.java" -d "$HARNESS_DIR" 2>/dev/null || return 1
  fi
  cp "$HARNESS_DIR"/SnoHarness.class "$HARNESS_DIR"/SnoRuntime.class "$W/" 2>/dev/null
  cp "$HARNESS_DIR"/'SnoRuntime$SnoExitException.class' "$W/" 2>/dev/null || true
}

# ── CSV helper — append one row (safe for parallel use via >>)  ───────────────
csv_row() {
  local status="$1" cell="$2" test="$3" detail="${4:-}"
  local ts; ts=$(date '+%Y-%m-%d %H:%M:%S')
  printf '%s,%s,%s,%s,%s\n' "$status" "$cell" "$test" "$detail" "$ts" >> "$CSV"
}
export -f csv_row

# ── x86 compile worker — written as per-test mini-script (M-G-INV-FAST-X86-FIX)
# Root cause: exported bash functions are NOT visible inside `bash -c` subshells
# spawned by xargs (xargs exec's bash fresh; BASH_FUNC_* env vars only work when
# the parent bash itself forks, not via execve). Fix: bake the worker body + all
# required variable values into a self-contained mini-script per test, then let
# xargs -P invoke plain `bash <script>` — no function inheritance required.
#
# _x86_write_job JOBS_DIR sno ref input asm obj bin lib timeout verbose → writes NNN.sh
_x86_write_job() {
  local jobs_dir="$1" sno="$2" ref="$3" input="$4" asm="$5" obj="$6" bin="$7"
  local lib="$8" tmo="$9" verb="${10}"
  local base; base=$(basename "$sno" .sno)
  local n; n=$(ls "$jobs_dir" 2>/dev/null | wc -l)
  local script="$jobs_dir/$(printf '%05d' "$n").sh"
  cat > "$script" <<EOJOB
#!/usr/bin/env bash
set -uo pipefail
base=$(printf '%q' "$base")
sno=$(printf '%q' "$sno")
ref=$(printf '%q' "$ref")
input=$(printf '%q' "$input")
asm=$(printf '%q' "$asm")
obj=$(printf '%q' "$obj")
bin=$(printf '%q' "$bin")
lib=$(printf '%q' "$lib")
scrip_cc=$(printf '%q' "$SCRIP_CC")
rt_asm_inc=$(printf '%q' "${RT}/asm/")
tmo=$(printf '%q' "$tmo")
verb=$(printf '%q' "$verb")
"\$scrip_cc" -asm -o "\$asm" "\$sno" 2>/dev/null || { echo "COMPILE_FAIL \$base"; printf 'COMPILE_FAIL,snobol4_x86,%s,,\n' "\$base" >> "$CSV"; exit 0; }
nasm -f elf64 -I"\$rt_asm_inc" "\$asm" -o "\$obj" 2>/dev/null || { echo "ASM_FAIL \$base"; printf 'ASM_FAIL,snobol4_x86,%s,,\n' "\$base" >> "$CSV"; exit 0; }
gcc -O0 -no-pie "\$obj" "\$lib" -lgc -lm -o "\$bin" 2>/dev/null || { echo "LINK_FAIL \$base"; printf 'LINK_FAIL,snobol4_x86,%s,,\n' "\$base" >> "$CSV"; exit 0; }
stdin_src=/dev/null; [[ -f "\$input" ]] && stdin_src="\$input"
got=\$(timeout "\$tmo" "\$bin" < "\$stdin_src" 2>/dev/null) || got="__TIMEOUT__"
exp=\$(cat "\$ref")
if [[ "\$got" == "\$exp" ]]; then
  echo "PASS \$base"; printf 'PASS,snobol4_x86,%s,,\n' "\$base" >> "$CSV"
else
  echo "FAIL \$base"; printf 'FAIL,snobol4_x86,%s,,\n' "\$base" >> "$CSV"
fi
EOJOB
  chmod +x "$script"
}

# ── Suite: SNOBOL4 x86 ────────────────────────────────────────────────────────
# ── Suite: SNOBOL4 WASM ───────────────────────────────────────────────────────
run_snobol4_wasm() {
  local cell="snobol4_wasm"
  local pass=0 fail=0
  if [[ -z "$CORPUS" || ! -d "$CORPUS/crosscheck" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  if ! command -v wat2wasm &>/dev/null || ! command -v node &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  local WASM_RUNNER="$ROOT/test/wasm/run_wasm.js"
  if [[ ! -f "$WASM_RUNNER" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi

  local DIRS="hello rung4 rung3 rung2 rung8 rung9 rungW01 rungW02 rungW03 rungW04 rungW05 rungW06 rungW07"
  for dir in $DIRS; do
    local full="$CORPUS/crosscheck/$dir"
    [[ -d "$full" ]] || continue
    for sno in "$full"/*.sno; do
      [[ -f "$sno" ]] || continue
      local base; base=$(basename "$sno" .sno)
      local ref="${sno%.sno}.ref"; [[ -f "$ref" ]] || continue
      local xfail="${sno%.sno}.xfail"; [[ -f "$xfail" ]] && continue
      local wat="$W/${base}.wat"
      local wasm="$W/${base}.wasm"
      local got="$W/${base}.got"

      if ! "$SCRIP_CC" -wasm -o "$wat" "$sno" 2>/dev/null; then
        fail=$((fail+1)); echo "  FAIL $cell $base [compile]"; continue
      fi
      if ! wat2wasm --enable-tail-call "$wat" -o "$wasm" 2>/dev/null; then
        fail=$((fail+1)); echo "  FAIL $cell $base [wat2wasm]"; continue
      fi
      if ! timeout "$TIMEOUT_X86" node "$WASM_RUNNER" "$wasm" > "$got" 2>/dev/null; then
        fail=$((fail+1)); echo "  FAIL $cell $base [run/timeout]"; continue
      fi
      if diff -q "$ref" "$got" > /dev/null 2>&1; then
        pass=$((pass+1)); [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
        csv_row PASS "$cell" "$base"
      else
        fail=$((fail+1)); echo "  FAIL $cell $base [output]"
        csv_row FAIL "$cell" "$base"
      fi
    done
  done

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

run_snobol4_x86() {
  local cell="snobol4_x86"
  local pass=0 fail=0
  if [[ -z "$CORPUS" || ! -d "$CORPUS/crosscheck" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  ensure_sno4_archive || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  local LIB="$RT_CACHE/libsno4rt_asm.a"
  local W="$WORK/$cell"; mkdir -p "$W"
  local RT_ASM_INC="$RT/asm/"

  # Gather all test tuples into a temp file for xargs
  local manifest="$WORK/${cell}_manifest"
  local DIRS="output assign concat arith_new control_new patterns capture strings functions data keywords"
  for dir in $DIRS; do
    local full="$CORPUS/crosscheck/$dir"
    [[ -d "$full" ]] || continue
    for sno in "$full"/*.sno; do
      [[ -f "$sno" ]] || continue
      local base; base=$(basename "$sno" .sno)
      local ref="${sno%.sno}.ref"; [[ -f "$ref" ]] || continue
      local input="${sno%.sno}.input"
      [[ ! -f "$input" ]] && input="/dev/null"
      echo "$sno|$ref|$input|$W/${base}.s|$W/${base}.o|$W/${base}|$LIB|$TIMEOUT_X86|$VERBOSE"
    done
  done > "$manifest"

  # Write per-test mini-scripts and dispatch via xargs -P (M-G-INV-FAST-X86-FIX)
  # xargs bash -c cannot see exported bash functions; mini-scripts need no inheritance.
  local jobs_dir="$WORK/${cell}_jobs"; mkdir -p "$jobs_dir"
  local results_file="$WORK/${cell}_results"
  while IFS='|' read -r sno ref input asm obj bin lib tmo verb; do
    _x86_write_job "$jobs_dir" "$sno" "$ref" "$input" "$asm" "$obj" "$bin" "$lib" "$tmo" "$verb"
  done < "$manifest"

  ls "$jobs_dir"/*.sh 2>/dev/null | \
    xargs -P"$JOBS" -I{} bash {} > "$results_file" 2>/dev/null || true

  while IFS= read -r line; do
    case "$line" in
      PASS*) pass=$((pass+1)); [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell ${line#PASS }" ;;
      FAIL*|*_FAIL*) fail=$((fail+1)); echo "  FAIL $cell ${line}" ;;
    esac
  done < "$results_file"

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: SNOBOL4 JVM ────────────────────────────────────────────────────────
run_snobol4_jvm() {
  local cell="snobol4_jvm"
  local pass=0 fail=0
  if [[ -z "$CORPUS" || ! -d "$CORPUS/crosscheck" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  if ! command -v java &>/dev/null || [[ ! -f "$JASMIN" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  ensure_sno_harness "$W" || { echo "SKIP" > "$RESULTS/${cell}_status"; return; }

  # Step 1: compile all .sno → .j in parallel
  local DIRS="output assign concat arith_new control_new patterns capture strings functions data keywords rung2 rung3 rung10 rung11"
  local jfiles=()
  local compile_fail=0
  for dir in $DIRS; do
    local full="$CORPUS/crosscheck/$dir"
    [[ -d "$full" ]] || continue
    for sno in "$full"/*.sno; do
      [[ -f "$sno" ]] || continue
      local base; base=$(basename "$sno" .sno)
      local ref="${sno%.sno}.ref"; [[ -f "$ref" ]] || continue
      local input="${sno%.sno}.input"
      local jfile="$W/${base}.j"
      if "$SCRIP_CC" -jvm -o "$jfile" "$sno" 2>/dev/null; then
        # Extract class name — SnoHarness looks for <classname>.ref not <basename>.ref
        local classname; classname=$(grep '\.class public' "$jfile" | head -1 | awk '{print $3}')
        [[ -z "$classname" ]] && classname="$base"
        cp "$ref" "$W/${classname}.ref"
        [[ -f "$input" ]] && cp "$input" "$W/${classname}.input"
        jfiles+=("$jfile")
      else
        compile_fail=$((compile_fail+1))
      fi
    done
  done

  # Step 2: BATCH jasmin — one JVM startup for all .j files; 60s hard ceiling
  if [[ ${#jfiles[@]} -gt 0 ]]; then
    timeout 60 java -jar "$JASMIN" "${jfiles[@]}" -d "$W/" 2>/dev/null || true
  fi

  # Step 3: One JVM via SnoHarness; per-test timeout inside (3s), plus 120s suite ceiling
  local harness_out
  harness_out=$(timeout 120 java -cp "$W" SnoHarness "$W" "$W" "$W" 2>/dev/null) || true
  while IFS= read -r line; do
    case "$line" in
      PASS*)    pass=$((pass+1)); csv_row PASS "$cell" "${line#PASS }"; [[ $VERBOSE -eq 1 ]] && echo "  $cell $line" ;;
      FAIL*)    fail=$((fail+1)); csv_row FAIL "$cell" "${line#FAIL }"; echo "  FAIL $cell ${line#FAIL }" ;;
      TIMEOUT*) fail=$((fail+1)); csv_row TIMEOUT "$cell" "${line#TIMEOUT }"; echo "  TIMEOUT $cell ${line#TIMEOUT }" ;;
    esac
  done <<< "$harness_out"
  fail=$((fail+compile_fail))

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: SNOBOL4 .NET ───────────────────────────────────────────────────────
run_snobol4_net() {
  local cell="snobol4_net"
  if ! command -v mono &>/dev/null || ! command -v ilasm &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local result pass=0 fail=0
  result=$(bash "$ROOT/test/crosscheck/run_crosscheck_net.sh" 2>/dev/null | grep "Results:" | tail -1) || true
  pass=$(echo "$result" | grep -o '[0-9]* passed' | grep -o '[0-9]*' || echo 0)
  fail=$(echo "$result" | grep -o '[0-9]* failed' | grep -o '[0-9]*' || echo 0)
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Rung summary parser ───────────────────────────────────────────────────────
# Handles two formats emitted by icon rung scripts:
#   New: "--- rungNN: 5 pass, 2 fail, 0 xfail ---"  (25 scripts)
#   Old: "0 PASS  6 FAIL"                            (13 scripts)
# Sets caller's $p and $m variables.
_parse_rung_summary() {
  local result="$1"
  # New format: "X pass, Y fail"
  if echo "$result" | grep -q '[0-9]* pass,'; then
    p=$(echo "$result" | grep -o '[0-9]* pass'  | grep -o '[0-9]*' | head -1 || echo 0)
    m=$(echo "$result" | grep -o '[0-9]* fail'  | grep -o '[0-9]*' | head -1 || echo 0)
  else
    # Old format: "X PASS  Y FAIL"
    p=$(echo "$result" | grep -o '[0-9]* PASS'  | grep -o '[0-9]*' | head -1 || echo 0)
    m=$(echo "$result" | grep -o '[0-9]* FAIL'  | grep -o '[0-9]*' | head -1 || echo 0)
  fi
  p="${p:-0}"; m="${m:-0}"
}

# ── Suite: Icon x86 ───────────────────────────────────────────────────────────
run_icon_x86() {
  local cell="icon_x86"
  local pass=0 fail=0
  local ICN_INC="$ROOT/src/frontend/icon"
  local RT_H="$ROOT/src/runtime"
  local ICN_CORPUS="${CORPUS}/programs/icon"
  if [[ ! -x "$SCRIP_CC" || ! -d "$ICN_CORPUS" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  # Direct compile: .icn → .s → .o → bin (bypasses broken per-test JVM rung scripts)
  # icon_runtime.c compiled per-test alongside the test object (-nostdlib model)
  for icn in "$ICN_CORPUS"/rung*.icn; do
    [[ -f "$icn" ]] || continue
    local base; base=$(basename "$icn" .icn)
    local exp="${icn%.icn}.expected"; [[ -f "$exp" ]] || continue
    [[ -f "${icn%.icn}.xfail" ]] && { pass=$((pass+1)); continue; }
    local asm="$W/${base}.s" obj="$W/${base}.o" bin="$W/${base}"
    if "$SCRIP_CC" -icn "$icn" -o "$asm" 2>/dev/null &&
       nasm -f elf64 "$asm" -o "$obj" 2>/dev/null &&
       gcc -nostdlib -no-pie -Wl,--no-warn-execstack \
           "$obj" "$ICN_INC/icon_runtime.c" \
           -I"$ICN_INC" -I"$RT_H/snobol4" -I"$RT_H" \
           -o "$bin" -lm 2>/dev/null; then
      local got; got=$(timeout "$TIMEOUT_X86" "$bin" 2>/dev/null) || got="__TIMEOUT__"
      if [[ "$got" == "$(cat "$exp")" ]]; then
        pass=$((pass+1)); csv_row PASS "$cell" "$base"
      else
        fail=$((fail+1)); csv_row FAIL "$cell" "$base"; echo "  FAIL $cell $base"
      fi
    else
      fail=$((fail+1)); csv_row COMPILE_FAIL "$cell" "$base" "compile"; echo "  FAIL $cell $base [compile]"
    fi
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: Icon JVM ───────────────────────────────────────────────────────────
run_icon_jvm() {
  local cell="icon_jvm"
  local pass=0 fail=0 compile_fail=0
  local ICN_CORPUS="${CORPUS}/programs/icon"
  if ! command -v java &>/dev/null || [[ ! -f "$JASMIN" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  if [[ ! -d "$ICN_CORPUS" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  ensure_sno_harness "$W" || { echo "SKIP" > "$RESULTS/${cell}_status"; return; }

  # Compile all .icn → .j, extract class name for ref copy
  local jfiles=()
  for icn in "$ICN_CORPUS"/rung*.icn; do
    [[ -f "$icn" ]] || continue
    local base; base=$(basename "$icn" .icn)
    local exp="${icn%.icn}.expected"; [[ -f "$exp" ]] || continue
    [[ -f "${icn%.icn}.xfail" ]] && continue
    local jfile="$W/${base}.j"
    if "$SCRIP_CC" -jvm -o "$jfile" "$icn" 2>/dev/null; then
      local classname; classname=$(grep '\.class public' "$jfile" | head -1 | awk '{print $3}')
      [[ -z "$classname" ]] && classname="$base"
      cp "$exp" "$W/${classname}.ref"
      jfiles+=("$jfile")
    else
      compile_fail=$((compile_fail+1))
    fi
  done

  # Batch jasmin — one JVM startup
  [[ ${#jfiles[@]} -gt 0 ]] && \
    timeout 60 java -jar "$JASMIN" "${jfiles[@]}" -d "$W/" 2>/dev/null || true

  # One SnoHarness run for all icon JVM tests
  local harness_out
  harness_out=$(timeout 120 java -cp "$W" SnoHarness "$W" "$W" "$W" 2>/dev/null) || true
  while IFS= read -r line; do
    case "$line" in
      PASS*)    pass=$((pass+1)); csv_row PASS "$cell" "${line#PASS }" ;;
      FAIL*)    fail=$((fail+1)); csv_row FAIL "$cell" "${line#FAIL }"; echo "  FAIL $cell ${line#FAIL }" ;;
      TIMEOUT*) fail=$((fail+1)); csv_row TIMEOUT "$cell" "${line#TIMEOUT }"; echo "  TIMEOUT $cell ${line#TIMEOUT }" ;;
    esac
  done <<< "$harness_out"
  fail=$((fail+compile_fail))
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: Icon WASM ──────────────────────────────────────────────────────────
# IW-session owns this cell. Run: run_invariants.sh icon_wasm
# Structural oracle: ByrdBox/test_icon-4.py + jcon-master/tran/irgen.icn
# Emitter: src/backend/emit_wasm_icon.c (M-IW-SCAFFOLD → M-IW-A01+)
run_icon_wasm() {
  local cell="icon_wasm"
  local pass=0 fail=0
  local ICN_CORPUS="${CORPUS}/programs/icon"
  if [[ ! -x "$SCRIP_CC" || ! -d "$ICN_CORPUS" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  if ! command -v wat2wasm &>/dev/null || ! command -v node &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local WASM_RUNNER="$ROOT/test/wasm/run_wasm.js"
  if [[ ! -f "$WASM_RUNNER" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"

  for icn in "$ICN_CORPUS"/rung*.icn; do
    [[ -f "$icn" ]] || continue
    local base; base=$(basename "$icn" .icn)
    local exp="${icn%.icn}.expected"; [[ -f "$exp" ]] || continue
    [[ -f "${icn%.icn}.xfail" ]] && { pass=$((pass+1)); continue; }
    local wat="$W/${base}.wat"
    local wasm="$W/${base}.wasm"
    local got="$W/${base}.got"

    # compile: scrip-cc -icn -wasm → .wat
    if ! "$SCRIP_CC" -icn -wasm -o "$wat" "$icn" 2>/dev/null; then
      fail=$((fail+1)); echo "  FAIL $cell $base [compile]"; continue
    fi
    # assemble: .wat → .wasm
    if ! wat2wasm --enable-tail-call "$wat" -o "$wasm" 2>/dev/null; then
      fail=$((fail+1)); echo "  FAIL $cell $base [wat2wasm]"; continue
    fi
    # run
    if ! timeout "$TIMEOUT_X86" node "$WASM_RUNNER" "$wasm" > "$got" 2>/dev/null; then
      fail=$((fail+1)); echo "  FAIL $cell $base [run/timeout]"; continue
    fi
    if diff -q "$exp" "$got" > /dev/null 2>&1; then
      pass=$((pass+1)); [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
      csv_row PASS "$cell" "$base"
    else
      fail=$((fail+1)); echo "  FAIL $cell $base [output]"
      csv_row FAIL "$cell" "$base"
    fi
  done

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: Prolog x86 ─────────────────────────────────────────────────────────
run_prolog_x86() {
  local cell="prolog_x86"
  if ! command -v nasm &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  ensure_sno4_archive  || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  ensure_prolog_archive || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  local SNO_LIB="$RT_CACHE/libsno4rt_asm.a"
  local PL_LIB="$RT_CACHE/libsno4rt_pl.a"
  local W="$WORK/$cell"; mkdir -p "$W"
  local PL_CORPUS="${CORPUS:-$ROOT/../corpus}/programs/prolog"
  local rpass=0 rfail=0
  for pl in "$PL_CORPUS"/*.pl; do
    [[ -f "$pl" ]] || continue
    local base; base=$(basename "$pl" .pl)
    local expected="${pl%.pl}.expected"; [[ -f "$expected" ]] || continue
    local xfail="${pl%.pl}.xfail"
    [[ -f "$xfail" ]] && { rpass=$((rpass+1)); continue; }
    local asm="$W/${base}.s" obj="$W/${base}.o" bin="$W/${base}"
    if "$SCRIP_CC" -pl -asm -o "$asm" "$pl" 2>/dev/null &&
       nasm -f elf64 "$asm" -o "$obj" 2>/dev/null &&
       gcc -O0 -no-pie "$obj" "$PL_LIB" -lm -o "$bin" 2>/dev/null; then
      local got; got=$(timeout "$TIMEOUT_X86" "$bin" 2>/dev/null) || got="__FAIL__"
      if [[ "$got" == "$(cat "$expected")" ]]; then
        rpass=$((rpass+1)); csv_row PASS "$cell" "$base"; [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
      else
        rfail=$((rfail+1)); csv_row FAIL "$cell" "$base"; echo "  FAIL $cell $base"
      fi
    else
      rfail=$((rfail+1)); csv_row COMPILE_FAIL "$cell" "$base" "compile"; echo "  FAIL $cell $base [compile]"
    fi
  done
  echo "$rpass" > "$RESULTS/${cell}_pass"
  echo "$rfail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: Prolog JVM ─────────────────────────────────────────────────────────
# OPTIMIZED: batch all jasmin per rung, one SnoHarness JVM per rung
run_prolog_jvm() {
  local cell="prolog_jvm"
  local pass=0 fail=0
  if ! command -v java &>/dev/null || [[ ! -f "$JASMIN" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  ensure_sno_harness "$W" || { echo "SKIP" > "$RESULTS/${cell}_status"; return; }

  # Compile ALL prolog tests at once, batch jasmin, one SnoHarness run
  local PL_CORPUS="${CORPUS:-$ROOT/../corpus}/programs/prolog"
  local jfiles=() compile_fail=0
  for pl in "$PL_CORPUS"/*.pl; do
    [[ -f "$pl" ]] || continue
    local base; base=$(basename "$pl" .pl)
    local expected="${pl%.pl}.expected"; [[ -f "$expected" ]] || continue
    local xfail="${pl%.pl}.xfail"; [[ -f "$xfail" ]] && continue
    local jfile="$W/${base}.j"
    if "$SCRIP_CC" -pl -jvm -o "$jfile" "$pl" 2>/dev/null; then
      local classname; classname=$(grep '\.class public' "$jfile" | head -1 | awk '{print $3}')
      [[ -z "$classname" ]] && classname="$base"
      cp "$expected" "$W/${classname}.ref"
      jfiles+=("$jfile")
    else
      compile_fail=$((compile_fail+1))
      echo "  FAIL $cell $base [compile]"
    fi
  done

  # Batch jasmin — 60s hard ceiling
  if [[ ${#jfiles[@]} -gt 0 ]]; then
    timeout 60 java -jar "$JASMIN" "${jfiles[@]}" -d "$W/" 2>/dev/null || true
  fi

  # One SnoHarness for all prolog JVM tests; per-test 3s inside, 120s suite ceiling
  local harness_out
  harness_out=$(timeout 120 java -cp "$W" SnoHarness "$W" "$W" "$W" 2>/dev/null) || true
  while IFS= read -r line; do
    case "$line" in
      PASS*) pass=$((pass+1)); csv_row PASS "$cell" "${line#PASS }"; [[ $VERBOSE -eq 1 ]] && echo "  $cell $line" ;;
      FAIL*) fail=$((fail+1)); csv_row FAIL "$cell" "${line#FAIL }"; echo "  FAIL $cell ${line#FAIL }" ;;
      TIMEOUT*) fail=$((fail+1)); csv_row TIMEOUT "$cell" "${line#TIMEOUT }"; echo "  TIMEOUT $cell ${line#TIMEOUT }" ;;
    esac
  done <<< "$harness_out"
  fail=$((fail+compile_fail))

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: Snocone x86 ───────────────────────────────────────────────────────
run_snocone_x86() {
  local cell="snocone_x86"
  local pass=0 fail=0
  if [[ -z "$CORPUS" || ! -d "$CORPUS/crosscheck/snocone" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local SC_RUNNER="$ROOT/test/crosscheck/run_sc_corpus_rung.sh"
  if [[ ! -f "$SC_RUNNER" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local DIRS="rungA01 rungA02 rungA03 rungA04 rungA05 rungA06 rungA07 rungA08 rungA09 rungA10 rungA11 rungA12 rungA13 rungA14 rungA15 rungA16 rungB01 rungB02 rungB03 rungB04 rungB05"
  local dir_args=()
  for d in $DIRS; do
    local full="$CORPUS/crosscheck/snocone/$d"
    [[ -d "$full" ]] && dir_args+=("$full")
  done
  if [[ ${#dir_args[@]} -eq 0 ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local raw stripped
  raw=$(SCRIP_CC="$SCRIP_CC" bash "$SC_RUNNER" "${dir_args[@]}" 2>/dev/null) || true
  stripped=$(echo "$raw" | sed 's/\x1b\[[0-9;]*m//g')
  pass=$(echo "$stripped" | grep -c '^PASS' 2>/dev/null | tr -d '[:space:]'); pass=${pass:-0}
  fail=$(echo "$stripped" | grep -c '^FAIL' 2>/dev/null | tr -d '[:space:]'); fail=${fail:-0}
  # propagate individual FAIL lines for visibility
  echo "$stripped" | grep '^FAIL' | while IFS= read -r ln; do
    echo "  FAIL $cell ${ln#FAIL }"
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Serial dispatch — filtered by requested cells ─────────────────────────────
# If one or more cell names are passed as args, only those suites run.
# If no args given, all suites run (G-sessions / full baseline check).
# Parallel dispatch caused suite-level timeouts; serial execution is reliable.
_run_cell() {
  local cell="$1"
  # If cells were requested, skip unless this cell was listed
  if [[ -n "$_cells_arg" ]] && ! echo "$_cells_arg" | grep -qw "$cell"; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  case "$cell" in
    snobol4_wasm) run_snobol4_wasm ;;
    snobol4_x86)  run_snobol4_x86  ;;
    snobol4_jvm)  run_snobol4_jvm  ;;
    snobol4_net)  run_snobol4_net  ;;
    icon_x86)     run_icon_x86     ;;
    icon_jvm)     run_icon_jvm     ;;
    icon_wasm)    run_icon_wasm    ;;
    prolog_x86)   run_prolog_x86   ;;
    prolog_jvm)   run_prolog_jvm   ;;
    prolog_wasm)  run_prolog_wasm  ;;
    snocone_x86)  run_snocone_x86  ;;
  esac
}

for _cell in snobol4_wasm snobol4_x86 snobol4_jvm snobol4_net \
             icon_x86 icon_jvm icon_wasm \
             prolog_x86 prolog_jvm prolog_wasm \
             snocone_x86; do
  _run_cell "$_cell"
done

# ── Results matrix ────────────────────────────────────────────────────────────
END_TIME=$(date +%s%N 2>/dev/null || date +%s)
if [[ ${#START_TIME} -gt 10 ]]; then
  ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))
  ELAPSED_S=$(awk "BEGIN{printf \"%.1f\", $ELAPSED_MS/1000}")
else
  ELAPSED_MS=$(( (END_TIME - START_TIME) * 1000 ))
  ELAPSED_S=$((END_TIME - START_TIME))
fi
FINISH_HUMAN=$(date '+%Y-%m-%d %H:%M:%S')

# ── Suite: Prolog WASM ────────────────────────────────────────────────────────
# PW-session owns this cell. Run: run_invariants.sh prolog_wasm
# Emitter: src/backend/emit_wasm_prolog.c (M-PW-HELLO → M-PW-PARITY)
run_prolog_wasm() {
  local cell="prolog_wasm"
  local pass=0 fail=0
  if ! command -v wat2wasm &>/dev/null || ! command -v node &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  local PL_CORPUS="${CORPUS:-$ROOT/../corpus}/programs/prolog"
  local PL_RUNNER="$ROOT/test/wasm/pl_run_wasm.js"
  local PL_RUNTIME="$ROOT/src/runtime/wasm/pl_runtime.wasm"
  if [[ ! -f "$PL_RUNNER" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  if [[ ! -f "$PL_RUNTIME" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  for pl in "$PL_CORPUS"/*.pl; do
    [[ -f "$pl" ]] || continue
    local base; base=$(basename "$pl" .pl)
    local expected="${pl%.pl}.expected"; [[ -f "$expected" ]] || continue
    local xfail="${pl%.pl}.xfail"
    [[ -f "$xfail" ]] && { pass=$((pass+1)); continue; }
    local wat="$W/${base}.wat"
    local wasm="$W/${base}.wasm"
    if ! "$SCRIP_CC" -pl -wasm -o "$wat" "$pl" 2>/dev/null; then
      fail=$((fail+1)); echo "  FAIL $cell $base [compile]"
      csv_row COMPILE_FAIL "$cell" "$base" "compile"; continue
    fi
    if ! wat2wasm --enable-tail-call "$wat" -o "$wasm" 2>/dev/null; then
      fail=$((fail+1)); echo "  FAIL $cell $base [wat2wasm]"
      csv_row COMPILE_FAIL "$cell" "$base" "wat2wasm"; continue
    fi
    local got; got=$(timeout "$TIMEOUT_X86" node "$PL_RUNNER" "$wasm" "$PL_RUNTIME" 2>/dev/null) || got="__FAIL__"
    if [[ "$got" == "$(cat "$expected")" ]]; then
      pass=$((pass+1)); csv_row PASS "$cell" "$base"
      [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
    else
      fail=$((fail+1)); echo "  FAIL $cell $base [output]"
      csv_row FAIL "$cell" "$base"
    fi
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

any_fail() {
  local cell="$1"
  [[ -f "$RESULTS/${cell}_fail" ]] && [[ $(cat "$RESULTS/${cell}_fail") -gt 0 ]]
}

echo ""
echo -e "${BOLD}Invariants — 3×4 matrix${RESET}"
echo -e "${BOLD}                x86              JVM             .NET            WASM${RESET}"

for row_label in "SNOBOL4" "Icon   " "Prolog " "Snocone"; do
  case $row_label in
    "SNOBOL4") cells="snobol4_x86 snobol4_jvm snobol4_net snobol4_wasm" ;;
    "Icon   ") cells="icon_x86    icon_jvm    icon_net    icon_wasm"    ;;
    "Prolog ") cells="prolog_x86  prolog_jvm  prolog_net  prolog_wasm"  ;;
    "Snocone") cells="snocone_x86 snocone_jvm snocone_net snocone_wasm" ;;
  esac
  printf "  %s  " "$row_label"
  for cell in $cells; do
    if [[ "$cell" == "icon_net" || "$cell" == "prolog_net" || \
          "$cell" == "icon_wasm" || \
          "$cell" == "snocone_jvm" || "$cell" == "snocone_net" || "$cell" == "snocone_wasm" ]]; then
      printf "  ${YELLOW}%-14s${RESET}" "SKIP"
    elif [[ -f "$RESULTS/${cell}_status" ]]; then
      printf "  %-14s" "$(cat "$RESULTS/${cell}_status")"
    else
      p=0; f=0
      [[ -f "$RESULTS/${cell}_pass" ]] && p=$(cat "$RESULTS/${cell}_pass")
      [[ -f "$RESULTS/${cell}_fail" ]] && f=$(cat "$RESULTS/${cell}_fail")
      if [[ $f -eq 0 ]]; then printf "  ${GREEN}%-14s${RESET}" "${p} ✓"
      else printf "  ${RED}%-14s${RESET}" "${p}p/${f}f ✗"; fi
    fi
  done
  echo ""
done

echo -e "${BOLD}──────────────────────────────────────────────────${RESET}"

OVERALL_FAIL=0
for cell in snobol4_x86 snobol4_jvm snobol4_net snobol4_wasm icon_x86 icon_jvm icon_wasm prolog_x86 prolog_jvm prolog_wasm snocone_x86; do
  any_fail "$cell" && OVERALL_FAIL=1
done

echo -e "${BOLD}START   $START_HUMAN${RESET}"
echo -e "${BOLD}FINISH  $FINISH_HUMAN${RESET}"
echo -e "${BOLD}ELAPSED ${ELAPSED_MS}ms  (${ELAPSED_S}s)${RESET}"
echo ""

# Finalise CSV — summary rows per cell + latest symlink
for cell in snobol4_x86 snobol4_jvm snobol4_net snobol4_wasm icon_x86 icon_jvm icon_wasm prolog_x86 prolog_jvm prolog_wasm snocone_x86; do
  p=0; f=0
  [[ -f "$RESULTS/${cell}_pass" ]] && p=$(cat "$RESULTS/${cell}_pass")
  [[ -f "$RESULTS/${cell}_fail" ]] && f=$(cat "$RESULTS/${cell}_fail")
  status=$(cat "$RESULTS/${cell}_status" 2>/dev/null || echo "")
  [[ -n "$status" ]] && printf 'SKIP,%s,_summary,,%s\n' "$cell" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$CSV" \
                     || printf 'SUMMARY,%s,_summary,pass=%d fail=%d,%s\n' "$cell" "$p" "$f" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$CSV"
done
ln -sf "$(basename "$CSV")" "$CSV_DIR/invariants_latest.csv"
echo -e "${BOLD}Report: $CSV${RESET}"

if [[ $OVERALL_FAIL -eq 0 ]]; then
  echo -e "${GREEN}${BOLD}  ALL PASS${RESET}"
  exit 0
else
  echo -e "${RED}${BOLD}  FAILURES PRESENT${RESET}"
  exit 1
fi
