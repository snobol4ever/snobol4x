# IR_LOWER_REBUS.md — Phase 5 audit: Rebus frontend lower-to-IR

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Milestone: M-G5-LOWER-REBUS-AUDIT*

## Executive summary

**Rebus has no lowering pass to the unified IR. It is currently a
standalone parser + SNOBOL4 source transpiler, disconnected from the
backend pipeline entirely.**

---

## Method

Read `rebus.h`, `rebus_main.c`, `rebus_emit.c`, `src/driver/main.c`.

---

## Current state

```
rebus_parse()  →  RProgram* (own AST: RExpr/RStmt/RDecl, RE_*/RS_* kinds)
                        ↓
              rebus_emit()  → SNOBOL4 source text  (transpiler only)

main.c: no reference to Rebus at all — not integrated into scrip-cc pipeline
```

`rebus_emit.c` is a source-to-source transpiler: it walks `RProgram` and
writes `.sno` text. There is no path from Rebus into `asm_emit`,
`jvm_emit`, `net_emit`, or any `EKind` IR consumer.

---

## Language character: 50% Icon, 50% SNOBOL4

Per Griswold TR 84-9 and the project design:

- **L-component (control flow) — Icon pool:**
  `if`/`else`, `while`, `until`, `repeat`, `for`/`from`/`to`/`by`,
  `case`, `exit`, `next`, `fail`, `return`, function declarations,
  record declarations, `!E` (generator bang), goal-directed evaluation.

- **P-component (pattern matching) — SNOBOL4 pool:**
  `subject ? pattern` (MATCH), `subject ? pattern <- repl` (REPLACE),
  `.var` conditional capture, `$var` immediate capture,
  `@var` cursor capture, `*var` deferred pattern ref,
  `~pat` optional pattern, `&` pattern concatenation, `|` alternation.

---

## RE_* → EKind mapping

### SNOBOL4-pool nodes (map to SNOBOL4 EKind)

| RE_* kind | EKind | Notes |
|---|---|---|
| `RE_STR` | `E_QLIT` | string literal |
| `RE_INT` | `E_ILIT` | integer literal |
| `RE_REAL` | `E_FLIT` | real literal |
| `RE_NULL` | `E_NUL` | empty/null |
| `RE_VAR` | `E_VAR` | variable reference |
| `RE_KEYWORD` | `E_KW` | `&ident` |
| `RE_NEG` | `E_NEG` | unary `-` |
| `RE_POS` | `E_POS` | unary `+` (identity) |
| `RE_NOT` | `E_FNC("DIFFER",1)` | `\` — SNOBOL4 DIFFER |
| `RE_VALUE` | `E_FNC("IDENT",1)` | `/` — SNOBOL4 IDENT |
| `RE_ADD` | `E_ADD` | |
| `RE_SUB` | `E_SUB` | |
| `RE_MUL` | `E_MPY` | |
| `RE_DIV` | `E_DIV` | |
| `RE_MOD` | `E_FNC("REMDR",2)` | SNOBOL4 REMDR built-in |
| `RE_POW` | `E_POW` | |
| `RE_STRCAT` | `E_CONCAT` | `\|\|` string concat |
| `RE_PATCAT` | `E_CONCAT` | `&` pattern concat (= string concat in unified IR) |
| `RE_ALT` | `E_ALT` | `\|` pattern alternation |
| `RE_EQ` | `E_FNC("EQ",2)` | numeric comparison — SNOBOL4 built-in style |
| `RE_NE` | `E_FNC("NE",2)` | |
| `RE_LT` | `E_FNC("LT",2)` | |
| `RE_LE` | `E_FNC("LE",2)` | |
| `RE_GT` | `E_FNC("GT",2)` | |
| `RE_GE` | `E_FNC("GE",2)` | |
| `RE_SEQ` | `E_FNC("IDENT",2)` | string equal |
| `RE_SNE` | `E_FNC("DIFFER",2)` | string not-equal |
| `RE_SLT` | `E_FNC("LLT",2)` | |
| `RE_SLE` | `E_FNC("LLE",2)` | |
| `RE_SGT` | `E_FNC("LGT",2)` | |
| `RE_SGE` | `E_FNC("LGE",2)` | |
| `RE_ASSIGN` | `E_ASSIGN` | `:=` |
| `RE_EXCHANGE` | `E_FNC("EXCHG",2)` | `:=:` swap — no direct EKind; use E_FNC |
| `RE_ADDASSIGN` | `E_ASSIGN(E_ADD(...))` | augmented — synthesize |
| `RE_SUBASSIGN` | `E_ASSIGN(E_SUB(...))` | augmented |
| `RE_CATASSIGN` | `E_ASSIGN(E_CONCAT(...))` | augmented |
| `RE_CALL` | `E_FNC` | function call |
| `RE_SUB_IDX` | `E_IDX` | `a[i]` subscript |
| `RE_RANGE` | `E_IDX` (section child) | `i+:n` substring range |
| `RE_COND` | `E_CAPT_COND` | `.var` conditional capture |
| `RE_IMM` | `E_CAPT_IMM` | `$var` immediate capture |
| `RE_CURSOR` | `E_CAPT_CUR` | `@var` cursor capture |
| `RE_DEREF` | `E_INDR` | `*var` deferred reference |

### Icon-pool nodes (map to Icon EKind or shared)

| RE_* kind | EKind | Notes |
|---|---|---|
| `RE_BANG` | `E_ITER` | `!E` generator (Icon `E_ITER`, was `E_BANG`) |
| `RE_PATOPT` | `E_ARBNO` | `~pat` optional — zero-or-one, maps to `E_ARBNO` |
| `RE_AUG` | augop synthesis | generalized augmented form |

### Statement kinds → IR

| RS_* kind | IR approach | EKind / mechanism |
|---|---|---|
| `RS_EXPR` | expression stmt | `STMT_t` with subject field |
| `RS_IF` | label/goto | SNOBOL4 goto model in STMT_t |
| `RS_UNLESS` | label/goto | same |
| `RS_WHILE` | label/goto loop | same |
| `RS_UNTIL` | label/goto loop | same |
| `RS_REPEAT` | label/goto loop | same |
| `RS_FOR` | label/goto + counter | same |
| `RS_CASE` | label/goto chain | same |
| `RS_RETURN` | `E_FNC("RETURN",...)` | SNOBOL4 RETURN convention |
| `RS_FAIL` | `E_FNC("FRETURN",0)` | SNOBOL4 FRETURN |
| `RS_EXIT` | goto end label | SNOBOL4 END label |
| `RS_NEXT` | goto loop-top label | |
| `RS_MATCH` | `STMT_t` subject+pattern | SNOBOL4 pattern match stmt |
| `RS_REPLACE` | `STMT_t` subject+pattern+replacement | |
| `RS_REPLN` | `STMT_t` subject+pattern, empty repl | |
| `RS_COMPOUND` | sequential STMT_t chain | |

---

## Gap table

| # | Gap | Severity | Notes |
|---|---|---|---|
| G1 | No `rebus_lower.c` — no lowering to `EKind` IR | **Architecture** | Root gap: `RProgram` AST is never converted to `EXPR_t`/`STMT_t` |
| G2 | Not integrated into `main.c` pipeline | **Architecture** | `scrip-cc` has no `-reb` flag; no `rebus_parse()` call in driver |
| G3 | `rebus_emit.c` is source-to-source only | High | SNOBOL4 text output, not IR — useful as oracle but not a backend path |
| G4 | `RE_EXCHANGE` (`:=:`) has no direct EKind | Low | Map to `E_FNC("EXCHG",2)` or add `E_SWAP` to the pool if reorg scope allows |
| G5 | `RE_PATOPT` (`~pat`) → `E_ARBNO` is approximate | Low | `~pat` = zero-or-one; `E_ARBNO` = zero-or-more. May need `E_OPT` or emit `E_ALT(pat, E_NUL)` |

---

## Fix milestone: M-G5-LOWER-REBUS-FIX

Two deliverables:

**1. `rebus_lower.c`** — walk `RProgram`, produce `Program*` (`EXPR_t`/`STMT_t`)
using the mapping table above. `RE_*` expression kinds map directly to
`EKind` values. `RS_*` control-flow kinds lower to SNOBOL4-style
label/goto `STMT_t` chains (same approach as `snocone_cf.c`).

**2. `main.c` integration** — add `-reb` flag and `file_reb` branch:
```c
if (file_reb) {
    RProgram *rp = rebus_parse(in, infile);
    prog = rebus_lower(rp);
    // then falls through to asm_emit / jvm_emit / net_emit
}
```

**Scope:** Post-reorg (after M-G7-UNFREEZE). Current 3/3 corpus PASS
uses the transpiler path (`rebus_emit` → SNOBOL4 text); that path is
unaffected.

**Prerequisite for M-G6-REBUS-JVM / M-G6-REBUS-NET.**

---

## Conclusion

**M-G5-LOWER-REBUS-AUDIT: COMPLETE — 2 architectural gaps + 3 derivative gaps.**

The `RE_*` → `EKind` mapping is fully specified above and is
straightforward — Rebus draws naturally from both pools without
inventing new node kinds (except possibly `E_SWAP` for `:=:` and
`E_OPT` for `~pat`, both of which are worth adding to the common pool).

---
*IR_LOWER_REBUS.md — authored G-9 s14. Do not add content beyond this line.*
