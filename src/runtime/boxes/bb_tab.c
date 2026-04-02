/* _XTB      TAB         advance cursor TO absolute position n */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_tab(void *zeta, int entry)
{
    tab_t *ζ = zeta;
    spec_t TAB;
    if (entry==α)                                                               goto TAB_α;
    if (entry==β)                                                               goto TAB_β;
    TAB_α:          if (Δ > ζ->n)                                               goto TAB_ω;
                    ζ->advance=ζ->n-Δ; TAB=spec(Σ+Δ,ζ->advance); Δ=ζ->n;        goto TAB_γ;
    TAB_β:          Δ -= ζ->advance;                                            goto TAB_ω;
    TAB_γ:                                                                      return TAB;
    TAB_ω:                                                                      return spec_empty;
}

tab_t *bb_tab_new(int n)
{ tab_t *ζ=calloc(1,sizeof(tab_t)); ζ->n=n; return ζ; }
