/* _XBRKC    BRK         scan to first char in set (may be zero-width) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_brk(void *zeta, int entry)
{
    brk_t *ζ = zeta;
    spec_t BRK;
    if (entry==α)                                                               goto BRK_α;
    if (entry==β)                                                               goto BRK_β;
    BRK_α:          ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Ω && !strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;     
                    if (Δ+ζ->δ >= Ω)                                            goto BRK_ω;
                    BRK = spec(Σ+Δ,ζ->δ); Δ += ζ->δ;                            goto BRK_γ;
    BRK_β:          Δ -= ζ->δ;                                                  goto BRK_ω;
    BRK_γ:                                                                      return BRK;
    BRK_ω:                                                                      return spec_empty;
}

brk_t *bb_brk_new(const char *chars)
{ brk_t *ζ=calloc(1,sizeof(brk_t)); ζ->chars=chars; return ζ; }
