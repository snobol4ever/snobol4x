# IR_LOWER_SNOCONE.md — Phase 5 audit: Snocone frontend lower-to-IR

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Corrected: 2026-03-29, G-9 s14 (initial version overstated the gap)*
*Milestone: M-G5-LOWER-SNOCONE-AUDIT*

## Method

Read `snocone_lower.c`, `snocone_driver.c`, `snocone_cf.h`,
`snocone_lower.h`, `scrip_cc.h`, and `src/ir/ir.h`.

---

## Architecture: Snocone is a standard frontend

Snocone occupies the same position as SNOBOL4, Icon, and Prolog:

```
snocone_lex()
  → snocone_parse()   (shunting-yard → postfix ScPToken[])
  → snocone_lower()   (postfix → EXPR_t* / STMT_t* IR)
  → Program*
  → asm_emit / jvm_emit / net_emit   (same entry points as SNOBOL4)
```

**The IR is shared.** `snocone_lower.h` includes `scrip_cc.h`, which
defines `EXPR_T_DEFINED` and `IR_COMPAT_ALIASES` before including
`ir/ir.h`. The `EXPR_t` struct used by Snocone is **identical** to the
unified IR struct in `ir.h`. The alias names (`E_VART`, `E_MNS`, etc.)
are `#define`d to their canonical `EKind` values at compile time.

Snocone shares ~90%+ of its node kinds with SNOBOL4. This is by design:
the unified IR is a common pool; each language uses the subset it needs
plus any language-specific additions.

---

## Node kinds emitted by snocone_lower.c

| Alias used | Canonical EKind | Notes |
|---|---|---|
| `E_ILIT` | `E_ILIT` | integer literal |
| `E_FLIT` | `E_FLIT` | real literal |
| `E_QLIT` | `E_QLIT` | string literal |
| `E_VART` | `E_VAR` | identifier (IR_COMPAT_ALIAS) |
| `E_KW` | `E_KW` | `&keyword` |
| `E_ADD` | `E_ADD` | `+` binary |
| `E_SUB` | `E_SUB` | `-` binary |
| `E_MNS` | `E_NEG` | `-` unary (IR_COMPAT_ALIAS) |
| `E_MPY` | `E_MPY` | `*` binary |
| `E_DIV` | `E_DIV` | `/` |
| `E_EXPOP` | `E_POW` | `^` (IR_COMPAT_ALIAS) |
| `E_INDR` | `E_INDR` | `*` unary / `$` unary indirect |
| `E_CONCAT` | `E_CONCAT` | `\|\|`, `\|`, `&&` all map here |
| `E_NAM` | `E_CAPT_COND` | `.` conditional capture (IR_COMPAT_ALIAS) |
| `E_DOL` | `E_CAPT_IMM` | `$` immediate capture (IR_COMPAT_ALIAS) |
| `E_ATP` | `E_CAPT_CUR` | `@var` cursor capture (IR_COMPAT_ALIAS) |
| `E_ASGN` | `E_ASSIGN` | `=` assignment (IR_COMPAT_ALIAS) |
| `E_FNC` | `E_FNC` | function calls; also all comparison/relational ops |
| `E_IDX` | `E_IDX` | array subscript `a[i]` |

All comparison and relational operators (`EQ`, `NE`, `LT`, `GT`, `LE`,
`GE`, `IDENT`, `DIFFER`, `LLT`, `LGT`, `LLE`, `LGE`, `LEQ`, `LNE`,
`REMDR`, `NOT`) are lowered to `E_FNC` nodes with the SNOBOL4 built-in
name. This matches SNOBOL4's own frontend behaviour and is correct.

---

## Gap table

| # | Gap | Severity | Notes |
|---|---|---|---|
| G1 | Alias names in snocone_lower.c (`E_VART`, `E_MNS`, `E_EXPOP`, `E_NAM`, `E_DOL`, `E_ATP`, `E_ASGN`) | Cosmetic | Aliases resolve correctly at compile time. Migration to canonical names is M-G3 scope (naming law pass), not Phase 5. |
| G2 | `snocone_cf_compile()` only called for `asm_mode` | Medium | JVM and .NET receive expression-only IR; `if`/`while`/`for`/`procedure` are not lowered for those backends. Fix: call `snocone_cf_compile` unconditionally (or refactor cf-pass to be backend-agnostic). |
| G3 | `~` → `E_FNC("NOT",1)` rather than a first-class negation kind | Low | Works correctly via SNOBOL4 NOT built-in. No canonical `E_NOT` exists for SNOBOL4-family; consistent with how SNOBOL4's own parser handles it. Not a gap relative to SNOBOL4. |

---

## Conclusion

**M-G5-LOWER-SNOCONE-AUDIT: PASS with 1 real gap (G2).**

Snocone is already a first-class frontend sharing the unified IR.
The alias names (G1) are cosmetic — they compile to correct `EKind`
values and will be cleaned up in the M-G3 naming pass along with
SNOBOL4's own aliases.

The only substantive gap is G2: the control-flow lowering pass
(`snocone_cf_compile`) is gated on `asm_mode` in `main.c`. JVM and
.NET targets receive expression-only IR, blocking `if`/`while`/
`procedure` for those backends.

Fix milestone **M-G5-LOWER-SNOCONE-FIX**: remove the `asm_mode` gate;
call `snocone_cf_compile` for all backends (or promote the cf-pass to
be the single entry point for all modes). Verify 10/10 corpus still
passes after change.

---
*IR_LOWER_SNOCONE.md — authored G-9 s14. Do not add content beyond this line.*
