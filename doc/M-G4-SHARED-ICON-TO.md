# M-G4-SHARED-ICON-TO — Audit: E_TO / E_TO_BY Icon generator wiring

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — three divergence axes make isomorphic wiring impossible

---

## ICN_TO — audit

### x64 (`emit_x64_icon.c`, `emit_to`, ~line 1273)

**Storage:** two BSS qword slots per node-id (`icon_%d_I`, `icon_%d_bound`),
plus `icon_%d_e1cur` and `icon_%d_e2seen` (int flag).

**Stack discipline:** child expressions push their value onto the x64 hardware stack.
Relay labels pop the value into BSS slots.

**β port:** increments `I` in-place via `inc qword [rel I]`, re-enters `code`.

**Child wiring order:** E2 emitted first (γ→init, ω→e1bf), then E1 (γ→e2a, ω→ports.ω).
Relay: E2 value popped from hardware stack into BSS at `init` label.

**e2seen flag:** tracks whether this is the first E1 success (both E1+E2 on stack)
vs. E2 re-advance (only E2). Flag reset to 0 when E1 advances (e1bf fires).

---

### JVM (`emit_jvm_icon.c`, `ij_emit_to`, ~line 4104)

**Storage:** six static long/int fields per node-id: `I`, `bound`, `e1cur`, `e2seen`,
`e1val`, `e2val`. JVM has no hardware stack accessible across labels → all cross-label
values must go through static fields. Requires two extra relay labels (`e1_relay`,
`e2_relay`) to drain operand stack before jumping to `init`.

**β port:** `lconst_1; ladd` on static field; JVM does not have `inc`.

**Child wiring order:** same logical order as x64 (E2 first, then E1).

**e2seen flag:** int static field; same semantics as x64.

**Key divergence:** JVM requires explicit relay labels to transfer stack values
to static fields before any label that is a backward-jump target (StackMapTable
verification requires empty stack at branch targets). x64 can leave values on
the hardware stack freely.

---

## ICN_TO_BY — audit

### x64 (`emit_to_by`, ~line 1356)

**Storage:** three BSS qword slots: `I`, `bound`, `step`.

**Stack discipline:** three relay labels (`start_relay`, `bound_relay`, `step_relay`)
pop values from hardware stack into BSS slots.

**Step-sign check:** x64 checks step > 0 / step < 0 via `cmp [rel step], 0; jle/jge`.

**β port:** `add [rel I], rax` where rax = step value loaded from BSS.

**Type handling:** x64 uses integer (qword) arithmetic only. No real/float path.

---

### JVM (`ij_emit_to_by`, ~line 5023)

**Storage:** three static fields: `I_f`, `end_f`, `step_f`. Field descriptor
is `J` (long) or `D` (double) depending on `is_dbl` flag.

**Type handling:** JVM detects if any child is real (`ij_expr_is_real`) and promotes
the entire computation to double. x64 has no such promotion — integer only.
This is a **fundamental semantic difference**: JVM TO_BY handles `1.5 to 5.0 by 0.5`;
x64 currently does not.

**Step-sign check:** JVM uses `lcmp`/`dcmpl`/`dcmpg` bytecodes + `ifgt`/`iflt`.
These map to different JVM instruction sequences than the x64 `cmp + jle/jge` pair.

**β port:** uses TB_ADD() macro which emits `ladd` or `dadd`.

**Forward-only jump comment:** JVM explicitly notes "forward-only jump structure
(no backward branches → no StackMapTable)". x64 has backward branches freely.

---

## Divergence summary

| Axis | x64 | JVM |
|------|-----|-----|
| Cross-label value storage | BSS `.bss` qword slots (1 pop = 1 store) | Static class fields; relay labels required to drain operand stack before branch targets |
| β increment instruction | `inc` / `add [mem], reg` | `ladd` / `dadd` (stack-based add, no in-place) |
| Real/float support (TO_BY) | Integer (qword) only | Promotes to double if any child is real |
| Step-sign check (TO_BY) | `cmp [rel step], 0; jle/jge` | `lcmp + ifgt/iflt` / `dcmpl + ifgt/iflt` |
| StackMapTable constraint | None — backward branches free | All branch targets must have empty stack; relay labels required |

## Conclusion

Same three divergence axes that blocked E_OR and E_ARBNO extraction apply here:

1. **Storage mechanism** — BSS vs. JVM static fields are fundamentally different;
   no shared `emit_wiring_TO(child_emit_fn_t)` callback signature can paper over
   the relay-label requirement.
2. **Type system** — JVM promotes integer to double; x64 does not. Shared wiring
   would need to carry type-dispatch into the common code, coupling backends.
3. **Instruction set** — `inc [mem]` vs. `lconst_1; ladd`; `jle` vs. `lcmp; ifle`.
   Callbacks cannot abstract these away without becoming wrappers that do all the work.

**Decision:** Wiring stays in-situ per backend. No `ir_emit_common.c` entry for
E_TO or E_TO_BY.

## What IS shared

The *logical Byrd-box structure* of TO is identical across backends:

```
α → E1.α
E1.γ → E2.α (with relay: save E1 value)
E2.γ → init (with relay: save E2 value)
init: set I = E1 value; set bound = E2 value; → code
code: if I > bound → β-of-E2; else push I → ports.γ
β: inc I → code
E2.β → E1.β (when E2 exhausts, advance E1)
E1.β → ports.ω
```

This is documented here for Phase 5 (frontend lowering) and future WASM implementors.
The WASM `emit_wasm_icon.c` implementor should use this as the spec.

