/* Sprint 2: concatenation — POS(0) LIT("hello") RPOS(0)
 * Subject: "hello"   Pattern: POS(0) "hello" RPOS(0)
 * Expected output: hello
 *
 * Wiring:
 *   pos_gamma  -> lit_alpha
 *   lit_gamma  -> rpos_alpha
 *   rpos_gamma -> assign_alpha  ($ OUTPUT)
 *   rpos_omega -> lit_beta
 *   lit_omega  -> pos_beta
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../src/runtime/runtime.h"

static int64_t lit1_saved_cursor;
static int64_t assign1_start;
static str_t   var_OUTPUT;

int main(void) {
    const char *subject     = "hello";
    int64_t     subject_len = 5;
    int64_t     cursor      = 0;

    goto pos1_alpha;

/* --- POS(0) --- */
pos1_alpha:
    if (cursor != 0) goto pos1_omega;
    goto pos1_gamma;
pos1_beta:
pos1_omega:
    goto match_fail;
pos1_gamma:
    goto lit1_alpha;    /* CAT: pos_gamma -> lit_alpha */

/* --- LIT("hello") --- */
lit1_alpha:
    if (cursor + 5 > subject_len) goto lit1_omega;
    if (memcmp(subject + cursor, "hello", 5) != 0) goto lit1_omega;
    lit1_saved_cursor = cursor;
    cursor += 5;
    goto lit1_gamma;
lit1_beta:
    cursor = lit1_saved_cursor;
    goto lit1_omega;
lit1_gamma:
    goto rpos1_alpha;   /* CAT: lit_gamma -> rpos_alpha */
lit1_omega:
    goto pos1_beta;     /* CAT: lit_omega -> pos_beta */

/* --- RPOS(0) --- */
rpos1_alpha:
    if (cursor != subject_len - 0) goto rpos1_omega;
    goto rpos1_gamma;
rpos1_beta:
    goto rpos1_omega;
rpos1_gamma:
    goto assign1_alpha; /* CAT: rpos_gamma -> assign_alpha */
rpos1_omega:
    goto lit1_beta;     /* CAT: rpos_omega -> lit_beta */

/* --- ASSIGN($ OUTPUT) --- */
assign1_alpha:
    assign1_start = 0;  /* whole subject: from cursor=0 after pos, to cursor=5 after lit */
    goto assign1_child_alpha;
assign1_child_alpha:
    /* child is the span POS(0)..RPOS(0) — already matched; cursor is at end */
    goto assign1_do_assign;
assign1_do_assign:
    var_OUTPUT.ptr = subject + assign1_start;
    var_OUTPUT.len = cursor - assign1_start;
    sno_output(var_OUTPUT);
    goto match_success;
assign1_beta:
    goto match_fail;

match_success:
    return 0;
match_fail:
    return 1;
}
