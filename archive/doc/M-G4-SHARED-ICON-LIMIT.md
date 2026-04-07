# M-G4-SHARED-ICON-LIMIT — Audit: E_LIMIT (ICN_LIMIT)

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — storage mechanism diverges (frame slots vs static fields)

---

## Logical structure — identical in both backends

Both implementations share the exact same Byrd-box wiring:

```
α → N.α (eval bound once)
N.γ → [store max, reset count=0] → E.α
E.γ → [check count < max; count++] → ports.γ  else → E.β (exhaust)
E.β → [check count < max] → E.β  else → ports.ω
E.ω → ports.ω
β → [check count < max] → E.β  else → ports.ω
```

This is the cleanest alignment seen in Phase 4 so far. The logical structure is
*isomorphic* across backends. What differs is exclusively the storage mechanism
for `count` and `max`.

---

## x64 (`emit_limit`, ~line 1997)

**Storage:** two frame slots allocated via `locals_alloc_tmp()`:
- `lim_slot` — the limit value (N), popped from hardware stack into `[rbp+offset]`.
- `cnt_slot` — the remaining count, initialized from `lim_slot`, decremented on
  each yield.

**Counter direction:** *count down* — initialized to N, decremented on each yield.
Exhausted when count < 0 (`dec [rbp+offset]; jl ports.ω`).

**β check:** `cmp [rbp+offset], 0; jl ports.ω` (check cnt_slot ≥ 0 before resuming E.β).

**Value pass-through:** value stays on hardware stack through the `got_val` relay.
No intermediate storage needed — x64 can leave the value on stack while doing the
counter check (`dec [rbp+cnt_slot]` does not touch rax/stack top).

---

## JVM (`ij_emit_limit`, ~line 5727)

**Storage:** two static long fields per site:
- `icn_N_limit_count` — values yielded so far (count up from 0).
- `icn_N_limit_max` — N value stored once at α.

**Counter direction:** *count up* — starts at 0, incremented on each yield.
Exhausted when `count >= max` (JVM `lcmp; ifge`).

**Additional field:** `icn_N_limit_val` — temporary storage for the yielded value
while doing the counter check. Required because JVM cannot leave a long on the
operand stack while loading two other longs for comparison (operand stack is
type-checked; three longs simultaneously = depth 6, which is legal but the
surrounding `e_gamma` label must have a consistent stack depth at branch targets).
x64 avoids this because `[rbp+cnt_slot]` is memory-to-memory — the hardware
stack is not touched during the counter decrement.

**β path:** same logic as x64 (check count < max, then resume E.β) but uses
`lcmp; ifge` instead of `cmp/jl`.

---

## Divergence summary

| Axis | x64 | JVM |
|------|-----|-----|
| Counter storage | rbp-relative frame slots (alloc'd per-proc) | Static long fields (per-site) |
| Count direction | Count down (init=N, decrement) | Count up (init=0, compare to max) |
| Value hold during check | Hardware stack (no extra storage) | Temp static field (`limit_val`) |
| Exhaustion test | `dec [rbp+slot]; jl ω` | `lcmp; ifge ω` |
| Frame slot lifetime | Proc-scoped (alloc'd from frame locals pool) | Global (static — lives for program duration) |

The logical Byrd-box wiring is the same. The implementation is backend-specific
enough that a shared `emit_wiring_LIMIT(child_emit_fn_t, bound_emit_fn_t)` callback
would need to carry the storage mechanism as a parameter, defeating the purpose.

---

## WASM guidance

WASM has locals (analogous to x64 frame slots) rather than static fields.
Use the x64 approach: two i64 locals for count and max. The count-down direction
(`local.set $cnt max; ...; i64.sub $cnt 1; local.tee $cnt; i64.const 0; i64.lt_s; br_if $ω`)
is marginally simpler than count-up in WASM's br_table structure.

---

## Decision

Wiring stays in-situ per backend. No `ir_emit_common.c` entry for ICN_LIMIT.
Note: this is the closest Phase 4 has come to a common wiring — if a future
`ir_emit_common.c` abstraction layer is introduced (post-Phase 7), LIMIT would
be the first candidate, with storage mechanism passed as a backend-specific callback.

