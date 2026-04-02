/* _XABRT    ABORT       always ω — force match failure */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_abort(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    if (entry==α)                                                               goto ABORT_α;
    if (entry==β)                                                               goto ABORT_β;
    ABORT_α:                                                                    goto ABORT_ω;
    ABORT_β:                                                                    goto ABORT_ω;
    ABORT_ω:                                                                    return spec_empty;
}

abort_t *bb_abort_new(void)
{ return calloc(1,sizeof(abort_t)); }
