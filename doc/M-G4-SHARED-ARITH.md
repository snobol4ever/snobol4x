# M-G4-SHARED-ARITH — Node Kind Extractability Audit

**Date:** 2026-03-29 · **Session:** G-9 s11 · **Author:** Claude Sonnet 4.6
**Nodes:** `E_ADD` · `E_SUB` · `E_MPY` · `E_DIV` · `E_MOD` (also covers `E_EXPOP`/`E_POW` as examined alongside)

## Decision: NOT extracted — backends use fundamentally different arithmetic models

---

## Evidence

### x64 (`emit_x64.c`, cases E_ADD/SUB/MPY/DIV/EXPOP, lines 3790–3860)

**Model:** NASM macro dispatch with fast-path specialization.
- Operand kinds inspected at emit time (VART, ILIT, QLIT, NULV)
- Fast paths via `CONC2_VV`, `CONC2_VI`, `CONC2_IV`, `CONC2_II`, `CONC2_VS`, `CONC2_SV`, `CONC2_VN` NASM macros when both args are known-type
- Generic path: `sub rsp, 32`; evaluate each child into stack frame via `emit_expr(child, -32)`; `STORE_ARG32`; `APPLY_FN_N fn, 2` — calls runtime function by name (`add`, `sub`, `mul`, `DIVIDE_fn`, `POWER_fn`)
- Result stored via `STORE_RESULT` macro or `mov [rbp+off]`
- `E_MOD` not present in x64 case block (falls through or uses generic `APPLY_FN_N`)

### JVM (`emit_jvm.c`, cases E_ADD/SUB/MPY/DIV/EXPOP, lines 535–650)

**Model:** Double-precision floating-point with integer detection and round-trip conversion.

- `E_DIV`: separate handling — string operands stashed in locals; `sno_is_integer` check; integer path uses `Long.parseLong` + `ldiv`; float path uses `ddiv` with whole-number check; result converted back to SNOBOL4 string via `jvm_l2sno()` or `jvm_d2sno()`
- `E_ADD/SUB/MPY/EXPOP`: both operands converted to double via `jvm_emit_to_double()`; `dadd`/`dsub`/`dmul`/`Math.pow`; whole-number check; result converted back via `jvm_l2sno()` or `jvm_d2sno()`
- Uses fixed scratch JVM locals (`jvm_arith_local_base`, `+2`, `+4`, `+5`) — shared across arith nodes (nested arith not fully supported per comment)
- Class-qualified `sno_is_integer` invokestatic descriptor built at emit time
- `E_MOD`: not in examined case block for JVM (likely via `E_FNC` path or stub)

### .NET (`emit_net.c`, cases E_ADD/SUB/MPY/DIV/EXPOP, lines 876–901)

**Model:** Pure library delegation — one CIL `call` per operation.
- Every op: evaluate both operands via `net_emit_expr`, then `call string [snobol4lib]Snobol4Lib::sno_add(string, string)` (etc.)
- No inline numeric logic — all type coercion and integer/float dispatch inside `Snobol4Lib`
- Cleanest of the three; all complexity hidden in the managed library
- `E_MOD`: not present (likely `E_FNC` path)

---

## Divergence Summary

| Dimension | x64 | JVM | .NET |
|-----------|-----|-----|------|
| Arithmetic model | NASM macro fast-paths + `APPLY_FN_N` runtime | Double conversion + JVM dadd/ldiv with integer check | Library call `Snobol4Lib::sno_add` etc. |
| Type coercion | Runtime C functions (`add`, `mul`, `DIVIDE_fn`) | Inline JVM bytecode (`parseLong`, `d2l`, `l2d`, `dcmpl`) | Hidden in `Snobol4Lib` managed assembly |
| Fast paths | ✅ 7 NASM macro variants (VV, VI, IV, II, VS, SV, VN) | ❌ none | ❌ none |
| Scratch storage | rsp-relative frame slots via `STORE_ARG32` | Fixed JVM locals (`jvm_arith_local_base` + offsets) | None — all on CIL stack |
| Integer/float distinction | Runtime (`add`/`DIVIDE_fn` handles both) | Inline: `sno_is_integer` + branch | Hidden in library |
| Child emit call | `emit_expr(child, rbp_off)` | `jvm_emit_expr(child)` | `net_emit_expr(child)` |
| E_MOD | Not in case block (separate path) | Not in case block | Not in case block |

**Three fundamentally different models** — NASM-macro/runtime-function (x64), double-bytecode/integer-detect (JVM), managed-library-delegation (.NET). No shared logic is possible across these.

**Verdict: NOT extracted. Wiring stays in-situ per backend.**

---

## Note on E_MOD

`E_MOD` is not present in any arithmetic case block examined. In x64 it presumably falls
through to a generic `APPLY_FN_N` path or `E_FNC("mod",...)`. In JVM and .NET it is
likely handled via `E_FNC` dispatch. This should be confirmed and documented separately
if E_MOD test coverage is added.
