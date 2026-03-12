#ifndef SNOC_H
#define SNOC_H
/*
 * snoc.h — IR for the snoc SNOBOL4→C compiler
 *
 * ONE expression type.  The emitter decides whether to call
 * sno_pat_* or sno_* based on emission context (subject vs pattern field).
 *
 * Statement structure:
 *   [label]  subject  [pattern]  [= replacement]  [: goto]
 *
 * subject, pattern, replacement are all Expr*.
 * The emitter receives context when walking each field.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- expression node kinds ---- */
typedef enum {
    /* literals */
    E_STR, E_INT, E_REAL, E_NULL,
    /* references */
    E_VAR,          /* plain variable */
    E_KEYWORD,      /* &IDENT */
    E_DEREF,        /* $expr  — indirect / immediate-assign-target */
    /* arithmetic */
    E_NEG,
    E_ADD, E_SUB, E_MUL, E_DIV, E_POW,
    /* string / pattern composition */
    E_CONCAT,       /* juxtaposition or &: both value and pattern concat */
    E_ALT,          /* | : pattern alternation */
    /* captures (pattern context only) */
    E_COND,         /* expr . var  — conditional assignment */
    E_IMM,          /* expr $ var  — immediate assignment */
    /* calls */
    E_CALL,         /* f(args) */
    E_ARRAY,        /* a[subs] — named array subscript */
    E_INDEX,        /* expr[subs] — postfix subscript on any expression */
    E_AT,           /* @var — cursor position capture */
    E_ASSIGN,       /* var = expr inside expression context (DIFFER(x = f())) */
} EKind;

typedef struct Expr Expr;
struct Expr {
    EKind  kind;
    char  *sval;        /* E_STR text, E_VAR/E_KEYWORD/E_CALL/E_ARRAY name */
    long   ival;        /* E_INT */
    double dval;        /* E_REAL */
    Expr  *left;        /* unary child, binary left */
    Expr  *right;       /* binary right, capture var (E_COND/E_IMM) */
    Expr **args;        /* E_CALL / E_ARRAY arguments */
    int    nargs;
};

/* ---- goto ---- */
typedef struct {
    char *onsuccess;
    char *onfailure;
    char *uncond;
} SnoGoto;

/* ---- statement ---- */
typedef struct Stmt Stmt;
struct Stmt {
    char    *label;
    Expr    *subject;
    Expr    *pattern;      /* NULL if no pattern field */
    Expr    *replacement;  /* NULL if no = field */
    SnoGoto *go;           /* NULL if no : field */
    int      lineno;
    int      is_end;       /* 1 if this is the END statement */
    Stmt    *next;
};

/* ---- program ---- */
typedef struct {
    Stmt *head;
    Stmt *tail;
    int   nstmts;
} Program;

/* ---- allocators ---- */
static inline Expr *expr_new(EKind k) {
    Expr *e = calloc(1, sizeof *e); e->kind = k; return e;
}
static inline SnoGoto *sgoto_new(void) { return calloc(1, sizeof(SnoGoto)); }
static inline Stmt    *stmt_new(void)  { return calloc(1, sizeof(Stmt)); }

/* ---- string helpers ---- */
static inline char *intern(const char *s) { return s ? strdup(s) : NULL; }
static inline char *intern_n(const char *s, int n) {
    char *p = malloc(n+1); memcpy(p,s,n); p[n]='\0'; return p;
}

/* ---- public API ---- */
void     snoc_add_include_dir(const char *d);
Program *snoc_parse(FILE *f, const char *filename);
void     snoc_emit(Program *prog, FILE *out);

/* ---- error ---- */
void snoc_error(int lineno, const char *fmt, ...);
extern int   snoc_nerrors;
extern char *yyfilename;
extern int   lineno_stmt;

#endif
