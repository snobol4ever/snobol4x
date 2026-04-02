/* _XFARB    ARB         match 0..n chars lazily; β extends by 1 */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_arb(void *zeta, int entry)
{
    arb_t *ζ = zeta;
    spec_t ARB;
    if (entry==α)                                                               goto ARB_α;
    if (entry==β)                                                               goto ARB_β;
    ARB_α:          ζ->count=0; ζ->start=Δ; ARB=spec(Σ+Δ,0);                    goto ARB_γ;
    ARB_β:          ζ->count++;                                                 
                    if (ζ->start+ζ->count > Ω)                                  goto ARB_ω;
                    Δ=ζ->start; ARB=spec(Σ+Δ,ζ->count); Δ+=ζ->count;            goto ARB_γ;
    ARB_γ:                                                                      return ARB;
    ARB_ω:                                                                      return spec_empty;
}

arb_t *bb_arb_new(void)
{ return calloc(1,sizeof(arb_t)); }
