/* _XANYC    ANY         match one char if in set */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_any(void *zeta, int entry)
{
    any_t *ζ = zeta;
    spec_t ANY;
    if (entry==α)                                                               goto ANY_α;
    if (entry==β)                                                               goto ANY_β;
    ANY_α:          if (Δ>=Ω || !strchr(ζ->chars,Σ[Δ]))                         goto ANY_ω;
                    ANY = spec(Σ+Δ,1); Δ++;                                     goto ANY_γ;
    ANY_β:          Δ--;                                                        goto ANY_ω;
    ANY_γ:                                                                      return ANY;
    ANY_ω:                                                                      return spec_empty;
}

any_t *bb_any_new(const char *chars)
{ any_t *ζ=calloc(1,sizeof(any_t)); ζ->chars=chars; return ζ; }
