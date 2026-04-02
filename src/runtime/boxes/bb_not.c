/* _XNOT     NOT         \X — succeed iff X fails; β always ω (no retry) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>

/* o$nta/b/c three-entry semantics mapped to two-entry BB:
 *   α: run child with α; if child γ → NOT_ω (child succeeded → we fail);
 *                         if child ω → NOT_γ zero-width (child failed → we succeed)
 *   β: unconditional NOT_ω — negation succeeds at most once per position */
typedef struct { bb_box_fn fn; void *state; int start; } not_t;

spec_t bb_not(void *zeta, int entry)
{
    not_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto NOT_α;
    if (entry==β)                                                               goto NOT_β;
    NOT_α:          ζ->start=Δ;                                                 \
                    cr=ζ->fn(ζ->state,α);                                       \
                    if (!spec_is_empty(cr))                                     goto NOT_ω;
                    Δ=ζ->start;                                                 goto NOT_γ;
    NOT_β:                                                                      goto NOT_ω;
    NOT_γ:                                                                      return spec(Σ+Δ,0);
    NOT_ω:                                                                      return spec_empty;
}

not_t *bb_not_new(bb_box_fn fn, void *state)
{ not_t *ζ=calloc(1,sizeof(not_t)); ζ->fn=fn; ζ->state=state; return ζ; }
