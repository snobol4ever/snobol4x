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

/* ── emit_pl_predicate ──────────────────────────────────────────────────
 *
 * Emit for each E_CHOICE (predicate with N clauses):
 *
 *   (global $pl_foo_1_ci (mut i32) (i32.const 0))
 *
 *   (func $pl_foo_1_call (param $trail i32) (param $a0 i32) (result i32)
 *     ;; Try clauses starting at $pl_foo_1_ci
 *     ;; Clause 0: if a0 matches atom_id of 'brown':
 *     ;;   bind any var args, incr ci, return 1
 *     ;; Clause 1: if a0 matches 'jones': ...
 *     ;; Exhausted: reset ci to 0, return 0
 *   )
 *
 * ci tracks resume point for generate-and-test.
 * On each successful call, ci is left pointing to the NEXT clause to try.
 * Caller loops calling foo until it returns 0.
 * ─────────────────────────────────────────────────────────────────────── */
static void emit_pl_predicate(const EXPR_t *choice) {
    if (!choice || choice->kind != E_CHOICE) return;

    char functor[128]; int arity;
    split_pred(choice->sval, functor, &arity);

    /* Use a fixed copy of mangle result before it gets overwritten */
    char mname[256];
    strncpy(mname, pl_mangle(functor, arity), 255);

    W("\n  ;; predicate %s/%d (%d clause(s))\n",
      functor, arity, choice->nchildren);

    /* Mutable global clause index */
    W("  (global $%s_ci (mut i32) (i32.const 0))\n", mname);

    /* Function signature */
    W("  (func $%s_call (param $trail i32)", mname);
    for (int a = 0; a < arity; a++) W(" (param $a%d i32)", a);
    W(" (result i32)\n");
    W("    (local $tm i32)\n");

    int nclauses = choice->nchildren;

    /* Dispatch on clause index using if/else chain */
    W("    ;; dispatch on clause index $%s_ci\n", mname);

    for (int ci = 0; ci < nclauses; ci++) {
        EXPR_t *clause = choice->children[ci];
        if (!clause || clause->kind != E_CLAUSE) continue;

        int n_args = (int)clause->dval;
        int n_vars = (int)clause->ival;
        int env_idx = g_clause_env_idx++;

        W("    ;; clause %d (env=%d n_args=%d n_vars=%d)\n",
          ci, env_idx, n_args, n_vars);

        /* Each clause: (if (i32.eq $ci N) (then ...) (else ...next...)) */
        /* We open (if here; the else+close is emitted after body */
        W("    (if (i32.eq (global.get $%s_ci) (i32.const %d))\n", mname, ci);
        W("      (then\n");
        W("        (local.set $tm (call $trail_mark))\n");

        /* Head argument unification */
        for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
            EXPR_t *harg = clause->children[ai];
            if (!harg) continue;

            if ((harg->kind == E_FNC && harg->sval && harg->nchildren == 0) ||
                (harg->kind == E_QLIT && harg->sval)) {
                /* Ground atom head: $aN is a slot address — bind it to atom_id */
                int atom_id = atom_intern(harg->sval);
                W("        ;; bind slot $a%d to atom '%s' (id=%d)\n",
                  ai, harg->sval, atom_id);
                W("        (i32.store (local.get $a%d) (i32.const %d))\n",
                  ai, atom_id);

            } else if (harg->kind == E_VAR) {
                /* Variable head arg: bind slot to call arg */
                int slot = (int)harg->ival;
                if (slot >= 0) {
                    int addr = env_slot_addr(env_idx, slot);
                    W("        ;; bind head var slot=%d addr=%d to a%d\n",
                      slot, addr, ai);
                    W("        (i32.const %d) (local.get $a%d) (call $pl_var_bind)\n",
                      addr, ai);
                }
            }
        }

        /* Body goals (facts have no body) */
        int n_body = clause->nchildren - n_args;
        if (n_body > 0) {
            W("        ;; body goals (%d) — stub until M-PW-B01\n", n_body);
        }

        /* Clause matched: advance ci to next clause, return 1 */
        W("        ;; success: advance ci to %d, return 1\n",
          ci + 1 < nclauses ? ci + 1 : nclauses);
        W("        (global.set $%s_ci (i32.const %d))\n",
          mname, ci + 1 < nclauses ? ci + 1 : nclauses);
        W("        (i32.const 1) (return)\n");
        W("      )\n"); /* close (then */
        /* else branch opened below — remaining clauses emit inside it,
         * or exhausted path if this is the last clause */
        if (ci < nclauses - 1)
            W("      (else\n");
    }

    /* Close all else+if chains (one ')' per non-first clause) */
    for (int ci = 1; ci < nclauses; ci++) W("      )\n"); /* close (else */
    /* Close all (if parens */
    for (int ci = 0; ci < nclauses; ci++) W("    )\n");
    W("\n");

    /* Exhausted: reset ci to 0, return 0 */
    W("    ;; ω — all clauses exhausted, reset ci\n");
    W("    (global.set $%s_ci (i32.const 0))\n", mname);
    W("    (i32.const 0)\n");
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
        W("    ;; (;/2) disjunction\n");
        W("    (block $disj_end\n");
        W("      ;; Left branch\n");
        emit_goals(goal->children[0], env_idx, /*in_disj_left=*/1);
        W("      (br $disj_end)\n");
        W("      ;; Right branch (unreachable if left succeeded and didn't fail)\n");
        emit_goals(goal->children[1], env_idx, 0);
        W("    ) ;; $disj_end\n");
        return;
    }

    /* nl/0 */
    if (strcmp(fn, "nl") == 0) { W("    (call $pl_output_nl)\n"); return; }

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
                if (arg->kind == E_VAR) {
                    /* Pass var slot address so predicate can bind it */
                    int slot = (int)arg->ival;
                    int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
                    /* For head unification: pass atom_id stored in slot.
                     * But slot is UNBOUND here — predicate needs to write to it.
                     * Solution: pass the SLOT ADDRESS (not its value) as arg.
                     * Predicate treats arg as address and var_binds it.
                     * Mark arg as address by passing ENV_BASE | slot_addr. */
                    W("        (i32.const %d) ;; var slot addr for _V%d\n",
                      addr, slot);
                } else if (arg->kind == E_FNC && arg->sval && arg->nchildren==0) {
                    int atom_id = atom_intern(arg->sval);
                    W("        (i32.const %d) ;; atom '%s'\n", atom_id, arg->sval);
                } else if (arg->kind == E_QLIT && arg->sval) {
                    int atom_id = atom_intern(arg->sval);
                    W("        (i32.const %d) ;; qlit '%s'\n", atom_id, arg->sval);
                } else if (arg->kind == E_ILIT) {
                    W("        (i32.const %ld)\n", arg->ival);
                } else {
                    W("        (i32.const 0) ;; unknown arg\n");
                }
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

        /* Non-backtracking call */
        W("    ;; call %s/%d\n", fn, n);
        W("    (local.get $trail)\n");
        for (int ai = 0; ai < n; ai++) {
            EXPR_t *arg = goal->children[ai];
            if (!arg) { W("    (i32.const 0)\n"); continue; }
            if (arg->kind == E_VAR) {
                int slot = (int)arg->ival;
                int addr = env_slot_addr(env_idx, slot < 0 ? 0 : slot);
                W("    (i32.const %d) ;; var addr _V%d\n", addr, slot);
            } else if (arg->kind == E_FNC && arg->sval && arg->nchildren==0) {
                W("    (i32.const %d) ;; atom '%s'\n",
                  atom_intern(arg->sval), arg->sval);
            } else if (arg->kind == E_ILIT) {
                W("    (i32.const %ld)\n", arg->ival);
            } else { W("    (i32.const 0)\n"); }
        }
        W("    (call $%s_call) drop\n", mangled);
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
                /* Generate-and-test: emit loop */
                EXPR_t *pred_call = first;
                int n_call_args = pred_call->nchildren;
                char mangled[256];
                strncpy(mangled, pl_mangle(pred_call->sval, n_call_args), 255);

                W("    ;; generate-and-test: %s/%d ... fail\n",
                  pred_call->sval, n_call_args);

                /* Zero out variable slots before loop */
                for (int ai = 0; ai < n_call_args; ai++) {
                    EXPR_t *arg = pred_call->children[ai];
                    if (arg && arg->kind == E_VAR && arg->ival >= 0) {
                        int addr = env_slot_addr(env_idx, (int)arg->ival);
                        W("    (i32.store (i32.const %d) (i32.const 0)) ;; clear _V%ld\n",
                          addr, arg->ival);
                    }
                }

                W("    (loop $retry_%s\n", mangled);
                /* Reset var slot(s) before each call — predicate will rebind */
                for (int ai = 0; ai < n_call_args; ai++) {
                    EXPR_t *arg = pred_call->children[ai];
                    if (arg && arg->kind == E_VAR && arg->ival >= 0) {
                        int addr = env_slot_addr(env_idx, (int)arg->ival);
                        W("      (i32.store (i32.const %d) (i32.const 0))\n", addr);
                    }
                }
                /* Call predicate with trail + var slot addresses */
                W("      (local.get $trail)\n");
                for (int ai = 0; ai < n_call_args; ai++) {
                    EXPR_t *arg = pred_call->children[ai];
                    if (!arg) { W("      (i32.const 0)\n"); continue; }
                    if (arg->kind == E_VAR) {
                        int addr = env_slot_addr(env_idx, (int)arg->ival < 0 ? 0 : (int)arg->ival);
                        W("      (i32.const %d) ;; slot addr _V%ld\n", addr, arg->ival);
                    } else if (arg->kind == E_FNC && arg->sval && arg->nchildren==0) {
                        W("      (i32.const %d) ;; atom '%s'\n",
                          atom_intern(arg->sval), arg->sval);
                    } else { W("      (i32.const 0)\n"); }
                }
                W("      (call $%s_call)\n", mangled);
                /* 0 = exhausted: exit loop (br to after loop) */
                W("      (if (i32.eqz) (then) (else\n");
                /* 1 = solution found: emit body goals (children[1..nc-2]) */
                for (int gi = 1; gi < nc - 1; gi++)
                    emit_goal(g->children[gi], env_idx, 0);
                /* Continue loop for next solution */
                W("      (br $retry_%s)\n", mangled);
                W("      )) ;; if\n");
                W("    ) ;; $retry_%s\n", mangled);
                /* After loop exhausted: fail/0 already exits to $disj_end */
                W("    (br $disj_end) ;; all solutions exhausted → fail\n");
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

/* ── emit_pl_main ──────────────────────────────────────────────────────── */
static void emit_pl_main(Program *prog) {
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

/* ── Public entry point ────────────────────────────────────────────────── */
void prolog_emit_wasm(Program *prog, FILE *out, const char *filename) {
    (void)filename;
    wpl_out = out;
    g_clause_env_idx = 0;
    atom_count = 0;

    emit_wasm_set_out(out);
    emit_wasm_strlit_reset();

    prescan_prog(prog);

    W(";; Generated by scrip-cc -pl -wasm (M-PW-A01)\n");
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
    W("\n) ;; end module\n");
}
