/* _XDSAR/_XVAR  DVAR        *VAR/VAR — re-resolve live value on every α */
#include "bb_box.h"
#include "../../snobol4/snobol4.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; bb_box_fn child_fn; void *child_state; size_t child_size; } dvar_t;

spec_t bb_deferred_var(void *zeta, int entry)
{
    dvar_t *ζ = zeta;
    spec_t DVAR;
    if (entry==α)                                                               goto DVAR_α;
    if (entry==β)                                                               goto DVAR_β;
    DVAR_α:         { DESCR_t val=NV_GET_fn(ζ->name); int rebuilt=0;            
                      if (val.v==DT_P && val.p && val.p!=ζ->child_state) {      
                          bb_node_t c=bb_build(val.p);                          
                          ζ->child_fn=c.fn; ζ->child_state=c.ζ; ζ->child_size=c.ζ_size; rebuilt=1; }
                      else if (val.v==DT_S && val.s) {                          
                          lit_t *lz=(lit_t*)ζ->child_state;                   
                          if (!lz||lz->lit!=val.s) {                            
                              lz=calloc(1,sizeof(lit_t)); lz->lit=val.s; lz->len=(int)strlen(val.s);
                              ζ->child_fn=(bb_box_fn)bb_lit; ζ->child_state=lz; 
                              ζ->child_size=sizeof(lit_t); rebuilt=1; } }      
                      if (!rebuilt&&ζ->child_state&&ζ->child_size               
                          &&ζ->child_fn!=(bb_box_fn)bb_lit)                     
                          memset(ζ->child_state,0,ζ->child_size); }             
                    if (!ζ->child_fn)                                           goto DVAR_ω;
                    DVAR=ζ->child_fn(ζ->child_state,α);                         
                    if (spec_is_empty(DVAR))                                    goto DVAR_ω;
                                                                                goto DVAR_γ;
    DVAR_β:         if (!ζ->child_fn)                                           goto DVAR_ω;
                    DVAR=ζ->child_fn(ζ->child_state,β);                         
                    if (spec_is_empty(DVAR))                                    goto DVAR_ω;
                                                                                goto DVAR_γ;
    DVAR_γ:                                                                     return DVAR;
    DVAR_ω:                                                                     return spec_empty;
}

dvar_t *bb_dvar_new(const char *name)
{ dvar_t *ζ=calloc(1,sizeof(dvar_t)); ζ->name=name; return ζ; }
