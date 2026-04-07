# SIL_NAMES_AUDIT.md — Broader SIL Heritage Naming Analysis

**Produced by:** Claude Sonnet 4.6 (G-7 session, 2026-03-28)
**Milestone:** M-G0-SIL-NAMES
**Source:** CSNOBOL4 2.3.3 (`v311.sil`, `equ.h`, `data.h`, `globals.h`),
`snobol4_asm.mac`, emitter source files, `engine.h`.
**Prerequisite for:** M-G3 (naming law extension if needed), M-G1-IR-HEADER-DEF.

G-7 covered IR node names only (`doc/IR_AUDIT.md`). This document covers the
four remaining naming surfaces: runtime variables in generated code, emitter C
source variable names and struct fields, generated label prefixes, and runtime
library macro/function names.

---

## Area 1 — Runtime Variable Names in Generated Code

These are the symbol names that appear in the `.s` / `.j` / `.il` output files,
visible in object files and debuggers. They must be consistent across all three
active backends (x64 ASM, JVM, .NET).

### 1a. SNOBOL4 Variables

SIL heritage: SNOBOL4 variables are `VARTYP EQU 3` (type code 3 in v311.sil).
The SIL runtime uses `GENVAR` to allocate variable storage; our generated code
uses flat BSS globals.

| What | x64 symbol | JVM field | .NET field | SIL origin |
|------|-----------|-----------|------------|------------|
| User variable `X` | `sno_var_X` | `sno_var_X` | `sno_var_X` | `VARTYP` → `sno_var_` prefix |
| Subject string | `sno_subject` | `sno_subject` | `sno_subject` | SIL `SUBJ`/`SUBJND` → `sno_subject` |
| Cursor position | `sno_cursor` | `sno_cursor` | `sno_cursor` | SIL `ATOP`/`ATP` (`XATP=4`) → `sno_cursor` |
| Subject length | `sno_sublen` | `sno_sublen` | `sno_sublen` | SIL `SUBJLN` → `sno_sublen` |
| `&STLIMIT` keyword | `sno_kw_STLIMIT` | `sno_kw_STLIMIT` | `sno_kw_STLIMIT` | SIL `STLIMIT` → `sno_kw_STLIMIT` |
| `&STCOUNT` keyword | `sno_kw_STCOUNT` | `sno_kw_STCOUNT` | `sno_kw_STCOUNT` | SIL `STCOUNT` → `sno_kw_STCOUNT` |
| `&ANCHOR` keyword | `sno_kw_ANCHOR` | `sno_kw_ANCHOR` | `sno_kw_ANCHOR` | SIL `ANCHOR` → `sno_kw_ANCHOR` |
| `&TRIM` keyword | `sno_kw_TRIM` | `sno_kw_TRIM` | `sno_kw_TRIM` | SIL `TRIM` → `sno_kw_TRIM` |
| `&STNO` keyword | `sno_kw_STNO` | `sno_kw_STNO` | `sno_kw_STNO` | SIL statement number → `sno_kw_STNO` |

**SIL alignment verdict:** ✅ All `sno_` prefixed names are correctly derived.
`SUBJ`/`SUBJND` → `sno_subject`, `ATOP`/`ATP` → `sno_cursor` (cursor position
capture `XATP=4` maps cleanly). Keyword prefix `sno_kw_` is our convention, not
from SIL directly (SIL uses `&NAME` at language level; no C-symbol equivalent
needed there). The convention is internally consistent and should be preserved.

**Naming law extension (Area 1 — SNOBOL4):** No changes needed. Current names
are correct and consistent across all three backends.

---

### 1b. Icon Runtime Variables

| What | x64 symbol | JVM field | .NET field | Origin |
|------|-----------|-----------|------------|--------|
| Failure flag | `icn_failed` | `icn_failed` | `icn_failed` | Icon-specific, no SIL origin |
| Suspended flag | `icn_suspended` | `icn_suspended` | `icn_suspended` | Icon-specific |
| Return value | `icn_retval` | `icn_retval` | `icn_retval` | Icon-specific |
| Global variable `n` | `icn_gvar_N` | `icn_gvar_N` | `icn_gvar_N` | Icon-specific |
| Proc-local var `v` in proc `p` | `icn_pv_p_v` | `icn_pv_p_v` | `icn_pv_p_v` | Icon-specific |
| String literal slot `N` | `icn_str_N` | (JVM constant pool) | (IL string) | Icon-specific |

**Verdict:** ✅ `icn_` prefix is consistent and clearly scoped to Icon. No SIL
heritage applies here. Prefix is the law.

---

### 1c. Prolog Runtime Variables

| What | x64 symbol | JVM field | .NET field | Origin |
|------|-----------|-----------|------------|--------|
| Trail top | `pl_trail_top` | `pl_trail_top` | `pl_trail_top` | Prolog-specific |
| Trail array | `pl_trail` | `pl_trail` | `pl_trail` | Prolog-specific |
| Env/frame base | `pl_env` | (JVM local slot) | (IL local) | Prolog-specific |
| Temporary | `pl_tmp` | (JVM local) | (IL local) | Prolog-specific |
| Runtime init | `pl_rt_init` | `pl_rt_init` | `pl_rt_init` | Prolog-specific |

**Verdict:** ✅ `pl_` prefix is consistent. No SIL heritage applies.

---

## Area 2 — Emitter C Source Variable Names and Struct Fields

These are names inside the emitter `.c` files themselves — local variables,
parameters, struct fields, typedef names.

### 2a. The `EXPR_t` / `EKind` IR struct (`scrip-cc.h`)

```c
typedef struct EXPR_t EXPR_t;
struct EXPR_t {
    EKind    kind;          /* node kind — from unified EKind enum */
    char    *sval;          /* string value: E_QLIT text, E_VART/E_KW/E_FNC name */
    int64_t  ival;          /* integer value: E_ILIT */
    double   fval;          /* float value: E_FLIT */
    EXPR_t **children;      /* child nodes (realloc-grown) */
    int      nchildren;     /* number of children */
    int      id;            /* unique node id — assigned during emit pass */
};
```

**SIL heritage check:**
- `kind` — maps to SIL `xxxTYP` type codes. Clean.
- `sval`/`ival`/`fval` — standard C field names, no SIL conflict.
- `children`/`nchildren` — our convention; SIL uses `LSON`/`RSIB` for tree
  structure (linked-list tree, not array of children). Our array-of-children
  model is a deliberate improvement. Field names are correct for our model.
- `id` — our convention for unique node numbering in emit pass.

**Verdict:** ✅ `EXPR_t` struct field names are correct. No changes needed.

**Old names in `scrip-cc.h` EKind enum still present (pre-reorg):**

| Old | Canonical | Status |
|-----|-----------|--------|
| `E_VART` | `E_VAR` | alias bridge needed in M-G1-IR-HEADER-WIRE |
| `E_NULV` | `E_NUL` | alias bridge |
| `E_STAR` | `E_DEFER` | alias bridge |
| `E_MNS` | `E_NEG` | alias bridge |
| `E_EXPOP` | `E_POW` | alias bridge |
| `E_CONC` | `E_SEQ` | alias bridge |
| `E_OR` | `E_ALT` | alias bridge |
| `E_NAM` | `E_CAPT_COND` | alias bridge |
| `E_DOL` | `E_CAPT_IMM` | alias bridge |
| `E_ATP` | `E_CAPT_CUR` | alias bridge |
| `E_ARY` | `E_IDX` | absorbed — merged |
| `E_ASGN` | `E_ASSIGN` | alias bridge |
| `E_BANG` | `E_ITER` | alias bridge |
| `E_SCAN` | `E_MATCH` | alias bridge |
| `E_ALT_GEN` | `E_GENALT` | alias bridge |

These aliases are added in M-G1-IR-HEADER-WIRE and removed in Phase 3/5.

---

### 2b. Emitter Port Variable Names (C source)

The naming law mandates: Greek letters `α β γ ω` everywhere — C source,
comments, generated output. No ASCII spelling-out.

**Current state by file:**

| File | Port params | Verdict |
|------|-------------|---------|
| `emit_byrd_asm.c` | `const char *α, *β, *γ, *ω` | ✅ Correct |
| `emit_byrd_jvm.c` | `const char *gamma, *omega` | ✗ ASCII — fix to `γ, ω` |
| `emit_byrd_net.c` | `const char *gamma, *omega` | ✗ ASCII — fix to `γ, ω` |
| `icon_emit.c` | `IcnPorts.γ, IcnPorts.ω` | ✅ Correct |
| `icon_emit_jvm.c` | `IjPorts.γ, IjPorts.ω`, `out_α, out_β` | ✅ Correct |
| `prolog_emit_jvm.c` | `const char *lbl_γ, *lbl_ω` | ✗ Drop `lbl_` prefix — fix to `γ, ω` |

**Law restatement (confirmed):** Parameter names are the bare Greek letter.
No prefix (`lbl_`, `port_`, etc.). The parameter IS the label string — the
Greek name makes the role self-documenting.

---

### 2c. IcnEmitter / IjPorts Struct Fields

```c
/* icon_emit.c — x64 */
typedef struct { char γ[64]; char ω[64]; } IcnPorts;

/* icon_emit_jvm.c — JVM */
typedef struct { char γ[64]; char ω[64]; } IjPorts;
```

Both use Unicode Greek correctly. ✅

**Post-reorg:** These structs are candidates for unification into a single
`BackendPorts` typedef in `src/ir/ir.h` or `src/backend/ports.h`, with
`α`/`β` added as fields (currently computed via `icn_label_α`/`lbl_α` helpers).
This is a Phase 4 concern, not Phase 3.

---

### 2d. Output Macro Names

| File | Current | Law target | Deviation |
|------|---------|-----------|-----------|
| `emit_byrd_asm.c` | `A(fmt,...)` | `E(fmt,...)` | ✗ — rename `A` → `E` |
| `emit_byrd_jvm.c` | `J(fmt,...)` | `J(fmt,...)` | ✅ |
| `emit_byrd_net.c` | `N(fmt,...)` | `N(fmt,...)` | ✅ |
| `icon_emit.c` | `E(em, fmt,...)` | Rename to `ICN_OUT(em, fmt,...)` | ✗ — collision with law's `E()` |
| `icon_emit_jvm.c` | `J(fmt,...)` | `J(fmt,...)` | ✅ |
| `prolog_emit_jvm.c` | `J(fmt,...)` | `J(fmt,...)` | ✅ |
| `emit_wasm.c` | (new) | `W(fmt,...)` | establish at creation |

**Key finding — `icon_emit.c` macro collision:** `icon_emit.c` uses `E()` to
write to an `IcnEmitter` buffer. The naming law assigns `E()` to the x64 ASM
output macro. These two files are in different compilation units so there is no
actual C symbol clash, but the name collision is confusing. **Resolution:**
rename `icon_emit.c`'s write macro to `ICN_OUT()` in M-G3-NAME-X64-ICON. This
frees `E()` unambiguously for x64 instruction output.

---

## Area 3 — Generated Label Prefixes

Labels that appear in `.s` / `.j` / `.il` output. Must be predictable and
distinct across frontends.

### 3a. x64 ASM Labels

**SNOBOL4 backend (`emit_byrd_asm.c`):**

| Pattern | Meaning | SIL origin |
|---------|---------|------------|
| `P_<safe>_α` | Named pattern alpha entry | `P_` = Pattern; `_α` = port |
| `P_<safe>_β` | Named pattern beta | same |
| `P_<safe>_ret_γ` | Named pattern success return | same |
| `P_<safe>_ret_ω` | Named pattern failure return | same |
| `L_<base>_<uid>` | Anonymous node label | `L_` = Label + uid integer |
| `sno_var_X` | Variable X BSS slot | Area 1 above |
| `cursor` | Cursor position BSS (also emitted as global) | SIL `ATOP`/`ATP` |
| `conc_tmp` | Concatenation scratch | local pattern |
| `cap_*` | Capture buffer slots | local pattern |
| `alt_l`, `alt_r` | ALT branch labels | local pattern |
| `arb` | ARB loop label | local pattern |

**Prolog backend (within `emit_byrd_asm.c`, §5417+):**

| Pattern | Meaning |
|---------|---------|
| `pl_<pred>_c<N>_α` | Clause N alpha entry for predicate `pred` |
| `pl_<pred>_c<N>_hω<M>` | Clause N head-unify fail for arg M |
| `pl_<pred>_c<N>_α<K>` | Ucall K re-entry within clause N |
| `pl_<pred>_c<N>_ufail<K>` | Ucall K failure label |
| `pl_<pred>_c<N>_naf_ok<K>` | NAF success |
| `pl_<pred>_c<N>_naf_fail<K>` | NAF failure |
| `pl_<pred>_c<N>_neq_ok<K>` | `\=` success |
| `pl_<pred>_c<N>_cfail<K>_<F>` | Compound fail branch |

**Icon x64 backend (`icon_emit.c`):**

| Pattern | Meaning |
|---------|---------|
| `icon_<id>_α` | Node alpha |
| `icon_<id>_β` | Node beta |
| `icon_<id>_efail` | Expression fail |
| `icn_str_<N>` | String literal BSS slot |
| `icn_gvar_<n>` | Global variable BSS slot |
| `icn_pv_<proc>_<n>` | Proc-local variable |
| `icn_find_s1/s2/pos_<id>` | `find()` scratch slots |
| `icn_scan_oldsubj_<id>` | Scan saved subject |
| `icn_scan_oldpos_<id>` | Scan saved position |
| `icn_<n>_caller_ret` | Procedure caller return |
| `icn_<n>_done` | Procedure done |

### 3b. JVM Labels

**SNOBOL4 backend (`emit_byrd_jvm.c`):**

| Pattern | Meaning | Verdict |
|---------|---------|---------|
| `Jn<uid>_lit_ok` | Literal match success | ✗ — law requires `L<id>_α`/`L<id>_β` |
| `Jn<uid>_arb_loop` | ARB loop | ✗ — same |
| `Lkwg_alpha_loop` | Keyword global loop (hardcoded) | ✗ — `alpha` should be `α` |
| `export_omega_<n>` | Export omega delegate | ✗ — `omega` should be `ω` |

**Icon JVM backend (`icon_emit_jvm.c`):**

| Pattern | Meaning | Verdict |
|---------|---------|---------|
| `icn_<id>_α` | Node alpha | ✅ |
| `icn_<id>_β` | Node beta | ✅ |
| `icn_pv_<proc>_<n>` | Proc-local field | ✅ |
| `icn_main_done` | Top-level fallthrough | ✅ |

**Prolog JVM backend (`prolog_emit_jvm.c`):**

| Pattern | Meaning | Verdict |
|---------|---------|---------|
| `pj_unwind_loop/done` | Trail unwind runtime helpers | ✅ — runtime names, not node labels |
| `pj_deref_loop/done` | Dereference helpers | ✅ |
| `pj_unify_true` | Unification success | ✅ |
| `pj_exp_omega_<n>` | Export predicate omega | ✗ — `omega` should be `ω` |
| `pj_exp_done_<n>` | Export predicate done | ✅ |
| `pj_<fresh_N>_*` | Anonymous goal labels | ✅ |

### 3c. .NET Labels

**SNOBOL4 backend (`emit_byrd_net.c`):**

| Pattern | Meaning | Verdict |
|---------|---------|---------|
| `Ndt<uid>_prim/done` | DATATYPE branches | ✗ — law requires `L<id>_α/β` |
| `Ncc<uid>_ok<N>` | Concat child ok | ✗ — same |
| `Nn<uid>_dc` | Deferred commit | ✗ |
| `Nn<uid>_sm<N>` | Sequence midpoint | ✗ |
| `net_imp_gamma_<N>` | Import gamma delegate | ✗ — `gamma` → `γ` |
| `net_imp_omega_<N>` | Import omega delegate | ✗ — `omega` → `ω` |

### 3d. Label Prefix Canonical Summary

This is the **definitive reference** for generated label prefixes post-reorg:

| Frontend | Backend | Node label prefix | Static symbol prefix |
|----------|---------|-------------------|---------------------|
| SNOBOL4 | x64 | `P_<id>_` (named) / `L_<id>_` (anon) | `sno_var_`, `sno_kw_` |
| SNOBOL4 | JVM | `L<id>_` | `sno_var_`, `sno_kw_` |
| SNOBOL4 | .NET | `L<id>_` | `sno_var_`, `sno_kw_` |
| SNOBOL4 | WASM | `W<id>_` | `sno_var_`, `sno_kw_` |
| Icon | x64 | `icon_<id>_` | `icn_gvar_`, `icn_pv_`, `icn_str_` |
| Icon | JVM | `icn_<id>_` | `icn_pv_`, `icn_main_` |
| Icon | .NET | `icn_<id>_` (establish in M-G6-ICON-NET) | `icn_gvar_`, `icn_pv_` |
| Icon | WASM | `icn_<id>_` (establish in M-G6-ICON-WASM) | `icn_gvar_`, `icn_pv_` |
| Prolog | x64 | `pl_<pred>_c<N>_` | `pl_trail`, `pl_env` |
| Prolog | JVM | `pj_<fresh_N>_` | `pl_trail_top`, `pl_rt_init` |
| Prolog | .NET | `pl_<pred>_c<N>_` (establish in M-G6-PROLOG-NET) | `pl_trail` |
| Prolog | WASM | `pl_<pred>_c<N>_` (establish in M-G6-PROLOG-WASM) | `pl_trail` |
| Snocone | x64 | (inherit SNOBOL4 prefix — Snocone lowers to same IR) | `sno_var_` |
| Rebus | x64 | (TBD — M-G5-LOWER-REBUS-AUDIT) | TBD |
| Scrip | all | (TBD — M-G5-LOWER-SCRIP-AUDIT) | TBD |

**Port suffix law (all backends, all frontends):**

| Port | Suffix |
|------|--------|
| α | `_α` |
| β | `_β` |
| γ | `_γ` (rare — usually γ is a caller-supplied jump target, not a defined label) |
| ω | `_ω` (rare — same) |

Greek suffixes are used in generated output labels. **Hardcoded ASCII forms
(`_alpha`, `_beta`, `_gamma`, `_omega`) found in `emit_byrd_jvm.c` and
`emit_byrd_net.c` are deviations — fixed in M-G3-NAME-JVM / M-G3-NAME-NET.**

---

## Area 4 — Runtime Library Macro and Function Names

### 4a. `snobol4_asm.mac` — NASM Macro Library

This is the Byrd box macro library. **All macro names already use Greek letters
for port suffixes.** Complete macro inventory:

**Structural macros (no IR kind mapping):**
`STMT_SEP`, `PORT_SEP`, `STMT_COMMENT`, `PORT_COMMENT`, `PROG_INIT`, `PROG_END`

**Pattern primitive macros — fully conformant (IR kind → macro):**

| IR Kind | α macro | β macro |
|---------|---------|---------|
| `E_ANY` | `ANY_α`, `ANY_α_PTR`, `ANY_α_VAR` | `ANY_β`, `ANY_β_PTR`, `ANY_β_VAR` |
| `E_NOTANY` | `NOTANY_α`, `NOTANY_α_VAR` | `NOTANY_β`, `NOTANY_β_VAR` |
| `E_SPAN` | `SPAN_α`, `SPAN_α_PTR`, `SPAN_α_VAR` | `SPAN_β`, `SPAN_β_PTR`, `SPAN_β_VAR` |
| `E_BREAK` | `BREAK_α`, `BREAK_α_PTR`, `BREAK_α_VAR` | `BREAK_β`, `BREAK_β_PTR`, `BREAK_β_VAR` |
| `E_BREAKX` | `BREAKX_α_LIT`, `BREAKX_α_VAR` | `BREAKX_β_LIT`, `BREAKX_β_VAR` |
| `E_LEN` | `LEN_α` | `LEN_β` |
| `E_TAB` | `TAB_α` | `TAB_β` |
| `E_RTAB` | `RTAB_α` | `RTAB_β` |
| `E_REM` | `REM_α` | `REM_β` |
| `E_POS` | `POS_α`, `POS_α_VAR` | `POS_β` |
| `E_RPOS` | (via `POS_MATCH`/`RPOS_MATCH`) | `RPOS_β` |
| `E_ARB` | `ARB_α` | `ARB_β` |
| `E_ARBNO` | `ARBNO_α`, `ARBNO_α1` | `ARBNO_β`, `ARBNO_β1` |
| `E_CAPT_CUR` | `AT_α` | `AT_β` |
| `E_CAPT_IMM` | `DOL_CAPTURE`, `DOL_SAVE` | — |
| `E_SEQ` | `SEQ_α` | `SEQ_β` |
| `E_ALT` | `ALT_α` | `ALT_ω` |
| `E_FNC` (fn call) | `FN_α_INIT` | `FN_ω` |
| `E_ASSIGN` | `ASSIGN_INT`, `ASSIGN_STR`, `ASSIGN_NULL` | — |

**Utility macros:** `CURSOR_SAVE`, `CURSOR_RESTORE`, `ALT_SAVE_CURSOR`,
`ALT_RESTORE_CURSOR`, `PAT_α_RESET`, `NAMED_PAT_γ`, `NAMED_PAT_ω`,
`SETUP_SUBJECT`, `SUBJ_FROM16`, `SET_VAR`, `SET_VAR_INDIR`, `GET_VAR`,
`LOAD_VAR`, `LOAD_STR`, `LOAD_INT`, `LOAD_REAL`, `LOAD_NULVCL`,
`LOAD_NULVCL32`, `LOAD_FAILDESCR32`, `SET_CAPTURE`, `SAVE_DESCR`,
`STORE_ARG16`, `STORE_ARG32`, `STORE_RESULT`, `STORE_RESULT16`,
`SET_OUTPUT`, `APPLY_FN_0`, `APPLY_FN_N`, `APPLY_REPL`,
`APPLY_REPL_SPLICE`, `CALL_FN`, `CALL1_INT`, `CALL1_STR`, `CALL1_VAR`,
`CALL_APPLY`, `CALL_PAT_α`, `CALL_PAT_β`, `CONC2` (+ variants),
`CAT2_*`, `GOTO_ALWAYS`, `GOTO_S`, `GOTO_F`, `FAIL_BR`, `FAIL_BR16`,
`CHECK_FAIL`, `FN_CLEAR_VAR`, `FN_SET_PARAM`, `FN_γ`, `LIT_α`,
`LIT_α1`, `LIT_β`, `LIT_MATCH`, `LIT_VAR_α`, `LIT_VAR_β`.

**Verdict:** ✅ **`snobol4_asm.mac` is fully conformant with the naming law.**
Greek port suffixes throughout. No ASCII `_alpha`/`_beta`/`_gamma`/`_omega`
anywhere. This file sets the example for all other files.

**SIL alignment check for macro names:**
- `ANY_α` ← SIL `XANYC=1` (`p$any`) ✅
- `ARB_α` / `ARBNO_α` ← SIL `XFARB=17` / `XARBN=3` ✅
- `AT_α` ← SIL `XATP=4` (`ATOP`/`ATP`) ✅
- `SPAN_α` ← SIL `XSPNC=31` (`p$spn`) ✅
- `BREAK_α` ← SIL `XBRKC=8` (`p$brk`) ✅
- `BREAKX_α` ← SIL `XBRKX=9` (`p$bkx`) ✅
- `LEN_α` ← SIL `XLNTH=19` (`p$len`) ✅
- `TAB_α` ← SIL `XTB=33` (`p$tab`) ✅
- `RTAB_α` ← SIL `XRTB=26` (`p$rtb`) ✅
- `REM_α` ← SIL `XREM` (`p$rem`) ✅
- `POS_α` ← SIL `XPOSI=24` ✅
- `RPOS_α` ← SIL `XRPSI=25` ✅
- `ALT_α` ← SIL `ORPP` ✅
- `SEQ_α` ← SIL `CONCAT`/`CONCL` ✅

All 14 pattern primitive macros correctly trace to their SIL `X___` codes.

---

### 4b. Runtime C Shim Functions (called from `snobol4_asm.mac`)

These are the C functions that `snobol4_asm.mac` macros call via `call` instructions:

| Function | Meaning | Prefix convention |
|----------|---------|-------------------|
| `stmt_init` | Program startup | `stmt_` = statement runtime |
| `stmt_finish` | Program exit | same |
| `stmt_get` | Variable get | same |
| `stmt_set` | Variable set | same |
| `stmt_set_capture` | Set capture variable | same |
| `stmt_set_indirect` | Set via indirection | same |
| `stmt_set_null` | Set to null | same |
| `stmt_concat` | String concatenation | same |
| `stmt_match_descr` | Match descriptor | same |
| `stmt_match_var` | Match variable | same |
| `stmt_any_ptr` / `stmt_any_var` | ANY match | same |
| `stmt_span_ptr` / `stmt_span_var` | SPAN match | same |
| `stmt_break_ptr` / `stmt_break_var` | BREAK match | same |
| `stmt_breakx_lit` / `stmt_breakx_var` | BREAKX match | same |
| `stmt_notany_var` | NOTANY match | same |
| `stmt_pos_var` / `stmt_rpos_var` | POS/RPOS match | same |
| `stmt_at_capture` | Cursor capture (`@`) | same |
| `stmt_intval` | Integer coercion | same |
| `stmt_realval` | Real coercion | same |
| `stmt_strval` | String coercion | same |
| `stmt_is_fail` | Test for FAIL descriptor | same |
| `stmt_output` | OUTPUT assignment | same |
| `stmt_setup_subject` | Subject setup | same |
| `stmt_apply` | Function apply | same |
| `stmt_apply_replacement` | Replacement apply | same |
| `stmt_apply_replacement_splice` | Splice replace | same |

**Verdict:** ✅ `stmt_` prefix is consistent and clearly scoped. No SIL conflict
(SIL uses `STMT` for statement number fields, which is a different layer). The
`stmt_` C functions are our runtime shim layer. Prefix is the law.

---

### 4c. Engine/Runtime Higher-Level API (`engine.h`)

| Function/macro | Meaning | Verdict |
|----------------|---------|---------|
| `VARVAL_fn(v)` | Variable value lookup | `_fn` suffix = our convention for SIL proc names |
| `STRCONCAT_fn` | String concatenation | same |
| `STRDUP_fn` | String duplicate | same |
| `IS_FAIL_fn` | Test for fail | same |
| `IS_NULL_fn`, `IS_STR_fn`, `IS_INT_fn`, `IS_REAL_fn`, `IS_DATA_fn` | Type tests | same |
| `NV_GET_fn`, `NV_SET_fn` | Keyword get/set | `NV_` = Named Variable (SIL concept) |
| `SNO_INIT_fn` | Runtime init | `SNO_` prefix for top-level runtime |
| `pat_alt`, `pat_ref`, `var_as_pattern` | Pattern construction | `pat_` prefix |
| `array_set`, `array_set2` | Array ops | generic prefix |
| `table_set`, `table_has` | Table ops | generic prefix |
| `tree_append`, `tree_prepend`, `tree_insert` | Tree (TREEBLK_t) ops | generic prefix |

**`_fn` suffix convention:** Functions named `VARVAL_fn`, `STRCONCAT_fn` etc.
use the SIL procedure name as the root (`VARVAL` is a SIL proc name) with `_fn`
appended to avoid clashing with SNOBOL4 source-language identifiers that happen
to spell the same thing. This is a deliberate and correct anti-collision strategy.
**It is the law for engine-level SIL-derived names.**

**`NV_` prefix:** `NV_GET_fn`/`NV_SET_fn` — `NV` = Named Variable. SIL uses
`GNVARI` (Generate Named Variable from Integer) and related procs. Our `NV_`
abbreviation is correct and distinct.

---

## Findings Summary and Naming Law Extensions

### Confirmed Law (no changes needed)

The following are already in the naming law and are verified correct:

1. **Greek port letters** (`α β γ ω`) everywhere — C source, comments, generated
   labels. `snobol4_asm.mac` demonstrates full compliance.
2. **`sno_var_X`** for SNOBOL4 variable X in all backends.
3. **`sno_kw_NAME`** for SNOBOL4 keywords.
4. **`sno_subject`**, **`sno_cursor`**, **`sno_sublen`** for pattern matching state.
5. **`icn_`** prefix for Icon runtime symbols.
6. **`pl_`** prefix for Prolog runtime symbols.
7. **`stmt_`** prefix for C shim functions called from ASM macros.
8. **`_fn`** suffix for engine-level SIL-derived function names.
9. **`NV_`** prefix for Named Variable engine functions.

### Required Fixes (deviations from law — fixed in Phase 3)

| Deviation | Where | Fix milestone |
|-----------|-------|---------------|
| `A()` output macro | `emit_byrd_asm.c` | M-G3-NAME-X64 |
| `E()` collision (IcnEmitter write) | `icon_emit.c` | M-G3-NAME-X64-ICON → rename to `ICN_OUT()` |
| ASCII `gamma`/`omega` port params | `emit_byrd_jvm.c`, `emit_byrd_net.c` | M-G3-NAME-JVM, M-G3-NAME-NET |
| `lbl_γ`/`lbl_ω` (prefixed ports) | `prolog_emit_jvm.c` | M-G3-NAME-JVM-PROLOG |
| `Jn<uid>_*` label prefix | `emit_byrd_jvm.c` | M-G3-NAME-JVM |
| `Nn<uid>_*` label prefix | `emit_byrd_net.c` | M-G3-NAME-NET |
| `net_imp_gamma_<N>` / `omega` | `emit_byrd_net.c` | M-G3-NAME-NET |
| `pj_exp_omega_<n>` | `prolog_emit_jvm.c` | M-G3-NAME-JVM-PROLOG |
| `Lkwg_alpha_loop` / `export_omega_<n>` | `emit_byrd_jvm.c` | M-G3-NAME-JVM |
| Function prefix `jvm_emit_*` | `emit_byrd_jvm.c` | M-G3-NAME-JVM |
| Function prefix `net_emit_*` | `emit_byrd_net.c` | M-G3-NAME-NET |
| Function prefix `emit_pl_*`/`emit_prolog_*` | `emit_byrd_asm.c` (Prolog section) | M-G3-NAME-X64-PROLOG |
| Function prefix `ij_emit_*` | `icon_emit_jvm.c` | M-G3-NAME-JVM-ICON |
| Function prefix `pj_emit_*` | `prolog_emit_jvm.c` | M-G3-NAME-JVM-PROLOG |
| Function prefix `emit_*` (no backend qualifier) | `icon_emit.c` | M-G3-NAME-X64-ICON |

### Law Addition: `ICN_OUT()` for Icon x64 Emitter Write

Add to naming law in GRAND_MASTER_REORG.md:

> `ICN_OUT(em, fmt, ...)` — write macro for `icon_emit.c` (Icon x64 backend).
> Distinct from `E(fmt, ...)` (SNOBOL4/x64 instruction output). The distinction
> is necessary because both files may be compiled together after Phase 2
> restructuring. `ICN_OUT` is the law; do not use bare `E()` in `icon_emit.c`.

### Law Addition: EKind Alias Bridges

Add to naming law:

> During Phase 1 (M-G1-IR-HEADER-WIRE), `scrip-cc.h` receives `#define` aliases
> mapping old names to canonical `ir.h` names (e.g. `#define E_VART E_VAR`).
> These aliases exist only for compilation compatibility. They are removed
> in Phase 5 when all frontends are updated to use canonical names.

---

## No Law Changes Required

All four areas confirm the existing naming law is sound. The two additions above
(`ICN_OUT()` and alias bridge documentation) are clarifications, not corrections.
The `snobol4_asm.mac` macro library is the gold standard — it is fully conformant
and demonstrates what all other files should look like after Phase 3.

---

*M-G0-SIL-NAMES complete. Next: M-G1-IR-HEADER-DEF.*
