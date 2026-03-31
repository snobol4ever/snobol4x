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
 *
 * IW-8: Rewritten to consume EXPR_t** (lowered IR from icon_lower.c).
 *   ICN_PROC  → E_FNC (proc decl: sval=name, ival=nparams)
 *   ICN_CALL  → E_FNC (call: sval=fname, children[0]=E_VAR name, [1..]=args)
 *   ICN_INT   → E_ILIT (ival)
 *   ICN_REAL  → E_FLIT (fval)
 *   ICN_STR   → E_QLIT (sval)
 *   ICN_VAR   → E_VAR  (sval)
 *   ICN_ADD   → E_ADD, ICN_SUB → E_SUB, ICN_MUL → E_MPY
 *   ICN_DIV   → E_DIV, ICN_MOD → E_MOD, ICN_NEG → E_NEG
 *   ICN_LT    → E_LT, LE→E_LE, GT→E_GT, GE→E_GE, EQ→E_EQ, NE→E_NE
 *   ICN_TO    → E_TO, ICN_ALT → E_GENALT
 *   ICN_EVERY → E_EVERY, ICN_RETURN → E_RETURN, ICN_FAIL → E_FAIL
 *   ICN_ASSIGN→ E_ASSIGN
 *
 * RULES.md §BYRD BOXES: emit labels+gotos, never interpret IR nodes at emit-time.
 */

#include "icon_ast.h"
#include "icon_emit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── §1  WAT output macros ────────────────────────────────────────────────── */

static FILE *icon_wasm_out = NULL;

void emit_wasm_icon_set_out(FILE *f) { icon_wasm_out = f; }

#define WI(fmt, ...)  fprintf(icon_wasm_out, fmt, ##__VA_ARGS__)

/* Generator state memory layout — same as before (IW-2) */
#define ICON_GEN_STATE_BASE  0xC000
#define ICON_GEN_SLOT_BYTES  64
#define ICON_GEN_MAX_SLOTS   256

static int icon_gen_slot_next = 0;

/* M-IW-P01: funcref table for call-site esucc trampolines.
 * Each user-proc call site registers its esucc WAT func name here.
 * At module end we emit (table N funcref) + (elem ...).
 * $icn_retcont global holds the table index the callee should return_call_indirect to. */
#define ICN_RETCONT_MAX 256
static char icn_retcont_funcs[ICN_RETCONT_MAX][64];
static int  icn_retcont_count = 0;

static void icn_retcont_reset(void) { icn_retcont_count = 0; }

/* Register an esucc func name; return its table index. */
static int icn_retcont_register(const char *fname) {
    for (int i = 0; i < icn_retcont_count; i++)
        if (strcmp(icn_retcont_funcs[i], fname) == 0) return i;
    if (icn_retcont_count >= ICN_RETCONT_MAX) return 0;
    int idx = icn_retcont_count++;
    snprintf(icn_retcont_funcs[idx], 64, "%s", fname);
    return idx;
}

/* ── §1b  String literal intern table ────────────────────────────────────── */
#define ICN_STR_DATA_BASE  65536
#define ICN_MAX_STRLITS    1024

typedef struct { char *text; int len; int offset; } IcnStrLit;
static IcnStrLit icn_str_lits[ICN_MAX_STRLITS];
static int       icn_str_nlit  = 0;
static int       icn_str_bytes = 0;

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

/* Pre-scan EXPR_t tree and intern every E_QLIT string. */
static void icn_prescan_node(const EXPR_t *n) {
    if (!n) return;
    if (n->kind == E_QLIT && n->sval)
        icn_strlit_intern(n->sval);
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

/* ── §2  Label helpers ────────────────────────────────────────────────────── */

static int wasm_icon_ctr = 0;

static char  icn_cur_proc_name[128] = "";
static int   icn_cur_nparams = 0;
static char  icn_cur_params[8][64];

static char *wfn(char *buf, size_t sz, int id, const char *suffix) {
    snprintf(buf, sz, "icon%d_%s", id, suffix);
    return buf;
}

static void emit_passthrough(const char *from, const char *to) {
    WI("  (func $%s (result i32)\n", from);
    WI("    return_call $%s)\n", to);
}

/* ── §3  Per-node emitters (operate on EXPR_t*) ───────────────────────────── */

static void emit_icn_int(const EXPR_t *n, int id,
                         const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; E_ILIT %lld  (node %d)\n", (long long)n->ival, id);
    WI("  (func $%s (result i32)\n", sa);
    WI("    i64.const %lld\n", (long long)n->ival);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
    WI("  (func $%s (result i32)\n", ra);
    WI("    return_call $%s)\n", fail);
}

static void emit_icn_real(const EXPR_t *n, int id,
                          const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; E_FLIT %g  (node %d)\n", n->dval, id);
    WI("  (func $%s (result i32)\n", sa);
    WI("    f64.const %g\n", n->dval);
    WI("    global.set $icn_flt%d\n", id);
    WI("    return_call $%s)\n", succ);
    WI("  (func $%s (result i32)\n", ra);
    WI("    return_call $%s)\n", fail);
}

static void emit_icn_var(const EXPR_t *n, int id,
                         const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; E_VAR \"%s\"  (node %d)\n", n->sval ? n->sval : "", id);

    int param_idx = -1;
    for (int i = 0; i < icn_cur_nparams; i++) {
        if (n->sval && strcmp(n->sval, icn_cur_params[i]) == 0) { param_idx = i; break; }
    }

    if (param_idx >= 0) {
        WI("  (func $%s (result i32)\n", sa);
        WI("    global.get $icn_param%d\n", param_idx);
        WI("    global.set $icn_int%d\n", id);
        WI("    return_call $%s)\n", succ);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
    } else {
        WI("  (func $%s (result i32)\n", sa);
        WI("    ;; TODO M-IW-V01: $icn_var_get for \"%s\"\n", n->sval ? n->sval : "");
        WI("    return_call $%s)  ;; stub-fail: local vars not yet impl\n", fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
    }
}

static void emit_icn_assign(const EXPR_t *n, int id,
                             const char *succ, const char *fail) {
    (void)n;
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; E_ASSIGN  (node %d) — stub until M-IW-A01\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
}

static void emit_icn_binop(const EXPR_t *n, int id,
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

    const char *op_instr = "i64.mul";
    const char *op_comment = "MUL";
    switch (n->kind) {
        case E_ADD: op_instr = "i64.add";   op_comment = "ADD"; break;
        case E_SUB: op_instr = "i64.sub";   op_comment = "SUB"; break;
        case E_MPY: op_instr = "i64.mul";   op_comment = "MUL"; break;
        case E_DIV: op_instr = "i64.div_s"; op_comment = "DIV"; break;
        case E_MOD: op_instr = "i64.rem_s"; op_comment = "MOD"; break;
        default: break;
    }

    WI("  ;; E_%s  (node %d)\n", op_comment, id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e2_resume);
    emit_passthrough(e1f, fail);
    emit_passthrough(e1s, e2_start);
    emit_passthrough(e2f, e1_resume);
    WI("  (func $%s (result i32)\n", e2s);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    %s\n", op_instr);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

static void emit_icn_unop(const EXPR_t *n, int id,
                          const char *succ, const char *fail,
                          const char *e_start, const char *e_resume,
                          int e_val_id) {
    char sa[64], ra[64], ef[64], es[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    wfn(ef, sizeof ef, id, "efail");
    wfn(es, sizeof es, id, "esucc");

    WI("  ;; E_%s unary  (node %d)\n",
       n->kind == E_NEG ? "NEG" : "POS", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    emit_passthrough(ef, fail);
    WI("  (func $%s (result i32)\n", es);
    WI("    global.get $icn_int%d\n", e_val_id);
    if (n->kind == E_NEG) { WI("    i64.const -1\n"); WI("    i64.mul\n"); }
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

static void emit_icn_relop(const EXPR_t *n, int id,
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

    const char *neg_instr = "i64.ge_s";
    const char *op_comment = "LT";
    switch (n->kind) {
        case E_LT: neg_instr = "i64.ge_s"; op_comment = "LT"; break;
        case E_LE: neg_instr = "i64.gt_s"; op_comment = "LE"; break;
        case E_GT: neg_instr = "i64.le_s"; op_comment = "GT"; break;
        case E_GE: neg_instr = "i64.lt_s"; op_comment = "GE"; break;
        case E_EQ: neg_instr = "i64.ne";   op_comment = "EQ"; break;
        case E_NE: neg_instr = "i64.eq";   op_comment = "NE"; break;
        default: break;
    }

    WI("  ;; E_%s  (node %d, goal-directed)\n", op_comment, id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e2_resume);
    emit_passthrough(e1f, fail);
    emit_passthrough(e1s, e2_start);
    emit_passthrough(e2f, e1_resume);
    WI("  (func $%s (result i32)\n", e2s);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    %s\n", neg_instr);
    WI("    (if (then return_call $%s))\n", e2_resume);
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

static void emit_icn_to(const EXPR_t *n, int id,
                        const char *succ, const char *fail,
                        const char *e1_start, const char *e1_resume,
                        const char *e2_start, const char *e2_resume,
                        int e1_val_id, int e2_val_id) {
    (void)n;
    int slot = icon_alloc_gen_slot();
    int slot_addr = icon_gen_slot_addr(slot);

    char sa[64], ra[64], code[64];
    char e1f[64], e1s[64], e2f[64], e2s[64];
    wfn(sa,   sizeof sa,   id, "start");
    wfn(ra,   sizeof ra,   id, "resume");
    wfn(code, sizeof code, id, "code");
    wfn(e1f,  sizeof e1f,  id, "e1fail");
    wfn(e1s,  sizeof e1s,  id, "e1succ");
    wfn(e2f,  sizeof e2f,  id, "e2fail");
    wfn(e2s,  sizeof e2s,  id, "e2succ");

    WI("  ;; E_TO  (node %d, gen-slot %d @ 0x%x)\n", id, slot, slot_addr);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);
    WI("  (func $%s (result i32)\n", ra);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i32.const 1\n");
    WI("    i32.add\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", code);
    emit_passthrough(e1f, fail);
    emit_passthrough(e1s, e2_start);
    emit_passthrough(e2f, e1_resume);
    WI("  (func $%s (result i32)\n", e2s);
    WI("    i32.const %d\n", slot_addr);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    i32.wrap_i64\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", code);
    WI("  (func $%s (result i32)\n", code);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    i32.wrap_i64\n");
    WI("    i32.gt_s\n");
    WI("    (if (then return_call $%s))\n", e2_resume);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i64.extend_i32_s\n");
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

static void emit_icn_alt(const EXPR_t *n, int id,
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

    WI("  ;; E_GENALT  (node %d, branch-slot %d @ 0x%x)\n", id, slot, slot_addr);
    WI("  (func $%s (result i32)\n", sa);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.const 1\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", e1_start);
    WI("  (func $%s (result i32)\n", ra);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i32.const 1\n");
    WI("    i32.eq\n");
    WI("    (if (then return_call $%s))\n", e1_resume);
    WI("    return_call $%s)\n", e2_resume);
    WI("  (func $%s (result i32)\n", e1f);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.const 2\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", e2_start);
    WI("  (func $%s (result i32)\n", e1s);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
    emit_passthrough(e2f, fail);
    WI("  (func $%s (result i32)\n", e2s);
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);
}

static void emit_icn_call_write(int id,
                                const char *succ, const char *fail,
                                const char *e_start, const char *e_resume,
                                int e_val_id,
                                int is_str,
                                int arg_slit_idx) {
    char sa[64], ra[64], ef[64], es[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    wfn(ef, sizeof ef, id, "efail");
    wfn(es, sizeof es, id, "esucc");

    WI("  ;; E_FNC write()  (node %d)\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    emit_passthrough(ef, fail);
    WI("  (func $%s (result i32)\n", es);
    if (is_str) {
        WI("    global.get $icn_strlit_off%d\n", arg_slit_idx);
        WI("    global.get $icn_strlit_len%d\n", arg_slit_idx);
        WI("    call $sno_output_str\n");
    } else {
        WI("    global.get $icn_int%d\n", e_val_id);
        WI("    call $sno_output_int\n");
    }
    WI("    return_call $%s)\n", succ);
}

/* Catch-all stub for unimplemented nodes */
static void emit_icn_stub(const EXPR_t *n, int id, const char *fail) {
    char kname_buf[32]; snprintf(kname_buf, sizeof kname_buf, "kind%d", (int)n->kind);
    const char *kname = kname_buf;
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    WI("  ;; %s STUB (node %d) — not yet implemented\n", kname, id);
    WI("  (func $%s (result i32)  return_call $%s)  ;; stub-fail\n", sa, fail);
    WI("  (func $%s (result i32)  return_call $%s)  ;; stub-fail\n", ra, fail);
}

/* ── §4  Recursive expression emitter ─────────────────────────────────────── */

static void emit_expr_wasm(const EXPR_t *n,
                            const char *succ, const char *fail,
                            char *out_start, char *out_resume);

static void emit_expr_wasm(const EXPR_t *n,
                            const char *succ, const char *fail,
                            char *out_start, char *out_resume) {
    if (!n) {
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

    /* ── Integer literal ─────────────────────────────────────────────────── */
    case E_ILIT:
        emit_icn_int(n, id, succ, fail);
        break;

    /* ── Float literal ───────────────────────────────────────────────────── */
    case E_FLIT:
        emit_icn_real(n, id, succ, fail);
        break;

    /* ── String literal ──────────────────────────────────────────────────── */
    case E_QLIT: {
        const char *sv = n->sval ? n->sval : "";
        int slit_idx = icn_strlit_intern(sv);
        int abs_off  = icn_strlit_abs(slit_idx);
        int slen     = icn_str_lits[slit_idx].len;
        WI("  ;; E_QLIT \"%s\" (node %d) slit=%d offset=%d len=%d\n",
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

    /* ── Variable ────────────────────────────────────────────────────────── */
    case E_VAR:
        emit_icn_var(n, id, succ, fail);
        break;

    /* ── Assignment ──────────────────────────────────────────────────────── */
    case E_ASSIGN:
        emit_icn_assign(n, id, succ, fail);
        break;

    /* ── Unary arithmetic ────────────────────────────────────────────────── */
    case E_NEG:
    case E_POS: {
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        char esucc[64], efail[64];
        wfn(esucc, sizeof esucc, id, "esucc");
        wfn(efail, sizeof efail, id, "efail");
        char e_start[64], e_resume[64];
        int e_id = wasm_icon_ctr;
        emit_expr_wasm(n->children[0], esucc, efail, e_start, e_resume);
        emit_icn_unop(n, id, succ, fail, e_start, e_resume, e_id);
        break;
    }

    /* ── Binary arithmetic ───────────────────────────────────────────────── */
    case E_ADD: case E_SUB: case E_MPY:
    case E_DIV: case E_MOD: {
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
        emit_icn_binop(n, id, succ, fail,
                       e1_start, e1_resume, e2_start, e2_resume,
                       e1_id, e2_id);
        break;
    }

    /* ── Numeric relational ──────────────────────────────────────────────── */
    case E_LT: case E_LE: case E_GT: case E_GE:
    case E_EQ: case E_NE: {
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

    /* ── Range generator: E1 to E2 ──────────────────────────────────────── */
    case E_TO: {
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

    /* ── Value alternation: E1 | E2 (E_GENALT) ──────────────────────────── */
    case E_GENALT: {
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

    /* ── every E ─────────────────────────────────────────────────────────── */
    case E_EVERY: {
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        char every_resume[64];
        wfn(every_resume, sizeof every_resume, id, "resume");
        char every_fail[64];
        wfn(every_fail, sizeof every_fail, id, "efail");
        char e_start[64], e_resume[64];
        emit_expr_wasm(n->children[0], every_resume, every_fail,
                       e_start, e_resume);
        WI("  ;; E_EVERY  (node %d)\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
        WI("  (func $%s (result i32)  return_call $%s)\n", every_resume, e_resume);
        WI("  (func $%s (result i32)  return_call $%s)\n", every_fail, succ);
        snprintf(ra, sizeof ra, "%s", every_resume);
        break;
    }

    /* ── Function call (E_FNC — both write() and user procs) ────────────── */
    case E_FNC: {
        /* When used as a call expression: sval = callee name, children[0] =
         * E_VAR name node, children[1..nargs] = arg nodes.
         * ival = nparams (0 for calls). */
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        /* child[0] is E_VAR with the function name */
        const char *fname = (n->children[0] && n->children[0]->sval)
                            ? n->children[0]->sval
                            : (n->sval ? n->sval : "unknown");
        int nargs = n->nchildren - 1;  /* args start at children[1] */

        /* write() builtin */
        if (strcmp(fname, "write") == 0 && nargs >= 1) {
            char esucc_name[64];
            wfn(esucc_name, sizeof esucc_name, id, "esucc");
            char e_start[64], e_resume[64];
            int e_id = wasm_icon_ctr;
            EXPR_t *arg_node = n->children[1];
            int is_str = (arg_node && arg_node->kind == E_QLIT);
            int arg_slit = 0;
            if (is_str && arg_node->sval)
                arg_slit = icn_strlit_intern(arg_node->sval);
            emit_expr_wasm(arg_node, esucc_name, fail, e_start, e_resume);
            emit_icn_call_write(id, succ, fail, e_start, e_resume,
                                e_id, is_str, arg_slit);
            break;
        }
        if (strcmp(fname, "write") == 0 && nargs == 0) {
            WI("  ;; E_FNC write() no args (node %d)\n", id);
            WI("  (func $%s (result i32)\n", sa);
            WI("    ;; write() no-arg: output newline\n");
            WI("    return_call $%s)\n", succ);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
            break;
        }
        /* User procedure call */
        if (fname[0] != '\0' && strcmp(fname, "unknown") != 0) {
            char esucc[64];
            wfn(esucc, sizeof esucc, id, "esucc");

            if (nargs > 0) {
                /* Two-pass: emit all arg expressions first to get their start names,
                 * then emit esucc trampolines that forward to the next arg's start. */
                int actual_nargs = (nargs < 8) ? nargs : 8;
                char arg_starts[8][64];
                char arg_esucc_names[8][64];
                int  arg_ids[8];
                char dummy_resume[64];

                for (int ai = 0; ai < actual_nargs; ai++) {
                    EXPR_t *arg = n->children[1 + ai];
                    snprintf(arg_esucc_names[ai], 64, "icon%d_arg%d_esucc", id, ai);
                    emit_expr_wasm(arg, arg_esucc_names[ai], fail,
                                   arg_starts[ai], dummy_resume);
                    arg_ids[ai] = wasm_icon_ctr - 1;
                }

                /* sa -> first arg's start */
                WI("  (func $%s (result i32)  return_call $%s)\n", sa, arg_starts[0]);

                /* esucc[ai]: store param, call next arg start or docall */
                for (int ai = 0; ai < actual_nargs; ai++) {
                    char next_buf[64];
                    if (ai < actual_nargs - 1)
                        snprintf(next_buf, sizeof next_buf, "%s", arg_starts[ai + 1]);
                    else
                        snprintf(next_buf, sizeof next_buf, "icon%d_docall", id);
                    WI("  (func $%s (result i32)\n", arg_esucc_names[ai]);
                    WI("    global.get $icn_int%d\n", arg_ids[ai]);
                    WI("    global.set $icn_param%d\n", ai);
                    WI("    return_call $%s)\n", next_buf);
                }
                /* Register esucc in retcont table; docall sets $icn_retcont then calls proc */
                int retcont_idx = icn_retcont_register(esucc);
                WI("  (func $icon%d_docall (result i32)\n", id);
                WI("    i32.const %d\n", retcont_idx);
                WI("    global.set $icn_retcont\n");
                WI("    return_call $icn_proc_%s_start)\n", fname);
            } else {
                int retcont_idx = icn_retcont_register(esucc);
                WI("  (func $%s (result i32)\n", sa);
                WI("    i32.const %d\n", retcont_idx);
                WI("    global.set $icn_retcont\n");
                WI("    return_call $icn_proc_%s_start)\n", fname);
            }

            WI("  (func $%s (result i32)\n", esucc);
            WI("    global.get $icn_retval\n");
            WI("    global.set $icn_int%d\n", id);
            WI("    return_call $%s)\n", succ);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
            break;
        }
        emit_icn_stub(n, id, fail);
        break;
    }

    /* ── return [E] ──────────────────────────────────────────────────────── */
    case E_RETURN:
        WI("  ;; E_RETURN (node %d)\n", id);
        if (strcmp(icn_cur_proc_name, "main") != 0 && icn_cur_proc_name[0] != '\0') {
            if (n->nchildren >= 1) {
                char e_start[64], e_resume[64];
                char esucc[64];
                wfn(esucc, sizeof esucc, id, "esucc");
                int e_id = wasm_icon_ctr;
                emit_expr_wasm(n->children[0], esucc, fail, e_start, e_resume);
                WI("  (func $%s (result i32)\n", esucc);
                WI("    global.get $icn_int%d\n", e_id);
                WI("    global.set $icn_retval\n");
                WI("    return_call $%s)\n", succ);
                WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
            } else {
                WI("  (func $%s (result i32)\n", sa);
                WI("    i64.const 0\n");
                WI("    global.set $icn_retval\n");
                WI("    return_call $%s)\n", succ);
            }
        } else {
            WI("  (func $%s (result i32)  return_call $%s)\n", sa, succ);
        }
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;

    /* ── fail ────────────────────────────────────────────────────────────── */
    case E_FAIL:
        WI("  ;; E_FAIL (node %d)\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;

    /* ── Global declaration (skip — no code to emit) ─────────────────────── */
    case E_GLOBAL:
        WI("  ;; E_GLOBAL \"%s\" (node %d) — decl only\n", n->sval ? n->sval : "", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, succ);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;

    /* ── Unimplemented: stub-fail ─────────────────────────────────────────── */
    default:
        emit_icn_stub(n, id, fail);
        break;
    }

    if (out_start)  snprintf(out_start,  64, "%s", sa);
    if (out_resume) snprintf(out_resume, 64, "%s", ra);
}

/* ── §5  Public entry points ──────────────────────────────────────────────── */

int emit_wasm_icon_node(const EXPR_t *n, FILE *out) {
    /* Legacy hook — kept for compatibility. Not used in primary file path. */
    (void)n; (void)out; return 0;
}

void emit_wasm_icon_globals(FILE *out) {
    emit_wasm_icon_set_out(out);
    WI("  ;; Icon node-value globals\n");
    for (int i = 0; i < 64; i++)
        WI("  (global $icn_int%d (mut i64) (i64.const 0))\n", i);
    for (int i = 0; i < 16; i++)
        WI("  (global $icn_flt%d (mut f64) (f64.const 0))\n", i);
    WI("  ;; M-IW-P01: proc call/return globals\n");
    WI("  (global $icn_retval (mut i64) (i64.const 0))\n");
    WI("  (global $icn_retcont (mut i32) (i32.const 0))\n");
    for (int i = 0; i < 8; i++)
        WI("  (global $icn_param%d (mut i64) (i64.const 0))\n", i);
}

void emit_wasm_icon_str_globals(FILE *out) {
    emit_wasm_icon_set_out(out);
    if (icn_str_nlit == 0) return;
    WI("  ;; Icon string literal (offset,len) globals\n");
    for (int i = 0; i < icn_str_nlit; i++) {
        WI("  (global $icn_strlit_off%d (mut i32) (i32.const 0))\n", i);
        WI("  (global $icn_strlit_len%d (mut i32) (i32.const 0))\n", i);
    }
}

int is_icon_node(int kind) {
    return (kind >= E_ILIT && kind < E_KIND_COUNT);
}

/* ── Emit one E_FNC proc node as a WAT function group ────────────────────── */
/*
 * EXPR_t proc layout (from icon_lower.c ICN_PROC case):
 *   e->kind          = E_FNC
 *   e->sval          = proc name
 *   e->ival          = nparams
 *   e->children[0]   = E_VAR name node (sval = proc name)
 *   e->children[1..np] = E_VAR param nodes
 *   e->children[np+1..] = body statements
 */
static void emit_wasm_icon_proc(const EXPR_t *proc) {
    if (!proc || proc->kind != E_FNC) return;
    /* A proc decl has nchildren >= 1 and children[0] is E_VAR with proc name.
     * Distinguish proc-decl from call-site E_FNC: proc sval matches children[0]->sval. */
    const char *pname = proc->sval;
    if (!pname || !pname[0]) return;

    int nparams = (int)proc->ival;
    int body_start = 1 + nparams;  /* children[0]=name, [1..np]=params, [np+1..]=body */
    int nstmts = proc->nchildren - body_start;
    if (nstmts < 0) nstmts = 0;

    WI("\n  ;; ── Procedure %s (%d params, %d stmts) ──\n", pname, nparams, nstmts);

    icon_gen_slot_next = 0;

    snprintf(icn_cur_proc_name, sizeof icn_cur_proc_name, "%s", pname);
    icn_cur_nparams = (nparams < 8) ? nparams : 8;
    for (int i = 0; i < icn_cur_nparams; i++) {
        EXPR_t *pnode = proc->children[1 + i];
        const char *pn = (pnode && pnode->sval) ? pnode->sval : "";
        snprintf(icn_cur_params[i], 64, "%s", pn);
    }

    if (nstmts == 0) {
        WI("  (func $icn_proc_%s_start (result i32)  return_call $icn_prog_end)\n", pname);
        return;
    }

    #define MAX_STMTS_PER_PROC 64
    char stmt_start [MAX_STMTS_PER_PROC][64];
    char stmt_resume[MAX_STMTS_PER_PROC][64];

    if (nstmts > MAX_STMTS_PER_PROC) {
        WI("  ;; WARNING: too many stmts in %s (%d > %d)\n", pname, nstmts, MAX_STMTS_PER_PROC);
        nstmts = MAX_STMTS_PER_PROC;
    }

    char chain_names[MAX_STMTS_PER_PROC][64];
    for (int i = 0; i < nstmts; i++)
        snprintf(chain_names[i], 64, "icn_%s_chain%d", pname, i);

    for (int i = 0; i < nstmts; i++) {
        const EXPR_t *stmt = proc->children[body_start + i];
        emit_expr_wasm(stmt, chain_names[i], "icn_program_fail",
                       stmt_start[i], stmt_resume[i]);
    }

    /* Non-main procs return via $icn_retcont trampoline (M-IW-P01).
     * main returns via icn_prog_end as before. */
    int is_main_proc = (strcmp(pname, "main") == 0);
    const char *last_succ = is_main_proc ? "icn_prog_end"
                                         : (char[64]){};
    char retcont_func[64];
    if (!is_main_proc)
        snprintf(retcont_func, sizeof retcont_func, "icn_proc_%s_retcont", pname);

    for (int i = 0; i < nstmts; i++) {
        const char *next;
        char next_buf[64];
        if (i + 1 < nstmts) {
            next = stmt_start[i+1];
        } else {
            next = is_main_proc ? "icn_prog_end" : retcont_func;
        }
        (void)last_succ;
        WI("  (func $%s (result i32)  return_call $%s)  ;; chain %d->%d\n",
           chain_names[i], next, i, i+1);
        (void)next_buf;
    }

    /* Emit retcont trampoline for non-main procs */
    if (!is_main_proc) {
        WI("  (func $%s (result i32)\n", retcont_func);
        WI("    global.get $icn_retcont\n");
        WI("    return_call_indirect (type $cont_t))\n");
    }

    WI("  (func $icn_proc_%s_start (result i32)  return_call $%s)\n",
       pname, stmt_start[0]);
}

/*
 * emit_wasm_icon_file() — top-level entry point for Icon × WASM compilation.
 * Receives EXPR_t** lowered procs from icon_lower_file().
 * IW-8: updated from IcnNode** to EXPR_t**.
 */
void emit_wasm_icon_file(EXPR_t **procs, int count, FILE *out,
                          const char *filename) {
    (void)filename;
    emit_wasm_icon_set_out(out);

    /* Prescan: intern all E_QLIT strings so globals declared before funcs */
    icn_strlit_reset();
    icn_retcont_reset();
    for (int i = 0; i < count; i++)
        icn_prescan_node(procs[i]);

    WI(";; Generated by scrip-cc -icn -wasm (IW-8)\n");
    WI("(module\n");

    WI("  ;; M-IW-P01: continuation type for return_call_indirect\n");
    WI("  (type $cont_t (func (result i32)))\n");
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

    emit_wasm_icon_globals(out);
    emit_wasm_icon_str_globals(out);
    icn_emit_data_segment();

    /* Emit all proc declarations (E_FNC with sval matching children[0]->sval) */
    for (int i = 0; i < count; i++) {
        if (procs[i] && procs[i]->kind == E_FNC &&
            procs[i]->nchildren >= 1 &&
            procs[i]->children[0] && procs[i]->children[0]->kind == E_VAR &&
            procs[i]->sval && procs[i]->children[0]->sval &&
            strcmp(procs[i]->sval, procs[i]->children[0]->sval) == 0) {
            emit_wasm_icon_proc(procs[i]);
        }
    }

    WI("\n  ;; ── Terminal functions ──\n");
    WI("  (func $icn_prog_end (result i32)\n");
    WI("    call $sno_output_flush)\n");
    WI("  (func $icn_program_fail (result i32)\n");
    WI("    call $sno_output_flush)\n");

    /* Exported main: find the main proc */
    WI("\n  ;; ── Exported main entry ──\n");
    WI("  (func (export \"main\") (result i32)\n");
    int found_main = 0;
    for (int i = 0; i < count; i++) {
        if (procs[i] && procs[i]->kind == E_FNC &&
            procs[i]->sval && strcmp(procs[i]->sval, "main") == 0 &&
            procs[i]->nchildren >= 1 &&
            procs[i]->children[0] && procs[i]->children[0]->sval &&
            strcmp(procs[i]->children[0]->sval, "main") == 0) {
            WI("    return_call $icn_proc_main_start)\n");
            found_main = 1;
            break;
        }
    }
    if (!found_main) {
        WI("    ;; no main procedure found\n");
        WI("    call $sno_output_flush)\n");
    }

    /* M-IW-P01: emit funcref table for call-site esucc return trampolines */
    if (icn_retcont_count > 0) {
        WI("\n  ;; M-IW-P01: retcont funcref table (%d entries)\n", icn_retcont_count);
        WI("  (table %d funcref)\n", icn_retcont_count);
        WI("  (elem (i32.const 0)");
        for (int i = 0; i < icn_retcont_count; i++)
            WI(" $%s", icn_retcont_funcs[i]);
        WI(")\n");
    }

    WI(")\n");
}
