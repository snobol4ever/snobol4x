/* _XFAIL    FAIL        always ω — force backtrack */
#include "bb_box.h"
#include <stdlib.h>
#include <string.h>


spec_t bb_fail(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    return spec_empty;
}

fail_t *bb_fail_new(void)
{ return calloc(1,sizeof(fail_t)); }
