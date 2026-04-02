/*
 * bb_abort.c — ABORT Byrd Box (M-DYN-5)
 *
 * ABORT: unconditionally fails the entire match.
 * Both α and β fire ω immediately.  No state; no advance.
 *
 * SNOBOL4: ABORT  (pattern function — causes immediate match failure)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     ABORT_α:                                           → ABORT_ω
 *     ABORT_β:                                           → ABORT_ω
 *     ABORT_ω:                                           return spec_empty;
 *
 * State ζ: none (dummy).
 *
 * Note: FENCE (bb_fence.c) is the complementary primitive — it succeeds
 * on α but cuts on β.  ABORT refuses to succeed at all.
 */

#include "bb_box.h"
#include <stdlib.h>

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
