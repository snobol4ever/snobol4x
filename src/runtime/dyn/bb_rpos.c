/*
 * bb_rpos.c — RPOS(n) Byrd Box (M-DYN-2)
 *
 * Succeeds if cursor Δ == Ω - n (zero-width assert from right end).
 * No advance; no backtrack.
 *
 * Pattern:  RPOS(0)  — match at end of subject
 * SNOBOL4:  RPOS(n)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     RPOS_α:             if (Δ != Ω-n)                  → RPOS_ω
 *                         RPOS = spec(Σ+Δ, 0);            → RPOS_γ
 *     RPOS_β:                                            → RPOS_ω
 *     RPOS_γ:                                            return RPOS;
 *     RPOS_ω:                                            return spec_empty;
 *
 * State ζ: n only.  RPOS is a pure assertion — no cursor mutation.
 */

#include "bb_box.h"
#include <stdlib.h>

typedef struct { int n; } rpos_t;

spec_t bb_rpos(rpos_t **ζζ, int entry)
{
    rpos_t *ζ = *ζζ;

    if (entry == α)                                 goto RPOS_α;
    if (entry == β)                                 goto RPOS_β;

    /*------------------------------------------------------------------------*/
    spec_t         RPOS;

    RPOS_α:           if (Δ != Ω - ζ->n)                     goto RPOS_ω;
                      RPOS = spec(Σ+Δ, 0);                   goto RPOS_γ;

    RPOS_β:                                                   goto RPOS_ω;

    /*------------------------------------------------------------------------*/
    RPOS_γ:                                                   return RPOS;
    RPOS_ω:                                                   return spec_empty;
}

rpos_t *bb_rpos_new(int n)
{
    rpos_t *ζ = calloc(1, sizeof(rpos_t));
    ζ->n = n;
                                                              return ζ;
}
