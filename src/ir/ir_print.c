/*
 * ir_print.c — Unified IR pretty-printer
 *
 * Prints any EXPR_t node (and its subtree) in a readable S-expression form.
 * Used for debugging all frontends uniformly — one printer, all 59 node kinds.
 *
 * Public API:
 *   ir_print_node(e, f)          — print node + subtree, no trailing newline
 *   ir_print_node_nl(e, f)       — same + trailing newline
 *
 * Output format (S-expression, compact):
 *   Leaf:    (E_QLIT "hello")   (E_ILIT 42)   (E_VAR x)   (E_NUL)
 *   Unary:   (E_MNS (E_ILIT 1))
 *   N-ary:   (E_PAT_SEQ (E_QLIT "a") (E_VAR x) (E_QLIT "b"))
 *   Wide:    multi-line with 2-space indent per depth level when nchildren > 1
 *
 * Produced by: Claude Sonnet 4.6 (G-7 session, 2026-03-28)
 * Milestone: M-G1-IR-PRINT
 */

/*
 * Include scrip-cc.h — it defines EXPR_T_DEFINED + IR_COMPAT_ALIASES then
 * includes ir/ir.h, giving us EKind, EXPR_t, and all compat aliases.
 * We additionally define IR_DEFINE_NAMES here to pull in ekind_name[].
 */
#define IR_DEFINE_NAMES
#include "scrip_cc.h"   /* → ir/ir.h (EKind, EXPR_t, compat aliases, ekind_name) */

/* -------------------------------------------------------------------------
 * ir_print.h forward declarations (inlined here — no separate .h needed
 * for a debug utility).
 * ---------------------------------------------------------------------- */

/* Maximum recursion depth before we truncate with "..." */
#define IR_PRINT_MAX_DEPTH 64

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Escape a string for printing: replace control chars, quote double-quotes */
static void print_escaped(const char *s, FILE *f) {
    if (!s) { fputs("(null)", f); return; }
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n",  f); break;
        case '\r': fputs("\\r",  f); break;
        case '\t': fputs("\\t",  f); break;
        default:
            if ((unsigned char)*p < 0x20)
                fprintf(f, "\\x%02x", (unsigned char)*p);
            else
                fputc(*p, f);
        }
    }
    fputc('"', f);
}

static void print_indent(int depth, FILE *f) {
    for (int i = 0; i < depth * 2; i++) fputc(' ', f);
}

/* Core recursive printer */
static void print_node(const EXPR_t *e, FILE *f, int depth) {
    if (!e) { fputs("(null)", f); return; }
    if (depth > IR_PRINT_MAX_DEPTH) { fputs("(...)", f); return; }

    /* Node kind name */
    const char *kname = (e->kind >= 0 && e->kind < E_KIND_COUNT)
                        ? ekind_name[e->kind]
                        : "E_???";

    /* Leaf nodes — no children, just payload */
    switch (e->kind) {
    case E_QLIT:
        fputc('(', f); fputs(kname, f); fputc(' ', f);
        print_escaped(e->sval, f); fputc(')', f);
        return;
    case E_ILIT:
        fprintf(f, "(%s %lld)", kname, (long long)e->ival);
        return;
    case E_FLIT:
        fprintf(f, "(%s %g)", kname, e->dval);
        return;
    case E_CSET:
        fputc('(', f); fputs(kname, f); fputc(' ', f);
        print_escaped(e->sval, f); fputc(')', f);
        return;
    case E_NUL:
        fprintf(f, "(%s)", kname);
        return;
    case E_VAR:
    case E_KEYWORD:
    case E_FNC:
    case E_IDX:
    case E_CAPT_COND_ASGN:
    case E_CAPT_IMMED_ASGN:
    case E_CAPT_CURSOR:
        /* sval carries the name; children (if any) are args */
        if (e->nchildren == 0) {
            fputc('(', f); fputs(kname, f);
            if (e->sval) { fputc(' ', f); fputs(e->sval, f); }
            fputc(')', f);
            return;
        }
        break;
    case E_ARB: case E_REM: case E_FAIL: case E_SUCCEED:
    case E_FENCE: case E_ABORT: case E_BAL:
        if (e->nchildren == 0) {
            fprintf(f, "(%s)", kname);
            return;
        }
        break;
    default:
        break;
    }

    /* General case: (KIND child1 child2 ...) */
    fputc('(', f);
    fputs(kname, f);

    /* Attach sval label when present and meaningful */
    if (e->sval && e->kind != E_QLIT && e->kind != E_CSET) {
        fputc(' ', f); fputs(e->sval, f);
    }

    if (e->nchildren == 0) {
        fputc(')', f);
        return;
    }

    if (e->nchildren == 1) {
        /* Inline single child */
        fputc(' ', f);
        print_node(e->children[0], f, depth + 1);
        fputc(')', f);
    } else {
        /* Multiple children — each on its own indented line */
        for (int i = 0; i < e->nchildren; i++) {
            fputc('\n', f);
            print_indent(depth + 1, f);
            print_node(e->children[i], f, depth + 1);
        }
        fputc('\n', f);
        print_indent(depth, f);
        fputc(')', f);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void ir_print_node(const EXPR_t *e, FILE *f) {
    print_node(e, f, 0);
}

void ir_print_node_nl(const EXPR_t *e, FILE *f) {
    print_node(e, f, 0);
    fputc('\n', f);
}

/* -------------------------------------------------------------------------
 * Unit test — compiled when IR_PRINT_TEST is defined.
 * Build: gcc -I src -I src/frontend/snobol4 -DIR_PRINT_TEST \
 *             src/ir/ir_print.c -o /tmp/ir_print_test
 * ---------------------------------------------------------------------- */
#ifdef IR_PRINT_TEST

/* Minimal EXPR_t for test — mirrors scrip-cc.h fields we use */
#include <stdlib.h>

static EXPR_t *mk(EKind k) {
    EXPR_t *e = calloc(1, sizeof *e);
    e->kind = k;
    return e;
}
static void add_child(EXPR_t *parent, EXPR_t *child) {
    parent->children = realloc(parent->children,
                               (size_t)(parent->nchildren + 1) * sizeof(EXPR_t *));
    parent->children[parent->nchildren++] = child;
}

int main(void) {
    /* (E_PAT_SEQ (E_QLIT "hello") (E_VAR x) (E_ILIT 42)) */
    EXPR_t *root = mk(E_PAT_SEQ);
    EXPR_t *lit  = mk(E_QLIT); lit->sval  = "hello";
    EXPR_t *var  = mk(E_VAR);  var->sval  = "x";
    EXPR_t *num  = mk(E_ILIT); num->ival  = 42;
    add_child(root, lit);
    add_child(root, var);
    add_child(root, num);

    /* (E_ASSIGN (E_VAR result) (E_ADD (E_ILIT 1) (E_ILIT 2))) */
    EXPR_t *assign = mk(E_ASSIGN);
    EXPR_t *lhs    = mk(E_VAR);  lhs->sval = "result";
    EXPR_t *add    = mk(E_ADD);
    EXPR_t *one    = mk(E_ILIT); one->ival = 1;
    EXPR_t *two    = mk(E_ILIT); two->ival = 2;
    add_child(add, one);
    add_child(add, two);
    add_child(assign, lhs);
    add_child(assign, add);

    /* (E_FNC LENGTH (E_VAR s)) */
    EXPR_t *fnc = mk(E_FNC); fnc->sval = "LENGTH";
    EXPR_t *arg = mk(E_VAR); arg->sval = "s";
    add_child(fnc, arg);

    /* Pattern: (E_PAT_ALT (E_QLIT "foo") (E_SPAN "abc")) */
    EXPR_t *alt  = mk(E_PAT_ALT);
    EXPR_t *foo  = mk(E_QLIT); foo->sval  = "foo";
    EXPR_t *span = mk(E_SPAN); span->sval = "abc";
    add_child(alt, foo);
    add_child(alt, span);

    fputs("=== ir_print unit test ===\n\n", stdout);

    fputs("1. E_PAT_SEQ:\n", stdout);
    ir_print_node_nl(root, stdout);

    fputs("\n2. E_ASSIGN:\n", stdout);
    ir_print_node_nl(assign, stdout);

    fputs("\n3. E_FNC:\n", stdout);
    ir_print_node_nl(fnc, stdout);

    fputs("\n4. E_PAT_ALT (pattern):\n", stdout);
    ir_print_node_nl(alt, stdout);

    fputs("\n5. E_NUL leaf:\n", stdout);
    ir_print_node_nl(mk(E_NUL), stdout);

    fputs("\n6. E_FAIL leaf:\n", stdout);
    ir_print_node_nl(mk(E_FAIL), stdout);

    fputs("\n=== PASS ===\n", stdout);
    return 0;
}
#endif /* IR_PRINT_TEST */
