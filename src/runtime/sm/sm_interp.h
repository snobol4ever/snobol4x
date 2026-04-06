/*
 * sm_interp.h — SM_Program C interpreter dispatch loop (M-SCRIP-U2)
 *
 * Interprets a SM_Program directly in C — no x86 emission.
 * This is the Mode I execution engine and the correctness reference
 * for Mode G (in-memory codegen).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date: 2026-04-06
 */

#ifndef SM_INTERP_H
#define SM_INTERP_H

#include "sm_prog.h"
#include "../../runtime/snobol4/snobol4.h"

/* Value stack depth */
#define SM_STACK_MAX 256

/* Interpreter state */
typedef struct {
    DESCR_t   stack[SM_STACK_MAX];
    int       sp;          /* stack pointer: stack[0..sp-1] are live */
    int       last_ok;     /* 1 = last operation succeeded, 0 = failed */
    int       pc;          /* program counter: index into SM_Program */
} SM_State;

/*
 * Execute prog from instruction 0 until SM_HALT or end.
 * Returns 0 on normal halt, -1 on error.
 * Uses the live SNOBOL4 runtime (NV_GET_fn / NV_SET_fn etc.) —
 * call only after SNO_INIT_fn() has run.
 */
int sm_interp_run(SM_Program *prog, SM_State *st);

/* Initialise a fresh SM_State (stack empty, pc=0, last_ok=1) */
void sm_state_init(SM_State *st);

/* Push / pop helpers (used by sm_interp_run and tests) */
void sm_push(SM_State *st, DESCR_t d);
DESCR_t sm_pop(SM_State *st);
DESCR_t sm_peek(SM_State *st);

#endif /* SM_INTERP_H */
