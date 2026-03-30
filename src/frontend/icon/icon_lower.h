/*
 * icon_lower.h — Icon AST → unified IR lowering pass API
 *
 * Converts IcnNode trees (produced by icon_parse.c) to EXPR_t trees
 * (using canonical EKind from ir/ir.h).  After this pass the IcnNode tree
 * is frontend-private; emitters operate only on EXPR_t.
 *
 * M-G9-ICON-IR-WIRE (2026-03-30)
 */

#ifndef ICON_LOWER_H
#define ICON_LOWER_H

#include "icon_ast.h"
#include "../../ir/ir.h"

/* Lower one procedure IcnNode → EXPR_t (E_FNC, sval = proc name). */
EXPR_t *icon_lower_proc(const IcnNode *proc);

/* Lower an array of procedure IcnNodes.
 * Returns malloc'd EXPR_t** of length *out_count.  Caller owns result. */
EXPR_t **icon_lower_file(IcnNode **procs, int count, int *out_count);

#endif /* ICON_LOWER_H */
