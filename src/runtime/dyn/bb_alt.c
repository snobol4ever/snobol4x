/*
 * bb_alt.c — ALT (alternation) Byrd Box (M-DYN-2)
 *
 * ALT(left, right): try left; if it fails, restore cursor and try right.
 * On backtrack (β): ask the child that succeeded to undo — if it can't, ALT ω's.
 *
 * THE KEY RULE (from test_sno_1.c §alt_β):
 *   ALT_β dispatches ONLY to the same child that last succeeded, asking it to β.
 *   If that child ω's, ALT ω's. Period.
 *   ALT_β NEVER advances to the next alternative — that is ARBNO's job.
 *   "Try next alternative" happens only at ALT_α via the child_ω → alt_i++ path.
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     ALT_α:              saved_Δ=Δ; alt_i=1;            → child[0]_α
 *     ALT_β:              child[alt_i-1] β               → child_β_γ / ALT_ω
 *     child_α_γ:          result=child_result;           → ALT_γ
 *     child_α_ω:          alt_i++; Δ=saved_Δ;           → child[alt_i-1]_α / ALT_ω
 *     child_β_γ:          result=child_result;           → ALT_γ
                      *     ALT_γ:                            return result;
                      *     ALT_ω:                            return spec_empty;
 */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "bb_box.h"
#include <stdlib.h>

#define BB_ALT_MAX 16

typedef struct {
    bb_box_fn  fn;
    void      *ζ;
} bb_child_t;

typedef struct {
    int        n;
    bb_child_t children[BB_ALT_MAX];
    int        alt_i;       /* which child is currently live (1-based) */
    int        saved_Δ;     /* cursor at ALT_α entry */
    spec_t      result;
} alt_t;

/* ── bb_alt ──────────────────────────────────────────────────────────────── */
spec_t bb_alt(alt_t **ζζ, int entry)
{
    alt_t *ζ = *ζζ;

    if (entry == α)                                 goto ALT_α;
    if (entry == β)                                 goto ALT_β;

    /*------------------------------------------------------------------------*/
    spec_t         child_result;

    ALT_α:        ζ->saved_Δ = Δ;
                  ζ->alt_i   = 1;
                  child_result = ζ->children[0].fn(&ζ->children[0].ζ, α);
                      if (spec_is_empty(child_result))        goto child_α_ω;
                      else                                    goto child_α_γ;

    ALT_β:        /* ask the child that last succeeded to undo — never try next */
                  child_result = ζ->children[ζ->alt_i-1].fn(
                                     &ζ->children[ζ->alt_i-1].ζ, β);
                      if (spec_is_empty(child_result))        goto ALT_ω;
                      else                                    goto child_β_γ;

    child_α_γ:        ζ->result = child_result;               goto ALT_γ;

    child_α_ω:    /* current child exhausted on α path — try next alternative */
                  ζ->alt_i++;
                      if (ζ->alt_i > ζ->n)                    goto ALT_ω;
                  Δ = ζ->saved_Δ;
                  child_result = ζ->children[ζ->alt_i-1].fn(
                                     &ζ->children[ζ->alt_i-1].ζ, α);
                      if (spec_is_empty(child_result))        goto child_α_ω;
                      else                                    goto child_α_γ;

    child_β_γ:        ζ->result = child_result;               goto ALT_γ;

    /*------------------------------------------------------------------------*/
    ALT_γ:                                                    return ζ->result;
    ALT_ω:                                                    return spec_empty;
}

/* ── bb_alt_new ──────────────────────────────────────────────────────────── */
alt_t *bb_alt_new(int n, bb_box_fn *fns)
{
    alt_t *ζ = calloc(1, sizeof(alt_t));
    ζ->n = n;
    for (int i = 0; i < n && i < BB_ALT_MAX; i++) {
        ζ->children[i].fn = fns[i];
        ζ->children[i].ζ  = NULL;
    }
                                                              return ζ;
}
