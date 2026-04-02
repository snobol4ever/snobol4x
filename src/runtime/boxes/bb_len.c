/* _XLNTH    LEN         match exactly n characters */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_len(void *zeta, int entry)
{
    len_t *ζ = zeta;
    spec_t LEN;
    if (entry==α)                                                               goto LEN_α;
    if (entry==β)                                                               goto LEN_β;
    LEN_α:          if (Δ + ζ->n > Ω)                                           goto LEN_ω;
                    LEN = spec(Σ+Δ, ζ->n); Δ += ζ->n;                           goto LEN_γ;
    LEN_β:          Δ -= ζ->n;                                                  goto LEN_ω;
    LEN_γ:                                                                      return LEN;
    LEN_ω:                                                                      return spec_empty;
}

len_t *bb_len_new(int n)
{ len_t *ζ=calloc(1,sizeof(len_t)); ζ->n=n; return ζ; }
