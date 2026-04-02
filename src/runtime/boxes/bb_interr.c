/* _XINT     INTERR      ?X — null result if X succeeds; ω if X fails (o$int) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>

/* o$int: replace operand with null, continue.
 * In BB terms: run child; if child γ → discard match, return zero-width at
 * the *original* cursor (null string); if child ω → propagate ω.
 * β: unconditional ω — interrogation succeeds at most once. */
typedef struct { bb_box_fn fn; void *state; int start; } interr_t;

spec_t bb_interr(void *zeta, int entry)
{
    interr_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto INT_α;
    if (entry==β)                                                               goto INT_β;
    INT_α:          ζ->start=Δ;                                                 \
                    cr=ζ->fn(ζ->state,α);                                       \
                    if (spec_is_empty(cr))                                      goto INT_ω;
                    Δ=ζ->start;                                                 goto INT_γ;
    INT_β:                                                                      goto INT_ω;
    INT_γ:                                                                      return spec(Σ+Δ,0);
    INT_ω:                                                                      return spec_empty;
}

interr_t *bb_interr_new(bb_box_fn fn, void *state)
{ interr_t *ζ=calloc(1,sizeof(interr_t)); ζ->fn=fn; ζ->state=state; return ζ; }
