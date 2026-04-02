/* _XFNCE    FENCE       succeed once; β cuts (no retry) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_fence(void *zeta, int entry)
{
    fence_t *ζ = zeta;
    if (entry==α)                                                               goto FENCE_α;
    if (entry==β)                                                               goto FENCE_β;
    FENCE_α:        ζ->fired=1;                                                 goto FENCE_γ;
    FENCE_β:                                                                    goto FENCE_ω;
    FENCE_γ:                                                                    return spec(Σ+Δ,0);
    FENCE_ω:                                                                    return spec_empty;
}

fence_t *bb_fence_new(void)
{ return calloc(1,sizeof(fence_t)); }
