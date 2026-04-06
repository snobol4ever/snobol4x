/*
 * sil_patval.h — pattern-valued functions and operations (v311.sil §10)
 *
 * Faithful C translation of v311.sil §10 lines 3119–3322:
 *   ANY BREAKX BREAK NOTANY SPAN — character-set pattern functions
 *   LEN POS RPOS RTAB TAB       — integer-argument pattern functions
 *   ARBNO                        — ARBNO(P) pattern
 *   ATOP                         — @X cursor capture
 *   NAM DOL                      — X.Y and X$Y value-assignment operators
 *   OR                           — X|Y alternation operator
 *
 * Pattern node layout (from S4D58 pp42-43, lib/pat.c):
 *   offset 0*DESCR: title (block header)
 *   offset 1*DESCR: function code descriptor (YCL / type dispatch)
 *   offset 2*DESCR: alternate link (next alternative, or 0)
 *   offset 3*DESCR: min-length / successor offset
 *   offset 4*DESCR: argument (for nodes that carry one: string, integer, expr)
 *   NODESZ = 3*DESCR (without argument), LNODSZ = 4*DESCR (with argument)
 *
 * Internal pat.c primitives translated here as static inlines:
 *   maknod_fn  — construct one pattern node
 *   cpypat_fn  — copy pattern block with offset fixup
 *   linkor_fn  — link alternation chains
 *   lvalue_fn  — compute minimum match length
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M7
 */

#ifndef SIL_PATVAL_H
#define SIL_PATVAL_H

#include "sil_types.h"

SIL_result ANY_fn(void);    /* ANY(S)    — match any char in set S   */
SIL_result BREAKX_fn(void); /* BREAKX(S) — break on set S [PLB31]   */
SIL_result BREAK_fn(void);  /* BREAK(S)  — break on set S           */
SIL_result NOTANY_fn(void); /* NOTANY(S) — match char not in S      */
SIL_result SPAN_fn(void);   /* SPAN(S)   — span chars in S          */

SIL_result LEN_fn(void);    /* LEN(N)    — match exactly N chars    */
SIL_result POS_fn(void);    /* POS(N)    — assert cursor at N       */
SIL_result RPOS_fn(void);   /* RPOS(N)   — assert cursor N from end */
SIL_result RTAB_fn(void);   /* RTAB(N)   — tab to N from end        */
SIL_result TAB_fn(void);    /* TAB(N)    — tab to position N        */

SIL_result ARBNO_fn(void);  /* ARBNO(P)  — zero-or-more P           */
SIL_result ATOP_fn(void);   /* @X        — cursor capture           */
SIL_result NAM_fn(void);    /* X . Y     — conditional assignment   */
SIL_result DOL_fn(void);    /* X $ Y     — immediate assignment     */
SIL_result OR_fn(void);     /* X | Y     — alternation              */

#endif /* SIL_PATVAL_H */
