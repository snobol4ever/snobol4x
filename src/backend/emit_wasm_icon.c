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
 *   ICN_ADD   → E_ADD, ICN_SUB → E_SUB, ICN_MUL → E_MUL
 *   ICN_DIV   → E_DIV, ICN_MOD → E_MOD, ICN_NEG → E_MNS
 *   ICN_LT    → E_LT, LE→E_LE, GT→E_GT, GE→E_GE, EQ→E_EQ, NE→E_NE
 *   ICN_TO    → E_TO, ICN_ALT → E_ALTERNATES
 *   ICN_EVERY → E_EVERY, ICN_RETURN → E_RETURN, ICN_FAIL → E_FAIL
 *   ICN_ASSIGN→ E_ASSIGN
 *
 * RULES.md §BYRD BOXES: emit labels+gotos, never interpret IR nodes at emit-time.
 */

#include "icon_ast.h"
#include "icon_emit.h"
#include "emit_wasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── §1  WAT output macros ────────────────────────────────────────────────── */

static FILE *icon_wasm_out = NULL;

void emit_wasm_icon_set_out(FILE *f) { icon_wasm_out = f; }

#define WI(fmt, ...)  fprintf(icon_wasm_out, fmt, ##__VA_ARGS__)

/* Generator state memory layout — same as before (IW-2) */
#define ICON_GEN_STATE_BASE  0x20000   /* page 2 (131072): safe above string heap+data */
#define ICON_GEN_SLOT_BYTES  64
#define ICON_GEN_MAX_SLOTS   256

/* Retcont call stack — handles recursive proc calls (IW-9).
 * Stack pointer at ICON_RETCONT_SP_ADDR (i32); stack grows upward.
 * Each frame = 4 bytes (one i32 retcont table index).
 * Separate from gen-state area (0x20000–0x23FFF).
 * Layout: [ICON_RETCONT_SP_ADDR] = current SP (points to next free slot)
 *          [ICON_RETCONT_STACK_BASE .. +4096] = frame storage */
#define ICON_RETCONT_SP_ADDR    0x24000   /* 147456: SP global in memory */
#define ICON_RETCONT_STACK_BASE 0x24004   /* 147460: stack data starts here */
#define ICON_RETCONT_MAX_DEPTH  1024      /* max recursion depth */

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

/* ── §1b2  Proc registry: name → nparams (IW-10) ─────────────────────────── */
/* Populated during prescan; used by call sites for frame save/restore. */

#define ICN_PROC_REG_MAX   64
#define ICON_FRAME_MAX_INTS 32

typedef struct { char name[64]; int nparams; } IcnProcReg;
static IcnProcReg icn_proc_reg[ICN_PROC_REG_MAX];
static int        icn_proc_reg_count = 0;

static void icn_proc_reg_reset(void) { icn_proc_reg_count = 0; }

static void icn_proc_reg_add(const char *name, int nparams) {
    if (icn_proc_reg_count >= ICN_PROC_REG_MAX) return;
    int idx = icn_proc_reg_count++;
    snprintf(icn_proc_reg[idx].name, 64, "%s", name);
    icn_proc_reg[idx].nparams = nparams;
}

static int icn_proc_reg_lookup(const char *name) {
    for (int i = 0; i < icn_proc_reg_count; i++)
        if (strcmp(icn_proc_reg[i].name, name) == 0)
            return icn_proc_reg[i].nparams;
    return 0;
}

/* ── §1b  String literal intern table — shared via emit_wasm.h ───────────── */
/* emit_wasm.c owns the table; call emit_wasm_strlit_* directly.             */

/* Pre-scan EXPR_t tree and intern every E_QLIT string. */
static void icn_prescan_node(const EXPR_t *n) {
    if (!n) return;
    if (n->kind == E_QLIT && n->sval)
        emit_wasm_strlit_intern(n->sval);
    for (int i = 0; i < n->nchildren; i++)
        icn_prescan_node(n->children[i]);
}

/* Returns 1 if any node in the subtree is E_SUSPEND */
static int icn_has_suspend(const EXPR_t *n) {
    if (!n) return 0;
    if (n->kind == E_SUSPEND) return 1;
    for (int i = 0; i < n->nchildren; i++)
        if (icn_has_suspend(n->children[i])) return 1;
    return 0;
}

/* Returns 1 if any E_FNC in the subtree calls a user procedure (non-builtin).
 * User proc calls push a retcont frame and set $icn_retcont on suspend yield.
 * E_EVERY must use return_call_indirect $icn_retcont to resume such generators.
 * Builtins (write, etc.) never push retcont frames and never set $icn_retcont.
 */
/* Returns 1 if fname is a user-defined procedure in icn_proc_reg.
 * Registry-based: correct when user procs shadow builtin names. */
static int icn_is_usercall(const char *fname) {
    if (!fname) return 0;
    return icn_proc_reg_lookup(fname) >= 0;
}

static int icn_has_usercall(const EXPR_t *n) {
    if (!n) return 0;
    if (n->kind == E_FNC) {
        const char *fname = (n->nchildren > 0 && n->children[0] && n->children[0]->sval)
                            ? n->children[0]->sval
                            : n->sval;
        if (icn_is_usercall(fname)) return 1;
    }
    for (int i = 0; i < n->nchildren; i++)
        if (icn_has_usercall(n->children[i])) return 1;
    return 0;
}

/* ── §1c  Per-proc local variable table (M-IW-V01) ───────────────────────── */
/* Local vars are emitted as per-proc globals: $icn_lv_PROC_VAR (mut i64).
 * This matches the param model (already globals) and is correct for
 * non-recursive local vars.  Recursive procs with locals need a stack frame
 * (future work); for now sum_to/fact class programs work correctly. */

#define ICN_MAX_LOCALS 32

typedef struct {
    char proc[64];
    char name[64];
    int  slot;   /* index within this proc's local table */
} IcnLocalVar;

static IcnLocalVar icn_locals[ICN_MAX_LOCALS];
static int         icn_nlocals = 0;

/* Reset local table at the start of each proc. */
static void icn_locals_reset(void) { icn_nlocals = 0; }

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

/* Save/restore live icn_intN + icn_paramN globals around a recursive call (IW-10).
 * Uses gen-state memory page as a per-call-site stack frame (8 bytes per slot). */
/* IW-13: Dynamic frame addressing using $icn_frame_depth (runtime global).
 * Frame N lives at: ICON_GEN_STATE_BASE + N * ICON_FRAME_STRIDE
 * where ICON_FRAME_STRIDE must hold max(nints+nparams)*8 bytes.
 * $icn_frame_depth is incremented by icn_retcont_push, decremented by icn_retcont_pop,
 * so each recursive activation gets its own frame slot. */
#define ICON_FRAME_STRIDE  512   /* 64 i64 slots = 512 bytes; handles up to 63 ints+params */

/* Emit WAT to compute base address of current frame into a local $frame_base.
 * Caller must have declared: (local $frame_base i32) */
static void emit_frame_base_load(void) {
    /* frame_base = ICON_GEN_STATE_BASE + $icn_frame_depth * ICON_FRAME_STRIDE */
    WI("    global.get $icn_frame_depth\n");
    WI("    i32.const %d\n", ICON_FRAME_STRIDE);
    WI("    i32.mul\n");
    WI("    i32.const %d\n", ICON_GEN_STATE_BASE);
    WI("    i32.add\n");
    WI("    local.set $frame_base\n");
}

static void emit_frame_push(int nints, int nparams) {
    if (nints == 0 && nparams == 0) return;
    int total = (nints < ICON_FRAME_MAX_INTS ? nints : ICON_FRAME_MAX_INTS) + nparams;
    WI("    ;; frame_push: save %d ints + %d params at depth=$icn_frame_depth\n", nints, nparams);
    /* IW-14 Bug 3: use call $icn_frame_base inline — no (local) declaration needed */
    for (int i = 0; i < nints && i < ICON_FRAME_MAX_INTS; i++) {
        WI("    call $icn_frame_base  i32.const %d  i32.add  global.get $icn_int%d  i64.store\n",
           i * 8, i);
    }
    for (int i = 0; i < nparams; i++) {
        WI("    call $icn_frame_base  i32.const %d  i32.add  global.get $icn_param%d  i64.store\n",
           (nints + i) * 8, i);
    }
    (void)total;
}

static void emit_frame_pop(int nints, int nparams) {
    if (nints == 0 && nparams == 0) return;
    WI("    ;; frame_pop: restore %d ints + %d params at depth=$icn_frame_depth\n", nints, nparams);
    /* IW-14 Bug 3: use call $icn_frame_base inline — no (local) declaration needed */
    for (int i = 0; i < nints && i < ICON_FRAME_MAX_INTS; i++) {
        WI("    call $icn_frame_base  i32.const %d  i32.add  i64.load  global.set $icn_int%d\n",
           i * 8, i);
    }
    for (int i = 0; i < nparams; i++) {
        WI("    call $icn_frame_base  i32.const %d  i32.add  i64.load  global.set $icn_param%d\n",
           (nints + i) * 8, i);
    }
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

/* M-IW-V01: local-var helpers (need icn_cur_nparams / icn_cur_params from §2) */

/* Scan EXPR_t tree; for every E_ASSIGN whose LHS is E_VAR and not a param,
 * register a local slot for proc_name. */
static void icn_locals_scan(const EXPR_t *n, const char *proc_name) {
    if (!n) return;
    if (n->kind == E_ASSIGN && n->nchildren >= 1) {
        const EXPR_t *lhs = n->children[0];
        if (lhs && lhs->kind == E_VAR && lhs->sval && lhs->sval[0]) {
            int is_param = 0;
            for (int i = 0; i < icn_cur_nparams; i++)
                if (strcmp(lhs->sval, icn_cur_params[i]) == 0) { is_param = 1; break; }
            if (!is_param) {
                int found = 0;
                for (int i = 0; i < icn_nlocals; i++)
                    if (strcmp(icn_locals[i].proc, proc_name) == 0 &&
                        strcmp(icn_locals[i].name, lhs->sval) == 0) { found = 1; break; }
                if (!found && icn_nlocals < ICN_MAX_LOCALS) {
                    snprintf(icn_locals[icn_nlocals].proc, 64, "%s", proc_name);
                    snprintf(icn_locals[icn_nlocals].name, 64, "%s", lhs->sval);
                    icn_locals[icn_nlocals].slot = icn_nlocals;
                    icn_nlocals++;
                }
            }
        }
    }
    for (int i = 0; i < n->nchildren; i++)
        icn_locals_scan(n->children[i], proc_name);
}

/* Find local slot index for (proc, varname); return >=0 or -1 if not found. */
static int icn_local_find(const char *proc_name, const char *var_name) {
    int slot = 0;
    for (int i = 0; i < icn_nlocals; i++) {
        if (strcmp(icn_locals[i].proc, proc_name) == 0) {
            if (strcmp(icn_locals[i].name, var_name) == 0) return slot;
            slot++;
        }
    }
    return -1;
}

/* Emit WAT global declarations for all locals of proc_name. */
static void icn_emit_local_globals(const char *proc_name) {
    for (int i = 0; i < icn_nlocals; i++) {
        if (strcmp(icn_locals[i].proc, proc_name) == 0) {
            WI("  (global $icn_lv_%s_%s (mut i64) (i64.const 0))\n",
               proc_name, icn_locals[i].name);
        }
    }
}

/* Forward declaration — emit_icn_assign calls emit_expr_wasm */
static void emit_expr_wasm(const EXPR_t *n,
                            const char *succ, const char *fail,
                            char *out_start, char *out_resume);

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
        /* Check local var table (M-IW-V01) */
        int local_slot = (n->sval && icn_cur_proc_name[0])
                         ? icn_local_find(icn_cur_proc_name, n->sval) : -1;
        (void)local_slot;
        if (local_slot >= 0) {
            WI("  (func $%s (result i32)\n", sa);
            WI("    global.get $icn_lv_%s_%s\n", icn_cur_proc_name, n->sval);
            WI("    global.set $icn_int%d\n", id);
            WI("    return_call $%s)\n", succ);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        } else {
            WI("  (func $%s (result i32)\n", sa);
            WI("    ;; TODO: global var \"%s\" not yet impl\n", n->sval ? n->sval : "");
            WI("    return_call $%s)  ;; stub-fail\n", fail);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        }
    }
}

static void emit_icn_assign(const EXPR_t *n, int id,
                             const char *succ, const char *fail) {
    char sa[64], ra[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");

    /* E_ASSIGN: children[0] = LHS E_VAR, children[1] = RHS expr */
    if (!n || n->nchildren < 2) {
        WI("  ;; E_ASSIGN  (node %d) — malformed, stub-fail\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        return;
    }
    const EXPR_t *lhs = n->children[0];
    const EXPR_t *rhs = n->children[1];
    const char *vname = (lhs && lhs->sval) ? lhs->sval : NULL;

    /* Check if LHS is a local var */
    int local_slot = (vname && icn_cur_proc_name[0])
                     ? icn_local_find(icn_cur_proc_name, vname) : -1;
    (void)local_slot;

    if (vname && local_slot >= 0) {
        /* Emit RHS; on success store into local global, then call succ */
        char rhs_start[64], rhs_resume[64];
        char esucc[64];
        wfn(esucc, sizeof esucc, id, "esucc");
        int rhs_id = wasm_icon_ctr;
        emit_expr_wasm(rhs, esucc, fail, rhs_start, rhs_resume);

        WI("  ;; E_ASSIGN \"%s\" := ...  (node %d, local)\n", vname, id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, rhs_start);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, rhs_resume);
        /* esucc: store RHS result into local global, then proceed */
        WI("  (func $%s (result i32)\n", esucc);
        WI("    global.get $icn_int%d\n", rhs_id);
        WI("    global.set $icn_lv_%s_%s\n", icn_cur_proc_name, vname);
        /* Icon assignment yields RHS value */
        WI("    global.get $icn_int%d\n", rhs_id);
        WI("    global.set $icn_int%d\n", id);
        WI("    return_call $%s)\n", succ);
    } else if (vname) {
        /* param assignment (update param global) or unknown — stub for now */
        int param_idx = -1;
        for (int i = 0; i < icn_cur_nparams; i++)
            if (strcmp(vname, icn_cur_params[i]) == 0) { param_idx = i; break; }

        if (param_idx >= 0) {
            char rhs_start[64], rhs_resume[64];
            char esucc[64];
            wfn(esucc, sizeof esucc, id, "esucc");
            int rhs_id = wasm_icon_ctr;
            emit_expr_wasm(rhs, esucc, fail, rhs_start, rhs_resume);

            WI("  ;; E_ASSIGN param%d \"%s\" := ...  (node %d)\n", param_idx, vname, id);
            WI("  (func $%s (result i32)  return_call $%s)\n", sa, rhs_start);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, rhs_resume);
            WI("  (func $%s (result i32)\n", esucc);
            WI("    global.get $icn_int%d\n", rhs_id);
            WI("    global.set $icn_param%d\n", param_idx);
            WI("    global.get $icn_int%d\n", rhs_id);
            WI("    global.set $icn_int%d\n", id);
            WI("    return_call $%s)\n", succ);
        } else {
            WI("  ;; E_ASSIGN \"%s\" (node %d) — not a known local/param, stub-fail\n",
               vname, id);
            WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
            WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        }
    } else {
        WI("  ;; E_ASSIGN  (node %d) — no LHS name, stub-fail\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, fail);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
    }
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
        case E_MUL: op_instr = "i64.mul";   op_comment = "MUL"; break;
        case E_DIV: op_instr = "i64.div_s"; op_comment = "DIV"; break;
        case E_MOD: op_instr = "i64.rem_s"; op_comment = "MOD"; break;
        default: break;
    }

    /* left_is_value: VAR/ILIT/QLIT/FNC — re-eval on resume (e.g. mutable total).
     * generator-left (E_TO etc.): left cache valid; go direct to e2_resume.
     * Mirrors x64 emit_x64_icon.c heuristic (line ~1217). */
    int left_is_value = (n->nchildren >= 1 &&
                         (n->children[0]->kind == E_VAR  ||
                          n->children[0]->kind == E_ILIT ||
                          n->children[0]->kind == E_QLIT ||
                          n->children[0]->kind == E_FNC));

    WI("  ;; E_%s  (node %d)\n", op_comment, id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e1_start);

    if (left_is_value) {
        /* value-left: use bflag global to distinguish start (->e2_start) vs resume (->e2_resume) */
        char bflag[64];
        wfn(bflag, sizeof bflag, id, "bf");
        WI("  (global $%s (mut i32) (i32.const 0))\n", bflag);
        /* ra: set bflag=1, re-eval e1 to pick up updated value */
        WI("  (func $%s (result i32)\n", ra);
        WI("    i32.const 1\n");
        WI("    global.set $%s\n", bflag);
        WI("    return_call $%s)\n", e1_start);
        emit_passthrough(e1f, fail);
        /* e1s: bflag=0 -> e2_start (fresh); bflag=1 -> e2_resume (advance generator) */
        WI("  (func $%s (result i32)\n", e1s);
        WI("    global.get $%s\n", bflag);
        WI("    i32.const 0\n");
        WI("    global.set $%s\n", bflag);  /* reset bflag for next call */
        WI("    (if (then return_call $%s))\n", e2_resume);
        WI("    return_call $%s)\n", e2_start);
    } else {
        /* generator-left: ra goes directly to e2_resume; left cache still valid */
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, e2_resume);
        emit_passthrough(e1f, fail);
        emit_passthrough(e1s, e2_start);
    }
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
       n->kind == E_MNS ? "NEG" : "POS", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    emit_passthrough(ef, fail);
    WI("  (func $%s (result i32)\n", es);
    WI("    global.get $icn_int%d\n", e_val_id);
    if (n->kind == E_MNS) { WI("    i64.const -1\n"); WI("    i64.mul\n"); }
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
                        int e1_val_id, int e2_val_id, int e2_is_gen) {
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

    /* slot layout: [slot_addr+0] = current counter (i32)
     *              [slot_addr+4] = initialized flag (i32, 0=fresh 1=running) */
    int slot_flag = slot_addr + 4;

    WI("  ;; E_TO  (node %d, gen-slot %d @ 0x%x)\n", id, slot, slot_addr);
    /* start: if already initialized (flag=1), skip re-eval and go to code (increment+check) */
    WI("  (func $%s (result i32)\n", sa);
    WI("    i32.const %d\n", slot_flag);
    WI("    i32.load\n");
    WI("    i32.const 1\n");
    WI("    i32.eq\n");
    WI("    (if (then\n");
    WI("      i32.const %d\n", slot_addr);
    WI("      i32.const %d\n", slot_addr);
    WI("      i32.load\n");
    WI("      i32.const 1\n");
    WI("      i32.add\n");
    WI("      i32.store\n");
    WI("      return_call $%s))\n", code);
    WI("    ;; fresh start: evaluate bounds\n");
    WI("    return_call $%s)\n", e1_start);
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
    int slot_e1val = slot_addr + 8;  /* e1_val snapshot for counter reset on e2 advance */

    WI("  (func $%s (result i32)\n", e2s);
    /* store counter = e1_val at slot_addr */
    WI("    i32.const %d\n", slot_addr);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    i32.wrap_i64\n");
    WI("    i32.store\n");
    /* store e1_val snapshot at slot_addr+8 for counter reset on e2 advance */
    WI("    i32.const %d\n", slot_e1val);
    WI("    global.get $icn_int%d\n", e1_val_id);
    WI("    i32.wrap_i64\n");
    WI("    i32.store\n");
    /* mark as initialized */
    WI("    i32.const %d\n", slot_flag);
    WI("    i32.const 1\n");
    WI("    i32.store\n");
    WI("    return_call $%s)\n", code);

    /* exhaust handler: try e2_resume; on success reset counter to e1_val_snapshot */
    char exhaust[64], e2adv_succ[64];
    wfn(exhaust,    sizeof exhaust,    id, "exhaust");
    wfn(e2adv_succ, sizeof e2adv_succ, id, "e2adv_succ");

    WI("  (func $%s (result i32)\n", code);
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    global.get $icn_int%d\n", e2_val_id);
    WI("    i32.wrap_i64\n");
    WI("    i32.gt_s\n");
    WI("    (if (then return_call $%s))\n", exhaust);  /* counter exhausted: try next e2 */
    WI("    i32.const %d\n", slot_addr);
    WI("    i32.load\n");
    WI("    i64.extend_i32_s\n");
    WI("    global.set $icn_int%d\n", id);
    WI("    return_call $%s)\n", succ);

    /* exhaust: counter > e2_val.
     * If e2 is a generator: try e2_resume for next upper bound.
     *   On e2 success: e2s resets counter=e1_val, flag=1, -> code.
     *   On e2 failure: e2f -> e1_resume -> e1 advances -> e1s -> e2_start (fresh e2).
     * If e2 is a value (literal/var): just fail directly. */
    if (e2_is_gen) {
        WI("  (func $%s (result i32)  return_call $%s)\n", exhaust, e2_resume);
    } else {
        /* simple literal/var e2: exhausted means fail */
        WI("  (func $%s (result i32)\n", exhaust);
        WI("    i32.const %d\n", slot_flag);
        WI("    i32.const 0\n");
        WI("    i32.store\n");
        WI("    return_call $%s)\n", fail);
    }
    (void)e2adv_succ;
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

    WI("  ;; E_ALTERNATES  (node %d, branch-slot %d @ 0x%x)\n", id, slot, slot_addr);
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
                                int arg_slit_idx,
                                int arg_has_usercall) {
    char sa[64], ra[64], ef[64], es[64];
    wfn(sa, sizeof sa, id, "start");
    wfn(ra, sizeof ra, id, "resume");
    wfn(ef, sizeof ef, id, "efail");
    wfn(es, sizeof es, id, "esucc");

    WI("  ;; E_FNC write()  (node %d)\n", id);
    WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
    if (arg_has_usercall) {
        /* Arg is a user-proc call (generator): resume via $icn_retcont */
        WI("  (func $%s (result i32)\n", ra);
        WI("    global.get $icn_retcont\n");
        WI("    return_call_indirect (type $cont_t))\n");
    } else {
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, e_resume);
    }
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
        int slit_idx = emit_wasm_strlit_intern(sv);
        int abs_off  = emit_wasm_strlit_abs(slit_idx);
        int slen     = emit_wasm_strlit_len(slit_idx);
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
    case E_MNS:
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
    case E_ADD: case E_SUB: case E_MUL:
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
        /* e2_is_gen: if e2 is a generator (E_TO, E_ALTERNATES etc.) the exhaust handler
         * should try e2_resume to get a new upper bound. For literals/vars, just fail. */
        int e2_is_gen = (n->children[1]->kind == E_TO   ||
                         n->children[1]->kind == E_ALTERNATES ||
                         n->children[1]->kind == E_EVERY);
        emit_icn_to(n, id, succ, fail,
                    e1_start, e1_resume, e2_start, e2_resume,
                    e1_id, e2_id, e2_is_gen);
        break;
    }

    /* ── Value alternation: E1 | E2 (E_ALTERNATES) ──────────────────────────── */
    case E_ALTERNATES: {
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
        /* every_resume: ask generator for NEXT value.
         * For suspend-based generators the resume path is stored in
         * $icn_retcont by E_SUSPEND.after_val; use return_call_indirect.
         * For simple generators (E_TO, E_TO_BY, etc.) use e_resume directly
         * — those never set $icn_retcont and have no funcref table entry. */
        if (icn_has_usercall(n->children[0])) {
            /* User proc calls set $icn_retcont on each suspend yield.
             * E_EVERY must resume via return_call_indirect $icn_retcont. */
            WI("  (func $%s (result i32)\n", every_resume);
            WI("    global.get $icn_retcont\n");
            WI("    return_call_indirect (type $cont_t))\n");
        } else {
            /* Inline generators (E_TO, E_TO_BY, etc.) never use $icn_retcont. */
            WI("  (func $%s (result i32)  return_call $%s)\n", every_resume, e_resume);
        }
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
                arg_slit = emit_wasm_strlit_intern(arg_node->sval);
            emit_expr_wasm(arg_node, esucc_name, fail, e_start, e_resume);
            emit_icn_call_write(id, succ, fail, e_start, e_resume,
                                e_id, is_str, arg_slit,
                                icn_has_usercall(arg_node));
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
            /* IW-10: number of live icn_intN slots to save + callee param count */
            int nints_to_save = id;
            if (nints_to_save > ICON_FRAME_MAX_INTS) nints_to_save = ICON_FRAME_MAX_INTS;
            int callee_nparams = icn_proc_reg_lookup(fname);

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
                    arg_ids[ai] = wasm_icon_ctr;  /* capture BEFORE emit — this is the arg node's own id */
                    emit_expr_wasm(arg, arg_esucc_names[ai], fail,
                                   arg_starts[ai], dummy_resume);
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
                /* Register esucc+efail in retcont table; docall pushes both */
                int retcont_idx = icn_retcont_register(esucc);
                int efail_idx   = icn_retcont_register(ra);
                WI("  (func $icon%d_docall (result i32)\n", id);
                emit_frame_push(nints_to_save, callee_nparams);  /* IW-10 */
                WI("    i32.const %d\n", retcont_idx);
                WI("    i32.const %d\n", efail_idx);
                WI("    call $icn_retcont_push\n");
                WI("    return_call $icn_proc_%s_start)\n", fname);
            } else {
                int retcont_idx = icn_retcont_register(esucc);
                int efail_idx   = icn_retcont_register(ra);
                WI("  (func $%s (result i32)\n", sa);
                emit_frame_push(nints_to_save, callee_nparams);  /* IW-10 */
                WI("    i32.const %d\n", retcont_idx);
                WI("    i32.const %d\n", efail_idx);
                WI("    call $icn_retcont_push\n");
                WI("    return_call $icn_proc_%s_start)\n", fname);
            }

            WI("  (func $%s (result i32)\n", esucc);
            emit_frame_pop(nints_to_save, callee_nparams);  /* IW-10 */
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
            /* return bypasses the statement chain — jump directly to proc retcont */
            char retcont_target[160];
            snprintf(retcont_target, sizeof retcont_target,
                     "icn_proc_%s_retcont", icn_cur_proc_name);
            if (n->nchildren >= 1) {
                char e_start[64], e_resume[64];
                char esucc[64];
                wfn(esucc, sizeof esucc, id, "esucc");
                int e_id = wasm_icon_ctr;
                emit_expr_wasm(n->children[0], esucc, fail, e_start, e_resume);
                WI("  (func $%s (result i32)\n", esucc);
                WI("    global.get $icn_int%d\n", e_id);
                WI("    global.set $icn_retval\n");
                WI("    return_call $%s)\n", retcont_target);
                WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);
            } else {
                WI("  (func $%s (result i32)\n", sa);
                WI("    i64.const 0\n");
                WI("    global.set $icn_retval\n");
                WI("    return_call $%s)\n", retcont_target);
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

    /* ── if/then/else (E_IF) ─────────────────────────────────────────────── */
    case E_IF: {
        /* children[0]=cond, children[1]=then_body, children[2]=else_body (opt)
         * Wiring (four-port):
         *   sa  → cond.start
         *   cond succeeds → then.start (or succ if no then)
         *   cond fails    → else.start (or fail if no else)
         *   then succeeds → succ;  then fails → fail
         *   else succeeds → succ;  else fails → fail
         *   ra  → fail  (if/then is not a generator) */
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        EXPR_t *cond  = n->children[0];
        EXPR_t *thenb = (n->nchildren > 1) ? n->children[1] : NULL;
        EXPR_t *elseb = (n->nchildren > 2) ? n->children[2] : NULL;

        char cond_start[64], cond_resume[64];
        char then_start[64], then_resume[64];
        char else_start[64], else_resume[64];

        /* Emit condition; its succ=then_entry, fail=else_entry */
        char then_entry[64], else_entry[64];
        wfn(then_entry, sizeof then_entry, id, "then_entry");
        wfn(else_entry, sizeof else_entry, id, "else_entry");

        emit_expr_wasm(cond, then_entry, else_entry, cond_start, cond_resume);

        if (thenb)
            emit_expr_wasm(thenb, succ, fail, then_start, then_resume);
        if (elseb)
            emit_expr_wasm(elseb, succ, fail, else_start, else_resume);

        WI("  ;; E_IF  (node %d)\n", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, cond_start);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        /* then_entry: cond succeeded — enter then branch or go to succ */
        WI("  (func $%s (result i32)  return_call $%s)\n",
           then_entry, thenb ? then_start : succ);
        /* else_entry: cond failed — enter else branch, or skip (go to succ) if no else.
         * Icon semantics: "if E then S" with no else — cond failure just skips S. */
        WI("  (func $%s (result i32)  return_call $%s)\n",
           else_entry, elseb ? else_start : succ);
        break;
    }

    /* ── Global declaration (skip — no code to emit) ─────────────────────── */
    case E_GLOBAL:
        WI("  ;; E_GLOBAL \"%s\" (node %d) — decl only\n", n->sval ? n->sval : "", id);
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, succ);
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);
        break;

    /* ── E_WHILE — while cond do body ────────────────────────────────────── *
     * Four-port WASM wiring (mirrors x64/JVM oracles):                       *
     *   sa        → cond.start                                               *
     *   cond.esucc (cond_ok) → body.start                                    *
     *   body.esucc → loop_top → cond.start  (iterate)                       *
     *   body.efail → loop_top → cond.start  (body fail also loops)          *
     *   cond.efail → outer_fail  (condition failed: while exits)             *
     *   ra        → outer_fail  (resume of while = exhausted)               *
     * ======================================================================= */
    case E_WHILE: {
        if (n->nchildren < 1) { emit_icn_stub(n, id, fail); break; }
        EXPR_t *cond = n->children[0];
        EXPR_t *body = (n->nchildren > 1) ? n->children[1] : NULL;

        char cond_ok[64],  loop_top[64];
        wfn(cond_ok,  sizeof cond_ok,  id, "condok");
        wfn(loop_top, sizeof loop_top, id, "top");

        /* Emit condition: esucc→cond_ok, efail→outer fail */
        char c_start[64], c_resume[64];
        emit_expr_wasm(cond, cond_ok, fail, c_start, c_resume);

        /* Emit body: esucc→loop_top, efail→loop_top (body fail loops back) */
        char b_start[64], b_resume[64];
        if (body) {
            emit_expr_wasm(body, loop_top, loop_top, b_start, b_resume);
        }

        WI("  ;; E_WHILE (node %d)\n", id);

        /* sa: enter condition */
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, c_start);
        /* ra: while resume = exhausted */
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, fail);

        /* cond_ok: condition succeeded — enter body or loop back */
        if (body) {
            WI("  (func $%s (result i32)  return_call $%s)\n", cond_ok, b_start);
        } else {
            WI("  (func $%s (result i32)  return_call $%s)\n", cond_ok, loop_top);
        }

        /* loop_top: go back to condition start */
        WI("  (func $%s (result i32)  return_call $%s)\n", loop_top, c_start);
        break;
    }

    /* ── E_SUSPEND — user-defined generator yield (M-IW-G01) ─────────────── *
     * suspend E [do body]                                                     *
     *                                                                         *
     * Four-port WASM wiring:                                                  *
     *   sa        → E.start                                                   *
     *   E.esucc   → store $icn_retval, arm resume index in $icn_retcont,     *
     *               return via proc retcont (yield value to call-site)       *
     *   ra        → resume_tramp (re-enter body or E.resume)                 *
     *   body.esucc → E.resume  (re-drive value expression)                  *
     *   E.efail / body.efail → outer_fail  (generator exhausted)            *
     *                                                                         *
     * Yield path: jump to icn_proc_{name}_retcont which calls                *
     * icn_retcont_pop → return_call_indirect back to the call-site esucc.   *
     * The call-site esucc reads $icn_retval into its local icn_int slot.     *
     * Resume path: the outer every/while calls ra which re-enters the        *
     * generator body or value expression.                                     *
     * Single-active-generator model (matches x64 oracle).                    *
     * ======================================================================= */
    case E_SUSPEND: {
        EXPR_t *val_node  = (n->nchildren > 0) ? n->children[0] : NULL;
        EXPR_t *body_node = (n->nchildren > 1) ? n->children[1] : NULL;

        char after_val[64];
        wfn(after_val, sizeof after_val, id, "yield");

        /* Resume trampoline: registered in funcref table so outer loop can
         * call return_call_indirect $icn_retcont to re-enter generator */
        char resume_tramp[64];
        wfn(resume_tramp, sizeof resume_tramp, id, "rtramp");
        int resume_idx = icn_retcont_register(resume_tramp);

        /* Value expression: esucc→after_val, efail→outer_fail */
        char e_start[64], e_resume[64];
        emit_expr_wasm(val_node, after_val, fail, e_start, e_resume);

        /* Body expression: esucc→e_resume (re-drive E), efail→outer_fail */
        char b_start[64], b_resume[64];
        if (body_node) {
            emit_expr_wasm(body_node, e_resume, fail, b_start, b_resume);
        }

        WI("  ;; E_SUSPEND (node %d)  resume_idx=%d\n", id, resume_idx);

        /* sa: start value evaluation */
        WI("  (func $%s (result i32)  return_call $%s)\n", sa, e_start);

        /* after_val: value in $icn_int{val_id} — store in $icn_retval,
         * arm resume index, then yield via proc retcont to call-site.
         * The val expression stored its result in the node-id global used
         * by emit_expr_wasm internals; we need $icn_retval for the handoff. */
        WI("  (func $%s (result i32)\n", after_val);
        /* Find which icn_int holds the val result: it's the child node's id.
         * The child was emitted with id = wasm_icon_ctr at emit time, which
         * was captured as the id assigned just before emit_expr_wasm returned.
         * Simpler: use $icn_retval directly — val node's after_val sets it
         * via its own esucc chain; for E_ILIT/E_VAR the value is in icn_int{n}.
         * Use the val_node's result: wasm_icon_ctr-1 after emit is unreliable.
         * Instead: store to $icn_retval in the after_val func itself.
         * The val expression's esucc (=after_val) fires when val succeeded,
         * meaning $icn_int{val_id} holds the result where val_id = id of the
         * first sub-node emitted. We access it via the e_start func name:
         * the convention is that node id = wasm_icon_ctr at the call site.
         * Cleanest: record the val sub-node's id by checking e_start name. */
        /* e_start is "iconN_start" — extract N */
        {
            int val_id = 0;
            sscanf(e_start, "icon%d_start", &val_id);
            WI("    global.get $icn_int%d\n", val_id);
            WI("    global.set $icn_retval\n");
        }
        /* Arm resume: store resume_tramp table index in $icn_retcont */
        WI("    i32.const %d\n", resume_idx);
        WI("    global.set $icn_retcont\n");
        /* Yield to caller:
         * 1. Decrement frame_depth so esucc reads the correct saved frame slot
         *    (retcont_push incremented depth; we must undo that for frame_pop).
         * 2. Peek top esucc_idx (NON-destructive — retcont frame stays live
         *    so the generator can yield multiple times).
         * 3. call_indirect → esucc → frame_pop (restores caller's icn_ints).
         * The retcont stack entry is NOT consumed; only frame_depth changes. */
        WI("    global.get $icn_frame_depth\n");
        WI("    i32.const 1\n");
        WI("    i32.sub\n");
        WI("    global.set $icn_frame_depth\n");
        WI("    call $icn_retcont_peek_esucc\n");
        WI("    return_call_indirect (type $cont_t))\n");
        /* resume_tramp: re-increment frame_depth (re-enter upto context)
         * then continue into body or re-drive val expression */
        if (body_node) {
            WI("  (func $%s (result i32)\n", resume_tramp);
            WI("    global.get $icn_frame_depth\n");
            WI("    i32.const 1\n");
            WI("    i32.add\n");
            WI("    global.set $icn_frame_depth\n");
            WI("    return_call $%s)\n", b_start);
        } else {
            WI("  (func $%s (result i32)\n", resume_tramp);
            WI("    global.get $icn_frame_depth\n");
            WI("    i32.const 1\n");
            WI("    i32.add\n");
            WI("    global.set $icn_frame_depth\n");
            WI("    return_call $%s)\n", e_resume);
        }

        /* ra: outer resume of this suspend node = resume_tramp */
        WI("  (func $%s (result i32)  return_call $%s)\n", ra, resume_tramp);
        break;
    }

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
    WI("  ;; IW-9: retcont stack (handles recursion)\n");
    WI("  ;; SP stored at mem[0x%x]; stack data at mem[0x%x]\n",
       ICON_RETCONT_SP_ADDR, ICON_RETCONT_STACK_BASE);
    /* IW-15: two-slot retcont — push [efail_idx, esucc_idx] (8 bytes per frame).
     * efail_idx at lower address, esucc_idx at higher.
     * retcont_push(esucc_idx, efail_idx): stores efail@sp, esucc@sp+4, SP+=8.
     * retcont_pop(): SP-=4, returns mem[SP] (esucc_idx), frame_depth--.
     * retcont_pop_fail(): SP-=8, returns mem[SP] (efail_idx), frame_depth--. */
    WI("  (func $icn_retcont_push (param $esucc_idx i32) (param $efail_idx i32)\n");
    WI("    (local $sp i32)\n");
    /* load SP; if zero (uninitialised), set to stack base */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    i32.load\n");
    WI("    local.set $sp\n");
    WI("    (if (i32.eqz (local.get $sp)) (then\n");
    WI("      i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("      i32.const %d\n", ICON_RETCONT_STACK_BASE);
    WI("      i32.store\n");
    WI("      i32.const %d\n", ICON_RETCONT_STACK_BASE);
    WI("      local.set $sp))\n");
    /* mem[sp] = efail_idx */
    WI("    local.get $sp\n");
    WI("    local.get $efail_idx\n");
    WI("    i32.store\n");
    /* mem[sp+4] = esucc_idx */
    WI("    local.get $sp\n");
    WI("    i32.const 4\n");
    WI("    i32.add\n");
    WI("    local.get $esucc_idx\n");
    WI("    i32.store\n");
    /* SP += 8 */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    local.get $sp\n");
    WI("    i32.const 8\n");
    WI("    i32.add\n");
    WI("    i32.store\n");
    /* frame_depth++ */
    WI("    global.get $icn_frame_depth\n");
    WI("    i32.const 1\n");
    WI("    i32.add\n");
    WI("    global.set $icn_frame_depth)\n");
    /* retcont_pop: pops esucc_idx (top slot), decrements frame_depth */
    WI("  (func $icn_retcont_pop (result i32)\n");
    WI("    (local $sp i32)\n");
    /* sp = current_sp - 4 (esucc slot) */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    i32.load\n");
    WI("    i32.const 4\n");
    WI("    i32.sub\n");
    WI("    local.set $sp\n");
    /* new SP = sp - 4 (discard whole 8-byte frame) */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    local.get $sp\n");
    WI("    i32.const 4\n");
    WI("    i32.sub\n");
    WI("    i32.store\n");
    /* frame_depth-- */
    WI("    global.get $icn_frame_depth\n");
    WI("    i32.const 1\n");
    WI("    i32.sub\n");
    WI("    global.set $icn_frame_depth\n");
    /* return mem[sp] = esucc_idx */
    WI("    local.get $sp\n");
    WI("    i32.load)\n");
    /* retcont_pop_fail: pops efail_idx (low slot), decrements frame_depth */
    WI("  (func $icn_retcont_pop_fail (result i32)\n");
    WI("    (local $sp i32)\n");
    /* sp = current_sp - 8 (efail slot) */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    i32.load\n");
    WI("    i32.const 8\n");
    WI("    i32.sub\n");
    WI("    local.set $sp\n");
    /* new SP = sp (discard whole 8-byte frame) */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    local.get $sp\n");
    WI("    i32.store\n");
    /* frame_depth-- */
    WI("    global.get $icn_frame_depth\n");
    WI("    i32.const 1\n");
    WI("    i32.sub\n");
    WI("    global.set $icn_frame_depth\n");
    /* return mem[sp] = efail_idx */
    WI("    local.get $sp\n");
    WI("    i32.load)\n");
    /* retcont_peek_esucc: read top esucc_idx WITHOUT popping the frame.
     * Used by E_SUSPEND to yield value back to call-site repeatedly
     * (the retcont frame must stay live across multiple yields). */
    WI("  (func $icn_retcont_peek_esucc (result i32)\n");
    WI("    (local $sp i32)\n");
    /* sp_top - 4 = esucc slot */
    WI("    i32.const %d\n", ICON_RETCONT_SP_ADDR);
    WI("    i32.load\n");
    WI("    i32.const 4\n");
    WI("    i32.sub\n");
    WI("    local.set $sp\n");
    WI("    local.get $sp\n");
    WI("    i32.load)\n");
    WI("  (global $icn_retcont (mut i32) (i32.const 0))\n");
    WI("  ;; IW-13: frame depth tracks recursive call depth for dynamic frame addressing\n");
    WI("  (global $icn_frame_depth (mut i32) (i32.const 0))\n");
    /* IW-14: helper func computes frame base — avoids mid-body (local) declarations (Bug 3) */
    WI("  (func $icn_frame_base (result i32)\n");
    WI("    global.get $icn_frame_depth\n");
    WI("    i32.const %d\n", ICON_FRAME_STRIDE);
    WI("    i32.mul\n");
    WI("    i32.const %d\n", ICON_GEN_STATE_BASE);
    WI("    i32.add)\n");
    for (int i = 0; i < 8; i++)
        WI("  (global $icn_param%d (mut i64) (i64.const 0))\n", i);
}

void emit_wasm_icon_str_globals(FILE *out) {
    emit_wasm_icon_set_out(out);
    int nlit = emit_wasm_strlit_count();
    if (nlit == 0) return;
    WI("  ;; Icon string literal (offset,len) globals\n");
    for (int i = 0; i < nlit; i++) {
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

    /* M-IW-V01: scan proc body for local vars (E_ASSIGN LHS that aren't params) */
    icn_locals_reset();
    for (int i = 0; i < nstmts; i++)
        icn_locals_scan(proc->children[body_start + i], pname);

    /* Emit (global $icn_lv_PROC_VAR ...) for each discovered local */
    icn_emit_local_globals(pname);

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
    char pfail_func_name[160];
    if (!is_main_proc) {
        snprintf(retcont_func, sizeof retcont_func, "icn_proc_%s_retcont", pname);
        snprintf(pfail_func_name, sizeof pfail_func_name, "icn_proc_%s_pfail", pname);
    }

    for (int i = 0; i < nstmts; i++) {
        const char *next;
        char next_buf[64];
        if (i + 1 < nstmts) {
            next = stmt_start[i+1];
        } else {
            /* chain-end: main → prog_end; non-main → pfail (fell off without return) */
            next = is_main_proc ? "icn_prog_end" : pfail_func_name;
        }
        (void)last_succ;
        WI("  (func $%s (result i32)  return_call $%s)  ;; chain %d->%d\n",
           chain_names[i], next, i, i+1);
        (void)next_buf;
    }

    /* Emit retcont trampolines for non-main procs (IW-15: two-slot) */
    if (!is_main_proc) {
        /* success path: retcont_pop returns esucc_idx */
        WI("  (func $%s (result i32)\n", retcont_func);
        WI("    call $icn_retcont_pop\n");
        WI("    return_call_indirect (type $cont_t))\n");
        /* fail path: proc fell off end without return */
        WI("  (func $%s (result i32)\n", pfail_func_name);
        WI("    call $icn_retcont_pop_fail\n");
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
    emit_wasm_set_out(out);   /* IW-12: sync shared wasm_out — emit_wasm_data_segment() uses W() */

    /* Prescan: intern all E_QLIT strings so globals declared before funcs */
    emit_wasm_strlit_reset();
    icn_retcont_reset();
    icn_proc_reg_reset();  /* IW-10: populate name→nparams before emitting calls */
    for (int i = 0; i < count; i++) {
        if (procs[i] && procs[i]->kind == E_FNC && procs[i]->sval &&
            procs[i]->nchildren >= 1 &&
            procs[i]->children[0] && procs[i]->children[0]->sval &&
            strcmp(procs[i]->sval, procs[i]->children[0]->sval) == 0)
            icn_proc_reg_add(procs[i]->sval, (int)procs[i]->ival);
    }
    for (int i = 0; i < count; i++)
        icn_prescan_node(procs[i]);

    WI(";; Generated by scrip-cc -icn -wasm (IW-8)\n");
    WI("(module\n");

    WI("  ;; M-IW-P01: continuation type for return_call_indirect\n");
    WI("  (type $cont_t (func (result i32)))\n");
    WI("  ;; Memory + base runtime imports shared with SNOBOL4 (emit_wasm.h)\n");
    emit_wasm_runtime_imports_sno_base(icon_wasm_out, 3,
        "page0=output/heap page1=str literals page2=gen state page3=frame/retcont stack");
    /* Icon-specific: no additional sno-namespace imports beyond base set */

    emit_wasm_icon_globals(out);
    emit_wasm_icon_str_globals(out);
    emit_wasm_data_segment();

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
