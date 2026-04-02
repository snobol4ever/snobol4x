/*
 * bb_rem.c — REM Byrd Box (M-DYN-2)
 *
 * Match the entire remainder of the subject from the current cursor.
 * Advances Δ to Ω.  No backtrack — REM is a one-shot consume.
 *
 * Pattern:  REM
 * SNOBOL4:  REM  (matches rest of subject unconditionally)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     REM_α:              REM = spec(Σ+Δ, Ω-Δ); Δ = Ω;  → REM_γ
 *     REM_β:                                             → REM_ω
 *     REM_γ:                                             return REM;
 *     REM_ω:                                             return spec_empty;
 *
 * State ζ: none (dummy).  Advance is always Ω-Δ, which can be recomputed
 * from current globals — no need to save it for β since β always ω's.
 */

#include "bb_box.h"
#include <stdlib.h>

typedef struct { int dummy; } rem_t;

spec_t bb_rem(rem_t **ζζ, int entry)
{
    (void)ζζ;

    if (entry == α)                                 goto REM_α;
    if (entry == β)                                 goto REM_β;

    /*------------------------------------------------------------------------*/
    spec_t         REM;

    REM_α:        REM = spec(Σ+Δ, Ω-Δ); Δ = Ω;               goto REM_γ;

    REM_β:                                                    goto REM_ω;

    /*------------------------------------------------------------------------*/
    REM_γ:                                                    return REM;
    REM_ω:                                                    return spec_empty;
}

rem_t *bb_rem_new(void)
{
                                                              return calloc(1, sizeof(rem_t));
}
