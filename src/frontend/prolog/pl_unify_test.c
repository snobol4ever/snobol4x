/*
 * pl_unify_test.c — unit test for M-PROLOG-TERM
 *
 * Acceptance criterion (from PLAN.md):
 *   unify(f(X,a), f(b,Y)) -> X=b, Y=a
 *   trail_unwind restores both X and Y to unbound.
 *
 * Build:
 *   gcc -I. -o pl_unify_test pl_unify_test.c pl_unify.c pl_atom.c
 *
 * Expected output:
 *   PASS: unify(f(X,a), f(b,Y)) succeeds
 *   PASS: X = b
 *   PASS: Y = a
 *   PASS: trail_unwind restores X to unbound
 *   PASS: trail_unwind restores Y to unbound
 *   5/5 tests passed
 */

#include "term.h"
#include "pl_atom.h"
#include "pl_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define CHECK(label, cond) do {                                 \
    tests_run++;                                                \
    if (cond) {                                                 \
        printf("PASS: %s\n", label);                            \
        tests_passed++;                                         \
    } else {                                                    \
        printf("FAIL: %s\n", label);                            \
    }                                                           \
} while(0)

int main(void) {
    /* --- setup ---------------------------------------------------------- */
    pl_atom_init();

    int atom_f = pl_atom_intern("f");
    int atom_a = pl_atom_intern("a");
    int atom_b = pl_atom_intern("b");

    Trail trail;
    trail_init(&trail);

    /*
     * Build: f(X, a)
     *   X = unbound variable at slot 0
     *   a = atom
     */
    Term *X    = term_new_var(0);      /* slot 0 */
    Term *atom_a_term = term_new_atom(atom_a);
    Term *args1[2] = { X, atom_a_term };
    Term *fXa  = term_new_compound(atom_f, 2, args1);

    /*
     * Build: f(b, Y)
     *   b = atom
     *   Y = unbound variable at slot 1
     */
    Term *atom_b_term = term_new_atom(atom_b);
    Term *Y    = term_new_var(1);      /* slot 1 */
    Term *args2[2] = { atom_b_term, Y };
    Term *fbY  = term_new_compound(atom_f, 2, args2);

    /* --- save trail mark before unification ----------------------------- */
    int mark = trail_mark(&trail);

    /* --- attempt unify(f(X,a), f(b,Y)) --------------------------------- */
    int ok = unify(fXa, fbY, &trail);
    CHECK("unify(f(X,a), f(b,Y)) succeeds", ok == 1);

    /* --- verify X is bound to b ---------------------------------------- */
    Term *Xval = term_deref(X);
    CHECK("X = b",
          Xval && Xval->tag == TT_ATOM && Xval->atom_id == atom_b);

    /* --- verify Y is bound to a ---------------------------------------- */
    Term *Yval = term_deref(Y);
    CHECK("Y = a",
          Yval && Yval->tag == TT_ATOM && Yval->atom_id == atom_a);

    /* --- undo bindings via trail_unwind --------------------------------- */
    trail_unwind(&trail, mark);

    /* --- verify X is unbound again ------------------------------------- */
    CHECK("trail_unwind restores X to unbound",
          X->tag == TT_VAR && X->var_slot == 0);

    /* --- verify Y is unbound again ------------------------------------- */
    CHECK("trail_unwind restores Y to unbound",
          Y->tag == TT_VAR && Y->var_slot == 1);

    /* --- summary -------------------------------------------------------- */
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
