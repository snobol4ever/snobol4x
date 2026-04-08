/*
 * arith.h — arithmetic operations, predicates, and functions (v311.sil §9)
 *
 * Faithful C translation of v311.sil §9 lines 2923–3118:
 *   ADD EXPOP DIV MPY SUB — binary arithmetic (→ ARITH dispatcher)
 *   EQ GE GT LE LT NE    — integer/real comparison predicates (→ ARITH)
 *   REMDR                 — remainder (→ ARITH)
 *   INTGER                — INTEGER() conversion function
 *   MNS                   — unary negation (-)
 *   PLS                   — unary plus (+)
 *
 * Architecture: ADD..REMDR all set SCL (operation selector 1..12) and
 * fall into ARITH_fn, which coerces XPTR/YPTR to a common numeric type
 * then dispatches on SCL.  INTGER/MNS/PLS are standalone entry points.
 *
 * Dependencies: XYARGS_fn (§8, M6) — forward-declared here as extern.
 *   ARGVAL_fn (§8) — same.
 *   Globals XPTR YPTR ZPTR WPTR SCL XSP YSP ARTHCL from data.h.
 *   Type-pair constants IIDTP..VVDTP from data.h.
 *   SPCINT_fn SPREAL_fn LOCSP_fn from strings.h.
 *
 * Error exits (matching v311.sil):
 *   AERROR  — arithmetic error (overflow, div-by-zero) → error(ERR_ARITH)
 *   INTR1   — illegal data type                        → error(ERR_TYPE)
 *   FAIL    — predicate failure or argument failure    → return FAIL
 *
 * Return convention: all entry points return RESULT_t (OK=1, FAIL=0).
 * On OK the result is in ZPTR (binary ops, MNS) or ZPTR/XPTR (PLS/INTGER).
 * Comparison predicates return OK with ZPTR = NULVCL on success.
 * ARITH_fn is internal — called only by ADD_fn..REMDR_fn.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M5
 */

#ifndef SIL_ARITH_H
#define SIL_ARITH_H

#include "types.h"

/* ── Binary arithmetic and comparison entry points ───────────────────── */
/* Each sets SCL and calls ARITH_fn. On return ZPTR holds the result.    */

RESULT_t ADD_fn(void);    /* XPTR + YPTR → ZPTR   (SCL=1)  */
RESULT_t DIV_fn(void);    /* XPTR / YPTR → ZPTR   (SCL=2)  */
RESULT_t EXPOP_fn(void);  /* XPTR ** YPTR → ZPTR  (SCL=3)  [PLB43] */
RESULT_t MPY_fn(void);    /* XPTR * YPTR → ZPTR   (SCL=4)  */
RESULT_t SUB_fn(void);    /* XPTR - YPTR → ZPTR   (SCL=5)  */
RESULT_t EQ_fn(void);     /* EQ(XPTR,YPTR)         (SCL=6)  */
RESULT_t GE_fn(void);     /* GE(XPTR,YPTR)         (SCL=7)  */
RESULT_t GT_fn(void);     /* GT(XPTR,YPTR)         (SCL=8)  */
RESULT_t LE_fn(void);     /* LE(XPTR,YPTR)         (SCL=9)  */
RESULT_t LT_fn(void);     /* LT(XPTR,YPTR)         (SCL=10) */
RESULT_t NE_fn(void);     /* NE(XPTR,YPTR)         (SCL=11) */
RESULT_t REMDR_fn(void);  /* REMDR(XPTR,YPTR)→ZPTR (SCL=12) */

/* ── Standalone functions ─────────────────────────────────────────────── */

/* INTEGER(X) — succeeds if X is (or converts to) INTEGER; result in XPTR */
RESULT_t INTGER_fn(void);

/* -X — unary negation; argument via ARGVAL, result in ZPTR */
RESULT_t MNS_fn(void);

/* +X — unary plus; argument via ARGVAL, result in ZPTR */
RESULT_t PLS_fn(void);

#endif /* SIL_ARITH_H */
