#ifndef PL_RUNTIME_H
#define PL_RUNTIME_H
/*
 * pl_runtime.h — Trail, EnvLayout, and runtime entry-points
 *
 * The Trail records every variable binding made during a unification
 * attempt so that trail_unwind() can undo them on backtrack.
 *
 * EnvLayout describes the per-clause DATA block layout that the
 * emitter uses to allocate [r12+k*8] slots at compile time.
 */

#include "term.h"

/* =========================================================================
 * Trail
 * ======================================================================= */

typedef struct {
    Term  **stack;    /* array of Term* — each entry is a bound TT_VAR node */
    int     top;      /* next free index                                     */
    int     capacity; /* allocated size of stack[]                           */
} Trail;

/* Initialise a trail (call once; trail lives on the heap).               */
void trail_init(Trail *t);

/* Record that term was just bound (tag changed to TT_REF).
 * Called inside unify() only.                                            */
void trail_push(Trail *t, Term *term);

/* Undo all bindings recorded since mark.  Sets their TT_REF back to
 * TT_VAR and pops the stack down to mark.                                */
void trail_unwind(Trail *t, int mark);

/* Save current trail top (use as argument to trail_unwind later).        */
static inline int trail_mark(const Trail *t) { return t->top; }

/* =========================================================================
 * EnvLayout — per-clause variable frame descriptor
 * ======================================================================= */

typedef struct {
    int n_vars;           /* number of distinct variables in the clause    */
    int n_args;           /* arity of the clause head predicate            */
    int trail_mark_slot;  /* reserved slot index for the saved trail mark  */
} EnvLayout;

/* =========================================================================
 * Unification
 * ======================================================================= */

/*
 * unify(t1, t2, trail) — attempt to unify t1 and t2.
 *
 * Returns 1 on success (bindings recorded on trail).
 * Returns 0 on failure (no bindings made; trail unchanged).
 *
 * Caller is responsible for trail_mark() before and trail_unwind() on
 * failure when it needs to backtrack.
 */
int unify(Term *t1, Term *t2, Trail *trail);

#endif /* PL_RUNTIME_H */
