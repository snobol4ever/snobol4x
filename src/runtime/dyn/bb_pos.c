/*
 * bb_pos.c — POS and RPOS Byrd Boxes (M-DYN-2)
 *
 * POS(n):  succeeds if cursor Δ == n (no advance, no backtrack)
 * RPOS(n): succeeds if cursor Δ == Ω - n
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     POS_α:              if (Δ != n)                    → POS_ω
 *                         POS = spec(Σ+Δ, 0);             → POS_γ
 *     POS_β:                                             → POS_ω   (no backtrack)
                      *     POS_γ:                            return POS;
                      *     POS_ω:                            return spec_empty;
 */

#include "bb_box.h"
#include <stdlib.h>

/* ── POS ─────────────────────────────────────────────────────────────────── */
typedef struct { int n; } pos_t;

spec_t bb_pos(pos_t **ζζ, int entry)
{
    pos_t *ζ = *ζζ;

    if (entry == α)                                 goto POS_α;
    if (entry == β)                                 goto POS_β;

    /*------------------------------------------------------------------------*/
    spec_t         POS;

    POS_α:            if (Δ != ζ->n)                          goto POS_ω;
                      POS = spec(Σ+Δ, 0);                     goto POS_γ;

    POS_β:                                                    goto POS_ω;

    /*------------------------------------------------------------------------*/
    POS_γ:                                                    return POS;
    POS_ω:                                                    return spec_empty;
}

pos_t *bb_pos_new(int n)
{
    pos_t *ζ = calloc(1, sizeof(pos_t));
    ζ->n = n;
                                                              return ζ;
}

/* ── RPOS ────────────────────────────────────────────────────────────────── */
typedef struct { int n; } rpos_t;

spec_t bb_rpos(rpos_t **ζζ, int entry)
{
    rpos_t *ζ = *ζζ;

    if (entry == α)                                 goto RPOS_α;
    if (entry == β)                                 goto RPOS_β;

    /*------------------------------------------------------------------------*/
    spec_t         RPOS;

    RPOS_α:           if (Δ != Ω - ζ->n)                      goto RPOS_ω;
                      RPOS = spec(Σ+Δ, 0);                    goto RPOS_γ;

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
