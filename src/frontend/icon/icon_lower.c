/*
 * icon_lower.c — Icon AST (IcnNode) → unified IR (EXPR_t) lowering pass
 *
 * Walks an IcnNode tree produced by icon_parse.c and produces an EXPR_t tree
 * using canonical EKind names from ir/ir.h.  After this pass the IcnNode tree
 * is no longer needed; callers should icn_node_free() it.
 *
 * Mapping policy (verified semantically, M-G9-ICON-IR-WIRE 2026-03-30):
 *   SHARED  — IcnKind maps to an existing EKind with identical Byrd-box semantics.
 *   NEW     — IcnKind has a new EKind added to ir.h in this milestone.
 *
 * See GRAND_MASTER_REORG.md §M-G9-ICON-IR-WIRE for the full mapping table.
 *
 * Produced by: Claude Sonnet 4.6 (G-9 s29, 2026-03-30)
 */

#include "icon_lower.h"
#include "icon_ast.h"
#include "../../ir/ir.h"           /* EKind + full EXPR_t */
#include "../snobol4/scrip_cc.h"   /* expr_new, expr_add_child, expr_unary,
                                      expr_binary, intern; EXPR_T_DEFINED set by ir.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Forward declaration
 * ========================================================================= */
static EXPR_t *lower_node(const IcnNode *n);

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* lower_children — lower all children of src and append to dst. */
static void lower_children(EXPR_t *dst, const IcnNode *src) {
    for (int i = 0; i < src->nchildren; i++)
        expr_add_child(dst, lower_node(src->children[i]));
}

/* leaf_sval — allocate a leaf node carrying a string payload. */
static EXPR_t *leaf_sval(EKind k, const char *s) {
    EXPR_t *e = expr_new(k);
    e->sval = intern(s);
    return e;
}

/* nary — lower all IcnNode children into an n-ary EXPR_t node. */
static EXPR_t *nary(EKind k, const IcnNode *n) {
    EXPR_t *e = expr_new(k);
    lower_children(e, n);
    return e;
}

/* unary — lower first child into a unary EXPR_t node. */
static EXPR_t *unary(EKind k, const IcnNode *n) {
    return expr_unary(k, lower_node(n->children[0]));
}

/* binary — lower first two children into a binary EXPR_t node. */
static EXPR_t *binary(EKind k, const IcnNode *n) {
    return expr_binary(k, lower_node(n->children[0]),
                          lower_node(n->children[1]));
}

/* =========================================================================
 * Main lowering dispatch
 * ========================================================================= */

static EXPR_t *lower_node(const IcnNode *n) {
    if (!n) return NULL;

    EXPR_t *e;

    switch (n->kind) {

    /* ----- Literals ------------------------------------------------------- */
    case ICN_INT:
        e = expr_new(E_ILIT);
        e->ival = n->val.ival;
        return e;

    case ICN_REAL:
        e = expr_new(E_FLIT);
        e->dval = n->val.fval;
        return e;

    case ICN_STR:
        return leaf_sval(E_QLIT, n->val.sval);   /* SHARED */

    case ICN_CSET:
        return leaf_sval(E_CSET, n->val.sval);   /* SHARED */

    /* ----- Variable ------------------------------------------------------- */
    case ICN_VAR:
        return leaf_sval(E_VAR, n->val.sval);    /* SHARED */

    /* ----- Arithmetic ----------------------------------------------------- */
    case ICN_ADD:   return binary(E_ADD,  n);    /* SHARED */
    case ICN_SUB:   return binary(E_SUB,  n);    /* SHARED */
    case ICN_MUL:   return binary(E_MPY,  n);    /* SHARED */
    case ICN_DIV:   return binary(E_DIV,  n);    /* SHARED */
    case ICN_MOD:   return binary(E_MOD,  n);    /* SHARED */
    case ICN_POW:   return binary(E_POW,  n);    /* SHARED */
    case ICN_NEG:   return unary (E_NEG,  n);    /* SHARED */
    case ICN_POS:   return unary (E_PLS,  n);    /* SHARED: unary coerce */

    /* ----- Numeric relational (NEW) --------------------------------------- */
    case ICN_LT:    return binary(E_LT,   n);
    case ICN_LE:    return binary(E_LE,   n);
    case ICN_GT:    return binary(E_GT,   n);
    case ICN_GE:    return binary(E_GE,   n);
    case ICN_EQ:    return binary(E_EQ,   n);
    case ICN_NE:    return binary(E_NE,   n);

    /* ----- String relational (NEW) ----------------------------------------
     * ICN_SEQ is string equality (==), NOT goal-directed sequence.
     * Maps to E_SSEQ (E_SEQ is already taken for goal-directed sequence).   */
    case ICN_SLT:   return binary(E_SLT,  n);
    case ICN_SLE:   return binary(E_SLE,  n);
    case ICN_SGT:   return binary(E_SGT,  n);
    case ICN_SGE:   return binary(E_SGE,  n);
    case ICN_SEQ:   return binary(E_SSEQ, n);    /* ICN_SEQ == string EQ */
    case ICN_SNE:   return binary(E_SNE,  n);

    /* ----- Cset operators (NEW) ------------------------------------------ */
    case ICN_COMPLEMENT:  return unary (E_CSET_COMPL, n);
    case ICN_CSET_UNION:  return binary(E_CSET_UNION, n);
    case ICN_CSET_DIFF:   return binary(E_CSET_DIFF,  n);
    case ICN_CSET_INTER:  return binary(E_CSET_INTER, n);

    /* ----- String / list concat ------------------------------------------ */
    case ICN_CONCAT:  return binary(E_CONCAT,  n);  /* SHARED: || string */
    case ICN_LCONCAT: return binary(E_LCONCAT, n);  /* NEW:    ||| list  */

    /* ----- Unary operators (NEW) ----------------------------------------- */
    case ICN_NONNULL: return unary(E_NONNULL, n);
    case ICN_NULL:    return unary(E_NULL,    n);
    case ICN_NOT:     return unary(E_NOT,     n);
    case ICN_SIZE:    return unary(E_SIZE,    n);
    case ICN_RANDOM:  return unary(E_RANDOM,  n);

    case ICN_IDENTICAL: return binary(E_IDENTICAL, n);  /* NEW: === */

    case ICN_AUGOP:
        /* Augmented assignment: E1 op:= E2
         * op subtype stored in n->val.ival (TK_AUG* token kind). */
        e = expr_new(E_AUGOP);
        e->ival = n->val.ival;            /* preserve op subtype */
        lower_children(e, n);
        return e;

    /* ----- Assignment / swap / scan -------------------------------------- */
    case ICN_ASSIGN: return binary(E_ASSIGN, n);  /* SHARED */
    case ICN_SWAP:   return binary(E_SWAP,   n);  /* SHARED */
    case ICN_MATCH:  return binary(E_MATCH,  n);  /* SHARED: E ? body */
    case ICN_SCAN_AUGOP: return binary(E_SCAN_AUGOP, n);  /* NEW */

    /* ----- Control flow (NEW) -------------------------------------------- */
    case ICN_SEQ_EXPR: return nary(E_SEQ_EXPR, n);

    /* ICN_AND — n-ary conjunction with full Byrd-box wiring.
     * Semantically identical to E_SEQ (goal-directed sequence): SHARED.   */
    case ICN_AND: return nary(E_SEQ, n);           /* SHARED */

    /* ICN_ALT — value alternation (left | right), same as E_GENALT. */
    case ICN_ALT: return nary(E_GENALT, n);        /* SHARED */

    case ICN_EVERY:
        e = expr_new(E_EVERY);
        lower_children(e, n);
        return e;

    case ICN_WHILE:
        e = expr_new(E_WHILE);
        lower_children(e, n);
        return e;

    case ICN_UNTIL:
        e = expr_new(E_UNTIL);
        lower_children(e, n);
        return e;

    case ICN_REPEAT:
        e = expr_new(E_REPEAT);
        lower_children(e, n);
        return e;

    case ICN_IF:
        /* children: cond, then-branch, [else-branch] */
        e = expr_new(E_IF);
        lower_children(e, n);
        return e;

    case ICN_CASE:
        e = expr_new(E_CASE);
        lower_children(e, n);
        return e;

    case ICN_BREAK:
        /* ICN_BREAK = loop break — E_LOOP_BREAK.
         * DISTINCT from E_BREAK = SNOBOL4 BREAK(S) pattern primitive.    */
        e = expr_new(E_LOOP_BREAK);
        if (n->nchildren > 0) lower_children(e, n);
        return e;

    case ICN_NEXT:
        return expr_new(E_LOOP_NEXT);

    case ICN_FAIL:
        return expr_new(E_FAIL);              /* SHARED */

    case ICN_RETURN:
        e = expr_new(E_RETURN);
        if (n->nchildren > 0) lower_children(e, n);
        return e;

    /* ----- Generators ---------------------------------------------------- */
    case ICN_SUSPEND:
        e = expr_new(E_SUSPEND);
        lower_children(e, n);
        return e;                                  /* SHARED */

    case ICN_TO:    return binary(E_TO,    n);     /* SHARED */
    case ICN_TO_BY: return nary  (E_TO_BY, n);     /* SHARED: 3 children */
    case ICN_LIMIT: return binary(E_LIMIT, n);     /* SHARED */
    case ICN_BANG:  return unary (E_ITER,  n);     /* SHARED: !E */

    case ICN_BANG_BINARY:
        return binary(E_BANG_BINARY, n);            /* NEW: E1 ! E2 */

    /* ----- Structure ----------------------------------------------------- */
    case ICN_SUBSCRIPT: return binary(E_IDX, n);   /* SHARED: E[i] */

    case ICN_SECTION:
        return nary(E_SECTION, n);      /* NEW: E[i:j],  3 children */

    case ICN_SECTION_PLUS:
        return nary(E_SECTION_PLUS, n); /* NEW: E[i+:n], 3 children */

    case ICN_SECTION_MINUS:
        return nary(E_SECTION_MINUS, n);/* NEW: E[i-:n], 3 children */

    case ICN_MAKELIST:
        return nary(E_MAKELIST, n);                /* SHARED */

    case ICN_RECORD:
        return leaf_sval(E_RECORD, n->val.sval);   /* NEW: record decl */

    case ICN_FIELD:
        /* E.name: child[0] = object, val.sval = field name */
        e = expr_new(E_FIELD);
        e->sval = intern(n->val.sval);
        expr_add_child(e, lower_node(n->children[0]));
        return e;

    case ICN_GLOBAL:
        return leaf_sval(E_GLOBAL, n->val.sval);   /* NEW: global decl */

    /* ----- Procedure / call ---------------------------------------------- */
    case ICN_PROC:
        /* Procedure declaration: sval = name, children = params + body.
         * Represented as E_FNC with sval = name; children lowered.
         * SHARED: same representation as other frontends' top-level E_FNC. */
        e = expr_new(E_FNC);
        e->sval = intern(n->val.sval);
        lower_children(e, n);
        return e;

    case ICN_CALL:
        /* Function call: first child is callee (E_VAR or E_FNC),
         * remaining children are arguments.  SHARED: E_FNC. */
        e = expr_new(E_FNC);
        lower_children(e, n);
        return e;

    /* ----- Initial / once-on-first-call ---------------------------------- */
    case ICN_INITIAL:
        e = expr_new(E_INITIAL);
        lower_children(e, n);
        return e;

    /* ----- Default: should not reach here -------------------------------- */
    default:
        fprintf(stderr, "icon_lower: unhandled IcnKind %d at line %d\n",
                (int)n->kind, n->line);
        /* Return a stub E_NUL so compilation can continue and report
         * further errors rather than crashing. */
        return expr_new(E_NUL);
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

/*
 * icon_lower_proc — lower one IcnNode procedure tree to EXPR_t.
 *
 * The returned EXPR_t is an E_FNC node whose sval is the procedure name
 * and whose children are the lowered parameter / body nodes.
 * Caller owns the result; free with expr_free() (or the emitter's own
 * lifetime management — consistent with how Prolog lower.c handles it).
 */
EXPR_t *icon_lower_proc(const IcnNode *proc) {
    return lower_node(proc);
}

/*
 * icon_lower_file — lower an array of IcnNode procedure trees.
 *
 * Returns a malloc'd array of EXPR_t* of length *out_count.
 * The IcnNode trees are not freed here — caller retains ownership.
 */
EXPR_t **icon_lower_file(IcnNode **procs, int count, int *out_count) {
    EXPR_t **result = malloc((size_t)count * sizeof(EXPR_t *));
    for (int i = 0; i < count; i++)
        result[i] = lower_node(procs[i]);
    *out_count = count;
    return result;
}
