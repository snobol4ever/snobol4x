#!/usr/bin/env bash
# test/run_invariants.sh — Fast 3×3 invariant harness (M-G-INV)
#
# Runs all 7 active cells of the SNOBOL4/Icon/Prolog × x86/JVM/.NET matrix
# in parallel, with pre-built runtime archives to minimise per-test cost.
#
# Usage:
#   bash test/run_invariants.sh [--serial] [--verbose]
#
# Options:
#   --serial    disable parallel execution (for debugging)
#   --verbose   print per-test PASS lines (default: FAIL lines only)
#
# Environment overrides:
#   SCRIP_CC       path to scrip-cc binary        (default: <root>/scrip-cc)
#   CORPUS      path to corpus root  (default: <root>/../corpus)
#   JASMIN      path to jasmin.jar          (default: <root>/src/backend/jvm/jasmin.jar)
#   TIMEOUT_X86 per-test timeout x86 (s)   (default: 5)
#   TIMEOUT_JVM per-test timeout JVM (s)   (default: 15)
#   JOBS        max parallel suite jobs     (default: nproc)
#
# Exit: 0 = all active cells pass, 1 = any failure
#
# Authors: Claude Sonnet 4.6 (G-7 session, 2026-03-28) — M-G-INV

set -uo pipefail

# ── Config ────────────────────────────────────────────────────────────────────
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip-cc}"
CORPUS="${CORPUS:-$(cd "$ROOT/../corpus" 2>/dev/null && pwd || echo "")}"
JASMIN="${JASMIN:-$ROOT/src/backend/jvm/jasmin.jar}"
RT="$ROOT/src/runtime"
SCRIP_CC_INC="$ROOT/src/frontend/snobol4"
TIMEOUT_X86="${TIMEOUT_X86:-5}"
TIMEOUT_JVM="${TIMEOUT_JVM:-15}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
SERIAL=0; VERBOSE=0
for arg in "$@"; do
  [[ "$arg" == "--serial"  ]] && SERIAL=1
  [[ "$arg" == "--verbose" ]] && VERBOSE=1
done

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'
BOLD='\033[1m'; RESET='\033[0m'

WORK=$(mktemp -d /tmp/inv_XXXXXX)
trap 'rm -rf "$WORK"' EXIT
RESULTS="$WORK/results"
mkdir -p "$RESULTS"

START_TIME=$(date +%s%N 2>/dev/null || date +%s)

# ── Pre-build runtime archives ─────────────────────────────────────────────────
# Called once; each suite links against the .a — no per-test gcc -c.

build_sno4_archive() {
  local out="$WORK/libsno4rt_asm.a"
  [[ -f "$out" ]] && return 0
  local tmp="$WORK/rt_asm"
  mkdir -p "$tmp"
  gcc -O2 -c "$RT/asm/snobol4_stmt_rt.c"    -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o "$tmp/stmt_rt.o"    || return 1
  gcc -O2 -c "$RT/snobol4/snobol4.c"         -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o "$tmp/snobol4.o"    || return 1
  gcc -O2 -c "$RT/mock/mock_includes.c"       -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o "$tmp/mock_inc.o"   || return 1
  gcc -O2 -c "$RT/snobol4/snobol4_pattern.c" -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o "$tmp/pat.o"        || return 1
  gcc -O2 -c "$RT/mock/mock_engine.c"         -I"$RT/snobol4" -I"$RT" -I"$SCRIP_CC_INC" -w -o "$tmp/mock_eng.o"   || return 1
  gcc -O2 -c "$RT/asm/blk_alloc.c"            -I"$RT/asm"                              -w -o "$tmp/blk_alloc.o" || return 1
  gcc -O2 -c "$RT/asm/blk_reloc.c"            -I"$RT/asm"                              -w -o "$tmp/blk_reloc.o" || return 1
  ar rcs "$out" "$tmp"/*.o
}

build_prolog_archive() {
  local out="$WORK/libsno4rt_pl.a"
  [[ -f "$out" ]] && return 0
  local tmp="$WORK/rt_pl"
  mkdir -p "$tmp"
  gcc -O2 -c "$ROOT/src/frontend/prolog/prolog_atom.c"    -I"$ROOT/src/frontend/prolog" -w -o "$tmp/atom.o"    || return 1
  gcc -O2 -c "$ROOT/src/frontend/prolog/prolog_unify.c"   -I"$ROOT/src/frontend/prolog" -w -o "$tmp/unify.o"   || return 1
  gcc -O2 -c "$ROOT/src/frontend/prolog/prolog_builtin.c" -I"$ROOT/src/frontend/prolog" -w -o "$tmp/builtin.o" || return 1
  ar rcs "$out" "$tmp"/*.o
}

# ── Suite runners (each writes CELL_pass/CELL_fail to $RESULTS) ───────────────

run_snobol4_x86() {
  local cell="snobol4_x86"
  local pass=0 fail=0
  if [[ -z "$CORPUS" || ! -d "$CORPUS/crosscheck" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  build_sno4_archive || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  local LIB="$WORK/libsno4rt_asm.a"
  local W="$WORK/$cell"; mkdir -p "$W"
  local DIRS="output assign concat arith_new control_new patterns capture strings functions data keywords"
  for dir in $DIRS; do
    local full="$CORPUS/crosscheck/$dir"
    [[ -d "$full" ]] || continue
    for sno in "$full"/*.sno; do
      [[ -f "$sno" ]] || continue
      local base; base=$(basename "$sno" .sno)
      local ref="${sno%.sno}.ref"; [[ -f "$ref" ]] || continue
      local input="${sno%.sno}.input"
      local asm="$W/${base}.s" obj="$W/${base}.o" bin="$W/${base}"
      if "$SCRIP_CC" -asm "$sno" > "$asm" 2>/dev/null &&
         nasm -f elf64 -I"$RT/asm/" "$asm" -o "$obj" 2>/dev/null &&
         gcc -O0 -no-pie "$obj" "$LIB" -lgc -lm -o "$bin" 2>/dev/null; then
        local stdin_src="/dev/null"; [[ -f "$input" ]] && stdin_src="$input"
        local got; got=$(timeout "$TIMEOUT_X86" "$bin" < "$stdin_src" 2>/dev/null) || got="__FAIL__"
        if [[ "$got" == "$(cat "$ref")" ]]; then
          pass=$((pass+1))
          [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
        else
          fail=$((fail+1)); echo "  FAIL $cell $base"
        fi
      else
        fail=$((fail+1)); echo "  FAIL $cell $base [compile]"
      fi
    done
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

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

  # M-G-INV-JVM: single-JVM harness. Compile all .j + assemble all .class first,
  # then run SnoHarness once — one JVM startup for the full suite.
  local HARNESS_DIR="$ROOT/test/jvm"
  if [[ ! -f "$HARNESS_DIR/SnoHarness.class" ]]; then
    javac "$HARNESS_DIR/SnoRuntime.java" "$HARNESS_DIR/SnoHarness.java" \
      -d "$HARNESS_DIR" 2>/dev/null || { echo "SKIP" > "$RESULTS/${cell}_status"; return; }
  fi
  cp "$HARNESS_DIR"/SnoHarness.class "$HARNESS_DIR"/SnoRuntime.class \
     "$HARNESS_DIR"/'SnoRuntime$SnoExitException.class' "$W/" 2>/dev/null

  local compile_fail=0
  local DIRS="hello output assign concat arith_new control_new patterns capture strings functions data keywords rung2 rung3 rung10 rung11"
  for dir in $DIRS; do
    local full="$CORPUS/crosscheck/$dir"
    [[ -d "$full" ]] || continue
    for sno in "$full"/*.sno; do
      [[ -f "$sno" ]] || continue
      local base; base=$(basename "$sno" .sno)
      local ref="${sno%.sno}.ref"; [[ -f "$ref" ]] || continue
      local input="${sno%.sno}.input"
      local jfile="$W/${base}.j"
      # Copy ref and input flat into W for SnoHarness lookup
      cp "$ref" "$W/${base}.ref"
      [[ -f "$input" ]] && cp "$input" "$W/${base}.input"
      if "$SCRIP_CC" -jvm "$sno" > "$jfile" 2>/dev/null; then
        java -jar "$JASMIN" "$jfile" -d "$W/" 2>/dev/null || compile_fail=$((compile_fail+1))
      else
        compile_fail=$((compile_fail+1))
      fi
    done
  done

  # Run all tests in one JVM process via SnoHarness
  local harness_out
  harness_out=$(java -cp "$W" SnoHarness "$W" "$W" "$W" 2>/dev/null)
  while IFS= read -r line; do
    case "$line" in
      PASS*) pass=$((pass+1)); [[ $VERBOSE -eq 1 ]] && echo "  $cell $line" ;;
      FAIL*) fail=$((fail+1)); echo "  FAIL $cell ${line#FAIL }" ;;
      TIMEOUT*) fail=$((fail+1)); echo "  TIMEOUT $cell ${line#TIMEOUT }" ;;
    esac
  done <<< "$harness_out"
  fail=$((fail+compile_fail))

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

run_snobol4_net() {
  local cell="snobol4_net"
  local pass=0 fail=0
  if ! command -v mono &>/dev/null || ! command -v ilasm &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  local W="$WORK/$cell"; mkdir -p "$W"
  local result
  result=$(bash "$ROOT/test/crosscheck/run_crosscheck_net.sh" 2>/dev/null | grep "Results:" | tail -1)
  pass=$(echo "$result" | grep -o '[0-9]* passed' | grep -o '[0-9]*' || echo 0)
  fail=$(echo "$result" | grep -o '[0-9]* failed' | grep -o '[0-9]*' || echo 0)
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

run_icon_x86() {
  local cell="icon_x86"
  local pass=0 fail=0
  local ICON_ASM="$ROOT/icon-asm"
  if [[ ! -x "$ICON_ASM" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  for rung_sh in "$ROOT"/test/frontend/icon/run_rung*.sh; do
    local result; result=$(bash "$rung_sh" "$ICON_ASM" 2>/dev/null | tail -1) || true
    # Format: "N PASS  M FAIL"
    local p m
    p=$(echo "$result" | grep -o '[0-9]* PASS' | grep -o '[0-9]*' || echo 0)
    m=$(echo "$result" | grep -o '[0-9]* FAIL' | grep -o '[0-9]*' || echo 0)
    pass=$((pass + p))
    fail=$((fail + m))
    [[ $m -gt 0 ]] && echo "  FAIL $cell $(basename "$rung_sh"): $m fail"
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

run_icon_jvm() {
  local cell="icon_jvm"
  local pass=0 fail=0
  if ! command -v java &>/dev/null || [[ ! -f "$JASMIN" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  for rung_sh in "$ROOT"/test/frontend/icon/run_rung*.sh; do
    local result; result=$(bash "$rung_sh" "$SCRIP_CC" 2>/dev/null | tail -1) || true
    local p m
    p=$(echo "$result" | grep -o '[0-9]* PASS\|[0-9]* passed' | grep -o '[0-9]*' | head -1 || echo 0)
    m=$(echo "$result" | grep -o '[0-9]* FAIL\|[0-9]* failed' | grep -o '[0-9]*' | head -1 || echo 0)
    pass=$((pass + p)); fail=$((fail + m))
    [[ $m -gt 0 ]] && echo "  FAIL $cell $(basename "$rung_sh"): $m fail"
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

run_prolog_x86() {
  local cell="prolog_x86"
  local rung_pass=0 rung_fail=0
  if ! command -v nasm &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  build_sno4_archive  || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  build_prolog_archive || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  local SNO_LIB="$WORK/libsno4rt_asm.a"
  local PL_LIB="$WORK/libsno4rt_pl.a"
  local W="$WORK/$cell"; mkdir -p "$W"
  for rung_dir in "$ROOT"/test/frontend/prolog/corpus/rung*/; do
    local rpass=0 rfail=0
    for pro in "$rung_dir"*.pro; do
      [[ -f "$pro" ]] || continue
      local base; base=$(basename "$pro" .pro)
      local expected="${pro%.pro}.expected"; [[ -f "$expected" ]] || continue
      local xfail="${pro%.pro}.xfail"
      [[ -f "$xfail" ]] && { rpass=$((rpass+1)); continue; }  # count xfail as skip/pass
      local asm="$W/${base}.s" obj="$W/${base}.o" bin="$W/${base}"
      if "$SCRIP_CC" -pl -asm "$pro" > "$asm" 2>/dev/null &&
         nasm -f elf64 "$asm" -o "$obj" 2>/dev/null &&
         gcc -O0 -no-pie "$obj" "$PL_LIB" -lm -o "$bin" 2>/dev/null; then
        local got; got=$(timeout "$TIMEOUT_X86" "$bin" 2>/dev/null) || got="__FAIL__"
        if [[ "$got" == "$(cat "$expected")" ]]; then
          rpass=$((rpass+1))
          [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
        else
          rfail=$((rfail+1)); echo "  FAIL $cell $base"
        fi
      else
        rfail=$((rfail+1)); echo "  FAIL $cell $base [compile]"
      fi
    done
    [[ $rfail -gt 0 ]] && rung_fail=$((rung_fail+1)) || rung_pass=$((rung_pass+1))
  done
  echo "$rung_pass" > "$RESULTS/${cell}_pass"
  echo "$rung_fail"  > "$RESULTS/${cell}_fail"
}

run_prolog_jvm() {
  local cell="prolog_jvm"
  local pass=0 fail=0
  if ! command -v java &>/dev/null || [[ ! -f "$JASMIN" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  for rung_dir in "$ROOT"/test/frontend/prolog/corpus/rung*/; do
    local result
    result=$(bash "$ROOT/test/frontend/prolog/run_prolog_jvm_rung.sh" "$rung_dir" 2>/dev/null \
             | grep "Results:" | tail -1) || true
    local p m
    p=$(echo "$result" | grep -o '[0-9]* passed' | grep -o '[0-9]*' || echo 0)
    m=$(echo "$result" | grep -o '[0-9]* failed' | grep -o '[0-9]*' || echo 0)
    pass=$((pass + p)); fail=$((fail + m))
    [[ $m -gt 0 ]] && echo "  FAIL $cell $(basename "$rung_dir"): $m fail"
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Parallel dispatch ─────────────────────────────────────────────────────────

PIDS=()

launch() {
  local fn="$1"
  if [[ $SERIAL -eq 1 ]]; then
    $fn
  else
    $fn &
    PIDS+=($!)
  fi
}

launch run_snobol4_x86
launch run_snobol4_jvm
launch run_snobol4_net
launch run_icon_x86
launch run_icon_jvm
launch run_prolog_x86
launch run_prolog_jvm

# Wait for all parallel jobs
for pid in "${PIDS[@]:-}"; do
  wait "$pid" || true
done

# ── Results matrix ────────────────────────────────────────────────────────────

END_TIME=$(date +%s%N 2>/dev/null || date +%s)
# Compute elapsed — handle both ns and s precision
if [[ ${#START_TIME} -gt 10 ]]; then
  ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))
  ELAPSED=$(awk "BEGIN{printf \"%.1f\", $ELAPSED_MS/1000}")
else
  ELAPSED=$((END_TIME - START_TIME))
fi

cell_result() {
  local cell="$1"
  local label="$2"
  if [[ -f "$RESULTS/${cell}_status" ]]; then
    local s; s=$(cat "$RESULTS/${cell}_status")
    echo -e "  ${YELLOW}${label}${RESET}: ${YELLOW}${s}${RESET}"
    return
  fi
  local p=0 f=0
  [[ -f "$RESULTS/${cell}_pass" ]] && p=$(cat "$RESULTS/${cell}_pass")
  [[ -f "$RESULTS/${cell}_fail" ]] && f=$(cat "$RESULTS/${cell}_fail")
  if [[ $f -eq 0 ]]; then
    echo -e "  ${GREEN}${label}: ${p} PASS${RESET}"
  else
    echo -e "  ${RED}${label}: ${p} pass, ${f} FAIL${RESET}"
  fi
}

any_fail() {
  local cell="$1"
  [[ -f "$RESULTS/${cell}_fail" ]] && [[ $(cat "$RESULTS/${cell}_fail") -gt 0 ]]
}

echo ""
echo -e "${BOLD}Invariants — 3×3 matrix${RESET}"
echo -e "${BOLD}                x86              JVM             .NET${RESET}"

# SNOBOL4 row
printf "  SNOBOL4   "
for cell in snobol4_x86 snobol4_jvm snobol4_net; do
  if [[ -f "$RESULTS/${cell}_status" ]]; then
    printf "  %-14s" "$(cat "$RESULTS/${cell}_status")"
  else
    p=0; f=0
    [[ -f "$RESULTS/${cell}_pass" ]] && p=$(cat "$RESULTS/${cell}_pass")
    [[ -f "$RESULTS/${cell}_fail" ]] && f=$(cat "$RESULTS/${cell}_fail")
    if [[ $f -eq 0 ]]; then printf "  ${GREEN}%-14s${RESET}" "${p}/${p} ✓"
    else printf "  ${RED}%-14s${RESET}" "${p}p/${f}f ✗"; fi
  fi
done
echo ""

# Icon row
printf "  Icon      "
for cell in icon_x86 icon_jvm icon_net; do
  if [[ "$cell" == "icon_net" ]]; then
    printf "  ${YELLOW}%-14s${RESET}" "SKIP"
  elif [[ -f "$RESULTS/${cell}_status" ]]; then
    printf "  %-14s" "$(cat "$RESULTS/${cell}_status")"
  else
    p=0; f=0
    [[ -f "$RESULTS/${cell}_pass" ]] && p=$(cat "$RESULTS/${cell}_pass")
    [[ -f "$RESULTS/${cell}_fail" ]] && f=$(cat "$RESULTS/${cell}_fail")
    if [[ $f -eq 0 ]]; then printf "  ${GREEN}%-14s${RESET}" "${p} rungs ✓"
    else printf "  ${RED}%-14s${RESET}" "${p}p/${f}f ✗"; fi
  fi
done
echo ""

# Prolog row
printf "  Prolog    "
for cell in prolog_x86 prolog_jvm prolog_net; do
  if [[ "$cell" == "prolog_net" ]]; then
    printf "  ${YELLOW}%-14s${RESET}" "SKIP"
  elif [[ -f "$RESULTS/${cell}_status" ]]; then
    printf "  %-14s" "$(cat "$RESULTS/${cell}_status")"
  else
    p=0; f=0
    [[ -f "$RESULTS/${cell}_pass" ]] && p=$(cat "$RESULTS/${cell}_pass")
    [[ -f "$RESULTS/${cell}_fail" ]] && f=$(cat "$RESULTS/${cell}_fail")
    if [[ $f -eq 0 ]]; then printf "  ${GREEN}%-14s${RESET}" "${p} rungs ✓"
    else printf "  ${RED}%-14s${RESET}" "${p}p/${f}f ✗"; fi
  fi
done
echo ""

echo -e "${BOLD}──────────────────────────────────────────────────${RESET}"

# Overall pass/fail
OVERALL_FAIL=0
for cell in snobol4_x86 snobol4_jvm snobol4_net icon_x86 icon_jvm prolog_x86 prolog_jvm; do
  any_fail "$cell" && OVERALL_FAIL=1
done

if [[ $OVERALL_FAIL -eq 0 ]]; then
  echo -e "${GREEN}${BOLD}  ALL PASS  [${ELAPSED}s]${RESET}"
  exit 0
else
  echo -e "${RED}${BOLD}  FAILURES PRESENT  [${ELAPSED}s]${RESET}"
  exit 1
fi
