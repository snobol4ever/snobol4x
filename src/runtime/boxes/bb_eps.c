/* _XEPS     EPS         zero-width success once; done flag prevents double-γ */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_eps(void *zeta, int entry)
{
    eps_t *ζ = zeta;
    spec_t EPS;
    if (entry==α)   ζ->done=0;                                                  goto EPS_α;
    if (entry==β)                                                               goto EPS_β;
    EPS_α:          if (ζ->done)                                                goto EPS_ω;
                    ζ->done=1; EPS=spec(Σ+Δ,0);                                 goto EPS_γ;
    EPS_β:                                                                      goto EPS_ω;
    EPS_γ:                                                                      return EPS;
    EPS_ω:                                                                      return spec_empty;
}

eps_t *bb_eps_new(void)
{ return calloc(1,sizeof(eps_t)); }
