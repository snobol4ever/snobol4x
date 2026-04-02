/*
 * bb_succeed.c — SUCCEED Byrd Box (M-DYN-2)
 *
 * Always succeeds with a zero-width match on every call (α or β).
 * The outer scan loop provides the "infinite retry" behavior — SUCCEED
 * itself never ω's; it always hands back an epsilon γ.
 *
 * Pattern:  SUCCEED
 * SNOBOL4:  SUCCEED  (pattern function — infinite success generator)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     SUCCEED_α / β:      return spec(Σ+Δ, 0);           (always γ)
 *
 * State ζ: none (dummy).
 *
 * Contrast with EPS (bb_eps.c): EPS uses a `done` flag to succeed exactly
 * once per α and ω on β.  SUCCEED never ω's — it is truly infinite.
 */

#include "bb_box.h"
#include <stdlib.h>

typedef struct { int dummy; } succeed_t;

spec_t bb_succeed(succeed_t **ζζ, int entry)
{
    (void)ζζ; (void)entry;
                                                              return spec(Σ+Δ, 0);
}

succeed_t *bb_succeed_new(void)
{
                                                              return calloc(1, sizeof(succeed_t));
}
