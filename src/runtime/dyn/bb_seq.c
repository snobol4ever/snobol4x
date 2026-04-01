/*
 * bb_seq.c — SEQ (sequence) Byrd Box (M-DYN-2)
 *
 * SEQ(left, right): match left, then right at the new cursor position.
 * On backtrack: retry right first, then if right exhausted retry left.
 *
 * Pattern:  POS(0) 'Bird' RPOS(0)
 * SNOBOL4:  implicit concatenation
 *
 * Three-column layout — canonical form from test_sno_*.c §seq:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     seq_alpha:              seq = spec(Σ+Δ, 0);             → left_alpha
 *     seq_beta:                                             → right_beta
 *     left_gamma:             seq = spec_cat(seq, left);          → right_alpha
 *     left_omega:                                            → seq_omega
 *     right_gamma:            seq = spec_cat(seq, right);         → seq_gamma
 *     right_omega:                                           → left_beta
 *     seq_gamma:              return seq;
 *     seq_omega:              return spec_empty;
 *
 * State zeta: the accumulated seq value, child states.
 */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "bb_box.h"
#include <stdlib.h>

/* forward from bb_alt.c */
typedef struct bb_child bb_child_t;
typedef struct { bb_box_fn fn; void *zeta; } bb_child2_t;

typedef struct {
    bb_child2_t left;
    bb_child2_t right;
    spec_t       seq;
} seq_t;

/* ── bb_seq ──────────────────────────────────────────────────────────────── */
spec_t bb_seq(seq_t **zetazeta, int entry)
{
    seq_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto SEQ_alpha;
    if (entry == beta)                                     goto SEQ_beta;

    /*------------------------------------------------------------------------*/
    spec_t         SEQ;
    spec_t         left_r;
    spec_t         right_r;

    SEQ_alpha:        zeta->seq = spec(Σ+Δ, 0);
                  left_r = zeta->left.fn(&zeta->left.zeta, alpha);
                  if (spec_is_empty(left_r))                 goto left_omega;
                  else                                  goto left_gamma;

    SEQ_beta:        right_r = zeta->right.fn(&zeta->right.zeta, beta);
                  if (spec_is_empty(right_r))                goto right_omega;
                  else                                  goto right_gamma;

    left_gamma:       zeta->seq = spec_cat(zeta->seq, left_r);
                  right_r = zeta->right.fn(&zeta->right.zeta, alpha);
                  if (spec_is_empty(right_r))                goto right_omega;
                  else                                  goto right_gamma;

    left_omega:                                             goto SEQ_omega;

    right_gamma:      SEQ = spec_cat(zeta->seq, right_r);          goto SEQ_gamma;

    right_omega:      left_r = zeta->left.fn(&zeta->left.zeta, beta);
                  if (spec_is_empty(left_r))                 goto left_omega;
                  else                                  goto left_gamma;

    /*------------------------------------------------------------------------*/
    SEQ_gamma:        return SEQ;
    SEQ_omega:        return spec_empty;
}

/* ── bb_seq_new ──────────────────────────────────────────────────────────── */
seq_t *bb_seq_new(bb_box_fn left_fn, bb_box_fn right_fn)
{
    seq_t *zeta = calloc(1, sizeof(seq_t));
    zeta->left.fn  = left_fn;
    zeta->left.zeta   = NULL;
    zeta->right.fn = right_fn;
    zeta->right.zeta  = NULL;
    return zeta;
}
