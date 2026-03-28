#!/usr/bin/env bash
# test/run_matrix_smoke.sh — snobol4x matrix smoke test
#
# Exercises every locally testable frontend × backend combination.
# Does NOT require corpus (uses tests bundled in this repo).
# Always run from the snobol4x root:
#
#   bash test/run_matrix_smoke.sh
#
# Exit code: 0 if all suites pass, 1 if any suite fails.
#
# What's tested:
#   SNOBOL4 × x64 ASM   — sc_corpus (hello, assign, output)
#   SNOBOL4 × JVM       — sc_corpus via sno2c -jvm + jasmin + java
#   SNOBOL4 × .NET      — sc_corpus via sno2c -net + ilasm + mono  (skipped if no mono)
#   Icon    × x64 ASM   — rungs 01-35 via run_icon_x64_rung.sh     (skipped if no icont)
#   Icon    × JVM       — run_crosscheck_jvm_rung.sh sc_corpus     (skipped if no jasmin)
#   Prolog  × x64 ASM   — rung scripts via run_prolog_x64_rung.sh  (skipped if no nasm)
#   Prolog  × JVM       — run_prolog_jvm_rung.sh                   (skipped if no java)
#
# Snocone, Rebus, Scrip × all backends: no automated corpus yet — skipped.
#
# Authors: Claude Sonnet 4.6 (G-7 session, 2026-03-28)
# Milestone: M-G1-IR-HEADER-WIRE support infrastructure

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CC="${CC:-gcc}"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

TOTAL_SUITES=0
PASS_SUITES=0
FAIL_SUITES=0
SKIP_SUITES=0

SUITE_LOG=()

# ── helpers ────────────────────────────────────────────────────────────────

log_suite() {
    local status="$1" name="$2" detail="$3"
    SUITE_LOG+=("$status|$name|$detail")
    TOTAL_SUITES=$((TOTAL_SUITES + 1))
    case "$status" in
        PASS) PASS_SU