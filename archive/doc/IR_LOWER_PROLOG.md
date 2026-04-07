# IR_LOWER_PROLOG.md — Phase 5 audit: Prolog frontend lower-to-IR

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Milestone: M-G5-LOWER-PROLOG-AUDIT*

## Node kinds emitted by prolog_lower.c

| Kind | Canonical ir.h name | Status |
|------|---------------------|--------|
| E_CHOICE | E_CHOICE | ✅ canonical |
| E_CLAUSE | E_CLAUSE | ✅ canonical |
| E_CUT | E_CUT | ✅ canonical |
| E_UNIFY | E_UNIFY | ✅ canonical |
| E_TRAIL_MARK | E_TRAIL_MARK | ✅ canonical |
| E_TRAIL_UNWIND | E_TRAIL_UNWIND | ✅ canonical |
| E_FNC | E_FNC | ✅ canonical (builtins / user calls) |
| E_ILIT | E_ILIT | ✅ canonical (integer literals) |
| E_QLIT | E_QLIT | ✅ canonical (atom literals) |

## Gap table

None. All emitted kinds are present in ir.h as first-class canonical enum values.
No compat aliases needed — the Prolog IR was defined specifically for the unified
enum (Phase 1, M-G1-IR-HEADER-DEF).

## Conclusion

**M-G5-LOWER-PROLOG-AUDIT: PASS — no gaps found (as expected per GRAND_MASTER_REORG.md).**
M-G5-LOWER-PROLOG-FIX is a no-op.

