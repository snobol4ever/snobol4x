/* LEGACY — sub-box text emitter; kept for reference; superseded by SM-based emit (see MILESTONE-SCRIP-X86-COMPLETION.md) */
/*
 * emit_wasm.c — WebAssembly text-format emitter for scrip-cc
 *
 * Memory layout (matches snobol4_runtime.wat):
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
WasmTy emit_wasm_expr(const EXPR_t *e);

/* ── Runtime imports ─────────────────────────────────────────────────────── */
/* Programs import all runtime functions from the "sno" namespace.           */
/* The runtime module (sno_runtime.wasm) is pre-compiled once per session.   */
static void emit_runtime_imports(void) {
    emit_wasm_runtime_imports_sno_base(wasm_out, 2,
        "runtime exports 2 pages; data segment at 65536");
    /* SNOBOL4-specific imports (pattern engine, builtins, string utilities) */
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
    /* Array / Table / DATA imports (M-SW-C02) */
    W("  (import \"sno\" \"sno_arr_alloc\"        (func $sno_arr_alloc        (param i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_array_create\"     (func $sno_array_create     (param i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_array_create2\"    (func $sno_array_create2    (param i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_array_get\"        (func $sno_array_get        (param i32 i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_array_get2\"       (func $sno_array_get2       (param i32 i32 i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_array_set\"        (func $sno_array_set        (param i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_array_set2\"       (func $sno_array_set2       (param i32 i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_array_prototype\"  (func $sno_array_prototype  (param i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_table_create\"     (func $sno_table_create     (param i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_table_get\"        (func $sno_table_get        (param i32 i32 i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_table_set\"        (func $sno_table_set        (param i32 i32 i32 i32 i32)))\n");
    W("  (import \"sno\" \"sno_table_count\"      (func $sno_table_count      (param i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_table_get_bucket\" (func $sno_table_get_bucket (param i32 i32) (result i32 i32 i32 i32)))\n");
    W("  (import \"sno\" \"sno_table_cap\"        (func $sno_table_cap        (param i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_data_define\"      (func $sno_data_define      (param i32 i32 i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_data_new\"         (func $sno_data_new         (param i32 i32) (result i32)))\n");
    W("  (import \"sno\" \"sno_data_get_field\"   (func $sno_data_get_field   (param i32 i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_data_set_field\"   (func $sno_data_set_field   (param i32 i32 i32 i32)))\n");
    W("  (import \"sno\" \"sno_data_typename\"    (func $sno_data_typename    (param i32) (result i32 i32)))\n");
    W("  (import \"sno\" \"sno_handle_type\"      (func $sno_handle_type      (param i32) (result i32)))\n");
    W("  ;; String heap pointer: programs use sno_str_alloc from runtime\n");
    W("  ;; (global $str_ptr is internal to runtime; programs use sno_str_alloc)\n");
}

static int needs_indr = 0;  /* set during prescan if E_INDIRECT found */

/* ── DATA type registry (Part A — M-SW-C02) ──────────────────────────────── */
/* Populated during prescan when E_FNC "data" is seen.                        */
/* Used by emitter to recognise constructor calls and field get/set.          */
#define MAX_DATA_TYPES  64
#define MAX_DATA_FIELDS 32

typedef struct {
    char  *name;              /* type name, e.g. "NODE" */
    int    name_lit;          /* strlit index for type name */
    int    nfields;
    char  *field_name[MAX_DATA_FIELDS];
    int    field_lit[MAX_DATA_FIELDS];   /* strlit index per field */
    int    field_names_lit;  /* strlit index of concatenated field-name pairs data */
} DataType;

static DataType data_types[MAX_DATA_TYPES];
static int      data_ntype = 0;

/* Register a DATA type from a "data(typename,f1,f2,...)" call in prescan. */
static void parse_data_spec(const EXPR_t *e) {
    /* Two calling conventions:
     *   (a) data('typename(f1,f2,...)')  -- single quoted spec string
     *   (b) data('typename','f1','f2')   -- separate args (less common)
     * Detect (a) by '(' in children[0]->sval. */
    if (e->nchildren < 1) return;
    const EXPR_t *c0 = e->children[0];
    if (!c0 || c0->kind != E_QLIT || !c0->sval) return;

    const char *spec = c0->sval;
    const char *paren = strchr(spec, '(');
    char tname[256]; tname[0] = '\0';
    char fbuf[512];  fbuf[0] = '\0';

    if (paren) {
        /* single-string: "typename(f1,f2,...)" */
        size_t tl = (size_t)(paren - spec);
        if (tl >= sizeof(tname)) tl = sizeof(tname)-1;
        memcpy(tname, spec, tl); tname[tl] = '\0';
        const char *cl = strchr(paren, ')');
        size_t fl = cl ? (size_t)(cl-paren-1) : strlen(paren+1);
        if (fl >= sizeof(fbuf)) fl = sizeof(fbuf)-1;
        memcpy(fbuf, paren+1, fl); fbuf[fl] = '\0';
    } else {
        strncpy(tname, spec, sizeof(tname)-1); tname[sizeof(tname)-1] = '\0';
    }

    for (int i = 0; i < data_ntype; i++)
        if (strcasecmp(data_types[i].name, tname) == 0) return;
    if (data_ntype >= MAX_DATA_TYPES) return;
    DataType *dt = &data_types[data_ntype++];
    dt->name     = strdup(tname);
    dt->name_lit = strlit_intern(tname);
    dt->nfields  = 0;

    if (paren) {
        char *buf = strdup(fbuf);
        char *tok = strtok(buf, ",");
        while (tok && dt->nfields < MAX_DATA_FIELDS) {
            while (*tok==' '||*tok=='\t') tok++;
            char *end = tok+strlen(tok)-1;
            while (end>tok&&(*end==' '||*end=='\t')) *end--='\0';
            if (*tok) {
                dt->field_name[dt->nfields] = strdup(tok);
                dt->field_lit[dt->nfields]  = strlit_intern(tok);
                dt->nfields++;
            }
            tok = strtok(NULL, ",");
        }
        free(buf);
    } else {
        for (int i = 1; i < e->nchildren && dt->nfields < MAX_DATA_FIELDS; i++) {
            const EXPR_t *fn = e->children[i];
            if (!fn || !fn->sval) continue;
            dt->field_name[dt->nfields] = strdup(fn->sval);
            dt->field_lit[dt->nfields]  = strlit_intern(fn->sval);
            dt->nfields++;
        }
    }
}

/* Lookup helpers used by emitter dispatch. */
static int data_type_by_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < data_ntype; i++)
        if (strcasecmp(data_types[i].name, name) == 0) return i;
    return -1;
}

/* Returns type index of the type that owns field 'name', or -1. */
static int data_field_owner(const char *name, int *field_idx_out) {
    if (!name) return -1;
    for (int i = 0; i < data_ntype; i++)
        for (int j = 0; j < data_types[i].nfields; j++)
            if (strcasecmp(data_types[i].field_name[j], name) == 0) {
                if (field_idx_out) *field_idx_out = j;
                return i;
            }
    return -1;
}

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
    if (e->kind == E_KEYWORD && e->sval) {
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
    if (e->kind == E_INDIRECT) {
        needs_indr = 1;
        /* $.var: child is E_CAPT_COND_ASGN; intern the variable name */
        const EXPR_t *ch = e->nchildren > 0 ? e->children[0] : NULL;
        if (ch && ch->kind == E_CAPT_COND_ASGN && ch->sval)
            var_intern(ch->sval);
        else if (ch && ch->kind == E_CAPT_COND_ASGN && ch->nchildren > 0
                 && ch->children[0] && ch->children[0]->sval)
            var_intern(ch->children[0]->sval);
    }
    if (e->kind == E_FNC && e->sval && strcasecmp(e->sval, "value") == 0)
        needs_indr = 1;  /* VALUE() needs sno_var_get */
    if (e->kind == E_FNC && e->sval && strcasecmp(e->sval, "data") == 0)
        parse_data_spec(e);  /* register DATA type for constructor/field dispatch */
    /* CAPT_COND/CAPT_IMM/CAPT_CUR: varname in children[1]->sval (binop shape) */
    if (e->kind == E_CAPT_COND_ASGN || e->kind == E_CAPT_IMMED_ASGN || e->kind == E_CAPT_CURSOR) {
        /* Try sval first (legacy path), then children[1] (parser binop path) */
        const char *vn = e->sval;
        if (!vn && e->nchildren >= 2 && e->children[1] && e->children[1]->sval)
            vn = e->children[1]->sval;
        if (vn && !is_keyword_name(vn))
            var_intern(vn);
    }
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
/* Emitted only when E_INDIRECT nodes are present. Uses if-else chain over       */
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

/* ── $sno_data_init — emitted WAT function that registers all DATA types ──── */
static void emit_data_init_func(void) {
    if (data_ntype == 0) return;

    W("\n  ;; $sno_data_init — register DATA types at program start\n");
    W("  (func $sno_data_init\n");
    W("    (local $fp i32)\n");  /* field-names array pointer */
    W("    (local $ti i32)\n");  /* type index (result of sno_data_define, discarded) */

    for (int ti = 0; ti < data_ntype; ti++) {
        DataType *dt = &data_types[ti];
        int name_off = strlit_abs(dt->name_lit);
        int name_len = str_lits[dt->name_lit].len;

        W("    ;; DATA %s: %d field(s)\n", dt->name, dt->nfields);

        if (dt->nfields > 0) {
            /* Allocate field-name pairs array: nfields * 8 bytes */
            W("    (local.set $fp (call $sno_str_alloc (i32.const %d)))\n",
              dt->nfields * 8);
            /* Write each field (off, len) pair */
            for (int fi = 0; fi < dt->nfields; fi++) {
                int foff = strlit_abs(dt->field_lit[fi]);
                int flen = str_lits[dt->field_lit[fi]].len;
                W("    (i32.store (i32.add (local.get $fp) (i32.const %d)) (i32.const %d))\n",
                  fi * 8,     foff);
                W("    (i32.store (i32.add (local.get $fp) (i32.const %d)) (i32.const %d))\n",
                  fi * 8 + 4, flen);
            }
            W("    (local.set $ti (call $sno_data_define\n");
            W("      (i32.const %d) (i32.const %d)\n", name_off, name_len);
            W("      (i32.const %d) (local.get $fp)))\n", dt->nfields);
        } else {
            W("    (local.set $ti (call $sno_data_define\n");
            W("      (i32.const %d) (i32.const %d)\n", name_off, name_len);
            W("      (i32.const 0) (i32.const 0)))\n");
        }
    }
    W("  )\n");
}


WasmTy emit_wasm_expr(const EXPR_t *e) {
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
    case E_MNS: {
        /* For literals: always wraps a numeric child */
        WasmTy t = emit_wasm_expr(e->nchildren > 0 ? e->children[0] : NULL);
        if (t == TY_INT) {
            W("    (i64.const -1)\n");
            W("    (i64.mul)\n");
        } else if (t == TY_FLOAT) {
            W("    (f64.neg)\n");
        }
        return t;
    }
    case E_PLS:
        return emit_wasm_expr(e->nchildren > 0 ? e->children[0] : NULL);
    case E_ADD: case E_SUB: case E_MUL: case E_DIV: case E_MOD: case E_POW: {
        WasmTy lt = emit_wasm_expr(e->children[0]);
        if (lt == TY_STR) { W("    (call $sno_str_to_int)\n"); lt = TY_INT; }
        WasmTy rt = emit_wasm_expr(e->children[1]);
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
            else if (e->kind == E_MUL) W("    (i64.mul)\n");
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
        else if (e->kind == E_MUL) W("    (f64.mul)\n");
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
    case E_INDIRECT: {
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
        } else if (name_e->kind == E_CAPT_COND_ASGN && name_e->sval) {
            /* $.var — parser wraps as E_INDIRECT(E_CAPT_COND_ASGN(var)); use identifier name */
            int si = strlit_intern(name_e->sval);
            W("    (i32.const %d) ;; $.var name=%s\n", strlit_abs(si), name_e->sval);
            W("    (i32.const %d)\n", str_lits[si].len);
        } else if (name_e->kind == E_CAPT_COND_ASGN && name_e->nchildren > 0
                   && name_e->children[0] && name_e->children[0]->sval) {
            /* $.var alternative: sval on child E_VAR */
            const char *vn = name_e->children[0]->sval;
            int si = strlit_intern(vn);
            W("    (i32.const %d) ;; $.var(child) name=%s\n", strlit_abs(si), vn);
            W("    (i32.const %d)\n", str_lits[si].len);
        } else {
            /* $'lit' or $expr — evaluate child to get name string */
            WasmTy nt = emit_wasm_expr(name_e);
            if (nt == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (nt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
        }
        W("    (call $sno_var_get)\n");
        return TY_STR;
    }
    case E_CAT: {
        if (e->nchildren == 0) {
            int idx = strlit_intern("");
            W("    (i32.const %d)\n", strlit_abs(idx));
            W("    (i32.const 0)\n");
            return TY_STR;
        }
        WasmTy t0 = emit_wasm_expr(e->children[0]);
        if (t0 == TY_INT)   W("    (call $sno_int_to_str)\n");
        if (t0 == TY_FLOAT) W("    (call $sno_float_to_str)\n");
        for (int i = 1; i < e->nchildren; i++) {
            WasmTy ti = emit_wasm_expr(e->children[i]);
            if (ti == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ti == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_str_concat)\n");
        }
        return TY_STR;
    }
    case E_KEYWORD: {
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
    case E_IDX: {
        /* arr<i> or tbl<key> or arr<i,j>
         * children[0] = array/table expression (handle in _off, MAGIC_HANDLE in _len)
         * children[1] = first index
         * children[2] = second index (2D only)
         * Handle representation: var_X_off = heap ptr, var_X_len = 32767 (MAGIC_HANDLE) */
        if (e->nchildren < 2) goto fallback_idx;
        /* Evaluate array/table — leaves (off, len) on stack; off = handle if len==32767 */
        WasmTy at = emit_wasm_expr(e->children[0]);
        if (at != TY_STR) {
            /* shouldn't happen — array vars are always TY_STR with magic len */
            W("    ;; E_IDX: non-string array expression — stub\n");
            { int ei = strlit_intern(""); W("    (drop)\n    (i32.const %d)\n    (i32.const 0)\n", strlit_abs(ei)); }
            return TY_STR;
        }
        /* Stack: (handle_off magic_len) — drop len, keep handle in local */
        W("    (drop)  ;; drop magic_len, keep handle\n");
        W("    (local.set $arr_h)\n");
        if (e->nchildren >= 3 && e->children[2]) {
            /* 2D: arr<row, col> — sno_array_get2(handle, row, col) */
            WasmTy t1 = emit_wasm_expr(e->children[1]);
            if (t1 == TY_STR) W("    (call $sno_str_to_int)\n");
            if (t1 == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
            W("    (i32.wrap_i64)\n");
            W("    (local.set $tmp_i32)  ;; save row\n");
            WasmTy t2 = emit_wasm_expr(e->children[2]);
            if (t2 == TY_STR) W("    (call $sno_str_to_int)\n");
            if (t2 == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
            W("    (i32.wrap_i64)\n");
            W("    (local.set $arr_h2)  ;; save col\n");
            W("    (local.get $arr_h)\n");
            W("    (local.get $tmp_i32)  ;; row\n");
            W("    (local.get $arr_h2)   ;; col\n");
            W("    (call $sno_array_get2)\n");
        } else {
            /* 1D or table: check type — use block for structured exit */
            W("    (block $eidx_done (result i32 i32)\n");
            W("    (local.set $arr_ok (call $sno_handle_type (local.get $arr_h)))\n");
            /* type==0 → null handle → empty string */
            { int ei = strlit_intern("");
              W("    (if (i32.eqz (local.get $arr_ok)) (then\n");
              W("      (i32.const %d) (i32.const 0) (br $eidx_done)))\n", strlit_abs(ei)); }
            /* Evaluate key */
            WasmTy kt = emit_wasm_expr(e->children[1]);
            if (kt == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (kt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (local.set $proto_len)  ;; key_len\n");
            W("    (local.set $arr_ok)     ;; key_off\n");
            /* type==2 → TABLE */
            W("    (if (i32.eq (call $sno_handle_type (local.get $arr_h)) (i32.const 2)) (then\n");
            W("      (local.get $arr_h)\n");
            W("      (local.get $arr_ok) (local.get $proto_len)\n");
            W("      (call $sno_table_get)\n");
            W("      (br $eidx_done)))\n");
            /* ARRAY: key string → i32 index */
            W("    (local.get $arr_ok) (local.get $proto_len)\n");
            W("    (call $sno_str_to_int)\n");
            W("    (i32.wrap_i64)\n");
            W("    (local.set $tmp_i32)\n");
            W("    (local.get $arr_h)\n");
            W("    (local.get $tmp_i32)\n");
            W("    (call $sno_array_get)\n");
            W("    ) ;; end $eidx_done\n");
        }
        /* Result (off, len) on stack — coerce null (0,0) to empty string.
         * Save both to locals; WASM structured control flow requires empty
         * stack at if-entry (only condition allowed). */
        W("    ;; coerce null slot (0,0) to empty string\n");
        W("    (local.set $proto_len)  ;; len\n");
        W("    (local.set $arr_ok)     ;; off\n");
        { int ei = strlit_intern("");
          /* if len==0 (null slot), replace off with empty-string offset */
          W("    (if (i32.eqz (local.get $proto_len)) (then\n");
          W("      (local.set $arr_ok (i32.const %d))))\n", strlit_abs(ei));
        }
        W("    (local.get $arr_ok) (local.get $proto_len)\n");
        return TY_STR;
        fallback_idx:;
        { int ei = strlit_intern(""); W("    (i32.const %d)\n    (i32.const 0)\n", strlit_abs(ei)); }
        return TY_STR;
    }
    case E_FNC: {
        const char *fn = e->sval ? e->sval : "";
        /* ARRAY(n) or ARRAY(n, default) → create 1D array, return handle */
        if (strcasecmp(fn, "array") == 0) {
            int def_idx = strlit_intern("");
            int def_off = strlit_abs(def_idx), def_len = 0;
            if (e->nchildren >= 2) {
                WasmTy dt = emit_wasm_expr(e->children[1]);
                if (dt == TY_INT)   W("    (call $sno_int_to_str)\n");
                if (dt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
                W("    (local.set $proto_len)  ;; def_len\n");
                W("    (local.set $arr_ok)     ;; def_off\n");
            }
            /* Detect multi-dim string literal: E_QLIT with ',' → dispatch sno_array_create2 */
            int is_multidim = 0;
            if (e->nchildren >= 1 && e->children[0]->kind == E_QLIT && e->children[0]->sval) {
                const char *ds = e->children[0]->sval;
                const char *comma = strchr(ds, ',');
                if (comma) {
                    /* Parse "r,c" or "-lo:hi,c" — extract first dim and second dim */
                    /* First dim: up to comma; second dim: after comma */
                    /* For "r,c": lo1=1,hi1=r, lo2=1,hi2=c */
                    /* For "-lo:hi,c": parse lo:hi for first dim */
                    int lo1 = 1, hi1 = 1, lo2 = 1, hi2 = 1;
                    char tmp[256]; strncpy(tmp, ds, 255); tmp[255]=0;
                    char *p = strchr(tmp, ',');
                    if (p) { *p = 0; p++; }
                    /* Parse first dimension: "lo:hi" or "n" (lo=1, hi=n) */
                    char *colon = strchr(tmp, ':');
                    if (colon) { *colon = 0; lo1 = atoi(tmp); hi1 = atoi(colon+1); }
                    else       { lo1 = 1; hi1 = atoi(tmp); }
                    /* Parse second dimension */
                    if (p) {
                        char *colon2 = strchr(p, ':');
                        if (colon2) { *colon2 = 0; lo2 = atoi(p); hi2 = atoi(colon2+1); }
                        else        { lo2 = 1; hi2 = atoi(p); }
                    }
                    /* sno_array_create2(lo1,hi1,lo2,hi2) */
                    W("    (i32.const %d) (i32.const %d)\n", lo1, hi1);
                    W("    (i32.const %d) (i32.const %d)\n", lo2, hi2);
                    W("    (call $sno_array_create2)\n");
                    W("    (i32.const 32767)  ;; MAGIC_HANDLE\n");
                    is_multidim = 1;
                }
            }
            if (!is_multidim) {
                /* 1D: evaluate size arg */
                if (e->nchildren >= 1) {
                    WasmTy st = emit_wasm_expr(e->children[0]);
                    if (st == TY_INT)   W("    (i32.wrap_i64)\n");
                    else if (st == TY_FLOAT) { W("    (i64.trunc_f64_s)\n"); W("    (i32.wrap_i64)\n"); }
                    else { W("    (call $sno_str_to_int)\n"); W("    (i32.wrap_i64)\n"); }
                } else {
                    W("    (i32.const 0)\n");
                }
                if (e->nchildren >= 2) {
                    W("    (local.get $arr_ok)    ;; def_off\n");
                    W("    (local.get $proto_len) ;; def_len\n");
                } else {
                    W("    (i32.const %d)  ;; def_off = empty\n", def_off);
                    W("    (i32.const 0)   ;; def_len = 0\n");
                }
                W("    (call $sno_array_create)\n");
                W("    (i32.const 32767)  ;; MAGIC_HANDLE sentinel\n");
            }
            return TY_STR;
        }
        /* TABLE(n) or TABLE() → create table, return handle */
        if (strcasecmp(fn, "table") == 0) {
            int cap = 16;
            if (e->nchildren >= 1 && e->children[0]->kind == E_ILIT)
                cap = (int)e->children[0]->ival;
            W("    (i32.const %d)\n", cap < 8 ? 8 : cap);
            W("    (call $sno_table_create)\n");
            W("    (i32.const 32767)\n");
            return TY_STR;
        }
        /* PROTOTYPE(arr) → dimension string "n" or "r,c" */
        if (strcasecmp(fn, "prototype") == 0 && e->nchildren >= 1) {
            WasmTy at2 = emit_wasm_expr(e->children[0]);
            if (at2 == TY_STR) {
                W("    (drop)  ;; drop magic_len\n");
                W("    (local.set $arr_h)\n");
                /* write prototype string into output buffer scratch area */
                W("    (local.get $arr_h)\n");
                W("    (i32.const 0)  ;; write to output buffer start (safe — overwritten before use)\n");
                W("    (call $sno_array_prototype)\n");
                W("    (local.set $proto_len)\n");
                /* allocate permanent copy in string heap */
                W("    (local.get $proto_len)  (call $sno_str_alloc)\n");
                W("    (local.set $arr_ok)  ;; arr_ok = dest ptr\n");
                /* byte-copy loop: i=0; while i<proto_len: mem[arr_ok+i]=mem[i]; i++ */
                W("    (local.set $tmp_i32 (i32.const 0))  ;; i=0\n");
                W("    (block $cpy_done\n");
                W("      (loop $cpy\n");
                W("        (br_if $cpy_done (i32.ge_u (local.get $tmp_i32) (local.get $proto_len)))\n");
                W("        (i32.store8\n");
                W("          (i32.add (local.get $arr_ok) (local.get $tmp_i32))\n");
                W("          (i32.load8_u (local.get $tmp_i32)))\n");
                W("        (local.set $tmp_i32 (i32.add (local.get $tmp_i32) (i32.const 1)))\n");
                W("        (br $cpy)))\n");
                W("    (local.get $arr_ok)\n");
                W("    (local.get $proto_len)\n");
            } else {
                /* not an array handle — return empty */
                int ei = strlit_intern("");
                W("    (drop)\n");
                W("    (i32.const %d)\n    (i32.const 0)\n", strlit_abs(ei));
            }
            return TY_STR;
        }
        /* DIFFER(a) — 1-arg: succeeds (returns null) if a is null/empty; fails otherwise */
        if (strcasecmp(fn, "differ") == 0 && e->nchildren == 1) {
            /* In SNOBOL4: DIFFER(x) succeeds if x is null, fails if non-null.
             * We emit inline — caller uses :s/:f branching on $ok. */
            WasmTy da = emit_wasm_expr(e->children[0]);
            if (da == TY_INT) W("    (call $sno_int_to_str)\n");
            if (da == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            /* stack: (off, len) — succeed if len==0 (null), fail otherwise */
            /* Return value not meaningful — just leave empty string */
            W("    ;; DIFFER 1-arg: set $ok based on emptiness\n");
            W("    (local.set $proto_len)  ;; len\n");
            W("    (drop)  ;; off\n");
            W("    (local.set $ok (i32.eqz (local.get $proto_len)))\n");
            { int ei = strlit_intern(""); W("    (i32.const %d)\n    (i32.const 0)\n", strlit_abs(ei)); }
            return TY_STR;
        }
        /* DIFFER(a, b) — 2-arg: succeeds if a != b, fails if a == b */
        if (strcasecmp(fn, "differ") == 0 && e->nchildren >= 2) {
            WasmTy da = emit_wasm_expr(e->children[0]);
            if (da == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (da == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy db = emit_wasm_expr(e->children[1]);
            if (db == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (db == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            /* stack: (b_off b_len a_off a_len) — compare */
            W("    ;; DIFFER(a,b): ok = (a != b)\n");
            W("    (local.set $proto_len)  ;; a_len\n");
            W("    (local.set $arr_ok)     ;; a_off\n");
            W("    ;; b on stack: (b_off b_len)\n");
            W("    (local.get $arr_ok) (local.get $proto_len)\n");
            W("    (call $sno_str_eq)\n");
            W("    (local.set $ok (i32.eqz))  ;; ok = NOT equal\n");
            { int ei = strlit_intern(""); W("    (i32.const %d)\n    (i32.const 0)\n", strlit_abs(ei)); }
            return TY_STR;
        }
        /* VALUE(name) → value of variable named by string */
        if (strcasecmp(fn, "value") == 0 && e->nchildren >= 1) {
            /* Emit as indirect variable lookup */
            WasmTy vt = emit_wasm_expr(e->children[0]);
            if (vt == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (vt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    ;; VALUE(): indirect var lookup by name string\n");
            W("    (local.set $proto_len)  ;; name_len\n");
            W("    (local.set $arr_ok)     ;; name_off\n");
            W("    (local.get $arr_ok) (local.get $proto_len)\n");
            W("    (call $sno_var_get)\n");
            return TY_STR;
        }
        /* REMDR(a,b) → i64.rem_s */
        if (strcasecmp(fn, "remdr") == 0 && e->nchildren >= 2) {
            WasmTy ta = emit_wasm_expr(e->children[0]);
            if (ta == TY_STR) W("    (call $sno_str_to_int)\n");
            if (ta == TY_FLOAT) { W("    (i64.trunc_f64_s)\n"); }
            WasmTy tb = emit_wasm_expr(e->children[1]);
            if (tb == TY_STR) W("    (call $sno_str_to_int)\n");
            if (tb == TY_FLOAT) { W("    (i64.trunc_f64_s)\n"); }
            W("    (i64.rem_s)\n");
            return TY_INT;
        }
        /* SIZE(s) → i64 character count */
        if (strcasecmp(fn, "size") == 0 && e->nchildren >= 1) {
            WasmTy ta = emit_wasm_expr(e->children[0]);
            if (ta == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ta == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_size)\n");
            return TY_INT;
        }
        /* DUPL(s, n) → replicated string */
        if (strcasecmp(fn, "dupl") == 0 && e->nchildren >= 2) {
            WasmTy ta = emit_wasm_expr(e->children[0]);
            if (ta == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ta == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy tb = emit_wasm_expr(e->children[1]);
            if (tb == TY_STR) W("    (call $sno_str_to_int)\n");
            if (tb == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
            W("    (call $sno_dupl)\n");
            return TY_STR;
        }
        /* REPLACE(s, from, to) → translated string */
        if (strcasecmp(fn, "replace") == 0 && e->nchildren >= 3) {
            WasmTy ts = emit_wasm_expr(e->children[0]);
            if (ts == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ts == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy tf = emit_wasm_expr(e->children[1]);
            if (tf == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (tf == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            WasmTy tt = emit_wasm_expr(e->children[2]);
            if (tt == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (tt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_replace)\n");
            return TY_STR;
        }
        /* CONVERT(val, type_str) → coerced value */
        if (strcasecmp(fn, "convert") == 0 && e->nchildren >= 2) {
            const EXPR_t *type_e = e->children[1];
            const char *tname = (type_e && type_e->kind == E_QLIT && type_e->sval)
                                 ? type_e->sval : "";
            /* TABLE → ARRAY: iterate buckets, build 2-column array (key,val) */
            if (strcasecmp(tname, "array") == 0) {
                WasmTy tv = emit_wasm_expr(e->children[0]);
                (void)tv;
                W("    (drop)  ;; drop magic sentinel, keep handle in $arr_h\n");
                W("    (local.set $arr_h)\n");
                /* nrows = count of occupied entries = i32.load(h+12) */
                W("    (local.set $tmp_i32 (i32.load (i32.add (local.get $arr_h) (i32.const 12))))\n");
                /* create 2-col array: sno_array_create2(1, nrows, 1, 2) */
                W("    (i32.const 1)\n");
                W("    (local.get $tmp_i32)  ;; hi1=nrows\n");
                W("    (i32.const 1)\n");
                W("    (i32.const 2)         ;; hi2=2 (cols: key=1, val=2)\n");
                W("    (call $sno_array_create2)\n");
                W("    (local.set $arr_h2)   ;; $arr_h2 = new array handle\n");
                /* loop: $arr_ok = bucket index, $proto_len = row index (1-based) */
                W("    (local.set $arr_ok (i32.const 0))    ;; bucket index\n");
                W("    (local.set $proto_len (i32.const 1)) ;; row counter (1-based)\n");
                W("    (block $tbl2arr_done\n");
                W("      (loop $tbl2arr_loop\n");
                W("        (br_if $tbl2arr_done (i32.ge_u (local.get $arr_ok)\n");
                W("          (call $sno_table_cap (local.get $arr_h))))\n");
                /* get_bucket returns (ko, kl, vo, vl) — skip if kl==0 (empty) */
                W("        (local.get $arr_h) (local.get $arr_ok)\n");
                W("        (call $sno_table_get_bucket)\n");
                /* stack: ko kl vo vl — check kl */
                W("        (local.set $int_pred_len)  ;; vl\n");
                W("        (local.set $int_pred_off)  ;; vo\n");
                W("        (local.set $int_pred_c)    ;; kl\n");
                W("        (local.set $int_pred_i)    ;; ko\n");
                W("        (if (i32.gt_u (local.get $int_pred_c) (i32.const 0)) (then\n");
                /* store key at (row, 1) */
                W("          (local.get $arr_h2)\n");
                W("          (local.get $proto_len)  ;; row\n");
                W("          (i32.const 1)           ;; col=1 (key)\n");
                W("          (local.get $int_pred_i) (local.get $int_pred_c)  ;; key off/len\n");
                W("          (call $sno_array_set2)\n");
                W("          (drop)\n");
                /* store val at (row, 2) */
                W("          (local.get $arr_h2)\n");
                W("          (local.get $proto_len)  ;; row\n");
                W("          (i32.const 2)           ;; col=2 (val)\n");
                W("          (local.get $int_pred_off) (local.get $int_pred_len)  ;; val off/len\n");
                W("          (call $sno_array_set2)\n");
                W("          (drop)\n");
                W("          (local.set $proto_len (i32.add (local.get $proto_len) (i32.const 1)))\n");
                W("        ))\n");
                W("        (local.set $arr_ok (i32.add (local.get $arr_ok) (i32.const 1)))\n");
                W("        (br $tbl2arr_loop)))\n");
                /* push result: array handle + magic sentinel */
                W("    (local.get $arr_h2)\n");
                W("    (i32.const 32767)  ;; MAGIC_HANDLE\n");
                return TY_STR;
            }
            /* ARRAY → TABLE: iterate array rows (assumed 2-col key/val) */
            if (strcasecmp(tname, "table") == 0) {
                WasmTy tv = emit_wasm_expr(e->children[0]);
                (void)tv;
                W("    (drop)  ;; drop magic sentinel\n");
                W("    (local.set $arr_h)\n");
                /* nrows = hi1 - lo1 + 1 */
                W("    (local.set $tmp_i32 (i32.add (i32.sub\n");
                W("      (i32.load (i32.add (local.get $arr_h) (i32.const 12)))\n");
                W("      (i32.load (i32.add (local.get $arr_h) (i32.const 8))))\n");
                W("      (i32.const 1)))  ;; nrows\n");
                W("    (i32.add (local.get $tmp_i32) (i32.const 4))\n");
                W("    (call $sno_table_create)\n");
                W("    (local.set $arr_h2)  ;; new table handle\n");
                /* loop rows: $arr_ok = row (1-based from lo1) */
                W("    (local.set $arr_ok (i32.load (i32.add (local.get $arr_h) (i32.const 8))))  ;; lo1\n");
                W("    (block $arr2tbl_done\n");
                W("      (loop $arr2tbl_loop\n");
                W("        (br_if $arr2tbl_done (i32.gt_s (local.get $arr_ok)\n");
                W("          (i32.load (i32.add (local.get $arr_h) (i32.const 12)))))\n");  /* hi1 */
                /* get key = array[row,1] */
                W("        (local.get $arr_h) (local.get $arr_ok) (i32.const 1)\n");
                W("        (call $sno_array_get2)\n");
                W("        (local.set $int_pred_c)   ;; key_len\n");
                W("        (local.set $int_pred_i)   ;; key_off\n");
                /* get val = array[row,2] */
                W("        (local.get $arr_h) (local.get $arr_ok) (i32.const 2)\n");
                W("        (call $sno_array_get2)\n");
                W("        (local.set $int_pred_len)  ;; val_len\n");
                W("        (local.set $int_pred_off)  ;; val_off\n");
                /* table_set(h, key_off, key_len, val_off, val_len) */
                W("        (local.get $arr_h2)\n");
                W("        (local.get $int_pred_i) (local.get $int_pred_c)\n");
                W("        (local.get $int_pred_off) (local.get $int_pred_len)\n");
                W("        (call $sno_table_set)\n");
                W("        (local.set $arr_ok (i32.add (local.get $arr_ok) (i32.const 1)))\n");
                W("        (br $arr2tbl_loop)))\n");
                W("    (local.get $arr_h2)\n");
                W("    (i32.const 32767)  ;; MAGIC_HANDLE\n");
                return TY_STR;
            }
            /* scalar type conversions */
            WasmTy tv = emit_wasm_expr(e->children[0]);
            if (strcasecmp(tname, "integer") == 0) {
                if (tv == TY_STR)   W("    (call $sno_str_to_int)\n");
                if (tv == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
                return TY_INT;
            } else if (strcasecmp(tname, "real") == 0) {
                if (tv == TY_STR)   W("    (call $sno_str_to_float)\n");
                if (tv == TY_INT)   W("    (f64.convert_i64_s)\n");
                return TY_FLOAT;
            } else {
                if (tv == TY_INT)   W("    (call $sno_int_to_str)\n");
                if (tv == TY_FLOAT) W("    (call $sno_float_to_str)\n");
                return TY_STR;
            }
        }
        /* DATATYPE(val) → string type name: "string", "integer", "real" */
        if (strcasecmp(fn, "datatype") == 0 && e->nchildren >= 1) {
            WasmTy tv = emit_wasm_expr(e->children[0]);
            /* For TY_STR, value might be a DATA object handle — check at runtime */
            if (tv == TY_STR) {
                /* Stack: (off i32) (len i32) */
                W("    (local.set $proto_len)  ;; save len\n");
                W("    (local.set $arr_ok)     ;; save off (= handle if DATA)\n");
                W("    (block $datatype_done (result i32 i32)\n");
                /* If sno_handle_type(off) == 3, it's a DATA object */
                W("      (if (i32.eq (call $sno_handle_type (local.get $arr_ok)) (i32.const 3)) (then\n");
                W("        (call $sno_data_typename (local.get $arr_ok))\n");
                W("        (br $datatype_done)))\n");
                /* Otherwise compile-time type = string */
                {   int ti = strlit_intern("string");
                    W("      (i32.const %d) (i32.const %d)\n", strlit_abs(ti), str_lits[ti].len); }
                W("    ) ;; $datatype_done\n");
            } else if (tv == TY_INT) {
                W("    (drop)\n");
                int ti = strlit_intern("integer");
                W("    (i32.const %d) ;; datatype=integer\n", strlit_abs(ti));
                W("    (i32.const %d)\n", str_lits[ti].len);
            } else {
                W("    (drop)\n");
                int ti = strlit_intern("real");
                W("    (i32.const %d) ;; datatype=real\n", strlit_abs(ti));
                W("    (i32.const %d)\n", str_lits[ti].len);
            }
            return TY_STR;
        }
        /* ITEM(arr, i) or ITEM(arr, i, j, ...) — programmatic subscript, same as arr<i> */
        if (strcasecmp(fn, "item") == 0 && e->nchildren >= 2) {
            /* Evaluate array/table handle */
            WasmTy at = emit_wasm_expr(e->children[0]);
            (void)at;
            W("    (drop)  ;; drop magic_len, keep handle\n");
            W("    (local.set $arr_h)\n");
            if (e->nchildren >= 3 && e->children[2]) {
                /* 2D: item(arr, row, col) — same as E_IDX 2D path */
                WasmTy t1 = emit_wasm_expr(e->children[1]);
                if (t1 == TY_STR) W("    (call $sno_str_to_int)\n");
                if (t1 == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
                W("    (i32.wrap_i64)\n");
                W("    (local.set $tmp_i32)  ;; save row\n");
                WasmTy t2 = emit_wasm_expr(e->children[2]);
                if (t2 == TY_STR) W("    (call $sno_str_to_int)\n");
                if (t2 == TY_FLOAT) W("    (i64.trunc_f64_s)\n");
                W("    (i32.wrap_i64)\n");
                W("    (local.set $arr_h2)  ;; save col\n");
                W("    (local.get $arr_h)\n");
                W("    (local.get $tmp_i32)  ;; row\n");
                W("    (local.get $arr_h2)   ;; col\n");
                W("    (call $sno_array_get2)\n");
            } else {
                /* 1D or table — same as E_IDX 1D path */
                W("    (block $item_done (result i32 i32)\n");
                W("    (local.set $arr_ok (call $sno_handle_type (local.get $arr_h)))\n");
                { int ei = strlit_intern("");
                  W("    (if (i32.eqz (local.get $arr_ok)) (then\n");
                  W("      (i32.const %d) (i32.const 0) (br $item_done)))\n", strlit_abs(ei)); }
                WasmTy kt = emit_wasm_expr(e->children[1]);
                if (kt == TY_INT)   W("    (call $sno_int_to_str)\n");
                if (kt == TY_FLOAT) W("    (call $sno_float_to_str)\n");
                W("    (local.set $proto_len)  ;; key_len\n");
                W("    (local.set $arr_ok)     ;; key_off\n");
                W("    (if (i32.eq (call $sno_handle_type (local.get $arr_h)) (i32.const 2)) (then\n");
                W("      (local.get $arr_h)\n");
                W("      (local.get $arr_ok) (local.get $proto_len)\n");
                W("      (call $sno_table_get)\n");
                W("      (br $item_done)))\n");
                W("    (local.get $arr_ok) (local.get $proto_len)\n");
                W("    (call $sno_str_to_int)\n");
                W("    (i32.wrap_i64)\n");
                W("    (local.set $tmp_i32)\n");
                W("    (local.get $arr_h)\n");
                W("    (local.get $tmp_i32)\n");
                W("    (call $sno_array_get)\n");
                W("    ) ;; end $item_done\n");
            }
            /* coerce null (0,0) to empty string */
            W("    (local.set $proto_len)\n");
            W("    (local.set $arr_ok)\n");
            { int ei = strlit_intern("");
              W("    (if (i32.eqz (local.get $proto_len)) (then\n");
              W("      (local.set $arr_ok (i32.const %d))))\n", strlit_abs(ei)); }
            W("    (local.get $arr_ok) (local.get $proto_len)\n");
            return TY_STR;
        }
        /* DATA declaration: data("typename", field1, ...) — handled at prescan/init, emit nothing */
        if (strcasecmp(fn, "data") == 0) {
            int idx = strlit_intern("");
            W("    (i32.const %d)\n", strlit_abs(idx));
            W("    (i32.const 0)\n");
            return TY_STR;
        }
        /* DATA constructor: <TypeName>(arg1, arg2, ...) */
        {   int dti = data_type_by_name(fn);
            if (dti >= 0) {
                DataType *dt = &data_types[dti];
                /* Allocate DATA instance */
                W("    (i32.const %d)  ;; type_idx for %s\n", dti, dt->name);
                W("    (i32.const %d)  ;; nfields\n", dt->nfields);
                W("    (call $sno_data_new)\n");
                W("    (local.set $arr_h)\n");
                /* Set each field from args (missing args → empty string) */
                for (int fi = 0; fi < dt->nfields; fi++) {
                    int empty_idx = strlit_intern("");
                    if (fi < e->nchildren) {
                        WasmTy at = emit_wasm_expr(e->children[fi]);
                        if (at == TY_INT)   W("    (call $sno_int_to_str)\n");
                        if (at == TY_FLOAT) W("    (call $sno_float_to_str)\n");
                    } else {
                        W("    (i32.const %d)\n", strlit_abs(empty_idx));
                        W("    (i32.const 0)\n");
                    }
                    W("    (local.set $proto_len)  ;; field val_len\n");
                    W("    (local.set $arr_ok)     ;; field val_off\n");
                    W("    (local.get $arr_h)\n");
                    W("    (i32.const %d)  ;; field index %d\n", fi, fi);
                    W("    (local.get $arr_ok)\n");
                    W("    (local.get $proto_len)\n");
                    W("    (call $sno_data_set_field)\n");
                }
                /* Return handle as (i32, magic_len=32767) */
                W("    (local.get $arr_h)\n");
                W("    (i32.const 32767)  ;; MAGIC_HANDLE\n");
                return TY_STR;
            }
        }
        /* DATA field getter: <fieldname>(handle) — if fn matches a registered field */
        {   int fi = -1;
            int dti = data_field_owner(fn, &fi);
            if (dti >= 0 && e->nchildren >= 1) {
                /* Evaluate the handle argument */
                WasmTy ht = emit_wasm_expr(e->children[0]);
                (void)ht;
                W("    (drop)  ;; drop magic_len, keep handle\n");
                W("    (i32.const %d)  ;; field index\n", fi);
                W("    (call $sno_data_get_field)\n");
                return TY_STR;
            }
        }
        /* Default: evaluate args, drop results, return empty string */
        for (int i = 0; i < e->nchildren; i++) {
            WasmTy t = emit_wasm_expr(e->children[i]);
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
        WasmTy t = emit_wasm_expr(e->children[0]);
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
    WasmTy t0 = emit_wasm_expr(e->children[0]);
    if (t0 == TY_INT)   W("      (call $sno_int_to_str)\n");
    if (t0 == TY_FLOAT) W("      (call $sno_float_to_str)\n");
    WasmTy t1 = emit_wasm_expr(e->children[1]);
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
    /* E_CAPT_COND_ASGN (.var) — conditional capture: extract subj[before..cursor] on success
     * IR shape: binop(E_CAPT_COND_ASGN, pattern_child, E_VAR(varname))
     * sval is NOT set; varname lives in children[1]->sval. */
    if (pat->kind == E_CAPT_COND_ASGN) {
        const EXPR_t *child   = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        const EXPR_t *varnode = (pat->nchildren >= 2) ? pat->children[1] : NULL;
        const char *varname = varnode && varnode->sval ? varnode->sval
                            : (pat->sval ? pat->sval : "");

        W("      ;; E_CAPT_COND_ASGN .%s: save cursor, run child, capture on success\n", varname);
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
    /* E_CAPT_IMMED_ASGN ($var) — immediate capture: captures exactly what the inner pattern matched.
     * IR shape: binop(E_CAPT_IMMED_ASGN, pattern_child, E_VAR(varname))
     * For a QLIT child: sno_pat_search may scan forward from cursor to find the literal,
     * so the match starts at (new_cursor - ndl_len), not at pat_before.
     * We track child cursor-before to compute match length = new_cursor - child_cursor_before,
     * and match offset via a second local $pat_imm_start that we set before child runs. */
    if (pat->kind == E_CAPT_IMMED_ASGN) {
        const EXPR_t *child   = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        const EXPR_t *varnode = (pat->nchildren >= 2) ? pat->children[1] : NULL;
        const char *varname = varnode && varnode->sval ? varnode->sval
                            : (pat->sval ? pat->sval : "");
        W("      ;; E_CAPT_IMMED_ASGN $%s: run child, capture exactly matched span\n", varname);
        /* Save cursor before child so we can detect how far child advanced */
        W("      (local.set $pat_before (local.get $pat_cursor))\n");
        if (child) emit_pattern_node(child);
        /* After child: pat_cursor = end of match (or -1).
         * For QLIT the match may not start at pat_before (search scans forward).
         * The child (QLIT) leaves cursor = match_start + ndl_len.
         * We don't have match_start directly; use sno_pat_search convention:
         * captured len = cursor - pat_before only if anchored. For unanchored search
         * (QLIT in non-ANCHOR mode), use the actual child match length.
         * Simplest correct approach: emit a sno_pat_search wrapper that also
         * returns match_start. Until then: for QLIT child, len = ndl_len,
         * off = subj_off + (cursor - ndl_len). For generic child: off = subj_off + pat_before,
         * len = cursor - pat_before (COND semantics as fallback). */
        if (*varname && !is_keyword_name(varname)) {
            if (child && child->kind == E_QLIT) {
                int idx = strlit_intern(child->sval ? child->sval : "");
                int ndl_len = str_lits[idx].len;
                W("      (if (i32.ge_s (local.get $pat_cursor) (i32.const 0)) (then\n");
                W("        (global.set $var_%s_off\n", varname);
                W("          (i32.add (local.get $pat_subj_off)\n");
                W("                   (i32.sub (local.get $pat_cursor) (i32.const %d))))\n", ndl_len);
                W("        (global.set $var_%s_len (i32.const %d))\n", varname, ndl_len);
                W("      ))\n");
            } else {
                /* Generic child: capture subj[pat_before..cursor] (same as COND) */
                W("      (if (i32.ge_s (local.get $pat_cursor) (i32.const 0)) (then\n");
                W("        (global.set $var_%s_off\n", varname);
                W("          (i32.add (local.get $pat_subj_off) (local.get $pat_before)))\n");
                W("        (global.set $var_%s_len\n", varname);
                W("          (i32.sub (local.get $pat_cursor) (local.get $pat_before)))\n");
                W("      ))\n");
            }
        }
        return;
    }
    /* E_CAPT_CURSOR (@var) — zero-width cursor capture: store cursor position as integer string
     * IR shape (cursor-after): binop(E_CAPT_CURSOR, pattern_child, E_VAR(varname))
     * children[0] = pattern to match first; children[1] = target variable.
     * Cursor position captured AFTER children[0] matches. */
    if (pat->kind == E_CAPT_CURSOR) {
        const EXPR_t *child   = (pat->nchildren >= 1) ? pat->children[0] : NULL;
        const EXPR_t *varnode = (pat->nchildren >= 2) ? pat->children[1] : NULL;
        const char *varname = varnode && varnode->sval ? varnode->sval
                            : (pat->sval ? pat->sval : "");
        /* Run the child pattern first (advances cursor), then capture position */
        if (child) emit_pattern_node(child);
        W("      ;; E_CAPT_CURSOR @%s: store cursor position as int string into var\n", varname);
        if (*varname && !is_keyword_name(varname)) {
            /* Only capture if child succeeded */
            W("      (if (i32.ge_s (local.get $pat_cursor) (i32.const 0)) (then\n");
            W("        (i64.extend_i32_u (local.get $pat_cursor))\n");
            W("        (call $sno_int_to_str)\n");
            W("        (global.set $var_%s_len)\n", varname);
            W("        (global.set $var_%s_off)\n", varname);
            W("      ))\n");
        }
        /* zero-width as far as pattern matching is concerned: cursor already set by child */
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
            WasmTy at = emit_wasm_expr(pat->children[0]);
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
    WasmTy tp = emit_wasm_expr(pat);
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
            WasmTy ta = emit_wasm_expr(e->children[0]);
            WasmTy tb = emit_wasm_expr(e->children[1]);
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
            WasmTy ta = emit_wasm_expr(e->children[0]);
            if (ta == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (ta == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            WasmTy tb = emit_wasm_expr(e->children[1]);
            if (tb == TY_INT)   W("      (call $sno_int_to_str)\n");
            if (tb == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            W("      (call $sno_lgt)\n");
            return;
        }
        /* INTEGER predicate: succeed if value is/coerces-to integer */
        if (strcasecmp(fn, "integer") == 0 && e->nchildren >= 1) {
            WasmTy ta = emit_wasm_expr(e->children[0]);
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
            WasmTy t = emit_wasm_expr(e->children[i]);
            if (t == TY_STR) { W("      (drop)\n      (drop)\n"); }
            else              { W("      (drop)\n"); }
        }
        W("      (i32.const 1) ;; builtin '%s' — treated as succeed\n", fn);
        return;
    }
    /* Any other expression: evaluate it; non-empty/non-zero = success */
    WasmTy t = emit_wasm_expr(e);
    if (t == TY_STR) {
        /* Stack: (off, len) — success if len > 0 */
        W("      (local.set $tmp_i32)  ;; save len\n");
        W("      (drop)               ;; drop off\n");
        W("      (local.get $tmp_i32) ;; restore len\n");
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
    WasmTy ty = emit_wasm_expr(val);
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
    /* Array/Table/DATA handle locals (M-SW-C02) */
    W("    (local $arr_h i32)\n");    /* array/table handle */
    W("    (local $arr_ok i32)\n");   /* bounds-check result / key_off scratch */
    W("    (local $proto_len i32)\n"); /* prototype string length / key_len scratch */
    W("    (local $arr_h2 i32)\n");   /* 2D col scratch */

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

    /* Call $sno_data_init to register DATA types before program execution */
    if (data_ntype > 0)
        W("    (call $sno_data_init)\n");

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
        int is_idxassign = 0;
        if (s->has_eq && s->subject) {
            const char *n = s->subject->sval ? s->subject->sval : "";
            if ((s->subject->kind == E_VAR || s->subject->kind == E_KEYWORD)) {
                if (strcasecmp(n, "OUTPUT") == 0)
                    is_output = 1;
                else if (!is_keyword_name(n))
                    is_varassign = 1;
            } else if (s->subject->kind == E_INDIRECT) {
                is_indrassign = 1;
            } else if (s->subject->kind == E_IDX) {
                is_idxassign = 1;
            } else if (s->subject->kind == E_FNC && s->subject->sval
                       && strcasecmp(s->subject->sval, "item") == 0) {
                is_idxassign = 1;
            } else if (s->subject->kind == E_FNC && s->subject->sval
                       && data_field_owner(s->subject->sval, NULL) >= 0) {
                is_idxassign = 1;  /* field(handle) = val */
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
            WasmTy ty = emit_wasm_expr(s->replacement);
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
            /* Evaluate name expression — same semantics as E_INDIRECT rvalue */
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
            } else if (name_e->kind == E_CAPT_COND_ASGN && name_e->sval) {
                int si = strlit_intern(name_e->sval);
                W("      (i32.const %d) ;; $.var lval name=%s\n", strlit_abs(si), name_e->sval);
                W("      (i32.const %d)\n", str_lits[si].len);
            } else if (name_e->kind == E_CAPT_COND_ASGN && name_e->nchildren > 0
                       && name_e->children[0] && name_e->children[0]->sval) {
                const char *vn = name_e->children[0]->sval;
                int si = strlit_intern(vn);
                W("      (i32.const %d) ;; $.var(child) lval name=%s\n", strlit_abs(si), vn);
                W("      (i32.const %d)\n", str_lits[si].len);
            } else {
                WasmTy nt = emit_wasm_expr(name_e);
                if (nt == TY_INT)   W("      (call $sno_int_to_str)\n");
                if (nt == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            }
            /* Stack: (name_off name_len) — save in locals */
            W("      (local.set $indr_nl)\n");
            W("      (local.set $indr_no)\n");
            /* Evaluate replacement value */
            WasmTy vt = emit_wasm_expr(s->replacement);
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
        } else if (is_idxassign) {
            /* field(handle) = val — DATA field setter */
            if (s->subject->kind == E_FNC && s->subject->sval
                && data_field_owner(s->subject->sval, NULL) >= 0) {
                int fi = -1;
                int dti = data_field_owner(s->subject->sval, &fi);
                (void)dti;
                W("      ;; %s(handle) = val (field setter)\n", s->subject->sval);
                /* Evaluate handle argument */
                const EXPR_t *h_e = s->subject->nchildren > 0 ? s->subject->children[0] : NULL;
                if (h_e) { WasmTy ht = emit_wasm_expr(h_e); (void)ht; }
                else     { W("      (i32.const 0) (i32.const 0)\n"); }
                W("      (drop)  ;; drop magic_len, keep handle\n");
                W("      (local.set $arr_h)\n");
                /* Evaluate replacement value — coerce to string */
                WasmTy vt = emit_wasm_expr(s->replacement);
                if (vt == TY_INT)   W("      (call $sno_int_to_str)\n");
                if (vt == TY_FLOAT) W("      (call $sno_float_to_str)\n");
                W("      (local.set $proto_len)  ;; val_len\n");
                W("      (local.set $arr_ok)     ;; val_off\n");
                W("      (local.get $arr_h)\n");
                W("      (i32.const %d)  ;; field index\n", fi);
                W("      (local.get $arr_ok)\n");
                W("      (local.get $proto_len)\n");
                W("      (call $sno_data_set_field)\n");
                W("      (local.set $ok (i32.const 1))\n");
            } else {
            /* arr<i> = val  or  tbl<key> = val  or  arr<r,c> = val */
            const EXPR_t *arr_e  = s->subject->nchildren > 0 ? s->subject->children[0] : NULL;
            const EXPR_t *idx_e  = s->subject->nchildren > 1 ? s->subject->children[1] : NULL;
            const EXPR_t *idx2_e = s->subject->nchildren > 2 ? s->subject->children[2] : NULL;
            W("      ;; arr/tbl<idx> = val (idxassign)\n");
            /* Evaluate array/table handle — expect TY_STR (handle, 32767) */
            if (arr_e) {
                WasmTy at = emit_wasm_expr(arr_e);
                (void)at;
            } else {
                W("      (i32.const 0) (i32.const 0)\n");
            }
            W("      (drop)  ;; drop magic sentinel\n");
            W("      (local.set $arr_h)\n");
            /* Evaluate replacement value — coerce to string */
            {
                WasmTy vt = emit_wasm_expr(s->replacement);
                if (vt == TY_INT)   W("      (call $sno_int_to_str)\n");
                if (vt == TY_FLOAT) W("      (call $sno_float_to_str)\n");
            }
            /* Stack: (val_off val_len) — save in locals */
            W("      (local.set $proto_len)  ;; val_len\n");
            W("      (local.set $arr_ok)     ;; val_off\n");
            if (idx2_e) {
                /* 2D array: arr<row,col> = val — sno_array_set2(handle,row,col,vo,vl) */
                WasmTy t1 = idx_e ? emit_wasm_expr(idx_e) : TY_INT;
                if (t1 == TY_STR)   W("      (call $sno_str_to_int)\n");
                if (t1 == TY_FLOAT) W("      (i64.trunc_f64_s)\n");
                W("      (i32.wrap_i64)\n");
                W("      (local.set $tmp_i32)  ;; row\n");
                WasmTy t2 = emit_wasm_expr(idx2_e);
                if (t2 == TY_STR)   W("      (call $sno_str_to_int)\n");
                if (t2 == TY_FLOAT) W("      (i64.trunc_f64_s)\n");
                W("      (i32.wrap_i64)\n");
                W("      (local.set $arr_h2)  ;; save col\n");
                /* sno_array_set2(handle, row, col, val_off, val_len) */
                W("      (local.get $arr_h)\n");
                W("      (local.get $tmp_i32)  ;; row\n");
                W("      (local.get $arr_h2)   ;; col\n");
                W("      (local.get $arr_ok)   ;; val_off\n");
                W("      (local.get $proto_len) ;; val_len\n");
                W("      (call $sno_array_set2)\n");
                W("      (local.set $ok)  ;; 1=ok, 0=OOB\n");
            } else {
                /* 1D array or table — check handle type */
                W("      (local.set $tmp_i32 (call $sno_handle_type (local.get $arr_h)))\n");
                W("      (block $idxa_done\n");
                W("        (if (i32.eq (local.get $tmp_i32) (i32.const 2)) (then\n");
                /* TABLE: key as string — save to locals, then push handle, key, val */
                if (idx_e) {
                    WasmTy kt = emit_wasm_expr(idx_e);
                    if (kt == TY_INT)   W("          (call $sno_int_to_str)\n");
                    if (kt == TY_FLOAT) W("          (call $sno_float_to_str)\n");
                } else {
                    int ei = strlit_intern("");
                    W("          (i32.const %d) (i32.const 0)\n", strlit_abs(ei));
                }
                W("          (local.set $tmp_i32)  ;; key_len\n");
                W("          (local.set $arr_h2)   ;; key_off\n");
                /* sno_table_set(handle, key_off, key_len, val_off, val_len) -> void */
                W("          (local.get $arr_h)\n");
                W("          (local.get $arr_h2) (local.get $tmp_i32)\n");
                W("          (local.get $arr_ok) (local.get $proto_len)\n");
                W("          (call $sno_table_set)\n");
                W("          (local.set $ok (i32.const 1))\n");
                W("          (br $idxa_done)))\n");
                /* ARRAY: key as i32 index — sno_array_set(handle, idx, val_off, val_len) */
                if (idx_e) {
                    WasmTy it = emit_wasm_expr(idx_e);
                    if (it == TY_STR)   W("        (call $sno_str_to_int)\n");
                    if (it == TY_FLOAT) W("        (i64.trunc_f64_s)\n");
                    W("        (i32.wrap_i64)\n");
                } else {
                    W("        (i32.const 1)\n");
                }
                W("        (local.set $tmp_i32)  ;; save idx\n");
                W("        (local.get $arr_h)\n");
                W("        (local.get $tmp_i32)  ;; idx\n");
                W("        (local.get $arr_ok) (local.get $proto_len)\n");
                W("        (call $sno_array_set)\n");
                W("        (local.set $ok)  ;; 1=ok, 0=OOB\n");
                W("      ) ;; block $idxa_done\n");
            }
            } /* end field-setter else */
        } else if (has_subject && s->pattern && s->pattern->kind != E_NUL) {
            /* Pattern-match statement: subject pat  :s/:f
               Use cursor-based emit_pattern_node() so SEQ can chain. */
            W("      ;; pattern match: subject ? pattern (cursor-based)\n");
            /* Evaluate subject — coerce to string, save in pat_subj locals */
            WasmTy ts = emit_wasm_expr(s->subject);
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

void emit_wasm_runtime_imports_sno_base(FILE *out, int npages, const char *page_comment) {
    fprintf(out, "  ;; Memory imported from runtime module\n");
    fprintf(out, "  (import \"sno\" \"memory\" (memory %d))  ;; %s\n", npages, page_comment ? page_comment : "");
    fprintf(out, "  ;; Runtime function imports (base set — shared by SNOBOL4 and Icon)\n");
    fprintf(out, "  (import \"sno\" \"sno_output_str\"   (func $sno_output_str   (param i32 i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_output_int\"   (func $sno_output_int   (param i64)))\n");
    fprintf(out, "  (import \"sno\" \"sno_output_flush\" (func $sno_output_flush (result i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_str_alloc\"    (func $sno_str_alloc    (param i32) (result i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_str_concat\"   (func $sno_str_concat   (param i32 i32 i32 i32) (result i32 i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_str_eq\"       (func $sno_str_eq       (param i32 i32 i32 i32) (result i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_str_to_int\"   (func $sno_str_to_int   (param i32 i32) (result i64)))\n");
    fprintf(out, "  (import \"sno\" \"sno_int_to_str\"   (func $sno_int_to_str   (param i64) (result i32 i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_float_to_str\" (func $sno_float_to_str (param f64) (result i32 i32)))\n");
    fprintf(out, "  (import \"sno\" \"sno_pow\"          (func $sno_pow          (param f64 f64) (result f64)))\n");
}

/* ── Public entry point ───────────────────────────────────────────────────── */
void emit_wasm(Program *prog, FILE *out, const char *filename) {
    (void)filename;
    wasm_out = out;

    for (int i = 0; i < str_nlit; i++) free(str_lits[i].text);
    str_nlit = str_bytes = 0;
    var_table_reset();
    needs_indr = 0;
    for (int i = 0; i < data_ntype; i++) {
        free(data_types[i].name);
        for (int j = 0; j < data_types[i].nfields; j++) free(data_types[i].field_name[j]);
    }
    data_ntype = 0;

    collect_labels(prog);
    prescan_prog(prog);

    W(";; Generated by scrip-cc -wasm (M-SW-A03)\n");
    W("(module\n");

    emit_runtime_imports();
    emit_data_segment();
    emit_var_globals();
    emit_var_indirect_funcs();
    emit_data_init_func();

    W("\n  (func (export \"main\") (result i32)\n");
    emit_main_body(prog);
    W("    (call $sno_output_flush)\n");
    W("  )\n");
    W(")\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Icon × WASM emitter — formerly emit_wasm_icon.c
 * ═══════════════════════════════════════════════════════════════════════════ */
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
 *   ICN_TO    → E_TO, ICN_ALT → E_ALTERNATE
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

    WI("  ;; E_ALTERNATE  (node %d, branch-slot %d @ 0x%x)\n", id, slot, slot_addr);
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
        /* e2_is_gen: if e2 is a generator (E_TO, E_ALTERNATE etc.) the exhaust handler
         * should try e2_resume to get a new upper bound. For literals/vars, just fail. */
        int e2_is_gen = (n->children[1]->kind == E_TO   ||
                         n->children[1]->kind == E_ALTERNATE ||
                         n->children[1]->kind == E_EVERY);
        emit_icn_to(n, id, succ, fail,
                    e1_start, e1_resume, e2_start, e2_resume,
                    e1_id, e2_id, e2_is_gen);
        break;
    }

    /* ── Value alternation: E1 | E2 (E_ALTERNATE) ──────────────────────────── */
    case E_ALTERNATE: {
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Prolog × WASM emitter — formerly emit_wasm_prolog.c
 * ═══════════════════════════════════════════════════════════════════════════ */
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
 *   Runtime imports from "pl" namespace (prolog_runtime.wat).
 *
 * Memory layout used by this emitter:
 *   [0..8191]       output buffer (prolog_runtime.wat)
 *   [8192..32767]   atom table: atom_id*8 → {i32 str_off, i32 str_len}
 *                   (emitted as (data) block by emit_pl_atom_table())
 *   [32768..49151]  variable env frames: slot_addr = 32768 + env_idx*64 + slot*4
 *   [49152..57343]  trail stack (prolog_runtime.wat)
 *   [57344..131071] term heap (prolog_runtime.wat)
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
    W(";; Prolog WASM runtime imports (prolog_runtime.wat)\n");
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
                    /* Update CP frame arg[ai] to the tail so GT loop retry
                     * passes the reduced list, not the original full list. */
                    W("      (call $pl_cp_set_arg (i32.const %d) (i32.load (i32.const %d)))\n", ai, addr);
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
    case E_MUL:
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
    case E_MNS:
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
                /* PW-14 fix: pass slot ADDRESS not loaded value.
                 * Alpha checks: if (a >= ENV_BASE) store(a, atom_id) else compare.
                 * Was i32.load(addr) -> passed value -> OOB on cons-cell pointer. */
                W("    (i32.const %d) ;; slot addr _V%d\n", addr, (int)arg->ival);
            } else {
                emit_term_value(arg, env_idx);
            }
        }
        W("    (local.get $gamma_idx) (local.get $omega_idx)\n");
        W("    (return_call $pl_%s_alpha)\n", mangled + 3);
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

                /* Poll flag: if γ fired (=1) loop advances CP ci then retries */
                W("      (if (i32.load (i32.const %d)) (then\n", PL_GT_FLAG);
                W("        (call $pl_cp_set_ci\n");
                W("          (i32.add (call $pl_cp_get_ci) (i32.const 1)))\n");
                W("        (br $gt_%d)\n", site_id);
                W("      ))\n");
                W("    ) ;; $gt_%d\n", site_id);
                W("    (call $pl_cp_pop) ;; discard CP frame\n");

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
        int is_gamma = (strstr(name, "_gamma_") != NULL);

        W("  (func $%s (param $trail i32) (result i32)\n", name);

        if (is_gamma) {
            /* Locals needed by emit_write_var and emit_write_atom_lit helpers */
            W("    (local $tmp i32)\n");
            W("    (local $tbl_entry i32)\n");
            /* Find the GTSiteData for this γ */
            int site_id = -1;
            char site_str[16];
            /* name = "pl_gt_gamma_N" — extract N */
            const char *p = strrchr(name, '_');
            if (p) site_id = atoi(p + 1);

            GTSiteData *sd = NULL;
            for (int si = 0; si < gt_site_total; si++) {
                if (gt_site_data[si].site_id == site_id) { sd = &gt_site_data[si]; break; }
            }
            (void)site_str;

            if (sd) {
                W("    ;; γ for GT site %d (%s/%d): advance CP ci, run body goals, return 0\n",
                  site_id, sd->mangled, sd->arity);


                /* Signal main's loop: a solution was found */
                W("    (i32.store (i32.const %d) (i32.const 1)) ;; GT flag=1 (γ fired)\n",
                  PL_GT_FLAG);

                /* Emit body goals (write(X), nl, etc.) */
                for (int gi = 0; gi < sd->n_body_goals; gi++)
                    emit_goal(sd->body_goals[gi], sd->env_idx, 0);

                /* Unwind trail — undo this clause's bindings for next retry.
                 * Use CP trail_mark (snapshot at cp_push time) for correctness. */
                W("    (call $trail_unwind (call $pl_cp_get_trail_mark))\n");

                /* Reset var slots so _call can rebind on next solution */
                for (int ai = 0; ai < sd->arity; ai++) {
                    if (sd->arg_slots[ai] >= 0)
                        W("    (i32.store (i32.const %d) (i32.const 0)) ;; reset V%d\n",
                          sd->arg_slots[ai], ai);
                }

                /* Return 0 — main's (loop) polls flag and calls _call again */
                W("    (i32.const 0)\n");
            } else {
                /* Fallback: should not happen */
                W("    (i32.const 0) ;; γ stub (no site data)\n");
            }
        } else {
            /* ω: all solutions exhausted — pop CP frame, clear GT flag, return 0 */
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
