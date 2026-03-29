# M-G4-CONVERGENCE-ANALYSIS — Should the Phase 4 backends converge?

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*
*Triggered by architectural review question: are the "NOT extracted" verdicts correct,
or should one backend change to match the other?*

---

## The question

Phase 4 audited nine node kinds. Eight were "NOT extracted" due to backend divergence.
The right follow-up is: **is each divergence correct/necessary, or is one backend
implementing something suboptimally that should change?**

This doc analyses each divergence axis and recommends convergence where warranted.

---

## Axis 1: Storage mechanism — BSS/frame-slots (x64) vs static fields (JVM)

**What's happening:**
- x64 uses BSS qword slots for cross-label state that persists across the
  generated function's execution (ICN_TO counter, LIMIT counter, etc.)
- JVM uses static fields for the same purpose

**Is this a real constraint or an accident?**

This is a **genuine, forced divergence**. The JVM's StackMapTable verification
requires that at every branch target, the operand stack height is consistent
across all paths that reach that label. This means the JVM *cannot* leave values
on the operand stack across backward-branch targets — they must be drained to
static fields (or local variables).

x64 has no such constraint. Values can sit on the hardware stack across any jump.

**Verdict: divergence is correct. No change needed.**

---

## Axis 2: ICN_TO counter direction — count-down (x64) vs count-up (JVM) for LIMIT

**What's happening:**
- x64 LIMIT initialises counter to N, decrements, fails when < 0
- JVM LIMIT initialises counter to 0, increments, fails when >= max

**Is this a real constraint or an accident?**

This is an **accident of independent implementation**. Both directions are
equally correct. The x64 count-down approach is marginally simpler (one BSS
slot instead of two: no need for separate `max` field — the initial value
stored in `lim_slot` *is* max; cnt_slot counts down to 0). The JVM
count-up approach is more readable and aligns with how `pj_trail_unwind(mark)`
works (mark = count at the start = 0).

**Should they converge?** Yes, but it's cosmetic — no correctness or performance
issue. If x64 LIMIT is ever rewritten (e.g. for Phase 3 naming cleanup or
WASM implementation), prefer count-up to match JVM. Not worth a standalone fix.

**Verdict: divergence is cosmetic. Prefer count-up in new implementations.**

---

## Axis 3: ICN_ALT beta-resume — hardwired-E1 (x64) vs gate+tableswitch (JVM)

**What's happening:**
- x64 ALT: β always drives E1.β regardless of which child last succeeded
- JVM ALT: β uses a gate field + tableswitch to drive the correct Ei.β

**Is this a real constraint or an accident?**

This is a **semantic difference that matters for correctness under `every`**.
The JVM comment explicitly says the x64-style model "loops infinitely in
`every`". Let me verify:

In `every gen do body`: body.ω → gen.β → gen must produce *next* value.
If `gen` is `E1 | E2` and E1 exhausted, gen.β should drive E2.β.
But x64 ALT β always drives E1.β — so when E1 is exhausted (E1.β → E1.ω →
E2.α), ALT re-starts from E1 each time, never advancing to E2's successive
values. This means `every (1|2) do write(x)` could loop on 1 in x64 if E1
generates multiple values.

However — x64's current irgen input for ALT is always "simple alternation"
(each alternative is a one-shot value: integer, string, etc.). For one-shot
children, E1.β immediately goes to E1.ω → E2.α, so "hardwired to E1.β" and
"gated to last-succeeded.β" are equivalent. The x64 comment says "irgen.icn
simple alternation model."

**Should x64 adopt the gate model?**

Yes — but only when x64 irgen produces ALT nodes with multi-shot children
(generators as alternatives). The JVM gate model is strictly more correct and
handles the general case. The x64 simple model is an optimisation for the
common case that happens to be the *only* case the current irgen produces.

**Recommendation:** Document x64 ALT as a known limitation. When x64 irgen
gains generator-as-alternative support (Phase 5/6), replace `emit_alt` with
the gate model. Until then, the simple model is correct for all inputs irgen
currently produces.

**Verdict: x64 model is conditionally correct (for current inputs). JVM model
is the target. x64 should migrate when irgen supports generator alternatives.**

---

## Axis 4: ICN_SUSPEND — pointer-slot coroutine (x64) vs tableswitch coroutine (JVM)

**What's happening:**
- x64: single `icn_suspend_resume` BSS pointer; one active suspend at a time;
  frame kept alive on call stack (bare jmp, not ret)
- JVM: integer suspend-ID + tableswitch; multiple suspend sites per procedure;
  frame reclaimed on yield (method returns, state in static fields)

**Is this a real constraint or an accident?**

Both are valid coroutine implementations. The difference is fundamental:

x64 keeps the activation frame alive — the procedure never returns, the
caller resumes by jumping back in. This is the classic Duff's device /
setjmp style. It is efficient (no re-entry overhead) but limits to one
active suspend per context.

JVM cannot keep frames alive (methods must return; the JVM owns the call stack).
So it must serialise state into static fields and re-enter via tableswitch.
This is the "CPS-transformed" or "state machine" approach.

**Should they converge?**

No — each backend is using the idiomatic coroutine mechanism for its execution
model. Forcing x64 to use the tableswitch model would be perverse (NASM has
no tableswitch; it would require a dispatch table, BSS function pointers, and
indirect calls — exactly what the pointer-slot already is, but more complex).
Forcing JVM to keep frames alive is impossible.

The conceptual model is shared (yield value, record resume point, resume).
The implementation is necessarily backend-specific.

**Verdict: divergence is correct and necessary. Document the shared concept.**

---

## Axis 5: ICN_BANG / ICN_MATCH — stub (x64) vs full implementation (JVM)

**What's happening:**
- x64 ICN_BANG and ICN_MATCH are stubs that always fail
- JVM has full implementations

**Is this correct?**

No — this is a **genuine gap**. The x64 backend is incomplete. Any Icon program
that uses `!str` (character iteration), `!list` (list iteration), or `=pat`
(scan match) will silently fail on x64 when it should produce results.

**Should x64 implement these?**

Yes. The JVM implementation is the reference spec. x64 should implement:

**ICN_BANG (string):**
- BSS slots: `icn_N_bang_str_ptr` (char*), `icn_N_bang_pos` (int64)
- α: eval child → store char* in BSS, reset pos=0, fall to check
- β: resume at check (string already stored)
- check: `pos >= strlen(str)` → ω; else push `icn_rt_char_at(str, pos)` → γ; `pos++`
- Runtime needed: `char* icn_rt_char_at(char *s, int64_t pos)` → 1-char malloc'd string

**ICN_BANG (list):**
- BSS slots: `icn_N_bang_list_ptr`, `icn_N_bang_idx`
- Same structure with `icn_rt_list_get(list, idx)` → DESCR_t

**ICN_MATCH:**
- Calls existing or new `icn_rt_match(char *subj, int64_t pos, char *pat) → int64_t`
- Updates `icn_pos` BSS slot on success
- One-shot (β → ω)

**Verdict: x64 BANG/MATCH are gaps, not valid divergences. Should be implemented.**
This is Phase 5/6 scope (beyond Phase 4's "audit shared wiring" mandate) but
should be tracked as a concrete backlog item.

---

## Axis 6: E_UNIFY / E_CLAUSE / E_CUT / E_TRAIL — ABI divergence (Prolog)

**What's happening:**
- x64 Prolog: C ABI — `Term*`, `Trail*` structs, SysV calling convention
- JVM Prolog: JVM ABI — `Object[]`, static trail `ArrayList`, invokestatic

**Is this correct?**

Yes — this is the ABI of each backend's runtime. The x64 Prolog runtime is
written in C and uses C structs. The JVM Prolog runtime is written in Jasmin
and uses JVM objects. These cannot converge without rewriting one runtime
in terms of the other's type system, which would be wrong.

**Verdict: divergence is correct and necessary. ABIs are backend-specific.**

---

## Summary table

| Node | Divergence type | Verdict | Action |
|------|----------------|---------|--------|
| ICN_TO / TO_BY | Storage (BSS vs static) + integer vs real | Necessary | None; note WASM should use count-up |
| ICN_SUSPEND | Coroutine model (pointer vs tableswitch) | Necessary | None |
| ICN_ALT | β-resume (hardwired vs gate) | x64 is conditionally correct | Migrate x64 when irgen gains generator-alternatives |
| ICN_BANG / MATCH | x64 stub vs JVM full | **Gap — x64 incomplete** | Implement x64 BANG/MATCH (Phase 5/6) |
| ICN_LIMIT | Storage + count direction | Direction cosmetic | Prefer count-up in new impls |
| E_UNIFY | ABI (C vs JVM) | Necessary | None |
| E_CLAUSE/E_CHOICE | ABI (C vs JVM) | Necessary | base[] helper extractable post-Phase-7 |
| E_CUT | Mechanism (flag vs cs-seal) | Necessary | None |
| E_TRAIL | ABI (C struct vs ArrayList) | Necessary | None |

## Concrete action items from this analysis

1. **BACKLOG-BANG-X64:** Implement ICN_BANG (string + list) and ICN_MATCH in
   `emit_x64_icon.c`. Runtime helpers needed in `snobol4_asm.mac` /
   `prolog_runtime.h` or a new `icon_rt_x64.c`. Medium complexity.

2. **BACKLOG-ALT-X64:** When irgen.icn gains generator-as-alternative support,
   replace x64 `emit_alt` with gate+tableswitch model matching JVM.

3. **BACKLOG-LIMIT-DIRECTION:** Prefer count-up in WASM and any future LIMIT
   rewrites. No change to existing x64/JVM implementations.

4. **BACKLOG-BASE-HELPER:** Post-Phase-7, extract `pl_compute_bases()` from
   both Prolog emitters into `ir_emit_common.c`.

