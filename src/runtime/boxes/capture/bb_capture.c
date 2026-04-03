/* _XNME/_XFNME  CAPTURE     $ writes on every γ; . buffers for Phase-5 commit */
#include "bb_box.h"
#include "../../snobol4/snobol4.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    bb_box_fn fn; void *state; const char *varname;
    int immediate; spec_t pending; int has_pending;
} capture_t;

spec_t bb_capture(void *zeta, int entry)
{
    capture_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto CAP_α;
    if (entry==β)                                                               goto CAP_β;
    CAP_α:          cr=ζ->fn(ζ->state,α);                                       
                    if (spec_is_empty(cr))                                      goto CAP_ω;
                                                                                goto CAP_γ_core;
    CAP_β:          cr=ζ->fn(ζ->state,β);                                       
                    if (spec_is_empty(cr))                                      goto CAP_ω;
                                                                                goto CAP_γ_core;
    CAP_γ_core:     if (ζ->varname && *ζ->varname && ζ->immediate) {            
                        char *s=GC_MALLOC(cr.δ+1);                              
                        memcpy(s,cr.σ,(size_t)cr.δ); s[cr.δ]=0;                 
                        DESCR_t v={.v=DT_S,.slen=(uint32_t)cr.δ,.s=s};          
                        NV_SET_fn(ζ->varname,v);                                
                    } else if (ζ->varname && *ζ->varname) {                     
                        ζ->pending=cr; ζ->has_pending=1; }                      
                                                                                return cr;
    CAP_ω:          ζ->has_pending=0;                                           return spec_empty;
}

capture_t *bb_capture_new(bb_box_fn fn, void *state, const char *varname, int immediate)
{ capture_t *ζ=calloc(1,sizeof(capture_t)); ζ->fn=fn; ζ->state=state; ζ->varname=varname; ζ->immediate=immediate; return ζ; }
