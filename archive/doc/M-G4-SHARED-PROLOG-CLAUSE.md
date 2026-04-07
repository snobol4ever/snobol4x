# M-G4-SHARED-PROLOG-CLAUSE — Audit: E_CLAUSE / E_CHOICE Prolog wiring

*Authored: 2026-03-29, G-9 s14, Claude Sonnet 4.6*

## Verdict: NOT extracted — predicate dispatch mechanism fundamentally differs

---

## Shared logical structure

Both backends implement the same Byrd-box predicate model:

```
CHOICE(pred/arity, clauses[]):
  dispatch on cs (continuation state integer) → clause[i]
  each clause[i]:
    save trail mark
    head unification (each arg)
    body goals (sequential, with retry on failure)
    on success: return cs encoding (next retry point)
    on failure: unwind trail, try next clause
    last clause fails: return -1 / null
```

The `cs` (continuation state, a.k.a. `start`) integer encodes which clause to
try and where within a clause to resume after a body user-call.

---

## x64 (`emit_prolog_choice`, ~line 1578)

**ABI:** emitted as a C-callable function:
```
int pl_NAME_ARITY_r(Term *arg0, ..., Trail *trail, int start)
```
Returns: clause index ≥ 0 on success, -1 on failure.

**Dispatch:** `switch(start)` table — emitted as `cmp/jge` chain over the `base[]`
array computed at emit time. Each base[ci] marks the entry point of clause[ci].

**Frame:** x64 stack frame with fixed slot layout:
- `[rbp-8]`: trail mark (per clause)
- `[rbp-16]`: `_cut` flag (byte)
- `[rbp-17..N]`: UCALL continuation state slots (per user-call in body)
- `[rbp+offset]`: per-clause variable Term* slots

**Head unification:** `emit_pl_term_load(arg)` → rax; `call unify(rdi,rsi,rdx)`.

**Body user-calls:** `call pl_FOO_N_r(arg..., trail, sub_cs)` — recursive C call.
Sub-cs packed into the clause's base range.

**Trail unwind on failure:** `call trail_unwind(&pl_trail, mark)`.

---

## JVM (`pj_emit_choice`, ~line 7503)

**ABI:** emitted as a JVM static method:
```
static Object[] p_NAME_ARITY(Object arg0, ..., int cs)
```
Returns: `Object[]` result array on success, `null` on failure.

**Dispatch:** linear scan from last clause to first over `base[]` array — `if_icmpge`
checking `cs >= base[ci]` → clause ci. Mirrors x64 dispatch but in Jasmin bytecode.

**Frame:** JVM local variables (typed slots):
- locals 0..arity-1: args (Object[])
- local arity: cs (int)
- local arity+1: trail mark (int)
- locals arity+2..N: per-clause variable slots

**Head unification:** `pj_emit_term(arg)` → Object on stack; `invokestatic pj_unify(Object,Object)Z`.

**Body user-calls:** `invokestatic p_FOO_N(args..., sub_cs)` → Object[] result.
Null check → fail path.

**Trail unwind on failure:** `invokestatic pj_trail_unwind(mark_int)`.

---

## Divergence axes

| Axis | x64 | JVM |
|------|-----|-----|
| Predicate ABI | C function `int pl_FOO_N_r(Term*, Trail*, int)` | JVM method `Object[] p_FOO_N(Object[], int)` |
| Term type | `Term*` (C tagged union pointer) | `Object` (boxed JVM reference) |
| Dispatch | `cmp/jge` over base[] in NASM | `if_icmpge` in Jasmin |
| Frame layout | rbp-relative fixed slots (NASM) | JVM local variable table |
| Trail mechanism | Global `pl_trail`, explicit `call trail_unwind` | Static trail in runtime class, `invokestatic pj_trail_unwind` |
| Success return | int (cs / clause index) | Object[] (result array) |
| Failure return | -1 (int) | null (Object[]) |

The dispatch logic (base[] array, cs integer, linear scan) is structurally shared
but the implementation language (NASM assembly vs Jasmin bytecode) makes code
sharing impossible without a new shared C abstraction layer that neither backend
currently uses.

## Decision

Wiring stays in-situ per backend. No `ir_emit_common.c` entry for E_CLAUSE/E_CHOICE.
The base[] computation logic is the most shareable part — a potential future
`pl_compute_bases(EXPR_t *choice, int *base, int nclauses)` helper in
`src/ir/ir_emit_common.c` could serve both backends. Tracked as post-Phase-7 idea.

