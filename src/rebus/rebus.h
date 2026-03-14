#ifndef REBUS_H
#define REBUS_H
/*
 * rebus.h  —  AST for the Rebus language (Griswold TR 84-9)
 *
 * Rebus is a SNOBOL4/Icon hybrid:
 *   - The L-component (control structures) is from Icon:
 *     if/else, while, until, repeat, for, case, exit, next,
 *     fail, return, function declarations, record declarations.
 *   - The P-component (pattern matching) is SNOBOL4's:
 *     subject ? pattern  (mtch)
 *     subject ? pattern <- replacement  (replc)
 *     subject ?- pattern  (shorthand replc-with-empty)
 *
 * Programs consist of a sequence of declarations (record and function).
 * Execution begins with a call to main().
 *
 * This file defines:
 *   RExpr   — expression AST node (value/pattern expressions)
 *   RStmt   — statement AST node
 *   RDecl   — declaration (function or record)
 *   RProgram — top-level
 *
 * No emitter in this file.  The parser fills the AST; analysis comes later.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ================================================================
 * Expression kinds
 * ================================================================ */
typedef enum {
    /* Literals */
    RE_STR,         /* "foo" or 'foo' */
    RE_INT,         /* 42 */
    RE_REAL,        /* 3.14 */
    RE_NULL,        /* empty / omitted expression */

    /* References */
    RE_VAR,         /* identifier */
    RE_KEYWORD,     /* &ident  — SNOBOL4 keyword reference */

    /* Arithmetic / logic */
    RE_NEG,         /* unary -  */
    RE_POS,         /* unary +  (identity) */
    RE_NOT,         /* unary \  (DIFFER) */
    RE_VALUE,       /* unary /  (IDENT)  */
    RE_BANG,        /* unary !  (generate — Icon generator dereference) */

    RE_ADD,         /* +  */
    RE_SUB,         /* -  */
    RE_MUL,         /* *  */
    RE_DIV,         /* /  */
    RE_MOD,         /* %  (REMDR) */
    RE_POW,         /* ^  */

    /* String / pattern */
    RE_STRCAT,      /* ||  string concatenation */
    RE_PATCAT,      /* &   pattern concatenation */
    RE_ALT,         /* |   pattern alternation  */

    /* Comparison operators (Icon-style, succeed/fail) */
    RE_EQ,          /* =    numeric equal      (EQ) */
    RE_NE,          /* ~=   numeric not-equal  (NE) */
    RE_LT,          /* <    numeric less       (LT) */
    RE_LE,          /* <=   numeric LE         (LE) */
    RE_GT,          /* >    numeric GT         (GT) */
    RE_GE,          /* >=   numeric GE         (GE) */
    RE_SEQ,         /* ==   string equal       (IDENT) */
    RE_SNE,         /* ~==  string not-equal   (DIFFER) */
    RE_SLT,         /* <<   string LT          (LLT) */
    RE_SLE,         /* <<=  string LE          (LLE) */
    RE_SGT,         /* >>   string GT          (LGT) */
    RE_SGE,         /* >>=  string GE          (LGE) */

    /* Assignment forms */
    RE_ASSIGN,      /* :=   */
    RE_EXCHANGE,    /* :=:  */
    RE_ADDASSIGN,   /* +:=  */
    RE_SUBASSIGN,   /* -:=  */
    RE_CATASSIGN,   /* ||:= */

    /* Subscript / call */
    RE_CALL,        /* f(args) — function call */
    RE_SUB_IDX,     /* a[i]   or a[i+:n] — subscript */
    RE_RANGE,       /* expr +: expr  (substring) */

    /* Pattern captures (SNOBOL4 P-component) */
    RE_COND,        /* pat . var   conditional assign */
    RE_IMM,         /* pat $ var   immediate assign */
    RE_CURSOR,      /* @var        cursor capture */
    RE_DEREF,       /* *var        deferred pattern reference */
    RE_PATOPT,      /* ~pat        optional (zero or one, Icon ~) */

    /* Augmented assignment target */
    RE_AUG,         /* expr augop expr — generalized augmented form */
} REKind;

/* ================================================================
 * Expression node
 * ================================================================ */
typedef struct RExpr RExpr;
struct RExpr {
    REKind    kind;
    int       lineno;

    char     *sval;      /* RE_STR text, RE_VAR/RE_KEYWORD/RE_CALL name */
    long      ival;      /* RE_INT */
    double    dval;      /* RE_REAL */

    RExpr    *left;      /* unary child / binary left / subscripted expr */
    RExpr    *right;     /* binary right / replacement expr */

    RExpr   **args;      /* RE_CALL / RE_SUB_IDX arguments */
    int       nargs;

    REKind    augop;     /* for RE_AUG: the underlying op */
};

/* ================================================================
 * Statement kinds
 * ================================================================ */
typedef enum {
    RS_EXPR,        /* bare expression statement */
    RS_ASSIGN,      /* handled inside expr; kept for clarity */

    RS_IF,          /* if cond then s1 [else s2] */
    RS_UNLESS,      /* unless cond then s */
    RS_WHILE,       /* while cond do s */
    RS_UNTIL,       /* until cond do s */
    RS_REPEAT,      /* repeat s */
    RS_FOR,         /* for id from e1 to e2 [by e3] do s */
    RS_CASE,        /* case expr of { caselist } */

    RS_EXIT,        /* exit */
    RS_NEXT,        /* next */
    RS_FAIL,        /* fail */
    RS_RETURN,      /* return [expr] */
    RS_STOP,        /* stop */

    RS_MATCH,       /* expr ? expr  (pattern mtch, no replacement) */
    RS_REPLACE,     /* expr ? expr <- expr  (pattern replc) */
    RS_REPLN,       /* expr ?- expr  (replc with empty, shorthand) */

    RS_COMPOUND,    /* { stmt ; stmt ; … } */
} RSKind;

typedef struct RStmt RStmt;
typedef struct RCase RCase;

struct RStmt {
    RSKind    kind;
    int       lineno;

    /* RS_EXPR / RS_MATCH / RS_REPLACE / RS_REPLN */
    RExpr    *expr;      /* expression, subject, or condition */
    RExpr    *pat;       /* pattern (RS_MATCH / RS_REPLACE / RS_REPLN) */
    RExpr    *repl;      /* replacement (RS_REPLACE) */

    /* RS_IF / RS_UNLESS / RS_WHILE / RS_UNTIL / RS_REPEAT / RS_FOR */
    RStmt    *body;      /* then/do body */
    RStmt    *alt;       /* else branch (RS_IF only) */

    /* RS_FOR */
    char     *for_var;   /* loop variable name */
    RExpr    *for_from;
    RExpr    *for_to;
    RExpr    *for_by;    /* NULL means step 1 */

    /* RS_CASE */
    RExpr    *case_expr;
    RCase    *cases;     /* linked list of case clauses */

    /* RS_RETURN */
    RExpr    *retval;    /* NULL means return "" */

    /* RS_COMPOUND */
    RStmt   **stmts;
    int       nstmts;

    RStmt    *next;      /* within compound or function body */
};

/* A single case clause: expr : stmt  or  default : stmt */
struct RCase {
    int       is_default;
    RExpr    *guard;     /* NULL if is_default */
    RStmt    *body;
    RCase    *next;
};

/* ================================================================
 * Declarations
 * ================================================================ */
typedef enum {
    RD_FUNCTION,
    RD_RECORD,
} RDKind;

typedef struct RDecl RDecl;
struct RDecl {
    RDKind    kind;
    int       lineno;
    char     *name;

    /* RD_FUNCTION */
    char    **params;       /* parameter names */
    int       nparams;
    char    **locals;       /* local variable names */
    int       nlocals;
    RStmt    *initial;      /* initial clause (evaluated on first call) */
    RStmt    *body;         /* function body statements (linked via ->next) */

    /* RD_RECORD */
    char    **fields;       /* field names */
    int       nfields;

    RDecl    *next;
};

/* ================================================================
 * Program
 * ================================================================ */
typedef struct {
    RDecl  *decls;          /* linked list of declarations */
    int     ndecls;
} RProgram;

/* ================================================================
 * Allocators
 * ================================================================ */
static inline RExpr *rexpr_new(REKind k, int lineno) {
    RExpr *e = calloc(1, sizeof *e);
    e->kind   = k;
    e->lineno = lineno;
    return e;
}
static inline RStmt *rstmt_new(RSKind k, int lineno) {
    RStmt *s = calloc(1, sizeof *s);
    s->kind   = k;
    s->lineno = lineno;
    return s;
}
static inline RDecl *rdecl_new(RDKind k, int lineno) {
    RDecl *d = calloc(1, sizeof *d);
    d->kind   = k;
    d->lineno = lineno;
    return d;
}
static inline RCase *rcase_new(int lineno) {
    RCase *c = calloc(1, sizeof *c);
    (void)lineno;
    return c;
}

/* ================================================================
 * String helpers (same pattern as sno2c.h)
 * ================================================================ */
static inline char *rebus_intern(const char *s) {
    return s ? strdup(s) : NULL;
}
static inline char *rebus_intern_n(const char *s, int n) {
    char *p = malloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ================================================================
 * Public API
 * ================================================================ */

/* Parsing */
RProgram *rebus_parse(FILE *f, const char *filename);

/* Pretty-print AST to stream (for debugging / smoke tests) */
void rebus_print(RProgram *prog, FILE *out);

/* Error reporting */
void rebus_error(int lineno, const char *fmt, ...);
extern int   rebus_nerrors;
extern char *rebus_filename;

#endif /* REBUS_H */
