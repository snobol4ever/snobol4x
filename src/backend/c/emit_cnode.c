/* emit_cnode.c — CNODE_t IR build + measure + print phases */

#include "sno2c.h"
#include "emit_cnode.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>

/* =====================================================================
 * Arena allocator
 * ===================================================================== */

CArena *cn_arena_new(size_t cap) {
    CArena *a = malloc(sizeof(CArena));
    a->buf  = malloc(cap);
    a->cap  = cap;
    a->used = 0;
    return a;
}

void *cn_arena_alloc(CArena *a, size_t sz) {
    /* Align to pointer size */
    sz = (sz + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
    if (a->used + sz > a->cap) {
        /* Grow — double capacity */
        a->cap = (a->cap + sz) * 2;
        a->buf = realloc(a->buf, a->cap);
    }
    void *p = a->buf + a->used;
    a->used += sz;
    return p;
}

char *cn_arena_strdup(CArena *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = cn_arena_alloc(a, len);
    memcpy(p, s, len);
    return p;
}

void cn_arena_free(CArena *a) {
    if (!a) return;
    free(a->buf);
    free(a);
}

/* =====================================================================
 * CNODE_t constructors
 * ===================================================================== */

CNODE_t *cn_raw(CArena *a, const char *text) {
    CNODE_t *n = cn_arena_alloc(a, sizeof(CNODE_t));
    n->kind  = CN_RAW;
    n->text  = cn_arena_strdup(a, text);
    n->args  = NULL; n->nargs = 0;
    n->left  = NULL; n->right = NULL;
    return n;
}

CNODE_t *cn_call(CArena *a, const char *fn, CNODE_t **args, int nargs) {
    CNODE_t *n = cn_arena_alloc(a, sizeof(CNODE_t));
    n->kind  = CN_CALL;
    n->text  = cn_arena_strdup(a, fn);
    n->nargs = nargs;
    if (nargs > 0) {
        n->args = cn_arena_alloc(a, nargs * sizeof(CNODE_t*));
        memcpy(n->args, args, nargs * sizeof(CNODE_t*));
    } else {
        n->args = NULL;
    }
    n->left  = NULL; n->right = NULL;
    return n;
}

CNODE_t *cn_seq(CArena *a, CNODE_t *left, CNODE_t *right) {
    if (!left)  return right;
    if (!right) return left;
    CNODE_t *n = cn_arena_alloc(a, sizeof(CNODE_t));
    n->kind  = CN_SEQ;
    n->text  = NULL;
    n->args  = NULL; n->nargs = 0;
    n->left  = left; n->right = right;
    return n;
}

CNODE_t *cn_call0(CArena *a, const char *fn) {
    return cn_call(a, fn, NULL, 0);
}
CNODE_t *cn_call1(CArena *a, const char *fn, CNODE_t *a0) {
    CNODE_t *args[1] = {a0};
    return cn_call(a, fn, args, 1);
}
CNODE_t *cn_call2(CArena *a, const char *fn, CNODE_t *a0, CNODE_t *a1) {
    CNODE_t *args[2] = {a0, a1};
    return cn_call(a, fn, args, 2);
}
CNODE_t *cn_call3(CArena *a, const char *fn, CNODE_t *a0, CNODE_t *a1, CNODE_t *a2) {
    CNODE_t *args[3] = {a0, a1, a2};
    return cn_call(a, fn, args, 3);
}

/* =====================================================================
 * Build phase helpers
 * ===================================================================== */

/* Build a quoted C string literal node: "text" */
static CNODE_t *cn_cstr(CArena *a, const char *s) {
    /* Reproduce the escaped C string that emit_cstr() would produce */
    char buf[4096]; int j = 0;
    buf[j++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p && j < 4090; p++) {
        if (*p == '"')       { buf[j++]='\\'; buf[j++]='"';  }
        else if (*p == '\\') { buf[j++]='\\'; buf[j++]='\\'; }
        else if (*p == '\n') { buf[j++]='\\'; buf[j++]='n';  }
        else if (*p == '\t') { buf[j++]='\\'; buf[j++]='t';  }
        else                 { buf[j++]=(char)*p; }
    }
    buf[j++] = '"'; buf[j] = '\0';
    return cn_raw(a, buf);
}

/* Build a snprintf'd raw atom */
static CNODE_t *cn_rawf(CArena *a, const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    return cn_raw(a, buf);
}

/* C-safe name for a SNOBOL4 variable (mirrors cs() in emit.c) */
static char cs_buf_cnode[512];
static const char *cs_cn(const char *s) {
    int j = 0; cs_buf_cnode[j++] = '_';
    for (int i = 0; s[i] && j < 510; i++) {
        unsigned char c = (unsigned char)s[i];
        cs_buf_cnode[j++] = (isalnum(c)||c=='_') ? (char)c : '_';
    }
    cs_buf_cnode[j] = '\0';
    return cs_buf_cnode;
}

/* Forward declarations */
static int is_io_name_cn(const char *name);
/* is_defined_function and expr_contains_pattern are exported from emit.c */
int is_defined_function(const char *name);
int expr_contains_pattern(EXPR_t *e);
static int is_defined_function_cn(const char *name) {
    return is_defined_function(name);
}CNODE_t *build_expr(CArena *a, EXPR_t *e);
CNODE_t *build_pat (CArena *a, EXPR_t *e);

static int is_io_name_cn(const char *name) {
    return (strcasecmp(name,"INPUT")==0 || strcasecmp(name,"OUTPUT")==0
         || strcasecmp(name,"PUNCH")==0);
}

/* =====================================================================
 * build_expr — mirrors emit_expr() exactly, returns CNODE_t* instead
 * ===================================================================== */
CNODE_t *build_expr(CArena *a, EXPR_t *e) {
    if (!e) return cn_raw(a, "NULVCL");

    switch (e->kind) {
    case E_NULV:  return cn_raw(a, "NULVCL");

    case E_QLIT:   return cn_call1(a, "STRVAL_fn", cn_cstr(a, e->sval));

    case E_ILIT:   return cn_rawf(a, "INTVAL_fn(%ld)", e->ival);

    case E_FLIT:  return cn_rawf(a, "real(%g)", e->dval);

    case E_VART:
        if (is_io_name_cn(e->sval))
            return cn_rawf(a, "NV_GET_fn(\"%s\")", e->sval);
        return cn_rawf(a, "get(%s)", cs_cn(e->sval));

    case E_KW:
        return cn_rawf(a, "kw(\"%s\")", e->sval);

    case E_INDR:
        if (!e->children[0]) {
            /* $expr — indirect lookup: operand is in e->children[1] */
            return cn_call1(a, "deref", build_expr(a, e->children[1]));
        } else if (e->children[0]->kind == E_VART) {
            /* *varname — deferred pattern reference */
            return cn_rawf(a, "var_as_pattern(pat_ref(\"%s\"))", e->children[0]->sval);
        } else if (e->children[0]->kind == E_FNC && e->children[0]->nchildren >= 1
                   && !is_defined_function_cn(e->children[0]->sval)) {
            /* *varname(arg...) — continuation-line misparse: deref-ref cat arg */
            char buf[256];
            snprintf(buf, sizeof buf, "CONCAT_fn(var_as_pattern(pat_ref(\"%s\")),", e->children[0]->sval);
            return cn_seq(a,
                cn_raw(a, buf),
                cn_seq(a, build_expr(a, e->children[0]->children[0]), cn_raw(a, ")")));
        }
        /* *(expr) — deref of compound expression */
        return cn_call1(a, "deref", build_expr(a, e->children[0]));

    case E_MNS:
        return cn_call1(a, "neg", build_expr(a, e->children[0]));

    case E_CONC: {
        if (e->nchildren == 0) return cn_raw(a, "STRVAL_fn(\"\")");
        if (e->nchildren == 1) return build_expr(a, e->children[0]);
        CNODE_t *_acc = cn_call2(a, "CONCAT_fn", build_expr(a, e->children[0]), build_expr(a, e->children[1]));
        for (int _i = 2; _i < e->nchildren; _i++)
            _acc = cn_call2(a, "CONCAT_fn", _acc, build_expr(a, e->children[_i]));
        return _acc;
    }

    case E_OR: {
        if (e->nchildren == 0) return cn_raw(a, "FAILDESCR");
        if (e->nchildren == 1) return build_expr(a, e->children[0]);
        int _is_pat = 0;
        for (int _i = 0; _i < e->nchildren; _i++) if (expr_contains_pattern(e->children[_i])) { _is_pat = 1; break; }
        CNODE_t *_acc = _is_pat
            ? cn_call2(a, "pat_alt", build_pat(a, e->children[0]), build_pat(a, e->children[1]))
            : cn_call2(a, "alt",     build_expr(a, e->children[0]), build_expr(a, e->children[1]));
        for (int _i = 2; _i < e->nchildren; _i++)
            _acc = _is_pat
                ? cn_call2(a, "pat_alt", _acc, build_pat(a, e->children[_i]))
                : cn_call2(a, "alt",     _acc, build_expr(a, e->children[_i]));
        return _acc;
    }

    case E_OPSYN:
        return cn_seq(a,
            cn_raw(a, "APPLY_fn(\"reduce\",(DESCR_t[]){"),
            cn_seq(a, build_expr(a, e->children[0]),
            cn_seq(a, cn_raw(a, ","),
            cn_seq(a, build_expr(a, e->children[1]),
                   cn_raw(a, "},2)")))));

    case E_ADD: return cn_call2(a, "add",    build_expr(a,e->children[0]), build_expr(a,e->children[1]));
    case E_SUB: return cn_call2(a, "sub",    build_expr(a,e->children[0]), build_expr(a,e->children[1]));
    case E_MPY: return cn_call2(a, "mul",    build_expr(a,e->children[0]), build_expr(a,e->children[1]));
    case E_DIV: return cn_call2(a, "DIVIDE_fn", build_expr(a,e->children[0]), build_expr(a,e->children[1]));
    case E_EXPOP: return cn_call2(a, "POWER_fn",   build_expr(a,e->children[0]), build_expr(a,e->children[1]));

    case E_FNC: {
        if (e->nchildren == 0)
            return cn_rawf(a, "APPLY_fn(\"%s\",NULL,0)", e->sval);
        /* Build args array: (DESCR_t[]){arg0, arg1, ...} */
        CNODE_t *arr_open = cn_rawf(a, "APPLY_fn(\"%s\",(DESCR_t[]){", e->sval);
        CNODE_t *inner = build_expr(a, e->children[0]);
        for (int i = 1; i < e->nchildren; i++)
            inner = cn_seq(a, inner, cn_seq(a, cn_raw(a,","), build_expr(a, e->children[i])));
        char close[32]; snprintf(close, sizeof close, "},%d)", e->nchildren);
        return cn_seq(a, arr_open, cn_seq(a, inner, cn_raw(a, close)));
    }

    case E_ARY: {
        CNODE_t *head = cn_rawf(a, "aref(%s,(DESCR_t[]){", cs_cn(e->sval));
        CNODE_t *inner = build_expr(a, e->children[0]);
        for (int i = 1; i < e->nchildren; i++)
            inner = cn_seq(a, inner, cn_seq(a, cn_raw(a,","), build_expr(a, e->children[i])));
        char close[32]; snprintf(close, sizeof close, "},%d)", e->nchildren);
        return cn_seq(a, head, cn_seq(a, inner, cn_raw(a, close)));
    }

    case E_NAM:
    case E_DOL:
        /* In value context, evaluate child */
        return build_expr(a, e->children[0]);

    case E_IDX: {
        /* children[0]=array expr, children[1..n-1]=subscripts */
        CNODE_t *head = cn_raw(a, "INDEX_fn(");
        CNODE_t *obj  = build_expr(a, e->children[0]);
        /* build subscript list from children[1..] only */
        CNODE_t *idx  = (e->nchildren > 1) ? build_expr(a, e->children[1]) : cn_raw(a, "");
        for (int i = 2; i < e->nchildren; i++)
            idx = cn_seq(a, idx, cn_seq(a, cn_raw(a,","), build_expr(a, e->children[i])));
        char close[32]; snprintf(close, sizeof close, "},%d)", e->nchildren - 1);
        return cn_seq(a, head,
               cn_seq(a, obj,
               cn_seq(a, cn_raw(a, ",(DESCR_t[]){"),
               cn_seq(a, idx,
                      cn_raw(a, close)))));
    }

    case E_ATP:
        return cn_rawf(a, "cursor_get(\"%s\")", e->sval);

    case E_ASGN:
        return cn_seq(a,
            cn_rawf(a, "assign_expr(%s,", cs_cn(e->children[0]->sval)),
            cn_seq(a, build_expr(a, e->children[1]), cn_raw(a, ")")));

    default:
        return cn_rawf(a, "/*unknown-expr-%d*/NULVCL", e->kind);
    }
}

/* =====================================================================
 * build_pat — mirrors emit_pat() exactly
 * ===================================================================== */
CNODE_t *build_pat(CArena *a, EXPR_t *e) {
    if (!e) return cn_raw(a, "pat_epsilon()");

    switch (e->kind) {
    case E_QLIT:  return cn_call1(a, "pat_lit", cn_cstr(a, e->sval));
    case E_VART:  return cn_rawf(a, "pat_var(\"%s\")", e->sval);

    case E_INDR:
        if (e->children[0] && e->children[0]->kind == E_VART)
            return cn_rawf(a, "pat_ref(\"%s\")", e->children[0]->sval);
        if (e->children[0] && e->children[0]->kind == E_FNC && e->children[0]->nchildren >= 1
                && !is_defined_function_cn(e->children[0]->sval)) {
            return cn_seq(a,
                cn_rawf(a, "pat_cat(pat_ref(\"%s\"),", e->children[0]->sval),
                cn_seq(a, build_pat(a, e->children[0]->children[0]), cn_raw(a, ")")));
        }
        return cn_call1(a, "pat_deref",
            build_expr(a, e->children[1] ? e->children[1] : e->children[0]));

    case E_CONC:
        return cn_call2(a, "pat_cat", build_pat(a, e->children[0]), build_pat(a, e->children[1]));

    case E_MPY:
        if (e->children[1] && e->children[1]->kind == E_VART)
            return cn_seq(a,
                cn_raw(a, "pat_cat("),
                cn_seq(a, build_pat(a, e->children[0]),
                cn_seq(a, cn_rawf(a, ",pat_ref(\"%s\"))", e->children[1]->sval),
                       cn_raw(a, ""))));
        return cn_seq(a,
            cn_raw(a, "pat_cat("),
            cn_seq(a, build_pat(a, e->children[0]),
            cn_seq(a, cn_raw(a, ",pat_deref("),
            cn_seq(a, build_expr(a, e->children[1]),
                   cn_raw(a, "))")))));

    case E_OR:
        return cn_call2(a, "pat_alt", build_pat(a, e->children[0]), build_pat(a, e->children[1]));

    case E_FNC: {
        /* Pattern builtins */
        const char *n = e->sval;
        char buf[256];
        /* Zero-arg builtins */
        if (strcasecmp(n,"ARB")==0)   return cn_raw(a, "pat_arb()");
        if (strcasecmp(n,"REM")==0)   return cn_raw(a, "pat_rem()");
        if (strcasecmp(n,"DT_FAIL")==0)  return cn_raw(a, "pat_fail()");
        if (strcasecmp(n,"ABORT")==0) return cn_raw(a, "pat_abort()");
        if (strcasecmp(n,"FENCE")==0 && e->nchildren==0) return cn_raw(a,"pat_fence()");
        if (strcasecmp(n,"FENCE")==0 && e->nchildren>=1)
            return cn_call1(a, "pat_fence_p", build_pat(a, e->children[0]));
        /* Fall through to pat_user_call for everything else */
        if (e->nchildren == 0) {
            snprintf(buf, sizeof buf, "pat_user_call(\"%s\",NULL,0)", n);
            return cn_raw(a, buf);
        }
        CNODE_t *head = cn_rawf(a, "pat_user_call(\"%s\",(DESCR_t[]){", n);
        CNODE_t *inner = build_expr(a, e->children[0]);
        for (int i = 1; i < e->nchildren; i++)
            inner = cn_seq(a, inner, cn_seq(a, cn_raw(a,","), build_expr(a, e->children[i])));
        snprintf(buf, sizeof buf, "},%d)", e->nchildren);
        return cn_seq(a, head, cn_seq(a, inner, cn_raw(a, buf)));
    }

    default:
        /* Value expression used as pattern — wrap */
        return cn_call1(a, "pat_val", build_expr(a, e));
    }
}

/* =====================================================================
 * cn_flat_print — flat printer for sprint 1 validation
 * Produces exactly the same output as the old emit_expr/emit_pat.
 * ===================================================================== */
void cn_flat_print(CNODE_t *n, FILE *fp) {
    if (!n) return;
    switch (n->kind) {
    case CN_RAW:
        fputs(n->text ? n->text : "", fp);
        break;
    case CN_CALL:
        fputs(n->text, fp); fputc('(', fp);
        for (int i = 0; i < n->nargs; i++) {
            if (i) fputc(',', fp);
            cn_flat_print(n->args[i], fp);
        }
        fputc(')', fp);
        break;
    case CN_SEQ:
        cn_flat_print(n->left, fp);
        cn_flat_print(n->right, fp);
        break;
    }
}

/* =====================================================================
 * cn_flat_width — "qq" lookahead
 * Returns flat char width, or INT_MAX if > limit (early exit).
 * ===================================================================== */
int cn_flat_width(CNODE_t *n, int limit) {
    if (!n) return 0;
    if (limit <= 0) return INT_MAX;
    switch (n->kind) {
    case CN_RAW: {
        int w = n->text ? (int)strlen(n->text) : 0;
        return (w > limit) ? INT_MAX : w;
    }
    case CN_CALL: {
        int w = (int)strlen(n->text) + 1; /* "fn(" */
        if (w > limit) return INT_MAX;
        for (int i = 0; i < n->nargs; i++) {
            if (i) { w++; if (w > limit) return INT_MAX; } /* comma */
            int aw = cn_flat_width(n->args[i], limit - w);
            if (aw == INT_MAX) return INT_MAX;
            w += aw; if (w > limit) return INT_MAX;
        }
        w++; /* closing ) */
        return (w > limit) ? INT_MAX : w;
    }
    case CN_SEQ: {
        int lw = cn_flat_width(n->left, limit);
        if (lw == INT_MAX) return INT_MAX;
        int rw = cn_flat_width(n->right, limit - lw);
        if (rw == INT_MAX) return INT_MAX;
        int w = lw + rw;
        return (w > limit) ? INT_MAX : w;
    }
    }
    return 0;
}

/* =====================================================================
 * pp_cnode — "pp" pretty-printer
 * col:    current output column
 * indent: spaces to add per wrap level (4)
 * maxcol: line width budget (120)
 * Returns: column after last character written.
 * ===================================================================== */
int pp_cnode(CNODE_t *n, FILE *fp, int col, int indent, int maxcol) {
    if (!n) return col;
    switch (n->kind) {
    case CN_RAW: {
        const char *t = n->text ? n->text : "";
        fputs(t, fp);
        return col + (int)strlen(t);
    }
    case CN_CALL: {
        int w = cn_flat_width(n, maxcol - col);
        if (w != INT_MAX) {
            /* Fits on current line — emit flat */
            fputs(n->text, fp); fputc('(', fp);
            int c = col + (int)strlen(n->text) + 1;
            for (int i = 0; i < n->nargs; i++) {
                if (i) { fputc(',', fp); c++; }
                c = pp_cnode(n->args[i], fp, c, indent, maxcol);
            }
            fputc(')', fp);
            return c + 1;
        }
        /* Doesn't fit — emit multiline */
        int arg_col = col + indent;
        fputs(n->text, fp); fputs("(\n", fp);
        for (int i = 0; i < n->nargs; i++) {
            /* Indent */
            for (int s = 0; s < arg_col; s++) fputc(' ', fp);
            int c = pp_cnode(n->args[i], fp, arg_col, indent, maxcol);
            if (i < n->nargs - 1) { fputc(',', fp); c++; }
            fputc('\n', fp);
            (void)c;
        }
        for (int s = 0; s < col; s++) fputc(' ', fp);
        fputc(')', fp);
        return col + 1;
    }
    case CN_SEQ:
        col = pp_cnode(n->left,  fp, col, indent, maxcol);
        col = pp_cnode(n->right, fp, col, indent, maxcol);
        return col;
    }
    return col;
}


