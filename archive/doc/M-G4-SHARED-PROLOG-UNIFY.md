# M-G4-SHARED-PROLOG-UNIFY — Audit: E_UNIFY Prolog wiring

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — single-call dispatch; ABI and term representation diverge

---

## x64 (`emit_x64_prolog.c`, ~line 458)

E_UNIFY is handled inline within the clause body loop (not a separate emit function).

```
emit_pl_term_load(child[0]) → rax → [rel pl_tmp]
emit_pl_term_load(child[1]) → rax → rsi
                               rdi = [rel pl_tmp]  (first term)
                               rdx = &pl_trail
call  unify                  ; C function: int unify(Term*, Term*, Trail*)
test  eax, eax               ; 0 = fail
jz    ufail:
  lea  rdi, [rel pl_trail]; mov esi, [rbp-8]; call trail_unwind
  jmp  next_clause
```

**Term representation:** `Term*` — C pointer to tagged-union struct.
**Trail:** global BSS array `pl_trail`, passed as pointer. Trail mark at `[rbp-8]`.
**On failure:** unwind trail to clause mark, jump to `next_clause` (next clause α or ω).
**Trail unwind:** explicit `call trail_unwind(Trail*, mark_int)`.

---

## JVM (`emit_jvm_prolog.c`, ~line 4262)

E_UNIFY is also inline within the goal-emit loop.

```
pj_emit_term(child[0])       ; push Object reference
pj_emit_term(child[1])       ; push Object reference
invokestatic pj_unify(Object,Object)Z
ifeq lbl_ω                   ; false (0) → fail
goto lbl_γ
```

**Term representation:** `Object` — boxed JVM objects (Long, String, ArrayList,
or compound term Object[]). No trail parameter — trail is managed internally by
`pj_unify` via a static trail stack in the runtime class.
**On failure:** `ifeq lbl_ω` jumps to the clause's ω label; JVM runtime handles
trail unwind inside `pj_unify` before returning false, OR the clause ω path
calls `pj_trail_unwind()` at clause boundary.
**Trail unwind:** implicit within `pj_unify()` on failure, or explicit
`invokestatic pj_trail_unwind()` at clause boundaries.

---

## Divergence axes

| Axis | x64 | JVM |
|------|-----|-----|
| Term type | `Term*` (C pointer to tagged struct) | `Object` (boxed JVM reference) |
| Trail parameter | Explicit `Trail*` in rdx | Implicit (global static trail in runtime) |
| Unify call | `call unify` (SysV ABI: rdi, rsi, rdx) | `invokestatic pj_unify(Object,Object)Z` |
| Fail handling | explicit `trail_unwind` + `jmp next_clause` | `ifeq lbl_ω` (runtime may unwind internally) |
| Structure | Inline in clause body loop | Inline in goal-emit dispatch |

Both are already minimal — one runtime call + branch. There is no Byrd-box
wiring to share; E_UNIFY is a direct pass-through to the runtime unifier.
The divergence is entirely in the runtime ABI and term representation.

## Decision

Wiring stays in-situ. No `ir_emit_common.c` entry for E_UNIFY.
The logical structure (load two terms, call runtime unifier, branch on result) is
identical, but it's one line of logic — the extraction overhead exceeds the value.

