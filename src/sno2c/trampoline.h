#ifndef TRAMPOLINE_H
#define TRAMPOLINE_H
/*
 * trampoline.h — block_fn_t + trampoline execution model
 *
 * Every SNOBOL4 statement compiles to a C function:
 *
 *   static block_fn_t stmt_N(block_locals_t *z);
 *
 * It returns the address of the next block to execute:
 *   - S-label block on success
 *   - F-label block on failure
 *   - NULL to terminate the program
 *
 * Statements are grouped into block functions by label reachability:
 *   static block_fn_t block_L(void);
 *
 * The trampoline IS the engine:
 *   block_fn_t pc = block_START;
 *   while (pc) pc = pc();
 *
 * Architecture decided: Lon Cherryholmes + Claude Sonnet 4.6, 2026-03-14
 * Sprint: trampoline (1/9 toward M-BEAUTY-FULL)
 */

#include <stddef.h>
#include <setjmp.h>

/* -----------------------------------------------------------------------
 * Core type: a block function returns the next block to execute.
 * NULL = program end (normal termination).
 *
 * C doesn't allow recursive typedefs directly.  We use the void* trick:
 *   block_fn_t = pointer to function(void) returning void*
 * At call sites: pc = (block_fn_t)pc();
 * This is the standard trampoline pattern in C89/C99/C11.
 * ----------------------------------------------------------------------- */
typedef void *(*block_fn_t)(void);

/* -----------------------------------------------------------------------
 * Trampoline: runs until pc returns NULL.
 * ----------------------------------------------------------------------- */
static inline void trampoline_run(block_fn_t start) {
    block_fn_t pc = start;
    while (pc) {
        pc = (block_fn_t)pc();
    }
}

/* -----------------------------------------------------------------------
 * Convenience macro for declaring a block function.
 * ----------------------------------------------------------------------- */
#define BLOCK_FN(name)   static void *name(void)
#define STMT_FN(name)    static void *name(void)

/* -----------------------------------------------------------------------
 * ABORT handler support (cold path only).
 * Normal S/F routing uses pure gotos — zero overhead.
 * ABORT/FENCE bare/runtime errors use longjmp to nearest handler.
 * ----------------------------------------------------------------------- */
typedef struct abort_frame {
    jmp_buf             jmp;
    struct abort_frame *prev;
} abort_frame_t;

extern abort_frame_t *_sno_abort_top;

#define ABORT_GUARD_PUSH(frame)  \
    do { (frame).prev = _sno_abort_top; _sno_abort_top = &(frame); } while(0)

#define ABORT_GUARD_POP()        \
    do { _sno_abort_top = _sno_abort_top->prev; } while(0)

#define ABORT_GUARD_THROW()      \
    do { if (_sno_abort_top) longjmp(_sno_abort_top->jmp, 1); } while(0)

#endif /* TRAMPOLINE_H */
