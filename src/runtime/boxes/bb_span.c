/* _XSPNC    SPAN        longest prefix of chars in set (≥1) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_span(void *zeta, int entry)
{
    span_t *ζ = zeta;
    spec_t SPAN;
    if (entry==α)                                                               goto SPAN_α;
    if (entry==β)                                                               goto SPAN_β;
    SPAN_α:         ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Ω && strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;      
                    if (ζ->δ <= 0)                                              goto SPAN_ω;
                    SPAN = spec(Σ+Δ, ζ->δ); Δ += ζ->δ;                          goto SPAN_γ;
    SPAN_β:         Δ -= ζ->δ;                                                  goto SPAN_ω;
    SPAN_γ:                                                                     return SPAN;
    SPAN_ω:                                                                     return spec_empty;
}

span_t *bb_span_new(const char *chars)
{ span_t *ζ=calloc(1,sizeof(span_t)); ζ->chars=chars; return ζ; }
