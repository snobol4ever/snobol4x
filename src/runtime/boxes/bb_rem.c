/* _XSTAR    REM         match entire remainder; no backtrack */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_rem(void *zeta, int entry)
{
    (void)zeta;
    spec_t REM;
    if (entry==α)                                                               goto REM_α;
    if (entry==β)                                                               goto REM_β;
    REM_α:          REM = spec(Σ+Δ, Ω-Δ); Δ = Ω;                                goto REM_γ;
    REM_β:                                                                      goto REM_ω;
    REM_γ:                                                                      return REM;
    REM_ω:                                                                      return spec_empty;
}

rem_t *bb_rem_new(void)
{ return calloc(1,sizeof(rem_t)); }
