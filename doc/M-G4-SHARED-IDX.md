# M-G4-SHARED-IDX — Node Kind Extractability Audit

**Date:** 2026-03-29 · **Session:** G-9 s11 · **Author:** Claude Sonnet 4.6
**Node:** `E_IDX` (absorbs `E_ARY` via compat alias; named array/table/record subscript)

## Decision: NOT extracted — ABI, key-building, and dispatch mechanisms all diverge

---

## Evidence

All three backends have `case E_IDX:` in their expr-emit switch.

### x64 (`emit_x64.c`, line 3508)

**Model:** SysV AMD64 calling convention, DESCR_t pairs in register pairs.
- 1D: evaluate array → push `[rbp-24/32]` onto C stack; evaluate key → `mov rdx:rcx`; pop arr into `rdi:rsi`; `call stmt_aref` → result in `rax:rdx`
- 2D: same pattern but 3-step push/pop loading all 3 args into `rdi:rsi`, `rdx:rcx`, `r8:r9`; `call stmt_aref2`
- Result stored at `[rbp-16/8]` or `[rbp-32/24]` depending on `rbp_off`
- Uses `emit_expr(child, rbp_off)` — stack-slot based child emit

### JVM (`emit_jvm.c`, line 1317)

**Model:** String-based array via `sno_array_get(arr, key)` invokestatic.
- Named path (`sval` set): `ldc name` → `sno_indr_get(name)` invokestatic to get array object; then build key string (1D: emit child[0]; 2D: StringBuilder concat child[0]+","+child[1])
- Postfix path (`sval` NULL): `jvm_emit_expr(child[0])` for array; then key from child[1]/[2]
- Final: `sno_array_get(arr, key)` invokestatic — class-qualified descriptor built at emit time
- Uses `jvm_emit_expr(child)` — stack-based child emit; `jvm_need_array_helpers = 1`

### .NET (`emit_net.c`, line 906)

**Model:** String-based array via `net_array_get(arr, key)` call on class.
- Named path (`sval` set): `ldsfld string ClassName::fieldname`; emit subscript child[0] (or `ldstr "1"`); `call net_array_get(string, string)`
- Postfix path (`sval` NULL): emit child[0] (array), emit child[1] (key); `call net_array_get(string, string)` — on the class itself
- Uses `net_emit_expr(child)`, `net_field_name()`, `net_ldstr()`, `net_need_array_helpers = 1`

---

## Divergence Summary

| Dimension | x64 | JVM | .NET |
|-----------|-----|-----|------|
| Array lookup | `stmt_aref(arr, key)` C call via reg ABI | `sno_array_get(arr, key)` invokestatic | `net_array_get(arr, key)` static call |
| Array resolution | Operand is DESCR_t pair (type+ptr) | `sno_indr_get(name)` for named arrays | `ldsfld ClassName::field` for named arrays |
| 2D key | Push/pop 3 args via C stack, `stmt_aref2` | StringBuilder concat child[0]+","+child[1] | Not shown (child[1] only) |
| Named vs postfix | Both paths use same `stmt_aref` | Separate paths with different key-build | Separate paths with `net_field_name` |
| Child emit call | `emit_expr(child, rbp_off)` | `jvm_emit_expr(child)` | `net_emit_expr(child)` |
| Result location | `rax:rdx` DESCR_t → `[rbp+off]` | Top of JVM operand stack (String) | Top of CIL evaluation stack (string) |

**Verdict: NOT extracted. Runtime ABI (C calling convention vs JVM invokestatic vs CIL call), array resolution model, key-building, and child-emit signatures all diverge. Wiring stays in-situ per backend.**
