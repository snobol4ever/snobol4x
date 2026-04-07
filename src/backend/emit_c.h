/* emit_c.h — C backend public interface
 *
 * Consolidates:
 *   emit_cnode.h  — CNODE_t IR for expression pretty-printing
 *   emit_pretty.h — shared 3-column Byrd box formatter (PLG/PL/PS/PG macros)
 *
 * Note: emit_pretty.h requires PRETTY_OUT to be defined before inclusion.
 */

#ifndef EMIT_C_H
#define EMIT_C_H

/* ── CNODE_t: expression pretty-printer IR (formerly emit_cnode.h) ──────── */
/* emit_cnode.h — CNODE_t IR for scrip-cc C expression pretty-printer
 *
 * Architecture: pp/qq split (same model as beauty.sno)
 *   build_expr(EXPR_t*)  → CNODE_t tree  (no output — the "build" phase)
 *   cn_flat_width(n)   → int         (the "qq" lookahead)
 *   pp_cnode(n,fp,col) → int         (the "pp" print phase)
 *
 * Scope: expression trees only (emit_expr / emit_pat).
 * Structural lines (PLG/PS/PG) stay in emit.c unchanged.
 */


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
 * CNODE_t — IR node for a C expression fragment
 * ----------------------------------------------------------------------- */
typedef enum {
    CN_RAW,   /* literal text atom: "NULVCL", "STRVAL_fn(", ")", "," … */
    CN_CALL,  /* fn(arg0, arg1, …) — function call with N arg subtrees */
    CN_SEQ,   /* left immediately followed by right (no separator) */
} CNKIND_t;

typedef struct CNODE_t {
    CNKIND_t     kind;
    const char   *text;    /* CN_RAW: literal text; CN_CALL: function name */
    struct CNODE_t **args;   /* CN_CALL: array of N arg subtrees */
    int           nargs;
    struct CNODE_t *left;    /* CN_SEQ: left fragment */
    struct CNODE_t *right;   /* CN_SEQ: right fragment */
} CNODE_t;

/* -----------------------------------------------------------------------
 * Constructors — all allocate from arena
 * ----------------------------------------------------------------------- */
CNODE_t *cn_raw (CArena *a, const char *text);
CNODE_t *cn_call(CArena *a, const char *fn, CNODE_t **args, int nargs);
CNODE_t *cn_seq (CArena *a, CNODE_t *left, CNODE_t *right);

/* Convenience: build a CN_CALL with 0..4 inline args */
CNODE_t *cn_call0(CArena *a, const char *fn);
CNODE_t *cn_call1(CArena *a, const char *fn, CNODE_t *a0);
CNODE_t *cn_call2(CArena *a, const char *fn, CNODE_t *a0, CNODE_t *a1);
CNODE_t *cn_call3(CArena *a, const char *fn, CNODE_t *a0, CNODE_t *a1, CNODE_t *a2);

/* -----------------------------------------------------------------------
 * Build phase — mirrors emit_expr / emit_pat
 * ----------------------------------------------------------------------- */
struct EXPR_t;  /* forward decl — defined in scrip-cc.h */
CNODE_t *build_expr(CArena *a, struct EXPR_t *e);
CNODE_t *build_pat (CArena *a, struct EXPR_t *e);

/* -----------------------------------------------------------------------
 * Validation — flat printer for sprint 1 diff check
 * ----------------------------------------------------------------------- */
void cn_flat_print(CNODE_t *n, FILE *fp);

/* -----------------------------------------------------------------------
 * Measure phase — "qq" lookahead
 * Returns flat character width of subtree, or INT_MAX if > limit.
 * ----------------------------------------------------------------------- */
int cn_flat_width(CNODE_t *n, int limit);

/* -----------------------------------------------------------------------
 * Print phase — "pp" pretty-printer
 * col:    current output column (chars already on this line)
 * indent: additional indent for wrapped args (recommended: 4)
 * maxcol: line width budget (recommended: 120)
 * Returns: column after last character written.
 * ----------------------------------------------------------------------- */
int pp_cnode(CNODE_t *n, FILE *fp, int col, int indent, int maxcol);

/* ── 3-column Byrd box formatter (formerly emit_pretty.h) ───────────────── */
/* emit_pretty.h — shared 3-column Byrd box formatter
 *
 * Column layout:
 *   Col 1  Label   display  0..17   (4-space indent + label + ":" + pad)
 *   Col 2  STMT_t    display 18..59   (C statement body)
 *   Col 3  Goto    display 60+      (goto target)
 *
 * Usage:
 *   Before including this header, define PRETTY_OUT as the FILE* to write to:
 *     #define PRETTY_OUT out          (in emit.c)
 *     #define PRETTY_OUT byrd_out     (in emit_byrd.c)
 *
 * API:
 *   PLG(label, goto)               — label colon + goto (no stmt)
 *   PL(label, goto, fmt, ...)      — label + stmt + goto
 *   PS(goto, fmt, ...)             — blank label + stmt + goto
 *   PG(goto)                       — blank label + blank stmt + goto only
 *
 * Wrap rule: if stmt > COL_STMT_W display chars AND a goto follows,
 *            emit stmt on its own line, then goto on the next line in col 3.
 * ----------------------------------------------------------------------- */


#include <stdio.h>
#include <string.h>

#define COL_LABEL_W   18   /* display width of label column (includes 4-space indent) */
#define COL_STMT_W    42   /* display width of stmt  column */
#define COL_GOTO_COL  60   /* display column where goto field starts */

/* Count display width of a UTF-8 string: each Unicode codepoint = 1 display char.
 * Greek port letters (α β γ ω, 2-byte UTF-8) each count as 1 display char. */
static inline int pretty_disp_width(const char *s) {
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80 || *p >= 0xC0) w++;
        p++;
    }
    return w;
}

/* Emit (target - cur) spaces to fp; at least 1. */
static inline void pretty_pad_to(FILE *fp, int cur, int target) {
    int n = target - cur;
    if (n < 1) n = 1;
    for (int i = 0; i < n; i++) fputc(' ', fp);
}

/* Core formatter.
 *   fp    — output FILE*
 *   label — C label (without colon), or NULL/"" for blank col 1
 *   stmt  — C statement body (including semicolon if needed), or NULL/"" for blank col 2
 *   gt    — goto target label (without "goto " prefix), or NULL/"" for no goto
 */
static inline void pretty_line(FILE *fp, const char *label, const char *stmt, const char *gt) {
    int cur = 0;
    int has_label = label && label[0];
    int has_stmt  = stmt  && stmt[0];
    int has_goto  = gt    && gt[0];

    if (has_label) {
        fprintf(fp, "    %s:", label);
        cur = 4 + pretty_disp_width(label) + 1;
    }

    if (has_stmt) {
        pretty_pad_to(fp, cur, COL_LABEL_W);
        cur = COL_LABEL_W;
        int sw = pretty_disp_width(stmt);
        fputs(stmt, fp);
        cur += sw;
        if (has_goto) {
            if (sw > COL_STMT_W) {
                /* STMT_t overflows col 2 — wrap goto to next line */
                fputc('\n', fp);
                cur = 0;
                pretty_pad_to(fp, cur, COL_GOTO_COL);
            } else {
                pretty_pad_to(fp, cur, COL_GOTO_COL);
            }
            fprintf(fp, "goto %s;", gt);
        }
    } else if (has_goto) {
        pretty_pad_to(fp, cur, COL_GOTO_COL);
        fprintf(fp, "goto %s;", gt);
    }

    fputc('\n', fp);
}

/* Macros — require PRETTY_OUT to be defined as a FILE* before inclusion */
#ifndef PRETTY_OUT
#  error "Define PRETTY_OUT before including emit_pretty.h"
#endif

/* label + stmt + goto */
#define PL(lbl, gt, ...) do { \
    char _ps[512]; snprintf(_ps, sizeof(_ps), __VA_ARGS__); \
    pretty_line(PRETTY_OUT, (lbl), _ps, (gt)); \
} while(0)

/* blank label + stmt + goto */
#define PS(gt, ...) PL("", (gt), __VA_ARGS__)

/* blank label + blank stmt + goto only */
#define PG(gt) pretty_line(PRETTY_OUT, "", "", (gt))

/* label + blank stmt + goto (two-section: label + goto, no middle) */
#define PLG(lbl, gt) pretty_line(PRETTY_OUT, (lbl), "", (gt))

#endif /* EMIT_C_H */
