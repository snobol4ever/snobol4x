# M-G4-SHARED-ARBNO — Node Kind Extractability Audit

**Date:** 2026-03-29 · **Session:** G-9 s11 · **Author:** Claude Sonnet 4.6

## Decision: NOT extracted — backends diverge

Same decision as M-G4-SHARED-OR and M-G4-SHARED-SEQ. Wiring stays in-situ per backend.

---

## Evidence

### IR representation
`E_ARBNO` is not dispatched via `case E_ARBNO:` in any emitter.
All three backends dispatch it as `E_FNC` with `sval == "ARBNO"` and `nchildren == 1`.
The child is the sub-pattern to repeat zero-or-more times.

### x64 (`emit_x64.c` → `emit_arbno()`, line 940)

**Wiring:** Full SIL-faithful Byrd box (oracle `arbno_match.s`, v311.sil ARBN/EARB/ARBF):
- α: save cursor onto a `.bss` stack (`arb%d_stack`, `resq 64`); `depth` counter in `.bss`; jump to γ (zero reps = immediate success)
- β: pop cursor; try child_α; child_γ → zero-advance guard; push new cursor; goto γ; child_ω → ω
- Uses NASM macros `ARBNO_α`, `ARBNO_β`, `ARBNO_α1`, `ARBNO_β1`
- **Unique mechanism:** static `.bss` stack of 64 slots (`resq 64`) registered via `extra_slots[]` side channel — not `var_register()`
- Parameters: `cursor`, `subj`, `subj_len`, `depth` (recursion guard)
- Recursive: calls `emit_pat_node(child, ...)` with `depth+1`

### JVM (`emit_jvm.c`, line 1771)

**Wiring:** Greedy iterative loop — NOT full Byrd box backtracking:
- α: save cursor in JVM local int `loc_save`; loop: try child; child_ok → zero-advance guard (`if_icmpeq done`); continue loop; child_fail → restore cursor; fall to γ
- No β port — ARBNO in JVM always succeeds; β wiring not implemented
- **Unique mechanism:** JVM local variable (`loc_save = (*p_cap_local)++`) allocated from JVM local frame counter
- No recursive depth tracking
- Calls `jvm_emit_pat_node(child, ...)` — different callback signature than x64

### .NET (`emit_net.c`, line 1261)

**Wiring:** Structurally identical to JVM (greedy iterative loop), different instruction set:
- α: `ldloc`/`stloc` for save; loop label; `beq done` for zero-advance guard; `br loop`; child_fail → restore; `br gamma`
- No β port — same limitation as JVM
- **Unique mechanism:** CIL local int `loc_save = (*p_next_int)++` from CIL local counter
- Calls `net_emit_pat_node(child, ...)` — different callback signature than x64 and JVM

---

## Divergence Summary

| Dimension | x64 | JVM | .NET |
|-----------|-----|-----|------|
| Wiring model | Full Byrd box (α/β/γ/ω) | Greedy loop (α/γ only) | Greedy loop (α/γ only) |
| Cursor save mechanism | `.bss` stack, `resq 64`, `extra_slots[]` | JVM local int (`p_cap_local`) | CIL local int (`p_next_int`) |
| β port | ✅ implemented | ❌ not implemented | ❌ not implemented |
| Zero-advance guard | macro `ARBNO_α1` | `if_icmpeq` | `beq` |
| Depth tracking | `depth+1` recursion guard | none | none |
| Child emit callback | `emit_pat_node(child, cα, cβ, child_ok, child_ω, cursor, subj, subj_len, depth+1)` | `jvm_emit_pat_node(child, lbl_cok, lbl_cfail, loc_subj, loc_cursor, loc_len, p_cap_local, out, classname)` | `net_emit_pat_node(child, lbl_cok, lbl_cfail, loc_subj, loc_cursor, loc_len, p_next_int, p_next_str)` |

**Three independent divergence axes** — same as OR and SEQ. Extraction would require:
1. Unified cursor-save ABI (impossible: x64 uses `.bss`/NASM macros, JVM/NET use typed local slots)
2. Unified β-port model (JVM/NET don't implement it)
3. Unified child-emit callback signature (all three differ)

**Verdict: NOT extracted. Wiring stays in-situ per backend.**

---

## Icon / Prolog ARBNO

Neither `emit_x64_icon.c` nor `emit_jvm_icon.c` nor any Prolog emitter references ARBNO.
Icon uses `E_ARBNO` IR node (from `ARCH-reorg-design.md` lowering table: `every/while/repeat → E_ARBNO(E_SEQ(...))`),
but the Icon x64 and JVM emitters have not yet wired `E_ARBNO` dispatch — this is a Phase 5/6 gap,
not in scope for M-G4.

