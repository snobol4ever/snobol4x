/* emit_cnode.h — CNode IR for sno2c C expression pretty-printer
 *
 * Architecture: pp/qq split (same model as beauty.sno)
 *   build_expr(Expr*)  → CNode tree  (no output — the "build" phase)
 *   cn_flat_width(n)   → int         (the "qq" lookahead)
 *   pp_cnode(n,fp,col) → int         (the "pp" print phase)
 *
 * Scope: expression trees only (emit_expr / emit_pat).
 * Structural lines (PLG/PS/PG) stay in emit.c unchanged.
 */

#ifndef EMIT_CNODE_H
#define EMIT_CNODE_H

#include <stdio.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Arena allocator — one per statement, freed after E(";\n").
 * Zero GC pressure; all CNodes for one expression live and die together.
 * ----------------------------------------------------------------------- */
typedef struct CArena {
    char  *buf;
    size_t cap;
    size_t used;
} CArena;

CArena *cn_arena_new(size_t cap);
void   *cn_arena_alloc(CArena *a, size_t sz);
char   *cn_arena_strdup(CArena *a, const char *s);
void    cn_arena_free(CArena *a);

/* -----------------------------------------------------------------------
 * CNode — IR node for a C expression fragment
 * ----------------------------------------------------------------------- */
typedef enum {
    CN_RAW,   /* literal text atom: "NULL_VAL", "strv(", ")", "," … */
    CN_CALL,  /* fn(arg0, arg1, …) — function call with N arg subtrees */
    CN_SEQ,   /* left immediately followed by right (no separator) */
} CNodeKind;

typedef struct CNode {
    CNodeKind     kind;
    const char   *text;    /* CN_RAW: literal text; CN_CALL: function name */
    struct CNode **args;   /* CN_CALL: array of N arg subtrees */
    int           nargs;
    struct CNode *left;    /* CN_SEQ: left fragment */
    struct CNode *right;   /* CN_SEQ: right fragment */
} CNode;

/* -----------------------------------------------------------------------
 * Constructors — all allocate from arena
 * ----------------------------------------------------------------------- */
CNode *cn_raw (CArena *a, const char *text);
CNode *cn_call(CArena *a, const char *fn, CNode **args, int nargs);
CNode *cn_seq (CArena *a, CNode *left, CNode *right);

/* Convenience: build a CN_CALL with 0..4 inline args */
CNode *cn_call0(CArena *a, const char *fn);
CNode *cn_call1(CArena *a, const char *fn, CNode *a0);
CNode *cn_call2(CArena *a, const char *fn, CNode *a0, CNode *a1);
CNode *cn_call3(CArena *a, const char *fn, CNode *a0, CNode *a1, CNode *a2);

/* -----------------------------------------------------------------------
 * Build phase — mirrors emit_expr / emit_pat
 * ----------------------------------------------------------------------- */
struct Expr;  /* forward decl — defined in sno2c.h */
CNode *build_expr(CArena *a, struct Expr *e);
CNode *build_pat (CArena *a, struct Expr *e);

/* -----------------------------------------------------------------------
 * Validation — flat printer for sprint 1 diff check
 * ----------------------------------------------------------------------- */
void cn_flat_print(CNode *n, FILE *fp);

/* -----------------------------------------------------------------------
 * Measure phase — "qq" lookahead
 * Returns flat character width of subtree, or INT_MAX if > limit.
 * ----------------------------------------------------------------------- */
int cn_flat_width(CNode *n, int limit);

/* -----------------------------------------------------------------------
 * Print phase — "pp" pretty-printer
 * col:    current output column (chars already on this line)
 * indent: additional indent for wrapped args (recommended: 4)
 * maxcol: line width budget (recommended: 120)
 * Returns: column after last character written.
 * ----------------------------------------------------------------------- */
int pp_cnode(CNode *n, FILE *fp, int col, int indent, int maxcol);

#endif /* EMIT_CNODE_H */
