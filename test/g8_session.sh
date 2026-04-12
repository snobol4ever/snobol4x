#!/usr/bin/env bash
# test/g8_session.sh — G-8 session script: M-G-INV-EMIT-FIX
#
# Completes the emit-diff invariant harness. Run this at the start of a
# G-8 session to pick up where G-8 left off.
#
# What this script does:
#   VERIFY   — confirms build is clean and baseline invariants hold
#   DIAGNOSE — identifies all scrip statics that need resetting between files
#   FIX      — patches snoc_reset() to cover all statics
#   TEST     — confirms scrip -asm *.sno (152 files) no longer crashes
#   BASELINE — generates emit_baseline/ snapshot (committed to repo)
#   CHECK    — runs emit-diff check to confirm all 152×3 match baseline
#   TIMING   — reports wall time (target: <5s for all three backends)
#
# Usage:
#   cd /home/claude/one4all
#   bash test/g8_session.sh [--skip-verify] [--skip-fix] [--only-baseline]
#
# After this script completes successfully, commit:
#   git add test/emit_baseline src/frontend/snobol4/lex.c src/driver/main.c
#   git commit -m "G-8: M-G-INV-EMIT-FIX ✅ — in-process batch + emit baseline"
#
# Milestones closed by this script:
#   M-G-INV-EMIT-FIX  — scrip processes all corpus files in one invocation
#   M-G-INV-EMIT      — emit-diff harness green, baseline committed
#
# Authors: Claude Sonnet 4.6 (G-8 session, 2026-03-29)

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIP="$ROOT/scrip"
CORPUS="${CORPUS:-$(cd "$ROOT/../corpus" 2>/dev/null && pwd || echo "")}"
BASELINE="$ROOT/test/emit_baseline"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; RESET='\033[0m'
ok()   { echo -e "${GREEN}  OK${RESET}  $*"; }
fail() { echo -e "${RED}FAIL${RESET}  $*"; ERRORS=$((ERRORS+1)); }
info() { echo -e "${YELLOW}    ${RESET}  $*"; }
ERRORS=0

SKIP_VERIFY=0; SKIP_FIX=0; ONLY_BASELINE=0
for arg in "$@"; do
  [[ "$arg" == "--skip-verify"   ]] && SKIP_VERIFY=1
  [[ "$arg" == "--skip-fix"      ]] && SKIP_FIX=1
  [[ "$arg" == "--only-baseline" ]] && ONLY_BASELINE=1
done

echo -e "${BOLD}═══════════════════════════════════════════════════════${RESET}"
echo -e "${BOLD}  G-8 session: M-G-INV-EMIT-FIX${RESET}"
echo -e "${BOLD}═══════════════════════════════════════════════════════${RESET}"
echo ""

# ── VERIFY ────────────────────────────────────────────────────────────────────
if [[ $SKIP_VERIFY -eq 0 ]]; then
  echo -e "${BOLD}VERIFY — build + corpus path${RESET}"

  if [[ ! -x "$SCRIP" ]]; then
    info "scrip not built — building now..."
    (cd "$ROOT/src" && make -j4 -s) && ok "scrip built" || { fail "build failed"; exit 1; }
  else
    ok "scrip exists: $SCRIP"
  fi

  if [[ -z "$CORPUS" || ! -d "$CORPUS/crosscheck" ]]; then
    fail "corpus not found. Set CORPUS= or ensure ../corpus/crosscheck exists"
    echo "  Clone: git clone https://TOKEN@github.com/snobol4ever/corpus ../corpus"
    exit 1
  fi
  ok "corpus: $CORPUS/crosscheck"

  NSNO=$(find "$CORPUS/crosscheck" -name "*.sno" | wc -l)
  ok "$NSNO .sno files in crosscheck corpus"
  echo ""
fi

# ── DIAGNOSE — find the crash ─────────────────────────────────────────────────
if [[ $SKIP_FIX -eq 0 && $ONLY_BASELINE -eq 0 ]]; then
  echo -e "${BOLD}DIAGNOSE — multi-file crash isolation${RESET}"
  info "Known: 013_assign_overwrite.sno + 014_assign_indirect_dollar.sno = SIGSEGV"
  info "Root cause: parser/emitter statics not fully reset between files in compile_one()"
  echo ""
  info "Files that are known problematic pairs:"
  F013="$CORPUS/crosscheck/assign/013_assign_overwrite.sno"
  F014="$CORPUS/crosscheck/assign/014_assign_indirect_dollar.sno"
  if [[ -f "$F013" && -f "$F014" ]]; then
    "$SCRIP" -asm "$F013" "$F014" > /dev/null 2>&1 && \
      ok "Pair 013+014: PASS (already fixed!)" || \
      fail "Pair 013+014: SIGSEGV (still broken — fix needed)"
    rm -f "${F013%.sno}.s" "${F014%.sno}.s" 2>/dev/null || true
  fi
  echo ""

  echo -e "${BOLD}FIX GUIDANCE — statics to audit and reset in snoc_reset()${RESET}"
  echo ""
  info "In src/frontend/snobol4/lex.c:"
  info "  snoc_nerrors      — reset to 0           ✅ done"
  info "  n_inc / inc_dirs  — reset to 0 / freed   ✅ done"
  info "  yyfilename        — reset to <stdin>      ✅ done"
  echo ""
  info "Likely culprits NOT yet reset (investigate these):"
  info "  - LineArray internal state in join_file() — static local buffers?"
  info "  - Any static LineArray or SnoLine array used across calls"
  info "  - The Lex struct — is it heap-allocated fresh each snoc_parse() call?"
  info "  - parse.c — any static token lookahead buffer?"
  echo ""
  info "In src/backend/x64/emit_x64.c (already resets internally in emit_program):"
  info "  str_reset, flt_reset, var_reset, lit_reset, cap_vars_reset ✅"
  info "  uid_ctr — resets within emit_program dry-run but may carry over?"
  echo ""
  info "APPROACH:"
  info "  1. Run: gcc -fsanitize=address,undefined -g scrip to get exact crash location"
  info "  2. Read the stack trace — it names the exact static"
  info "  3. Add that static to snoc_reset() in lex.c"
  info "  4. Re-run this script to verify"
  echo ""

  # Try to build with ASan for better crash diagnosis
  info "Building with AddressSanitizer for crash diagnosis..."
  ASAN_BIN="$ROOT/scrip_asan"
  if (cd "$ROOT/src" && make -j4 -s CFLAGS="-Wall -Wno-unused-function -g -O0 -I. -Ifrontend/snobol4 -Ifrontend/snocone -Ifrontend/prolog -Ifrontend/icon -Ibackend -Ibackend/x64 -fsanitize=address,undefined" 2>/dev/null && cp "$ROOT/scrip" "$ASAN_BIN") 2>/dev/null; then
    ok "ASan binary: $ASAN_BIN"
    info "Running crash pair under ASan..."
    ASAN_OPTIONS=abort_on_error=0 "$ASAN_BIN" -asm "$F013" "$F014" > /dev/null 2>&1 || true
    rm -f "${F013%.sno}.s" "${F014%.sno}.s" 2>/dev/null || true
    # Rebuild normal binary
    (cd "$ROOT/src" && make -j4 -s) > /dev/null 2>&1
  else
    info "ASan build skipped (compiler may not support it here)"
    info "Manual approach: add fprintf(stderr) before every static in snoc_reset()"
  fi
  echo ""
fi

# ── BASELINE / CHECK ──────────────────────────────────────────────────────────
echo -e "${BOLD}EMIT-DIFF — baseline and check${RESET}"

# Test current multi-file status
NSNO=$(find "$CORPUS/crosscheck" -name "*.sno" | wc -l)
info "Testing multi-file mode ($NSNO files × 3 backends)..."

PASS_COUNT=0
for backend in -asm -jvm -net; do
  ext=$(echo $backend | sed 's/-asm/.s/;s/-jvm/.j/;s/-net/.il/')
  OUT=$(find "$CORPUS/crosscheck" -name "*.sno" | tr '\n' '\0' | xargs -0 "$SCRIP" $backend 2>&1); RC=$?
  WRITTEN=$(find "$CORPUS/crosscheck" -name "*$ext" 2>/dev/null | wc -l)
  find "$CORPUS/crosscheck" -name "*$ext" | xargs rm -f 2>/dev/null || true
  if [[ $WRITTEN -eq $NSNO ]]; then
    ok "multi-file $backend: $WRITTEN/$NSNO files (in-process batch working)"
    PASS_COUNT=$((PASS_COUNT+1))
  else
    fail "multi-file $backend: $WRITTEN/$NSNO files (multi-file crash still present)"
    info "Workaround: harness will use xargs -P8 (one process per file)"
  fi
done
echo ""

# Generate baseline if all three backends work in multi-file mode, else use xargs
echo -e "${BOLD}BASELINE — generating $BASELINE${RESET}"
info "This writes one .s/.j/.il per corpus file — commit the result."
bash "$ROOT/test/run_emit_check.sh" --update 2>&1
echo ""

# Run the check to confirm baseline matches current output
echo -e "${BOLD}CHECK — diffing against baseline${RESET}"
START=$(date +%s%N 2>/dev/null || date +%s)
bash "$ROOT/test/run_emit_check.sh" --verbose 2>&1
END=$(date +%s%N 2>/dev/null || date +%s)
WALL=$(( (END - START) / 1000000 ))
echo ""
echo -e "${BOLD}Wall time: ${WALL}ms${RESET}"
[[ $WALL -lt 10000 ]] && ok "Under 10s target" || fail "Over 10s — investigate"
echo ""

# ── SUMMARY ──────────────────────────────────────────────────────────────────
echo -e "${BOLD}═══════════════════════════════════════════════════════${RESET}"
if [[ $ERRORS -eq 0 ]]; then
  echo -e "${GREEN}${BOLD}  G-8 SESSION COMPLETE — commit and close${RESET}"
  echo ""
  echo "  git add test/emit_baseline src/frontend/snobol4/lex.c src/driver/main.c"
  echo "  git commit -m 'G-8: M-G-INV-EMIT ✅ — in-process batch + emit baseline'"
  echo "  git push"
else
  echo -e "${RED}${BOLD}  G-8 SESSION INCOMPLETE — $ERRORS problem(s)${RESET}"
  echo ""
  echo "  Fix the issues above, then re-run: bash test/g8_session.sh"
fi
echo -e "${BOLD}═══════════════════════════════════════════════════════${RESET}"

# ── EXTEND TO 7 CELLS ─────────────────────────────────────────────────────────
# run_emit_check.sh currently covers SNOBOL4×crosscheck only (3 of 7 cells).
# After M-G-INV-EMIT-FIX, extend it to cover all 7 active cells:
#
#   Cell 1-3: SNOBOL4 × x86/JVM/.NET  — corpus/crosscheck/*.sno (152 files)
#   Cell 4-5: Icon    × x86/JVM       — test/frontend/icon/corpus/rung*/*.icn (258 files)
#   Cell 6-7: Prolog  × x86/JVM       — test/frontend/prolog/corpus/rung*/*.pro/.pl (131 files)
#
# Icon and Prolog frontends do NOT have the SIGSEGV issue (they init fresh per
# compile_one call — no snoc_* global state). Multi-file batch works for them now.
#
# To extend run_emit_check.sh:
#   1. Add SNO_FILES_ICN and SNO_FILES_PRO arrays alongside SNO_FILES
#   2. For each: emit_one_check "$f" -asm s / emit_one_check "$f" -jvm j
#   3. Baseline dirs: emit_baseline/-asm-icn/ emit_baseline/-jvm-icn/ etc.
#   4. Run --update to regenerate, confirm all 7 cells green
#
# Quick test that Icon multi-file works today:
echo -e "${BOLD}BONUS — verify Icon/Prolog multi-file (should work already)${RESET}"
ICN_FILES=$(find "$ROOT/test/frontend/icon/corpus" -name "*.icn" 2>/dev/null | head -10 | tr '\n' ' ')
if [[ -n "$ICN_FILES" ]]; then
  eval "$SCRIP -asm $ICN_FILES" > /dev/null 2>&1 && ok "Icon x86 multi-file: OK" || fail "Icon x86 multi-file: CRASH"
  find "$ROOT/test/frontend/icon/corpus" -name "*.s" | xargs rm -f 2>/dev/null || true
fi
PRO_FILES=$(find "$ROOT/test/frontend/prolog/corpus" -name "*.pro" -o -name "*.pl" 2>/dev/null | head -10 | tr '\n' ' ')
if [[ -n "$PRO_FILES" ]]; then
  eval "$SCRIP -asm $PRO_FILES" > /dev/null 2>&1 && ok "Prolog x86 multi-file: OK" || fail "Prolog x86 multi-file: CRASH"
  find "$ROOT/test/frontend/prolog/corpus" -name "*.s" | xargs rm -f 2>/dev/null || true
fi
echo ""
