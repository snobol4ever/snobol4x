/*
 * bb_fence.c — FENCE (cut) and ABORT Byrd Boxes (M-DYN-5)
 *
 * FENCE:
 *   α: succeeds immediately (epsilon match), sets ζ->fired.
 *   β: ALWAYS ω — the fence has been cut. No backtrack crosses FENCE.
 *
 * ABORT:
 *   α: immediately ω — forces entire match to fail.
 *   β: ω (unreachable, but defined for completeness).
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     FENCE_α:            ζ->fired = 1;                  → FENCE_γ
 *     FENCE_β:            (cut — no retry)               → FENCE_ω
 *     FENCE_γ:                                           return spec(Σ+Δ,0);
 *     FENCE_ω:                                           return spec_empty;
 *
 *     ABORT_α:                                           → ABORT_ω
 *     ABORT_β:                                           → ABORT_ω
 *     ABORT_ω:                                           return spec_empty;
 */

#include "bb_box.h"
#include <stdlib.h>

/* ── FENCE ───────────────────────────────────────────────────────────────── */
typedef struct { int fired; } fence_t;

spec_t bb_fence(fence_t **ζζ, int entry)
{
    fence_t *ζ = *ζζ;

    if (entry == α)                                 goto FENCE_α;
    if (entry == β)                                 goto FENCE_β;

    /*------------------------------------------------------------------------*/
    FENCE_α:          ζ->fired = 1;                           goto FENCE_γ;

    FENCE_β:                                                  goto FENCE_ω;

    /*------------------------------------------------------------------------*/
    FENCE_γ:                                                  return spec(Σ+Δ, 0);
    FENCE_ω:                                                  return spec_empty;
}

fence_t *bb_fence_new(void)
{
                                                              return calloc(1, sizeof(fence_t));
}

/* ── ABORT ───────────────────────────────────────────────────────────────── */
typedef struct { int dummy; } abort_t;

spec_t bb_abort(abort_t **ζζ, int entry)
{
    (void)ζζ; (void)entry;

    if (entry == α)                                 goto ABORT_α;
    if (entry == β)                                 goto ABORT_β;

    /*------------------------------------------------------------------------*/
    ABORT_α:                                                  goto ABORT_ω;

    ABORT_β:                                                  goto ABORT_ω;

    /*------------------------------------------------------------------------*/
    ABORT_ω:                                                  return spec_empty;
}

abort_t *bb_abort_new(void)
{
                                                              return calloc(1, sizeof(abort_t));
}
