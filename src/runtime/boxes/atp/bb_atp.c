/* _XATP     ATP         @var — write cursor Δ as DT_I into varname; no backtrack */
#include "bb_box.h"
#include "../../snobol4/snobol4.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_atp(void *zeta, int entry)
{
    atp_t *ζ = zeta;
    spec_t ATP;
    if (entry==α)                                                               goto ATP_α;
    if (entry==β)                                                               goto ATP_β;
    ATP_α:          ζ->done=1;                                                  
                    if (ζ->varname && ζ->varname[0]) {                          
                        DESCR_t v={.v=DT_I,.i=(int64_t)Δ};                      
                        NV_SET_fn(ζ->varname, v); }                             
                    ATP = spec(Σ+Δ,0);                                          goto ATP_γ;
    ATP_β:                                                                      goto ATP_ω;
    ATP_γ:                                                                      return ATP;
    ATP_ω:                                                                      return spec_empty;
}

atp_t *bb_atp_new(const char *varname)
{ atp_t *ζ=calloc(1,sizeof(atp_t)); ζ->varname=varname; return ζ; }
