/* _XRTB     RTAB        advance cursor TO position Ω-n */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_rtab(void *zeta, int entry)
{
    rtab_t *ζ = zeta;
    spec_t RTAB;
    if (entry==α)                                                               goto RTAB_α;
    if (entry==β)                                                               goto RTAB_β;
    RTAB_α:         if (Δ > Ω-ζ->n)                                             goto RTAB_ω;
                    ζ->advance=(Ω-ζ->n)-Δ; RTAB=spec(Σ+Δ,ζ->advance); Δ=Ω-ζ->n; goto RTAB_γ;
    RTAB_β:         Δ -= ζ->advance;                                            goto RTAB_ω;
    RTAB_γ:                                                                     return RTAB;
    RTAB_ω:                                                                     return spec_empty;
}

rtab_t *bb_rtab_new(int n)
{ rtab_t *ζ=calloc(1,sizeof(rtab_t)); ζ->n=n; return ζ; }
