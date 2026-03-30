#ifndef PROLOG_LOWER_H
#define PROLOG_LOWER_H
/*
 * prolog_lower.h — Prolog ClauseAST -> scrip-cc IR lowering
 *
 * prolog_lower() takes a PlProgram (from prolog_parse) and produces a
 * Program* of STMT_t nodes carrying the new Prolog IR node kinds:
 *
 *   E_CLAUSE      one Horn clause: sval=functor/arity, children=head_args+body
 *   E_CHOICE      all clauses for functor/arity: children=E_CLAUSE nodes
 *   E_UNIFY       unification: children[0]=lhs, children[1]=rhs
 *   E_CUT         cut: no children
 *   E_TRAIL_MARK  save trail top: ival=env slot index
 *   E_TRAIL_UNWIND restore trail: ival=env slot index
 *
 * Reused existing kinds for terms:
 *   E_QLIT  atom literals
 *   E_ILIT  integer literals
 *   E_FLIT  float literals
 *   E_VAR  variables (sval=slot index as decimal string, ival=slot)
 *   E_FNC   goals / compound terms (sval=functor name, children=args)
 *   E_ADD/E_SUB/E_MPY/E_DIV  arithmetic within is/2
 *
 * EnvLayout per clause is stored in the STMT_t's subject EXPR_t:
 *   subject->ival = n_vars
 *   subject->dval = (double)n_args  (reusing dval field)
 */

#include "scrip_cc.h"
#include "prolog_parse.h"

/*
 * prolog_lower(prog) — lower a parsed Prolog program to IR.
 *
 * Groups clauses by functor/arity into E_CHOICE nodes.
 * Returns a heap-allocated Program*; caller owns it.
 * Errors are printed to stderr; check returned prog->nstmts > 0.
 */
Program *prolog_lower(PlProgram *pl_prog);

/*
 * prolog_lower_pretty(prog, out) — dump IR for debugging.
 */
void prolog_lower_pretty(Program *prog, FILE *out);

#endif /* PROLOG_LOWER_H */
