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
