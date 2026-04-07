# M-G4-SHARED-PROLOG-CUT — Audit: E_CUT Prolog wiring

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — cut mechanism differs (BSS byte flag vs cs-local seal)

---

## Shared logical intent

Both backends implement cut as: "seal β — after cut, failures skip remaining
clauses and go directly to ω (caller failure)."

---

## x64 (emit_x64_prolog.c, ~line 450)

```nasm
mov  byte [rbp - 17], 1    ; _cut = 1
; redirect next_clause label to ω_lbl
snprintf(next_clause, ..., "%s", ω_lbl);
```

**Mechanism:** Sets a byte flag in the frame at `[rbp-17]`. At emit time,
the `next_clause` variable (a string holding the next label to jump to on
failure) is overwritten with `ω_lbl`. All subsequent clause-failure paths
in the loop now jump directly to ω rather than the next clause.

**Key:** This is a compile-time redirect — the cut changes which label
subsequent failure paths branch to within the emitted clause body.
No runtime check of the `_cut` flag is needed for the basic case.

---

## JVM (emit_jvm_prolog.c, ~line 4248)

```jasmin
ldc  cut_cs_seal          ; base[nclauses] sentinel
istore cs_local_for_cut   ; write into cs JVM local
goto lbl_cutγ             ; jump to cutγ label (returns sentinel)
```

**Mechanism:** Stores `base[nclauses]` (a sentinel beyond all clause indices)
into the `cs` JVM local variable. When the predicate later returns and
the caller retries, it will dispatch on `cs` and find no matching clause
(sentinel > all base[ci]), so the predicate appears exhausted.

`lbl_cutγ` is a label that performs a return of the sentinel Object[] — it
bypasses the normal `lbl_γ` (success return) to signal "cut committed."

**Key:** This is a runtime mechanism — the sentinel is stored in the cs local
and the predicate method returns immediately with a "cut" return value.
The cut effect is carried across the call boundary in the cs integer.

---

## Divergence axes

| Axis | x64 | JVM |
|------|-----|-----|
| Mechanism | Compile-time label redirect (`next_clause` string reassigned) | Runtime cs-local sentinel (`istore cs_local`) |
| State carrier | BSS byte `[rbp-17]` = `_cut` flag | JVM local int `cs` overwritten with `base[nclauses]` |
| Cross-boundary | No — cut stays within current activation frame | Yes — sentinel returned through call stack |
| Implementation site | Inline within body-goal emit loop | Inline within goal-emit dispatch (`pj_emit_goal`) |

Both are single-instruction cuts at their respective abstraction levels,
but the mechanism is qualitatively different and neither can be abstracted
into a shared callback.

## Decision

Wiring stays in-situ per backend. No `ir_emit_common.c` entry for E_CUT.

