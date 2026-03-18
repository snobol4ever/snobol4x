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

## session148 — 2026-03-17 — ASM backend Sprint A7 (M-ASM-ASSIGN)

### artifacts/asm/assign_lit.s  (Sprint A7 — M-ASM-ASSIGN ✅)
- **status:** PASS — `POS(0) LIT("hello") $ CAP RPOS(0)` on `"hello"` → `hello\n` exit 0
- **milestone:** M-ASM-ASSIGN fires session148
- **assemble:** `nasm -f elf64 assign_lit.s -o assign_lit.o && ld assign_lit.o -o assign_lit && ./assign_lit`
- **design:** DOL α saves entry_cursor; child (LIT) γ jumps dol_γ; dol_γ computes span length, rep movsb into cap_buf, stores cap_len; dol_ω propagates failure without assignment. No rollback needed — $ is forward-only (v311.sil ENMI).

### artifacts/asm/assign_digits.s  (Sprint A7 — M-ASM-ASSIGN ✅)
- **status:** PASS — `SPAN("0123456789") $ NUM` on `"abc123def"` (unanchored) → `123\n` exit 0
- **assemble:** `nasm -f elf64 assign_digits.s -o assign_digits.o && ld assign_digits.o -o assign_digits && ./assign_digits`
- **design:** Outer unanchored loop advances outer_cursor on dol_ω; SPAN counts consecutive digit chars (greedy, no β retry); dol_γ copies span into cap_buf. Demonstrates $ capture across non-LIT child with unanchored match.

### emit_byrd_asm.c — E_DOL wired (session148)
- Added `emit_asm_assign()` implementing the DOL Byrd box for `expr $ var`
- Also handles `E_NAM` (`.` conditional assignment) with same box — assignment timing distinction deferred to crosscheck phase
- `cap_buf` (resb 256) registered via `asm_extra_bss[]`; `cap_len` + `dol_N_entry` via `bss_add()`
- 106/106 crosscheck invariant confirmed PASS before and after

## session148b — 2026-03-17 — ASM backend Sprint A8 (M-ASM-NAMED)

### artifacts/asm/ref_astar_bstar.s  (Sprint A8 — M-ASM-NAMED ✅)
- **status:** PASS — `POS(0) ASTAR BSTAR RPOS(0)` on `"aaabb"` → `aaabb\n` exit 0
- **milestone:** M-ASM-NAMED fires session148
- **assemble:** `nasm -f elf64 ref_astar_bstar.s -o ref_astar_bstar.o && ld ref_astar_bstar.o -o ref_astar_bstar && ./ref_astar_bstar`
- **design:** Two named patterns ASTAR=ARBNO(LIT("a")) and BSTAR=ARBNO(LIT("b")). Calling convention: caller stores γ/ω addresses into `pat_NAME_ret_gamma/omega` (.bss qwords) then `jmp pat_NAME_alpha`. Named pattern body ends with `jmp [pat_NAME_ret_gamma]` / `jmp [pat_NAME_ret_omega]`. Pure indirect-jmp — no call stack (Proebsting §4.5 gate mechanism).

### artifacts/asm/anbn.s  (Sprint A8 — M-ASM-NAMED ✅)
- **status:** PASS — `A_BLOCK A_BLOCK B_BLOCK B_BLOCK RPOS(0)` on `"aabb"` → `aabb\n` exit 0
- **assemble:** `nasm -f elf64 anbn.s -o anbn.o && ld anbn.o -o anbn && ./anbn`
- **design:** Four sequential named-pattern call sites (2×A_BLOCK + 2×B_BLOCK). Proves multiple references to same named pattern in sequence, and full backtrack chain through all call sites.

### emit_byrd_asm.c — named pattern support (session148)
- `AsmNamedPat` registry (64 slots); `asm_safe_name()` sanitiser
- `asm_named_register()` / `asm_named_lookup()` 
- `emit_asm_named_ref()` — call site: loads γ/ω into ret_ slots, `jmp pat_NAME_alpha/beta`; γ/ω trampolines forward to caller's continuation labels
- `emit_asm_named_def()` — named pattern body: `pat_NAME_alpha:` entry, recursive `emit_asm_node`, inner γ/ω → `jmp [pat_NAME_ret_gamma/omega]`
- `asm_scan_named_patterns()` — pre-pass over program to register all `VAR = <expr>` assignments
- `E_VART` wired in `emit_asm_node` → `emit_asm_named_ref()`
- End-to-end: `.sno` → `sno2c -asm` → `.s` → `nasm` → `ld` → run: PASS/FAIL correct
- 106/106 crosscheck invariant confirmed

## session151 — 2026-03-17 — ASM backend Sprint A9 (M-ASM-CROSSCHECK ✅)

### artifacts/asm/multi_capture_abc.s  (Sprint A9 — M-ASM-CROSSCHECK ✅)
- **status:** PASS — `LEN(2) . A LEN(2) . B LEN(2) . C` on `"abcdef"` → `ab\ncd\nef\n` exit 0
- **milestone:** M-ASM-CROSSCHECK fires session151
- **assemble:** `nasm -f elf64 multi_capture_abc.s -o multi_capture_abc.o && gcc -no-pie -o multi_capture_abc multi_capture_abc.o snobol4_asm_harness.o && ./multi_capture_abc abcdef`
- **design:** Per-variable capture buffers (`cap_A_buf resb 256`, `cap_A_len resq 1` etc.) in `.bss`. `cap_order[]` table in `.data` — null-terminated `{name*, buf*, len*}` triples. Harness walks `cap_order` at `match_success`, prints one capture per line. `/dev/null` dry-run collection pass pre-registers all symbols before sections are emitted; uid counter saved/restored so real pass generates identical labels.

### artifacts/asm/star_deref_capture.s  (Sprint A9 — M-ASM-CROSSCHECK ✅)
- **status:** PASS — `*PAT . V` where `PAT = 'hello'` on `"say hello world"` → `hello\n` exit 0
- **assemble:** `nasm -f elf64 star_deref_capture.s -o star_deref_capture.o && gcc -no-pie -o star_deref_capture star_deref_capture.o snobol4_asm_harness.o && ./star_deref_capture "say hello world"`
- **design:** `E_INDR` case in `emit_asm_node` resolves `*VAR` via named-pattern registry, calls `emit_asm_named_ref()`. `build_bare_sno` keeps plain-string assignments when var referenced as `*VAR`. `extract_subject` finds subject var from match line, then looks up its assignment.

### emit_byrd_asm.c — session151 changes
- `CaptureVar` registry: per-variable `cap_VAR_buf`/`cap_VAR_len` in `.bss`; `cap_order[]` table in `.data`
- `/dev/null` dry-run collection pass: replaces `open_memstream`; uid counter saved before dry run, restored before real pass
- `E_INDR` case added — `*VAR` indirect pattern ref resolved via named-pattern registry
- `.asm.ref` convention: `TEST.asm.ref` preferred over `TEST.ref` for harness-specific expected output
- 26/26 ASM crosscheck PASS · 106/106 main invariant holds

### beauty_tramp_session154 — no change
- md5: 185f391427d156b6a7ee5f9153f19b89 (same as session116)
- sessions 152–154: no change to sno2c C emitter path; only ASM emitter (emit_byrd_asm.c) modified
- active bug: Bug7 (FENCE nPop imbalance) — unchanged

### artifacts/asm/stmt_output_lit.s  (Sprint A10 — step 1)
- status: PASS — OUTPUT = 'hello world' via ASM calling stmt_* shims
- assemble: nasm -f elf64 stmt_output_lit.s -o o.o && gcc -no-pie o.o [rt] -lgc -lm -o prog
- design: DESCR_t=16 bytes rax:rdx; stmt_init+stmt_strval+stmt_is_fail+stmt_output

### artifacts/asm/stmt_assign.s  (Sprint A10 — step 2)
- status: PASS — X = 'foo'; OUTPUT = X via ASM

### artifacts/asm/stmt_goto.s  (Sprint A10 — step 3)
- status: PASS — N=1; LOOP: OUTPUT=N; N+1; GT(N,3):F(LOOP) via ASM

### artifacts/asm/beauty_prog_session154.s  (Sprint A10 — M-ASM-BEAUTY in progress)
- status: ASSEMBLES CLEAN — beauty.sno compiled via sno2c -asm, 6091 lines
- nasm: clean (zero errors)
- link: links clean against snobol4_stmt_rt.o + runtime
- runs: hangs — pattern-match stmts fall through (Case 2 not yet wired to Byrd box)
- next: stmt_setup_subject() shim + jmp root_alpha per pattern stmt
- assemble: nasm -f elf64 beauty_prog_session154.s -o beauty_prog.o

### artifacts/asm/beauty_prog_session155.s  (Sprint A10)
- status: ASSEMBLES CLEAN — S_/L_/P_/P_N_αβγω naming convention
- Convention: S_=string literal, L_=label, P_=pattern (named body + Byrd ports)
- Greek α/β/γ/ω for internal Byrd ports (not linkable as extern symbols)
- Named pattern bodies (P_name_alpha/beta) now emitted in program mode
- Remaining: asm_named_count=0 at body emission (reset bug); pattern match
  stmt execution (Case 2) wired but not yet running

### artifacts/asm/beauty_prog_session156.s  (Sprint A12 — M-ASM-MACROS begun)
- status: macro-driven — %include snobol4_asm.mac; PROG_INIT/PROG_END/STMT_SEP
- L_SNO_END replaces _SNO_END; L_/S_/P_ naming consistent
- global cursor/subject_data/subject_len_val exported for stmt_rt.c linkage
- hello-world test: assembles + links + runs correctly
- remaining: all inline mov/call → macro calls; body macros (GET_VAR/SET_OUTPUT etc)

### artifacts/asm/beauty_prog_session156b.s  (Sprint A12 — end of session156)
- status: 0 errors, 1 warning (label-orphan from STMT_SEP — benign)
- 12843 lines; cap_* symbols fixed; empty db fixed; STMT_SEP reordered
- links clean; hangs on run (asm_named_count=0 prevents pattern execution)
- next: fix asm_named_count, wire pattern-match execution, M-ASM-BEAUTY

### beauty_tramp session156b — no change
- md5: 185f391427d156b6a7ee5f9153f19b89 (same as session116)

## beauty_prog_session158.s

- **Session:** 158
- **Sprint:** A10 M-ASM-BEAUTY
- **Status:** 101_comment PASS ✅; 102-109 Parse Error (E_OR/E_CONC → NULVCL for named pattern assignments)
- **Assemble:** `nasm -f elf64 -i src/runtime/asm/ beauty_prog_session158.s -o /dev/null`
- **Changes since session156:**
  - `section .text` before named pattern bodies (was in `.data` → segfault)
  - `PROG_INIT`: push r15..rbx before frame; `sub rsp,56` (16-byte aligned: 6 pushes+56=112)
  - `PROG_END`: explicit pops matching push order
  - E_FNC → `stmt_apply()` in `prog_emit_expr`
  - Case 1 S/F dispatch for expression-only stmts (DIFFER/IDENT with `:F`)
  - `stmt_set_capture()` shim + gamma-path materialisation of DOL/NAM captures
  - Pattern capture working: `X *PAT . V` → `V = "bc"` PASS

### artifacts/asm/beauty_prog_session159.s  (Sprint A10/A14 — M-ASM-BEAUTY/M-ASM-BEAUTIFUL progress)
- status: ASSEMBLES CLEAN — beauty.sno compiled to macro-driven x64 ASM
- session: 159 — pivot to M-ASM-BEAUTIFUL
- assemble: nasm -f elf64 -I src/runtime/asm/ beauty_prog_session159.s -o /dev/null
- lines: 18220
- changes this session:
  - E_OR/E_CONC → ALT/CONCAT runtime builtins registered; test 101 PASS
  - snobol4_asm.mac: STORE_ARG32/16, LOAD_NULVCL/32, APPLY_FN_0/N, SET_CAPTURE,
    IS_FAIL_BRANCH/16, LOAD_VAR, SETUP_SUBJECT_FROM16 macros added
  - emit_byrd_asm.c: prog_emit_expr + asm_emit_program raw register sequences
    replaced with macro calls throughout (GET_VAR, LOAD_STR, LOAD_INT, SET_VAR,
    SET_OUTPUT, IS_FAIL_BRANCH, APPLY_FN_0/N, STORE_ARG32, SET_CAPTURE,
    APPLY_REPL, SETUP_SUBJECT_FROM16, LOAD_NULVCL)
- invariants: 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session160.s
- **Sprint:** A14 — M-ASM-BEAUTIFUL
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Lines:** 16421 (vs 18220 session159 — 1799 lines eliminated)
- **What changed:** All pattern node ports now emit named macros instead of raw instructions:
  LIT_ALPHA/LIT_BETA, SPAN_ALPHA/SPAN_BETA, BREAK_ALPHA/BREAK_BETA,
  ANY_ALPHA/ANY_BETA, NOTANY_ALPHA/NOTANY_BETA, POS_ALPHA/POS_BETA,
  RPOS_ALPHA/RPOS_BETA, LEN_ALPHA/LEN_BETA, TAB_ALPHA/TAB_BETA,
  RTAB_ALPHA/RTAB_BETA, REM_ALPHA/REM_BETA, SEQ_ALPHA/SEQ_BETA,
  ALT_SAVE_CURSOR/ALT_RESTORE_CURSOR, STORE_RESULT/SAVE_DESCR.
  Body-only (-asm-body) now emits %include + crosscheck script uses -I flag.
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session161.s
- **Sprint:** A14 — M-ASM-BEAUTIFUL
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Lines:** 15883 (vs 16421 session160 — 538 more lines eliminated)
- **What changed:** ALF() helper — label and instruction on the same line.
  Every Byrd box port now reads: `label:  MACRO  args`
  40 asmL+A/asmJ pairs folded into single ALF() calls.
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session162.s
- **Sprint:** A14 — M-ASM-BEAUTIFUL
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Lines:** 14950 (down from 18220 at session159 — 3270 lines eliminated total)
- **What changed:** ALFC() helper — three-column format: `label:  MACRO args ; comment`
  Comments folded from preceding lines onto the instruction line.
  ALT emitter now uses ALT_SAVE_CURSOR/ALT_RESTORE_CURSOR macros fully.
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session163.s
- **Sprint:** A14 — M-ASM-BEAUTIFUL
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Lines:** 14448 (down 3772 from session159)
- **What changed:** DOL_SAVE/DOL_CAPTURE/ALT_ALPHA/ALT_OMEGA macros collapse multi-line
  blocks to single lines. All double-newlines removed. Every state is one line:
  `label:  MACRO args ; comment` — four columns throughout.
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session164.s
- **Sprint:** A14 — M-ASM-BEAUTIFUL
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Lines:** 13664 (down 4556 from session159's 18220)
- **What changed:** Pending-label mechanism in A() — labels fold onto their
  first instruction. Rule: label on own line only when two labels are consecutive.
  `L_sn_0:  GET_VAR S_457` — one line per state throughout program body.
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session165.s
- **Sprint:** A14 — M-ASM-BEAUTIFUL
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Lines:** 13664 (same as session164)
- **What changed:** Inline column alignment (COL_W=28). Added out_col tracker,
  oc_char()/oc_str()/emit_to_col() — counts display columns, skips UTF-8
  continuation bytes. Every instruction (labeled or unlabeled) starts at
  display column 28. ALFC fixed to use emit_to_col instead of %-28s printf
  padding. STMT_SEP/PORT_SEP/directives exempt from alignment.
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session166.s
- **Session:** 166
- **Lines:** 13664
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/)
- **Change:** STMT_SEP shifted to column 28 (instruction column); was 4-space indent
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session167.s
- **Session:** 167
- **Lines:** 12745 (down 919 from session166 — 6.7% reduction)
- **Status:** assembles clean (nasm -f elf64 -I src/runtime/asm/); one pre-existing warning (empty db)
- **Changes:**
  1. **Macro collapses (M-ASM-BEAUTIFUL sprint A14):**
     - `ASSIGN_INT var, n, fail_lbl` — collapses LOAD_INT + IS_FAIL_BRANCH + SET_VAR
     - `ASSIGN_STR var, s, fail_lbl` — collapses LOAD_STR + IS_FAIL_BRANCH + SET_VAR
     - `CALL1_INT fn, n` — collapses sub rsp + LOAD_INT + STORE_ARG32 + APPLY_FN_N + add rsp + mov-pair
     - `CALL1_STR fn, s` — same with LOAD_STR
     - Redundant `mov [rbp-32],rax` / `mov [rbp-24],rdx` after LOAD_INT/LOAD_STR eliminated (those macros already write there)
     - Post-APPLY_FN_N raw mov pair → STORE_RESULT macro
  2. **Comment separators (SEP_W=80, configurable):**
     - `emit_sep_major(tag)` — `; === tag ====...` at every SNOBOL4 statement, section headers, named pattern headers
     - `emit_sep_minor(tag)` — `; --- tag ----...` before γ/ω trampolines within named pattern defs
     - STMT_SEP NASM macro replaced by raw comment text (visible without macro expansion)
     - SNOBOL4 source labels embedded in `===` lines when present
     - Section headers: PROGRAM BODY / END / NAMED PATTERN BODIES / STUB LABELS / STRING TABLE
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

### artifacts/asm/beauty_prog_session168.s  (Sprint A14 — M-ASM-BEAUTIFUL, active)
- status: 12689 lines — assembles clean (1 pre-existing db-empty warning)
- session: session168
- assemble: nasm -f elf64 beauty_prog_session168.s -I src/runtime/asm/ -o /dev/null
- changes this session:
  - Macro renames in snobol4_asm.mac: IS_FAIL_BRANCH→FAIL_BR, IS_FAIL_BRANCH16→FAIL_BR16,
    SETUP_SUBJECT_FROM16→SUBJ_FROM16, CALL2_SS→CONC2, CALL2_SN→CONC2_N;
    ALT2/ALT2_N aliases added; all back-compat %define aliases preserved
  - COL2_W=12, COL_CMT=72 defined in emit_byrd_asm.c; ALFC comment column uses COL_CMT
  - CONC2_N/CONC2 fast paths added in E_OR/E_CONC for QLIT+NULV and QLIT+QLIT children
  - FAIL_BR/FAIL_BR16/SUBJ_FROM16 emit sites updated in emitter
- next: CONC2_SV macro (QLIT left + VART right — dominant shape, ~300 sites remaining)
- **Invariants:** 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

### artifacts/asm/beauty_prog_session169.s  (Sprint A14 — M-ASM-BEAUTIFUL)
- status: 12689 lines, NASM clean (1 harmless db-empty warning)
- change: SEP_W 80 → 120; separator lines now 120 chars wide (Cherryholmes standard)
- assemble: nasm -I src/runtime/asm/ -f elf64 beauty_prog_session169.s -o /dev/null
- invariants: 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

### artifacts/asm/beauty_prog_session170.s  (Sprint A14 — M-ASM-BEAUTIFUL)
- status: 12689 lines, NASM clean
- change: REF/DOL/ARBNO block-header comments moved to col2 on label line ("alpha: ; REF(Name)")
- ALFC empty-label guard added (suppresses bare ":" when label is "")
- assemble: nasm -I src/runtime/asm/ -f elf64 beauty_prog_session170.s -o /dev/null
- invariants: 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

### artifacts/asm/beauty_prog_session171.s  (Sprint A14 — M-ASM-BEAUTIFUL)
- status: 12444 lines (down 245 from session170), NASM clean
- change: CONC2_SV/CONC2_VS/CONC2_VN/CONC2_VV macros + fast paths; ALT2_SV/VS/VS/VN/VV aliases
- 529 verbose sub-rsp,32 blocks remain (nested expression trees — irreducible with atom fast-paths)
- assemble: nasm -I src/runtime/asm/ -f elf64 beauty_prog_session171.s -o /dev/null
- invariants: 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS

## beauty_prog_session172.s
- milestone: M-ASM-BEAUTIFUL (Sprint A14, active)
- status: 12100 lines (down 344 from session171), NASM clean
- change: CONC2_*16/ALT2_*16 macros (rbp-16 slot variants); E_FNC 2-arg atom fast paths; 529→496 verbose blocks
- assemble: nasm -I src/runtime/asm/ -f elf64 beauty_prog_session172.s -o /dev/null
- invariants: 106/106 C crosscheck PASS, 26/26 ASM crosscheck PASS
