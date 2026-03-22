#ifndef TERM_H
#define TERM_H
/*
 * term.h — Prolog term representation for snobol4x Prolog frontend
 *
 * TERM_t is independent of DESCR_t (SNOBOL4 runtime descriptor).
 * Lives entirely in the compile-time / unification world.
 *
 * Memory model:
 *   - All Terms are heap-allocated via term_new_*().
 *   - TT_REF forms a dereference chain; walk with term_deref().
 *   - The trail records which TT_VAR slots were bound so that
 *     trail_unwind() can restore them on backtrack.
 *
 * Compile-time variable slots:
 *   TT_VAR carries a var_slot index into the per-clause ENV DATA block.
 *   At runtime r12 points at the live env frame; slot k lives at [r12+k*8].
 *   A var_slot of -1 means "unbound wildcard" (_).
 *
 * List notation:
 *   [H|T]  ->  compound{ functor=ATOM_DOT, arity=2, args=[H,T] }
 *   []     ->  atom with atom_id for "[]"
 */

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Forward declaration
 * ---------------------------------------------------------------------- */
typedef struct Term Term;

/* -------------------------------------------------------------------------
 * TermTag — discriminates the union
 * ---------------------------------------------------------------------- */
typedef enum {
    TT_ATOM,      /* 'foo' or foo  — interned atom index               */
    TT_VAR,       /* X             — var_slot in env frame; -1=wildcard */
    TT_COMPOUND,  /* f(a,b)        — functor + arity + args[]           */
    TT_INT,       /* 42            — machine long                        */
    TT_FLOAT,     /* 3.14          — double                              */
    TT_REF        /* bound var     — pointer to target (dereference chain) */
} TermTag;

/* -------------------------------------------------------------------------
 * Term struct
 * ---------------------------------------------------------------------- */
struct Term {
    TermTag tag;
    int     saved_slot;  /* TT_VAR slot preserved across bind() for trail_unwind */
    union {
        /* TT_ATOM */
        int atom_id;

        /* TT_VAR: compile-time slot in env DATA block.
         * -1 = anonymous wildcard (_), never trailed.              */
        int var_slot;

        /* TT_COMPOUND */
        struct {
            int    functor;   /* atom_id of functor name              */
            int    arity;
            Term **args;      /* heap array of arity Term* pointers   */
        } compound;

        /* TT_INT */
        long ival;

        /* TT_FLOAT */
        double fval;

        /* TT_REF: dereference chain.
         * A TT_VAR becomes TT_REF after unification binds it.       */
        Term *ref;
    };
};

/* -------------------------------------------------------------------------
 * term_deref — walk TT_REF chain to the ultimate non-REF term
 * ---------------------------------------------------------------------- */
static inline Term *term_deref(Term *t) {
    while (t && t->tag == TT_REF) t = t->ref;
    return t;
}

/* -------------------------------------------------------------------------
 * Constructors (defined in pl_atom.c / pl_unify.c)
 * ---------------------------------------------------------------------- */
Term *term_new_atom(int atom_id);
Term *term_new_var(int var_slot);
Term *term_new_compound(int functor, int arity, Term **args);
Term *term_new_int(long ival);
Term *term_new_float(double fval);

/* Convenience: unbound variable with given slot */
static inline Term *term_new_unbound(int slot) {
    return term_new_var(slot);
}

/* -------------------------------------------------------------------------
 * Well-known functor atom IDs (populated by pl_atom_init())
 * ---------------------------------------------------------------------- */
extern int ATOM_DOT;    /* '.'  — list cons cell functor               */
extern int ATOM_NIL;    /* '[]' — empty list                           */
extern int ATOM_TRUE;   /* true                                        */
extern int ATOM_FAIL;   /* fail                                        */
extern int ATOM_CUT;    /* !                                           */

#endif /* TERM_H */
