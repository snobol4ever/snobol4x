/* _XSUCF    SUCCEED     always γ zero-width; outer loop retries */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_succeed(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    return spec(Σ+Δ,0);
}

succeed_t *bb_succeed_new(void)
{ return calloc(1,sizeof(succeed_t)); }
