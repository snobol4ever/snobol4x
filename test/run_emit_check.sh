#!/usr/bin/env bash
# test/run_emit_check.sh — Emit-diff invariant check (M-G-INV-EMIT)
#
# Source and generated files are co-located:
#   test/snobol4/{subdir}/foo.sno  + foo.s  foo.j  foo.il
#   test/icon/foo.icn              + foo.s  foo.j
#   test/prolog/foo.pro            + foo.s  foo.j
#
# Usage:
#   bash test/run_emit_check.sh            # diff mode — all backends (default)
#   bash test/run_emit_check.sh --update   # regenerate generated files
#   bash test/run_emit_check.sh --verbose  # print PASS lines too
#
# Scoping (CELLS env var — mirrors run_invariants.sh cell names):
#   CELLS=snobol4_x86  bash test/run_emit_check.sh   # SNO×asm only (DYN/x86 sessions)
#   CELLS=snobol4_jvm  bash test/run_emit_check.sh   # SNO×jvm only
#   CELLS=snobol4_net  bash test/run_emit_check.sh   # SNO×net only
#   CELLS=icon_x86     bash test/run_emit_check.sh   # ICN×asm only
#   CELLS=icon_jvm     bash test/run_emit_check.sh   # ICN×jvm only
#   CELLS=prolog_x86   bash test/run_emit_check.sh   # PRO×asm only
#   CELLS=prolog_jvm   bash test/run_emit_check.sh   # PRO×jvm only
#   (combine with spaces: CELLS="snobol4_x86 icon_x86")
#   Omit CELLS or CELLS="" → all backends (cross-session shared gate)
#
# Environment: SCRIP_CC (default: <root>/scrip), JOBS (default: nproc), CELLS

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip}"
export SCRIP_CC
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
# Source locations: post-corpus-migration (M-G0-CORPUS-AUDIT) sources live in the
# corpus repo.  CORPUS env var must be set (e.g. CORPUS=/home/claude/corpus).
# Fallback to legacy in-tree paths for backward compat (no longer populated).
if [[ -n "${CORPUS:-}" ]]; then
  TEST_SNO="$CORPUS/crosscheck"
  TEST_ICN="$CORPUS/programs/icon"
  TEST_PRO="$CORPUS/programs/prolog"
  TEST_REB="$CORPUS/programs/rebus"
else
  TEST_SNO="$ROOT/test/snobol4"
  TEST_ICN="$ROOT/test/icon"
  TEST_PRO="$ROOT/test/prolog"
  TEST_REB="$ROOT/test/rebus"
fi

UPDATE=0; VERBOSE=0
for arg in "$@"; do
  [[ "$arg" == "--update"  ]] && UPDATE=1
  [[ "$arg" == "--verbose" ]] && VERBOSE=1
done

# ── CELLS-based scope filter ─────────────────────────────────────────────────
# Derive which (frontend × backend) pairs to check from CELLS env var.
# If CELLS is unset or empty → all pairs (legacy/cross-session behaviour).
_CELLS="${CELLS:-}"
_want_sno_asm=1; _want_sno_jvm=1; _want_sno_net=1; _want_sno_js=1; _want_sno_wasm=1
_want_icn_asm=1; _want_icn_jvm=1
_want_pro_asm=1; _want_pro_jvm=1
_want_reb_asm=1; _want_reb_jvm=1; _want_reb_net=1
if [[ -n "$_CELLS" ]]; then
  _want_sno_asm=0; _want_sno_jvm=0; _want_sno_net=0; _want_sno_js=0; _want_sno_wasm=0
  _want_icn_asm=0; _want_icn_jvm=0
  _want_pro_asm=0; _want_pro_jvm=0
  _want_reb_asm=0; _want_reb_jvm=0; _want_reb_net=0
  echo "$_CELLS" | grep -qw 'snobol4_x86'   && _want_sno_asm=1
  echo "$_CELLS" | grep -qw 'snobol4_jvm'   && _want_sno_jvm=1
  echo "$_CELLS" | grep -qw 'snobol4_net'   && _want_sno_net=1
  echo "$_CELLS" | grep -qw 'snobol4_js'    && _want_sno_js=1
  echo "$_CELLS" | grep -qw 'snobol4_wasm'  && _want_sno_wasm=1
  echo "$_CELLS" | grep -qw 'icon_x86'      && _want_icn_asm=1
  echo "$_CELLS" | grep -qw 'icon_jvm'      && _want_icn_jvm=1
  echo "$_CELLS" | grep -qw 'prolog_x86'    && _want_pro_asm=1
  echo "$_CELLS" | grep -qw 'prolog_jvm'    && _want_pro_jvm=1
  echo "$_CELLS" | grep -qw 'snocone_x86'   && _want_sno_asm=1  # snocone uses same .s oracle
  _scope_label=" [CELLS=$_CELLS]"
else
  _scope_label=" [all backends]"
fi

GREEN='\033[0;32m'; RED='\033[0;31m'; BOLD='\033[1m'; RESET='\033[0m'

# ── Tool bootstrap (run_emit_check needs: scrip only) ─────────────────────
# Preflight — verify tools present (no installs; run SESSION_SETUP.sh first):
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
_need "scrip" "$([[ -x "$SCRIP_CC" && -s "$SCRIP_CC" ]] && echo 1 || echo 0)"
_need "gcc"      "$(command -v gcc  &>/dev/null && echo 1 || echo 0)"
echo -e "${GREEN}  [tools] all required tools present ✓${RESET}"

mapfile -t SNO_FILES < <(find "$TEST_SNO" -name "*.sno" | sort)
mapfile -t ICN_FILES < <(find "$TEST_ICN" -name "*.icn" 2>/dev/null | while read -r f; do [[ -f "${f%.icn}.s" ]] && echo "$f"; done | sort)
mapfile -t PRO_FILES < <(find "$TEST_PRO" -name "*.pl"  2>/dev/null | while read -r f; do [[ -f "${f%.pl}.s"  ]] && echo "$f"; done | sort)
mapfile -t REB_FILES < <(find "$TEST_REB" -name "*.reb" 2>/dev/null | while read -r f; do [[ -f "${f%.reb}.s" ]] && echo "$f"; done | sort)

if [[ $UPDATE -eq 1 ]]; then
  # scrip gcc-style: when given a source file with no -o, it writes the
  # output alongside the source with the appropriate extension replaced.
  # e.g.  scrip -asm foo/bar.sno  →  foo/bar.s  (side by side, always)
  # regen_one simply invokes scrip with the right backend flag and no -o.
  # Only non-empty output is kept (compile errors leave no file).
  regen_one() {
    local src="$1" backend="$2"
    # Let scrip derive the output path itself — same directory, ext replaced.
    "$SCRIP_CC" "$backend" "$src" 2>/dev/null
    # scrip exits non-zero on error and writes nothing (or an empty file).
    # If it left an empty output file, remove it so stale oracles aren't created.
    local ext
    case "$backend" in
      -asm)  ext=s  ;; -jvm)  ext=j  ;; -net)  ext=il ;;
      -wasm) ext=wat;; -js)   ext=js  ;; *)     ext=out;;
    esac
    local out="${src%.*}.$ext"
    [[ -f "$out" && ! -s "$out" ]] && rm -f "$out"
  }
  export -f regen_one; export SCRIP_CC

  # ALL_SNO: every .sno in the corpus — not just those with an existing .s.
  # Every clean compile drops its output next to the source automatically.
  mapfile -t ALL_SNO < <(find "$TEST_SNO" -name "*.sno" | sort)
  mapfile -t ALL_ICN < <(find "$TEST_ICN" -name "*.icn" 2>/dev/null | sort)
  mapfile -t ALL_PRO < <(find "$TEST_PRO" -name "*.pl"  2>/dev/null | sort)
  mapfile -t ALL_REB < <(find "$TEST_REB" -name "*.reb" 2>/dev/null | sort)

  echo "Regenerating: ${#ALL_SNO[@]} SNOBOL4×5 + ${#ALL_ICN[@]} Icon×2 + ${#ALL_PRO[@]} Prolog×2 + ${#ALL_REB[@]} Rebus×3 (side-by-side, clean compiles only)..."

  [[ $_want_sno_asm  -eq 1 ]] && printf '%s\n' "${ALL_SNO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm'  _ {}
  [[ $_want_sno_jvm  -eq 1 ]] && printf '%s\n' "${ALL_SNO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm'  _ {}
  [[ $_want_sno_net  -eq 1 ]] && printf '%s\n' "${ALL_SNO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -net'  _ {}
  [[ $_want_sno_js   -eq 1 ]] && printf '%s\n' "${ALL_SNO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -js'   _ {}
  [[ $_want_sno_wasm -eq 1 ]] && printf '%s\n' "${ALL_SNO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -wasm' _ {}
  [[ ${#ALL_ICN[@]} -gt 0 && $_want_icn_asm -eq 1 ]] && printf '%s\n' "${ALL_ICN[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm' _ {}
  [[ ${#ALL_ICN[@]} -gt 0 && $_want_icn_jvm -eq 1 ]] && printf '%s\n' "${ALL_ICN[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm' _ {}
  [[ ${#ALL_PRO[@]} -gt 0 && $_want_pro_asm -eq 1 ]] && printf '%s\n' "${ALL_PRO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm' _ {}
  [[ ${#ALL_PRO[@]} -gt 0 && $_want_pro_jvm -eq 1 ]] && printf '%s\n' "${ALL_PRO[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm' _ {}
  [[ ${#ALL_REB[@]} -gt 0 && $_want_reb_asm -eq 1 ]] && printf '%s\n' "${ALL_REB[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm' _ {}
  [[ ${#ALL_REB[@]} -gt 0 && $_want_reb_jvm -eq 1 ]] && printf '%s\n' "${ALL_REB[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm' _ {}
  [[ ${#ALL_REB[@]} -gt 0 && $_want_reb_net -eq 1 ]] && printf '%s\n' "${ALL_REB[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -net' _ {}

  COUNT=$(find "$TEST_SNO" "$TEST_ICN" "$TEST_PRO" "$TEST_REB" \
    \( -name "*.s" -o -name "*.j" -o -name "*.il" -o -name "*.js" -o -name "*.wat" \) \
    2>/dev/null | wc -l)
  echo "Done: $COUNT generated files alongside sources."
  echo "Next: cd $CORPUS && git add -A && git commit -m 'regen: update generated artifacts alongside sources'"
  exit 0
fi

WORK=$(mktemp -d /tmp/emit_check_XXXXXX)
trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
FAIL_LOG="$WORK/failures.txt"
touch "$FAIL_LOG"
START=$(date +%s%N 2>/dev/null || date +%s)
START_HUMAN=$(date '+%Y-%m-%d %H:%M:%S')

# ── Persistent CSV report ─────────────────────────────────────────────────────
CSV_DIR="$ROOT/test-results"
mkdir -p "$CSV_DIR"
TS=$(date '+%Y%m%d_%H%M%S')
CSV="$CSV_DIR/emit_${TS}.csv"
printf 'status,backend,label,timestamp\n' > "$CSV"

echo -e "${BOLD}START  $START_HUMAN  run_emit_check.sh${_scope_label}${RESET}"

check_one() {
  local src="$1" backend="$2" ext="$3"
  local dir name label expected got
  dir="$(dirname "$src")"; name="$(basename "${src%.*}")"
  label="$(basename "$dir")/$name"
  expected="$dir/$name.$ext"
  local tmp; tmp=$(mktemp)
  "$SCRIP_CC" "$backend" -o /dev/stdout "$src" > "$tmp" 2>/dev/null
  if [[ ! -f "$expected" ]]; then
    rm -f "$tmp"; echo "SKIP $backend $label.$ext" >> "$FAIL_LOG"; return
  fi
  local ts; ts=$(date '+%Y-%m-%d %H:%M:%S')
  if diff -q "$tmp" "$expected" > /dev/null 2>&1; then
    echo "PASS $backend $label" >> "$FAIL_LOG"
    printf 'PASS,%s,%s,%s\n' "$backend" "$label" "$ts" >> "$CSV"
  else
    echo "FAIL $backend $label" >> "$FAIL_LOG"
    diff "$tmp" "$expected" | head -20 >> "$FAIL_LOG"
    echo "---" >> "$FAIL_LOG"
    printf 'FAIL,%s,%s,%s\n' "$backend" "$label" "$ts" >> "$CSV"
  fi
  rm -f "$tmp"
}
export -f check_one; export SCRIP_CC FAIL_LOG CSV
export TEST_REB

[[ $_want_sno_asm  -eq 1 && ${#SNO_FILES[@]} -gt 0 ]] && printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s'    _ {}
[[ $_want_sno_jvm  -eq 1 && ${#SNO_FILES[@]} -gt 0 ]] && printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j'    _ {}
[[ $_want_sno_net  -eq 1 && ${#SNO_FILES[@]} -gt 0 ]] && printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -net il'   _ {}
[[ $_want_sno_js   -eq 1 && ${#SNO_FILES[@]} -gt 0 ]] && printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -js  js'   _ {}
[[ $_want_sno_wasm -eq 1 && ${#SNO_FILES[@]} -gt 0 ]] && printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -wasm wat' _ {}
[[ $_want_icn_asm -eq 1 && ${#ICN_FILES[@]} -gt 0 ]] && printf '%s\n' "${ICN_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s' _ {}
[[ $_want_icn_jvm -eq 1 && ${#ICN_FILES[@]} -gt 0 ]] && printf '%s\n' "${ICN_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j' _ {}
[[ $_want_pro_asm -eq 1 && ${#PRO_FILES[@]} -gt 0 ]] && printf '%s\n' "${PRO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s' _ {}
[[ $_want_pro_jvm -eq 1 && ${#PRO_FILES[@]} -gt 0 ]] && printf '%s\n' "${PRO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j' _ {}
[[ $_want_reb_asm -eq 1 && ${#REB_FILES[@]} -gt 0 ]] && printf '%s\n' "${REB_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s'  _ {}
[[ $_want_reb_jvm -eq 1 && ${#REB_FILES[@]} -gt 0 ]] && printf '%s\n' "${REB_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j'  _ {}
[[ $_want_reb_net -eq 1 && ${#REB_FILES[@]} -gt 0 ]] && printf '%s\n' "${REB_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -net il' _ {}

END=$(date +%s%N 2>/dev/null || date +%s)
WALL_MS=$(( (END - START) / 1000000 ))

while IFS= read -r line; do
  if [[ "$line" == PASS* ]]; then
    PASS=$((PASS + 1))
    [[ $VERBOSE -eq 1 ]] && echo -e "${GREEN}PASS${RESET} ${line#PASS }"
  elif [[ "$line" == FAIL* || "$line" == MISSING* ]]; then
    FAIL=$((FAIL + 1))
    echo -e "${RED}${line}${RESET}"
  fi
done < "$FAIL_LOG"

FINISH_HUMAN=$(date '+%Y-%m-%d %H:%M:%S')
echo ""
echo -e "${BOLD}START   $START_HUMAN${RESET}"
echo -e "${BOLD}FINISH  $FINISH_HUMAN${RESET}"
echo -e "${BOLD}ELAPSED ${WALL_MS}ms  ($(awk "BEGIN{printf \"%.1f\", $WALL_MS/1000}")s)${RESET}"
echo -e "${BOLD}Emit-diff results: ${GREEN}$PASS pass${RESET} / ${RED}$FAIL fail${RESET} — ${WALL_MS}ms wall${RESET}"

# Finalise CSV — append summary row and update latest symlink
printf 'SUMMARY,total_pass=%d,total_fail=%d,elapsed_ms=%d\n' "$PASS" "$FAIL" "$WALL_MS" >> "$CSV"
ln -sf "$(basename "$CSV")" "$CSV_DIR/emit_latest.csv"
echo -e "${BOLD}Report: $CSV${RESET}"

[[ $FAIL -eq 0 ]] && exit 0 || exit 1
