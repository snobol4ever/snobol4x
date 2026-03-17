# Artifacts

## Session 95 — 2026-03-15

### beauty_tramp_session95.c
- **md5:** cc34e62fee07676e12d0824c14fe6e85
- **lines:** 15639
- **CHANGED from session94** (31dfdcbf...)
- **compile status:** 0 errors, warnings only
- **crosscheck:** 106/106 pass (rungs 1–11 complete)

### Changes since session94
- fix(emit): `block_roman_end` undefined — alias emitted after `block_START` when first stmt has non-START label
- fix(emit): skip START in forward-decl loop and label table loop (no duplicate block_START)
- fix(emit): `E_IDX` subscript assignment now emits `aset` instead of falling through to `iset`
- fix(runtime): `ARRAY`, `TABLE`, `CONVERT`, `COPY` builtins registered in `SNO_INIT_fn`
- fix(corpus): `beauty.sno` lines 405–406 — `'comment'`→`'Comment'`, `'control'`→`'Control'` (case-sensitive variable names)
- fix(runner): crosscheck harness feeds `.input` files to programs that read INPUT

### Active bug / next action
- Rungs 1–11 all pass 100%. Sprint 3 (`crosscheck-ladder`) is COMPLETE.
- Next: Sprint 4 (`compiled-byrd-boxes-full`) — inline all pattern variables as static Byrd boxes, drop engine.c entirely. Gates on rung 11 being complete (it is).

## Session 98 — 2026-03-15

### beauty_tramp_session95.c (no new artifact)
- **md5:** cc34e62fee07676e12d0824c14fe6e85 — UNCHANGED from session95
- **lines:** 15639
- **compile status:** not recompiled this session (no sno2c changes)
- **crosscheck:** 106/106 (verified at session start)

### Session 98 work
- No compiler changes this session.
- HQ refactor: PLAN.md shrunk from 85KB to 3744 bytes (under 4096 limit).
- New HQ files: ARCH.md (architecture), TESTING.md (four-paradigm TDD), RULES.md (mandatory rules).
- Four-paradigm TDD plan written to TESTING.md — Sprint A (beauty-crosscheck) is next.
- CSNOBOL4 2.3.3 built from source at /usr/local/bin/snobol4 ✅
- beauty_full.c generated (15639 lines) ✅ — beauty_full_bin not yet linked (next session).

### Active bug / next action
- Sprint A (`beauty-crosscheck`) begins next session.
- First action: build beauty_full_bin, write 101_beauty_comment test, run run_beauty.sh.
- See TESTING.md for full sprint map and rung 12 protocol.

## Session 99 — 2026-03-15

### beauty_tramp_session95.c (no new artifact)
- **md5:** cc34e62fee07676e12d0824c14fe6e85 — UNCHANGED from session95
- **lines:** 15639
- **compile status:** not recompiled this session (no sno2c changes)
- **crosscheck:** 106/106 (invariant maintained)

### Session 99 work
- No compiler or sno2c changes this session.
- HQ restructured into true 3-level pyramid (L1/L2/L3).
- SESSION.md eliminated — content absorbed into PLAN.md (L1) and TINY.md (L2).
- PLAN.md: 3.3KB true index. TINY.md: 10.7KB L2 platform doc. ARCH.md: 8.3KB L3 reference.
- RULES.md updated: hierarchy rule explicit — future Claude writes to L2/L3, never PLAN.md.

### Active bug / next action
- Sprint A (`beauty-crosscheck`) begins next session.
- First action: build beauty_full_bin, write 101_comment test, run run_beauty.sh.
- See TINY.md §"Next action" and TESTING.md for full protocol.

## Session 100 — 2026-03-15

### beauty_tramp_session95.c (no new artifact)
- **md5:** cc34e62fee07676e12d0824c14fe6e85 — UNCHANGED from session95
- **compile status:** not recompiled (no sno2c changes)
- **crosscheck:** 106/106 maintained

### Session 100 work
- No compiler or sno2c changes.
- HQ: correct frontend×backend split — one file per input language / output target.
- New files: FRONTEND-SNOBOL4.md, FRONTEND-SNOCONE.md, FRONTEND-ICON.md,
  FRONTEND-PROLOG.md, FRONTEND-CSHARP.md, FRONTEND-CLOJURE.md,
  BACKEND-X64.md, IMPL-SNO2C.md (renamed from FRONTEND-SNO2C.md).
- Removed: FRONTEND-BEAUTY.md (absorbed into FRONTEND-SNOBOL4.md),
  FRONTEND-SNO2C.md (renamed IMPL-SNO2C.md — it's a compiler impl, not a language).
- PLAN.md: real product matrix table (frontend × backend × repo).
- TINY.md: frontier table showing which frontend×backend combinations active/done/planned.

### Active bug / next action
- Sprint A (`beauty-crosscheck`) begins next session (101).
- First: build beauty_full_bin → write 101_comment test → run run_beauty.sh.
- See TINY.md §NOW and FRONTEND-SNOBOL4.md §Rung 12 Test Format.

## Session 101 — 2026-03-15

### beauty_tramp_session95.c (no new artifact)
- **md5:** cc34e62fee07676e12d0824c14fe6e85 — UNCHANGED from session95
- **compile status:** not recompiled (no sno2c changes)
- **crosscheck:** 106/106 maintained

### Session 101 work
- No compiler or sno2c changes.
- README.md: authorship corrections — Jeffrey built Roslyn runtime, Lon did MSIL
  speedup; TINY is co-authored by Lon + Claude Sonnet 4.6 (third developer).
- Taglines fixed: snobol4all. snobol4now. snobol4ever. / SNOBOL for all. SNOBOL for now. SNOBOL forever.

### Active bug / next action
- Sprint A (`beauty-crosscheck`) begins next session (102).
- First: build beauty_full_bin → write 101_comment test → run run_beauty.sh.
- See TINY.md §NOW and FRONTEND-SNOBOL4.md §Rung 12 Test Format.

## Session 102 — 2026-03-15

### beauty_tramp_session95.c (no new artifact)
- **md5:** cc34e62fee07676e12d0824c14fe6e85 — UNCHANGED
- **compile status:** not recompiled (no sno2c changes)
- **crosscheck:** 106/106 maintained

### Session 102 work
- No compiler changes.
- HQ matrix audit: 8 discrepancies found vs real frontend×backend spec.
- profile/README.md: SNOBOL4/SPITBOL merged into one frontend column (they are
  one executable, switch-selectable). CSNOBOL4 and SPITBOL correctly identified
  as oracles, not our products. Python row removed. Rebus/ICON/Prolog columns added.
- Remaining 7 discrepancies documented, not yet fixed (next session).

### Active bug / next action
- Sprint A (`beauty-crosscheck`) — still pending, begin session 103.
- Remaining HQ discrepancies to fix: FRONTEND-REBUS.md scope, stale FRONTEND-SNO2C.md
  references, BACKEND-NET/JVM missing TINY mention, JVM/DOTNET frontier tables,
  TINY.md missing Tiny-Prolog row, PLAN.md matrix cleanup.

## Session 103 — 2026-03-15
- **Artifact:** beauty_tramp_session103.c
- **md5:** b35a3c1b1e2b7e11cf1aaee9adafc19d
- **Lines:** 15537
- **Compile status:** OK (beauty_full_bin builds)
- **Active bug:** E_NAM/~ dispatch fixed (E_QLIT→emit_imm do_shift=1, varname sanitization skipped for tags). Shift("=") now fires. But Function/Id/Integer atoms in pattern context: match() FRETURN not propagating as pattern failure — pat_Function succeeds for any identifier. Subject and replacement shift incorrectly as Call(2) nodes. Next: fix function-call-in-pattern failure propagation in emit_byrd.c.

## Session 104 — 2026-03-15
- **Artifact:** beauty_tramp_session104.c
- **md5:** 37ede108d27279ebcd73e09f6796e62c
- **Lines:** 15542
- **Compile status:** OK (beauty_full_bin builds)
- **Active bug:** Named pattern RHS truncation. `Function = SPAN(...) $ tx $ *match(Functions, TxInList)` — compiled pat_Function only contains the SPAN, dropping `$ tx` and `$ *match(...)`. Root: byrd_emit_named_pattern() receives only partial AST. Investigate emit.c:1939 — how replacement expression is extracted and passed. Fix: ensure full concatenation AST reaches Byrd emitter.

## session105
- Date: 2026-03-15
- md5: 7f4e252a32d5c23f05e296b728c4618b
- Lines: 15632
- Compile status: gcc ERROR (duplicate labels — fix in progress)
- Active bug: E_DOL computed-right label duplication in byrd_emit
  - dolc_N_resume / dolc_N_rb defined twice
  - l_lb vs l_rb separation fix drafted but not yet applied cleanly

## session106 — 2026-03-15
- **Artifact:** beauty_tramp_session106.c
- **md5:** e8cbe7a005bb99507a4a27951cc98565
- **Lines:** 15632
- **Compile status:** OK — beauty_full_bin builds clean, zero errors
- **Crosscheck:** 106/106 pass
- **Beauty crosscheck:** 101_comment PASS, 102_output FAIL

### Work done this session
- **Fixed E_DOL computed-right label dup** (the session105 blocking bug).
  Root cause: any label passed as `beta` to `byrd_emit()` is PLG-emitted as
  a C label inside that recursion. The old code passed the same label to two
  recursive calls, producing a duplicate C label definition.
  Fix: follow the `emit_seq` pattern — PLG(alpha, left_a) and PLG(beta, right_b)
  BEFORE recursion; each arm receives a unique internal label as its beta.
  Wiring: left arm beta=left_b, right arm beta=right_b (== outer beta, defined
  upfront by PLG). No label is PLG-emitted more than once.
- **Removed unused `uid` in E_ATP case** (warning cleanup).
- **4× crosscheck speedup**: precompile runtime into static archive once per run.
  Per-test gcc time: 2100ms → 410ms. Total: 255s → 61s.
  Change in: test/crosscheck/run_crosscheck.sh.

### Active bug / next action
- 102_output FAIL: `OUTPUT = 'hello'` → empty line (blank with spaces).
  Same failure for any assignment (`FOO = 'hello'`).
  beauty.sno uses -INCLUDE 'assign.sno' (line 14).
  pp() walk produces blank output — assignment tree node not being pretty-printed.
  Next: read inc/assign.sno, trace which Shift/Reduce tree node assignment produces,
  find the pp() case that handles it, diagnose why output is blank.

## Session 113 — 2026-03-16
- md5: 22c0a70210fea64cba68e916c0f2ca34
- lines: 15814
- compile: OK
- active bug: Bug5 — concat counter (Expr4/X4 NINC_AT_fn fix partially applied in beauty_full.c; sno2c emit_byrd.c not yet updated; pp_.. crash on Reduce("..",2) unresolved)
session115 | 2026-03-16 | 6d5919daa03d3c56646b5f0a165f86ee | 15859 lines | compiles clean | Bug6 fixed (101-105 PASS 106/106)

## session116 — 2026-03-16
- **md5:** 185f391427d156b6a7ee5f9153f19b89
- **lines:** 16307
- **compile:** ok
- **active bug:** Bug5 saved-frame NSTACK_AT port incomplete — pending_npush_uid not surviving nested CAT levels to reach E_OPSYN; 101-103 PASS, 104-105 FAIL from regenerated C
- **note:** beauty_full_bin (in repo) still from WIP — passes 101-105; emit_byrd.c port WIP

## session147 — 2026-03-17 — ASM backend Sprint A4+A5: M-ASM-ALT + M-ASM-ARBNO ✅

### artifacts/asm/alt_first.s  (Sprint A4 — M-ASM-ALT ✅)
- **status:** PASS — subject "cat", pattern `LIT("cat") | LIT("dog")` → `cat\n` exit 0
- **milestone:** M-ASM-ALT fires this session
- **assemble:** `nasm -f elf64 alt_first.s -o alt_first.o && ld alt_first.o -o alt_first_bin && ./alt_first_bin`
- **design:** ALT α saves cursor_at_alt; left_ω restores and jumps right_α; both γ wire to alt_γ; right_ω → alt_ω. Proebsting §4.5 ifstmt pattern, compile-time wiring (no indirect jmp needed for two-arm pattern ALT).

### artifacts/asm/alt_second.s  (Sprint A4 — M-ASM-ALT ✅)
- **status:** PASS — subject "dog" → first arm fails, second arm matches → `dog\n` exit 0
- **assemble:** `nasm -f elf64 alt_second.s -o alt_second.o && ld alt_second.o -o alt_second_bin && ./alt_second_bin`
- **design:** identical wiring to alt_first.s, only subject differs

### artifacts/asm/alt_fail.s  (Sprint A4 — M-ASM-ALT ✅)
- **status:** PASS — subject "fish" → both arms fail → no output, exit 1
- **assemble:** `nasm -f elf64 alt_fail.s -o alt_fail.o && ld alt_fail.o -o alt_fail_bin && ./alt_fail_bin`
- **design:** right_ω → alt_ω chain; alt_ω exits 1

### artifacts/asm/arbno_match.s  (Sprint A5 — M-ASM-ARBNO ✅)
- **status:** PASS — subject "aaa", `POS(0) ARBNO(LIT("a")) RPOS(0)` → `aaa\n` exit 0
- **milestone:** M-ASM-ARBNO fires session147
- **assemble:** `nasm -f elf64 arbno_match.s -o arbno_match.o && ld arbno_match.o -o arbno_match_bin && ./arbno_match_bin`
- **design:** flat .bss cursor stack (64 slots + depth counter). α pushes cursor, succeeds immediately (zero reps). β pops, tries one LIT rep; if cursor advances pushes new cursor and re-succeeds. Zero-advance guard prevents infinite loop.

### artifacts/asm/arbno_empty.s  (Sprint A5 — M-ASM-ARBNO ✅)
- **status:** PASS — subject "aaa", `POS(0) ARBNO(LIT("x")) RPOS(0)` → no output, exit 1
- **assemble:** `nasm -f elf64 arbno_empty.s -o arbno_empty.o && ld arbno_empty.o -o arbno_empty_bin && ./arbno_empty_bin`
- **design:** zero reps succeed, RPOS(0) fails, backtrack β, LIT("x") fails → exit 1

### artifacts/asm/arbno_alt.s  (Sprint A5 — M-ASM-ARBNO ✅)
- **status:** PASS — subject "abba", `POS(0) ARBNO(LIT("a")|LIT("b")) RPOS(0)` → `abba\n` exit 0
- **assemble:** `nasm -f elf64 arbno_alt.s -o arbno_alt.o && ld arbno_alt.o -o arbno_alt_bin && ./arbno_alt_bin`
- **design:** ALT wired inside ARBNO β rep attempt; rep_success checks zero-advance guard then pushes new cursor → arbno_γ

## session146 — 2026-03-17 — ASM backend Sprint A0–A1

### artifacts/asm/null.s  (Sprint A0 — M-ASM-HELLO ✅)
- **status:** PASS — assembles, links, runs → exit 0
- **milestone:** M-ASM-HELLO fired session145; artifact archived here session146
- **assemble:** `nasm -f elf64 null.s -o null.o && ld null.o -o null && ./null`

### artifacts/asm/lit_hello.s  (Sprint A1 — M-ASM-LIT ✅)
- **status:** PASS — `hello\n` on stdout, exit 0; diff vs lit_hello_expected.txt CLEAN
- **milestone:** M-ASM-LIT fires session146
- **assemble:** `nasm -f elf64 lit_hello.s -o lit_hello.o && ld lit_hello.o -o lit_hello && ./lit_hello`
- **design:** α/β/γ/ω as real NASM labels; cursor+saved_cursor in flat .bss qwords; no structs, no malloc
- **notes:** repe cmpsb for multi-byte compare; single-char case can use cmp byte
- **active:** Sprint A2 next — POS/RPOS nodes
