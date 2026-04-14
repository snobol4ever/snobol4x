#ifndef SCRIP_CC_H
#define SCRIP_CC_H
/*
 * scrip_cc.h — IR for the scrip-cc SNOBOL4→C compiler
 *
 * ONE expression type.  The emitter decides whether to call
 * pat_* or * based on emission context (subject vs pattern field).
 *
 * Statement structure:
 *   [label]  subject  [pattern]  [= replacement]  [: goto]
 *
 * subject, pattern, replacement are all EXPR_t*.
 * The emitter receives context when walking each field.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- expression node kinds — from shared IR ---- */
/*
 * M-G1-IR-HEADER-WIRE: EKind is now defined in ir/ir.h (the single source
 * of truth for all canonical node kinds).
 *
 * M-G3-ALIAS-CLEANUP: IR_COMPAT_ALIASES section removed — dead code,
 * never enabled. All code uses canonical EKind names directly:
 * E_VAR, E_ALT, E_MNS, E_POW, E_CAPT_COND_ASGN, E_CAPT_IMMED_ASGN,
 * E_CAPT_CURSOR, E_NUL, E_ASSIGN, E_SCAN, E_ITERATE, E_ALTERNATE, E_IDX.
 *
 * ir.h defines EXPR_t (with fval, nalloc, id) when included first.
 * scrip_cc.h defines a compatible subset when included standalone.
 * Both are guarded by EXPR_T_DEFINED to prevent double-definition.
 */
#include "ir/ir.h"

/*
 * EXPR_t — unified n-ary expression node.
 *
 * All structural children live in the `children` array (realloc-grown via
 * expr_add_child).  Use the named accessor macros — never index children[]
 * directly in backends; the macros give NULL-safe bounds-checked access.
 *
 * Layout by kind:
 *   leaves  (E_QLIT/E_ILIT/E_FLIT/E_NUL/E_VAR/E_KEYWORD)     nchildren=0
 *   unary   (E_MNS/E_CAPT_CURSOR/E_INDIRECT/...)                nchildren=1
 *   binary  (E_ADD/E_SUB/E_MUL/E_DIV/E_POW/E_OPSYN/
 *            E_ASSIGN/E_CAPT_COND_ASGN/E_CAPT_IMMED_ASGN/E_IDX)      nchildren=2
 *   n-ary   (E_SEQ / E_CAT / E_ALT)                   nchildren>=0
 *   call    (E_FNC)                                       nchildren=nargs
 *   subscript (E_IDX)                                     children[0]=base, children[1..]=indices
 *
 * Named accessors (NULL-safe):
 *   expr_left(e)    — children[0]
 *   expr_right(e)   — children[1]
 *   expr_arg(e, i)  — children[i]
 *   expr_nargs(e)   — nchildren
 */
#ifndef EXPR_T_DEFINED
#define EXPR_T_DEFINED
typedef struct EXPR_t EXPR_t;
struct EXPR_t {
    EKind    kind;
    char    *sval;        /* E_QLIT text, E_VAR/E_KEYWORD/E_FNC/E_IDX name */
    long     ival;        /* E_ILIT */
    double   dval;        /* E_FLIT */
    EXPR_t **children;    /* realloc-grown child array */
    int      nchildren;
};
#else
/* ir.h was included first; its EXPR_t uses fval (not dval).
 * Code that references e->dval on ir.h's EXPR_t must use e->fval directly.
 * The #define below is intentionally omitted to avoid polluting other structs. */
#endif /* EXPR_T_DEFINED */

/* NULL-safe named accessors */
#define expr_left(e)     ((e) && (e)->nchildren >= 1 ? (e)->children[0] : NULL)
#define expr_right(e)    ((e) && (e)->nchildren >= 2 ? (e)->children[1] : NULL)
#define expr_arg(e, i)   ((e) && (i) >= 0 && (i) < (e)->nchildren ? (e)->children[(i)] : NULL)
#define expr_nargs(e)    ((e) ? (e)->nchildren : 0)

/* ---- goto ---- */
typedef struct {
    char   *onsuccess;
    char   *onfailure;
    char   *uncond;
    EXPR_t *computed_success_expr;
    EXPR_t *computed_failure_expr;
    EXPR_t *computed_uncond_expr;
} SnoGoto;

/* Forward declaration so STMT_t can reference EXPR_t* before the full
 * struct definition (which appears below, guarded by EXPR_T_DEFINED). */
#ifndef EXPR_T_DEFINED
typedef struct EXPR_t EXPR_t;
#endif

/* ---- source language tags (U-12) ---- */
#define LANG_SNO  0   /* SNOBOL4 */
#define LANG_ICN  1   /* Icon    */
#define LANG_PL   2   /* Prolog  */
#define LANG_RAKU 3   /* Raku    */

/* ---- statement ---- */
typedef struct STMT_t STMT_t;
struct STMT_t {
    char    *label;
    EXPR_t  *subject;
    EXPR_t  *pattern;
    EXPR_t  *replacement;
    SnoGoto *go;
    int      lineno;
    int      is_end;
    int      has_eq;
    int      lang;    /* LANG_SNO / LANG_ICN / LANG_PL  (U-12) */
    STMT_t  *next;
};

/* ---- program ---- */
/* ---- EXPORT / IMPORT lists (linker sprint LP-4) ---- */

typedef struct ExportEntry {
    char              *name;   /* exported symbol name, e.g. "WORDCOUNT" */
    struct ExportEntry *next;
} ExportEntry;

typedef struct ImportEntry {
    char              *lang;   /* source language prefix, e.g. "SNOBOL4"  */
    char              *name;   /* assembly base name,     e.g. "Greet_lib" */
    char              *method; /* exported method name,   e.g. "GREET"     */
    struct ImportEntry *next;
} ImportEntry;

typedef struct {
    STMT_t      *head;
    STMT_t      *tail;
    int          nstmts;
    ExportEntry *exports;   /* singly-linked list of EXPORT directives */
    ImportEntry *imports;   /* singly-linked list of IMPORT directives */
} Program;

/* ---- allocators ---- */
static inline EXPR_t *expr_new(EKind k) {
    EXPR_t *e = calloc(1, sizeof *e); e->kind = k; return e;
}
static inline SnoGoto *sgoto_new(void) { return calloc(1, sizeof(SnoGoto)); }
static inline STMT_t  *stmt_new(void)  { return calloc(1, sizeof(STMT_t)); }

/* Append one child — the only way to grow a node's children array. */
static inline void expr_add_child(EXPR_t *e, EXPR_t *child) {
    e->children = realloc(e->children,
                          (size_t)(e->nchildren + 1) * sizeof(EXPR_t *));
    e->children[e->nchildren++] = child;
}

/* Convenience: build a unary node (one child). */
static inline EXPR_t *expr_unary(EKind k, EXPR_t *operand) {
    EXPR_t *e = expr_new(k);
    expr_add_child(e, operand);
    return e;
}

/* Convenience: build a binary node (two children). */
static inline EXPR_t *expr_binary(EKind k, EXPR_t *left, EXPR_t *right) {
    EXPR_t *e = expr_new(k);
    expr_add_child(e, left);
    expr_add_child(e, right);
    return e;
}

/* ---- string helpers ---- */
static inline char *intern(const char *s) { return s ? strdup(s) : NULL; }
static inline char *intern_n(const char *s, int n) {
    char *p = malloc(n+1); memcpy(p,s,n); p[n]='\0'; return p;
}

/* ---- public API ---- */
void     sno_add_include_dir(const char *d);
void     sno_reset(void);          /* reset per-file state between multi-file compilations */
Program *sno_parse(FILE *f, const char *filename);
EXPR_t  *parse_expr_from_str(const char *src);
EXPR_t  *parse_expr_pat_from_str(const char *src); /* bison: bare expr -> EXPR_t, pattern slot */
Program *sno_parse_string(const char *src);         /* bison: multi-stmt string -> Program* */
void     c_emit(Program *prog, FILE *out);

/* emit_byrd.c interface now internal to emit_byrd_c.c */

/* ---- error ---- */
void sno_error(int lineno, const char *fmt, ...);
extern int   sno_nerrors;
extern char *yyfilename;
extern int   lineno_stmt;

#endif
