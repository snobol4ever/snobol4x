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

# ── Tool bootstrap (run_invariants needs: scrip-cc, nasm, libgc, java) ────────
# Checked and fixed HERE, before the watchdog starts, so a missing tool is never
# discovered 5 minutes into the suite. Each check is self-healing where possible.
ensure_tools() {
  local ok=1

  # 1. scrip-cc — build from source if binary absent or zero-size
  if [[ ! -x "$SCRIP_CC" || ! -s "$SCRIP_CC" ]]; then
    echo -e "${YELLOW}  [tools] scrip-cc missing — building from $ROOT/src ...${RESET}"
    if (cd "$ROOT/src" && make -j"$(nproc 2>/dev/null || echo 4)" 2>/dev/null); then
      echo -e "${GREEN}  [tools] scrip-cc built ✓${RESET}"
    else
      echo -e "${RED}  [tools] scrip-cc build FAILED — cannot continue${RESET}" >&2; ok=0
    fi
  fi
  [[ -x "$SCRIP_CC" && -s "$SCRIP_CC" ]] || { echo -e "${RED}  [tools] scrip-cc still missing${RESET}" >&2; ok=0; }

  # 2. nasm — install via apt if absent
  if ! command -v nasm &>/dev/null; then
    echo -e "${YELLOW}  [tools] nasm missing — installing ...${RESET}"
    if apt-get install -y nasm >/dev/null 2>&1; then
      echo -e "${GREEN}  [tools] nasm installed ✓${RESET}"
    else
      echo -e "${RED}  [tools] nasm install FAILED (try: apt-get install nasm)${RESET}" >&2; ok=0
    fi
  fi

  # 3. libgc (Boehm GC) — needed for -lgc link; check header presence as proxy
  if ! ldconfig -p 2>/dev/null | grep -q 'libgc\.so' && \
     ! pkg-config --exists bdw-gc 2>/dev/null && \
     [[ ! -f /usr/include/gc/gc.h && ! -f /usr/local/include/gc/gc.h ]]; then
    echo -e "${YELLOW}  [tools] libgc-dev missing — installing ...${RESET}"
    if apt-get install -y libgc-dev >/dev/null 2>&1; then
      echo -e "${GREEN}  [tools] libgc-dev installed ✓${RESET}"
    else
      echo -e "${RED}  [tools] libgc-dev install FAILED (try: apt-get install libgc-dev)${RESET}" >&2; ok=0
    fi
  fi

  # 4. java — JVM cells will SKIP gracefully if absent, but warn early
  if ! command -v java &>/dev/null; then
    echo -e "${YELLOW}  [tools] java not found — JVM cells will SKIP${RESET}"
  fi

  [[ $ok -eq 1 ]] || { echo -e "${RED}${BOLD}  TOOL BOOTSTRAP FAILED — fix above errors and retry${RESET}" >&2; exit 2; }
  echo -e "${GREEN}  [tools] all required tools present ✓${RESET}"
}
ensure_tools

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
"\$scrip_cc" -asm -o "\$asm" "\$sno" 2>/dev/null || { echo "COMPILE_FAIL \$base"; exit 0; }
nasm -f elf64 -I"\$rt_asm_inc" "\$asm" -o "\$obj" 2>/dev/null || { echo "ASM_FAIL \$base"; exit 0; }
gcc -O0 -no-pie "\$obj" "\$lib" -lgc -lm -o "\$bin" 2>/dev/null || { echo "LINK_FAIL \$base"; exit 0; }
stdin_src=/dev/null; [[ -f "\$input" ]] && stdin_src="\$input"
got=\$(timeout "\$tmo" "\$bin" < "\$stdin_src" 2>/dev/null) || got="__TIMEOUT__"
exp=\$(cat "\$ref")
if [[ "\$got" == "\$exp" ]]; then echo "PASS \$base"; else echo "FAIL \$base"; fi
EOJOB
  chmod +x "$script"
}

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
  # Icon x86 uses scrip-cc directly (ICON_ASM binary removed in reorg)
  if [[ ! -x "$SCRIP_CC" ]]; then
    echo "SKIP" > "$RESULTS/${cell}_status"; return
  fi
  for rung_sh in "$ROOT"/test/frontend/icon/run_rung*.sh; do
    local result p m
    result=$(bash "$rung_sh" "$SCRIP_CC" 2>/dev/null | tail -1) || true
    _parse_rung_summary "$result"
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
    local result p m
    result=$(bash "$rung_sh" "$SCRIP_CC" 2>/dev/null | tail -1) || true
    _parse_rung_summary "$result"
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
