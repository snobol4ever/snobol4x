# M-G4-SHARED-ICON-ALT — Audit: E_ALT_GEN (ICN_ALT) Icon wiring

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — β-resume dispatch diverges fundamentally

---

## x64 (`emit_alt`, ~line 820)

Simple n-ary right-fold:

```
α → E1.α
Ei.γ → ports.γ   (all children share the same success port)
Ei.ω → E(i+1).α  (En.ω → ports.ω)
β → E1.β          (always resumes leftmost child)
```

No per-site state needed. x64 comment: "irgen.icn simple alternation model."
This works because x64 hardware stack is balanced at each label and backward
jumps are free.

**β-resume:** hardwired to `E1.β`. This is correct for the simple case
(first child always generates all values before advancing to E2).

---

## JVM (`ij_emit_alt`, ~line 3735)

Cannot use the simple model. JVM comment: "Naïve wiring (BUGGY for β-resume):
β → E1.β ← always restarts from E1, loops infinitely in `every`."

JVM uses the JCON `ir_MoveLabel + ir_IndirectGoto` model:

**Gate field:** `icn_N_alt_gate` (static int, per alt-site). Tracks which Ei
last succeeded (1-based; 0 = α, no prior success).

```
α:    gate=0; goto E1.α
Ei.γ: [relay] gate=i; store value; goto ports.γ
Ei.ω: goto E(i+1).α  (En.ω → ports.ω)
β:    iload gate → tableswitch → E1.β / E2.β / ... / En.β
```

Each child gets a γ-relay label that sets the gate before forwarding to
`ports.γ`. β uses `tableswitch` on the gate value to dispatch to the correct
Ei.β for resumption.

**Value pass-through:** relay labels must preserve the child's value while
setting the gate. Requires a temporary static field (`icn_N_alt_val`) per
site, typed J/D/Object depending on alternative types.

---

## Divergence axes

| Axis | x64 | JVM |
|------|-----|-----|
| β-resume mechanism | Hardwired to E1.β (simple) | Gate field + tableswitch (JCON model) |
| Per-site state | None | `icn_N_alt_gate` int field; `icn_N_alt_val` temp field |
| γ interception | None — children wire directly to ports.γ | Relay labels per child to set gate |
| Value preservation at γ | Hardware stack holds value through relay | Must store to temp field, set gate, reload |
| n-ary structure | Right-fold loop (uniform) | Right-fold loop + relay label array |

The gate mechanism is absent from x64 entirely. Introducing it would require
BSS int slots and indirect-jump machinery that x64 currently — and correctly —
avoids. The x64 simpler model is valid for the current irgen.icn input; the
JVM model is forced by StackMapTable + static-field discipline.

---

## Logical Byrd-box structure (shared spec)

```
α → E1.α
For i in 1..n:
  Ei.γ → [record "Ei succeeded"] → ports.γ  (value on stack/field)
  Ei.ω → E(i+1).α  (last: → ports.ω)
β → [dispatch to Ei.β where i = last-succeeded]
```

The "record + dispatch" step is the divergence point. WASM should use the
integer-keyed br_table model (closer to JVM tableswitch).

---

## Decision

Wiring stays in-situ per backend. No `ir_emit_common.c` entry for ICN_ALT.

