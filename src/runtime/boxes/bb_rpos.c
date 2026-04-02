/* _XRPSI    RPOS        assert cursor == Ω-n (zero-width) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_rpos(void *zeta, int entry)
{
    rpos_t *ζ = zeta;
    spec_t RPOS;
    if (entry==α)                                                               goto RPOS_α;
    if (entry==β)                                                               goto RPOS_β;
    RPOS_α:         if (Δ != Ω-ζ->n)                                            goto RPOS_ω;
                    RPOS = spec(Σ+Δ,0);                                         goto RPOS_γ;
    RPOS_β:                                                                     goto RPOS_ω;
    RPOS_γ:                                                                     return RPOS;
    RPOS_ω:                                                                     return spec_empty;
}

rpos_t *bb_rpos_new(int n)
{ rpos_t *ζ=calloc(1,sizeof(rpos_t)); ζ->n=n; return ζ; }
