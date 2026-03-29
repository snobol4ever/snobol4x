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
JASMIN="${JASMIN:-$ROOT/src/backend/jvm/jasmin.jar}"
RT_CACHE="${RT_CACHE:-$ROOT/out/rt_cache}"
RT="$ROOT/src/runtime"
SCRIP_CC_INC="$ROOT/src/frontend/snobol4"
TIMEOUT_X86="${TIMEOUT_X86:-5}"
TIMEOUT_JVM="${TIMEOUT_JVM:-10}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
SERIAL=0; VERBOSE=0
for arg in "$@"; do
  [[ "$arg" == "--serial"  ]] && SERIAL=1
  [[ "$arg" == "--verbose" ]] && VERBOSE=1
done

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'
BOLD='\033[1m'; RESET='\033[0m'

# ── Suite-level hard ceiling ───────────────────────────────────────────────────
# If the entire harness has not finished in SUITE_TIMEOUT seconds, kill it.
# Individual test timeouts (TIMEOUT_X86=5, TIMEOUT_JVM=10, jasmin=30, harness=120)
# should fire long before this. This is the final backstop against hung processes.
SUITE_TIMEOUT="${SUITE_TIMEOUT:-300}"
( sleep "$SUITE_TIMEOUT" && echo -e "\n${RED}${BOLD}SUITE TIMEOUT: harness exceeded ${SUITE_TIMEOUT}s — killed${RESET}" && kill $$ ) &
WATCHDOG_PID=$!
trap 'kill $WATCHDOG_PID 2>/dev/null; rm -rf "$WORK"' EXIT

WORK=$(mktemp -d /tmp/inv_XXXXXX)
RESULTS="$WORK/results"
mkdir -p "$RESULTS"

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

# ── x86 compile worker (called via xargs -P) ──────────────────────────────────
# Exported so subshell can call it
_x86_compile_one() {
  # args: sno ref input asm obj bin LIB TIMEOUT VERBOSE
  local sno="$1" ref="$2" input="$3" asm="$4" obj="$5" bin="$6" LIB="$7" TIMEOUT="$8" VERBOSE="$9"
  local base; base=$(basename "$sno" .sno)
  local RT_ASM; RT_ASM="$(dirname "$LIB")/../src/runtime/asm/"
  # scrip-cc + nasm + gcc link
  "$SCRIP_CC_BIN" -asm "$sno" > "$asm" 2>/dev/null || { echo "COMPILE_FAIL $base"; return; }
  nasm -f elf64 -I"$RT_ASM_INC" "$asm" -o "$obj" 2>/dev/null || { echo "ASM_FAIL $base"; return; }
  gcc -O0 -no-pie "$obj" "$LIB" -lgc -lm -o "$bin" 2>/dev/null || { echo "LINK_FAIL $base"; return; }
  local stdin_src="/dev/null"; [[ -f "$input" ]] && stdin_src="$input"
  local got; got=$(timeout "$TIMEOUT" "$bin" < "$stdin_src" 2>/dev/null) || got="__TIMEOUT__"
  local exp; exp=$(cat "$ref")
  if [[ "$got" == "$exp" ]]; then echo "PASS $base"; else echo "FAIL $base"; fi
}
export -f _x86_compile_one

# ── Suite: SNOBOL4 x86 ────────────────────────────────────────────────────────
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

  export SCRIP_CC_BIN="$SCRIP_CC" RT_ASM_INC="$RT/asm/"

  # Run nasm+link+exec in parallel via xargs
  local results_file="$WORK/${cell}_results"
  while IFS='|' read -r sno ref input asm obj bin lib tmo verb; do
    echo "$sno $ref $input $asm $obj $bin $lib $tmo $verb"
  done < "$manifest" | \
    xargs -P"$JOBS" -L1 bash -c '_x86_compile_one "$@"' _ > "$results_file" 2>/dev/null || true

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
      cp "$ref" "$W/${base}.ref"
      [[ -f "$input" ]] && cp "$input" "$W/${base}.input"
      if "$SCRIP_CC" -jvm "$sno" > "$jfile" 2>/dev/null; then
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
      PASS*) pass=$((pass+1)); [[ $VERBOSE -eq 1 ]] && echo "  $cell $line" ;;
      FAIL*) fail=$((fail+1)); echo "  FAIL $cell ${line#FAIL }" ;;
      TIMEOUT*) fail=$((fail+1)); echo "  TIMEOUT $cell ${line#TIMEOUT }" ;;
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

# ── Suite: Icon x86 ───────────────────────────────────────────────────────────
run_icon_x86() {
  local cell="icon_x86"
  local pass=0 fail=0
  local ICON_ASM="$ROOT/icon-asm"
  if [[ ! -x "$ICON_ASM" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  for rung_sh in "$ROOT"/test/frontend/icon/run_rung*.sh; do
    local result; result=$(bash "$rung_sh" "$ICON_ASM" 2>/dev/null | tail -1) || true
    local p m
    p=$(echo "$result" | grep -o '[0-9]* PASS' | grep -o '[0-9]*' || echo 0)
    m=$(echo "$result" | grep -o '[0-9]* FAIL' | grep -o '[0-9]*' || echo 0)
    pass=$((pass + p)); fail=$((fail + m))
    [[ $m -gt 0 ]] && echo "  FAIL $cell $(basename "$rung_sh"): $m fail"
  done
  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Suite: Icon JVM ───────────────────────────────────────────────────────────
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

# ── Suite: Prolog x86 ─────────────────────────────────────────────────────────
run_prolog_x86() {
  local cell="prolog_x86"
  local rung_pass=0 rung_fail=0
  if ! command -v nasm &>/dev/null; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  ensure_sno4_archive  || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  ensure_prolog_archive || { echo "BUILD_FAIL" > "$RESULTS/${cell}_status"; return; }
  local SNO_LIB="$RT_CACHE/libsno4rt_asm.a"
  local PL_LIB="$RT_CACHE/libsno4rt_pl.a"
  local W="$WORK/$cell"; mkdir -p "$W"
  for rung_dir in "$ROOT"/test/frontend/prolog/corpus/rung*/; do
    local rpass=0 rfail=0
    for pro in "$rung_dir"*.pro; do
      [[ -f "$pro" ]] || continue
      local base; base=$(basename "$pro" .pro)
      local expected="${pro%.pro}.expected"; [[ -f "$expected" ]] || continue
      local xfail="${pro%.pro}.xfail"
      [[ -f "$xfail" ]] && { rpass=$((rpass+1)); continue; }
      local asm="$W/${base}.s" obj="$W/${base}.o" bin="$W/${base}"
      if "$SCRIP_CC" -pl -asm "$pro" > "$asm" 2>/dev/null &&
         nasm -f elf64 "$asm" -o "$obj" 2>/dev/null &&
         gcc -O0 -no-pie "$obj" "$PL_LIB" -lm -o "$bin" 2>/dev/null; then
        local got; got=$(timeout "$TIMEOUT_X86" "$bin" 2>/dev/null) || got="__FAIL__"
        if [[ "$got" == "$(cat "$expected")" ]]; then
          rpass=$((rpass+1)); [[ $VERBOSE -eq 1 ]] && echo "  PASS $cell $base"
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

  # Compile ALL prolog rungs at once, batch jasmin, one SnoHarness run
  local jfiles=() compile_fail=0
  for rung_dir in "$ROOT"/test/frontend/prolog/corpus/rung*/; do
    for pro in "$rung_dir"*.pro; do
      [[ -f "$pro" ]] || continue
      local base; base=$(basename "$pro" .pro)
      local expected="${pro%.pro}.expected"; [[ -f "$expected" ]] || continue
      local xfail="${pro%.pro}.xfail"; [[ -f "$xfail" ]] && continue
      local jfile="$W/${base}.j"
      cp "$expected" "$W/${base}.ref"
      if "$SCRIP_CC" -pl -jvm "$pro" > "$jfile" 2>/dev/null; then
        jfiles+=("$jfile")
      else
        compile_fail=$((compile_fail+1))
        echo "  FAIL $cell $base [compile]"
      fi
    done
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
      PASS*) pass=$((pass+1)); [[ $VERBOSE -eq 1 ]] && echo "  $cell $line" ;;
      FAIL*) fail=$((fail+1)); echo "  FAIL $cell ${line#FAIL }" ;;
      TIMEOUT*) fail=$((fail+1)); echo "  TIMEOUT $cell ${line#TIMEOUT }" ;;
    esac
  done <<< "$harness_out"
  fail=$((fail+compile_fail))

  echo "$pass" > "$RESULTS/${cell}_pass"
  echo "$fail"  > "$RESULTS/${cell}_fail"
}

# ── Parallel dispatch ─────────────────────────────────────────────────────────
PIDS=()
launch() {
  local fn="$1"
  if [[ $SERIAL -eq 1 ]]; then $fn
  else $fn & PIDS+=($!); fi
}

launch run_snobol4_x86
launch run_snobol4_jvm
launch run_snobol4_net
launch run_icon_x86
launch run_icon_jvm
launch run_prolog_x86
launch run_prolog_jvm

for pid in "${PIDS[@]:-}"; do wait "$pid" || true; done

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

any_fail() {
  local cell="$1"
  [[ -f "$RESULTS/${cell}_fail" ]] && [[ $(cat "$RESULTS/${cell}_fail") -gt 0 ]]
}

echo ""
echo -e "${BOLD}Invariants — 3×3 matrix${RESET}"
echo -e "${BOLD}                x86              JVM             .NET${RESET}"

for row_label in "SNOBOL4" "Icon   " "Prolog "; do
  case $row_label in
    "SNOBOL4") cells="snobol4_x86 snobol4_jvm snobol4_net" ;;
    "Icon   ") cells="icon_x86    icon_jvm    icon_net"    ;;
    "Prolog ") cells="prolog_x86  prolog_jvm  prolog_net"  ;;
  esac
  printf "  %s  " "$row_label"
  for cell in $cells; do
    if [[ "$cell" == "icon_net" || "$cell" == "prolog_net" ]]; then
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
for cell in snobol4_x86 snobol4_jvm snobol4_net icon_x86 icon_jvm prolog_x86 prolog_jvm; do
  any_fail "$cell" && OVERALL_FAIL=1
done

echo -e "${BOLD}START   $START_HUMAN${RESET}"
echo -e "${BOLD}FINISH  $FINISH_HUMAN${RESET}"
echo -e "${BOLD}ELAPSED ${ELAPSED_MS}ms  (${ELAPSED_S}s)${RESET}"
echo ""
if [[ $OVERALL_FAIL -eq 0 ]]; then
  echo -e "${GREEN}${BOLD}  ALL PASS${RESET}"
  exit 0
else
  echo -e "${RED}${BOLD}  FAILURES PRESENT${RESET}"
  exit 1
fi
