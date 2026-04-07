# M-G4-SHARED-PROLOG-TRAIL — Audit: E_TRAIL_MARK / E_TRAIL_UNWIND

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — trail data structure is ABI-specific

---

## x64

**Trail struct:** C `Trail` — BSS-allocated 32-byte struct at `[rel pl_trail]`:
`{ int top, cap; Term **stack; }` (or similar). Pointer passed as rdi.

**trail_mark_fn:** `call trail_mark_fn` with `rdi = &pl_trail` → returns `eax` = current top.
Mark stored in `[rbp-8]` (clause trail mark) and `[rbp-UCALL_MARK_OFFSET(n)]` (per-ucall).

**trail_unwind:** `call trail_unwind` with `rdi = &pl_trail, esi = mark` → unbinds all
`Term*` variables pushed since mark by resetting their `TT_VAR` binding to unbound.

**Variable binding model:** `Term*` cells — variables are `TT_VAR` structs; binding
writes a reference into the struct's value field; trail records the `Term*` to reset.

---

## JVM

**Trail struct:** `ArrayList<Object[]>` static field `pj_trail` in the generated class.

**pj_trail_mark:** `invokestatic pj_trail_mark()I` → returns `pj_trail.size()` as int.
Mark stored in a JVM local int variable (local arity+1 in predicate method).

**pj_trail_unwind:** `invokestatic pj_trail_unwind(I)V` → pops entries from ArrayList
until `size() <= mark`; for each entry (an `Object[]` variable cell): sets `cell[0]="var"`
and `cell[1]=null` to restore unbound state.

**Variable binding model:** `Object[]` cells — variables are 2-element arrays;
`cell[0]` = tag string `"var"` when unbound; `cell[1]` = binding when bound.
Trail records the `Object[]` cell to restore.

---

## Divergence axes

| Axis | x64 | JVM |
|------|-----|-----|
| Trail container | C struct (BSS, pointer-based) | `ArrayList<Object[]>` (JVM static field) |
| Mark type | `int` (stack top index) | `int` (ArrayList size) — same semantics |
| Mark call ABI | `call trail_mark_fn; rdi=&trail` → eax | `invokestatic pj_trail_mark()I` |
| Unwind call ABI | `call trail_unwind; rdi=&trail, esi=mark` | `invokestatic pj_trail_unwind(I)V` |
| Variable cell type | `Term*` (C pointer to tagged struct) | `Object[]` (2-element JVM array) |
| Unbind operation | Reset `Term.kind = TT_VAR, Term.ref = NULL` | Set `cell[0]="var", cell[1]=null` |
| Mark storage site | `[rbp-8]` (frame slot) | JVM local int (local variable table) |

The *mark-and-unwind* concept is identical. The ABI is backend-specific and cannot
be abstracted without a C/JVM interop layer that doesn't exist.

## Shared concept (for WASM spec)

```
trail_mark()  → opaque integer (current trail depth)
trail_push(var_ref)  → record variable reference for undo
trail_unwind(mark)  → for each entry since mark: set var_ref to unbound
```

WASM: use a linear-memory array of i32 indices into the variable table.
`trail_mark` = load a global trail_top counter.
`trail_unwind(mark)` = while trail_top > mark: load var_idx = trail[--trail_top]; clear var_table[var_idx].

## Decision

Wiring stays in-situ per backend. No `ir_emit_common.c` entry for E_TRAIL_*.

## Phase 4 complete

This is the final Phase 4 milestone. All nine node-kind rows are audited:
- M-G4-SHARED-CONC-FOLD ✅ extracted (ir_nary_right_fold in ir_emit_common.c)
- M-G4-SHARED-ICON-TO through M-G4-SHARED-PROLOG-TRAIL: all NOT extracted,
  with docs explaining divergence + convergence analysis in M-G4-CONVERGENCE-ANALYSIS.md
- BACKLOG-BANG-X64 resolved this session (ICN_BANG/MATCH now implemented in x64)

Next: Phase 5 — M-G5-LOWER-*-AUDIT (frontend lower-to-IR audits, one per frontend).
