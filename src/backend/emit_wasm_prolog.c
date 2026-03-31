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

/* ── Memory layout constants ───────────────────────────────────────────── */
#define ATOM_TABLE_BASE 8192   /* atom_id*8 → {i32 off, i32 len} */
#define ENV_BASE        32768  /* variable env frames             */
#define ENV_STRIDE      64     /* bytes per clause instance       */

static int g_clause_env_idx = 0;  /* bumped per clause emitted */

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
    W("  (import \"pl\" \"memory\"         (memory 2))\n");
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

            /* Output-var writeback for E_VAR head args before body */
            /* Actually we need writeback AFTER body succeeds. For Byrd-box,
             * writeback happens when γ is called — but γ is the continuation.
             * Solution: emit writeback inline after body goals, before calling γ. */

            /* Emit body goals inline (they do writes/calls but don't branch to γ/ω) */
            /* For body goals that are non-backtracking (write, nl, is/2, recursive det calls):
             * emit them inline. The final γ call at end of body. */

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

        /* Body predicate call: call α with same γ/ω as current clause.
         * This propagates success/failure correctly through recursive calls.
         * The α function will return_call_indirect to γ on success (continuing
         * execution from the call site) or to ω on failure. */
        W("    ;; call %s/%d (Byrd-box α, pass through γ/ω)\n", fn, n);
        W("    (local.get $trail)\n");
        for (int ai = 0; ai < n; ai++) {
            EXPR_t *arg = goal->children[ai];
            if (!arg) { W("    (i32.const 0)\n"); continue; }
            /* For unbound vars in clause env: pass slot address so callee can bind */
            if (arg->kind == E_VAR && (int)arg->ival >= 0) {
                int addr = env_slot_addr(env_idx, (int)arg->ival);
                /* Check if slot holds a forwarded caller address or a real value */
                /* Pass the slot value — if it's ≥ ENV_BASE it's a slot addr, callee handles it */
                W("    (i32.load (i32.const %d)) ;; var _V%d\n", addr, (int)arg->ival);
            } else {
                emit_term_value(arg, env_idx);
            }
        }
        W("    (local.get $gamma_idx) (local.get $omega_idx)\n");
        W("    (return_call $pl_%s_alpha)\n", mangled + 3); /* mangled = "pl_foo_N", skip "pl_" → "foo_N", prefix "pl_" → "pl_foo_N_alpha" */
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

                /* Register γ and ω continuation functions in the table.
                 * They have type $pl_cont_t: (param $trail i32) (result i32).
                 * γ: writes 1 to mem[PL_GT_FLAG=4], returns 0
                 * ω: writes 0 to mem[PL_GT_FLAG=4], returns 0 */
                int site_id = gt_site_counter++;
                char gamma_name[64], omega_name[64];
                snprintf(gamma_name, sizeof gamma_name, "pl_gt_gamma_%d", site_id);
                snprintf(omega_name, sizeof omega_name, "pl_gt_omega_%d", site_id);
                int gamma_idx = cont_register(gamma_name);
                int omega_idx = cont_register(omega_name);

                W("    ;; generate-and-test (Byrd-box): %s/%d ... fail\n",
                  pred_call->sval, n_call_args);
                W("    ;; γ_idx=%d ω_idx=%d\n", gamma_idx, omega_idx);

                /* Zero + loop */
                for (int ai = 0; ai < n_call_args; ai++) {
                    if (arg_slots[ai] >= 0)
                        W("    (i32.store (i32.const %d) (i32.const 0)) ;; clear V%d\n",
                          arg_slots[ai], ai);
                }

                W("    (loop $retry_%s_%d\n", mangled, site_id);
                /* Reset var slots */
                for (int ai = 0; ai < n_call_args; ai++) {
                    if (arg_slots[ai] >= 0)
                        W("      (i32.store (i32.const %d) (i32.const 0))\n", arg_slots[ai]);
                }
                /* Call α: trail, args, gamma_idx, omega_idx */
                W("      (local.get $trail)\n");
                for (int ai = 0; ai < n_call_args; ai++) {
                    EXPR_t *arg = pred_call->children[ai];
                    if (!arg) { W("      (i32.const 0)\n"); continue; }
                    if (arg_slots[ai] >= 0)
                        W("      (i32.const %d) ;; slot addr V%d\n", arg_slots[ai], ai);
                    else
                        emit_term_value(arg, env_idx);
                }
                W("      (i32.const %d) ;; gamma_idx\n", gamma_idx);
                W("      (i32.const %d) ;; omega_idx\n", omega_idx);
                W("      (call $pl_%s_alpha)\n", mangled + 3);
                W("      drop\n");
                /* Poll flag at mem[4]: 1 = γ fired (solution), 0 = ω fired (done) */
                W("      (i32.load (i32.const 8188)) ;; PL_GT_FLAG\n");
                W("      (if (i32.eqz) (then) (else\n");
                /* Solution: emit body goals */
                for (int gi = 1; gi < nc - 1; gi++)
                    emit_goal(g->children[gi], env_idx, 0);
                W("      (br $retry_%s_%d)\n", mangled, site_id);
                W("      )) ;; if\n");
                W("    ) ;; $retry_%s_%d\n", mangled, site_id);
                W("    (br $disj_end) ;; exhausted → fail\n");
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
    /* Declare ci locals for each generate-and-test predicate */
    for (int i = 0; i < gt_pred_count; i++)
        W("    (local $ci_%s i32)\n", gt_pred_names[i]);
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

        for (int bi = n_args; bi < clause->nchildren; bi++)
            emit_goals(clause->children[bi], env_idx, 0);
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

/* ── emit_cont_functions: emit γ/ω continuation stubs + funcref table ───
 * γ stub: writes 1 to mem[4] (PL_GT_FLAG), returns 0
 * ω stub: writes 0 to mem[4], returns 0
 * Called after all predicates and main are emitted.
 */
static void emit_cont_functions_and_table(void) {
    if (cont_func_count == 0) return;
    W("  ;; Continuation stubs (γ/ω) for generate-and-test\n");
    W("  ;; mem[4] = PL_GT_FLAG: 1=success(γ), 0=failure(ω)\n");
    for (int i = 0; i < cont_func_count; i++) {
        const char *name = cont_func_names[i];
        int is_gamma = (strstr(name, "_gamma_") != NULL);
        W("  (func $%s (param $trail i32) (result i32)\n", name);
        W("    (i32.store (i32.const 8188) (i32.const %d)) ;; %s\n",
          is_gamma ? 1 : 0, is_gamma ? "γ: success" : "ω: failure");
        W("    (i32.const 0)\n");
        W("  )\n");
    }
    /* Funcref table */
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
    g_clause_env_idx = 0;
    atom_count = 0;
    cont_func_count = 0;
    gt_site_counter = 0;

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
