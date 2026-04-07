# M-G4-SHARED-CAPTURE — Node Kind Extractability Audit

**Date:** 2026-03-29 · **Session:** G-9 s11 · **Author:** Claude Sonnet 4.6
**Nodes:** `E_CAPT_COND` (`.` conditional capture, old name `E_NAM`) · `E_CAPT_IMM` (`$` immediate capture, old name `E_DOL`)

## Decision: NOT extracted — backends diverge on cursor-save and variable-store mechanisms

---

## Evidence

### x64 (`emit_x64.c`, `emit_imm()` line 1292, cases E_NAM/E_DOL lines 1482/1493)

Both `E_NAM` and `E_DOL` dispatch to the **same** `emit_imm()` function (x64 treats
them identically at the Byrd-box level).

**Wiring:** Full Byrd box (SIL ENMI, Proebsting §4.3):
- α: save cursor into `.bss` slot `dol_entry_<varname>` via `DOL_SAVE` macro; enter child at `child_α`
- β: transparent — `jmp child_β`
- child_γ → capture: `DOL_CAPTURE` macro spans `subject[entry_cursor..cursor]` into `.bss` buffer `cap_<varname>_N_buf` (resb 256) + `cap_<varname>_N_len`; goto γ
- child_ω → ω (no assignment)

**Unique mechanisms:**
- `.bss` buffers via `CaptureVar` registry (`cap_var_register`), `extra_slots[]` side channel
- `expand_name()` to derive safe label fragment from varname
- Per-variable capture buffer: `cap_<safe>_N_buf` (resb 256), `cap_<safe>_N_len` (resq 1), `dol_entry_<safe>` (resq 1)
- β port fully implemented (transparent backtrack into child)
- Parameters: `cursor`, `subj`, `subj_len`, `depth`

### JVM (`emit_jvm.c`, cases E_NAM line 1684, E_DOL line 1729)

Both cases are **structurally identical** (JVM comment says "same as E_NAM for J4").

**Wiring:** No β port; greedy capture only:
- Save cursor: `iload loc_cursor; istore loc_before` (JVM local int, `(*p_cap_local)++`)
- Child: emit child with `lbl_inner_ok` / `omega` — no β label passed
- On child success: `String.substring(cursor_before, cursor)` → `sno_var_put(name, substring)` via `invokestatic` to classname-qualified method

**Unique mechanisms:**
- JVM local int slot (`p_cap_local` counter)
- `substring(int, int)` via JVM `invokevirtual`
- `sno_var_put` invokestatic — class-qualified descriptor built at emit time
- varname escaped inline into JVM `ldc` string literal
- No β port; no rollback on backtrack

### .NET (`emit_net.c`, cases E_NAM line 1217, E_DOL line 1237)

Both cases are structurally identical.

**Wiring:** No β port; greedy capture only:
- Save cursor: `ldloc/stloc` for CIL local int (`(*p_next_int)++`)
- Child: `net_emit_pat_node(left, lbl_ok, omega, ...)` — no β label
- On child success: `String::Substring(int32, int32)` → `stsfld` into static class field (or `Console::WriteLine` if output variable)

**Unique mechanisms:**
- CIL local int (`p_next_int` counter), separate from string locals (`p_next_str`)
- `net_field_name()` to derive CIL field name from varname
- `net_is_output()` check — output variables go to `Console::WriteLine` not a field
- `stsfld string ClassName::fieldname` — class-static field store
- No β port; no rollback

---

## Divergence Summary

| Dimension | x64 | JVM | .NET |
|-----------|-----|-----|------|
| Cursor save | `.bss` slot via `CaptureVar` registry + `extra_slots[]` | JVM local int (`p_cap_local`) | CIL local int (`p_next_int`) |
| β port | ✅ transparent backtrack implemented | ❌ absent | ❌ absent |
| Variable store | `.bss` buffer `cap_<name>_buf` (resb 256) + `cap_<name>_len` (resq 1) | `sno_var_put(name, substring)` invokestatic | `stsfld` to class static field (or Console::WriteLine) |
| Substring extraction | `DOL_CAPTURE` macro (memcpy from subject) | `String.substring(before, cursor)` | `String::Substring(before, cursor-before)` |
| varname handling | `expand_name()` → safe BSS label fragment | Escaped into `ldc` string literal | `net_field_name()` → CIL field name |
| E_NAM vs E_DOL distinction | Same function (`emit_imm`) — no semantic difference at Byrd-box level | Separate cases, identical code | Separate cases, identical code |
| Child callback signature | `emit_pat_node(child, cα, cβ, dol_γ, dol_ω, cursor, subj, subj_len, depth+1)` | `jvm_emit_pat_node(child, lbl_ok, omega, loc_subj, loc_cursor, loc_len, p_cap_local, out, classname)` | `net_emit_pat_node(left, lbl_ok, omega, loc_subj, loc_cursor, loc_len, p_next_int, p_next_str)` |

**Four independent divergence axes.** Extraction would require:
1. Unified cursor-save ABI (impossible across `.bss`, JVM locals, CIL locals)
2. Unified β port model (JVM and .NET don't implement it)
3. Unified variable-store mechanism (BSS buffer vs JVM invokestatic vs CIL stsfld)
4. Unified child-emit callback (all three differ)

**Verdict: NOT extracted. Wiring stays in-situ per backend.**

---

## Note: E_NAM vs E_DOL semantic equivalence

All three backends treat `E_NAM` and `E_DOL` identically at the Byrd-box wiring level.
The SNOBOL4 semantic distinction (conditional `.` commits on statement success vs
immediate `$` commits on pattern match) is not yet differentiated in any backend's
pattern-matching emit path. This is a pre-existing limitation, not introduced by the reorg.
