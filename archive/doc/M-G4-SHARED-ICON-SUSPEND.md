# M-G4-SHARED-ICON-SUSPEND — Audit: E_SUSPEND Icon generator wiring

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — resume dispatch mechanism fundamentally differs

---

## Divergence axes

### 1. Resume dispatch (most fundamental)

**x64:** A single BSS qword `icn_suspend_resume` holds the resume address.
`β: jmp [rel icn_suspend_resume]` — an indirect jump through a pointer slot.
Only one suspend can be active at a time (single-active-generator design).

**JVM:** Each suspend site receives a unique integer ID (`susp_id`, 1-based counter).
`icn_suspend_id` static field is set to `susp_id` at yield time.
The proc's β entry uses `tableswitch` to dispatch to `resume_here_N` for each
suspend site. Multiple suspend sites per procedure are supported.
`β:` label simply jumps to `resume_here`; the actual dispatch lives in the proc
β prologue (emitted separately in the proc scaffold).

This is a qualitatively different coroutine mechanism — pointer-to-label vs.
integer-keyed switch table. No common callback signature can bridge this.

### 2. Value yield mechanism

**x64:** Pops value from hardware stack into `icn_retval` (BSS qword). Sets
`icn_suspended` byte, saves `rbp` in `icn_suspend_rbp`. Jumps to
`cur_suspend_ret_label` (a bare `ret` — frame stays alive on the call stack).

**JVM:** Calls `ij_put_long("icn_retval")` (static field store). Sets
`icn_suspended` byte, stores `susp_id` in `icn_suspend_id` static field.
Jumps to `ij_sret_label` (a bare `return` from the JVM method — frame is
reclaimed by the JVM, state persisted only in static fields).

x64 keeps the activation frame alive (suspend = caller saves rbp, resumes into
same frame). JVM tears down the frame and reconstructs via tableswitch.

### 3. Body drain

**x64:** Body's γ and ω both route to `ports.γ` (continue) — no value drain
needed because hardware stack is balanced manually.

**JVM:** Body success leaves a long on the JVM operand stack. A `body_done`
drain label (`pop2`) is required before jumping back to the loop top, because
other paths arrive at `ports.ω` (loop top) with an empty stack. StackMapTable
requires consistent frame at branch targets.

---

## Logical Byrd-box structure (shared spec for WASM implementors)

```
α → val_expr.α
val_expr.γ → after_val:
  store value in icn_retval
  mark icn_suspended = 1
  store resume label / ID
  yield to caller (cur_suspend_ret / sret_label)
β → [resume dispatch] → resume_here:
  if body: → body.α
    body.γ → [drain] → E.β  (advance value expression)
    body.ω → E.β
  else: → ports.γ
val_expr.ω → cur_fail_label / ij_fail_label
```

The yield point and resume are the structurally shared concepts.
The *mechanism* for storing and restoring that point differs per backend.

---

## Decision

Wiring stays in-situ per backend. No `ir_emit_common.c` entry for E_SUSPEND.
WASM implementor: use the Byrd-box spec above; choose a mechanism appropriate
for the WASM execution model (likely a continuation integer + br_table, closer
to the JVM tableswitch approach than x64's pointer slot).

