/*
 * emit_wasm_prolog.c — Prolog IR → WebAssembly text-format emitter
 *
 * Entry point: prolog_emit_wasm(Program *prog, FILE *out, const char *filename)
 * Called from driver/main.c when -pl -wasm flags are both set.
 *
 * Design:
 *   Shared EKinds (E_QLIT/ILIT/FLIT, arithmetic) live in emit_wasm.c (SW session).
 *   All Prolog-specific EKinds handled here only.
 *   Byrd-box ports encoded as WAT tail-call functions (WASM has no goto).
 *   Runtime imports from "pl" namespace (pl_runtime.wat).
 *
 * Memory layout used by this emitter:
 *   [0..8191]       output buffer (pl_runtime.wat)
 *   [8192..32767]   atom table: atom_id*8 → {i32 str_off, i32 str_len}
 *                   (emitted as (data) block by emit_pl_atom_table())
 *   [32768..49151]  variable env frames: slot_addr = 32768 + env_idx*64 + slot*4
 *   [49152..57343]  trail stack (pl_runtime.wat)
 *   [57344..131071] term heap (pl_runtime.wat)
 *   [65536..]       string literal data (emit_wasm.c STR_DATA_BASE)
 *
 * Variable binding:
 *   var slot stores the string offset of the bound atom (i32).
 *   write(X): load slot → off; off+4 → len; call output_str.
 *   Wait — atom table gives us (off,len) by atom_id.
 *   var slot stores atom_id (i32). 0 = unbound.
 *   write(X): load atom_id from slot → lookup atom_table[id*8] = off,
 *             atom_table[id*8+4] = len → call output_str.
 *   Head unification: compare call-arg atom_id with clause atom_id.
 *
 * Predicate encoding (generate-and-test / M-PW-A01):
 *   Each predicate foo/N emits:
 *     - A mutable global $pl_foo_N_ci (clause index, init 0)
 *     - (func $pl_foo_N_call (param $trail i32) (param $a0..aN-1 i32) (result i32))
 *       Tries clause[ci], on match binds vars + increments ci, returns 1.
 *       On no match increments ci, tries next. Exhausted: reset ci, return 0.
 *   Caller wraps in (loop) to get all solutions.
 *
 * Milestones:
 *   M-PW-SCAFFOLD  (PW-1 2026-03-30)
 *   M-PW-HELLO     write/1 atom + nl/0 (PW-2 2026-03-30)
 *   M-PW-A01       E_CHOICE/CLAUSE/UNIFY + ; disjunction + var binding (PW-5 2026-03-31)
 */

#include "scrip_cc.h"
#include "emit_wasm.h"
#include "../frontend/prolog/prolog_atom.h"
#include "../frontend/prolog/prolog_parse.h"
#include "../frontend/prolog/prolog_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ── Output ────────────────────────────────────────────────────────────── */
static FILE *wpl_out = NULL;
static void W(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(wpl_out, fmt, ap); va_end(ap);
}

static Program *g_prog = NULL;  /* set at emit start; used by emit_goals for pred lookup */

/* ── Memory layout constants ───────────────────────────────────────────── */
#define ATOM_TABLE_BASE 8192   /* atom_id*8 → {i32 off, i32 len} */
#define ENV_BASE        32768  /* variable env frames             */
#define ENV_STRIDE      64     /* bytes per clause instance       */

static int g_clause_env_idx = 0;  /* bumped per clause emitted */

/* Main-context flag: set while emitting main/0 body goals.
 * When set, predicate calls must use (call) not (return_call) so sequential
 * goals in main execute in order.  γ/ω are concrete funcref indices, not params. */
static int g_in_main        = 0;
static int g_main_gamma_idx = -1;  /* funcref index of pl_main_nop_gamma */
static int g_main_omega_idx = -1;  /* funcref index of pl_main_nop_omega  */

/* Head-param → clause-slot mapping for current clause being emitted.
 * Lets emit_goal pass local.get $a{i} instead of clause slot addr for
 * vars that were bound from head params (fixes recursive body call writeback). */
static int g_head_var_slot[32];  /* g_head_var_slot[ai] = clause-slot addr, -1 if not a simple var */
static int g_head_arity = 0;

static int env_slot_addr(int env_idx, int slot) {
    return ENV_BASE + env_idx * ENV_STRIDE + slot * 4;
}

/* ── Atom table ────────────────────────────────────────────────────────── */
/* Maps atom name → sequential integer id.
 * atom_id 0 = "" (unbound sentinel).
 * Atom table emitted as (data) block at ATOM_TABLE_BASE.
 * Entry[id] = { i32 str_off, i32 str_len }  (8 bytes each).
 */
#define MAX_ATOMS 512
static char *atom_names[MAX_ATOMS];
static int   atom_str_off[MAX_ATOMS];   /* absolute string offset */
static int   atom_str_len[MAX_ATOMS];
static int   atom_count = 0;

static int atom_intern(const char *name) {
    if (!name) name = "";
    for (int i = 0; i < atom_count; i++)
        if (strcmp(atom_names[i], name) == 0) return i;
    if (atom_count >= MAX_ATOMS) return 0;
    int idx = emit_wasm_strlit_intern(name);
    atom_names[atom_count]   = strdup(name);
    atom_str_off[atom_count] = emit_wasm_strlit_abs(idx);
    atom_str_len[atom_count] = emit_wasm_strlit_len(idx);
    return atom_count++;
}

/* Emit the atom table as a WAT (data) block */
static void emit_pl_atom_table(void) {
    if (atom_count == 0) return;
    W("  ;; Atom table at %d: atom_id*8 → {i32 str_off, i32 str_len}\n",
      ATOM_TABLE_BASE);
    W("  (data (i32.const %d)\n   \"", ATOM_TABLE_BASE);
    for (int i = 0; i < atom_count; i++) {
        int off = atom_str_off[i];
        int len = atom_str_len[i];
        /* little-endian i32 for off */
        W("\\%02x\\%02x\\%02x\\%02x",
          off & 0xff, (off>>8)&0xff, (off>>16)&0xff, (off>>24)&0xff);
        /* little-endian i32 for len */
        W("\\%02x\\%02x\\%02x\\%02x",
          len & 0xff, (len>>8)&0xff, (len>>16)&0xff, (len>>24)&0xff);
    }
    W("\")\n");
}

/* ── Name mangling ─────────────────────────────────────────────────────── */
static char mangle_buf[512];
static const char *pl_mangle(const char *functor, int arity) {
    int di = 0;
    mangle_buf[di++]='p'; mangle_buf[di++]='l'; mangle_buf[di++]='_';
    for (const char *s = functor; *s && di < 480; s++) {
        char c = *s;
        if (isalnum((unsigned char)c) || c == '_') mangle_buf[di++] = c;
        else {
            mangle_buf[di++]='_';
            mangle_buf[di++]="0123456789abcdef"[(unsigned char)c>>4];
            mangle_buf[di++]="0123456789abcdef"[(unsigned char)c&0xf];
        }
    }
    mangle_buf[di++]='_';
    if (arity>=10) mangle_buf[di++]='0'+arity/10;
    mangle_buf[di++]='0'+arity%10;
    mangle_buf[di]='\0';
    return mangle_buf;
}

/* ── Runtime imports ───────────────────────────────────────────────────── */
static void emit_pl_runtime_imports(void) {
    W(";; Prolog WASM runtime imports (pl_runtime.wat)\n");
    W("  (import \"pl\" \"memory\"         (memory 3))\n");
    W("  (import \"pl\" \"trail_mark\"      (func $trail_mark      (result i32)))\n");
    W("  (import \"pl\" \"trail_unwind\"    (func $trail_unwind    (param i32)))\n");
    W("  (import \"pl\" \"output_str\"      (func $pl_output_str   (param i32 i32)))\n");
    W("  (import \"pl\" \"output_flush\"    (func $pl_output_flush (result i32)))\n");
    W("  (import \"pl\" \"output_nl\"       (func $pl_output_nl))\n");
    W("  (import \"pl\" \"unify_atom\"      (func $pl_unify_atom   (param i32 i32) (result i32)))\n");
    W("  (import \"pl\" \"var_bind\"        (func $pl_var_bind     (param i32 i32)))\n");
    W("  (import \"pl\" \"var_deref\"       (func $pl_var_deref    (param i32) (result i32)))\n");
    W("  (import \"pl\" \"int_to_atom\"     (func $pl_int_to_atom  (param i32) (result i32)))\n");
    W("  (import \"pl\" \"atom_to_int\"     (func $pl_atom_to_int  (param i32) (result i32)))\n");
    W("  (import \"pl\" \"cons\"            (func $pl_cons         (param i32 i32) (result i32)))\n");
    W("  (import \"pl\" \"is_cons\"         (func $pl_is_cons      (param i32) (result i32)))  \n");
    W("  (import \"pl\" \"cons_head\"       (func $pl_cons_head    (param i32) (result i32)))\n");
    W("  (import \"pl\" \"cons_tail\"       (func $pl_cons_tail    (param i32) (result i32)))\n");
    W("  (import \"pl\" \"cp_push\"         (func $pl_cp_push      (param i32 i32 i32 i32 i32 i32 i32 i32)))\n");
    W("  (import \"pl\" \"cp_get_ci\"       (func $pl_cp_get_ci    (result i32)))\n");
    W("  (import \"pl\" \"cp_set_ci\"       (func $pl_cp_set_ci    (param i32)))\n");
    W("  (import \"pl\" \"cp_get_arg\"      (func $pl_cp_get_arg   (param i32) (result i32)))\n");
    W("  (import \"pl\" \"cp_set_arg\"      (func $pl_cp_set_arg   (param i32 i32)))\n");
    W("  (import \"pl\" \"cp_get_trail_mark\" (func $pl_cp_get_trail_mark (result i32)))\n");
    W("  (import \"pl\" \"cp_pop\"          (func $pl_cp_pop))\n");
    /* Continuation type: (trail i32) → i32 — used by return_call_indirect for γ/ω */
    W("  (type $pl_cont_t (func (param i32) (result i32)))\n");
    W("\n");
}

/* ── Split "foo/2" → functor + arity ──────────────────────────────────── */
static void split_pred(const char *sval, char *functor_out, int *arity_out) {
    strncpy(functor_out, sval, 127); functor_out[127]='\0';
    char *sl = strrchr(functor_out, '/');
    if (sl) { *arity_out = atoi(sl+1); *sl='\0'; }
    else     { *arity_out = 0; }
}

/* ── emit_write_var: write atom bound in variable slot ─────────────────── */
static void emit_write_var(int env_idx, int slot) {
    int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
    W("    ;; write(Var slot=%d): load atom_id, lookup table → off+len\n", slot);
    /* atom_id = load(slot_addr) */
    W("    (local.tee $tmp (i32.load (i32.const %d)))\n", addr);
    /* table_entry = ATOM_TABLE_BASE + atom_id * 8 */
    W("    (i32.const 3) (i32.shl)  ;; *8\n");
    W("    (i32.const %d) (i32.add) ;; + ATOM_TABLE_BASE\n", ATOM_TABLE_BASE);
    W("    (local.tee $tbl_entry)\n");
    /* off = load(entry) */
    W("    (i32.load)               ;; str_off\n");
    /* len = load(entry+4) */
    W("    (local.get $tbl_entry) (i32.const 4) (i32.add) (i32.load) ;; str_len\n");
    W("    (call $pl_output_str)\n");
}

/* ── emit_write_atom_lit: write a literal atom string ─────────────────── */
static void emit_write_atom_lit(const char *name) {
    int idx = emit_wasm_strlit_intern(name);
    int off = emit_wasm_strlit_abs(idx);
    int len = emit_wasm_strlit_len(idx);
    W("    ;; write('%s') off=%d len=%d\n", name, off, len);
    W("    (i32.const %d) (i32.const %d) (call $pl_output_str)\n", off, len);
}

/* ── emit_write_goal ─────────────────────────────────────────────────── */
static void emit_write_goal(const EXPR_t *arg, int env_idx) {
    if (!arg) return;
    if (arg->kind == E_VAR) {
        emit_write_var(env_idx, (int)arg->ival);
        return;
    }
    if (arg->kind == E_FNC && arg->sval && arg->nchildren == 0) {
        emit_write_atom_lit(arg->sval); return;
    }
    if (arg->kind == E_QLIT && arg->sval) {
        emit_write_atom_lit(arg->sval); return;
    }
    if (arg->kind == E_ILIT) {
        char nb[32]; snprintf(nb,sizeof nb,"%ld",arg->ival);
        emit_write_atom_lit(nb); return;
    }
    W("    ;; write/1 arg kind=%d stub\n    unreachable\n", (int)arg->kind);
}

/* ── Continuation funcref table ─────────────────────────────────────────
 * Each predicate's α/β functions take (trail, a0..an-1, γ_idx, ω_idx).
 * On success → return_call_indirect γ_idx; on failure → return_call_indirect ω_idx.
 * γ and ω are indices into the module funcref table.
 * All continuation functions share type: (param i32) (result i32)
 *   param = trail (passed through so γ/ω can do trail_unwind on backtrack)
 * ─────────────────────────────────────────────────────────────────────── */
#define MAX_CONT_FUNCS 128
static char cont_func_names[MAX_CONT_FUNCS][256];
static int  cont_func_count = 0;
static int  gt_site_counter = 0;

static int cont_register(const char *name) {
    for (int i = 0; i < cont_func_count; i++)
        if (strcmp(cont_func_names[i], name) == 0) return i;
    if (cont_func_count >= MAX_CONT_FUNCS) return 0;
    strncpy(cont_func_names[cont_func_count], name, 255);
    return cont_func_count++;
}

/* GT scratch cells for ground arg storage: [8000..8127] (32 i32 cells).
 * At each GT call site, ground args are stored here so γ can pass them
 * back to α on each re-call (the runtime cons-cell pointer doesn't change). */
#define GT_SCRATCH_BASE  8000
#define GT_SCRATCH_CELLS 32
static int gt_scratch_used = 0;  /* bumped per ground arg slot allocated */

/* Body-GT flag cells: [8128..8187] (15 i32 cells, 4 bytes each).
 * Cell 0 (8128): PL_SET_ARG_FLAG — set by cp_set_arg emission in [H|T] head unification.
 * Signals the outer GT loop that the clause did a tail-update (beta clause),
 * so loop should retry at same ci rather than advancing. */
#define PL_BGT_FLAG_BASE  8128
#define PL_BGT_FLAG_CELLS 15
#define PL_SET_ARG_FLAG   8128   /* mem[8128]: 1 if cp_set_arg was called this iteration */
static int bgt_site_counter = 0;  /* reset per prolog_emit_wasm call */

/* GT clause-index cells: [7872..7999] (32 i32 cells, 4 bytes each).
 * Each GT site gets one cell: GT_CI_BASE + site_id*4.
 * Initialized to 0 before each GT call; incremented by γ before looping.
 * γ reads the counter and dispatches to the correct clause (alpha/betaN). */
#define GT_CI_BASE   7872
#define GT_CI_CELLS  32

/* GT result flag: mem[PL_GT_FLAG] — γ writes 1 (solution found), ω writes 0 (exhausted).
 * Main's (loop) polls this after each _call returns to decide whether to run body goals. */
#define PL_GT_FLAG   8188

#define MAX_GT_SITES   64
#define MAX_GT_BODY    32
typedef struct {
    int  site_id;
    char mangled[256];        /* e.g. "pl_member_2" */
    int  arity;
    int  arg_slots[16];       /* slot addrs for var args (-1 if ground) */
    int  ground_cells[16];    /* scratch cell addr for ground args (-1 if var) */
    int  n_body_goals;        /* goals between pred call and fail */
    const EXPR_t *body_goals[MAX_GT_BODY];
    int  env_idx;             /* clause env_idx for body goal emit */
    int  gamma_idx;           /* funcref table index */
    int  omega_idx;
    int  nclauses;            /* number of clauses */
    int  ci_cell_addr;        /* memory address of clause-index counter (GT_CI_BASE + site_id*4) */
    char beta_fns[32][320];   /* beta_fns[0]="$pl_foo_alpha", [1]="$pl_foo_beta1"... */
    int  is_body_gt;          /* 1 = body-call site: inner γ/ω just set flag, no body goals */
    int  flag_addr;           /* PL_BGT_FLAG_BASE + bsite*4 for body-GT sites */
} GTSiteData;
static GTSiteData gt_site_data[MAX_GT_SITES];
static int        gt_site_total = 0;

/* ── emit_pl_predicate ──────────────────────────────────────────────────
 *
 * Byrd-box α/β/γ/ω encoding via WASM tail calls.
 *
 * For predicate foo/N with K clauses, emit:
 *
 *   $pl_foo_N_α(trail, a0..aN-1, γ_idx, ω_idx) → i32
 *     Try clause 0. On head-match: run body, return_call_indirect γ_idx.
 *     On head-fail: return_call $pl_foo_N_β1 (try clause 1).
 *
 *   $pl_foo_N_β1(trail, a0..aN-1, γ_idx, ω_idx) → i32
 *     trail_unwind. Try clause 1. On match: body, γ. On fail: β2.
 *     ...
 *   $pl_foo_N_βK(trail, a0..aN-1, γ_idx, ω_idx) → i32
 *     ω: return_call_indirect ω_idx.
 *
 * γ and ω are funcref table indices. All continuation functions have type:
 *   (param $trail i32) (result i32)
 *
 * ─────────────────────────────────────────────────────────────────────── */
static void emit_goals(const EXPR_t *g, int env_idx, int in_disj_left);

static void emit_pl_predicate(const EXPR_t *choice) {
    if (!choice || choice->kind != E_CHOICE) return;

    char functor[128]; int arity;
    split_pred(choice->sval, functor, &arity);

    char mname[256];
    strncpy(mname, pl_mangle(functor, arity), 255);

    int nclauses = choice->nchildren;

    W("\n  ;; predicate %s/%d (%d clause(s)) — Byrd-box α/β encoding\n",
      functor, arity, nclauses);

    /* Emit one function per clause: α for clause 0, β{ci} for clause ci≥1.
     * Each also handles "fall to next" via return_call to next β. */
    for (int ci = 0; ci < nclauses; ci++) {
        EXPR_t *clause = choice->children[ci];
        if (!clause || clause->kind != E_CLAUSE) continue;

        int n_args = (int)clause->dval;
        int env_idx = g_clause_env_idx++;

        /* Function name: α for first clause, β{ci} for rest */
        char fn_this[320], fn_next[320];
        if (ci == 0)
            snprintf(fn_this, sizeof fn_this, "$pl_%s_alpha", mname + 3); /* skip "pl_" */
        else
            snprintf(fn_this, sizeof fn_this, "$pl_%s_beta%d", mname + 3, ci);

        if (ci + 1 < nclauses) {
            if (ci + 1 == 1)
                snprintf(fn_next, sizeof fn_next, "$pl_%s_beta1", mname + 3);
            else
                snprintf(fn_next, sizeof fn_next, "$pl_%s_beta%d", mname + 3, ci + 1);
        } else {
            /* Last clause: next = ω (call_indirect ω_idx) */
            snprintf(fn_next, sizeof fn_next, "__omega__");
        }

        W("  ;; clause %d — %s\n", ci, fn_this);
        W("  (func %s\n", fn_this);
        W("    (param $trail i32)");
        for (int a = 0; a < arity; a++) W(" (param $a%d i32)", a);
        W(" (param $gamma_idx i32) (param $omega_idx i32)\n");
        W("    (result i32)\n");
        W("    (local $tm i32)\n");

        /* Trail mark for this clause attempt */
        W("    (local.set $tm (call $trail_mark))\n");

        /* Head unification block — br $head_fail on mismatch */
        W("    (block $head_fail\n");

        int head_var_slot[32];
        for (int ai = 0; ai < 32; ai++) head_var_slot[ai] = -1;

        for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
            EXPR_t *harg = clause->children[ai];
            if (!harg) continue;

            if ((harg->kind == E_FNC && harg->sval && harg->nchildren == 0) ||
                 harg->kind == E_QLIT) {
                int atom_id = atom_intern(harg->sval);
                W("      ;; head arg %d: atom '%s'\n", ai, harg->sval);
                W("      (if (i32.ge_u (local.get $a%d) (i32.const %d))\n", ai, ENV_BASE);
                W("        (then (i32.store (local.get $a%d) (i32.const %d)))\n", ai, atom_id);
                W("        (else\n");
                W("          (i32.ne (local.get $a%d) (i32.const %d))\n", ai, atom_id);
                W("          (br_if $head_fail)\n");
                W("      ))\n");

            } else if (harg->kind == E_VAR) {
                int slot = (int)harg->ival;
                if (slot >= 0) {
                    int addr = env_slot_addr(env_idx, slot);
                    W("      ;; head arg %d: var _V%d → clause slot %d\n", ai, slot, addr);
                    W("      (i32.const %d) (local.get $a%d) (call $pl_var_bind)\n", addr, ai);
                    if (ai < 32) head_var_slot[ai] = addr;
                }

            } else if (harg->kind == E_FNC && harg->sval &&
                       strcmp(harg->sval, ".") == 0 && harg->nchildren == 2) {
                W("      ;; head arg %d: cons [H|T]\n", ai);
                W("      (i32.eqz (call $pl_is_cons (local.get $a%d)))\n", ai);
                W("      (br_if $head_fail)\n");
                EXPR_t *hh = harg->children[0];
                if (hh && hh->kind == E_VAR && (int)hh->ival >= 0) {
                    int addr = env_slot_addr(env_idx, (int)hh->ival);
                    W("      (i32.const %d) (call $pl_cons_head (local.get $a%d)) (call $pl_var_bind)\n", addr, ai);
                }
                EXPR_t *ht = harg->children[1];
                if (ht && ht->kind == E_VAR && (int)ht->ival >= 0) {
                    int addr = env_slot_addr(env_idx, (int)ht->ival);
                    W("      (i32.const %d) (call $pl_cons_tail (local.get $a%d)) (call $pl_var_bind)\n", addr, ai);
                    /* Update CP frame arg[ai] to the tail so the outer GT loop retry
                     * passes the reduced list when it re-enters beta1.
                     * With per-call inner CP frames (PW-15), cp_set_arg updates the
                     * INNER frame (top of stack), leaving the outer frame intact. */
                    W("      (call $pl_cp_set_arg (i32.const %d) (i32.load (i32.const %d)))\n", ai, addr);
                    /* Signal outer GT loop: tail was updated, retry at same ci */
                    W("      (i32.store (i32.const %d) (i32.const 1)) ;; set_arg_flag\n",
                      PL_SET_ARG_FLAG);
                }
            }
        }

        /* Head matched — run body goals then γ */
        {
            /* Build γ/ω function names for body goals */
            /* Body goals in clause ci: pass gamma_idx through, omega_fn = next clause β */
            char body_omega[320];
            if (strcmp(fn_next, "__omega__") == 0) {
                /* Last clause: ω_idx propagates to caller's ω */
                snprintf(body_omega, sizeof body_omega, "__caller_omega__");
            } else {
                snprintf(body_omega, sizeof body_omega, "%s", fn_next);
            }

            int n_body = clause->nchildren - n_args;

            /* Publish head-param→slot mapping so emit_goal can pass caller slot
             * addrs through recursive body calls (fixes rung05 writeback). */
            memset(g_head_var_slot, -1, sizeof g_head_var_slot);
            g_head_arity = n_args;
            for (int ai = 0; ai < n_args && ai < 32; ai++)
                g_head_var_slot[ai] = head_var_slot[ai];

            if (n_body > 0) {
                for (int bi = n_args; bi < clause->nchildren; bi++) {
                    emit_goals(clause->children[bi], env_idx, 0);
                }
            }

            /* Output-var writeback before calling γ */
            for (int ai = 0; ai < n_args && ai < 32; ai++) {
                if (head_var_slot[ai] < 0) continue;
                int clause_addr = head_var_slot[ai];
                W("      ;; writeback _V%d → caller slot if needed\n", ai);
                W("      (if (i32.ge_u (local.get $a%d) (i32.const %d))\n", ai, ENV_BASE);
                W("        (then\n");
                W("          (if (i32.ne (i32.load (i32.const %d)) (local.get $a%d))\n",
                  clause_addr, ai);
                W("            (then (i32.store (local.get $a%d) (i32.load (i32.const %d))))\n",
                  ai, clause_addr);
                W("          )\n");
                W("        )\n");
                W("      )\n");
            }

            /* γ: succeed to caller — return_call_indirect gamma_idx */
            W("      ;; γ — head+body matched, call continuation γ\n");
            W("      (local.get $trail)\n");
            W("      (local.get $gamma_idx)\n");
            W("      (return_call_indirect (type $pl_cont_t))\n");
        }

        W("    ) ;; $head_fail — br here on head mismatch\n");

        /* Head failed — trail_unwind and try next clause (β) */
        W("    (call $trail_unwind (local.get $tm))\n");

        if (strcmp(fn_next, "__omega__") == 0) {
            /* Last clause exhausted → call caller's ω */
            W("    ;; ω — all clauses tried, propagate failure\n");
            W("    (local.get $trail)\n");
            W("    (local.get $omega_idx)\n");
            W("    (return_call_indirect (type $pl_cont_t))\n");
        } else {
            /* Try next clause */
            W("    ;; β — try next clause %s\n", fn_next);
            W("    (local.get $trail)");
            for (int a = 0; a < arity; a++) W(" (local.get $a%d)", a);
            W(" (local.get $gamma_idx) (local.get $omega_idx)\n");
            W("    (return_call %s)\n", fn_next);
        }

        W("  )\n");

    }

    /* $pl_foo_N_call(trail, a0..N, gamma_idx, omega_idx, ci) — GT dispatcher.
     * GT loop in main calls this with ci from a WAT local (per-activation).
     * ci=0→alpha, ci=1→beta1, ..., ci>=nclauses→omega (exhausted).
     * Recursive calls inside clause bodies call alpha/beta DIRECTLY — they
     * never use this wrapper, so main's ci local is not corrupted. */
    W("\n  ;; $pl_%s_call — GT ci-dispatcher\n", mname + 3);
    W("  (func $pl_%s_call\n", mname + 3);
    W("    (param $trail i32)");
    for (int a = 0; a < arity; a++) W(" (param $a%d i32)", a);
    W(" (param $gamma_idx i32) (param $omega_idx i32) (param $ci i32)\n");
    W("    (result i32)\n");
    for (int ci2 = 0; ci2 < nclauses; ci2++) {
        char cfn[320];
        if (ci2 == 0) snprintf(cfn, sizeof cfn, "$pl_%s_alpha", mname + 3);
        else          snprintf(cfn, sizeof cfn, "$pl_%s_beta%d", mname + 3, ci2);
        W("    (if (i32.eq (local.get $ci) (i32.const %d)) (then\n", ci2);
        W("      (local.get $trail)");
        for (int a = 0; a < arity; a++) W(" (local.get $a%d)", a);
        W(" (local.get $gamma_idx) (local.get $omega_idx)\n");
        W("      (return_call %s)))\n", cfn);
    }
    W("    ;; ci >= nclauses: exhausted — call omega\n");
    W("    (local.get $trail) (local.get $omega_idx)\n");
    W("    (return_call_indirect (type $pl_cont_t))\n");
    W("  )\n");

}

/* ── emit_unify_terms ────────────────────────────────────────────────────
 * Recursively unify two IR terms (children of E_UNIFY).
 * Handles: var↔atom, atom↔var, atom↔atom, var↔var, compound↔compound.
 * Compound: check functor/arity statically; recurse on args.
 */
static void emit_unify_terms(const EXPR_t *lhs, const EXPR_t *rhs, int env_idx) {
    if (!lhs || !rhs) return;

    int lhs_is_atom = (lhs->kind == E_FNC  && lhs->sval && lhs->nchildren == 0) ||
                      (lhs->kind == E_QLIT && lhs->sval);
    int rhs_is_atom = (rhs->kind == E_FNC  && rhs->sval && rhs->nchildren == 0) ||
                      (rhs->kind == E_QLIT && rhs->sval);
    int lhs_is_var  = (lhs->kind == E_VAR);
    int rhs_is_var  = (rhs->kind == E_VAR);

    /* var ↔ atom: bind slot to atom_id */
    if (lhs_is_var && rhs_is_atom) {
        int slot = (int)lhs->ival;
        int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
        int aid  = atom_intern(rhs->sval);
        W("    ;; unify: _V%d = '%s' (id=%d)  [var←atom]\n", slot, rhs->sval, aid);
        W("    (call $pl_var_bind (i32.const %d) (i32.const %d))\n", addr, aid);
        return;
    }

    /* atom ↔ var: bind slot to atom_id */
    if (lhs_is_atom && rhs_is_var) {
        int slot = (int)rhs->ival;
        int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
        int aid  = atom_intern(lhs->sval);
        W("    ;; unify: '%s' = _V%d (id=%d)  [atom→var]\n", lhs->sval, slot, aid);
        W("    (call $pl_var_bind (i32.const %d) (i32.const %d))\n", addr, aid);
        return;
    }

    /* atom ↔ atom: ids must match */
    if (lhs_is_atom && rhs_is_atom) {
        int la = atom_intern(lhs->sval);
        int ra = atom_intern(rhs->sval);
        W("    ;; unify: '%s'=%d vs '%s'=%d  [atom=atom]\n",
          lhs->sval, la, rhs->sval, ra);
        if (la != ra) {
            W("    unreachable ;; static unification failure: '%s' != '%s'\n",
              lhs->sval, rhs->sval);
        }
        return;
    }

    /* var ↔ var: bind lhs slot to rhs slot's current value */
    if (lhs_is_var && rhs_is_var) {
        int ls = (int)lhs->ival; int la = env_slot_addr(env_idx, ls < 0 ? 0 : ls);
        int rs = (int)rhs->ival; int ra = env_slot_addr(env_idx, rs < 0 ? 0 : rs);
        W("    ;; unify: _V%d = _V%d  [var=var]\n", ls, rs);
        W("    (call $pl_var_bind (i32.const %d) (i32.load (i32.const %d)))\n", la, ra);
        return;
    }

    /* int literal ↔ var */
    if (lhs->kind == E_ILIT && rhs_is_var) {
        int slot = (int)rhs->ival;
        int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
        W("    ;; unify: %ld = _V%d  [int→var]\n", lhs->ival, slot);
        W("    (call $pl_var_bind (i32.const %d) (i32.const %ld))\n", addr, lhs->ival);
        return;
    }
    if (rhs->kind == E_ILIT && lhs_is_var) {
        int slot = (int)lhs->ival;
        int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
        W("    ;; unify: _V%d = %ld  [var←int]\n", slot, rhs->ival);
        W("    (call $pl_var_bind (i32.const %d) (i32.const %ld))\n", addr, rhs->ival);
        return;
    }

    /* compound ↔ compound: check functor/arity, recurse on args */
    int lhs_is_cmp = (lhs->kind == E_FNC && lhs->sval && lhs->nchildren > 0);
    int rhs_is_cmp = (rhs->kind == E_FNC && rhs->sval && rhs->nchildren > 0);
    if (lhs_is_cmp && rhs_is_cmp) {
        if (strcmp(lhs->sval, rhs->sval) != 0 ||
            lhs->nchildren != rhs->nchildren) {
            W("    unreachable ;; static unification failure: %s/%d vs %s/%d\n",
              lhs->sval, lhs->nchildren, rhs->sval, rhs->nchildren);
            return;
        }
        W("    ;; unify compound %s/%d arg by arg\n", lhs->sval, lhs->nchildren);
        for (int i = 0; i < lhs->nchildren; i++)
            emit_unify_terms(lhs->children[i], rhs->children[i], env_idx);
        return;
    }

    W("    ;; STUB unify lhs_kind=%d rhs_kind=%d\n    unreachable\n",
      (int)lhs->kind, (int)rhs->kind);
}

/* ── emit_term_value: emit an i32 term value onto WASM stack ────────────
 * Atoms   → atom_id (i32)
 * Cons    → call $pl_cons(head, tail) → tagged pointer
 * Var     → load from env slot (dereferenced value)
 * []      → atom_id of "[]"
 */
static void emit_term_value(const EXPR_t *e, int env_idx) {
    if (!e) { W("    (i32.const 0)\\n"); return; }

    /* Variable: load current value from slot */
    if (e->kind == E_VAR) {
        int slot = (int)e->ival;
        if (slot < 0) {
            W("    (i32.const 0) ;; anon var\\n");
            return;
        }
        int addr = env_slot_addr(env_idx, slot);
        W("    (i32.load (i32.const %d)) ;; var _V%d value\n", addr, slot);
        return;
    }

    /* Atom literal (0-arity functor or quoted) */
    if ((e->kind == E_FNC && e->sval && e->nchildren == 0) ||
        e->kind == E_QLIT) {
        W("    (i32.const %d) ;; atom '%s'\n", atom_intern(e->sval), e->sval);
        return;
    }

    /* Integer literal */
    if (e->kind == E_ILIT) {
        char nb[32]; snprintf(nb, sizeof nb, "%ld", e->ival);
        W("    (i32.const %d) ;; int atom '%s'\n", atom_intern(nb), nb);
        return;
    }

    /* Cons cell: E_FNC "." with 2 children = [Head|Tail] */
    if (e->kind == E_FNC && e->sval && strcmp(e->sval, ".") == 0 && e->nchildren == 2) {
        emit_term_value(e->children[0], env_idx);  /* head */
        emit_term_value(e->children[1], env_idx);  /* tail */
        W("    (call $pl_cons)\n");
        return;
    }

    W("    (i32.const 0) ;; term stub kind=%d\\n", (int)e->kind);
}

/* ── emit_arith_i32: emit inline i32 arithmetic for is/2 RHS ──────────── */
/* Pushes one i32 value onto the WASM stack. */
static void emit_arith_i32(const EXPR_t *e, int env_idx) {
    if (!e) { W("    (i32.const 0)\n"); return; }
    switch (e->kind) {
    case E_ILIT:
        W("    (i32.const %ld)\n", e->ival);
        return;
    case E_VAR: {
        int slot = (int)e->ival;
        int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
        /* var holds atom_id; convert to int */
        W("    (call $pl_atom_to_int (i32.load (i32.const %d)))\n", addr);
        return;
    }
    case E_FNC:
        if (e->sval && e->nchildren == 0) {
            /* atom that looks like an integer literal */
            W("    (i32.const %s)\n", e->sval);
            return;
        }
        break;
    case E_ADD:
        emit_arith_i32(e->children[0], env_idx);
        emit_arith_i32(e->children[1], env_idx);
        W("    (i32.add)\n");
        return;
    case E_SUB:
        emit_arith_i32(e->children[0], env_idx);
        emit_arith_i32(e->children[1], env_idx);
        W("    (i32.sub)\n");
        return;
    case E_MPY:
        emit_arith_i32(e->children[0], env_idx);
        emit_arith_i32(e->children[1], env_idx);
        W("    (i32.mul)\n");
        return;
    case E_DIV:
        emit_arith_i32(e->children[0], env_idx);
        emit_arith_i32(e->children[1], env_idx);
        W("    (i32.div_s)\n");
        return;
    case E_MOD:
        emit_arith_i32(e->children[0], env_idx);
        emit_arith_i32(e->children[1], env_idx);
        W("    (i32.rem_s)\n");
        return;
    case E_NEG:
        emit_arith_i32(e->children[0], env_idx);
        W("    (i32.const -1)\n    (i32.mul)\n");
        return;
    default:
        break;
    }
    W("    (i32.const 0) ;; arith stub kind=%d\n", (int)e->kind);
}

/* ── emit_arith_lhs_addr: get i32 address of LHS var for is/2 ─────────── */
static int emit_is_lhs_addr(const EXPR_t *lhs, int env_idx) {
    if (!lhs) return ENV_BASE;
    if (lhs->kind == E_VAR) {
        int slot = (int)lhs->ival;
        return env_slot_addr(env_idx, slot < 0 ? 0 : slot);
    }
    return ENV_BASE; /* fallback */
}

static void emit_goals(const EXPR_t *g, int env_idx, int in_disj_left);

static void emit_goal(const EXPR_t *goal, int env_idx, int in_disj_left) {
    if (!goal) return;

    if (goal->kind == E_SEQ) {
        for (int i = 0; i < goal->nchildren; i++)
            emit_goal(goal->children[i], env_idx, in_disj_left);
        return;
    }

    /* E_UNIFY: =/2 — structural unification */
    if (goal->kind == E_UNIFY) {
        if (goal->nchildren >= 2)
            emit_unify_terms(goal->children[0], goal->children[1], env_idx);
        return;
    }

    if (goal->kind != E_FNC || !goal->sval) {
        W("    ;; STUB goal kind=%d\n    unreachable\n", (int)goal->kind);
        return;
    }

    const char *fn = goal->sval;

    /* Conjunction */
    if (strcmp(fn, ",") == 0) {
        for (int i = 0; i < goal->nchildren; i++)
            emit_goal(goal->children[i], env_idx, in_disj_left);
        return;
    }

    /* Disjunction (;/2): Left ; Right
     * Left branch runs as a generate-and-test loop.
     * If Left definitively fails (reaches fail/0), br $disj_end → Right runs.
     */
    if (strcmp(fn, ";") == 0 && goal->nchildren >= 2) {
        /* Check for (;/2 (->/2 Cond Then) Else) — if-then-else */
        const EXPR_t *left = goal->children[0];
        if (left && left->kind == E_FNC && left->sval &&
            strcmp(left->sval, "->") == 0 && left->nchildren >= 2) {
            /* (Cond -> Then ; Else):
             * Two nested blocks:
             *   (block $ite_end          ;; outer: br here = done
             *     (block $cond_fail      ;; inner: br here = cond failed
             *       Cond (br_if $cond_fail on failure)
             *       Then
             *       br $ite_end          ;; skip else
             *     )  ;; $cond_fail falls through here
             *     Else
             *   )  ;; $ite_end
             */
            W("    ;; (Cond -> Then ; Else)\n");
            W("    (block $ite_end\n");
            W("      (block $cond_fail\n");
            W("        ;; Condition\n");
            emit_goals(left->children[0], env_idx, /*in_disj_left=*/1);
            W("        ;; Then branch (condition succeeded)\n");
            emit_goals(left->children[1], env_idx, 0);
            W("        (br $ite_end)\n");
            W("      ) ;; $cond_fail\n");
            W("      ;; Else branch\n");
            emit_goals(goal->children[1], env_idx, 0);
            W("    ) ;; $ite_end\n");
        } else {
            W("    ;; (;/2) disjunction\n");
            W("    (block $disj_end\n");
            W("      ;; Left branch\n");
            emit_goals(left, env_idx, /*in_disj_left=*/1);
            W("      (br $disj_end)\n");
            W("      ;; Right branch\n");
            emit_goals(goal->children[1], env_idx, 0);
            W("    ) ;; $disj_end\n");
        }
        return;
    }

    /* ->/2 outside of ;/2 (bare if-then, no else): Cond must succeed */
    if (strcmp(fn, "->") == 0 && goal->nchildren >= 2) {
        W("    ;; (Cond -> Then) bare if-then\n");
        emit_goals(goal->children[0], env_idx, 0);
        emit_goals(goal->children[1], env_idx, 0);
        return;
    }

    /* nl/0 */
    if (strcmp(fn, "nl") == 0) { W("    (call $pl_output_nl)\n"); return; }

    /* is/2 — arithmetic evaluation: LHS is Var, RHS is arith expr */
    if (strcmp(fn, "is") == 0 && goal->nchildren >= 2) {
        const EXPR_t *lhs = goal->children[0];
        const EXPR_t *rhs = goal->children[1];
        int lhs_addr = emit_is_lhs_addr(lhs, env_idx);
        W("    ;; is/2: eval RHS arith → int_to_atom → bind LHS var\n");
        /* pl_var_bind(slot, val): push slot addr first, then val */
        W("    (i32.const %d)\n", lhs_addr);  /* slot addr */
        emit_arith_i32(rhs, env_idx);          /* val (i32) */
        W("    (call $pl_int_to_atom)\n");     /* val → atom_id */
        W("    (call $pl_var_bind)\n");
        return;
    }

    /* Comparison ops: </2 >/2 =</2 >=/2 =:=/2 =\=/2
     * In context (Cond -> Then ; Else): if condition fails, br to $disj_end.
     * Outside disjunction: we emit as a conditional that falls through on success.
     * We use in_disj_left to know if we should br on failure. */
    if (goal->nchildren >= 2 &&
        (strcmp(fn, "<") == 0 || strcmp(fn, ">") == 0 ||
         strcmp(fn, "=<") == 0 || strcmp(fn, ">=") == 0 ||
         strcmp(fn, "=:=") == 0 || strcmp(fn, "=\=") == 0)) {
        const EXPR_t *a = goal->children[0];
        const EXPR_t *b = goal->children[1];
        W("    ;; comparison %s/2\n", fn);
        emit_arith_i32(a, env_idx);
        emit_arith_i32(b, env_idx);
        /* Emit comparison; result i32: 1=true, 0=false */
        if      (strcmp(fn, "<")   == 0) W("    (i32.lt_s)\n");
        else if (strcmp(fn, ">")   == 0) W("    (i32.gt_s)\n");
        else if (strcmp(fn, "=<")  == 0) W("    (i32.le_s)\n");
        else if (strcmp(fn, ">=")  == 0) W("    (i32.ge_s)\n");
        else if (strcmp(fn, "=:=") == 0) W("    (i32.eq)\n");
        else if (strcmp(fn, "=\=") == 0) W("    (i32.ne)\n");
        /* if result == 0 (false): branch to $disj_end (skip Then, run Else) */
        if (in_disj_left)
            W("    (i32.eqz) (br_if $cond_fail)\n");
        else
            W("    (drop) ;; comparison result (non-disj context)\n");
        return;
    }

    /* write/1 */
    if (strcmp(fn, "write") == 0 || strcmp(fn, "writeln") == 0) {
        if (goal->nchildren >= 1) emit_write_goal(goal->children[0], env_idx);
        if (strcmp(fn, "writeln") == 0) W("    (call $pl_output_nl)\n");
        return;
    }

    /* halt/0 */
    if (strcmp(fn, "halt") == 0) {
        W("    (call $pl_output_flush) drop\n    return\n"); return;
    }

    /* true/0 */
    if (strcmp(fn, "true") == 0) { W("    ;; true/0\n"); return; }

    /* fail/0 — in left branch of (;): br to $disj_end (right branch) */
    if (strcmp(fn, "fail") == 0) {
        W("    ;; fail/0 → exit generate-and-test loop\n");
        if (in_disj_left) W("    (br $disj_end)\n");
        else              W("    unreachable ;; fail/0 outside disj\n");
        return;
    }

    /* Predicate call: foo(Arg, ...) */
    {
        int n = goal->nchildren;
        char mangled[256];
        strncpy(mangled, pl_mangle(fn, n), 255);

        /* For generate-and-test: wrap call in (loop $retry) */
        if (in_disj_left && n > 0) {
            W("    ;; generate-and-test loop: %s/%d\n", fn, n);
            W("    (loop $retry_%s\n", mangled);
            W("      (block $exhausted_%s\n", mangled);
            /* Push trail param */
            W("        (local.get $trail)\n");
            /* Push call args */
            for (int ai = 0; ai < n; ai++) {
                EXPR_t *arg = goal->children[ai];
                if (!arg) { W("        (i32.const 0)\n"); continue; }
                /* Pass term value (atom_id or tagged cons pointer) */
                emit_term_value(arg, env_idx);
            }
            W("        (call $%s_call)\n", mangled);
            /* result 0 = fail → break out of retry loop */
            W("        (i32.eqz) (br_if $exhausted_%s)\n", mangled);
            /* result 1 = success: the goals AFTER this call in the conjunction
             * are emitted inside the retry loop — but we need to emit the
             * rest of the conjunction here. That requires a continuation.
             * For now: the caller (emit_goals for conjunction) handles this
             * by emitting remaining goals after the loop in a "post-solve" block.
             * We signal "loop body starts here" by leaving the loop open.
             * Caller closes it after emitting the body goals + (br $retry_...).
             */
            /* NOTE: loop body (write + nl) emitted by emit_goals continuation below */
            /* We return a sentinel so the caller knows to close the loop */
            /* Actually: emit the loop body here inline by peeking at parent context */
            /* This requires restructuring — instead, use a different approach:
             * emit the ENTIRE conjunction as a loop when it contains a predicate
             * call followed by goals followed by fail. */
            W("      ) ;; $exhausted_%s\n", mangled);
            W("    ) ;; $retry_%s\n", mangled);
            return;
        }

        /* Body predicate call — two cases:
         *   1-clause predicate (deterministic): return_call α passing parent γ/ω.
         *      α succeeds → return_call_indirect γ (tail to parent continuation).
         *      α fails    → return_call_indirect ω (tail to parent failure).
         *   N-clause predicate (may have multiple solutions, e.g. member/2):
         *      Push own CP frame, inline body-GT loop, poll per-site flag.
         *      Inner γ: sets flag=1, returns 0 (WAT unwinds back to loop).
         *      On flag=1: pop CP frame, writeback vars, return_call_indirect γ (first solution).
         *      On flag=0 (ω fired): pop CP frame, return_call_indirect ω.
         *
         * PW-15: removed return_call for multi-clause case to fix rung05 (member/2).
         * Old code passed outer γ/ω into recursive call, causing outer CP frame corruption.
         */

        /* Look up clause count for called predicate */
        int body_nclauses = 1;
        if (g_prog) {
            char pred_key[256];
            snprintf(pred_key, sizeof pred_key, "%s/%d", fn, n);
            for (STMT_t *s = g_prog->head; s; s = s->next) {
                if (!s->subject) continue;
                EXPR_t *ch = s->subject;
                if (ch->kind == E_CHOICE && ch->sval &&
                    strcmp(ch->sval, pred_key) == 0) {
                    body_nclauses = ch->nchildren;
                    break;
                }
            }
        }

        if (body_nclauses == 1) {
            /* Deterministic: direct tail-call through parent γ/ω */
            W("    ;; call %s/%d (det, Byrd-box α → parent γ/ω)\n", fn, n);
            W("    (local.get $trail)\n");
            for (int ai = 0; ai < n; ai++) {
                EXPR_t *arg = goal->children[ai];
                if (!arg) { W("    (i32.const 0)\n"); continue; }
                if (arg->kind == E_VAR && (int)arg->ival >= 0) {
                    int addr = env_slot_addr(env_idx, (int)arg->ival);
                    int hp = -1;
                    for (int hi = 0; hi < g_head_arity; hi++)
                        if (g_head_var_slot[hi] == addr) { hp = hi; break; }
                    if (hp >= 0)
                        W("    (local.get $a%d) ;; caller slot for _V%d\n", hp, (int)arg->ival);
                    else
                        W("    (i32.load (i32.const %d)) ;; value of _V%d\n", addr, (int)arg->ival);
                } else {
                    emit_term_value(arg, env_idx);
                }
            }
            if (g_in_main) {
                W("    (i32.const %d) ;; main gamma_idx\n", g_main_gamma_idx);
                W("    (i32.const %d) ;; main omega_idx\n", g_main_omega_idx);
                W("    (call $pl_%s_alpha) drop\n", mangled + 3);
            } else {
                W("    (local.get $gamma_idx) (local.get $omega_idx)\n");
                W("    (return_call $pl_%s_alpha)\n", mangled + 3);
            }
            return;
        }

        /* Multi-clause body call */
        W("    ;; call %s/%d (multi-clause, Byrd-box α → outer γ/ω)\n", fn, n);
        W("    (local.get $trail)\n");
        for (int ai = 0; ai < n; ai++) {
            EXPR_t *arg = goal->children[ai];
            if (!arg) { W("    (i32.const 0)\n"); continue; }
            if (arg->kind == E_VAR && (int)arg->ival >= 0) {
                int addr = env_slot_addr(env_idx, (int)arg->ival);
                int hp = -1;
                for (int hi = 0; hi < g_head_arity; hi++)
                    if (g_head_var_slot[hi] == addr) { hp = hi; break; }
                if (hp >= 0)
                    W("    (local.get $a%d) ;; caller slot for _V%d\n", hp, (int)arg->ival);
                else
                    W("    (i32.load (i32.const %d)) ;; value of _V%d\n", addr, (int)arg->ival);
            } else {
                emit_term_value(arg, env_idx);
            }
        }
        if (g_in_main) {
            W("    (i32.const %d) ;; main gamma_idx\n", g_main_gamma_idx);
            W("    (i32.const %d) ;; main omega_idx\n", g_main_omega_idx);
            W("    (call $pl_%s_alpha) drop\n", mangled + 3);
        } else {
            W("    (local.get $gamma_idx) (local.get $omega_idx)\n");
            W("    (return_call $pl_%s_alpha)\n", mangled + 3);
        }
        return;
    }
}

/* emit_goals: emit a list of goals from a conjunction or single node */
static void emit_goals(const EXPR_t *g, int env_idx, int in_disj_left) {
    if (!g) return;
    if (g->kind == E_FNC && g->sval && strcmp(g->sval, ",") == 0) {
        /* Detect generate-and-test pattern:
         * foo(X), ..., fail — where foo is a defined predicate
         * Emit as a retry loop wrapping all goals between foo and fail. */
        /* Check: first child is a predicate call, last child is fail */
        int nc = g->nchildren;
        if (nc >= 2 && in_disj_left) {
            EXPR_t *first = g->children[0];
            EXPR_t *last  = g->children[nc-1];
            int first_is_pred = (first && first->kind == E_FNC &&
                                 first->sval && first->nchildren > 0 &&
                                 strcmp(first->sval, "write") != 0 &&
                                 strcmp(first->sval, "nl")    != 0 &&
                                 strcmp(first->sval, "true")  != 0 &&
                                 strcmp(first->sval, "fail")  != 0 &&
                                 strcmp(first->sval, "halt")  != 0);
            int last_is_fail  = (last && last->kind == E_FNC &&
                                 last->sval && strcmp(last->sval, "fail") == 0);

            if (first_is_pred && last_is_fail) {
                /* Generate-and-test via Byrd-box α call.
                 * We emit two small continuation functions per call site:
                 *   $gt_gamma_N: called on each solution — emit body goals, br $retry
                 *   $gt_omega_N: called on exhaustion — br $disj_end
                 * These are registered in the funcref table.
                 * α is called once; it tail-calls γ on success or ω on failure.
                 * γ does the body goals then calls α again (next solution).
                 * But γ/ω can't access main's locals directly as separate functions.
                 *
                 * SIMPLER APPROACH: call α with sentinel table indices that
                 * write a flag to a memory cell, then inspect the flag.
                 * Even simpler: use a global flag written by γ/ω trampolines.
                 *
                 * SIMPLEST (correct): emit continuation funcs that write result
                 * to a dedicated memory cell and return 0. Poll after α returns.
                 * γ writes 1 to PL_CONT_RESULT (mem[4]), ω writes 0.
                 * α always returns 0 (via return_call_indirect which may return).
                 */
                EXPR_t *pred_call = first;
                int n_call_args = pred_call->nchildren;
                char mangled[256];
                strncpy(mangled, pl_mangle(pred_call->sval, n_call_args), 255);

                /* Collect arg slot addresses for output vars */
                int arg_slots[32]; /* slot addr or -1 */
                for (int ai = 0; ai < 32; ai++) arg_slots[ai] = -1;
                for (int ai = 0; ai < n_call_args; ai++) {
                    EXPR_t *arg = pred_call->children[ai];
                    if (arg && arg->kind == E_VAR && (int)arg->ival >= 0)
                        arg_slots[ai] = env_slot_addr(env_idx, (int)arg->ival);
                }

                /* Register γ and ω continuation functions.
                 * γ: emits body goals, resets var slots, return_call $alpha
                 *    (gets the next solution — proper Byrd-box continuation)
                 * ω: called when α exhausts all clauses — just returns 0 (done)
                 *
                 * γ must be a real WAT function, not a flag trampoline, because
                 * return_call_indirect from α ends that activation.  The only way
                 * to get back into α for the next solution is via a tail-call from γ.
                 */
                int site_id = gt_site_counter++;
                char gamma_name[64], omega_name[64];
                snprintf(gamma_name, sizeof gamma_name, "pl_gt_gamma_%d", site_id);
                snprintf(omega_name, sizeof omega_name, "pl_gt_omega_%d", site_id);
                int gamma_idx = cont_register(gamma_name);
                int omega_idx = cont_register(omega_name);

                /* Store site data for emit_cont_functions_and_table */
                if (gt_site_total < MAX_GT_SITES) {
                    GTSiteData *sd = &gt_site_data[gt_site_total++];
                    sd->site_id   = site_id;
                    strncpy(sd->mangled, mangled, 255);
                    sd->arity     = n_call_args;
                    for (int ai = 0; ai < 16; ai++) {
                        sd->arg_slots[ai]    = (ai < n_call_args) ? arg_slots[ai] : -1;
                        sd->ground_cells[ai] = -1;
                    }
                    /* Allocate scratch cells for ground args */
                    for (int ai = 0; ai < n_call_args; ai++) {
                        if (arg_slots[ai] < 0 && gt_scratch_used < GT_SCRATCH_CELLS) {
                            sd->ground_cells[ai] = GT_SCRATCH_BASE + gt_scratch_used * 4;
                            gt_scratch_used++;
                        }
                    }
                    /* Body goals: children[1..nc-2] (skip pred call and fail) */
                    int nb = 0;
                    for (int gi = 1; gi < nc - 1 && nb < MAX_GT_BODY; gi++)
                        sd->body_goals[nb++] = g->children[gi];
                    sd->n_body_goals = nb;
                    sd->env_idx   = env_idx;
                    sd->gamma_idx = gamma_idx;
                    sd->omega_idx = omega_idx;
                    /* Look up clause count so γ can call β1 instead of α for multi-clause preds */
                    sd->nclauses = 1;
                    if (g_prog) {
                        char pred_key[256];
                        snprintf(pred_key, sizeof pred_key, "%s/%d",
                                 pred_call->sval, n_call_args);
                        for (STMT_t *s = g_prog->head; s; s = s->next) {
                            if (!s->subject) continue;
                            EXPR_t *ch = s->subject;
                            if (ch->kind == E_CHOICE && ch->sval &&
                                strcmp(ch->sval, pred_key) == 0) {
                                sd->nclauses = ch->nchildren;
                                break;
                            }
                        }
                    }
                    /* Build beta_fns array: [0]=alpha, [1]=beta1, [2]=beta2, ... */
                    sd->ci_cell_addr = GT_CI_BASE + site_id * 4;
                    {
                        int nc = sd->nclauses > 32 ? 32 : sd->nclauses;
                        /* clause 0 → alpha */
                        snprintf(sd->beta_fns[0], 320, "$pl_%s_alpha", sd->mangled + 3);
                        for (int bi = 1; bi < nc; bi++)
                            snprintf(sd->beta_fns[bi], 320, "$pl_%s_beta%d",
                                     sd->mangled + 3, bi);
                    }
                }

                W("    ;; generate-and-test (Byrd-box): %s/%d ... fail\n",
                  pred_call->sval, n_call_args);
                W("    ;; γ_idx=%d ω_idx=%d (γ is a real body-goal function)\n",
                  gamma_idx, omega_idx);

                /* Reset var slots before first α call */
                for (int ai = 0; ai < n_call_args; ai++) {
                    if (arg_slots[ai] >= 0)
                        W("    (i32.store (i32.const %d) (i32.const 0)) ;; clear V%d\n",
                          arg_slots[ai], ai);
                }

                /* GT loop model (CP-stack, M-PW-B01):
                 *   cp_push(pred_id, ci=0, trail_mark, a0..a4)
                 *   (loop $gt_N)
                 *     clear V slots; call $pl_foo_N_call(trail, args, γ, ω, cp_get_ci())
                 *     if PL_GT_FLAG==1: γ already called cp_set_ci(ci+1); br $gt_N
                 *     else: ω fired — cp_pop(); fall through
                 *   Each recursive call inside a clause body pushes its OWN frame,
                 *   so inner-frame ci never touches outer-frame ci. */
                GTSiteData *sd_cur = (gt_site_total > 0) ? &gt_site_data[gt_site_total-1] : NULL;
                (void)sd_cur;

                /* Capture ground arg values to emit into cp_push */
                /* We need up to 5 args (pad with 0) */
                int cp_arity = (n_call_args > 5) ? 5 : n_call_args;

                /* Initialize GT flag and clear var slots before loop */
                W("    (i32.store (i32.const %d) (i32.const 0)) ;; GT flag init\n",
                  PL_GT_FLAG);
                for (int ai = 0; ai < n_call_args; ai++) {
                    if (arg_slots[ai] >= 0)
                        W("    (i32.store (i32.const %d) (i32.const 0)) ;; clear V%d\n",
                          arg_slots[ai], ai);
                }

                /* Push choice-point frame: cp_push(pred_id, 0, trail, a0..a4) */
                W("    ;; CP push for GT site %d (%s/%d)\n", site_id, pred_call->sval, n_call_args);
                W("    (i32.const %d)            ;; pred_id (site_id)\n", site_id);
                W("    (i32.const 0)             ;; ci=0\n");
                W("    (call $trail_mark)        ;; trail_mark snapshot\n");
                for (int ai = 0; ai < 5; ai++) {
                    if (ai < n_call_args) {
                        EXPR_t *arg = pred_call->children[ai];
                        if (!arg) { W("    (i32.const 0) ;; a%d=null\n", ai); }
                        else if (arg_slots[ai] >= 0)
                            W("    (i32.const %d) ;; a%d=slot addr V%d\n", arg_slots[ai], ai, ai);
                        else
                            emit_term_value(arg, env_idx); /* ground term */
                    } else {
                        W("    (i32.const 0) ;; a%d=unused\n", ai);
                    }
                }
                W("    (call $pl_cp_push)\n");
                (void)cp_arity;

                W("    (loop $gt_%d\n", site_id);
                W("      (i32.store (i32.const %d) (i32.const 0)) ;; GT flag reset\n", PL_GT_FLAG);
                W("      (i32.store (i32.const %d) (i32.const 0)) ;; set_arg_flag reset\n",
                  PL_SET_ARG_FLAG);

                /* Call _call wrapper with ci from CP top */
                W("      (local.get $trail)\n");
                for (int ai = 0; ai < n_call_args; ai++) {
                    if (arg_slots[ai] >= 0)
                        W("      (i32.const %d) ;; slot addr V%d\n", arg_slots[ai], ai);
                    else
                        W("      (call $pl_cp_get_arg (i32.const %d)) ;; ground a%d\n", ai, ai);
                }
                W("      (i32.const %d) ;; gamma_idx\n", gamma_idx);
                W("      (i32.const %d) ;; omega_idx\n", omega_idx);
                W("      (call $pl_cp_get_ci)    ;; ci from CP top\n");
                W("      (call $pl_%s_call) drop\n", mangled + 3);

                /* γ fired: advance ci only for direct head-match (SET_ARG_FLAG=0).
                 * If SET_ARG_FLAG=1, a beta clause updated the tail via cp_set_arg —
                 * stay at same ci so beta retries with the new tail next iteration. */
                W("      (if (i32.load (i32.const %d)) (then\n", PL_GT_FLAG);
                W("        (if (i32.eqz (i32.load (i32.const %d))) (then\n", PL_SET_ARG_FLAG);
                W("          ;; direct match (alpha): advance ci for next clause\n");
                W("          (call $pl_cp_set_ci\n");
                W("            (i32.add (call $pl_cp_get_ci) (i32.const 1)))\n");
                W("        ))\n");
                W("        ;; beta tail-update: ci stays, loop retries beta with new tail\n");
                W("        (br $gt_%d)\n", site_id);
                W("      ))\n");
                W("    ) ;; $gt_%d\n", site_id);
                /* ω pops its own frame before returning — no extra cp_pop needed */

                W("    ;; ω fired — all solutions exhausted\n");
                W("    (br $disj_end)\n");
                return;
            }
        }

        /* Plain conjunction */
        for (int i = 0; i < g->nchildren; i++)
            emit_goal(g->children[i], env_idx, in_disj_left);
        return;
    }
    emit_goal(g, env_idx, in_disj_left);
}

/* ── collect_gentest_preds: collect mangled names of predicates used in
 * generate-and-test (conjunction starting with pred call ending in fail)
 * so we can declare their ci locals at the top of the calling function. */
#define MAX_GT_PREDS 32
static char gt_pred_names[MAX_GT_PREDS][256];
static int  gt_pred_count = 0;

static void collect_gt_expr(const EXPR_t *g) {
    if (!g) return;
    /* Detect conjunction: (,/N first ... last) where first=pred, last=fail */
    if (g->kind == E_FNC && g->sval && strcmp(g->sval, ",") == 0 && g->nchildren >= 2) {
        EXPR_t *first = g->children[0];
        EXPR_t *last  = g->children[g->nchildren - 1];
        int first_is_pred = (first && first->kind == E_FNC && first->sval &&
                             first->nchildren > 0 &&
                             strcmp(first->sval, "write") != 0 &&
                             strcmp(first->sval, "nl")    != 0 &&
                             strcmp(first->sval, "true")  != 0 &&
                             strcmp(first->sval, "fail")  != 0 &&
                             strcmp(first->sval, "halt")  != 0);
        int last_is_fail = (last && last->kind == E_FNC && last->sval &&
                            strcmp(last->sval, "fail") == 0);
        if (first_is_pred && last_is_fail && gt_pred_count < MAX_GT_PREDS) {
            char mangled[256];
            strncpy(mangled, pl_mangle(first->sval, first->nchildren), 255);
            /* Deduplicate */
            int found = 0;
            for (int i = 0; i < gt_pred_count; i++)
                if (strcmp(gt_pred_names[i], mangled) == 0) { found = 1; break; }
            if (!found)
                strncpy(gt_pred_names[gt_pred_count++], mangled, 255);
        }
    }
    for (int i = 0; i < g->nchildren; i++) collect_gt_expr(g->children[i]);
}

/* ── emit_pl_main ──────────────────────────────────────────────────────── */
static void emit_pl_main(Program *prog) {
    /* Prescan main clause to collect generate-and-test predicate names */
    gt_pred_count = 0;
    if (prog) {
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            EXPR_t *g = s->subject;
            if (g->kind != E_CHOICE) continue;
            if (!g->sval || strcmp(g->sval, "main/0") != 0) continue;
            if (g->nchildren != 1) continue;
            EXPR_t *clause = g->children[0];
            if (!clause || clause->kind != E_CLAUSE) continue;
            int n_args = (int)clause->dval;
            for (int bi = n_args; bi < clause->nchildren; bi++)
                collect_gt_expr(clause->children[bi]);
            break;
        }
    }

    W("  (func (export \"main\") (result i32)\n");
    W("    (local $trail i32)\n");
    W("    (local $tmp i32)\n");
    W("    (local $tbl_entry i32)\n");
    W("    (local.set $trail (call $trail_mark))\n");

    if (!prog) goto done;

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        EXPR_t *g = s->subject;
        if (g->kind != E_CHOICE) continue;
        if (!g->sval || strcmp(g->sval, "main/0") != 0) continue;
        if (g->nchildren != 1) continue;

        EXPR_t *clause = g->children[0];
        if (!clause || clause->kind != E_CLAUSE) continue;

        int n_args = (int)clause->dval;
        int n_vars = (int)clause->ival;
        int env_idx = g_clause_env_idx++;
        W("    ;; main/0 (n_vars=%d env=%d)\n", n_vars, env_idx);

        /* Register terminal continuations for top-level sequential calls.
         * pl_main_nop_gamma: succeeds silently (main continues sequentially).
         * pl_main_nop_omega: failure from top-level call — just return 0. */
        g_main_gamma_idx = cont_register("pl_main_nop_gamma");
        g_main_omega_idx = cont_register("pl_main_nop_omega");
        g_in_main = 1;
        for (int bi = n_args; bi < clause->nchildren; bi++)
            emit_goals(clause->children[bi], env_idx, 0);
        g_in_main = 0;
        break;
    }

done:
    W("    (call $pl_output_flush)\n");
    W("  )\n");
}

/* ── prescan ───────────────────────────────────────────────────────────── */
static void prescan_expr(const EXPR_t *g) {
    if (!g) return;
    if (g->kind == E_QLIT && g->sval) atom_intern(g->sval);
    if (g->kind == E_FNC  && g->sval) {
        /* Always intern for atom table (both ground atoms and functor names) */
        atom_intern(g->sval);
    }
    if (g->kind == E_ILIT) {
        char nb[32]; snprintf(nb,sizeof nb,"%ld",g->ival);
        atom_intern(nb);
    }
    for (int i = 0; i < g->nchildren; i++) prescan_expr(g->children[i]);
}

static void prescan_prog(Program *prog) {
    emit_wasm_strlit_intern("");   /* index 0 = empty */
    atom_intern("");               /* atom_id 0 = unbound */
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        prescan_expr(s->subject);
        prescan_expr(s->pattern);
        prescan_expr(s->replacement);
    }
}

/* ── emit_cont_functions: emit γ/ω continuation functions + funcref table ─
 *
 * γ function for site N:
 *   (func $pl_gt_gamma_N (param $trail i32) (result i32)
 *     ;; run body goals (write(X), nl, etc.) using bound var slots
 *     ;; reset var slots for next iteration
 *     ;; return_call $pl_FOO_alpha  ← get next solution
 *   )
 *
 * ω function for site N:
 *   (func $pl_gt_omega_N (param $trail i32) (result i32)
 *     (i32.const 0)
 *   )
 *
 * Called after all predicates and main are emitted.
 */
static void emit_cont_functions_and_table(void) {
    if (cont_func_count == 0) return;
    W("  ;; Continuation functions (γ/ω) for generate-and-test\n");

    for (int i = 0; i < cont_func_count; i++) {
        const char *name = cont_func_names[i];
        int is_gamma     = (strstr(name, "_gamma_") != NULL);
        int is_body_gt   = (strstr(name, "_bgt_gamma_") != NULL ||
                            strstr(name, "_bgt_omega_") != NULL);
        int is_main_nop  = (strstr(name, "_nop_gamma") != NULL ||
                            strstr(name, "_nop_omega")  != NULL);

        W("  (func $%s (param $trail i32) (result i32)\n", name);

        if (is_main_nop) {
            /* pl_main_nop_gamma / pl_main_nop_omega: trivial continuations for
             * top-level sequential calls from main.  γ succeeds silently; ω fails
             * silently.  main already uses (call) not (return_call) so it continues
             * sequentially regardless. */
            W("    ;; main nop continuation — return 0\n");
            W("    (i32.const 0)\n");
        } else if (is_body_gt) {
            /* Body-GT inner γ/ω: just set the per-site flag, return 0.
             * The inline loop in the clause body handles cp_pop and continuation. */
            /* Extract bsite index from suffix */
            const char *p = strrchr(name, '_');
            int bsite_id = p ? atoi(p + 1) : 0;
            int bflag = PL_BGT_FLAG_BASE + bsite_id * 4;
            if (is_gamma) {
                W("    ;; body-GT inner γ (bsite=%d): set flag@%d, return 0\n",
                  bsite_id, bflag);
                W("    (i32.store (i32.const %d) (i32.const 1))\n", bflag);
            } else {
                W("    ;; body-GT inner ω (bsite=%d): clear flag@%d, return 0\n",
                  bsite_id, bflag);
                W("    (i32.store (i32.const %d) (i32.const 0))\n", bflag);
            }
            W("    (i32.const 0)\n");
        } else if (is_gamma) {
            /* Main-GT γ: emit body goals, trail_unwind, set PL_GT_FLAG, return 0 */
            W("    (local $tmp i32)\n");
            W("    (local $tbl_entry i32)\n");
            /* Find the GTSiteData for this γ — match by site_id AND !is_body_gt */
            const char *p = strrchr(name, '_');
            int site_id = p ? atoi(p + 1) : -1;

            GTSiteData *sd = NULL;
            for (int si = 0; si < gt_site_total; si++) {
                if (gt_site_data[si].site_id == site_id &&
                    !gt_site_data[si].is_body_gt) {
                    sd = &gt_site_data[si]; break;
                }
            }

            if (sd) {
                W("    ;; γ for GT site %d (%s/%d): run body goals, return 0\n",
                  site_id, sd->mangled, sd->arity);
                /* NOTE: ci advancement moved to outer GT loop (conditional on SET_ARG_FLAG) */

                /* Signal main's loop: a solution was found */
                W("    (i32.store (i32.const %d) (i32.const 1)) ;; GT flag=1 (γ fired)\n",
                  PL_GT_FLAG);

                /* Emit body goals (write(X), nl, etc.) */
                for (int gi = 0; gi < sd->n_body_goals; gi++)
                    emit_goal(sd->body_goals[gi], sd->env_idx, 0);

                /* Unwind trail — undo this clause's bindings for next retry */
                W("    (call $trail_unwind (call $pl_cp_get_trail_mark))\n");

                /* Reset var slots so _call can rebind on next solution */
                for (int ai = 0; ai < sd->arity; ai++) {
                    if (sd->arg_slots[ai] >= 0)
                        W("    (i32.store (i32.const %d) (i32.const 0)) ;; reset V%d\n",
                          sd->arg_slots[ai], ai);
                }

                W("    (i32.const 0)\n");
            } else {
                W("    (i32.const 0) ;; γ stub (no site data)\n");
            }
        } else {
            /* Main-GT ω: pop CP frame, clear PL_GT_FLAG, return 0 */
            W("    ;; ω: pop CP frame + clear GT flag\n");
            W("    (call $pl_cp_pop)\n");
            W("    (i32.store (i32.const %d) (i32.const 0))\n", PL_GT_FLAG);
            W("    (i32.const 0)\n");
        }
        W("  )\n");
    }

    /* Funcref table — same type $pl_cont_t for all entries */
    W("  (table %d funcref)\n", cont_func_count);
    W("  (elem (i32.const 0)");
    for (int i = 0; i < cont_func_count; i++)
        W(" $%s", cont_func_names[i]);
    W(")\n");
}

/* ── Public entry point ────────────────────────────────────────────────── */
void prolog_emit_wasm(Program *prog, FILE *out, const char *filename) {
    (void)filename;
    wpl_out = out;
    g_prog  = prog;
    g_clause_env_idx = 0;
    atom_count = 0;
    cont_func_count = 0;
    gt_site_counter = 0;
    gt_site_total   = 0;
    gt_scratch_used = 0;
    bgt_site_counter = 0;
    memset(g_head_var_slot, -1, sizeof g_head_var_slot);
    g_head_arity = 0;

    emit_wasm_set_out(out);
    emit_wasm_strlit_reset();

    prescan_prog(prog);

    W(";; Generated by scrip-cc -pl -wasm (M-PW-B01)\n");
    W("(module\n\n");
    emit_pl_runtime_imports();
    emit_wasm_data_segment();    /* string literals at 65536 */
    W("\n");
    emit_pl_atom_table();        /* atom table at 8192 */
    W("\n");

    /* Emit all predicates (non-main E_CHOICE nodes) */
    if (prog) {
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            EXPR_t *g = s->subject;
            if (g->kind != E_CHOICE) continue;
            if (g->sval && strcmp(g->sval, "main/0") == 0) continue;
            emit_pl_predicate(g);
        }
    }

    W("\n");
    emit_pl_main(prog);
    W("\n");
    emit_cont_functions_and_table();
    W("\n) ;; end module\n");
}
