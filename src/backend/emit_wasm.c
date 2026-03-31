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
    W("  (import \"sno\" \"sno_size\"         (func $sno_size         (param i32 i32) (result i64)))\n");
    W("  (import \"sno\" \"sno_dupl\"         (func $sno_dupl         (param i32 i32 i64) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_replace\"      (func $sno_replace      (param i32 i32 i32 i32 i32 i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_str_to_float\" (func $sno_str_to_float (param i32 i32) (result f64)))\n");
    W("  (import \"sno\" \"sno_lgt\"          (func $sno_lgt          (param i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_str_contains\" (func $sno_str_contains (param i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_pat_search\"   (func $sno_pat_search   (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_any\"          (func $sno_any          (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_notany\"       (func $sno_notany       (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_span\"         (func $sno_span         (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_break\"        (func $sno_break        (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_breakx\"       (func $sno_breakx       (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  ;; String heap pointer: programs use sno_str_alloc from runtime\n");
    W("  ;; (global $str_ptr is internal to runtime; programs use sno_str_alloc)\n");
}

static int needs_indr = 0;  /* set during prescan if E_INDR found */

/* reserved names that are not user variables */
static int is_keyword_name(const char *n) {
    if (!n) return 0;
    return (strcasecmp(n,"OUTPUT")==0 || strcasecmp(n,"INPUT")==0 ||
            strcasecmp(n,"PUNCH")==0  || strcasecmp(n,"TERMINAL")==0);
}

/* ── expr pre-scan (intern all string literals; collect E_VAR names) ─────── */
/* Pre-intern &ALPHABET (256-byte binary string) by name. */
static void prescan_intern_alphabet(void) {
    /* Check if already present (len == 256) */
    for (int i = 0; i < str_nlit; i++)
        if (str_lits[i].len == 256) return;
    int idx = str_nlit++;
    str_lits[idx].text   = (char *)malloc(256);
    for (int i = 0; i < 256; i++) str_lits[idx].text[i] = (char)i;
    str_lits[idx].len    = 256;
    str_lits[idx].offset = str_bytes;
    str_bytes += 256;
}

static void prescan_expr(const EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_QLIT) { strlit_intern(e->sval); return; }
    if (e->kind == E_KW && e->sval) {
        const char *kw = e->sval;
        if      (strcasecmp(kw, "alphabet") == 0) prescan_intern_alphabet();
        else if (strcasecmp(kw, "ucase")    == 0) strlit_intern("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        else if (strcasecmp(kw, "lcase")    == 0) strlit_intern("abcdefghijklmnopqrstuvwxyz");
        else if (strcasecmp(kw, "digits")   == 0) strlit_intern("0123456789");
        /* &NULL, &ANCHOR, etc. → empty string (already interned in prescan_prog) */
        return;
    }
    if (e->kind == E_VAR && e->sval && !is_keyword_name(e->sval))
        var_intern(e->sval);
    if (e->kind == E_INDR) {
        needs_indr = 1;
        /* $.var: child is E_CAPT_COND; intern the variable name */
        const EXPR_t *ch = e->nchildren > 0 ? e->children[0] : NULL;
        if (ch && ch->kind == E_CAPT_COND && ch->sval)
            var_intern(ch->sval);
        else if (ch && ch->kind == E_CAPT_COND && ch->nchildren > 0
                 && ch->children[0] && ch->children[0]->sval)
            var_intern(ch->children[0]->sval);
    }
    /* CAPT_COND/CAPT_IMM: sval = target varname */
    if ((e->kind == E_CAPT_COND || e->kind == E_CAPT_IMM) && e->sval
            && !is_keyword_name(e->sval))
        var_intern(e->sval);
    /* CAPT_CUR (@var): varname in children[0]->sval */
    if (e->kind == E_CAPT_CUR && e->nchildren > 0
            && e->children[0] && e->children[0]->sval
            && !is_keyword_name(e->children[0]->sval))
        var_intern(e->children[0]->sval);
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

/* ── Indirect variable access functions ───────────────────────────────────── */
/* $sno_var_get(name_off, name_len) → (val_off, val_len)                      */
/*   Looks up variable by name string; returns ("", 0) for unknown.          */
/* $sno_var_set(name_off, name_len, val_off, val_len)                         */
/*   Assigns to variable by name; no-op for unknown.                         */
/* Emitted only when E_INDR nodes are present. Uses if-else chain over       */
/* all interned var names, each compared with $sno_str_eq.                   */
static void emit_var_indirect_funcs(void) {
    if (!needs_indr || nvar == 0) return;
    /* sno_var_get */
    W("\n  ;; $sno_var_get — indirect variable lookup by name\n");
    W("  (func $sno_var_get (param $no i32) (param $nl i32) (result i32 i32)\n");
    for (int i = 0; i < nvar; i++) {
        int si = strlit_intern(var_names[i]);
        W("    (if (call $sno_str_eq (local.get $no) (local.get $nl)"
          " (i32.const %d) (i32.const %d)) (then\n",
          strlit_abs(si), str_lits[si].len);
        W("      (return (global.get $var_%s_off) (global.get $var_%s_len))\n",
          var_names[i], var_names[i]);
        W("    ))\n");
    }
    /* unknown name → empty string */
    W("    (i32.const %d) (i32.const 0)\n", strlit_abs(strlit_intern("")));
    W("  )\n");

    /* sno_var_set */
    W("\n  ;; $sno_var_set — indirect variable assignment by name\n");
    W("  (func $sno_var_set"
      " (param $no i32) (param $nl i32) (param $vo i32) (param $vl i32)\n");
    for (int i = 0; i < nvar; i++) {
        int si = strlit_intern(var_names[i]);
        W("    (if (call $sno_str_eq (local.get $no) (local.get $nl)"
          " (i32.const %d) (i32.const %d)) (then\n",
          strlit_abs(si), str_lits[si].len);
        W("      (global.set $var_%s_off (local.get $vo))\n", var_names[i]);
        W("      (global.set $var_%s_len (local.get $vl))\n", var_names[i]);
        W("      (return)\n");
        W("    ))\n");
    }
    W("  )\n");
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
    case E_INDR: {
        /* $expr — indirect: look up variable by name.
         * SNOBOL4 semantics:
         *   $'lit'  → sno_var_get("lit")     — name is the literal string
         *   $.var   → sno_var_get("var")     — name is the IDENTIFIER, not its value
         *   $expr   → sno_var_get(eval(expr)) — general: evaluate to get name string
         * For E_VAR child, push the variable NAME as a string literal, not its value.
         * For E_QLIT child, push the literal.
         * For other children, evaluate and coerce. */
        const EXPR_t *name_e = e->nchildren > 0 ? e->children[0] : NULL;
        if (!name_e) {
            int si = strlit_intern("");
            W("    (i32.const %d)\n", strlit_abs(si));
            W("    (i32.const 0)\n");
        } else if (name_e->kind == E_VAR && name_e->sval) {
            /* $var — name is the identifier string itself */
            int si = strlit_intern(name_e->sval);
            W("    (i32.const %d) ;; $var name=%s\n", strlit_abs(si), name_e->sval);
            W("    (i32.const %d)\n", str_lits[si].len);
        } else if (name_e->kind == E_CAPT_COND && name_e->sval) {
            /* $.var — parser wraps as E_INDR(E_CAPT_COND(var)); use identifier name */
            int si = strlit_intern(name_e->sval);
            W("    (i32.const %d) ;; $.var name=%s\n", strlit_abs(si), name_e->sval);
            W("    (i32.const %d)\n", str_lits[si].len);
        } else if (name_e->kind == E_CAPT_COND && name_e->nchildren > 0
                   && name_e->children[0] && name_e->children[0]->sval) {
            /* $.var alternative: sval on child E_VAR */
            const char *vn = name_e->children[0]->sval;
            int si = strlit_intern(vn);
            W("    (i32.const %d) ;; $.var(child) name=%s\n", strlit_abs(si), vn);
            W("    (i32.const %d)\n", str_lits[si].len);
        } else {
            /* $'lit' or $expr — evaluate child to get name string */
            WasmTy nt = emit_expr(name_e);
            if (nt == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (nt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
        }
        W("    (call $sno_var_get)\n");
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
    case E_KW: {
        /* &KEYWORD — emit as pre-interned string constant */
        const char *kw = e->sval ? e->sval : "";
        const char *val = "";
        if      (strcasecmp(kw, "alphabet") == 0) {
            /* &ALPHABET: all 256 chars 0..255 */
            /* intern a 256-byte string with chars 0..255 */
            char alpha[256];
            for (int i = 0; i < 256; i++) alpha[i] = (char)i;
            /* Use raw strlit_intern — but it uses strlen; need raw version */
            /* Find or insert manually */
            int found = -1;
            for (int i = 0; i < str_nlit; i++)
                if (str_lits[i].len == 256) { found = i; break; }
            if (found < 0) {
                found = str_nlit++;
                str_lits[found].text   = (char *)malloc(256);
                memcpy(str_lits[found].text, alpha, 256);
                str_lits[found].len    = 256;
                str_lits[found].offset = str_bytes;
                str_bytes += 256;
            }
            W("    (i32.const %d) ;; &ALPHABET\n", strlit_abs(found));
            W("    (i32.const 256)\n");
            return TY_STR;
        } else if (strcasecmp(kw, "ucase") == 0) {
            val = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        } else if (strcasecmp(kw, "lcase") == 0) {
            val = "abcdefghijklmnopqrstuvwxyz";
        } else if (strcasecmp(kw, "digits") == 0) {
            val = "0123456789";
        } else if (strcasecmp(kw, "null") == 0) {
            val = "";
        }
        /* anchor, etc. fall through to empty string */
        int ki = strlit_intern(val);
        W("    (i32.const %d) ;; &%s\n", strlit_abs(ki), kw);
        W("    (i32.const %d)\n", str_lits[ki].len);
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
        /* SIZE(s) → i64 character count */
        if (strcasecmp(fn, "size") == 0 && e->nchildren >= 1) {
            WasmTy ta = emit_expr(e->children[0]);
            if (ta == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ta == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_size)\n");
            return TY_INT;
        }
        /* DUPL(s, n) → replicated string */
        if (strcasecmp(fn, "dupl") == 0 && e->nchildren >= 2) {
            WasmTy ta = emit_expr(e->children[0]);
            if (ta == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ta == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy tb = emit_expr(e->children[1]);
            if (tb == TY_STR) W("    (call $sno_str_to_int)\n");
            if (tb == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
            W("    (call $sno_dupl)\n");
            return TY_STR;
        }
        /* REPLACE(s, from, to) → translated string */
        if (strcasecmp(fn, "replace") == 0 && e->nchildren >= 3) {
            WasmTy ts = emit_expr(e->children[0]);
            if (ts == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ts == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy tf = emit_expr(e->children[1]);
            if (tf == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (tf == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy tt = emit_expr(e->children[2]);
            if (tt == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (tt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_replace)\n");
            return TY_STR;
        }
        /* CONVERT(val, type_str) → coerced value */
        if (strcasecmp(fn, "convert") == 0 && e->nchildren >= 2) {
            /* Evaluate type arg as string to get the target type name */
            /* We evaluate val first; type is a compile-time string literal in corpus */
            /* Strategy: emit val, then dispatch on type literal value */
            const EXPR_t *type_e = e->children[1];
            const char *tname = (type_e && type_e->kind == E_QLIT && type_e->sval)
                                 ? type_e->sval : "";
            WasmTy tv = emit_expr(e->children[0]);
            if (strcasecmp(tname, "integer") == 0) {
                if (tv == TY_STR)   W("    (call $sno_str_to_int)\n");
                if (tv == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
                return TY_INT;
            } else if (strcasecmp(tname, "real") == 0) {
                if (tv == TY_STR)   W("    (call $sno_str_to_float)\n");
                if (tv == TY_INT)   W("    (f64.convert_i64_s)\n");
                return TY_FLOAT;
            } else {
                /* "string" or anything else → stringify */
                if (tv == TY_INT)   W("    (call $sno_int_to_str)\n");
                if (tv == TY_FLOAT) W("    (call $sno_float_to_str)\n");
                return TY_STR;
            }
        }
        /* DATATYPE(val) → string type name: "string", "integer", "real" */
        if (strcasecmp(fn, "datatype") == 0 && e->nchildren >= 1) {
            WasmTy tv = emit_expr(e->children[0]);
            /* Drop the value — we only need its compile-time type */
            if (tv == TY_STR)   { W("    (drop)\n    (drop)\n"); }
            else if (tv == TY_INT)   { W("    (drop)\n"); }
            else                     { W("    (drop)\n"); }
            const char *tname = (tv == TY_INT) ? "integer"
                               : (tv == TY_FLOAT) ? "real" : "string";
            int ti = strlit_intern(tname);
            W("    (i32.const %d) ;; datatype=%s\n", strlit_abs(ti), tname);
            W("    (i32.const %d)\n", str_lits[ti].len);
            return TY_STR;
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
    if (e->nchildren == 1) {
        /* DIFFER(x): succeeds if x is non-null; IDENT(x): succeeds if x is null */
        WasmTy t = emit_expr(e->children[0]);
        if (t == TY_INT) {
            W("      (i64.const 0)\n");
            W("      (i64.ne)\n");
            W("      (i32.wrap_i64)\n");
        } else if (t == TY_FLOAT) {
            W("      (f64.const 0)\n");
            W("      (f64.ne)\n");
        } else {
            /* TY_STR: stack is (off, len) with len on top.
             * Save len, drop off, restore len, then test len != 0. */
            W("      (local.set $tmp_i32) ;; save len (top)\n");
            W("      (drop)               ;; drop off\n");
            W("      (local.get $tmp_i32) ;; restore len\n");
            W("      (i32.const 0)\n");
            W("      (i32.ne) ;; len != 0 → non-null\n");
        }
        if (!is_differ) W("      (i32.eqz) ;; IDENT: invert\n");
        return;
    }
    if (e->nchildren < 1) { W("      (i32.const %d)\n", is_differ ? 1 : 0); return; }
    WasmTy t0 = emit_expr(e->children[0]);
    if (t0 == TY_INT)   W("      (call $sno_int_to_str)\n");
    if (t0 == TY_FLOAT) W("      (call $sno_float_to_str)\n");
    WasmTy t1 = emit_expr(e->children[1]);
    if (t1 == TY_INT)   W("      (call $sno_int_to_str)\n");
    if (t1 == TY_FLOAT) W("      (call $sno_float_to_str)\n");
    W("      (call $sno_str_eq)\n");           /* 1 if equal */
    if (is_differ) W("      (i32.eqz)\n");    /* differ = not-equal */
}

/* ── Cursor-based pattern node emitter ───────────────────────────────────── */
/* Emits WAT that searches for `pat` inside the subject string whose offset   */
/* and length are in locals $pat_subj_off / $pat_subj_len, starting from the  */
/* current value of local $pat_cursor.  On success $pat_cursor is updated to  */
/* the position past the match.  On failure $pat_cursor is set to -1.         */
/* Handles: E_QLIT (literal), E_SEQ (left then right, cursor threaded).       */
static void emit_pattern_node(const EXPR_t *pat) {
    if (!pat || pat->kind == E_NUL) {
        /* empty pattern — always succeeds, cursor unchanged */
        return;
    }
    if (pat->kind == E_QLIT) {
        /* literal string search from current cursor */
        int idx = strlit_intern(pat->sval ? pat->sval : "");
        int off  = strlit_abs(idx);
        int len  = str_lits[idx].len;
        W("      ;; E_QLIT pattern node: sno_pat_search\n");
        W("      (local.get $pat_subj_off) (local.get $pat_subj_len)\n");
        W("      (i32.const %d) (i32.const %d)\n", off, len);
        W("      (local.get $pat_cursor)\n");
        W("      (call $sno_pat_search)\n");
        W("      (local.set $pat_cursor)\n");
        return;
    }
    if (pat->kind == E_SEQ) {
        /* sequential: emit left then right, threading cursor */
        W("      ;; E_SEQ pattern node: left then right\n");
        if (pat->nchildren >= 1) emit_pattern_node(pat->children[0]);
        /* only attempt right if left succeeded (cursor >= 0) */
        W("      (if (i32.ge_s (local.get $pat_cursor) (i32.const 0)) (then\n");
        if (pat->nchildren >= 2) emit_pattern_node(pat->children[1]);
        W("      ))\n");
        return;
    }
    /* E_ARBNO may come as E_ARBNO or as E_FNC with sval=="ARBNO" in pattern context */
    if (pat->kind == E_ARBNO ||
        (pat->kind == E_FNC && pat->sval && strcasecmp(pat->sval, "ARBNO") == 0)) {
        const EXPR_t *inner = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        W("      ;; E_ARBNO: zero-or-more with zero-advance guard\n");
        W("      (block $arbno_done\n");
        W("      (loop $arbno_loop\n");
        W("        (local.set $pat_save_cursor (local.get $pat_cursor))\n");
        if (inner) emit_pattern_node(inner);
        W("        (br_if $arbno_done (i32.lt_s (local.get $pat_cursor) (i32.const 0)))\n");
        W("        (br_if $arbno_done (i32.eq (local.get $pat_cursor) (local.get $pat_save_cursor)))\n");
        W("        (br $arbno_loop)\n");
        W("      ))\n");
        /* restore last good cursor if inner failed */
        W("      (if (i32.lt_s (local.get $pat_cursor) (i32.const 0)) (then\n");
        W("        (local.set $pat_cursor (local.get $pat_save_cursor))\n");
        W("      ))\n");
        return;
    }
    if (pat->kind == E_ALT) {
        /* alternation: try left; if cursor==-1 restore and try right */
        W("      ;; E_ALT: save cursor, try left, restore+try right on fail\n");
        W("      (local.set $pat_save_cursor (local.get $pat_cursor))\n");
        if (pat->nchildren >= 1) emit_pattern_node(pat->children[0]);
        W("      (if (i32.lt_s (local.get $pat_cursor) (i32.const 0)) (then\n");
        W("        (local.set $pat_cursor (local.get $pat_save_cursor))\n");
        if (pat->nchildren >= 2) emit_pattern_node(pat->children[1]);
        W("      ))\n");
        return;
    }
    /* E_CAPT_COND (.var) — conditional capture: extract subj[before..cursor] on success */
    if (pat->kind == E_CAPT_COND) {
        const char *varname = pat->sval ? pat->sval : "";
        const EXPR_t *child = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        W("      ;; E_CAPT_COND .%s: save cursor, run child, capture on success\n", varname);
        W("      (local.set $pat_before (local.get $pat_cursor))\n");
        if (child) emit_pattern_node(child);
        /* only assign if child succeeded (cursor >= 0) */
        if (*varname && !is_keyword_name(varname)) {
            W("      (if (i32.ge_s (local.get $pat_cursor) (i32.const 0)) (then\n");
            W("        (global.set $var_%s_off\n", varname);
            W("          (i32.add (local.get $pat_subj_off) (local.get $pat_before)))\n");
            W("        (global.set $var_%s_len\n", varname);
            W("          (i32.sub (local.get $pat_cursor) (local.get $pat_before)))\n");
            W("      ))\n");
        }
        return;
    }
    /* E_CAPT_IMM ($var) — immediate capture: same substring extraction as CAPT_COND */
    if (pat->kind == E_CAPT_IMM) {
        const char *varname = pat->sval ? pat->sval : "";
        const EXPR_t *child = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        W("      ;; E_CAPT_IMM $%s: save cursor, run child, capture matched span\n", varname);
        W("      (local.set $pat_before (local.get $pat_cursor))\n");
        if (child) emit_pattern_node(child);
        if (*varname && !is_keyword_name(varname)) {
            W("      (if (i32.ge_s (local.get $pat_cursor) (i32.const 0)) (then\n");
            W("        (global.set $var_%s_off\n", varname);
            W("          (i32.add (local.get $pat_subj_off) (local.get $pat_before)))\n");
            W("        (global.set $var_%s_len\n", varname);
            W("          (i32.sub (local.get $pat_cursor) (local.get $pat_before)))\n");
            W("      ))\n");
        }
        return;
    }
    /* E_CAPT_CUR (@var) — zero-width cursor capture: store cursor position as integer string */
    if (pat->kind == E_CAPT_CUR) {
        const EXPR_t *vnode = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        const char *varname = (vnode && vnode->sval) ? vnode->sval
                            : (pat->sval ? pat->sval : "");
        W("      ;; E_CAPT_CUR @%s: store cursor position as int string into var\n", varname);
        if (*varname && !is_keyword_name(varname)) {
            /* convert cursor i32 → i64, call sno_int_to_str → (off, len), store */
            W("      (i64.extend_i32_u (local.get $pat_cursor))\n");
            W("      (call $sno_int_to_str)\n");
            W("      (global.set $var_%s_len)\n", varname);
            W("      (global.set $var_%s_off)\n", varname);
        }
        /* zero-width: cursor unchanged; always succeeds */
        return;
    }
    /* E_FNC cursor-assertion / cursor-advance patterns: POS RPOS LEN TAB RTAB REM */
    if (pat->kind == E_FNC && pat->sval) {
        const char *fn = pat->sval;
        /* REM — no arg: cursor = subj_len */
        if (strcasecmp(fn, "REM") == 0) {
            W("      ;; REM: cursor = subj_len\n");
            W("      (local.set $pat_cursor (local.get $pat_subj_len))\n");
            return;
        }
        /* One-arg cursor ops: POS RPOS LEN TAB RTAB */
        if (pat->nchildren == 1 &&
            (strcasecmp(fn,"POS")==0  || strcasecmp(fn,"RPOS")==0 ||
             strcasecmp(fn,"LEN")==0  || strcasecmp(fn,"TAB")==0  ||
             strcasecmp(fn,"RTAB")==0)) {
            /* Evaluate arg → i32 n (arg is typically E_ILIT → TY_INT i64) */
            W("      ;; %s: evaluate arg → $pat_n\n", fn);
            WasmTy at = emit_expr(pat->children[0]);
            if (at == TY_INT)   W("      (i32.wrap_i64)\n");
            else if (at == TY_FLOAT) W("      (i32.trunc_f64_s)\n");
            else /* STR */      W("      (call $sno_str_to_int) (i32.wrap_i64)\n");
            W("      (local.set $pat_n)\n");
            if (strcasecmp(fn, "POS") == 0) {
                /* fail unless cursor == n */
                W("      (if (i32.ne (local.get $pat_cursor) (local.get $pat_n)) (then\n");
                W("        (local.set $pat_cursor (i32.const -1))\n");
                W("      ))\n");
            } else if (strcasecmp(fn, "RPOS") == 0) {
                /* fail unless cursor == subj_len - n */
                W("      (if (i32.ne (local.get $pat_cursor)\n");
                W("              (i32.sub (local.get $pat_subj_len) (local.get $pat_n))) (then\n");
                W("        (local.set $pat_cursor (i32.const -1))\n");
                W("      ))\n");
            } else if (strcasecmp(fn, "LEN") == 0) {
                /* advance by n if cursor+n <= subj_len */
                W("      (if (i32.gt_s\n");
                W("            (i32.add (local.get $pat_cursor) (local.get $pat_n))\n");
                W("            (local.get $pat_subj_len)) (then\n");
                W("        (local.set $pat_cursor (i32.const -1))\n");
                W("      ) (else\n");
                W("        (local.set $pat_cursor\n");
                W("          (i32.add (local.get $pat_cursor) (local.get $pat_n)))\n");
                W("      ))\n");
            } else if (strcasecmp(fn, "TAB") == 0) {
                /* set cursor=n if cursor<=n and n<=subj_len, else fail */
                W("      (if (i32.or\n");
                W("            (i32.gt_s (local.get $pat_cursor) (local.get $pat_n))\n");
                W("            (i32.gt_s (local.get $pat_n) (local.get $pat_subj_len))) (then\n");
                W("        (local.set $pat_cursor (i32.const -1))\n");
                W("      ) (else\n");
                W("        (local.set $pat_cursor (local.get $pat_n))\n");
                W("      ))\n");
            } else { /* RTAB */
                /* tgt = subj_len - n; set cursor=tgt if cursor<=tgt, else fail */
                W("      (local.set $pat_n\n");
                W("        (i32.sub (local.get $pat_subj_len) (local.get $pat_n)))\n");
                W("      (if (i32.gt_s (local.get $pat_cursor) (local.get $pat_n)) (then\n");
                W("        (local.set $pat_cursor (i32.const -1))\n");
                W("      ) (else\n");
                W("        (local.set $pat_cursor (local.get $pat_n))\n");
                W("      ))\n");
            }
            return;
        }
    }
    /* E_FNC with sval ANY / NOTANY / SPAN / BREAK / BREAKX — character-class patterns */
    if (pat->kind == E_FNC && pat->sval && pat->nchildren == 1) {
        const char *fn = pat->sval;
        const char *runtime_fn = NULL;
        if      (strcasecmp(fn, "ANY")    == 0) runtime_fn = "sno_any";
        else if (strcasecmp(fn, "NOTANY") == 0) runtime_fn = "sno_notany";
        else if (strcasecmp(fn, "SPAN")   == 0) runtime_fn = "sno_span";
        else if (strcasecmp(fn, "BREAK")  == 0) runtime_fn = "sno_break";
        else if (strcasecmp(fn, "BREAKX") == 0) runtime_fn = "sno_breakx";
        if (runtime_fn) {
            const EXPR_t *arg = pat->children[0];
            int idx = strlit_intern((arg->kind == E_QLIT && arg->sval) ? arg->sval : "");
            int set_off = strlit_abs(idx);
            int set_len = str_lits[idx].len;
            W("      ;; %s: char-class match\\n", fn);
            W("      (local.get $pat_subj_off) (local.get $pat_subj_len)\\n");
            W("      (i32.const %d) (i32.const %d)\\n", set_off, set_len);
            W("      (local.get $pat_cursor)\\n");
            W("      (call $%s)\\n", runtime_fn);
            W("      (local.set $pat_cursor)\\n");
            return;
        }
    }
    /* fallback: evaluate as string expression, use sno_pat_search */
    WasmTy tp = emit_expr(pat);
    if (tp == TY_INT)   W("      (call $sno_int_to_str)\n");
    if (tp == TY_FLOAT) W("      (call $sno_float_to_str)\n");
    /* stack: (ndl_off ndl_len) — save to locals, then call */
    W("      ;; fallback pattern via sno_pat_search\n");
    W("      (local.set $pat_ndl_len) (local.set $pat_ndl_off)\n");
    W("      (local.get $pat_subj_off) (local.get $pat_subj_len)\n");
    W("      (local.get $pat_ndl_off)  (local.get $pat_ndl_len)\n");
    W("      (local.get $pat_cursor)\n");
    W("      (call $sno_pat_search)\n");
    W("      (local.set $pat_cursor)\n");
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
        /* Numeric comparison predicates: LT LE EQ NE GT GE */
        /* Each takes two args, coerces to numbers, returns 0/1 */
        int is_cmp = (strcasecmp(fn,"lt")==0 || strcasecmp(fn,"le")==0 ||
                      strcasecmp(fn,"eq")==0 || strcasecmp(fn,"ne")==0 ||
                      strcasecmp(fn,"gt")==0 || strcasecmp(fn,"ge")==0);
        if (is_cmp && e->nchildren >= 2) {
            /* Evaluate both args; coerce to numbers for comparison */
            WasmTy ta = emit_expr(e->children[0]);
            WasmTy tb = emit_expr(e->children[1]);
            /* If either is float, promote both to float comparison */
            int use_float = (ta == TY_FLOAT || tb == TY_FLOAT);
            if (!use_float) {
                /* Both int (or str coerced to int) */
                if (ta == TY_STR) {
                    /* Need to reorder: currently stack is (a_off a_len b...) */
                    /* Simpler: always coerce strings via str_to_int before push */
                    /* Actually stack is already: a_val b_val after emit */
                    /* ta was STR so stack has (a_off a_len), then b was emitted */
                    /* We need: coerce a first, then b. But they're already on stack. */
                    /* Use locals to save/restore */
                }
                /* Use i64 comparison — both must be i64 on stack */
                /* Re-evaluate cleanly using locals */
                W("      ;; %s predicate\n", fn);
                W("      (local.set $cmp_b_i\n");
                if (tb == TY_STR)   W("        (call $sno_str_to_int)\n");
                else if (tb == TY_FLOAT) W("        (i64.trunc_f64_s)\n");
                W("      )\n");
                W("      (local.set $cmp_a_i\n");
                if (ta == TY_STR)   W("        (call $sno_str_to_int)\n");
                else if (ta == TY_FLOAT) W("        (i64.trunc_f64_s)\n");
                W("      )\n");
                W("      (local.get $cmp_a_i)\n");
                W("      (local.get $cmp_b_i)\n");
                if      (strcasecmp(fn,"lt")==0) W("      (i64.lt_s)\n");
                else if (strcasecmp(fn,"le")==0) W("      (i64.le_s)\n");
                else if (strcasecmp(fn,"eq")==0) W("      (i64.eq)\n");
                else if (strcasecmp(fn,"ne")==0) W("      (i64.ne)\n");
                else if (strcasecmp(fn,"gt")==0) W("      (i64.gt_s)\n");
                else                             W("      (i64.ge_s)\n");
            } else {
                /* Float path */
                W("      (local.set $cmp_b_f\n");
                if (tb == TY_STR)   W("        (call $sno_str_to_float)\n");
                else if (tb == TY_INT)   W("        (f64.convert_i64_s)\n");
                W("      )\n");
                W("      (local.set $cmp_a_f\n");
                if (ta == TY_STR)   W("        (call $sno_str_to_float)\n");
                else if (ta == TY_INT)   W("        (f64.convert_i64_s)\n");
                W("      )\n");
                W("      (local.get $cmp_a_f)\n");
                W("      (local.get $cmp_b_f)\n");
                if      (strcasecmp(fn,"lt")==0) W("      (f64.lt)\n");
                else if (strcasecmp(fn,"le")==0) W("      (f64.le)\n");
                else if (strcasecmp(fn,"eq")==0) W("      (f64.eq)\n");
                else if (strcasecmp(fn,"ne")==0) W("      (f64.ne)\n");
                else if (strcasecmp(fn,"gt")==0) W("      (f64.gt)\n");
                else                             W("      (f64.ge)\n");
            }
            return;
        }
        /* NE with mixed string/int: SNOBOL4 semantics — type mismatch always differs */
        /* Already handled above via numeric coercion — "12" coerces to 12 for ne(12,12)=fail */

        /* LGT: lexicographic greater-than */
        if (strcasecmp(fn, "lgt") == 0 && e->nchildren >= 2) {
            WasmTy ta = emit_expr(e->children[0]);
            if (ta == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (ta == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            WasmTy tb = emit_expr(e->children[1]);
            if (tb == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (tb == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            W("      (call $sno_lgt)\n");
            return;
        }
        /* INTEGER predicate: succeed if value is/coerces-to integer */
        if (strcasecmp(fn, "integer") == 0 && e->nchildren >= 1) {
            WasmTy ta = emit_expr(e->children[0]);
            if (ta == TY_INT) {
                W("      (drop) ;; integer literal — always succeed\n");
                W("      (i32.const 1)\n");
            } else if (ta == TY_FLOAT) {
                W("      (drop) ;; float — not integer\n");
                W("      (i32.const 0)\n");
            } else {
                /* String: try parsing as integer — succeed if all chars are digits (with optional sign) */
                /* Emit: str_to_int, convert back to str, compare with original — or simpler:
                   check that str_to_int gives non-zero OR original == "0" */
                /* Simplest correct approach: call sno_str_to_int, then sno_int_to_str,
                   compare result string with original stripped string */
                /* Even simpler: succeed if every char (after optional sign) is 0-9 */
                /* Use a runtime helper — but we don't have one. Inline WAT scan: */
                W("      ;; INTEGER predicate: scan string for all-digit\n");
                W("      (local.set $int_pred_len)\n");
                W("      (local.set $int_pred_off)\n");
                W("      (local.set $int_pred_i (i32.const 0))\n");
                W("      (local.set $int_pred_ok (i32.const 1))\n");
                /* skip optional sign */
                W("      (if (i32.gt_u (local.get $int_pred_len) (i32.const 0))\n");
                W("        (then\n");
                W("          (local.set $int_pred_c (i32.load8_u (local.get $int_pred_off)))\n");
                W("          (if (i32.or (i32.eq (local.get $int_pred_c) (i32.const 43))\n");
                W("                      (i32.eq (local.get $int_pred_c) (i32.const 45)))\n");
                W("            (then (local.set $int_pred_i (i32.const 1))))))\n");
                /* empty or sign-only → fail */
                W("      (if (i32.ge_u (local.get $int_pred_i) (local.get $int_pred_len))\n");
                W("        (then (local.set $int_pred_ok (i32.const 0))))\n");
                /* scan remaining chars */
                W("      (block $ipb (loop $ipl\n");
                W("        (br_if $ipb (i32.ge_u (local.get $int_pred_i) (local.get $int_pred_len)))\n");
                W("        (local.set $int_pred_c (i32.load8_u\n");
                W("          (i32.add (local.get $int_pred_off) (local.get $int_pred_i))))\n");
                W("        (if (i32.or\n");
                W("              (i32.lt_u (local.get $int_pred_c) (i32.const 48))\n");
                W("              (i32.gt_u (local.get $int_pred_c) (i32.const 57)))\n");
                W("          (then (local.set $int_pred_ok (i32.const 0)) (br $ipb)))\n");
                W("        (local.set $int_pred_i (i32.add (local.get $int_pred_i) (i32.const 1)))\n");
                W("        (br $ipl)))\n");
                W("      (local.get $int_pred_ok)\n");
            }
            return;
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
    W("    (local $tmp_i32 i32)\n");
    /* Comparison predicate locals */
    W("    (local $cmp_a_i i64)\n");
    W("    (local $cmp_b_i i64)\n");
    W("    (local $cmp_a_f f64)\n");
    W("    (local $cmp_b_f f64)\n");
    /* INTEGER predicate scan locals */
    W("    (local $int_pred_off i32)\n");
    W("    (local $int_pred_len i32)\n");
    W("    (local $int_pred_i i32)\n");
    W("    (local $int_pred_ok i32)\n");
    W("    (local $int_pred_c i32)\n");
    if (needs_indr) {
        W("    (local $indr_no i32)\n");
        W("    (local $indr_nl i32)\n");
        W("    (local $indr_vo i32)\n");
        W("    (local $indr_vl i32)\n");
    }
    /* Pattern-match cursor locals (used by emit_pattern_node) */
    W("    (local $pat_subj_off i32)\n");
    W("    (local $pat_subj_len i32)\n");
    W("    (local $pat_cursor i32)\n");
    W("    (local $pat_ndl_off i32)\n");
    W("    (local $pat_ndl_len i32)\n");
    W("    (local $pat_save_cursor i32)\n");
    W("    (local $pat_n i32)\n");
    W("    (local $pat_before i32)\n");

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
        int is_indrassign = 0;
        if (s->has_eq && s->subject) {
            const char *n = s->subject->sval ? s->subject->sval : "";
            if ((s->subject->kind == E_VAR || s->subject->kind == E_KW)) {
                if (strcasecmp(n, "OUTPUT") == 0)
                    is_output = 1;
                else if (!is_keyword_name(n))
                    is_varassign = 1;
            } else if (s->subject->kind == E_INDR) {
                is_indrassign = 1;
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
        } else if (is_indrassign) {
            /* $name_expr = replacement — indirect assignment */
            W("      ;; $... = (indirect assign)\n");
            /* Evaluate name expression — same semantics as E_INDR rvalue */
            const EXPR_t *name_e = s->subject->nchildren > 0
                                   ? s->subject->children[0] : NULL;
            if (!name_e) {
                int si = strlit_intern("");
                W("      (i32.const %d)\n", strlit_abs(si));
                W("      (i32.const 0)\n");
            } else if (name_e->kind == E_VAR && name_e->sval) {
                int si = strlit_intern(name_e->sval);
                W("      (i32.const %d) ;; $var lval name=%s\n", strlit_abs(si), name_e->sval);
                W("      (i32.const %d)\n", str_lits[si].len);
            } else if (name_e->kind == E_CAPT_COND && name_e->sval) {
                int si = strlit_intern(name_e->sval);
                W("      (i32.const %d) ;; $.var lval name=%s\n", strlit_abs(si), name_e->sval);
                W("      (i32.const %d)\n", str_lits[si].len);
            } else if (name_e->kind == E_CAPT_COND && name_e->nchildren > 0
                       && name_e->children[0] && name_e->children[0]->sval) {
                const char *vn = name_e->children[0]->sval;
                int si = strlit_intern(vn);
                W("      (i32.const %d) ;; $.var(child) lval name=%s\n", strlit_abs(si), vn);
                W("      (i32.const %d)\n", str_lits[si].len);
            } else {
                WasmTy nt = emit_expr(name_e);
                if (nt == TY_INT)   W("      (call $sno_int_to_str)\n");
                if (nt == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            }
            /* Stack: (name_off name_len) — save in locals */
            W("      (local.set $indr_nl)\n");
            W("      (local.set $indr_no)\n");
            /* Evaluate replacement value */
            WasmTy vt = emit_expr(s->replacement);
            if (vt == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (vt == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            /* Stack: (val_off val_len) */
            W("      (local.set $indr_vl)\n");
            W("      (local.set $indr_vo)\n");
            /* Call sno_var_set(name_off, name_len, val_off, val_len) */
            W("      (local.get $indr_no) (local.get $indr_nl)\n");
            W("      (local.get $indr_vo) (local.get $indr_vl)\n");
            W("      (call $sno_var_set)\n");
            W("      (local.set $ok (i32.const 1))\n");
        } else if (has_subject && s->pattern && s->pattern->kind != E_NUL) {
            /* Pattern-match statement: subject pat  :s/:f
               Use cursor-based emit_pattern_node() so SEQ can chain. */
            W("      ;; pattern match: subject ? pattern (cursor-based)\n");
            /* Evaluate subject — coerce to string, save in pat_subj locals */
            WasmTy ts = emit_expr(s->subject);
            if (ts == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (ts == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            W("      (local.set $pat_subj_len)\n");
            W("      (local.set $pat_subj_off)\n");
            /* Initialise cursor to 0 (start of subject) */
            W("      (local.set $pat_cursor (i32.const 0))\n");
            /* Emit pattern tree — updates $pat_cursor, -1 on fail */
            emit_pattern_node(s->pattern);
            /* $ok = (cursor >= 0) */
            W("      (local.set $ok (i32.ge_s (local.get $pat_cursor) (i32.const 0)))\n");
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
int  emit_wasm_strlit_count(void)            { return str_nlit; }
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
    needs_indr = 0;

    collect_labels(prog);
    prescan_prog(prog);

    W(";; Generated by scrip-cc -wasm (M-SW-A03)\n");
    W("(module\n");

    emit_runtime_imports();
    emit_data_segment();
    emit_var_globals();
    emit_var_indirect_funcs();

    W("\n  (func (export \"main\") (result i32)\n");
    emit_main_body(prog);
    W("    (call $sno_output_flush)\n");
    W("  )\n");
    W(")\n");
}
