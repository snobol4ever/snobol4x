/* _XNNYC    NOTANY      match one char if NOT in set */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_notany(void *zeta, int entry)
{
    notany_t *ζ = zeta;
    spec_t NOTANY;
    if (entry==α)                                                               goto NOTANY_α;
    if (entry==β)                                                               goto NOTANY_β;
    NOTANY_α:       if (Δ>=Ω || strchr(ζ->chars,Σ[Δ]))                          goto NOTANY_ω;
                    NOTANY = spec(Σ+Δ,1); Δ++;                                  goto NOTANY_γ;
    NOTANY_β:       Δ--;                                                        goto NOTANY_ω;
    NOTANY_γ:                                                                   return NOTANY;
    NOTANY_ω:                                                                   return spec_empty;
}

notany_t *bb_notany_new(const char *chars)
{ notany_t *ζ=calloc(1,sizeof(notany_t)); ζ->chars=chars; return ζ; }
