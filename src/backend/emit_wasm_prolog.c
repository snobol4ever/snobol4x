/*
 * emit_wasm_prolog.c — Prolog IR → WebAssembly text-format emitter
 *
 * Consumes E_CHOICE/E_CLAUSE/E_UNIFY/E_CUT/E_TRAIL_MARK/E_TRAIL_UNWIND
 * nodes produced by prolog_lower() and emits a WebAssembly Text (.wat) file.
 *
 * Entry point: prolog_emit_wasm(Program *prog, FILE *out, const char *filename)
 * Called from driver/main.c when -pl -wasm flags are both set.
 *
 * Design:
 *   - Shares emit_wasm.c for all common IR nodes (E_QLIT/ILIT/FLIT/ADD/etc.)
 *     via emit_wasm_expr(). Call emit_wasm_expr() for any arithmetic/literal
 *     subexpression inside a Prolog goal.
 *   - Each Prolog-specific EKind gets a case here ONLY — never in emit_wasm.c.
 *   - Byrd-box four-port model: α/β/γ/ω encoded as tail-call WAT functions.
 *     WASM has no arbitrary goto; return_call is zero-overhead tail dispatch.
 *   - Runtime imports from "pl" namespace (pl_runtime.wat), not "sno".
 *
 * Port encoding (mirrors emit_x64_prolog.c and emit_jvm_prolog.c):
 *   α  — try:     initial entry, attempt first clause head unification
 *   β  — retry:   backtrack, unwind trail, attempt next clause
 *   γ  — succeed: head unified + body executed, signal caller
 *   ω  — fail:    all clauses exhausted, propagate failure up
 *
 * Milestones:
 *   M-PW-SCAFFOLD  stub scaffold, -pl -wasm wired (PW-1 2026-03-30)
 */

#include "scrip_cc.h"
#include "../frontend/prolog/prolog_atom.h"
#include "../frontend/prolog/prolog_parse.h"
#include "../frontend/prolog/prolog_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Output state
 * ------------------------------------------------------------------------- */

static FILE *wpl_out = NULL;

static void W(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(wpl_out, fmt, ap);
    va_end(ap);
}

static void WI(const char *instr, const char *ops) {
    if (ops && ops[0])
        W("    %s %s\n", instr, ops);
    else
        W("    %s\n", instr);
}

/* -------------------------------------------------------------------------
 * Name mangling — safe WAT identifiers from functor/arity
 * Mirrors the JVM emitter's pj_mangle() convention.
 * -------------------------------------------------------------------------
 * foo/2  →  pl_foo_2
 * 'Is'/2 →  pl_Is_2
 * Result always starts with pl_ to avoid WAT keyword collision.
 * ------------------------------------------------------------------------- */
static char mangle_buf[512];
static const char *pl_mangle(const char *functor, int arity) {
    int di = 0;
    mangle_buf[di++] = 'p'; mangle_buf[di++] = 'l'; mangle_buf[di++] = '_';
    for (const char *s = functor; *s && di < 480; s++) {
        char c = *s;
        if (isalnum((unsigned char)c) || c == '_')
            mangle_buf[di++] = c;
        else {
            /* encode non-alphanum as _XX hex */
            mangle_buf[di++] = '_';
            mangle_buf[di++] = "0123456789abcdef"[(unsigned char)c >> 4];
            mangle_buf[di++] = "0123456789abcdef"[(unsigned char)c & 0xf];
        }
    }
    mangle_buf[di++] = '_';
    int a = arity < 0 ? 0 : arity;
    if (a >= 10) { mangle_buf[di++] = '0' + a / 10; }
    mangle_buf[di++] = '0' + a % 10;
    mangle_buf[di] = '\0';
    return mangle_buf;
}

/* -------------------------------------------------------------------------
 * Runtime imports (pl namespace, pl_runtime.wat)
 * Emitted once at top of every .wat module.
 * ------------------------------------------------------------------------- */
static void emit_pl_runtime_imports(void) {
    W(";; --- Prolog WASM runtime imports (pl_runtime.wat) ---\n");
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

/* -------------------------------------------------------------------------
 * Stub body for unimplemented goals — emits a WAT comment + unreachable.
 * Replaced milestone by milestone as each EKind is implemented.
 * ------------------------------------------------------------------------- */
static void emit_goal_stub(const EXPR_t *g) {
    const char *kind_name = "unknown";
    if (g) {
        switch (g->kind) {
            case E_CHOICE:      kind_name = "E_CHOICE";      break;
            case E_CLAUSE:      kind_name = "E_CLAUSE";      break;
            case E_UNIFY:       kind_name = "E_UNIFY";       break;
            case E_CUT:         kind_name = "E_CUT";         break;
            case E_TRAIL_MARK:  kind_name = "E_TRAIL_MARK";  break;
            case E_TRAIL_UNWIND:kind_name = "E_TRAIL_UNWIND";break;
            default:            kind_name = "other";         break;
        }
    }
    W("    ;; STUB: %s not yet implemented (PW milestone pending)\n", kind_name);
    W("    unreachable\n");
}

/* -------------------------------------------------------------------------
 * write/1 builtin — emit atom arg to output buffer
 * M-PW-HELLO: first real goal emission
 * ------------------------------------------------------------------------- */
static void emit_write_atom(const EXPR_t *arg) {
    if (!arg) return;
    if (arg->kind == E_QLIT && arg->sval) {
        /* inline atom: data offset + length already in string table via
         * emit_wasm_expr() shared path — for now emit as direct i32 constant.
         * Full integration deferred to M-PW-HELLO implementation. */
        W("    ;; write('%s') — stub until M-PW-HELLO\n", arg->sval);
        W("    unreachable\n");
        return;
    }
    emit_goal_stub(arg);
}

/* -------------------------------------------------------------------------
 * Goal emission dispatch — handles Prolog-specific goals.
 * Common goals (write, nl, is arithmetic) delegate to helpers below.
 * All Prolog-specific EKinds are handled here — never in emit_wasm.c.
 * ------------------------------------------------------------------------- */
static void emit_pl_goal(const EXPR_t *goal) {
    if (!goal) return;

    /* E_CUT ---------------------------------------------------------------- */
    if (goal->kind == E_CUT) {
        emit_goal_stub(goal);   /* M-PW-B03 */
        return;
    }

    /* E_UNIFY —  =/2 -------------------------------------------------------- */
    if (goal->kind == E_UNIFY) {
        emit_goal_stub(goal);   /* M-PW-A01 */
        return;
    }

    /* E_TRAIL_MARK / E_TRAIL_UNWIND ---------------------------------------- */
    if (goal->kind == E_TRAIL_MARK || goal->kind == E_TRAIL_UNWIND) {
        emit_goal_stub(goal);   /* M-PW-A01 */
        return;
    }

    /* E_CHOICE / E_CLAUSE -------------------------------------------------- */
    if (goal->kind == E_CHOICE) {
        emit_goal_stub(goal);   /* M-PW-A01 */
        return;
    }
    if (goal->kind == E_CLAUSE) {
        emit_goal_stub(goal);   /* M-PW-A01 */
        return;
    }

    /* E_FNC — builtin goals ------------------------------------------------ */
    if (goal->kind == E_FNC && goal->sval) {
        const char *fn = goal->sval;

        if (strcasecmp(fn, "nl") == 0) {
            W("    call $pl_output_nl\n");
            return;
        }
        if (strcasecmp(fn, "write") == 0 || strcasecmp(fn, "writeln") == 0) {
            if (goal->nchildren >= 1) emit_write_atom(goal->children[0]);
            if (strcasecmp(fn, "writeln") == 0) W("    call $pl_output_nl\n");
            return;
        }
        if (strcasecmp(fn, "halt") == 0) {
            W("    ;; halt/0 — flush output and return\n");
            W("    call $pl_output_flush\n");
            W("    return\n");
            return;
        }
        if (strcasecmp(fn, "true") == 0) {
            W("    ;; true/0 — no-op\n");
            return;
        }
        if (strcasecmp(fn, "fail") == 0) {
            W("    ;; fail/0 — stub (M-PW-B01)\n");
            W("    unreachable\n");
            return;
        }

        /* Unimplemented builtin */
        W("    ;; STUB builtin: %s/%d (PW milestone pending)\n",
          fn, goal->nchildren);
        W("    unreachable\n");
        return;
    }

    /* Fallback */
    emit_goal_stub(goal);
}

/* -------------------------------------------------------------------------
 * Program-level main function emission (scaffold)
 * One flat (func $main) — clause dispatch added milestone by milestone.
 * Full α/β/γ/ω per-predicate functions added from M-PW-A01 onward.
 * ------------------------------------------------------------------------- */
static void emit_pl_main(Program *prog) {
    W("  (func (export \"main\") (result i32)\n");
    W("    ;; Prolog program body — scaffold (M-PW-SCAFFOLD)\n");
    W("    ;; Goal dispatch will be wired here from M-PW-HELLO onward.\n");
    W("    call $pl_output_flush\n");
    W("  )\n");
    (void)prog;
}

/* -------------------------------------------------------------------------
 * Public entry point
 * Called from driver/main.c when -pl and -wasm are both set.
 * ------------------------------------------------------------------------- */
void prolog_emit_wasm(Program *prog, FILE *out, const char *filename) {
    (void)filename;
    wpl_out = out;

    W(";; Generated by scrip-cc -pl -wasm (M-PW-SCAFFOLD)\n");
    W("(module\n");
    W("\n");
    emit_pl_runtime_imports();
    emit_pl_main(prog);
    W("\n");
    W(") ;; end module\n");
}
