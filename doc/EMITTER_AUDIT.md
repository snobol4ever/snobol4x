# EMITTER_AUDIT.md — Pre-Reorg Emitter File Audit

**Produced by:** Claude Sonnet 4.6 (G-7 session, 2026-03-28)
**Milestone:** M-G0-AUDIT
**Purpose:** Document every emitter file's function naming prefix, output macro,
port variable names, and generated label patterns — the baseline that Phase 3
(Naming Unification) will mechanically transform into the naming law.

The **naming law** (target state) is defined in `GRAND_MASTER_REORG.md` § Naming
Convention. This document records the *current* state so deviations are explicit.

---

## Files Covered

| # | File (current path) | Lines | Backend | Frontend(s) |
|---|---------------------|-------|---------|-------------|
| 1 | `src/backend/x64/emit_byrd_asm.c` | 7247 | x64 ASM | SNOBOL4 |
| 2 | `src/backend/x64/emit_byrd_asm.c` §5417–7238 | (same file) | x64 ASM | Prolog |
| 3 | `src/backend/jvm/emit_byrd_jvm.c` | 4868 | JVM | SNOBOL4 |
| 4 | `src/backend/net/emit_byrd_net.c` | 2840 | .NET | SNOBOL4 |
| 5 | `src/frontend/icon/icon_emit.c` | 1931 | x64 ASM | Icon |
| 6 | `src/frontend/icon/icon_emit_jvm.c` | 8113 | JVM | Icon |
| 7 | `src/frontend/prolog/prolog_emit_jvm.c` | 9972 | JVM | Prolog |
| 8 | `src/backend/wasm/emit_wasm.c` | — | WASM | (all) |

**File 8 does not exist yet.** It is created as a skeleton in M-G2-SCAFFOLD-WASM.
The naming law is applied from scratch in M-G3-NAME-WASM.

Post-reorg target paths are listed in `GRAND_MASTER_REORG.md` § Folder Structure.

---

## File 1 — `emit_byrd_asm.c` (SNOBOL4 x64 ASM)

**Top-level entry:** called from driver; no single named entry function — the
SNOBOL4 pass is integrated into the main emit loop via `Program *prog`.

**Output macro:** `A(fmt, ...)` — `fprintf(out, fmt, ##__VA_ARGS__)`. Also
`ALFC(lbl, comment, fmt, ...)` for annotated lines with label/comment columns.

**Column constants:**
```c
#define COL_W   28    /* label column width */
#define COL2_W  12    /* instruction column width */
#define COL_CMT (COL_W + COL2_W + 32)  /* comment column = 72 */
```

**Output helpers:** `emit_instr()`, `emit_raw()`, `emit_to_col()`, `emit_sep_major()`,
`emit_sep_minor()`.

**Port variable names (current):** The four Byrd ports are passed as `const char *`
parameters named `α`, `β`, `γ`, `ω` (literal Unicode Greek letters in C source).

**Core emit function signature:**
```c
static void emit_pat_node(EXPR_t *pat,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *cursor,
                           const char *subj, const char *subj_len,
                           int depth)
```

**Other emit functions:** `emit_lit`, `emit_seq`, `emit_alt`, `emit_arbno`,
`emit_arb`, `emit_pos`, `emit_rpos`, `emit_any`, `emit_notany`, `emit_span`,
`emit_break`, `emit_span_var`, `emit_break_var`, `emit_breakx_var`,
`emit_breakx_lit`, `emit_any_var`, `emit_notany_var`, `emit_len`, `emit_tab`,
`emit_rtab`, `emit_rem`, `emit_imm`, `emit_named_ref`, `emit_named_def`,
`emit_expr`.

**Generated label patterns (current):**
```
P_<safe>_α        named pattern alpha entry     (NamedPat)
P_<safe>_β        named pattern beta            (NamedPat)
P_<safe>_ret_γ    named pattern success return  (NamedPat)
P_<safe>_ret_ω    named pattern fail return     (NamedPat)
L_<base>_<uid>    anonymous node labels         (uid = integer)
alt<uid>_left_ω   ALT trampoline label
```

**Deviations from naming law:**
- Parameter names use Unicode `α/β/γ/ω` directly rather than `lbl_alpha` etc.
- Output macro is `A()` not `E()` (law requires `E()`)
- Helper macros are `ALFC()`, not `EL()`/`EI()`
- Function prefix is `emit_<kind>` (matches law) ✓
- Label prefix is `P_<id>_<port>` for named, `L_<base>_<uid>` for anonymous
  (law requires `P_<id>_alpha` / `P_<id>_beta` uniformly)

---

## File 2 — `emit_byrd_asm.c` §5417–7238 (Prolog x64 ASM)

**Section boundary:** lines 5417–7238 within `emit_byrd_asm.c`. Becomes
`emit_x64_prolog.c` after M-G2-MOVE-PROLOG-ASM-a/b.

**Top-level entry:** `emit_prolog_program(Program *prog)` (line 7208).
Called from `emit_prolog_choice()` dispatcher (line 4274).

**Output macro:** Same `A()` as File 1 (shared file).

**Port variable names (current):** Mixed — uses both Unicode `α`/`β` in struct
field names (`.α_lbl`, `.β_lbl`, `.ret_γ`, `.ret_ω`) and plain ASCII strings
for local label buffers (`char this_α[]`, `char hω_lbl[]`, `char last_β_lbl[]`).

**Emitter functions:**
```c
static void emit_pl_atom_data(void)
static void emit_pl_atom_data_v2(void)
static void emit_pl_header(Program *prog)
static void emit_pl_term_load(EXPR_t *e, int frame_base_words)
static void emit_prolog_clause_block(EXPR_t *clause, int idx, int total, ...)
static void emit_prolog_choice(EXPR_t *choice)
static void emit_prolog_main(Program *prog)
static void emit_prolog_program(Program *prog)
```

**Generated label patterns (current):**
```
pl_<pred>_c<N>_α          clause N alpha entry
pl_<pred>_c<N>_hω<M>      clause N head-unify fail for arg M
pl_<pred>_c<N>_α<K>       ucall K re-entry within clause N
pl_<pred>_c<N>_ufail<K>   ucall K failure label
pl_<pred>_c<N>_naf_ok<K>  NAF (negation-as-failure) success
pl_<pred>_c<N>_naf_fail<K> NAF failure
pl_<pred>_c<N>_neq_ok<K>  \\= success
pl_<pred>_c<N>_neq_fail<K> \\= failure
pl_<pred>_c<N>_cfail<K>_<F> compound fail branch
```
where `<pred>` = `pl_safe()` mangled predicate name, `<N>` = clause index.

**Deviations from naming law:**
- Function prefix is `emit_pl_*` and `emit_prolog_*` — inconsistent; law
  requires `emit_x64_prolog_*` for Prolog-specific, `emit_x64_<Kind>` for shared
- Label prefix `pl_<pred>_c<N>_α` uses Unicode α — law requires `_alpha`
- Output macro `A()` — law requires `E()`

---

## File 3 — `emit_byrd_jvm.c` (SNOBOL4 JVM)

**Top-level entry:** `void jvm_emit(Program *prog, FILE *out, const char *filename)`
(line 4728).

**Output macros:**
```c
static void J(const char *fmt, ...)      /* fprintf(out, fmt, ...) */
static void PN(fmt,...)                  /* fprintf(out, "    " fmt "\n", ...) */
static void PNL(lbl,fmt,...)             /* labelled instruction */
```
Also `JI(instr, ops)` and `JL(label, instr, ops)` (defined elsewhere or inline).

**Port variable names (current):** `gamma` and `omega` as plain ASCII `const char *`
parameter names. No `alpha`/`beta` at the `jvm_emit_pat_node` level — α/β are
implicit in the JVM stack and label flow rather than explicit parameters.

**Core emit function signature:**
```c
static void jvm_emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_cap_local,
                               FILE *out, const char *classname)
```

**Other emit functions (prefix `jvm_emit_*`):**
`jvm_emit_expr`, `jvm_emit_goto`, `jvm_emit_parse_helper`,
`jvm_emit_to_double`, `jvm_emit_pat_node`.

**Generated label patterns (current):**
```
Jn<uid>_lit_ok       literal match success
Jn<uid>_arb_loop     ARB loop
Jn<uid>_arb_decr     ARB decrement
Lkwg_alpha_loop      keyword global (hardcoded, not uid-based)
Lkwg_alpha_done
Lkwg_not_alphabet
export_omega_<name>  export omega delegate
```

**Deviations from naming law:**
- Function prefix is `jvm_emit_*` — law requires `emit_jvm_*`
- Parameter names `gamma`/`omega` rather than `lbl_gamma`/`lbl_omega`
- Output macro is `J()` — matches law ✓
- Instruction helper `JI()` — matches law ✓
- Label helper `JL()` / `PNL()` — inconsistent; law requires `JL()`
- Label prefix `Jn<uid>_*` — law requires `L<id>_alpha` / `L<id>_beta`

---

## File 4 — `emit_byrd_net.c` (SNOBOL4 .NET)

**Top-level entry:** `void net_emit(Program *prog, FILE *out, const char *filename)`
(line 2813).

**Output macros:**
```c
static void N(const char *fmt, ...)    /* fprintf(out, fmt, ...) */
static void NL(label, instr, ops)      /* labelled instruction */
static void NI(instr, ops)             /* unlabelled instruction */
static void NC(comment)                /* comment line */
static void NSep(tag)                  /* section separator */
```

**Port variable names (current):** `gamma` and `omega` as `const char *` parameters.
Also `lbl_dc` (deferred commit label), `true_gamma`, `seq_omega_buf` as locals.
`net_imp_gamma_<N>` and `net_imp_omega_<N>` for import delegates.

**Core emit function signature:**
```c
static void net_emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_cap_local, FILE *out)
```

**Other emit functions (prefix `net_emit_*`):**
`net_emit_expr`, `net_emit_goto`, `net_emit_branch_success`,
`net_emit_branch_fail`, `net_emit_pat_node`, `net_emit_one_stmt`,
`net_emit_stmts`, `net_emit_header`, `net_emit_main_open`,
`net_emit_main_close`, `net_emit_fn_method`, `net_emit_fn_methods`,
`net_emit_array_helpers`, `net_emit_footer`, `net_emit_import_call`.

**Generated label patterns (current):**
```
Ndt<uid>_prim        DATATYPE primary branch
Ndt<uid>_done        DATATYPE done
Ncc<uid>_ok<N>       concatenation child N ok
Nn<uid>_dc           deferred commit
Nn<uid>_sm<N>        sequence mid-point N
Nn<uid>_aok          ARB ok
Nn<uid>_alt_n<N>     ALT branch N
Nn<uid>_nam_ok       named-pattern ok
Nn<uid>_dol_ok       dollar-capture ok
Nn<uid>_arb_loop     ARB loop
Nn<uid>_arb_done     ARB done
Nn<uid>_arb_cok      ARB cursor ok
Nn<uid>_arb_cf       ARB cursor fail
Nn<uid>_spn_lp       SPAN loop
net_imp_gamma_<N>    import gamma delegate (class scope)
net_imp_omega_<N>    import omega delegate (class scope)
```

**Deviations from naming law:**
- Function prefix `net_emit_*` — law requires `emit_net_*`
- Parameter names `gamma`/`omega` not `lbl_gamma`/`lbl_omega`
- Output macro `N()` — matches law ✓
- `NL()`/`NI()` — match law ✓
- Label prefix `Nn<uid>_*` — law requires `L<id>_alpha` / `L<id>_beta`

---

## File 5 — `icon_emit.c` (Icon x64 ASM)

**Top-level entry:** `void icn_emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
const char *succ, const char *fail)` (line 58 in `icon_emit.h`).

**Port struct (current):**
```c
typedef struct {
    char γ[64];   /* success port — jump here on γ (succeed) */
    char ω[64];   /* failure port — jump here on ω (fail)    */
} IcnPorts;
```
α and β are computed per-node via helpers:
```c
void icn_label_α(int id, char *b, size_t s)  { snprintf(b,s,"icon_%d_α",id); }
void icn_label_β(int id, char *b, size_t s)  { snprintf(b,s,"icon_%d_β",id); }
```

**Output macro:** `E(em, fmt, ...)` — writes to emitter's output buffer.
`Jmp(em, label)` for unconditional jumps.

**Emit function signature:**
```c
static void emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      const char *succ, const char *fail)
```
(Plus per-kind helpers: `emit_int`, `emit_str`, `emit_var`, `emit_assign`,
`emit_return`, `emit_fail_node`, `emit_suspend`, `emit_if`, `emit_call`,
`emit_alt`, `emit_cset`, `emit_neg`, `emit_not`, `emit_seq`.)

**Generated label patterns (current):**
```
icon_<id>_α           node alpha (via icn_label_α)
icon_<id>_β           node beta  (via icn_label_β)
icon_<id>_efail       expression fail
icon_<id>_cfail       conditional fail
icon_<id>_scan_bfail  scan body fail
icn_str_<N>           string literal BSS slot
icn_gvar_<name>       global variable BSS slot
icn_pv_<proc>_<name>  proc-local variable
icn_find_s1_<id>      find() scratch slot
icn_find_s2_<id>      find() scratch slot
icn_find_pos_<id>     find() position slot
icn_sq<id>_lptr       string-to-cset left pointer
icn_cc<id>_lptr       concat left pointer
icn_scan_oldsubj_<id> scan saved subject
icn_scan_oldpos_<id>  scan saved position
icn_<name>_caller_ret procedure caller return label
icn_<name>_done       procedure done label
```

**Deviations from naming law:**
- `IcnPorts` struct uses `γ`/`ω` fields (Unicode) — law requires struct fields
  named `lbl_gamma`/`lbl_omega`, or passed as `const char *lbl_gamma` parameters
- Function prefix `emit_*` (no backend qualifier) — law requires `emit_x64_icon_*`
  for Icon-specific, `emit_x64_<Kind>` for shared
- Output macro `E()` — matches law ✓ (but different meaning: law's `E()` is
  for x64 SNOBOL4; here `E()` writes to IcnEmitter buffer)
- Label prefix `icon_<id>_α` uses Unicode — law requires `icon_<id>_alpha`

---

## File 6 — `icon_emit_jvm.c` (Icon JVM)

**Top-level entry:** `void ij_emit_file(IcnNode **nodes, int count, FILE *out,
const char *filename, const char *outpath, ImportEntry *imports)` (line 7133).

**Port struct (current):**
```c
typedef struct { char γ[64]; char ω[64]; } IjPorts;
```
α and β computed via:
```c
static void lbl_α(int id, char *b, size_t s) { snprintf(b,s,"icn_%d_α", id); }
static void lbl_β(int id, char *b, size_t s) { snprintf(b,s,"icn_%d_β", id); }
```
Output parameters named `oα` and `oβ` (written by callee, read by caller).

**Output macro:** `J(fmt, ...)` — matches law ✓.

**Emit function signature:**
```c
static void ij_emit_expr(IcnNode *n, IjPorts ports,
                         char *out_α, char *out_β)
```
Per-kind helpers: `ij_emit_int`, `ij_emit_real`, `ij_emit_str`, `ij_emit_var`,
`ij_emit_assign`, plus many more `ij_emit_*` functions.

**Generated label patterns (current):**
```
icn_<id>_α           node alpha (via lbl_α)
icn_<id>_β           node beta  (via lbl_β)
icn_pv_<proc>_<name> proc-local variable field
icn_main_done        top-level fallthrough
pj_exp_omega_<name>  (from Prolog linker integration — not Icon-native)
pj_exp_done_<name>
```

**Deviations from naming law:**
- Function prefix `ij_emit_*` — law requires `emit_jvm_icon_*` for Icon-specific
- `IjPorts` struct uses Unicode `γ`/`ω` — law requires `lbl_gamma`/`lbl_omega`
- `out_α`/`out_β` output parameters use Unicode — law requires `out_alpha`/`out_beta`
- Label prefix `icn_<id>_α` uses Unicode — law requires `icn_<id>_alpha`
- Output macro `J()` — matches law ✓

---

## File 7 — `prolog_emit_jvm.c` (Prolog JVM)

**Top-level entry:** Implicit — called via `pj_emit_main(prog)` at end of file.
No single named public entry point visible; driven from the SNOBOL4 JVM emit loop
which dispatches Prolog programs to `pj_emit_*` functions.

**Output macro:** `J(fmt, ...)` — matches law ✓.
Also `JI(instr, ops)` and `JL(label, instr, ops)`.

**Port variable names (current):** `lbl_γ` and `lbl_ω` as `const char *` parameters
(Unicode subscript). These are closer to the law than other emitters but use Unicode
γ/ω rather than ASCII `lbl_gamma`/`lbl_omega`.

**Core emit function signature:**
```c
static void pj_emit_goal(EXPR_t *goal, const char *lbl_γ, const char *lbl_ω,
                         ...)
static void pj_emit_body(EXPR_t **goals, int ngoals, const char *lbl_γ,
                         const char *lbl_ω, const char *lbl_outer_ω, ...)
```

**Other emit functions (prefix `pj_emit_*`):**
`pj_emit_runtime_helpers`, `pj_emit_assertz_helpers`, `pj_emit_class_header`,
`pj_emit_goal`, `pj_emit_body`, `pj_emit_term`, `pj_emit_arith`,
`pj_emit_arith_as_term`, `pj_emit_arith_as_double`, `pj_emit_dbl_const`,
`pj_emit_main`.

**Generated label patterns (current):**
```
pj_unwind_loop       trail unwind runtime helper (fixed name)
pj_unwind_done
pj_deref_loop        dereference runtime helper (fixed name)
pj_deref_done
pj_unify_true        unification success (fixed name)
pj_unify_check_b_var
pj_exp_omega_<name>  export predicate omega
pj_exp_done_<name>   export predicate done
pj_<fresh_N>_*       anonymous goal labels (via pj_fresh_label())
```
where `pj_fresh_label()` returns an integer counter.

**Deviations from naming law:**
- Function prefix `pj_emit_*` — law requires `emit_jvm_prolog_*` for Prolog-specific
- Parameter names `lbl_γ`/`lbl_ω` use Unicode — law requires `lbl_gamma`/`lbl_omega`
- Output macro `J()` — matches law ✓
- `JI()`/`JL()` — match law ✓
- Label prefix `pj_*` for runtime helpers — acceptable as runtime names, not node labels

---

## File 8 — `emit_wasm.c` (WASM — does not exist yet)

Created as a skeleton in **M-G2-SCAFFOLD-WASM**. The naming law is applied from
scratch in **M-G3-NAME-WASM** — no deviations to document here. Target conventions:

- Output macro: `W(fmt, ...)` / `WI(instr, ops)` / `WL(label, instr, ops)`
- Port parameters: `const char *lbl_alpha`, `lbl_beta`, `lbl_gamma`, `lbl_omega`
- Function prefix: `emit_wasm_<Kind>` for shared kinds, `emit_wasm_<frontend>_*` for frontend-specific
- Label prefix: `W<id>_alpha`, `W<id>_beta` (WASM uses `br_table` for dispatch)

---

## Deviation Summary Table

This table drives Phase 3 work. Greek letters (α β γ ω) in C source and generated
labels are **correct** — the law mandates Greek everywhere. The deviations are in
function prefixes and output macros only.

| File | Output macro | Func prefix → target | Port params | Labels |
|------|-------------|----------------------|-------------|--------|
| emit_byrd_asm.c (SNO x64) | `A()` → `E()` ✗ | `emit_<kind>` ✓ | `α/β/γ/ω` ✓ | `P_<id>_α` ✓ |
| emit_byrd_asm.c (PL x64) | `A()` → `E()` ✗ | `emit_pl_*` → `emit_x64_prolog_*` ✗ | `α/β` ✓ | `pl_<pred>_c<N>_α` ✓ |
| emit_byrd_jvm.c (SNO JVM) | `J()` ✓ | `jvm_emit_*` → `emit_jvm_*` ✗ | `gamma/omega` → `γ/ω` ✗ | `Jn<uid>_*` → `L<id>_α/β` ✗ |
| emit_byrd_net.c (SNO .NET) | `N()` ✓ | `net_emit_*` → `emit_net_*` ✗ | `gamma/omega` → `γ/ω` ✗ | `Nn<uid>_*` → `L<id>_α/β` ✗ |
| icon_emit.c (Icon x64) | `E()` ✓* | `emit_*` → `emit_x64_icon_*` ✗ | `IcnPorts.γ/ω` ✓ | `icon_<id>_α` ✓ |
| icon_emit_jvm.c (Icon JVM) | `J()` ✓ | `ij_emit_*` → `emit_jvm_icon_*` ✗ | `IjPorts.γ/ω`, `oα/oβ` ✓ | `icn_<id>_α` ✓ |
| prolog_emit_jvm.c (PL JVM) | `J()` ✓ | `pj_emit_*` → `emit_jvm_prolog_*` ✗ | `lbl_γ/lbl_ω` → `γ/ω` ✗ | `pj_*` ✓ (runtime names) |
| emit_wasm.c | establish `W()` | establish `emit_wasm_*` | establish `γ/ω` | establish `W<id>_α/β` |

*`icon_emit.c`'s `E()` writes to an `IcnEmitter` buffer — same letter as the law's
x64 output macro but different mechanism. M-G3-NAME-X64-ICON must rename it to
distinguish from the law's `E()` for x64 instruction output.

**The two real deviations by file:**

1. **Output macro name** — `emit_byrd_asm.c` uses `A()` instead of `E()`. All others correct.
2. **Function prefix** — every file uses its own legacy prefix (`jvm_emit_*`, `net_emit_*`, `ij_emit_*`, `pj_emit_*`, `emit_pl_*`). Law requires `emit_<backend>_<kind>` or `emit_<backend>_<frontend>_*`.
3. **Port parameter spelling** — `emit_byrd_jvm.c` and `emit_byrd_net.c` use ASCII `gamma`/`omega` instead of `γ`/`ω`. `prolog_emit_jvm.c` uses `lbl_γ`/`lbl_ω` instead of bare `γ`/`ω`. All Icon emitters already use Greek correctly.
4. **Generated label prefix** — JVM and .NET use `Jn<uid>_*` and `Nn<uid>_*` instead of `L<id>_α`/`L<id>_β`.

---

## Key Finding: The Existing Code Is Largely Correct

The current emitters' use of Greek letters (α β γ ω) in C source code, struct
fields, and generated labels is **right** — it conforms to the law. The law
mandates Greek everywhere, no ASCII spelling-out.

The actual deviations requiring Phase 3 work are narrower than initially assessed:
- Output macro `A()` → `E()` in `emit_byrd_asm.c`
- Function prefixes throughout (mechanical rename, one file at a time)
- ASCII `gamma`/`omega` parameter names in JVM and NET SNOBOL4 emitters → `γ`/`ω`
- `lbl_γ`/`lbl_ω` prefix in Prolog JVM → bare `γ`/`ω`
- Label prefix `Jn<uid>_*` / `Nn<uid>_*` → `L<id>_α` / `L<id>_β`

Note: an earlier version of this document incorrectly called the Greek usage a
"deviation" and recommended switching to ASCII. That was wrong. The law was also
incorrectly written (using `lbl_alpha` etc.) and has been corrected in
GRAND_MASTER_REORG.md as of G-7.

---

*M-G0-AUDIT complete. Next: M-G0-IR-AUDIT.*
