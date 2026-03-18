/* Sprint 1: single literal "hello"
 * Subject: "hello"  Pattern: "hello"
 * Expected: match succeeds, OUTPUT = "hello" (via $ assign to OUTPUT)
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../src/runtime/runtime.h"

static int64_t lit1_saved_cursor;

int main(void) {
    const char *subject     = "hello";
    int64_t     subject_len = 5;
    int64_t     cursor      = 0;

    goto lit1_alpha;

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
    /* matched — print the matched substring */
    sno_output((str_t){ subject + lit1_saved_cursor, 5 });
    return 0;

lit1_omega:
    /* no match */
    return 1;
}
