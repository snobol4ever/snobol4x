/* _XCHR     LIT         literal string match */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_lit(void *zeta, int entry)
{
    lit_t *ζ = zeta;
    spec_t LIT;
    if (entry==α)                                                               goto LIT_α;
    if (entry==β)                                                               goto LIT_β;
    LIT_α:          if (Δ + ζ->len > Ω)                                         goto LIT_ω;
                    if (memcmp(Σ+Δ, ζ->lit, (size_t)ζ->len) != 0)               goto LIT_ω;
                    LIT = spec(Σ+Δ, ζ->len); Δ += ζ->len;                       goto LIT_γ;
    LIT_β:          Δ -= ζ->len;                                                goto LIT_ω;
    LIT_γ:                                                                      return LIT;
    LIT_ω:                                                                      return spec_empty;
}

lit_t *bb_lit_new(const char *lit, int len)
{ lit_t *ζ=calloc(1,sizeof(lit_t)); ζ->lit=lit; ζ->len=len; return ζ; }
