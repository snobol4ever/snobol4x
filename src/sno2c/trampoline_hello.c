/*
 * trampoline_hello.c — Sprint `trampoline` proof-of-concept
 *
 * Hand-translation of this SNOBOL4 program:
 *
 *   * SNOBOL4:
 *        OUTPUT = 'hello, trampoline'
 *        OUTPUT = 'two statements, one block'
 *   END
 *
 * Architecture:
 *   - Two stmts → one block (no labels → same block)
 *   - block_START calls stmt_1, stmt_2 in sequence
 *   - Each stmt returns next block address or NULL
 *   - Trampoline runs: while (pc) pc = pc()
 *
 * This validates M-TRAMPOLINE: hello world through the trampoline loop.
 *
 * Sprint: trampoline (1/9 toward M-BEAUTY-FULL)
 * Session: 56  (2026-03-14)
 */

#include <stdio.h>
#include <stdlib.h>
#include "trampoline.h"

/* -----------------------------------------------------------------------
 * ABORT handler chain (cold path — not exercised in this hello world)
 * ----------------------------------------------------------------------- */
abort_frame_t *_sno_abort_top = NULL;

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
static void *block_START(void);
static void *block_END(void);

/* -----------------------------------------------------------------------
 * SNOBOL4 statement 1:  OUTPUT = 'hello, trampoline'
 *
 *   α: evaluate RHS → print → γ
 *   ω: (unreachable for pure assignment)
 *   γ: return next block (fall-through → stmt_2 inside block_START)
 *
 * Pure assignment: no pattern, no setjmp needed on hot path.
 * Returns NULL to signal "continue in caller" — block_START sequences them.
 * ----------------------------------------------------------------------- */
STMT_FN(stmt_1) {
    /* α: evaluate subject assignment */
    puts("hello, trampoline");
    /* γ: success — fall through to next stmt in block */
    return NULL;  /* NULL here means "done, caller continues sequencing" */
}

/* -----------------------------------------------------------------------
 * SNOBOL4 statement 2:  OUTPUT = 'two statements, one block'
 * ----------------------------------------------------------------------- */
STMT_FN(stmt_2) {
    puts("two statements, one block");
    return NULL;
}

/* -----------------------------------------------------------------------
 * block_START — groups stmt_1 and stmt_2 (no internal labels → same block)
 *
 * A block function sequences its member stmts.
 * If a stmt escapes (returns non-NULL), that IS the next block.
 * If all stmts fall through (return NULL), block falls through to block_END.
 * ----------------------------------------------------------------------- */
BLOCK_FN(block_START) {
    void *next;

    next = stmt_1();
    if (next) return next;   /* stmt escaped (S or F goto) */

    next = stmt_2();
    if (next) return next;

    /* All stmts fell through → normal end of program */
    return block_END;
}

/* -----------------------------------------------------------------------
 * block_END — program termination
 * Returns NULL → trampoline stops.
 * ----------------------------------------------------------------------- */
BLOCK_FN(block_END) {
    return NULL;   /* NULL → trampoline exits */
}

/* -----------------------------------------------------------------------
 * main — the engine IS the trampoline
 * ----------------------------------------------------------------------- */
int main(void) {
    trampoline_run(block_START);
    return 0;
}
