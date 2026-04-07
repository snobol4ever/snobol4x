# M-G4-SHARED-ASSIGN — Node Kind Extractability Audit

**Date:** 2026-03-29 · **Session:** G-9 s11 · **Author:** Claude Sonnet 4.6
**Node:** `E_ASSIGN` (was `E_ASGN`; compat alias in `scrip-cc.h`)

## Decision: NOT extracted — E_ASSIGN is not dispatched as an IR expr node in any SNOBOL4/Prolog backend; Icon uses a separate ICN_ASSIGN path

---

## Evidence

### SNOBOL4 — all three backends (x64, JVM, .NET)

`E_ASSIGN` is defined in `ir.h` (line 119) and has a compat alias `#define E_ASGN E_ASSIGN`.
**No backend contains `case E_ASSIGN:` in any expr-emit switch.**

SNOBOL4 assignment is a **statement-level** operation, not an IR expression node:
- The parser produces `stmt_t` records with `has_eq=1`, `subject` (lvalue), `replacement` (rvalue)
- The x64 emitter handles this in the statement emitter (`emit_stmt`): fast-path variable assignment via `SNOBOL4_ASSIGN` macro or `SET_CAPTURE` for pattern-captures; no `E_ASSIGN` node ever appears in the expr IR tree
- JVM and .NET emitters similarly handle assignment at the statement dispatch level, not via `jvm_emit_expr` or `net_emit_expr`

`E_ASSIGN` exists in the IR enum to support future frontend lowerings (Icon's `:=`, augmented assignment `x op:= e` which lowers to `E_ASSIGN(x, E_op(x, e))` per the lowering table in `ARCH-reorg-design.md`). It is not yet wired in any backend.

### Icon — x64 (`emit_x64_icon.c`, `emit_assign()` line 308) and JVM (`emit_jvm_icon.c`, `ij_emit_assign()` line 1040)

Icon uses `ICN_ASSIGN` (its own node type, not `E_ASSIGN`) dispatched via `ICN_ASSIGN` in the Icon-specific switch.

**x64 Icon wiring (`emit_assign`):**
- RHS evaluated via `emit_expr(child[1], rhs_ports, ra, rb)` with a `store` γ-label
- α/β ports wired through (`strncpy oa/ob`)
- Store: if RHS is ICN_STR/ICN_CSET, `rdi` already holds pointer (`mov [rbp+slot], rdi`); else `pop rax` then `mov [rbp+slot], rax`
- LHS dispatch: local slot (rbp-relative) or global `icn_gvar_<name>` (.bss)
- Jump to `ports.γ` on success

**JVM Icon wiring (`ij_emit_assign`):**
- Completely separate from x64 — uses JVM-local type inference, static fields (`ij_is_global`), `putstatic`/`istore`/`astore` depending on inferred type
- Pending-dflt registration for table() default tracking
- `ICN_FIELD` lhs detection for record field assignment

**Divergence between the two Icon backends** alone is substantial (store mechanism, type system, global vs local detection). Extraction between x64-Icon and JVM-Icon is not feasible even within Icon.

### Prolog

No Prolog backend has assignment as an IR node. Prolog "assignment" is unification (`E_UNIFY`), handled separately.

---

## Summary

| Backend | E_ASSIGN dispatched? | How assignment is handled |
|---------|---------------------|--------------------------|
| x64 SNOBOL4 | ❌ | Statement-level `has_eq` + `SNOBOL4_ASSIGN` macro |
| JVM SNOBOL4 | ❌ | Statement-level `has_eq` handler |
| NET SNOBOL4 | ❌ | Statement-level `has_eq` handler |
| x64 Icon | ❌ (uses `ICN_ASSIGN`) | `emit_assign()`: rbp-slot or `icn_gvar_*`, type-tagged by RHS |
| JVM Icon | ❌ (uses `ICN_ASSIGN`) | `ij_emit_assign()`: type-inferred, `putstatic`/`istore`/`astore` |
| Prolog | ❌ | Not applicable; unification is `E_UNIFY` |

**Verdict: NOT extracted. E_ASSIGN is not yet wired in any backend as an expr-IR dispatch target. When Icon frontend lower.c is unified (Phase 5) and produces E_ASSIGN nodes from ICN_ASSIGN, each backend will need a `case E_ASSIGN:` — but the store mechanisms diverge too much for a shared implementation.**
