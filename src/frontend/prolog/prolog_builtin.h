#ifndef PL_BUILTIN_H
#define PL_BUILTIN_H
/*
 * pl_builtin.h — Prolog runtime builtin declarations for scrip-cc
 *
 * These are the C-side implementations of write/1, nl/0, functor/3,
 * arg/3, =../2, and type tests.  Called from pl_emit.c generated code.
 */

#include "term.h"
#include "prolog_runtime.h"

/* Output */
void pl_write(Term *t);
void pl_writeln(Term *t);
void pl_writeq(Term *t);
void pl_write_canonical(Term *t);

/* functor/3: functor(Term, Name, Arity) */
int pl_functor(Term *t, Term *name, Term *arity, Trail *tr);

/* arg/3: arg(N, Term, Arg) */
int pl_arg(Term *n, Term *compound, Term *arg, Trail *tr);

/* =../2: Term =.. List  (univ) */
int pl_univ(Term *t, Term *list, Trail *tr);

/* read/1 (stub) */
int pl_read(Term *result, Trail *tr);

#endif /* PL_BUILTIN_H */

/* Arithmetic */
long pl_eval_arith(Term *t);
int  pl_is(Term *result, Term *expr, Trail *trail);
int  pl_num_lt(Term *a, Term *b);
int  pl_num_gt(Term *a, Term *b);
int  pl_num_le(Term *a, Term *b);
int  pl_num_ge(Term *a, Term *b);
int  pl_num_eq(Term *a, Term *b);
int  pl_num_ne(Term *a, Term *b);

/* Type tests */
int pl_atom(Term *t);
int pl_integer(Term *t);
int pl_is_float(Term *t);
int pl_var(Term *t);
int pl_nonvar(Term *t);
int pl_compound(Term *t);
int pl_callable(Term *t);

/* interp_exec_pl_builtin — execute one Prolog builtin goal node.
 * Defined in scrip.c. Returns 1=success, 0=failure. (S-BB-2) */
#ifndef EXPR_T_DEFINED
struct EXPR_t;
typedef struct EXPR_t EXPR_t;
#define EXPR_T_DEFINED 1
#endif
int interp_exec_pl_builtin(EXPR_t *goal, Term **env);
