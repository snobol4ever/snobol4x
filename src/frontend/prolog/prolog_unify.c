/*
 * prolog_unify.c — Trail and unification for the Prolog frontend
 *
 * Implements Robinson's unification algorithm with occurs-check omitted
 * (standard Prolog semantics).  All variable bindings are recorded on
 * the Trail so that backtracking can undo them via trail_unwind().
 *
 * Byrd Box connection (per Proebsting / FRONTEND-PROLOG.md):
 *   unify() is called in the α port (try-head) of each clause box.
 *   On failure the box jumps to β (try next clause) or ω (all exhausted).
 *   trail_unwind() is called at the start of every β and ω port to restore
 *   the variable environment before retrying or signalling failure.
 */

#include "prolog_runtime.h"
#include "term.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Trail
 * ======================================================================= */

#define TRAIL_INIT_CAP 64

void trail_init(Trail *t) {
    t->stack    = malloc(TRAIL_INIT_CAP * sizeof(Term *));
    t->top      = 0;
    t->capacity = TRAIL_INIT_CAP;
}

void trail_push(Trail *t, Term *term) {
    if (t->top >= t->capacity) {
        t->capacity *= 2;
        t->stack = realloc(t->stack, t->capacity * sizeof(Term *));
    }
    t->stack[t->top++] = term;
}

/*
 * trail_unwind — undo all bindings recorded since mark.
 *
 * Each entry on the stack is a Term* that was a TT_VAR node converted
 * to TT_REF by bind().  We restore tag to TT_VAR.
 * IMPORTANT: var_slot and ref share the same union memory.  We must
 * restore var_slot, not just clear ref.  bind() saves it in saved_slot.
 */
void trail_unwind(Trail *t, int mark) {
    while (t->top > mark) {
        Term *bound = t->stack[--t->top];
        int saved_slot = bound->saved_slot;
        bound->tag      = TT_VAR;
        bound->var_slot = saved_slot;
    }
}

/* =========================================================================
 * Internal: bind one unbound variable to a term
 * ======================================================================= */

/*
 * bind(var, val, trail) — var must be TT_VAR, unbound.
 *
 * We store val in var->ref and change var->tag to TT_REF.
 * We push (Term *)var onto the trail so trail_unwind can reverse this.
 * The cast to Term** is intentional — see trail_unwind above.
 */
static void bind(Term *var, Term *val, Trail *trail) {
    /* Don't trail anonymous wildcards (var_slot == -1). */
    if (var->var_slot != -1)
        trail_push(trail, var);   /* push the Term node itself */
    var->ref = val;
    var->tag = TT_REF;
}

/* =========================================================================
 * unify
 * ======================================================================= */

int unify(Term *t1, Term *t2, Trail *trail) {
    t1 = term_deref(t1);
    t2 = term_deref(t2);

    /* Identical pointers (includes two refs to same unbound var) */
    if (t1 == t2) return 1;

    /* If either is an unbound variable, bind it */
    if (t1 && t1->tag == TT_VAR) { bind(t1, t2, trail); return 1; }
    if (t2 && t2->tag == TT_VAR) { bind(t2, t1, trail); return 1; }

    /* Both must be non-NULL and non-VAR at this point */
    if (!t1 || !t2) return 0;

    /* Atoms */
    if (t1->tag == TT_ATOM && t2->tag == TT_ATOM)
        return t1->atom_id == t2->atom_id;

    /* Integers */
    if (t1->tag == TT_INT && t2->tag == TT_INT)
        return t1->ival == t2->ival;

    /* Floats */
    if (t1->tag == TT_FLOAT && t2->tag == TT_FLOAT)
        return t1->fval == t2->fval;

    /* Compound terms: same functor, same arity, pairwise-unify args */
    if (t1->tag == TT_COMPOUND && t2->tag == TT_COMPOUND) {
        if (t1->compound.functor != t2->compound.functor) return 0;
        if (t1->compound.arity   != t2->compound.arity  ) return 0;
        int arity = t1->compound.arity;
        for (int i = 0; i < arity; i++) {
            if (!unify(t1->compound.args[i], t2->compound.args[i], trail))
                return 0;
        }
        return 1;
    }

    /* Type mismatch */
    return 0;
}

/* Non-inline wrapper so ASM backend can call trail_mark via extern */
int trail_mark_fn(const Trail *t) { return t->top; }
