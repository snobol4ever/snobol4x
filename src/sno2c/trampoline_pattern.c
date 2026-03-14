/*
 * trampoline_pattern.c — Sprint `trampoline`: pattern match + trampoline
 *
 * Hand-translation of:
 *        S = 'hello world'
 *        S  'world'   :S(FOUND)F(NOTFOUND)
 * FOUND  OUTPUT = 'matched: world'    :(END)
 * NOTFOUND OUTPUT = 'no match'
 *   END
 *
 * Also exercises the loop-back case:
 *        I = 1
 * LOOP   EQ(I,3)   :S(DONE)
 *        OUTPUT = I
 *        I = I + 1   :(LOOP)
 * DONE   OUTPUT = 'done'
 *   END
 *
 * Uses real snobol4.c runtime for literal string pattern match (S/F routing).
 * Captures tested separately once runtime capture bug is fixed.
 *
 * Sprint: trampoline (1/9 toward M-BEAUTY-FULL)
 * Session: 56  (2026-03-14)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trampoline.h"
#include "../runtime/snobol4/snobol4.h"

/* ABORT handler chain */
abort_frame_t *_sno_abort_top = NULL;

/* SNOBOL4 variables */
static SnoVal _S;
static int    _I;

/* ---- Forward declarations ---- */
static void *prog1_block_START(void);
static void *prog1_block_FOUND(void);
static void *prog1_block_NOTFOUND(void);
static void *prog2_block_START(void);
static void *prog2_block_LOOP(void);
static void *prog2_block_DONE(void);
static void *block_END(void);

/* ===========================================================
 * Program 1: literal pattern S/F routing
 * =========================================================== */

BLOCK_FN(prog1_block_START) {
    _S = STR_VAL("hello world");
    /* S  'world'  :S(FOUND)F(NOTFOUND) */
    SnoVal pat = pat_lit("world");
    int ok = match_pattern(pat, to_str(_S));
    return ok ? prog1_block_FOUND : prog1_block_NOTFOUND;
}

BLOCK_FN(prog1_block_FOUND) {
    puts("matched: world");
    return prog2_block_START;   /* fall into program 2 */
}

BLOCK_FN(prog1_block_NOTFOUND) {
    puts("no match");
    return prog2_block_START;
}

/* ===========================================================
 * Program 2: loop with EQ predicate
 * =========================================================== */

BLOCK_FN(prog2_block_START) {
    _I = 1;
    return prog2_block_LOOP;
}

BLOCK_FN(prog2_block_LOOP) {
    /* EQ(I,3) :S(DONE) */
    if (_I == 3) return prog2_block_DONE;
    /* OUTPUT = I */
    printf("%d\n", _I);
    /* I = I + 1  :(LOOP) */
    _I++;
    return prog2_block_LOOP;
}

BLOCK_FN(prog2_block_DONE) {
    puts("done");
    return block_END;
}

BLOCK_FN(block_END) {
    return NULL;
}

int main(void) {
    runtime_init();
    trampoline_run(prog1_block_START);
    return 0;
}
