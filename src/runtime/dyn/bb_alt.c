/*
 * bb_alt.c — ALT (alternation) Byrd Box (M-DYN-2)
 *
 * ALT(left, right): try left; if it fails, restore cursor and try right.
 * On backtrack (beta): ask the child that succeeded to undo — if it can't, ALT omega's.
 *
 * THE KEY RULE (from test_sno_1.c §alt_beta):
 *   ALT_beta dispatches ONLY to the same child that last succeeded, asking it to beta.
 *   If that child omega's, ALT omega's. Period.
 *   ALT_beta NEVER advances to the next alternative — that is ARBNO's job.
 *   "Try next alternative" happens only at ALT_alpha via the child_omega → alt_i++ path.
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     ALT_alpha:              saved_Δ=Δ; alt_i=1;            → child[0]_alpha
 *     ALT_beta:              child[alt_i-1] beta               → child_beta_gamma / ALT_omega
 *     child_alpha_gamma:          result=child_result;           → ALT_gamma
 *     child_alpha_omega:          alt_i++; Δ=saved_Δ;           → child[alt_i-1]_alpha / ALT_omega
 *     child_beta_gamma:          result=child_result;           → ALT_gamma
 *     ALT_gamma:              return result;
 *     ALT_omega:              return spec_empty;
 */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "bb_box.h"
#include <stdlib.h>

#define BB_ALT_MAX 16

typedef struct {
    bb_box_fn  fn;
    void      *zeta;
} bb_child_t;

typedef struct {
    int        n;
    bb_child_t children[BB_ALT_MAX];
    int        alt_i;       /* which child is currently live (1-based) */
    int        saved_Δ;     /* cursor at ALT_alpha entry */
    spec_t      result;
} alt_t;

/* ── bb_alt ──────────────────────────────────────────────────────────────── */
spec_t bb_alt(alt_t **zetazeta, int entry)
{
    alt_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto ALT_alpha;
    if (entry == beta)                                     goto ALT_beta;

    /*------------------------------------------------------------------------*/
    spec_t         child_result;

    ALT_alpha:        zeta->saved_Δ = Δ;
                  zeta->alt_i   = 1;
                  child_result = zeta->children[0].fn(&zeta->children[0].zeta, alpha);
                  if (spec_is_empty(child_result))           goto child_alpha_omega;
                  else                                  goto child_alpha_gamma;

    ALT_beta:        /* ask the child that last succeeded to undo — never try next */
                  child_result = zeta->children[zeta->alt_i-1].fn(
                                     &zeta->children[zeta->alt_i-1].zeta, beta);
                  if (spec_is_empty(child_result))           goto ALT_omega;
                  else                                  goto child_beta_gamma;

    child_alpha_gamma:    zeta->result = child_result;             goto ALT_gamma;

    child_alpha_omega:    /* current child exhausted on alpha path — try next alternative */
                  zeta->alt_i++;
                  if (zeta->alt_i > zeta->n)                 goto ALT_omega;
                  Δ = zeta->saved_Δ;
                  child_result = zeta->children[zeta->alt_i-1].fn(
                                     &zeta->children[zeta->alt_i-1].zeta, alpha);
                  if (spec_is_empty(child_result))           goto child_alpha_omega;
                  else                                  goto child_alpha_gamma;

    child_beta_gamma:    zeta->result = child_result;             goto ALT_gamma;

    /*------------------------------------------------------------------------*/
    ALT_gamma:        return zeta->result;
    ALT_omega:        return spec_empty;
}

/* ── bb_alt_new ──────────────────────────────────────────────────────────── */
alt_t *bb_alt_new(int n, bb_box_fn *fns)
{
    alt_t *zeta = calloc(1, sizeof(alt_t));
    zeta->n = n;
    for (int i = 0; i < n && i < BB_ALT_MAX; i++) {
        zeta->children[i].fn = fns[i];
        zeta->children[i].zeta  = NULL;
    }
    return zeta;
}
