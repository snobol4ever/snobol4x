/*
 * bb_fail.c — FAIL Byrd Box (M-DYN-2)
 *
 * Always fails (ω) on every call (α or β).
 * Used for the SNOBOL4 FAIL pattern function, which forces the pattern
 * match to backtrack unconditionally.
 *
 * Pattern:  FAIL
 * SNOBOL4:  FAIL  (pattern function — forces backtrack)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     FAIL_α / β:         return spec_empty;             (always ω)
 *
 * State ζ: none (dummy).
 *
 * Contrast with ABORT (bb_abort.c): ABORT and FAIL both always ω, but
 * they signal different intent — FAIL is a soft "please backtrack",
 * ABORT is a hard "abandon the entire match".  In the C dynamic
 * interpreter they are currently identical; the distinction matters
 * more at the statement-executor level (Phase 3/5 handling).
 */

#include "bb_box.h"
#include <stdlib.h>

typedef struct { int dummy; } fail_t;

spec_t bb_fail(fail_t **ζζ, int entry)
{
    (void)ζζ; (void)entry;
                                                              return spec_empty;
}

fail_t *bb_fail_new(void)
{
                                                              return calloc(1, sizeof(fail_t));
}
