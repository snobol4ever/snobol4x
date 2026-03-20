#ifndef SNO2C_H
#define SNO2C_H
/*
 * sno2c.h — IR for the sno2c SNOBOL4→C compiler
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

/* ---- expression node kinds ---- */
typedef enum {
    /* literals */
    E_QLIT, E_ILIT, E_FLIT, E_NULV,
    /* references */
    E_VART,          /* plain variable */
    E_KW,            /* &IDENT */
    E_INDR,          /* $expr  — indirect / immediate-assign-target */
    /* arithmetic */
    E_MNS,
    E_ADD, E_SUB, E_MPY, E_DIV, E_EXPOP,
    /* string / pattern composition */
    E_CONC,          /* juxtaposition: value/pattern CONCAT_fn  (n-ary) */
    E_OPSYN,         /* & operator: reduce(left, right) */
    E_OR,            /* | : pattern alternation  (n-ary) */
    /* captures (pattern context only) */
    E_NAM,           /* expr . var  — conditional assignment */
    E_DOL,           /* expr $ var  — immediate assignment */
    /* calls */
    E_FNC,           /* f(args)  — children[0..nchildren-1] are args */
    E_ARY,           /* a[subs]  — named array subscript */
    E_IDX,           /* expr[subs] — postfix: children[0]=expr, children[1..]=indices */
    E_ATP,           /* @var — cursor position capture */
    E_ASGN,          /* var = expr inside expression context */
} EKind;

/*
 * EXPR_t — unified n-ary expression node.
 *
 * All structural children live in the `children` array (realloc-grown via
 * expr_add_child).  Use the named accessor macros — never index children[]
 * directly in backends; the macros give NULL-safe bounds-checked access.
 *
 * Layout by kind:
 *   leaves  (E_QLIT/E_ILIT/E_FLIT/E_NULV/E_VART/E_KW)   nchildren=0
 *   unary   (E_MNS/E_ATP/E_INDR/...)                     nchildren=1
 *   binary  (E_ADD/E_SUB/E_MPY/E_DIV/E_EXPOP/E_OPSYN/
 *            E_ASGN/E_NAM/E_DOL/E_ARY)                   nchildren=2
 *   n-ary   (E_CONC / E_OR)                               nchildren>=0
 *   call    (E_FNC)                                       nchildren=nargs
 *   subscript (E_IDX)                                     children[0]=base, children[1..]=indices
 *
 * Named accessors (NULL-safe):
 *   expr_left(e)    — children[0]
 *   expr_right(e)   — children[1]
 *   expr_arg(e, i)  — children[i]
 *   expr_nargs(e)   — nchildren
 */
typedef struct EXPR_t EXPR_t;
struct EXPR_t {
    EKind    kind;
    char    *sval;        /* E_QLIT text, E_VART/E_KW/E_FNC/E_ARY name */
    long     ival;        /* E_ILIT */
    double   dval;        /* E_FLIT */
    EXPR_t **children;    /* realloc-grown child array */
    int      nchildren;
};

/* NULL-safe named accessors */
#define expr_left(e)     ((e) && (e)->nchildren >= 1 ? (e)->children[0] : NULL)
#define expr_right(e)    ((e) && (e)->nchildren >= 2 ? (e)->children[1] : NULL)
#define expr_arg(e, i)   ((e) && (i) >= 0 && (i) < (e)->nchildren ? (e)->children[(i)] : NULL)
#define expr_nargs(e)    ((e) ? (e)->nchildren : 0)

/* ---- goto ---- */
typedef struct {
    char *onsuccess;
    char *onfailure;
    char *uncond;
    char *computed_success_expr;
    char *computed_failure_expr;
    char *computed_uncond_expr;
} SnoGoto;

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
    STMT_t  *next;
};

/* ---- program ---- */
typedef struct {
    STMT_t *head;
    STMT_t *tail;
    int     nstmts;
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
void     snoc_add_include_dir(const char *d);
Program *snoc_parse(FILE *f, const char *filename);
EXPR_t  *parse_expr_from_str(const char *src);
void     c_emit(Program *prog, FILE *out);

/* ---- Byrd box emitter (emit_byrd.c) ---- */
void byrd_fn_scope_reset(void);
void byrd_named_pat_reset(void);
void byrd_preregister_named_pattern(const char *varname);
void byrd_emit_named_typedecls(FILE *out_file);
void byrd_emit_named_fwdecls(FILE *out_file);
void byrd_emit_pattern(EXPR_t *pat, FILE *out_file,
                       const char *root_name,
                       const char *subject_var,
                       const char *subj_len_var,
                       const char *cursor_var,
                       const char *gamma_label,
                       const char *omega_label);
void byrd_emit_standalone(EXPR_t *pat, FILE *out_file,
                          const char *subject,
                          const char *root_name);
void byrd_emit_named_pattern(const char *varname, EXPR_t *pat, FILE *out_file);
void byrd_cond_reset(void);
void byrd_cond_emit_assigns(FILE *out_file, int stmt_u);

/* ---- error ---- */
void snoc_error(int lineno, const char *fmt, ...);
extern int   snoc_nerrors;
extern char *yyfilename;
extern int   lineno_stmt;

#endif
