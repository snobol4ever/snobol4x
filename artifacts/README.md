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
