# IR_LOWER_SNOBOL4.md — Phase 5 audit: SNOBOL4 frontend lower-to-IR

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Milestone: M-G5-LOWER-SNOBOL4-AUDIT*

## Method

Extracted all `E_*` identifiers from `src/frontend/snobol4/parse.c` and `sno.y`.
Cross-referenced against `src/ir/ir.h` (canonical enum + IR_COMPAT_ALIASES).
Checked which names are actually *assigned* to `e->kind` (vs. referenced in
comments, switch cases, or dead legacy code).

## Node kinds actually emitted by SNOBOL4 parse.c

| Kind (as used) | Canonical ir.h name | Status |
|----------------|---------------------|--------|
| E_QLIT | E_QLIT | ✅ canonical |
| E_ILIT | E_ILIT | ✅ canonical |
| E_FLIT | E_FLIT | ✅ canonical |
| E_KW | E_KW | ✅ canonical |
| E_NULV | E_NUL | ✅ alias (IR_COMPAT_ALIASES) |
| E_VART | E_VAR | ✅ alias |
| E_ADD | E_ADD | ✅ canonical |
| E_SUB | E_SUB | ✅ canonical |
| E_MPY | E_MPY | ✅ canonical |
| E_DIV | E_DIV | ✅ canonical |
| E_EXPOP | E_POW | ✅ alias |
| E_MNS | E_NEG | ✅ alias |
| E_INDR | E_INDR | ✅ canonical |
| E_SEQ | E_SEQ | ✅ canonical |
| E_CONCAT | E_CONCAT | ✅ canonical |
| E_OR → E_ALT | E_ALT | ✅ alias |
| E_NAM | E_CAPT_COND | ✅ alias |
| E_DOL | E_CAPT_IMM | ✅ alias |
| E_ATP | E_CAPT_CUR | ✅ alias |
| E_ARB | E_ARB | ✅ canonical |
| E_ARBNO | E_ARBNO | ✅ canonical |
| E_STAR | E_STAR | ✅ canonical |
| E_FNC | E_FNC | ✅ canonical |
| E_IDX | E_IDX | ✅ canonical |
| E_OPSYN | E_OPSYN | ✅ canonical |
| E_ASGN / E_ASSIGN | E_ASGN | ✅ canonical (both spellings present; ASSIGN is alias) |
| E_VAR | E_VAR | ✅ canonical |
| E_NEG | E_NEG | ✅ canonical |
| E_POW | E_POW | ✅ canonical |

## Non-emitted names (comments / dead scrip_cc.h legacy)

E_AT, E_CALL, E_COND, E_DEREF, E_IMM, E_INDEX, E_INT, E_KEYWORD,
E_MUL, E_NULL, E_REAL, E_REDUCE, E_STR — zero assignments to `e->kind`
in parse.c. These appear in switch cases or comments inherited from
scrip_cc.h's older vocabulary. Not emitted; not a gap.

## Gap table

| Gap | Type | Action needed |
|-----|------|---------------|
| None | — | — |

## Conclusion

**M-G5-LOWER-SNOBOL4-AUDIT: PASS — no gaps found.**

The SNOBOL4 frontend produces only canonical ir.h node kinds (or compat-aliased
equivalents). IR_COMPAT_ALIASES is `#define`d in `scrip_cc.h`, so the old names
compile correctly today. Phase 5 Fix milestone (M-G5-LOWER-SNOBOL4-FIX) is a
no-op — no fixes required.

The alias names (E_VART, E_NULV, E_EXPOP, E_MNS, E_OR, E_NAM, E_DOL, E_ATP)
should be migrated to their canonical forms as part of Phase 3 naming cleanup
(M-G3-NAME-X64 etc.) when each backend file is touched. Not Phase 5 scope.

