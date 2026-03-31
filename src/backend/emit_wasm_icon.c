/*
 * emit_wasm_icon.c — Icon × WASM emitter (IW-session)
 *
 * Structural oracles (read before editing):
 *   ByrdBox/byrd_box.py  genc()       — flat-goto C template per node
 *   ByrdBox/test_icon-4.py            — return-function Python = direct WAT map
 *   jcon-master/tran/irgen.icn        — authoritative four-port wiring per AST node
 *   one4all/src/backend/emit_jvm_icon.c — JVM encoding reference (8k lines)
 *   one4all/src/backend/emit_x64_icon.c — x64 encoding reference
 *
 * Translation principle (BACKEND-WASM.md §Control Flow Model):
 *   x64/JVM encode each Byrd port as a flat label + jmp/goto.
 *   WASM has no flat goto — structured control only.
 *   Each Byrd port becomes a WAT function with return_call (zero-stack-growth tail call).
 *   Generator state (to.I counter, alt branch index, etc.) lives in WASM linear memory.
 *
 *   Python oracle (test_icon-4.py):
 *     def to1_start():  return x1_start          → (func $to1_start  return_call $x1_start)
 *     def to1_resume(): to1_I += 1; return to1_code → (func $to1_resume  i32.store ...; return_call $to1_code)
 *
 * RULES.md §BYRD BOXES: emit labels+gotos, never interpret IR nodes at emit-time.
 *
 * Shared IR nodes (E_ADD, E_SUB, E_MPY, E_DIV, E_CONCAT, E_QLIT, E_ILIT…) stay
 * in emit_wasm.c.  Icon-specific ICN_* nodes live here only.
 *
 * File layout:
 *   §1  WAT output macros + generator-state memory
 *   §2  Label/function-name helpers
 *   §3  Node emitters (one per ICN_ kind) — Tier 0 first
 *   §4  Public dispatch  emit_wasm_icon_node()
 *
 * Milestones:
 *   M-IW-SCAFFOLD  scaffold + clean build (IW-1 2026-03-30)
 *   M-IW-A01       ICN_INT/STR/VAR/ASSIGN/CALL(write)/PROC/RETURN/FAIL  rung01 hello
 *   M-IW-A02       ICN_ADD/SUB/MUL/DIV/MOD + ICN_NEG/POS               rung01 arith
 *   M-IW-A03       ICN_LT/LE/GT/GE/EQ/NE                               rung01 relops
 *   M-IW-G01       ICN_TO                                               rung01 to-gen
 *   M-IW-G02       ICN_TO_BY
 *   M-IW-G03       ICN_EVERY
 *   M-IW-G04       ICN_ALT   (value alternation)
 *   M-IW-G05       ICN_BANG  (string/list iteration)
 *   M-IW-G06       ICN_LIMIT
 *   M-IW-S01       ICN_CONCAT (via shared $sno_str_concat)
 *   M-IW-S02       ICN_SLT/SLE/SGT/SGE/SEQ/SNE
 *   M-IW-S03       ICN_SIZE / ICN_NONNULL / ICN_NULL
 *   M-IW-S04       ICN_SCAN
 *   M-IW-C01       ICN_IF   (§4.5 indirect-goto gate → br_table in WASM)
 *   M-IW-C02       ICN_WHILE / ICN_UNTIL
 *   M-IW-C03       ICN_REPEAT / ICN_NEXT / ICN_BREAK
 *   M-IW-C04       ICN_AUGOP / ICN_SWAP / ICN_IDENTICAL
 *   M-IW-P01       ICN_PROC / ICN_CALL / ICN_RETURN
 *   M-IW-P02       ICN_FAIL
 *   M-IW-P03       ICN_SUSPEND
 *   M-IW-P04       ICN_INITIAL
 *   M-IW-CS01..B01 csets, string builtins
 *   M-IW-D01..D05  data structures (subscript, section, list, table, record, case)
 */

#include "icon_ast.h"
#include "icon_emit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── §1  WAT output macros ────────────────────────────────────────────────── */

/* WI() — write to the shared wasm_out file handle declared in emit_wasm.c    */
/* emit_wasm.c exposes it via a getter; we use the same W() macro convention. */
/* FORWARD: emit_wasm.c must call emit_wasm_icon_set_out(FILE*) at startup.   */

static FILE *icon_wasm_out = NULL;

void emit_wasm_icon_set_out(FILE *f) { icon_wasm_out = f; }

#define WI(fmt, ...)  fprintf(icon_wasm_out, fmt, ##__VA_ARGS__)

/*
 * Generator state memory layout (WASM linear memory, above snobol4 heap):
 *
 * The SNOBOL4 WASM runtime uses:
 *   [0..8191]       output buffer
 *   [8192..32767]   string literal data
 *   [32768..]       string heap (bump allocator)
 *
 * Icon generator state sits at a fixed block above the snobol4 region.
 * Each generator instance gets a 64-byte slot (16 × i32).
 * Slot 0 = first live generator frame, assigned at compile time (static).
 * This is adequate for Tier 0-3 (no recursive generators); deep recursion
 * needs a dynamic stack — deferred to M-IW-DEEP.
 *
 * ICN_TO uses:  slot[0] = current counter I
 * ICN_ALT uses: slot[0] = branch index (1=left, 2=right)
 * ICN_BANG uses: slot[0] = char index into string
 * ICN_LIMIT uses: slot[0] = remaining count
 * ICN_SUSPEND: slot[0] = resume-gate index
 *
 * Base address: 0xC000 (49152) — variable table area [49152..65535], unused in Tier 0.
 * 16KB = 256 slots × 64 bytes, fits within the 1-page (64KB) sno_runtime.
 * (0x10000 would require a 2nd page — deferred to M-IW-DEEP.)
 */
#define ICON_GEN_STATE_BASE  0xC000   /* 49152 — variable table area */
#define ICON_GEN_SLOT_BYTES  64
#define ICON_GEN_MAX_SLOTS   256

static int icon_gen_slot_next = 0;   /* next free slot index */

/* ── §1b  String literal intern table (M-IW-A02) ────────────────────────────
 * Mirrors emit_wasm.c strlit_intern / emit_data_segment.
 * String data is placed at ICN_STR_DATA_BASE (page 1, offset 65536).
 * Each unique string gets globals $icn_str_off{id} and $icn_str_len{id}
 * where id = ICN_STR node id assigned during emit (see ICN_STR case).
 * Pre-scan pass interns all ICN_STR nodes; data segment emitted before funcs.
 */
#define ICN_STR_DATA_BASE  65536     /* page 1 — safe above runtime page 0 */
#define ICN_MAX_STRLITS    1024

typedef struct { char *text; int len; int offset; } IcnStrLit;
static IcnStrLit icn_str_lits[ICN_MAX_STRLITS];
static int       icn_str_nlit  = 0;
static int       icn_str_bytes = 0;

/* Returns index into icn_str_lits[] (deduplicated). */
static int icn_strlit_intern(const char *s) {
    int len = s ? (int)strlen(s) : 0;
    const char *t = s ? s : "";
    for (int i = 0; i < icn_str_nlit; i++)
        if (icn_str_lits[i].len == len &&
            (len == 0 || memcmp(icn_str_lits[i].text, t, (size_t)len) == 0))
            return i;
    if (icn_str_nlit >= ICN_MAX_STRLITS) return 0;
    int idx = icn_str_nlit++;
    icn_str_lits[idx].text   = strdup(t);
    icn_str_lits[idx].len    = len;
    icn_str_lits[idx].offset = icn_str_bytes;
    icn_str_bytes += len;
    return idx;
}

static int icn_strlit_abs(int idx) {
    return ICN_STR_DATA_BASE + icn_str_lits[idx].offset;
}

/* Emit the (data ...) segment.  Called after globals, before functions. */
static void icn_emit_data_segment(void) {
    if (icn_str_bytes == 0) return;
    WI("\n  ;; Icon string literals at offset %d (page 1)\n", ICN_STR_DATA_BASE);
    WI("  (data (i32.const %d) \"", ICN_STR_DATA_BASE);
    for (int i = 0; i < icn_str_nlit; i++) {
        const unsigned char *t = (const unsigned char *)icn_str_lits[i].text;
        for (int j = 0; j < icn_str_lits[i].len; j++) {
            unsigned char c = t[j];
            if (c == '"' || c == '\\') WI("\\%02x", c);
            else if (c < 32 || c > 126) WI("\\%02x", c);
            else WI("%c", (char)c);
        }
    }
    WI("\")\n");
}

/* Pre-scan: walk ICN tree and intern every ICN_STR literal. */
static void icn_prescan_node(const IcnNode *n) {
    if (!n) return;
    if (n->kind == ICN_STR && n->val.sval)
        icn_strlit_intern(n->val.sval);
    for (int i = 0; i < n->nchildren; i++)
        icn_prescan_node(n->children[i]);
}

static void icn_strlit_reset(void) {
    for (int i = 0; i < icn_str_nlit; i++) { free(icn_str_lits[i].text); icn_str_lits[i].text = NULL; }
    icn_str_nlit  = 0;
    icn_str_bytes = 0;
}

static int icon_alloc_gen_slot(void) {
    if (icon_gen_slot_next >= ICON_GEN_MAX_SLOTS) {
        fprintf(stderr, "emit_wasm_icon: too many generator slots\n");
        return 0;
    }
    return icon_gen_slot_next++;
}

static int icon_gen_slot_addr(int slot) {
    return ICON_GEN_STATE_BASE + slot * ICON_GEN_SLOT_BYTES;
}

/* ── §2  Label / function-name helpers ───────────────────────────────────────
 *
 * WASM function names must be globally unique within a module.
 * We use a global counter + node address to guarantee uniqueness.
 * Convention mirrors byrd_box.py / test_icon-4.py:
 *   $to{N}_α  $to{N}_β  $to{N}_γ  (α=start, β=resume/fail-propagate, γ=succeed-propagate)
 *   $to{N}_code  — extra chunk for ICN_TO range check
 *
 * The four Byrd ports map as:
 *   α (start)   — initial entry
 *   β (resume)  — re-entry after backtrack from child (also called fail-propagate
 *                 when the node itself has no backtrack state, just forwards β up)
 *   γ (succeed) — departure on success
 *   ω (fail)    — departure on failure (becomes β of the enclosing context)
 *
 * In WASM tail-call encoding each is a separate (func …) that ends with return_call.
 * The caller's γ and ω are passed in as function-index parameters where needed,
 * or baked in at compile time for static wiring.
 */

/* Unique node counter (reset per procedure) */
static int wasm_icon_ctr = 0;

/* M-IW-P01: current procedure context — set by emit_wasm_icon_proc().
 * Used by ICN_RETURN (to know whether to store into $icn_retval or jump
 * to icn_prog_end) and ICN_VAR (to map param names → $icn_param{i}). */
static char  icn_cur_proc_name[128] = "";   /* "" = not in a proc */
static int   icn_cur_nparams = 0;
static char  icn_cur_params[8][64];         /* param names, up to 8 */

/* Fill buf with "iconN_<suffix>", return buf */
static char *wfn(char *buf, size_t sz, int id, const char *suffix) {
    snprintf(buf, sz, "icon%d_%s", id, suffix);
    return buf;
}

/* Emit one WAT function that does nothing but return_call another */
static void emit_passthrough(const char *from, const char *to) {
    WI("  (func $%s (result i32)\n", from);
    WI("    return_call $%s)\n", to);
}

/* ── §3  Per-node emitters ────────────────────────────────────────────────── */

/*
 * Each emitter receives:
 *   n        — the IcnNode* being emitted
 *   id       — unique integer for this node instance (from wasm_icon_ctr++)
 *   succ     — name of the WAT function to call on success  (caller's γ)
 *   fail     — name of the WAT function to call on failure  (caller's ω)
 *   resume   — name of the WAT function caller uses to re-enter this node (β)
 *              (this node must emit a function with that name)
 *   start    — name the caller uses to start this node (α)
 *              (this node must emit a function with that name)
 *
 * On return the caller's start/resume function names for this node
 * are the names this function emitted.
 *
 * For Tier 0 we use a simplified signature; richer nodes get more parameters.
 * See the JVM oracle (emit_jvm_icon.c) and irgen.icn for full wiring.
 */

/* ─── ICN_INT — integer literal (paper §4.1) ────────────────────────────────
 * irgen.icn ir_a_Intlit:
 *   start:  lhs ← N; goto success
 *   resume: goto failure          (/bounded only — always emitted for safety)
 *
 * test_icon-4.py:
 *   def x5_start():  x5_V = 5; return x5_succeed
 *   def x5_resume(): return x5_fail
 *
 * WAT encoding (no generator state needed — literals don't backtrack):
 *   (func $iconN_start (result i32)
 *     i64.const <val>
 *     global.set $icn_tmp_int          ;; store value for parent to pick up
 *     return_call $<succ>)
 *   (func $iconN_resume (result i32)
 *     return_call $<fail>)
 *
 * NOTE: For Tier 0 we use a single global $icn_tmp_int to pass integer
 * values between nodes (matches the test_icon-4.py "global x5_V" pattern).
 * This is safe for non-recursive programs; recursive generators need a stack.
 */
static void emit_icn_int(const IcnNode *n, int id,
                         const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");

    WI("  ;; ICN_INT %ld  (node %d)\n", n->val.ival, id);
    WI("  (func $%s (result i32)\n", sa);
    WI("    i64.const %ld\n", n->val.ival);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);

    WI("  (func $%s (result i32)\n", ra);
    WI("    return_call $%s)\n", fail);
}

/* ─── ICN_REAL — real literal ───────────────────────────────────────────────*/
static void emit_icn_real(const IcnNode *n, int id,
                          const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");

    WI("  ;; ICN_REAL %g  (node %d)\n", n->val.fval, id);
    WI("  (func $%s (result i32)\n", sa);
    WI("    f64.const %g\n", n->val.fval);
    WI("    global.set $icn_flt%d\n", id);
    WI("    return_call $%s)\n", succ);

    WI("  (func $%s (result i32)\n", ra);
    WI("    return_call $%s)\n", fail);
}

/* ─── ICN_VAR — variable (paper §4.1, ir_a_Ident) ──────────────────────────
 * start:  lhs ← var[name]; goto success
 * resume: goto failure
 *
 * Variables stored as i64 in the Icon variable table in linear memory.
 * $icn_var_get(name_ptr, name_len) → i64  (runtime function)
 * $icn_var_set(name_ptr, name_len, i64)   (runtime function)
 *
 * For Tier 0 (rung01) we use a simplified direct-address scheme: each variable
 * name is interned to a fixed memory slot at compile time.
 */
static void emit_icn_var(const IcnNode *n, int id,
                         const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");

    WI("  ;; ICN_VAR \"%s\"  (node %d)\n", n->val.sval, id);

    /* M-IW-P01: if name matches a current-proc param, read $icn_param{i} */
    int param_idx = -1;
    for (int i = 0; i < icn_cur_nparams; i++) {
        if (strcmp(n->val.sval, icn_cur_params[i]) == 0) { param_idx = i; break; }
    }

    if (param_idx >= 0) {
        /* Parameter variable: value lives in $icn_param{i} */
        WI("  (func $%s (result i32)\n", sa);
        WI("    global.get $icn_param%d\n", param_idx);
        WI("    global.set $icn_int%d\n", id);
        WI("    return_call $%s)\n", succ);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
    } else {
        /* Local / global variable: stub-fail until local var table (M-IW-V01) */
        WI("  (func $%s (result i32)\n", sa);
        WI("    ;; TODO M-IW-V01: $icn_var_get for \"%s\"\n", n->val.sval);
        WI("    return_call $%s)  ;; stub-fail: local vars not yet impl\n", fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
    }
}

/* ─── ICN_ASSIGN — E1 := E2 (ir_a_Binop \":=\" wiring) ──────────────────────
 * Wiring (funcs set, no backtrack):
 *   start  → E2.start
 *   resume → E2.resume
 *   E2.fail  → assign.fail
 *   E2.succeed: store E2.value into E1.lvalue; goto assign.succeed
 *
 * (Left-hand side must be a variable for Tier 0.)
 */
static void emit_icn_assign(const IcnNode *n, int id,
                             const char *succ, const char *fail) {
    (void)n;
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");

    WI("  ;; ICN_ASSIGN  (node %d) — stub until M-IW-A01\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
}

/* ─── ICN_ADD / SUB / MUL / DIV / MOD (binary arithmetic, funcs set) ────────
 * irgen.icn ir_binary (funcs path):
 *   start  → E1.start
 *   resume → E2.resume
 *   E1.fail  → op.fail
 *   E1.succeed → E2.start
 *   E2.fail  → E1.resume
 *   E2.succeed: op.value ← E1.value OP E2.value; goto op.succeed
 *
 * test_icon-4.py mult:
 *   def mult_start():  return to1_start
 *   def mult_resume(): return to2_resume
 *   def to1_fail():    return mult_fail   ← wired as E1's failure destination
 *   def to2_fail():    return to1_resume  ← wired as E2's failure destination
 *   def to1_succeed(): return to2_start   ← wired as E1's success destination
 *   def to2_succeed(): mult_V = to1_V * to2_V; return mult_succeed
 *
 * In WASM: each of these wiring points is a (func …) that return_calls next.
 * The E1/E2 sub-nodes are emitted recursively; their α/β names are known.
 * We then emit the 6 "glue" functions that connect them.
 */
static void emit_icn_binop(const IcnNode *n, int id,
                           const char *succ, const char *fail,
                           const char *e1_start, const char *e1_resume,
                           const char *e2_start, const char *e2_resume,
                           int e1_val_id, int e2_val_id) {
    (void)n;
    char sa[64], ra[64], e1f[64], e1s[64], e2f[64], e2s[64];
    wfn(sa,  sizeof sa,  id, "start");
    wfn(ra,  sizeof ra,  id, "resume");
    wfn(e1f, sizeof e1f, id, "e1fail");
    wfn(e1s, sizeof e1s, id, "e1succ");
    wfn(e2f, sizeof e2f, id, "e2fail");
    wfn(e2s, sizeof e2s, id, "e2succ");

    const char *op_instr = "i64.mul"; /* default */
    const char *op_comment = "MUL";
    switch (n->kind) {
        case ICN_ADD: op_instr = "i64.add"; op_comment = "ADD"; break;
        case ICN_SUB: op_instr = "i64.sub"; op_comment = "SUB"; break;
        case ICN_MUL: op_instr = "i64.mul"; op_comment = "MUL"; break;
        case ICN_DIV: op_instr = "i64.div_s"; op_comment = "DIV"; break;
        case ICN_MOD: op_instr = "i64.rem_s"; op_comment = "MOD"; break;
        default: break;
    }

    WI("  ;; ICN_%s  (node %d)\n", op_comment, id);
    /* op.start → E1.start */
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);
    /* op.resume → E2.resume */
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e2_resume);
    /* E1.fail → op.fail */
    emit_passthrough(e1f, fail);
    /* E1.succeed → E2.start */
    emit_passthrough(e1s, e2_start);
    /* E2.fail → E1.resume */
    emit_passthrough(e2f, e1_resume);
    /* E2.succeed: compute, store, → op.succeed */
    WI("  (func $%s (result i32)\n", e2s);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    %s\n", op_instr);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

/* ─── ICN_NEG / ICN_POS — unary arithmetic ──────────────────────────────────
 * irgen.icn ir_unary (funcs path, op in {"+","-","~","^","*",".","/","\\"}):
 *   start  → E.start
 *   resume → E.resume
 *   E.fail  → neg.fail
 *   E.succeed: neg.value ← -E.value; goto neg.succeed
 */
static void emit_icn_unop(const IcnNode *n, int id,
                          const char *succ, const char *fail,
                          const char *e_start, const char *e_resume,
                          int e_val_id) {
    char sa[64], ra[64], ef[64], es[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    wfn(ef, sizeof ef, id, "efail");
    wfn(es, sizeof es, id, "esucc");

    WI("  ;; ICN_%s unary  (node %d)\n",
       n->kind == ICN_NEG ? "NEG" : "POS", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    emit_passthrough(ef, fail);
    WI("  (func $%s (result i32)\n", es);
    WI("    global.get $icn_int%d\n", e_val_id);
    if (n->kind == ICN_NEG) { WI("    i64.const -1\n"); WI("    i64.mul\n"); }
    /* ICN_POS: identity — value unchanged */
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

/* ─── ICN_LT/LE/GT/GE/EQ/NE — numeric relational (paper §4.3) ───────────────
 * irgen.icn ir_binary (funcs path):
 *   Same wiring as binop EXCEPT E2.succeed:
 *     if (E1.value >= E2.value) goto E2.resume   ← retry right if compare fails
 *     op.value ← E2.value; goto op.succeed
 *
 * genc() case '<': nop = '>=' → if (E1_V >= E2_V) goto E2_resume
 * test_icon-4.py mult_succeed:
 *   if x5_V <= mult_V: return mult_resume   ← negate and retry
 *   else: greater_V = mult_V; return greater_succeed
 */
static void emit_icn_relop(const IcnNode *n, int id,
                           const char *succ, const char *fail,
                           const char *e1_start, const char *e1_resume,
                           const char *e2_start, const char *e2_resume,
                           int e1_val_id, int e2_val_id) {
    char sa[64], ra[64], e1f[64], e1s[64], e2f[64], e2s[64];
    wfn(sa,  sizeof sa,  id, "start");
    wfn(ra,  sizeof ra,  id, "resume");
    wfn(e1f, sizeof e1f, id, "e1fail");
    wfn(e1s, sizeof e1s, id, "e1succ");
    wfn(e2f, sizeof e2f, id, "e2fail");
    wfn(e2s, sizeof e2s, id, "e2succ");

    /*
     * negated comparison instruction for the "retry" branch:
     *   op=LT → negate is i64.ge_s (if !(E1 < E2) → retry)
     * WAT i64.lt_s returns 1 if E1<E2; we test the negation to retry.
     */
    const char *neg_instr = "i64.ge_s";  /* default for LT */
    const char *op_comment = "LT";
    switch (n->kind) {
        case ICN_LT: neg_instr = "i64.ge_s"; op_comment = "LT"; break;
        case ICN_LE: neg_instr = "i64.gt_s"; op_comment = "LE"; break;
        case ICN_GT: neg_instr = "i64.le_s"; op_comment = "GT"; break;
        case ICN_GE: neg_instr = "i64.lt_s"; op_comment = "GE"; break;
        case ICN_EQ: neg_instr = "i64.ne";   op_comment = "EQ"; break;
        case ICN_NE: neg_instr = "i64.eq";   op_comment = "NE"; break;
        default: break;
    }

    WI("  ;; ICN_%s  (node %d, goal-directed §4.3)\n", op_comment, id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e2_resume);
    emit_passthrough(e1f, fail);
    emit_passthrough(e1s, e2_start);
    emit_passthrough(e2f, e1_resume);
    /* E2.succeed: goal-directed retry */
    WI("  (func $%s (result i32)\n", e2s);
    WI("    global.get $icn_int%d\n", e1_val_id);  /* E1.value */
    WI("    global.get $icn_int%d\n", e2_val_id);  /* E2.value */
    WI("    %s\n", neg_instr);   /* 1 if comparison FAILS (goal not met) */
    WI("    (if (then return_call $%s))\n", e2_resume); /* retry right */
    /* comparison succeeded: op.value ← E2.value; succeed */
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

/* ─── ICN_TO — range generator inline counter (paper §4.4) ──────────────────
 * irgen.icn ir_a_ToBy (simplified to 2-operand case):
 *   to.start  → E1.start
 *   to.resume: to.I += 1; goto to.code
 *   E1.fail   → to.fail
 *   E1.succeed → E2.start
 *   E2.fail   → E1.resume
 *   E2.succeed: to.I ← E1.value; goto to.code
 *   to.code: if (to.I > E2.value) goto E2.resume
 *            to.value ← to.I; goto to.succeed
 *
 * test_icon-4.py:
 *   def to1_start():  return x1_start
 *   def to1_resume(): to1_I += 1; return to1_code
 *   def to1_code():
 *       if to1_I > x2_V: return x2_resume  (= E2_resume)
 *       else: to1_V = to1_I; return to1_succeed
 *   def x1_fail():  return to1_fail
 *   def x2_fail():  return x1_resume
 *   def x1_succeed(): return x2_start
 *   def x2_succeed(): to1_I = x1_V; return to1_code
 *
 * Generator state: slot[0] = to.I (i32 counter at ICON_GEN_STATE_BASE + slot*64)
 * E1 value is in $icn_int{e1_id}; E2 value in $icn_int{e2_id}.
 */
static void emit_icn_to(const IcnNode *n, int id,
                        const char *succ, const char *fail,
                        const char *e1_start, const char *e1_resume,
                        const char *e2_start, const char *e2_resume,
                        int e1_val_id, int e2_val_id) {
    (void)n;
    int slot = icon_alloc_gen_slot();
    int slot_addr = icon_gen_slot_addr(slot);  /* address of to.I in memory */

    char sa[64], ra[64], code[64];
    char e1f[64], e1s[64], e2f[64], e2s[64];
    wfn(sa,   sizeof sa,   id, "start");
    wfn(ra,   sizeof ra,   id, "resume");
    wfn(code, sizeof code, id, "code");
    wfn(e1f,  sizeof e1f,  id, "e1fail");
    wfn(e1s,  sizeof e1s,  id, "e1succ");
    wfn(e2f,  sizeof e2f,  id, "e2fail");
    wfn(e2s,  sizeof e2s,  id, "e2succ");

    WI("  ;; ICN_TO  (node %d, gen-slot %d @ 0x%x)\n", id, slot, slot_addr);

    /* to.start → E1.start */
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);

    /* to.resume: to.I += 1; goto to.code */
    WI("  (func $%s (result i32)\n", ra);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i32.const 1\n");
    WI("    i32.add\n");
    WI("    i32.store\n");   /* mem[slot_addr] = mem[slot_addr] + 1 */
    WI("    return_call $%s)\n", code);

    /* E1.fail → to.fail */
    emit_passthrough(e1f, fail);
    /* E1.succeed → E2.start */
    emit_passthrough(e1s, e2_start);
    /* E2.fail → E1.resume */
    emit_passthrough(e2f, e1_resume);

    /* E2.succeed: to.I ← E1.value; goto to.code */
    WI("  (func $%s (result i32)\n", e2s);
    WI("    i32.const %d\n", slot_addr);
    WI("    global.get $icn_int%d\n", e1_val_id);  /* E1.value (i64) */
    WI("    i32.wrap_i64\n");                       /* truncate to i32 counter */
    WI("    i32.store\n");
    WI("    return_call $%s)\n", code);

    /* to.code: if (to.I > E2.value) goto E2.resume else to.value ← to.I; succeed */
    WI("  (func $%s (result i32)\n", code);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");                      /* to.I */
    WI("    global.get $icn_int%d\n", e2_val_id);   /* E2.value (i64) */
    WI("    i32.wrap_i64\n");
    WI("    i32.gt_s\n");                      /* to.I > E2.value ? */
    WI("    (if (then return_call $%s))\n", e2_resume);  /* exhausted → retry E2 */
    /* to.value ← to.I; goto to.succeed */
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i64.extend_i32_s\n");
    WI("    global.set $icn_int%d\n", id);   /* to.value (carried as int global) */
    WI("    return_call $%s)\n", succ);
}

/* ─── ICN_EVERY — drives generator to exhaustion (irgen.icn ir_a_Every) ─────
 * every E [do body]:
 *   start  → E.start
 *   E.success → body.start  (if body) else → E.resume
 *   body.success → E.resume
 *   body.failure → E.resume
 *   E.failure   → every.failure
 *   (every itself never produces a value in bounded context)
 *
 * For "every write(…)" — no explicit body — body is ICN_CALL(write).
 * JCON irgen: every.resume → ir_IndirectGoto(continue) — deferred to M-IW-G03 full.
 *
 * Tier 0 simplification: every has exactly one expression child (no separate body).
 * The expression is the full E; we just drive it to exhaustion.
 */
static void emit_icn_every(const IcnNode *n, int id,
                           const char *fail,
                           const char *e_start, const char *e_resume,
                           const char *e_succ_target) {
    (void)n;
    char sa[64], ra[64], ef[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    wfn(ef, sizeof ef, id, "efail");

    WI("  ;; ICN_EVERY  (node %d)\n", id);
    /* every.start → E.start */
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    /* every.resume (called when body wants next value) → E.resume */
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    /* E.fail → every.fail (generator exhausted) */
    emit_passthrough(ef, fail);
    /* E.succeed → body.start (or write action, handled by caller-wired e_succ_target) */
    /* The caller wires E's success destination as e_succ_target.
     * After the body (write), it returns_calls every.resume = e_resume.
     * This function is emitted by the CALL(write) node itself. */
    (void)e_succ_target; /* wired externally by parent dispatch */
}

/* ─── ICN_ALT — value alternation E1 | E2 (ir_a_Alt) ────────────────────────
 * irgen.icn:
 *   start  → E1.start
 *   resume → [alt.gate]  (indirect goto — gate holds which branch's resume)
 *   E1.success: gate ← addrOf(E1.resume); goto alt.success
 *   E1.fail    → E2.start
 *   E2.success: gate ← addrOf(E2.resume); goto alt.success
 *   E2.fail    → alt.fail
 *
 * test_icon-4.py style (byrd_box.py genc '|'):
 *   alt_start:  alt_i = 1; goto E1_start
 *   alt_resume: if (alt_i == 1) goto E1_resume; if (alt_i == 2) goto E2_resume
 *   E1_fail:    alt_i = 2; goto E2_start
 *   E1_succeed: alt_value = E1_value; goto alt_succeed
 *   E2_fail:    goto alt_fail
 *   E2_succeed: alt_value = E2_value; goto alt_succeed
 *
 * WASM: use gen-slot[0] = branch index (1=left, 2=right) for indirect resume.
 */
static void emit_icn_alt(const IcnNode *n, int id,
                         const char *succ, const char *fail,
                         const char *e1_start, const char *e1_resume,
                         const char *e2_start, const char *e2_resume,
                         int e1_val_id, int e2_val_id) {
    (void)n;
    int slot = icon_alloc_gen_slot();
    int slot_addr = icon_gen_slot_addr(slot);

    char sa[64], ra[64];
    char e1f[64], e1s[64], e2f[64], e2s[64];
    wfn(sa,  sizeof sa,  id, "start");
    wfn(ra,  sizeof ra,  id, "resume");
    wfn(e1f, sizeof e1f, id, "e1fail");
    wfn(e1s, sizeof e1s, id, "e1succ");
    wfn(e2f, sizeof e2f, id, "e2fail");
    wfn(e2s, sizeof e2s, id, "e2succ");

    WI("  ;; ICN_ALT  (node %d, branch-slot %d @ 0x%x)\n", id, slot, slot_addr);

    /* alt.start: branch_index ← 1; goto E1.start */
    WI("  (func $%s (result i32)\n", sa);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.const 1\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", e1_start);

    /* alt.resume: indirect via branch index */
    WI("  (func $%s (result i32)\n", ra);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i32.const 1\n");
    WI("    i32.eq\n");
    WI("    (if (then return_call $%s))\n", e1_resume);
    WI("    return_call $%s)\n", e2_resume);  /* branch == 2 */

    /* E1.fail: branch_index ← 2; goto E2.start */
    WI("  (func $%s (result i32)\n", e1f);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.const 2\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", e2_start);

    /* E1.succeed: alt.value ← E1.value; goto alt.succeed */
    WI("  (func $%s (result i32)\n", e1s);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);

    /* E2.fail → alt.fail */
    emit_passthrough(e2f, fail);

    /* E2.succeed: alt.value ← E2.value; goto alt.succeed */
    WI("  (func $%s (result i32)\n", e2s);
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

/* ─── ICN_CALL write() — Tier 0 built-in output ─────────────────────────────
 * For rung01 the only call is write(E):
 *   start  → E.start
 *   resume → E.resume
 *   E.fail  → call.fail
 *   E.succeed: output E.value; goto call.succeed
 *
 * arg_kind: the IcnKind of the argument node.
 *   ICN_STR → call $sno_output_str($icn_strlit_off{slit_idx}, $icn_strlit_len{slit_idx})
 *   otherwise → call $sno_output_int($icn_int{e_val_id})
 *
 * Uses $sno_output_int / $sno_output_str (from sno_runtime.wasm, already imported).
 * Full ICN_CALL (user procedures) deferred to M-IW-P01.
 */
static void emit_icn_call_write(int id,
                                const char *succ, const char *fail,
                                const char *e_start, const char *e_resume,
                                int e_val_id,
                                int arg_kind,     /* IcnKind of arg node */
                                int arg_slit_idx) /* strlit index if arg_kind==ICN_STR */ {
    char sa[64], ra[64], ef[64], es[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    wfn(ef, sizeof ef, id, "efail");
    wfn(es, sizeof es, id, "esucc");

    WI("  ;; ICN_CALL write()  (node %d)\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    emit_passthrough(ef, fail);
    WI("  (func $%s (result i32)\n", es);
    if (arg_kind == ICN_STR) {
        WI("    global.get $icn_strlit_off%d\n", arg_slit_idx);
        WI("    global.get $icn_strlit_len%d\n", arg_slit_idx);
        WI("    call $sno_output_str\n");
    } else {
        WI("    global.get $icn_int%d\n", e_val_id);
        WI("    call $sno_output_int\n");
    }
    WI("    return_call $%s)\n", succ);
}

/* ─── ICN_PROC / ICN_RETURN / ICN_FAIL — Tier 1 stubs ───────────────────────
 * Full procedure support (M-IW-P01) requires:
 *   - Each procedure compiled to a named WAT function group
 *   - ICN_RETURN → ir_Succeed(t, null) = call $sno_output_flush + return 0
 *   - ICN_FAIL   → ir_Fail()           = return_call $program_fail
 *
 * Tier 0 (rung01): main() implicitly executes body, no explicit return needed.
 * Emit a stub-fail for now; M-IW-P01 wires properly.
 */
static void emit_icn_proc_stub(int id, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; ICN_PROC stub (node %d) — M-IW-P01\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
}

static void emit_icn_return_stub(int id, const char *fail) {
    char sa[64];
    wfn(sa, sizeof sa, id, "start");
    WI("  ;; ICN_RETURN stub (node %d) — M-IW-P01\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
}

static void emit_icn_fail_stub(int id, const char *fail) {
    char sa[64];
    wfn(sa, sizeof sa, id, "start");
    WI("  ;; ICN_FAIL stub (node %d) — M-IW-P02\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
}

/* ─── Catch-all stub for unimplemented ICN_ nodes ────────────────────────────
 * RULES.md §FRONTEND/BACKEND SEPARATION: when a node can't be emitted yet,
 * emit a stub-fail (not a silent skip) so the gap is visible in test output.
 * The .xfail mechanism tracks which tests hit stubs.
 */
static void emit_icn_stub(const IcnNode *n, int id, const char *fail) {
    static const char *kind_names[] = {
        "INT","REAL","STR","CSET","VAR","ADD","SUB","MUL","DIV","MOD","POW",
        "NEG","POS","RANDOM","COMPLEMENT","CSET_UNION","CSET_DIFF","CSET_INTER",
        "BANG_BINARY","SECTION_PLUS","SECTION_MINUS","LT","LE","GT","GE","EQ","NE",
        "SLT","SLE","SGT","SGE","SEQ","SNE","CONCAT","LCONCAT","TO","TO_BY","ALT",
        "AND","BANG","SIZE","LIMIT","NOT","NONNULL","NULL","SEQ_EXPR","EVERY",
        "WHILE","UNTIL","REPEAT","IF","CASE","ASSIGN","AUGOP","SWAP","IDENTICAL",
        "MATCH","SCAN","SCAN_AUGOP","CALL","RETURN","SUSPEND","FAIL","BREAK","NEXT",
        "PROC","FIELD","SUBSCRIPT","SECTION","MAKELIST","RECORD","GLOBAL","INITIAL"
    };
    const char *kname = (n->kind < ICN_KIND_COUNT) ? kind_names[n->kind] : "UNKNOWN";
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; ICN_%s STUB (node %d) — not yet implemented\n", kname, id);
    WI("  (func $%s (result i32)  return_call $%s)  ;; stub-fail\n", sa, fail);
    WI("  (func $%s (result i32)  return_call $%s)  ;; stub-fail\n", ra, fail);
}

/* ── §4  Recursive expression emitter ────────────────────────────────────── */

/*
 * emit_expr_wasm() — recursive Byrd-box expression emitter.
 *
 * Walks an IcnNode tree depth-first, emitting WAT functions for each node.
 * Threading: succ/fail are the caller's γ/ω ports (string names of WAT funcs).
 * Returns: this node's α (start) name in out_start, β (resume) name in out_resume.
 *
 * All individual emitter functions (emit_icn_int, emit_icn_binop, etc.) are
 * already written above. This dispatcher calls them with correct arguments by
 * first recursively emitting children, then wiring the glue.
 *
 * M-IW-A01: ICN_INT, ICN_REAL, ICN_STR, ICN_VAR (stub), ICN_ASSIGN (stub),
 *           ICN_ADD/SUB/MUL/DIV/MOD, ICN_NEG/POS, ICN_LT/LE/GT/GE/EQ/NE,
 *           ICN_TO, ICN_EVERY, ICN_ALT, ICN_CALL(write), ICN_RETURN, ICN_FAIL.
 */
static void emit_expr_wasm(const IcnNode *n,
                            const char *succ, const char *fail,
                            char *out_start, char *out_resume) {
    if (!n) {
        /* Null node: emit passthrough to fail */
        int id = wasm_icon_ctr++;
        char sa[64], ra[64];
        wfn(sa, sizeof sa, id, "start");
        wfn(ra, sizeof ra, id, "resume");
        WI("  ;; NULL node %d\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        if (out_start)  snprintf(out_start,  64, "%s", sa);
        if (out_resume) snprintf(out_resume, 64, "%s", ra);
        return;
    }

    int id = wasm_icon_ctr++;
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");

    switch (n->kind) {

    /* ── Integer literal ────────────────────────────────────────────────── */
    case ICN_INT:
        emit_icn_int(n, id, succ, fail);
        break;

    /* ── Real literal ───────────────────────────────────────────────────── */
    case ICN_REAL:
        emit_icn_real(n, id, succ, fail);
        break;

    /* ── String literal ─────────────────────────────────────────────────── */
    case ICN_STR: {
        /* M-IW-A02: string value carried as (offset, len) pair.
         * Globals are keyed on the interned literal index (not node id), so
         * they can be declared once in the globals section via prescan.
         * Name pattern: $icn_strlit_off{slit_idx} / $icn_strlit_len{slit_idx}
         * Byrd wiring:
         *   start  → store literal offset+len, tail-call succ  (value emitted)
         *   resume → literal cannot backtrack, tail-call fail
         */
        const char *sv = n->val.sval ? n->val.sval : "";
        int slit_idx = icn_strlit_intern(sv);
        int abs_off  = icn_strlit_abs(slit_idx);
        int slen     = icn_str_lits[slit_idx].len;

        WI("  ;; ICN_STR \"%s\" (node %d) slit=%d offset=%d len=%d\n",
           sv, id, slit_idx, abs_off, slen);
        WI("  (func $%s (result i32)\n", sa);
        WI("    i32.const %d\n", abs_off);
        WI("    global.set $icn_strlit_off%d\n", slit_idx);
        WI("    i32.const %d\n", slen);
        WI("    global.set $icn_strlit_len%d\n", slit_idx);
        WI("    return_call $%s)\n", succ);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;
    }

    /* ── Variable ───────────────────────────────────────────────────────── */
    case ICN_VAR:
        emit_icn_var(n, id, succ, fail);
        break;

    /* ── Assignment ─────────────────────────────────────────────────────── */
    case ICN_ASSIGN:
        emit_icn_assign(n, id, succ, fail);
        break;

    /* ── Unary arithmetic ───────────────────────────────────────────────── */
    case ICN_NEG:
    case ICN_POS: {
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        /* unop: child's succ = our esucc (which applies op then calls outer succ) */
        char esucc[64], efail[64];
        wfn(esucc, sizeof esucc, id, "esucc");
        wfn(efail, sizeof efail, id, "efail");
        char e_start[64], e_resume[64];
        int e_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[0], esucc, efail, e_start, e_resume);
        emit_icn_unop(n, id, succ, fail, e_start, e_resume, e_id);
        break;
    }

    /* ── Binary arithmetic ──────────────────────────────────────────────── */
    case ICN_ADD: case ICN_SUB: case ICN_MUL:
    case ICN_DIV: case ICN_MOD: {
        if (n->nchildren < 2) { emit_icn_stub(n, id, fail); break; }
        /* Pre-compute glue names so children wire into them correctly. */
        char e1f[64], e1s[64], e2f[64], e2s[64];
        wfn(e1f, sizeof e1f, id, "e1fail");
        wfn(e1s, sizeof e1s, id, "e1succ");
        wfn(e2f, sizeof e2f, id, "e2fail");
        wfn(e2s, sizeof e2s, id, "e2succ");
        char e1_start[64], e1_resume[64], e2_start[64], e2_resume[64];
        int e1_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[0], e1s, e1f, e1_start, e1_resume);
        int e2_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[1], e2s, e2f, e2_start, e2_resume);
        emit_icn_binop(n, id, succ, fail,
                       e1_start, e1_resume, e2_start, e2_resume,
                       e1_id, e2_id);
        break;
    }

    /* ── Numeric relational ─────────────────────────────────────────────── */
    case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE:
    case ICN_EQ: case ICN_NE: {
        if (n->nchildren < 2) { emit_icn_stub(n, id, fail); break; }
        char e1f[64], e1s[64], e2f[64], e2s[64];
        wfn(e1f, sizeof e1f, id, "e1fail");
        wfn(e1s, sizeof e1s, id, "e1succ");
        wfn(e2f, sizeof e2f, id, "e2fail");
        wfn(e2s, sizeof e2s, id, "e2succ");
        char e1_start[64], e1_resume[64], e2_start[64], e2_resume[64];
        int e1_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[0], e1s, e1f, e1_start, e1_resume);
        int e2_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[1], e2s, e2f, e2_start, e2_resume);
        emit_icn_relop(n, id, succ, fail,
                       e1_start, e1_resume, e2_start, e2_resume,
                       e1_id, e2_id);
        break;
    }

    /* ── Range generator: E1 to E2 ─────────────────────────────────────── */
    case ICN_TO: {
        if (n->nchildren < 2) { emit_icn_stub(n, id, fail); break; }
        char e1f[64], e1s[64], e2f[64], e2s[64];
        wfn(e1f, sizeof e1f, id, "e1fail");
        wfn(e1s, sizeof e1s, id, "e1succ");
        wfn(e2f, sizeof e2f, id, "e2fail");
        wfn(e2s, sizeof e2s, id, "e2succ");
        char e1_start[64], e1_resume[64], e2_start[64], e2_resume[64];
        int e1_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[0], e1s, e1f, e1_start, e1_resume);
        int e2_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[1], e2s, e2f, e2_start, e2_resume);
        emit_icn_to(n, id, succ, fail,
                    e1_start, e1_resume, e2_start, e2_resume,
                    e1_id, e2_id);
        break;
    }

    /* ── Value alternation: E1 | E2 ─────────────────────────────────────── */
    case ICN_ALT: {
        if (n->nchildren < 2) { emit_icn_stub(n, id, fail); break; }
        char e1f[64], e1s[64], e2f[64], e2s[64];
        wfn(e1f, sizeof e1f, id, "e1fail");
        wfn(e1s, sizeof e1s, id, "e1succ");
        wfn(e2f, sizeof e2f, id, "e2fail");
        wfn(e2s, sizeof e2s, id, "e2succ");
        char e1_start[64], e1_resume[64], e2_start[64], e2_resume[64];
        int e1_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[0], e1s, e1f, e1_start, e1_resume);
        int e2_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[1], e2s, e2f, e2_start, e2_resume);
        emit_icn_alt(n, id, succ, fail,
                     e1_start, e1_resume, e2_start, e2_resume,
                     e1_id, e2_id);
        break;
    }

    /* ── every E ────────────────────────────────────────────────────────── */
    case ICN_EVERY: {
        /* every E — drives generator E to exhaustion.
         * E's success target loops back to E's resume (drives next value).
         * every.fail is the overall fail port.
         *
         * For rung01: every write(E) — E is ICN_CALL(write, arg).
         * The write call succeeds, then every resumes E.
         * When E is exhausted, every fails (falls through to succ of the stmt
         * which is typically proc-end). */
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }

        /* every.resume WAT function name — must be known before emitting E
         * because E's success points back to every.resume. */
        /* We emit every's resume func after E, forward-referencing every.resume
         * by name. WAT allows forward references within a module. */

        /* Emit E with:
         *   E.succ → every.resume (loop back to get next value)
         *   E.fail → every.fail (= succ of every, since every succeeds when exhausted) */
        char every_resume[64];
        wfn(every_resume, sizeof every_resume, id, "resume");
        char every_fail[64];
        wfn(every_fail, sizeof every_fail, id, "efail");

        char e_start[64], e_resume[64];
        emit_expr_wasm(n->children[0], every_resume, every_fail,
                       e_start, e_resume);

        /* every.start → E.start */
        WI("  ;; ICN_EVERY  (node %d)\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
        /* every.resume → E.resume (drives next iteration) */
        WI("  (func $%s (result i32)  return_call $%s)\n", every_resume, e_resume);
        /* every.efail → succ (generator exhausted = every succeeded) */
        WI("  (func $%s (result i32)  return_call $%s)\n", every_fail, succ);
        /* every.resume is already emitted above (same as ra) — ra IS every_resume */
        /* ra = iconN_resume = every_resume — already emitted above, don't redeclare */
        /* Fix: rename ra to not conflict */
        snprintf(ra, sizeof ra, "%s", every_resume); /* ra = every_resume */
        break;
    }

    /* ── Procedure call ─────────────────────────────────────────────────── */
    case ICN_CALL: {
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        IcnNode *fn_node = n->children[0];
        const char *fname = (fn_node->kind == ICN_VAR) ? fn_node->val.sval : "unknown";
        int nargs = n->nchildren - 1;

        /* write() builtin: Tier 0 */
        if (fname && strcmp(fname, "write") == 0 && nargs >= 1) {
            /* The arg's success port must be call's own esucc (which runs output).
             * NOT the outer succ — that would bypass the output call. */
            char esucc_name[64];
            wfn(esucc_name, sizeof esucc_name, id, "esucc");
            char e_start[64], e_resume[64];
            int e_id = wasm_icon_ctr;
            IcnNode *arg_node = n->children[1];
            int arg_kind = arg_node ? arg_node->kind : ICN_INT;
            /* For ICN_STR: get slit_idx now (before emit_expr_wasm interns it
             * again — intern is idempotent, same string → same index). */
            int arg_slit = 0;
            if (arg_kind == ICN_STR && arg_node->val.sval)
                arg_slit = icn_strlit_intern(arg_node->val.sval);
            emit_expr_wasm(arg_node, esucc_name, fail, e_start, e_resume);
            emit_icn_call_write(id, succ, fail, e_start, e_resume,
                                e_id, arg_kind, arg_slit);
            break;
        }
        /* write() with no args: write newline — just succeed */
        if (fname && strcmp(fname, "write") == 0 && nargs == 0) {
            WI("  ;; ICN_CALL write() no args (node %d)\n", id);
            WI("  (func $%s (result i32)\n", sa);
            WI("    ;; write() no-arg: output newline (M-IW-S01 full impl)\n");
            WI("    return_call $%s)\n", succ);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
            break;
        }
        /* All other calls: user procedure (M-IW-P01) */
        if (fname && fname[0] != '\0' && strcmp(fname, "unknown") != 0) {
            /* Evaluate each arg into $icn_param{i}, then call proc's _start.
             * After proc returns (esucc), read $icn_retval → $icn_int{id}. */
            char esucc[64];
            wfn(esucc, sizeof esucc, id, "esucc");

            /* Chain: sa → arg0_start → ... → argN_start → param_store → proc_start → esucc → succ */
            /* Build arg eval chain in order */
            char chain_entry[64];
            snprintf(chain_entry, sizeof chain_entry, "%s", sa);

            /* We'll build the chain via a small glue func per arg */
            for (int ai = 0; ai < nargs; ai++) {
                IcnNode *arg = n->children[1 + ai];
                char arg_start[64], arg_resume[64];
                char arg_esucc[64];
                snprintf(arg_esucc, sizeof arg_esucc, "icon%d_arg%d_esucc", id, ai);
                /* Next in chain: either next arg's start or the final call */
                char next_chain[64];
                if (ai == nargs - 1)
                    snprintf(next_chain, sizeof next_chain, "icon%d_docall", id);
                else
                    snprintf(next_chain, sizeof next_chain, "icon%d_arg%d_next", id, ai + 1);

                emit_expr_wasm(arg, arg_esucc, fail, arg_start, arg_resume);
                int arg_id = wasm_icon_ctr - 1;

                /* arg_esucc: store result into $icn_param{ai}, then next */
                WI("  (func $%s (result i32)\n", arg_esucc);
                WI("    global.get $icn_int%d\n", arg_id);
                WI("    global.set $icn_param%d\n", ai);
                WI("    return_call $%s)\n", next_chain);

                if (ai == 0) {
                    /* sa → first arg's start */
                    WI("  (func $%s (result i32)  return_call $%s)\n", sa, arg_start);
                } else {
                    WI("  (func $%s (result i32)  return_call $%s)\n", chain_entry, arg_start);
                }
                snprintf(chain_entry, sizeof chain_entry, "%s", next_chain);
            }

            /* docall: call the procedure */
            WI("  (func $icon%d_docall (result i32)  return_call $icn_proc_%s_start)\n", id, fname);

            /* esucc: read $icn_retval → $icn_int{id}, then succ */
            WI("  (func $%s (result i32)\n", esucc);
            WI("    global.get $icn_retval\n");
            WI("    global.set $icn_int%d\n", id);
            WI("    return_call $%s)\n", succ);

            if (nargs == 0) {
                /* No args: sa goes directly to call */
                WI("  (func $%s (result i32)  return_call $icn_proc_%s_start)\n", sa, fname);
                /* esucc already emitted above */
                /* But we need proc to return_call to esucc, not succ.
                 * The proc's succ port needs to be esucc — handled by how
                 * emit_wasm_icon_proc chains: last stmt succ = icn_prog_end.
                 * For user-proc calls we can't redirect that here retroactively.
                 * Workaround: route through esucc at docall level. */
            }

            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
            break;
        }
        /* Unknown call: stub-fail */
        emit_icn_stub(n, id, fail);
        break;
    }

    /* ── return [E] ─────────────────────────────────────────────────────── */
    case ICN_RETURN:
        WI("  ;; ICN_RETURN (node %d)\n", id);
        if (strcmp(icn_cur_proc_name, "main") != 0 && icn_cur_proc_name[0] != '\0') {
            /* Non-main procedure: evaluate return expr (child[0] if present),
             * store result in $icn_retval, then return_call to caller's succ. */
            if (n->nchildren >= 1) {
                char e_start[64], e_resume[64];
                char esucc[64];
                wfn(esucc, sizeof esucc, id, "esucc");
                emit_expr_wasm(n->children[0], esucc, fail, e_start, e_resume);
                int e_id = /* child was just emitted; its id = wasm_icon_ctr-1 — use e_start node id */
                    wasm_icon_ctr - 1;
                /* esucc: store child value into $icn_retval, then jump to succ */
                WI("  (func $%s (result i32)\n", esucc);
                WI("    global.get $icn_int%d\n", e_id);
                WI("    global.set $icn_retval\n");
                WI("    return_call $%s)\n", succ);
                WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
            } else {
                /* return with no expr: store 0 */
                WI("  (func $%s (result i32)\n", sa);
                WI("    i64.const 0\n");
                WI("    global.set $icn_retval\n");
                WI("    return_call $%s)\n", succ);
            }
        } else {
            /* main(): flush output and jump to prog_end (succ = icn_prog_end) */
            WI("  (func $%s (result i32)  return_call $%s)\n", sa, succ);
        }
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;

    /* ── fail ───────────────────────────────────────────────────────────── */
    case ICN_FAIL:
        WI("  ;; ICN_FAIL (node %d)\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;

    /* ── Unimplemented: stub-fail with .xfail visibility ────────────────── */
    default:
        emit_icn_stub(n, id, fail);
        break;
    }

    if (out_start)  snprintf(out_start,  64, "%s", sa);
    if (out_resume) snprintf(out_resume, 64, "%s", ra);
}

/* ── §5  Public entry points ──────────────────────────────────────────────── */

/*
 * emit_wasm_icon_node() — legacy hook for emit_wasm.c routing.
 * Returns 1 if kind is an ICN_* node handled here, 0 otherwise.
 * (Not used for the primary file-level path; kept for compatibility.)
 */
int emit_wasm_icon_node(const IcnNode *n, FILE *out) {
    if (!n) return 0;
    emit_wasm_icon_set_out(out);
    return (n->kind >= ICN_INT && n->kind < ICN_KIND_COUNT);
}

/*
 * emit_wasm_icon_globals() — emit WAT (global …) declarations.
 * Called once by emit_wasm_icon_file() before the function section.
 */
void emit_wasm_icon_globals(FILE *out) {
    emit_wasm_icon_set_out(out);
    WI("  ;; Icon node-value globals (test_icon-4.py: 'global x_V')\n");
    for (int i = 0; i < 64; i++)
        WI("  (global $icn_int%d (mut i64) (i64.const 0))\n", i);
    for (int i = 0; i < 16; i++)
        WI("  (global $icn_flt%d (mut f64) (f64.const 0))\n", i);
    WI("  ;; Icon generator state memory at 0x%x (%d slots × %d bytes)\n",
       ICON_GEN_STATE_BASE, ICON_GEN_MAX_SLOTS, ICON_GEN_SLOT_BYTES);
    /* M-IW-P01: procedure call/return globals.
     * Calling convention: caller stores args in $icn_param0..N before
     * return_call to callee's _start.  Callee stores return value in
     * $icn_retval before return_call to the call-site's esucc. */
    WI("  ;; M-IW-P01: proc call/return globals\n");
    WI("  (global $icn_retval (mut i64) (i64.const 0))\n");
    for (int i = 0; i < 8; i++)
        WI("  (global $icn_param%d (mut i64) (i64.const 0))\n", i);
}

/* emit_wasm_icon_str_globals() — emit one (offset,len) global pair per interned string.
 * Called after prescan (icn_prescan_node) has populated icn_str_lits[].
 * Must be called after emit_wasm_icon_globals() and before any function emission.
 * M-IW-A02.
 */
void emit_wasm_icon_str_globals(FILE *out) {
    emit_wasm_icon_set_out(out);
    if (icn_str_nlit == 0) return;
    WI("  ;; Icon string literal (offset,len) globals — M-IW-A02\n");
    for (int i = 0; i < icn_str_nlit; i++) {
        WI("  (global $icn_strlit_off%d (mut i32) (i32.const 0))\n", i);
        WI("  (global $icn_strlit_len%d (mut i32) (i32.const 0))\n", i);
    }
}

/*
 * is_icon_node() — true if kind is an ICN_* node.
 */
int is_icon_node(int kind) {
    return (kind >= ICN_INT && kind < ICN_KIND_COUNT);
}

/* ── Emit one ICN_PROC as a WAT function group ───────────────────────────── */
/*
 * ICN_PROC layout (from emit_x64_icon.c analysis):
 *   children[0]          = ICN_VAR name node  (val.sval = proc name)
 *   val.ival             = number of params
 *   children[1..nparams] = param nodes
 *   children[1+nparams..nchildren-1] = body statement nodes
 *
 * For Tier 0 (rung01): main() has 0 params, body is a sequence of stmts.
 * We emit each body statement as an expression, chaining succ ports sequentially.
 * Final stmt's succ = "icn_prog_end"; all fail ports = "icn_program_fail".
 */
static void emit_wasm_icon_proc(const IcnNode *proc) {
    if (!proc || proc->kind != ICN_PROC || proc->nchildren < 1) return;

    const char *pname = proc->children[0]->val.sval;
    int nparams = (int)proc->val.ival;
    int body_start = 1 + nparams;
    int nstmts = proc->nchildren - body_start;

    WI("\n  ;; ── Procedure %s (%d params, %d stmts) ──\n", pname, nparams, nstmts);

    /* Reset per-procedure state.
     * icon_gen_slot_next: reset each proc — generator slots are per-proc.
     * wasm_icon_ctr: NOT reset — node ids must be globally unique across all
     *   procs so WAT func names like $icon0_start don't collide. (IW-5 fix.) */
    icon_gen_slot_next = 0;

    /* M-IW-P01: set current-proc context for ICN_RETURN / ICN_VAR */
    snprintf(icn_cur_proc_name, sizeof icn_cur_proc_name, "%s", pname);
    icn_cur_nparams = (nparams < 8) ? nparams : 8;
    for (int i = 0; i < icn_cur_nparams; i++) {
        IcnNode *pnode = proc->children[1 + i];
        const char *pn = (pnode && pnode->val.sval) ? pnode->val.sval : "";
        snprintf(icn_cur_params[i], 64, "%s", pn);
    }

    if (nstmts == 0) {
        /* Empty body: just emit a start→prog_end function */
        WI("  (func $icn_proc_%s_start (result i32)  return_call $icn_prog_end)\n", pname);
        return;
    }

    /* Build the chain: stmt[i].succ = stmt[i+1].start
     * Last stmt.succ = icn_prog_end
     * All stmt.fail  = icn_program_fail
     *
     * We emit statements in reverse order so each stmt knows the next start name.
     * But WAT allows forward references, so we can also emit forward.
     * Strategy: emit all stmts front-to-back, building name arrays.
     */

    /* Arrays to hold each stmt's start/resume names (allocated on stack — max 64 stmts) */
    #define MAX_STMTS_PER_PROC 64
    char stmt_start [MAX_STMTS_PER_PROC][64];
    char stmt_resume[MAX_STMTS_PER_PROC][64];

    if (nstmts > MAX_STMTS_PER_PROC) {
        WI("  ;; WARNING: too many stmts in %s (%d > %d)\n", pname, nstmts, MAX_STMTS_PER_PROC);
        nstmts = MAX_STMTS_PER_PROC;
    }

    /* For each stmt, determine its succ port (= next stmt's start, or prog_end) */
    /* We do two passes: first emit all exprs (which fills stmt_start/resume),
     * then emit the chain glue. But emit_expr_wasm emits WAT immediately.
     * Solution: emit in order, using forward references. WAT modules allow
     * function forward references — they're just identifiers resolved at link time. */

    /* Pass 1: emit all stmt expressions.
     * Each stmt i has:
     *   succ = "icn_stmt_chain_N_i" (a glue func we emit after)
     *   fail = "icn_program_fail"
     * After pass 1 we know all start/resume names.
     * Then emit the chain glue funcs. */

    char chain_names[MAX_STMTS_PER_PROC][64];
    for (int i = 0; i < nstmts; i++)
        snprintf(chain_names[i], 64, "icn_%s_chain%d", pname, i);

    for (int i = 0; i < nstmts; i++) {
        const IcnNode *stmt = proc->children[body_start + i];
        /* succ of this stmt = chain_names[i] which routes to next stmt or prog_end */
        emit_expr_wasm(stmt, chain_names[i], "icn_program_fail",
                       stmt_start[i], stmt_resume[i]);
    }

    /* Pass 2: emit chain glue functions.
     * chain[i] → stmt[i+1].start  (or icn_prog_end for last) */
    for (int i = 0; i < nstmts; i++) {
        const char *next = (i + 1 < nstmts) ? stmt_start[i+1] : "icn_prog_end";
        WI("  (func $%s (result i32)  return_call $%s)  ;; chain %d→%d\n",
           chain_names[i], next, i, i+1);
    }

    /* Procedure entry: icn_proc_NAME_start → first stmt's start */
    WI("  (func $icn_proc_%s_start (result i32)  return_call $%s)\n",
       pname, stmt_start[0]);
}

/*
 * emit_wasm_icon_file() — top-level entry point for Icon × WASM compilation.
 *
 * Called from main.c when -icn -wasm flags are both set.
 * Emits a complete .wat module for a file containing one or more ICN_PROC nodes.
 *
 * Module structure:
 *   1. imports from "sno" namespace (shared runtime — same as SNOBOL4 WASM)
 *   2. globals: $icn_int0..$icn_int63, $icn_flt0..$icn_flt15
 *   3. Per-procedure function groups (emitted by emit_wasm_icon_proc)
 *   4. Terminal functions: $icn_prog_end (flush+return), $icn_program_fail (return 0)
 *   5. Exported "main" function that calls the main procedure entry
 *
 * M-IW-A01 (2026-03-30)
 */
void emit_wasm_icon_file(IcnNode **procs, int count, FILE *out,
                          const char *filename) {
    (void)filename;
    emit_wasm_icon_set_out(out);

    /* Prescan: intern all ICN_STR literals so globals can be declared before funcs */
    icn_strlit_reset();
    for (int i = 0; i < count; i++)
        icn_prescan_node(procs[i]);

    WI(";; Generated by scrip-cc -icn -wasm (M-IW-A02)\n");
    WI("(module\n");

    /* 1. Runtime imports (shared "sno" namespace) */
    WI("  ;; Memory imported from runtime module\n");
    WI("  (import \"sno\" \"memory\" (memory 2))  ;; page0=runtime page1=str literals\n");
    WI("  ;; Runtime function imports\n");
    WI("  (import \"sno\" \"sno_output_str\"   (func $sno_output_str   (param i32 i32)))\n");
    WI("  (import \"sno\" \"sno_output_int\"   (func $sno_output_int   (param i64)))\n");
    WI("  (import \"sno\" \"sno_output_flush\" (func $sno_output_flush (result i32)))\n");
    WI("  (import \"sno\" \"sno_str_alloc\"    (func $sno_str_alloc    (param i32) (result i32)))\n");
    WI("  (import \"sno\" \"sno_str_concat\"   (func $sno_str_concat   (param i32 i32 i32 i32) (result i32 i32)))\n");
    WI("  (import \"sno\" \"sno_str_eq\"       (func $sno_str_eq       (param i32 i32 i32 i32) (result i32)))\n");
    WI("  (import \"sno\" \"sno_str_to_int\"   (func $sno_str_to_int   (param i32 i32) (result i64)))\n");
    WI("  (import \"sno\" \"sno_int_to_str\"   (func $sno_int_to_str   (param i64) (result i32 i32)))\n");
    WI("  (import \"sno\" \"sno_float_to_str\" (func $sno_float_to_str (param f64) (result i32 i32)))\n");
    WI("  (import \"sno\" \"sno_pow\"          (func $sno_pow          (param f64 f64) (result f64)))\n");

    /* 2. Per-node value globals + string literal (offset,len) globals */
    emit_wasm_icon_globals(out);
    emit_wasm_icon_str_globals(out);

    /* 3. String literal data segment (page 1 = offset 65536) */
    icn_emit_data_segment();

    /* 4. Emit all procedures */
    for (int i = 0; i < count; i++) {
        if (procs[i] && procs[i]->kind == ICN_PROC) {
            emit_wasm_icon_proc(procs[i]);
        }
    }

    /* 4. Terminal functions */
    WI("\n  ;; ── Terminal functions ──\n");
    /* icn_prog_end: flush output buffer, return byte-count to runner */
    WI("  (func $icn_prog_end (result i32)\n");
    WI("    call $sno_output_flush)\n");
    /* icn_program_fail: generator exhaustion = normal end (return 0 bytes) */
    WI("  (func $icn_program_fail (result i32)\n");
    WI("    call $sno_output_flush)\n");

    /* 5. Exported main: find and call the "main" procedure */
    WI("\n  ;; ── Exported main entry ──\n");
    WI("  (func (export \"main\") (result i32)\n");
    /* Find main procedure */
    int found_main = 0;
    for (int i = 0; i < count; i++) {
        if (procs[i] && procs[i]->kind == ICN_PROC &&
            procs[i]->nchildren >= 1 &&
            procs[i]->children[0]->val.sval &&
            strcmp(procs[i]->children[0]->val.sval, "main") == 0) {
            WI("    return_call $icn_proc_main_start)\n");
            found_main = 1;
            break;
        }
    }
    if (!found_main) {
        WI("    ;; no main procedure found\n");
        WI("    call $sno_output_flush)\n");
    }

    WI(")\n");
}
