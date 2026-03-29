#!/usr/bin/env bash
# test/run_emit_check.sh — Emit-diff invariant check (M-G-INV-EMIT)
#
# Source and generated files are co-located:
#   test/snobol4/{subdir}/foo.sno  + foo.s  foo.j  foo.il
#   test/icon/foo.icn              + foo.s  foo.j
#   test/prolog/foo.pro            + foo.s  foo.j
#
# Usage:
#   bash test/run_emit_check.sh            # diff mode
#   bash test/run_emit_check.sh --update   # regenerate generated files
#   bash test/run_emit_check.sh --verbose  # print PASS lines too
#
# Environment: SCRIP_CC (default: <root>/scrip-cc), JOBS (default: nproc)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP_CC="${SCRIP_CC:-$ROOT/scrip-cc}"
export SCRIP_CC
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TEST_SNO="$ROOT/test/snobol4"
TEST_ICN="$ROOT/test/icon"
TEST_PRO="$ROOT/test/prolog"

UPDATE=0; VERBOSE=0
for arg in "$@"; do
  [[ "$arg" == "--update"  ]] && UPDATE=1
  [[ "$arg" == "--verbose" ]] && VERBOSE=1
done

GREEN='\033[0;32m'; RED='\033[0;31m'; BOLD='\033[1m'; RESET='\033[0m'

if [[ ! -x "$SCRIP_CC" ]]; then
  echo "ERROR: scrip-cc not found at $SCRIP_CC — build first (cd src && make)" >&2; exit 1
fi

mapfile -t SNO_FILES < <(find "$TEST_SNO" -name "*.sno" | sort)
mapfile -t ICN_FILES < <(find "$TEST_ICN" -name "*.icn" 2>/dev/null | sort)
mapfile -t PRO_FILES < <(find "$TEST_PRO" -name "*.pl"  2>/dev/null | sort)

if [[ $UPDATE -eq 1 ]]; then
  echo "Regenerating: ${#SNO_FILES[@]} SNOBOL4×3 + ${#ICN_FILES[@]} Icon×2 + ${#PRO_FILES[@]} Prolog×2..."
  regen_one() {
    local src="$1" backend="$2" ext="$3"
    local dir name; dir="$(dirname "$src")"; name="$(basename "${src%.*}")"
    local tmp; tmp=$(mktemp)
    "$SCRIP_CC" "$backend" -o /dev/stdout "$src" > "$tmp" 2>/dev/null
    [[ -s "$tmp" ]] && mv "$tmp" "$dir/$name.$ext" || rm -f "$tmp"
  }
  export -f regen_one; export SCRIP_CC
  printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm s'  _ {}
  printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm j'  _ {}
  printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -net il' _ {}
  [[ ${#ICN_FILES[@]} -gt 0 ]] && printf '%s\n' "${ICN_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm s' _ {}
  [[ ${#ICN_FILES[@]} -gt 0 ]] && printf '%s\n' "${ICN_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm j' _ {}
  [[ ${#PRO_FILES[@]} -gt 0 ]] && printf '%s\n' "${PRO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -asm s' _ {}
  [[ ${#PRO_FILES[@]} -gt 0 ]] && printf '%s\n' "${PRO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'regen_one "$1" -jvm j' _ {}
  COUNT=$(find "$TEST_SNO" "$TEST_ICN" "$TEST_PRO" \( -name "*.s" -o -name "*.j" -o -name "*.il" \) 2>/dev/null | wc -l)
  echo "Done: $COUNT generated files alongside sources."
  echo "Commit: git add test/snobol4 test/icon test/prolog"
  exit 0
fi

WORK=$(mktemp -d /tmp/emit_check_XXXXXX)
trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
FAIL_LOG="$WORK/failures.txt"
touch "$FAIL_LOG"
START=$(date +%s%N 2>/dev/null || date +%s)
START_HUMAN=$(date '+%Y-%m-%d %H:%M:%S')
echo -e "${BOLD}START  $START_HUMAN  run_emit_check.sh${RESET}"

check_one() {
  local src="$1" backend="$2" ext="$3"
  local dir name label expected got
  dir="$(dirname "$src")"; name="$(basename "${src%.*}")"
  label="$(basename "$dir")/$name"
  expected="$dir/$name.$ext"
  local tmp; tmp=$(mktemp)
  "$SCRIP_CC" "$backend" -o /dev/stdout "$src" > "$tmp" 2>/dev/null
  if [[ ! -f "$expected" ]]; then
    rm -f "$tmp"; echo "MISSING $backend $label.$ext" >> "$FAIL_LOG"; return
  fi
  if diff -q "$tmp" "$expected" > /dev/null 2>&1; then
    echo "PASS $backend $label" >> "$FAIL_LOG"
  else
    echo "FAIL $backend $label" >> "$FAIL_LOG"
    diff "$tmp" "$expected" | head -20 >> "$FAIL_LOG"
    echo "---" >> "$FAIL_LOG"
  fi
  rm -f "$tmp"
}
export -f check_one; export SCRIP_CC FAIL_LOG

printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s'  _ {}
printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j'  _ {}
printf '%s\n' "${SNO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -net il' _ {}
[[ ${#ICN_FILES[@]} -gt 0 ]] && printf '%s\n' "${ICN_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s' _ {}
[[ ${#ICN_FILES[@]} -gt 0 ]] && printf '%s\n' "${ICN_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j' _ {}
[[ ${#PRO_FILES[@]} -gt 0 ]] && printf '%s\n' "${PRO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -asm s' _ {}
[[ ${#PRO_FILES[@]} -gt 0 ]] && printf '%s\n' "${PRO_FILES[@]}" | xargs -P"$JOBS" -I{} bash -c 'check_one "$1" -jvm j' _ {}

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
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
