/*
 * sil_trace.h — Tracing procedures (v311.sil §16 lines 5466–5827)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M16
 */

#ifndef SIL_TRACE_H
#define SIL_TRACE_H

#include "types.h"

RESULT_t TRACE_fn(void);    /* TRACE(V,R,T,F)         */
RESULT_t STOPTR_fn(void);   /* STOPTR(V,R)            */
RESULT_t FENTR_fn(void);    /* FENTR  — call trace    */
RESULT_t FENTR2_fn(DESCR_t name); /* FENTR2 std entry  */
RESULT_t KEYTR_fn(void);    /* KEYTR  — keyword trace */
RESULT_t LABTR_fn(void);    /* LABTR  — label trace   */
RESULT_t TRPHND_fn(DESCR_t atptr); /* trace handler    */
RESULT_t VALTR_fn(void);    /* VALTR  — value trace   */
RESULT_t FNEXTR_fn(void);   /* FNEXTR — return trace  */
RESULT_t FNEXT2_fn(DESCR_t name);  /* FNEXT2 ftrace    */
RESULT_t SETEXIT_fn(void);  /* SETEXIT(LBL)           */
RESULT_t XITHND_fn(void);   /* SETEXIT handler        */

#endif /* SIL_TRACE_H */
