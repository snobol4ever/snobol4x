# Artifacts — beauty_tramp generated C snapshots

## Session 74 — 2026-03-14

**File:** beauty_tramp_session74.c  
**Lines:** 30108  
**md5:** 2925548631caf0b659c04669bef5b6ef  
**Status:** CHANGED from session73

### What changed
- `emit_pretty.h` extracted — shared 3-column formatter (PLG/PL/PS/PG macros)
- `emit_byrd.c` now uses shared header (`#define PRETTY_OUT byrd_out`)
- `emit.c` now uses shared header (`#define PRETTY_OUT out`)
- `goto_target_str()` + `emit_pretty_goto()` helpers added — capture goto targets for 3-column output
- `emit_ok_goto()` helper extracted — replaces 3× identical cond-goto tail blocks
- All structural label/goto lines in `emit_stmt` and `emit_fn` converted to PLG/PL/PS/PG
- Compiles clean, same behavior as session73

### Active bug
`c` field of UDEF tree node returns SSTR (type=1) instead of ARRAY — `START` produces empty output.
See SESSION.md for full trace.

---

## Session 73 — 2026-03-15

**File:** beauty_tramp_session73.c  
**Lines:** 30108  
**md5:** 95c6eb104a1ab7cf5c8415c9fbbf9245  
**Status:** CHANGED from session69

### What changed
- `emit_byrd.c` emit_simple_val: strips outer single-quote pairs from E_STR sval
- `emit.c` computed-goto dispatch: strips all quote chars before strcmp chain
- Two fixes committed in `5837bf1`

### Active bug
Same `c` field / START bug.
