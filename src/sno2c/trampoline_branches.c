/*
 * trampoline_branches.c — Sprint `trampoline` S/F routing proof
 *
 * Hand-translation of this SNOBOL4 program:
 *
 *   * SNOBOL4:
 *        I = 1
 * LOOP   OUTPUT = I
 *        I = I + 1
 *        EQ(I, 4)          :S(DONE)
 *        :(LOOP)
 * DONE   OUTPUT = 'done'
 *   END
 *
 * This tests:
 *   1. A labeled block (LOOP) — trampoline jumps back to it
 *   2. S goto (EQ succeeds → DONE)
 *   3. Unconditional goto (:(LOOP) → returns block_LOOP)
 *   4. Fall-through within a block (stmt_I_assign falls through in LOOP block)
 *
 * Each labeled statement starts a new block.
 * LOOP block: stmt_output + stmt_incr + stmt_eq (with :S(DONE)) + stmt_goto_loop
 * DONE block: stmt_done_output + block_END
 *
 * Sprint: trampoline (1/9 toward M-BEAUTY-FULL)
 * Session: 56  (2026-03-14)
 */

#include <stdio.h>
#include <stdlib.h>
#include "trampoline.h"

/* -----------------------------------------------------------------------
 * ABORT handler chain
 * ----------------------------------------------------------------------- */
abort_frame_t *_sno_abort_top = NULL;

/* -----------------------------------------------------------------------
 * SNOBOL4 "runtime" — just enough for this demo
 * ----------------------------------------------------------------------- */
static int sno_I = 0;   /* SNOBOL4 variable I */

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
static void *block_START(void);
static void *block_LOOP(void);
static void *block_DONE(void);
static void *block_END(void);

/* -----------------------------------------------------------------------
 * block_START:  I = 1  (no label on this stmt → same block as next until LOOP)
 * Actually: in SNOBOL4, "LOOP" is the first labeled stmt, so START is the
 * unlabeled preamble.  We have one stmt here.
 * ----------------------------------------------------------------------- */
BLOCK_FN(block_START) {
    /* stmt: I = 1 */
    sno_I = 1;
    /* fall through to LOOP */
    return block_LOOP;
}

/* -----------------------------------------------------------------------
 * block_LOOP — labeled block starting at LOOP
 *
 * SNOBOL4 statements in this block:
 *   LOOP  OUTPUT = I                     (no goto → fall through)
 *         I = I + 1                      (no goto → fall through)
 *         EQ(I, 4)          :S(DONE)     (success → DONE, fail → fall through)
 *         :(LOOP)                        (unconditional → LOOP)
 * ----------------------------------------------------------------------- */
BLOCK_FN(block_LOOP) {
    void *next;

    /* stmt: OUTPUT = I */
    printf("%d\n", sno_I);

    /* stmt: I = I + 1  (pure assignment, always succeeds, fall through) */
    sno_I = sno_I + 1;

    /* stmt: EQ(I, 4)  :S(DONE)  [implicit :F → fall through] */
    if (sno_I == 4) {
        /* S-goto → DONE */
        return block_DONE;
    }
    /* F → fall through to next stmt in block */

    /* stmt: :(LOOP)  — unconditional goto */
    return block_LOOP;

    (void)next;
}

/* -----------------------------------------------------------------------
 * block_DONE — labeled block starting at DONE
 * ----------------------------------------------------------------------- */
BLOCK_FN(block_DONE) {
    puts("done");
    return block_END;
}

/* -----------------------------------------------------------------------
 * block_END — program termination
 * ----------------------------------------------------------------------- */
BLOCK_FN(block_END) {
    return NULL;
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void) {
    trampoline_run(block_START);
    return 0;
}
