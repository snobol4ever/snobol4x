/*
 * trepub.h — Code tree publication (v311.sil §6 TREPUB)
 *
 * TREPUB walks the expression tree built by ELEMNT/EXPR and emits each
 * node's CODE descriptor into the compiler output buffer (CMBSCL/CMOFCL).
 * When the buffer fills it spills to a new block via SPLIT.
 *
 * Also: ADDSON — attach a tree node as left-son of a parent.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18a
 */

#ifndef SIL_TREPUB_H
#define SIL_TREPUB_H

#include "types.h"

/* TREPUB — publish code tree rooted at node on operand stack.
 * v311.sil TREPUB line 2466.
 * Walks LSON → RSIB → FATHER chain, emitting CODE field of each node. */
RESULT_t TREPUB_fn(DESCR_t node);

/* ADDSON — add node 'son' as left son of 'parent'.
 * v311.sil ADDSON (inline macro in compiler). */
void ADDSON_fn(DESCR_t parent, DESCR_t son);

#endif /* SIL_TREPUB_H */
