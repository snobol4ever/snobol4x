/* Sprint 0: null program
 * Full runtime skeleton with alpha/beta/gamma/omega wiring.
 * No pattern nodes. Exits normally.
 * Expected output: (empty)
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../src/runtime/runtime.h"

int main(void) {
    /* Sprint 0: the null program.
     * Subject: empty string. Pattern: empty (always succeeds).
     * No nodes emitted. Runtime skeleton only. */
    const char *subject     = "";
    int64_t     subject_len = 0;
    int64_t     cursor      = 0;
    (void)subject; (void)subject_len; (void)cursor;

    /* alpha/beta/gamma/omega skeleton — no nodes wired yet */
    goto program_alpha;

program_alpha:
    goto program_gamma;   /* empty pattern always succeeds */

program_beta:
    goto program_omega;

program_gamma:
    /* match succeeded — do nothing for null program */
    return 0;

program_omega:
    /* match failed — null program cannot fail */
    return 1;
}
