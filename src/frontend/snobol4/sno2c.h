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
    E_KW,      /* &IDENT */
    E_INDR,        /* $expr  — indirect / immediate-assign-target */
    /* arithmetic */
    E_MNS,
    E_ADD, E_SUB, E_MPY, E_DIV, E_EXPOP,
    /* string / pattern composition */
    E_CONC,       /* juxtaposition: value/pattern CONCAT_fn */
    E_OPSYN,       /* & operator: reduce(left, right) — OPSYN('&','reduce',2) */
    E_OR,          /* | : pattern alternation */
    /* captures (pattern context only) */
    E_NAM,         /* expr . var  — conditional assignment */
    E_DOL,          /* expr $ var  — immediate assignment */
    /* calls */
    E_FNC,         /* f(args) */
    E_ARY,        /* a[subs] — named array subscript */
    E_IDX,        /* expr[subs] — postfix subscript on any expression */
    E_ATP,           /* @var — cursor position capture */
    E_ASGN,       /* var = expr inside expression context (DIFFER(x = f())) */
} EKind;

typedef struct EXPR_t EXPR_t;
struct EXPR_t {
    EKind  kind;
    char  *sval;        /* E_QLIT text, E_VART/E_KW/E_FNC/E_ARY name */
    long   ival;        /* E_ILIT */
    double dval;        /* E_FLIT */
    EXPR_t  *left;        /* unary child, binary left */
    EXPR_t  *right;       /* binary right, capture var (E_NAM/E_DOL) */
    EXPR_t **args;        /* E_FNC / E_ARY arguments */
    int    nargs;
};

/* ---- goto ---- */
typedef struct {
    char *onsuccess;
    char *onfailure;
    char *uncond;
    /* For computed gotos $(expr): the expression text (NULL if not computed) */
    char *computed_success_expr;
    char *computed_failure_expr;
    char *computed_uncond_expr;
} SnoGoto;

/* ---- statement ---- */
typedef struct STMT_t STMT_t;
struct STMT_t {
    char    *label;
    EXPR_t    *subject;
    EXPR_t    *pattern;      /* NULL if no pattern field */
    EXPR_t    *replacement;  /* NULL if no = field */
    SnoGoto *go;           /* NULL if no : field */
    int      lineno;
    int      is_end;       /* 1 if this is the END statement */
    int      has_eq;       /* 1 if = was present (even with empty RHS) */
    STMT_t    *next;
};

/* ---- program ---- */
typedef struct {
    STMT_t *head;
    STMT_t *tail;
    int   nstmts;
} Program;

/* ---- allocators ---- */
static inline EXPR_t *expr_new(EKind k) {
    EXPR_t *e = calloc(1, sizeof *e); e->kind = k; return e;
}
static inline SnoGoto *sgoto_new(void) { return calloc(1, sizeof(SnoGoto)); }
static inline STMT_t    *stmt_new(void)  { return calloc(1, sizeof(STMT_t)); }

/* ---- string helpers ---- */
static inline char *intern(const char *s) { return s ? strdup(s) : NULL; }
static inline char *intern_n(const char *s, int n) {
    char *p = malloc(n+1); memcpy(p,s,n); p[n]='\0'; return p;
}

/* ---- public API ---- */
void     snoc_add_include_dir(const char *d);
Program *snoc_parse(FILE *f, const char *filename);
EXPR_t    *parse_expr_from_str(const char *src); /* for computed goto dispatch */
void     snoc_emit(Program *prog, FILE *out);

/* ---- Byrd box emitter (emit_byrd.c) ---- */
void byrd_fn_scope_reset(void);   /* call at start of each emitted C function */
void byrd_named_pat_reset(void);  /* clear registry between programs */
void byrd_preregister_named_pattern(const char *varname); /* forward-register name */
void byrd_emit_named_typedecls(FILE *out_file); /* emit struct typedef fwd-decls */
void byrd_emit_named_fwdecls(FILE *out_file); /* emit all forward decls at once */
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
