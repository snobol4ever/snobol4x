/*
 * ir_emit_common.c — Shared IR emit utilities
 *
 * Backend-agnostic helpers used by two or more emitters.
 * No output macros here — no E(), J(), N(), W().
 * No backend-specific headers — only scrip-cc.h and stdlib.
 *
 * Public API:
 *   ir_nary_right_fold(node, fold_kind, out_nodes, out_kids)
 *       — Reduce an n-ary EXPR_t node (nchildren > 2) into a right-folded
 *         chain of binary nodes of kind fold_kind.
 *         Returns the root of the chain (a synthesized binary node).
 *         Caller must free via ir_nary_right_fold_free(out_nodes, out_kids, n).
 *
 *   ir_nary_right_fold_free(nodes, kids, n)
 *       — Release allocations from ir_nary_right_fold.
 *
 * Usage pattern (replacing the duplicated right-fold in each emitter):
 *
 *   EXPR_t **fold_nodes = NULL, **fold_kids = NULL;
 *   EXPR_t *root = ir_nary_right_fold(pat, E_PAT_SEQ, &fold_nodes, &fold_kids);
 *   int n = pat->nchildren - 1;
 *   // ... emit root ...
 *   ir_nary_right_fold_free(fold_nodes, fold_kids, n);
 *
 * Naming law: this file is written to the law from creation (M-G3-NAME-COMMON).
 * All function names use the ir_ prefix. No Greek letters in this file
 * (it is not an emitter — it has no α/β/γ/ω parameters).
 *
 * Produced by: Claude Sonnet 4.6 (G-8 session, 2026-03-29)
 * Milestone: M-G4-SHARED-CONC-FOLD
 */

#include "scrip_cc.h"   /* → ir/ir.h (EKind, EXPR_t) */
#include <stdlib.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * ir_nary_right_fold
 *
 * Given an n-ary node (nchildren >= 3) and a fold_kind, produce a
 * right-folded binary tree:
 *
 *   children: [c0, c1, c2, c3]
 *   result:    fold(c0, fold(c1, fold(c2, c3)))
 *
 * The synthesized nodes share children[] pointers with the original node —
 * they are thin wrappers, not deep copies. Do not free the original node's
 * children while the fold is alive.
 *
 * Returns: root of the right-folded binary tree (one of the synthesized nodes).
 * Sets *out_nodes and *out_kids to heap allocations that must be freed via
 * ir_nary_right_fold_free.
 *
 * Preconditions: node != NULL, node->nchildren >= 3, fold_kind is a valid EKind.
 * ------------------------------------------------------------------------- */
EXPR_t *ir_nary_right_fold(EXPR_t *node, EKind fold_kind,
                            EXPR_t ***out_nodes, EXPR_t ***out_kids)
{
    assert(node != NULL);
    assert(node->nchildren >= 3);

    int nc = node->nchildren;
    int n  = nc - 1;   /* number of synthesized binary nodes */

    EXPR_t **nodes = malloc((size_t)n * sizeof(EXPR_t *));
    EXPR_t **kids  = malloc((size_t)n * 2 * sizeof(EXPR_t *));
    assert(nodes && kids);

    EXPR_t *right = node->children[nc - 1];
    for (int i = nc - 2; i >= 0; i--) {
        int slot = nc - 2 - i;
        nodes[slot] = calloc(1, sizeof(EXPR_t));
        assert(nodes[slot]);
        nodes[slot]->kind      = fold_kind;
        kids[slot*2 + 0]       = node->children[i];
        kids[slot*2 + 1]       = right;
        nodes[slot]->children  = &kids[slot*2];
        nodes[slot]->nchildren = 2;
        right = nodes[slot];
    }

    *out_nodes = nodes;
    *out_kids  = kids;
    return right;   /* root of the right-folded chain */
}

/* -------------------------------------------------------------------------
 * ir_nary_right_fold_free
 *
 * Release the n synthesized nodes and the kids array returned by
 * ir_nary_right_fold. n must equal original_node->nchildren - 1.
 * ------------------------------------------------------------------------- */
void ir_nary_right_fold_free(EXPR_t **nodes, EXPR_t **kids, int n)
{
    for (int i = 0; i < n; i++) free(nodes[i]);
    free(nodes);
    free(kids);
}
