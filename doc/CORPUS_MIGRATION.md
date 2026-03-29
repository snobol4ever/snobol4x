# doc/CORPUS_MIGRATION.md — Corpus Migration Execution Checklist

This file is the verify condition for M-G0-CORPUS-AUDIT execution.
**Open this file first at every session that touches corpus migration.**
Do not declare the migration complete until every box is checked.

Naming convention in corpus: `rungNN_name_testname.ext`
No prefix. No serial numbers. Extension reflects actual content (.s not .c).
Runners stay in each compiler repo pointing at `$CORPUS_REPO/programs/<lang>/`.

---

## Icon (`programs/icon/`)

- [x] Copy `test/frontend/icon/corpus/rung01–38/` → `corpus/programs/icon/` (flat, rungNN_name_testname.icn)
- [x] Update all 38 `run_rung*.sh` runners to use `CORPUS_REPO`
- [x] Correct `.c` → `.s` extension (NASM x64 assembly)
- [x] **DELETE** `one4all/test/frontend/icon/corpus/` from one4all — commit `8327311`

## Prolog (`programs/prolog/`)

- [x] Copy `test/frontend/prolog/corpus/rung*/` → `corpus/programs/prolog/` (flat, rungNN_name_testname.ext) — corpus `92cff0a`
- [x] Update prolog rung runners to use `CORPUS_REPO` (run_invariants.sh flat .pl glob) — one4all `6b5f6a5`
- [x] **DELETE** `one4all/test/frontend/prolog/corpus/` from one4all — commit `4da8aed`

## SNOBOL4 smoke (`programs/snobol4/smoke/`)

- [x] Copy `test/frontend/snobol4/*.sno` → `corpus/programs/snobol4/smoke/` — corpus `606c141`
- [x] Update any runners (jvm_artifact_check.sh null.sno path fixed — one4all `e63d8d6`)
- [x] **DELETE** originals from one4all — commit `2e22f6e`

## Beauty tests (`programs/snobol4/beauty/`)

- [x] Copy `test/beauty/*/driver.sno` + subsystems → `corpus/programs/snobol4/beauty/` — corpus `606c141`
- [x] Update any runners (none hardcoded — runner takes dirs as args)
- [x] **DELETE** originals from one4all — commit `2e22f6e`

## Feat tests (`programs/snobol4/feat/`)

- [x] Copy `test/feat/f*.sno` → `corpus/programs/snobol4/feat/` — corpus `606c141`
- [x] Update any runners (none hardcoded)
- [x] **DELETE** originals from one4all — commit `2e22f6e`

## JVM J3 tests (`programs/snobol4/jvm_j3/`)

- [x] Copy `test/jvm_j3/*.sno` → `corpus/programs/snobol4/jvm_j3/` — corpus `606c141`
- [x] Update any runners (none hardcoded)
- [x] **DELETE** originals from one4all — commit `2e22f6e`

## Snocone (`programs/snocone/`)

- [x] Copy `test/frontend/snocone/sc_asm_corpus/*.sc` → `corpus/programs/snocone/corpus/` — corpus `c29fe83`
- [x] Copy `test/crosscheck/sc_corpus/*.sc` → `corpus/crosscheck/snocone/` — corpus `c29fe83`
- [x] Update any runners (run_sc_corpus_rung.sh comments updated — one4all `e63d8d6`)
- [x] **DELETE** originals from one4all — commit `edc0ab4`

## Rebus (`programs/rebus/`)

- [x] Copy `test/rebus/*.reb` → `corpus/programs/rebus/` — corpus `c29fe83`
- [x] Update any runners (none hardcoded)
- [x] **DELETE** originals from one4all — commit `edc0ab4`

---

## Done when

Every box above is checked. `one4all/test/` contains no `.icn`, `.pl`, `.sno`,
`.sc`, or `.reb` corpus source files. All runners use `CORPUS_REPO`.

## ✅ MIGRATION COMPLETE — G-9 session 8 (2026-03-29)

All source programs migrated to corpus repo. one4all/test/ is clean.
