/*
 * sil_expr.h — Expression and element analysis (v311.sil §6)
 *
 * ELEMNT — break one element from TEXTSP and build a tree node.
 * EXPR   — compile a complete expression into a tree.
 * EXPR1  — EXPR with saved expression context (continuation).
 * NULNOD — build a LIT(null-string) tree node.
 * ADDSIB — add a sibling to a tree node.
 * INSERT — insert a node above another in the tree.
 * BINOP  — binary operator analysis.
 * UNOP   — unary operator analysis.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18d
 */

#ifndef SIL_EXPR_H
#define SIL_EXPR_H

#include "types.h"

RESULT_t ELEMNT_fn(DESCR_t *out);   /* compile one element → tree node */
RESULT_t EXPR_fn(DESCR_t *out);     /* compile full expression → tree  */
RESULT_t EXPR1_fn(DESCR_t *out);    /* EXPR continuation entry         */
RESULT_t NULNOD_fn(DESCR_t *out);   /* build LIT(null) node            */
void       ADDSIB_fn(DESCR_t node, DESCR_t sib);  /* add sibling         */
void       INSERT_fn(DESCR_t node, DESCR_t above); /* insert node above  */
RESULT_t BINOP_fn(DESCR_t *out);    /* binary operator analysis        */
RESULT_t UNOP_fn(DESCR_t *out);     /* unary operator analysis         */

#endif /* SIL_EXPR_H */
