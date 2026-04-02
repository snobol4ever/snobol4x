/* _XPOSI    POS         assert cursor == n (zero-width) */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_pos(void *zeta, int entry)
{
    pos_t *ζ = zeta;
    spec_t POS;
    if (entry==α)                                                               goto POS_α;
    if (entry==β)                                                               goto POS_β;
    POS_α:          if (Δ != ζ->n)                                              goto POS_ω;
                    POS = spec(Σ+Δ,0);                                          goto POS_γ;
    POS_β:                                                                      goto POS_ω;
    POS_γ:                                                                      return POS;
    POS_ω:                                                                      return spec_empty;
}

pos_t *bb_pos_new(int n)
{ pos_t *ζ=calloc(1,sizeof(pos_t)); ζ->n=n; return ζ; }
