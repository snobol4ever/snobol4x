/*
 * bb_rtab.c — RTAB(n) Byrd Box (M-DYN-5)
 *
 * Advance cursor TO position (Ω-n) if Δ ≤ (Ω-n); else ω.
 * Matches the span from current cursor to (Ω-n) characters from end.
 *
 * Pattern:  RTAB(0)  — advance to end of subject
 * SNOBOL4:  RTAB(n)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     RTAB_α:             if (Δ > Ω-n)                   → RTAB_ω
 *                         advance = (Ω-n)-Δ; Δ = Ω-n;    → RTAB_γ
 *     RTAB_β:             Δ -= advance;                  → RTAB_ω
 *     RTAB_γ:                                            return RTAB;
 *     RTAB_ω:                                            return spec_empty;
 *
 * State ζ: n + saved advance (for β restore).
 */

#include "bb_box.h"
#include <stdlib.h>

typedef struct { int n; int advance; } rtab_t;

spec_t bb_rtab(rtab_t **ζζ, int entry)
{
    rtab_t *ζ = *ζζ;

    if (entry == α)                                 goto RTAB_α;
    if (entry == β)                                 goto RTAB_β;

    /*------------------------------------------------------------------------*/
    spec_t         RTAB;

    RTAB_α:           if (Δ > Ω - ζ->n)                      goto RTAB_ω;
                      RTAB = spec(Σ+Δ, (Ω - ζ->n) - Δ);
                      ζ->advance = (Ω - ζ->n) - Δ;
                      Δ = Ω - ζ->n;                          goto RTAB_γ;

    RTAB_β:           Δ -= ζ->advance;                       goto RTAB_ω;

    /*------------------------------------------------------------------------*/
    RTAB_γ:                                                   return RTAB;
    RTAB_ω:                                                   return spec_empty;
}

rtab_t *bb_rtab_new(int n)
{
    rtab_t *ζ = calloc(1, sizeof(rtab_t));
    ζ->n = n;
                                                              return ζ;
}
