/*
 * emit_wasm.c — WebAssembly text-format emitter for scrip-cc
 *
 * Memory layout (matches sno_runtime.wat):
 *   [0..32767]   output buffer  (written by $sno_output_*; runtime page 0 lower)
 *   [32768..65535] string heap  ($sno_str_alloc bump pointer; runtime page 0 upper)
 *   [65536..]    string literal data segment (STR_DATA_BASE; program page 1)
 *
 * Runtime uses 2 pages (128KB). Program (data) segment lives in page 1 (65536+)
 * so it never collides with output buffer or dynamic heap.
 *
 * Value representation on WASM stack:
 *   String  → two i32: (offset, len)
 *   Integer → one i64
 *   Float   → one f64
 *
 * main() calls $sno_output_flush → returns byte-count (i32) to run_wasm.js.
 *
 * Milestones:
 *   M-G2-SCAFFOLD-WASM  scaffold (G-7 2026-03-28)
 *   M-SW-0   -wasm driver flag (SW-1 2026-03-30)
 *   M-SW-1   runtime header inlined (SW-1 2026-03-30)
 *   M-SW-A01 hello/literals: E_QLIT/ILIT/FLIT/NUL/NEG/arith/CONCAT→OUTPUT
 *            (SW-1 2026-03-30)
 */

#include "scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W(fmt, ...)  fprintf(wasm_out, fmt, ##__VA_ARGS__)

static FILE *wasm_out = NULL;

/* ── String literal table ─────────────────────────────────────────────────── */
#define MAX_STRLITS   4096
#define STR_DATA_BASE 65536

typedef struct { char *text; int len; int offset; } StrLit;
static StrLit str_lits[MAX_STRLITS];
static int    str_nlit  = 0;
static int    str_bytes = 0;

static int strlit_intern(const char *s) {
    int len = s ? (int)strlen(s) : 0;
    const char *t = s ? s : "";
    for (int i = 0; i < str_nlit; i++)
        if (str_lits[i].len == len && memcmp(str_lits[i].text, t, len) == 0)
            return i;
    if (str_nlit >= MAX_STRLITS) return 0;
    int idx = str_nlit++;
    str_lits[idx].text   = strdup(t);
    str_lits[idx].len    = len;
    str_lits[idx].offset = str_bytes;
    str_bytes += len ? len : 0;
    return idx;
}
static int strlit_abs(int idx) { return STR_DATA_BASE + str_lits[idx].offset; }

/* ── Variable table ───────────────────────────────────────────────────────── */
/* Each SNOBOL4 variable X → two WASM globals:                               */
/*   (global $var_X_off (mut i32) (i32.const 0))  offset in string heap      */
/*   (global $var_X_len (mut i32) (i32.const 0))  length                     */
/* Variables are always TY_STR (SNOBOL4 untyped; coerce on arithmetic).      */
#define MAX_VARS 512
static char *var_names[MAX_VARS];
static int   nvar = 0;

/* Returns index (0..nvar-1), or -1 if table full. */
static int var_intern(const char *name) {
    if (!name || !*name) return -1;
    /* Case-insensitive: SNOBOL4 variables are case-insensitive */
    for (int i = 0; i < nvar; i++)
        if (strcasecmp(var_names[i], name) == 0) return i;
    if (nvar >= MAX_VARS) return -1;
    var_names[nvar] = strdup(name);
    return nvar++;
}
static void var_table_reset(void) {
    for (int i = 0; i < nvar; i++) { free(var_names[i]); var_names[i] = NULL; }
    nvar = 0;
}

/* ── Expression type ──────────────────────────────────────────────────────── */
typedef enum { TY_STR = 0, TY_INT = 1, TY_FLOAT = 2 } WasmTy;

/* ── Forward decls ────────────────────────────────────────────────────────── */
static WasmTy emit_expr(const EXPR_t *e);

/* ── Runtime imports ─────────────────────────────────────────────────────── */
/* Programs import all runtime functions from the "sno" namespace.           */
/* The runtime module (sno_runtime.wasm) is pre-compiled once per session.   */
static void emit_runtime_imports(void) {
    W("  ;; Memory imported from runtime module\n");
    W("  (import \"sno\" \"memory\" (memory 2))  ;; runtime exports 2 pages; data segment at 65536\n");
    W("  ;; Runtime function imports\n");
    W("  (import \"sno\" \"sno_output_str\"   (func $sno_output_str   (param i32 i32)))\n");
    W("  (import \"sno\" \"sno_output_int\"   (func $sno_output_int   (param i64)))\n");
    W("  (import \"sno\" \"sno_output_flush\" (func $sno_output_flush (result i32)))\n");
    W("  (import \"sno\" \"sno_str_alloc\"    (func $sno_str_alloc    (param i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_str_concat\"   (func $sno_str_concat   (param i32 i32 i32 i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_str_eq\"       (func $sno_str_eq       (param i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_str_to_int\"   (func $sno_str_to_int   (param i32 i32) (result i64)))\n");
    W("  (import \"sno\" \"sno_int_to_str\"   (func $sno_int_to_str   (param i64) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_float_to_str\" (func $sno_float_to_str (param f64) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_pow\"          (func $sno_pow          (param f64 f64) (result f64)))\n");
    W("  ;; String heap pointer: programs use sno_str_alloc from runtime\n");
    W("  ;; (global $str_ptr is internal to runtime; programs use sno_str_alloc)\n");
}

/* reserved names that are not user variables */
static int is_keyword_name(const char *n) {
    if (!n) return 0;
    return (strcasecmp(n,"OUTPUT")==0 || strcasecmp(n,"INPUT")==0 ||
            strcasecmp(n,"PUNCH")==0  || strcasecmp(n,"TERMINAL")==0);
}

/* ── expr pre-scan (intern all string literals; collect E_VAR names) ─────── */
static void prescan_expr(const EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_QLIT) { strlit_intern(e->sval); return; }
    if (e->kind == E_VAR && e->sval && !is_keyword_name(e->sval))
        var_intern(e->sval);
    for (int i = 0; i < e->nchildren; i++) prescan_expr(e->children[i]);
}
static void prescan_prog(Program *prog) {
    strlit_intern("");  /* always intern empty string */
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* Collect lvalue variable name */
        if (s->has_eq && s->subject &&
            s->subject->kind == E_VAR && s->subject->sval &&
            !is_keyword_name(s->subject->sval))
            var_intern(s->subject->sval);
        prescan_expr(s->subject);
        prescan_expr(s->pattern);
        prescan_expr(s->replacement);
    }
}

/* ── Data segment ─────────────────────────────────────────────────────────── */
static void emit_data_segment(void) {
    if (str_bytes == 0) return;
    W("\n  ;; String literals at offset %d\n", STR_DATA_BASE);
    W("  (data (i32.const %d) \"", STR_DATA_BASE);
    for (int i = 0; i < str_nlit; i++) {
        const unsigned char *t = (const unsigned char *)str_lits[i].text;
        for (int j = 0; j < str_lits[i].len; j++) {
            unsigned char c = t[j];
            if (c == '"' || c == '\\') W("\\%02x", c);
            else if (c < 32 || c > 126) W("\\%02x", c);
            else W("%c", (char)c);
        }
    }
    W("\")\n");
}

/* ── Variable globals ─────────────────────────────────────────────────────── */
static void emit_var_globals(void) {
    if (nvar == 0) return;
    W("\n  ;; SNOBOL4 variables: each → (offset, length) global pair\n");
    for (int i = 0; i < nvar; i++) {
        W("  (global $var_%s_off (mut i32) (i32.const 0))\n", var_names[i]);
        W("  (global $var_%s_len (mut i32) (i32.const 0))\n", var_names[i]);
    }
}


static WasmTy emit_expr(const EXPR_t *e) {
    if (!e || e->kind == E_NUL) {
        int idx = strlit_intern("");
        W("    (i32.const %d)\n", strlit_abs(idx));
        W("    (i32.const 0)\n");
        return TY_STR;
    }
    switch (e->kind) {
    case E_QLIT: {
        int idx = strlit_intern(e->sval);
        W("    (i32.const %d)\n", strlit_abs(idx));
        W("    (i32.const %d)\n", str_lits[idx].len);
        return TY_STR;
    }
    case E_ILIT:
        W("    (i64.const %lld)\n", (long long)e->ival);
        return TY_INT;
    case E_FLIT:
        W("    (f64.const %g)\n", e->dval);
        return TY_FLOAT;
    case E_NEG: {
        /* For literals: always wraps a numeric child */
        WasmTy t = emit_expr(e->nchildren > 0 ? e->children[0] : NULL);
        if (t == TY_INT) {
            W("    (i64.const -1)\n");
            W("    (i64.mul)\n");
        } else if (t == TY_FLOAT) {
            W("    (f64.neg)\n");
        }
        return t;
    }
    case E_PLS:
        return emit_expr(e->nchildren > 0 ? e->children[0] : NULL);
    case E_ADD: case E_SUB: case E_MPY: case E_DIV: case E_MOD: case E_POW: {
        WasmTy lt = emit_expr(e->children[0]);
        if (lt == TY_STR) { W("    (call $sno_str_to_int)\n"); lt = TY_INT; }
        WasmTy rt = emit_expr(e->children[1]);
        if (rt == TY_STR) { W("    (call $sno_str_to_int)\n"); rt = TY_INT; }
        /* Exponentiation: int**int → integer (loop); any float → float via $sno_pow */
        if (e->kind == E_POW) {
            if (lt == TY_INT && rt == TY_INT) {
                /* Integer exponentiation: result = lt ^ rt using loop */
                /* Stack: [i64_base, i64_exp] — use $tmp_f to hold base temporarily */
                /* Emit inline: result=1; while(exp-->0) result*=base */
                /* Use a helper approach: sno_pow already does integer loop internally
                 * but returns f64. Instead, emit inline i64 exponent loop.
                 * We need two locals but only have $tmp_f (f64). Use runtime sno_pow
                 * then truncate to i64 (exact for integer inputs). */
                W("    (f64.convert_i64_s)\n");   /* rt: i64→f64 (top of stack) */
                W("    (local.set $tmp_f)\n");     /* save rt_f */
                W("    (f64.convert_i64_s)\n");   /* lt: i64→f64 */
                W("    (local.get $tmp_f)\n");     /* restore rt_f */
                W("    (call $sno_pow)\n");
                W("    (i64.trunc_f64_s)\n");      /* back to i64 (exact for small ints) */
                return TY_INT;
            } else if (lt == TY_INT && rt == TY_FLOAT) {
                W("    (local.set $tmp_f)\n");
                W("    (f64.convert_i64_s)\n");
                W("    (local.get $tmp_f)\n");
            } else if (lt == TY_FLOAT && rt == TY_INT) {
                W("    (f64.convert_i64_s)\n");
            }
            W("    (call $sno_pow)\n");
            return TY_FLOAT;
        }
        int floaty = (lt == TY_FLOAT || rt == TY_FLOAT);
        if (!floaty) {
            if      (e->kind == E_ADD) W("    (i64.add)\n");
            else if (e->kind == E_SUB) W("    (i64.sub)\n");
            else if (e->kind == E_MPY) W("    (i64.mul)\n");
            else if (e->kind == E_DIV) W("    (i64.div_s)\n");
            else if (e->kind == E_MOD) W("    (i64.rem_s)\n");
            return TY_INT;
        }
        /* Mixed: int-lhs + float-rhs needs local temp to swap */
        if (lt == TY_INT && rt == TY_FLOAT) {
            W("    (local.set $tmp_f)\n");
            W("    (f64.convert_i64_s)\n");
            W("    (local.get $tmp_f)\n");
        } else if (rt == TY_INT) {
            W("    (f64.convert_i64_s)\n");
        } else if (lt == TY_INT) {
            W("    (f64.convert_i64_s)\n");
        }
        if      (e->kind == E_ADD) W("    (f64.add)\n");
        else if (e->kind == E_SUB) W("    (f64.sub)\n");
        else if (e->kind == E_MPY) W("    (f64.mul)\n");
        else if (e->kind == E_DIV) W("    (f64.div)\n");
        return TY_FLOAT;
    }
    case E_VAR: {
        const char *vn = e->sval ? e->sval : "";
        int idx = var_intern(vn);   /* idempotent — already collected in prescan */
        if (idx < 0) {
            /* fallback: empty string */
            int si = strlit_intern("");
            W("    (i32.const %d) ;; E_VAR '%s' unknown\n", strlit_abs(si), vn);
            W("    (i32.const 0)\n");
        } else {
            W("    (global.get $var_%s_off)\n", var_names[idx]);
            W("    (global.get $var_%s_len)\n", var_names[idx]);
        }
        return TY_STR;
    }
    case E_CONCAT: {
        if (e->nchildren == 0) {
            int idx = strlit_intern("");
            W("    (i32.const %d)\n", strlit_abs(idx));
            W("    (i32.const 0)\n");
            return TY_STR;
        }
        WasmTy t0 = emit_expr(e->children[0]);
        if (t0 == TY_INT)   W("    (call $sno_int_to_str)\n");
        if (t0 == TY_FLOAT) W("    (call $sno_float_to_str)\n");
        for (int i = 1; i < e->nchildren; i++) {
            WasmTy ti = emit_expr(e->children[i]);
            if (ti == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ti == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_str_concat)\n");
        }
        return TY_STR;
    }
    case E_FNC: {
        const char *fn = e->sval ? e->sval : "";
        /* REMDR(a,b) → i64.rem_s */
        if (strcasecmp(fn, "remdr") == 0 && e->nchildren >= 2) {
            WasmTy ta = emit_expr(e->children[0]);
            if (ta == TY_STR) W("    (call $sno_str_to_int)\n");
            if (ta == TY_FLOAT) { W("    (i64.trunc_f64_s)\n"); }
            WasmTy tb = emit_expr(e->children[1]);
            if (tb == TY_STR) W("    (call $sno_str_to_int)\n");
            if (tb == TY_FLOAT) { W("    (i64.trunc_f64_s)\n"); }
            W("    (i64.rem_s)\n");
            return TY_INT;
        }
        /* Default: evaluate args, drop results, return empty string */
        for (int i = 0; i < e->nchildren; i++) {
            WasmTy t = emit_expr(e->children[i]);
            if (t == TY_STR) { W("    (drop)\n    (drop)\n"); }
            else              { W("    (drop)\n"); }
        }
        W("    ;; unimplemented builtin '%s' in value ctx — empty string\n", fn);
        { int idx = strlit_intern("");
          W("    (i32.const %d)\n", strlit_abs(idx));
          W("    (i32.const 0)\n"); }
        return TY_STR;
    }
    default:
        W("    ;; UNHANDLED EKind %d\n", (int)e->kind);
        { int idx = strlit_intern("");
          W("    (i32.const %d)\n", strlit_abs(idx));
          W("    (i32.const 0)\n"); }
        return TY_STR;
    }
}

/* ── Label table ──────────────────────────────────────────────────────────── */
#define MAX_LABELS 1024
static char *lbl_names[MAX_LABELS];
static int   nlabels = 0;

static int lbl_index(const char *name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < nlabels; i++)
        if (strcasecmp(lbl_names[i], name) == 0) return i;
    if (nlabels < MAX_LABELS) { lbl_names[nlabels] = strdup(name); return nlabels++; }
    return -1;
}

/* index 0 is always the "end/done" sentinel */
static void collect_labels(Program *prog) {
    for (int i = 0; i < nlabels; i++) free(lbl_names[i]);
    nlabels = 0;
    lbl_names[nlabels++] = strdup("__end__");          /* index 0 */
    /* Pass 1: statement labels in position order — defines block layout */
    for (STMT_t *s = prog->head; s; s = s->next)
        if (s->label && *s->label) lbl_index(s->label);
    /* Pass 2: goto targets — may reference labels not yet defined as stmts */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->go) continue;
        if (s->go->onsuccess) lbl_index(s->go->onsuccess);
        if (s->go->onfailure) lbl_index(s->go->onfailure);
        if (s->go->uncond)    lbl_index(s->go->uncond);
    }
}

/* ── Emit a goto to a named label ────────────────────────────────────────── */
/* "end" → index 0 (falls out of loop), otherwise → index N */
static void emit_goto_target(const char *target) {
    if (!target || !*target) return;
    int idx = (strcasecmp(target, "end") == 0) ? 0 : lbl_index(target);
    if (idx < 0) idx = 0;
    W("      (local.set $pc (i32.const %d)) ;; → %s\n", idx, target);
    W("      (br $dispatch)\n");
}

/* ── Emit DIFFER/IDENT as i32 success flag ───────────────────────────────── */
static void emit_differ(const EXPR_t *e) {
    int is_differ = (strcasecmp(e->sval, "differ") == 0);
    if (e->nchildren < 2) { W("      (i32.const %d)\n", is_differ ? 1 : 0); return; }
    WasmTy t0 = emit_expr(e->children[0]);
    if (t0 == TY_INT)   W("      (call $sno_int_to_str)\n");
    if (t0 == TY_FLOAT) W("      (call $sno_float_to_str)\n");
    WasmTy t1 = emit_expr(e->children[1]);
    if (t1 == TY_INT)   W("      (call $sno_int_to_str)\n");
    if (t1 == TY_FLOAT) W("      (call $sno_float_to_str)\n");
    W("      (call $sno_str_eq)\n");           /* 1 if equal */
    if (is_differ) W("      (i32.eqz)\n");    /* differ = not-equal */
}

/* ── Emit any subject expression as i32 success flag ─────────────────────── */
/* Returns 1 (success) or 0 (failure) on the stack.                          */
static void emit_subject_as_bool(const EXPR_t *e) {
    if (!e || e->kind == E_NUL) { W("      (i32.const 1)\n"); return; }
    if (e->kind == E_FNC) {
        const char *fn = e->sval ? e->sval : "";
        if (strcasecmp(fn, "differ") == 0 || strcasecmp(fn, "ident") == 0) {
            emit_differ(e); return;
        }
        /* Other builtins: evaluate args for side effects, succeed */
        for (int i = 0; i < e->nchildren; i++) {
            WasmTy t = emit_expr(e->children[i]);
            if (t == TY_STR) { W("      (drop)\n      (drop)\n"); }
            else              { W("      (drop)\n"); }
        }
        W("      (i32.const 1) ;; builtin '%s' — treated as succeed\n", fn);
        return;
    }
    /* Any other expression: evaluate it; non-empty/non-zero = success */
    WasmTy t = emit_expr(e);
    if (t == TY_STR) {
        /* success if len > 0 */
        W("      (drop) ;; drop offset, keep len\n");
        W("      (i32.const 0)\n");
        W("      (i32.ne)\n");
    } else if (t == TY_INT) {
        W("      (i64.const 0)\n");
        W("      (i64.ne)\n");
        W("      (i32.wrap_i64)\n");
    } else {
        /* float: non-zero = success */
        W("      (f64.const 0)\n");
        W("      (f64.ne)\n");
    }
}

/* ── Emit OUTPUT = expr ───────────────────────────────────────────────────── */
static void emit_output(const EXPR_t *val) {
    WasmTy ty = emit_expr(val);
    if (ty == TY_STR)        { W("      (call $sno_output_str)\n"); }
    else if (ty == TY_INT)   { W("      (call $sno_output_int)\n"); }
    else /* TY_FLOAT */      { W("      (call $sno_float_to_str)\n"); W("      (call $sno_output_str)\n"); }
}

/* ── Main body: dispatch-loop state machine ───────────────────────────────── */
/*
 * Pattern: $pc sentinel = nlabels (out of br_table range → sequential fall-through).
 *   index 0      → br $exit (end program)
 *   index 1..N-1 → br $LN  (jump to label)
 *   default      → fall through (sentinel, sequential execution)
 * Label blocks $L1..$LN wrap statement ranges. Breaking out of $LN skips
 * past those statements to the code following $LN's close (the label body).
 */
static void emit_main_body(Program *prog) {
    W("    (local $pc i32)\n");
    W("    (local $ok i32)\n");
    W("    (local $tmp_f f64)\n");

    int closed[MAX_LABELS] = {0};
    closed[0] = 1;

    /* Dispatch-loop pattern:
     *   $pc=0       -> br $exit  (terminate program)
     *   $pc=1..N-1  -> br $LN   (jump to label N; skips stmts in $LN's body)
     *   $pc=N (sentinel, default) -> br $br_nop (exits only $br_nop; sequential)
     *
     * Layout: $br_nop is innermost, nested INSIDE $L1 (innermost label block).
     * "br $br_nop" exits only $br_nop — sequential execution continues inside $L1.
     * "br $L1" exits $br_nop AND $L1 — skips stmts before L1's position.
     * "br $exit" exits everything — terminates.
     *
     *   (block $exit
     *     (loop $dispatch
     *       (block $L{N-1}
     *         ...
     *         (block $L1          <- innermost label block
     *           (block $br_nop    <- default target; exits immediately
     *             (br_table $exit $L1 ... $L{N-1} $br_nop (local.get $pc))
     *           )                 <- falls through here, still inside $L1
     *           <- stmts before L1 label (skipped by br $L1)
     *         ) ;; $L1 closes     <- L1 entry; stmts at/after L1 label
     *         <- stmts before L2
     *       ) ;; $L2 closes       <- L2 entry...
     */
    W("    (local.set $pc (i32.const %d)) ;; sentinel: sequential start\n", nlabels);

    W("    (block $exit\n");
    W("    (loop $dispatch\n");

    /* Open label blocks outermost-first ($L{N-1} outermost, $L1 innermost) */
    for (int i = nlabels - 1; i >= 1; i--)
        W("      (block $L%d\n", i);

    /* $br_nop: innermost block, default target — exits only itself */
    W("      (block $br_nop\n");
    W("        (br_table $exit");               /* index 0 -> $exit */
    for (int i = 1; i < nlabels; i++) W(" $L%d", i);  /* index i -> $Li */
    W(" $br_nop (local.get $pc))\n");          /* default (sentinel) -> $br_nop */
    W("      ) ;; $br_nop -- sequential falls through here\n");

    /* Reset $pc to sentinel so next dispatch is sequential again */
    W("      (local.set $pc (i32.const %d))\n", nlabels);

    /* Emit statements */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->label && *s->label) {
            int idx = lbl_index(s->label);
            if (idx > 0 && !closed[idx]) {
                W("      ) ;; $L%d %s\n", idx, s->label);
                closed[idx] = 1;
            }
        }

        if (s->is_end) {
            W("      ;; END\n");
            emit_goto_target("end");
            continue;
        }

        int has_subject = s->subject && s->subject->kind != E_NUL;
        int is_output = 0;
        int is_varassign = 0;
        if (s->has_eq && s->subject) {
            const char *n = s->subject->sval ? s->subject->sval : "";
            if ((s->subject->kind == E_VAR || s->subject->kind == E_KW)) {
                if (strcasecmp(n, "OUTPUT") == 0)
                    is_output = 1;
                else if (!is_keyword_name(n))
                    is_varassign = 1;
            }
        }

        if (is_output) {
            W("      ;; OUTPUT = ...\n");
            emit_output(s->replacement);
            W("      (local.set $ok (i32.const 1))\n");
        } else if (is_varassign) {
            const char *vn = s->subject->sval;
            int vi = var_intern(vn);
            W("      ;; %s = ...\n", vn);
            WasmTy ty = emit_expr(s->replacement);
            /* Coerce to TY_STR — variables are always stored as strings */
            if (ty == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (ty == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            /* Stack: (off len) — store len first (it's on top) */
            W("      (global.set $var_%s_len)\n", var_names[vi]);
            W("      (global.set $var_%s_off)\n", var_names[vi]);
            W("      (local.set $ok (i32.const 1))\n");
        } else if (has_subject) {
            W("      ;; subject eval\n");
            emit_subject_as_bool(s->subject);
            W("      (local.set $ok)\n");
        } else {
            W("      (local.set $ok (i32.const 1)) ;; no subject\n");
        }

        if (s->go) {
            if (s->go->uncond && *s->go->uncond) {
                emit_goto_target(s->go->uncond);
            } else {
                int has_s = s->go->onsuccess && *s->go->onsuccess;
                int has_f = s->go->onfailure && *s->go->onfailure;
                if (has_s && has_f) {
                    W("      (if (local.get $ok) (then\n");
                    W("        (local.set $pc (i32.const %d))\n", lbl_index(s->go->onsuccess));
                    W("        (br $dispatch)\n");
                    W("      ) (else\n");
                    W("        (local.set $pc (i32.const %d))\n", lbl_index(s->go->onfailure));
                    W("        (br $dispatch)\n");
                    W("      ))\n");
                } else if (has_f) {
                    W("      (if (i32.eqz (local.get $ok)) (then\n");
                    emit_goto_target(s->go->onfailure);
                    W("      ))\n");
                } else if (has_s) {
                    W("      (if (local.get $ok) (then\n");
                    emit_goto_target(s->go->onsuccess);
                    W("      ))\n");
                }
            }
        }
    }

    for (int i = nlabels - 1; i >= 1; i--)
        if (!closed[i]) W("      ) ;; close $L%d (unreached)\n", i);

    /* Fall off end = terminate */
    W("      (local.set $pc (i32.const 0))\n");
    W("      (br $dispatch) ;; fall-off end -> $exit\n");
    W("    ) ;; loop $dispatch\n");
    W("    ) ;; block $exit\n");
}

/* ── Shared string table API (used by emit_wasm_prolog.c, emit_wasm_icon.c) ── */
void emit_wasm_set_out(FILE *f)              { wasm_out = f; }
int  emit_wasm_strlit_intern(const char *s)  { return strlit_intern(s); }
int  emit_wasm_strlit_abs(int idx)           { return strlit_abs(idx); }
int  emit_wasm_strlit_len(int idx)           { return str_lits[idx].len; }
void emit_wasm_data_segment(void)            { emit_data_segment(); }
void emit_wasm_strlit_reset(void) {
    for (int i = 0; i < str_nlit; i++) { free(str_lits[i].text); str_lits[i].text = NULL; }
    str_nlit = str_bytes = 0;
}

/* ── Public entry point ───────────────────────────────────────────────────── */
void emit_wasm(Program *prog, FILE *out, const char *filename) {
    (void)filename;
    wasm_out = out;

    for (int i = 0; i < str_nlit; i++) free(str_lits[i].text);
    str_nlit = str_bytes = 0;
    var_table_reset();

    collect_labels(prog);
    prescan_prog(prog);

    W(";; Generated by scrip-cc -wasm (M-SW-A03)\n");
    W("(module\n");

    emit_runtime_imports();
    emit_data_segment();
    emit_var_globals();

    W("\n  (func (export \"main\") (result i32)\n");
    emit_main_body(prog);
    W("    (call $sno_output_flush)\n");
    W("  )\n");
    W(")\n");
}
