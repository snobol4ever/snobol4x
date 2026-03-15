# Artifacts — beauty_tramp generated C snapshots

| Session | Date | Lines | MD5 | Compile | Status |
|---------|------|-------|-----|---------|--------|
| 73 | 2026-03-15 | — | 2028344da06f3f862deae3efedf4bc9b | 0 errors | quote-strip fixes |
| 76 | 2026-03-15 | — | 2028344da06f3f862deae3efedf4bc9b | 0 errors | M-CNODE cnode-wire done |
| 77 | 2026-03-14 | 31773 | e784cec765f711df8bbe7a2427689eae | 0 errors | CHANGED — pat_lit strv() bug fixed |

## Session 77 notes
- Commit: `0113d90 fix(emit_cnode): pat_lit takes const char* not SnoVal — remove strv() wrapper in build_pat E_STR`
- pat_lit(strv("...")) → pat_lit("...") in build_pat E_STR case
- Binary compiles 0 errors
- START → START now works ✅
- Active bug: $expr indirect read uses e->left (NULL) instead of e->right — see SESSION.md
- `$'@S' = link(...)` stores STRING not link UDEF — broken until E_DEREF fix applied

## Session 78 notes
- Commit: `b20329f fix(emit_cnode): build_expr E_DEREF — check !e->left first, use e->right for $expr`
- emit_cnode.c build_expr E_DEREF fixed: grammar puts $expr operand in e->right (e->left is NULL)
- Old code fell through to build_expr(a, e->left) → NULL → deref(NULL_VAL)
- New code mirrors emit_expr: !e->left → deref(e->right), e->left->E_VAR → var_as_pattern(pat_ref(...))
- Binary compiles 0 errors with mock_engine.c
- Parse Error still active — emit.c emit_expr E_DEREF NOT YET FIXED (session 79 first action)
- Lines: 31776 | MD5: 5046a4b6f8a751ea92a67d271c1c05a2 | CHANGED from session 77
- Session also: TINY.md/SESSION.md rewritten (19-session staleness fixed), bootstrap plan written

## Session 80 — 2026-03-14
- **Artifact**: beauty_tramp_session78.c (UNCHANGED — md5 5046a4b6f8a751ea92a67d271c1c05a2)
- **sno2c changes**: none this session
- **Runtime fixes this session**:
  - `mock_engine.c`: added T_FUNC and T_CAPTURE handlers
  - `snobol4_pattern.c`: SPAT_USER_CALL materialise — primitive builtins (ANY/SPAN/BREAK/NOTANY/LEN/POS/RPOS/TAB/RTAB) now resolve to proper T_* nodes instead of T_FUNC
  - `snobol4.c`: pre-init UCASE, LCASE, digits in runtime_init (physical constants, available before global.sno line 25)
- **Active bug**: `c[N]` subscript in pp_Stmt returns wrong value — `_c` variable type needs tracing. `indx(get(_c), {vint(N)}, 1)` suspected to be operating on UDEF tree node not SARRAY children
- **Symptom**: ` OUTPUT = 'hello'` outputs `OUTPUT` only — ppLbl correct, ppSubj/ppPatrn/ppRepl wrong
- **Next action**: trace type of `_c` at pp_Stmt line 431 — add fprintf after `set(_c, _v803)` in beauty_tramp.c

## Session 91 — 2026-03-15

| Field | Value |
|-------|-------|
| Artifact | beauty_tramp_session79.c |
| Lines | 15452 |
| md5 | e0ebfbf38e866f92e28a999db182a6a2 |
| Compile status | not tested (crosscheck sprint, not beauty sprint) |
| Changed from previous | CHANGED (session78 md5=5046a4b6f8a751ea92a67d271c1c05a2) |
| Active bug | _mstart set before ARB scan — replacement splices from pos 0 instead of match start |
| HEAD | 4e0831d |

Changes since session78: bare builtin patterns (REM/FAIL/ARB/SUCCEED/FENCE/ABORT) as
E_VART now route to correct emitters; POS/RPOS/TAB/RTAB with variable args emit
to_int(NV_GET_fn("var")) dynamically instead of hardcoded 0.

## Session 93 — 2026-03-14

**Artifact:** `beauty_tramp_session93.c`
**Lines:** 15638
**md5:** f6938127f2aad8592ba7e7ff8b1255ea
**Compile:** 2 errors (_comment/_control undeclared — pre-existing)
**Status:** CHANGED from session79

**Changes since session79:**
- SNO_MSTART: _mstart set after ARB scan (not before)
- Null replacement (X pat =) now deletes matched region
- pat_is_anchored: dynamic POS(N) gets ARB wrap
- ? operator in statement position (S ? P and S ? P = R)
- E_NAM conditional capture: deferred via pending-cond list
- E_ATP position capture: @VAR emitted (bug: @→_ pending fix)
- coerce_numeric: integer-string args in arithmetic stay DT_I
- null treated as integer 0 in arithmetic

**Active bug:** E_ATP emits to _ instead of varname (pat->right vs pat->sval)
**Rung status:** 1-7 clean (64/64), rung 8 15/17

## Session 94 — 2026-03-15

- **Artifact:** beauty_tramp_session94.c
- **Lines:** 15641
- **md5:** 31dfdcbf66138fcf831c9844c1155af0
- **Changed from session93:** YES (15638→15641, +3 lines)
- **Compile status:** not tested against beauty_full_bin (crosscheck sprint)
- **Changes:** E_ATP varname fix (left not right), E_ATP beta→omega, ARB kw_anchor guard, BREAKX implemented, DIFFER fix, &STCOUNT/&STNO/&STLIMIT/&ANCHOR/&TRIM/&FULLSCAN wired into NV_GET/SET
- **Active bug:** 082_keyword_stcount still fails (STNO reads kw_stcount — off by one?), 100_roman_numeral (block_roman_end not emitted in Pass 2 block walker)
- **Rung 8:** 17/17 ✅ | **Rung 9:** 9/11
