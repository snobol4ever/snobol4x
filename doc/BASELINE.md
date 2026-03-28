# BASELINE.md — Pre-Reorg Freeze Baseline

**Tag:** `pre-reorg-freeze`
**snobol4x HEAD:** `a051367`
**Date:** 2026-03-28
**Recorded by:** Claude Sonnet 4.6 (G-7 session)

This file records the exact test counts at the moment the Grand Master Reorganization
freeze was declared. Every invariant listed here must remain green at the end of every
reorg milestone. See `GRAND_MASTER_REORG.md` § Migration Strategy for trigger rules.

---

## x64 ASM Backend

| Frontend | Suite | Count | Runner |
|----------|-------|-------|--------|
| SNOBOL4 | crosscheck corpus | `106/106` | `test/crosscheck/run_crosscheck_asm_corpus.sh` |
| Icon | rung ladder (rungs 01–35+) | `38 rungs` | `test/frontend/icon/run_icon_x64_rung.sh` |
| Prolog | rung ladder (rungs 1–9, PX-1 WIP) | per-rung PASS | `test/frontend/prolog/` (per-rung scripts) |
| Snocone | ASM corpus | `10/10` | `test/frontend/snocone/sc_asm_corpus/run_sc_asm_corpus.sh` |
| Rebus | round-trip | `3/3` | `test/rebus/run_roundtrip.sh` |

## JVM Backend

| Frontend | Suite | Count | Runner |
|----------|-------|-------|--------|
| SNOBOL4 | crosscheck corpus | `106/106` | `test/crosscheck/run_crosscheck_jvm_rung.sh` |
| Icon | rung corpus (rungs 01–38) | `38 rung folders` | `test/frontend/icon/corpus/` (per-rung) |
| Prolog | SWI bench ladder | `31/31` | `test/frontend/prolog/run_prolog_jvm_rung.sh` |

## .NET Backend

| Frontend | Suite | Count | Runner |
|----------|-------|-------|--------|
| SNOBOL4 | crosscheck corpus | `110/110` | `test/crosscheck/run_crosscheck_net.sh` |

## WASM Backend

No test suite yet. Gate: `builds clean`. Suites added as M-G6 milestones deliver them.

---

## Notes

- `snobol4dotnet` (C#) and `snobol4jvm` (Clojure) are separate repos; not tracked here.
- `snobol4harness` and `snobol4corpus` are infrastructure repos; their baselines are
  their own HEADs at freeze time (`eced661` and `ccd79fa` respectively).
- The C backend (`sno2c -c`, `sno2c.c`) is dead/unmaintained and excluded from all invariants.
- Scrip frontend: corpus runners exist (`test/scrip/run_corpus_icon.sh`,
  `run_corpus_prolog.sh`) but these test the *parser tools*, not a Scrip→backend pipeline.
  No Scrip backend invariant yet — added when M-G6-SCRIP-X64 lands.
