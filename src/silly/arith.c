/*
 * arith.c — arithmetic operations, predicates, and functions (v311.sil §9)
 *
 * Faithful C translation of v311.sil §9 lines 2923–3118.
 * See arith.h for full API documentation.
 *
 * Source oracle: v311.sil §9
 * Reference C:   snobol4-2.3.3/snobol4.c ARITH/INTGER/MNS/PLS
 *                snobol4-2.3.3/lib/generic/expops.c expint/exreal
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M5
 */

#include <math.h>
#include <stdint.h>

#include "types.h"
#include "data.h"
#include "strings.h"
#include "arith.h"

/* ── Forward declarations for §8 functions (M6) ─────────────────────── */
extern RESULT_t XYARGS_fn(void);   /* v311.sil §8 XYARGS — eval X and Y args */
extern RESULT_t ARGVAL_fn(void);   /* v311.sil §8 ARGVAL — eval one arg      */

/* DTCL: data type pair descriptor — declared in data.h */

/* ── Math error flag (mirrors snobol4-2.3.3 volatile math_error) ─────── */
static volatile int math_error_flag;
#define CLR_MATH_ERROR()  (math_error_flag = 0)
#define MATH_ERROR()      (math_error_flag)
#define RMATH_ERROR(r)    (MATH_ERROR() || !isfinite((double)(r)))

/* ── Error helpers — map to fatal/fail exits ─────────────────────────── */
/* These mirror AERROR (errtyp=2) and INTR1 (errtyp=1) in v311.sil.
 * For now they abort; the error subsystem (M22+) will replace these.  */
#include <stdio.h>
#include <stdlib.h>
static RESULT_t aerror(void) {
    fprintf(stderr, "arith: arithmetic error (overflow/div-zero)\n");
    exit(2);
}
static RESULT_t intr1(void) {
    fprintf(stderr, "arith: illegal data type\n");
    exit(1);
}

/* ── Integer exponentiation (from lib/generic/expops.c expint) ───────── */
/* Returns 1 on success (result in *res), 0 on overflow/0**neg.          */
static int expint(DESCR_t *res, const DESCR_t *x, const DESCR_t *y)
{
    int32_t ix = x->a.i, iy = y->a.i;
    int32_t p;
    if (ix == 0 && iy < 0) return 0;
    if (iy < 0) {
        p = 0;
    } else {
#define SIGN32 ((uint32_t)1u << 31)
#define MULT32(X, Y) do { \
    int32_t _t = (X) * (Y); \
    if (((uint32_t)((X) ^ (Y) ^ _t)) & SIGN32) return 0; \
    (X) = _t; \
} while (0)
        p = 1;
        for (;;) {
            if (iy & 1) MULT32(p, ix);
            iy >>= 1;
            if (iy == 0) break;
            MULT32(ix, ix);
        }
#undef SIGN32
#undef MULT32
    }
    *res = *x;
    res->a.i = p;
    return 1;
}

/*====================================================================================================================*/
/* ── Real exponentiation (from lib/generic/expops.c exreal) ─────────── */
static int exreal(DESCR_t *res, const DESCR_t *x, const DESCR_t *y)
{
    real_t r;
    CLR_MATH_ERROR();
    r = (real_t)pow((double)x->a.f, (double)y->a.f);
    if (MATH_ERROR()) return 0;
    *res = *x;
    res->a.f = r;
    return 1;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * ARITH_fn — shared dispatcher for all binary arithmetic/compare ops
 *
 * v311.sil ARITH (line ~2976):
 *   PUSH SCL / RCALL XYARGS / POP SCL
 *   Build DTCL = (V(XPTR) in A, V(YPTR) in V)
 *   Dispatch on DTCL to type-coercion block
 *   Then dispatch on SCL to actual operation
 * ════════════════════════════════════════════════════════════════════════ */
static RESULT_t ARITH_fn(void)
{
    if (XYARGS_fn() == FAIL) return FAIL; /* ── evaluate arguments (§8 XYARGS) ── */
    DTCL.a.i = XPTR.v; /* ── build type-pair ── */
    DTCL.f = 0;
    DTCL.v = YPTR.v;
coerce: /* ── type coercion: promote strings / convert int↔real ── */
    if (DTCL.a.i == I && DTCL.v == I) goto do_ii; /* INTEGER × INTEGER */
    if (DTCL.a.i == I && DTCL.v == S) { /* INTEGER × STRING */
        LOCSP_fn(&YSP, &YPTR);
        if (SPCINT_fn(&YPTR, &YSP) == OK) { DTCL.v = I; goto do_ii; }
        if (SPREAL_fn(&YPTR, &YSP) == OK) { DTCL.v = R; goto do_ir; }
        return intr1();
    }
    if (DTCL.a.i == S && DTCL.v == I) { /* STRING × INTEGER */
        LOCSP_fn(&XSP, &XPTR);
        if (SPCINT_fn(&XPTR, &XSP) == OK) { DTCL.a.i = I; goto do_ii; }
        if (SPREAL_fn(&XPTR, &XSP) == OK) { DTCL.a.i = R; goto do_ri; }
        return intr1();
    }
    if (DTCL.a.i == S && DTCL.v == S) { /* STRING × STRING */
        LOCSP_fn(&XSP, &XPTR);
        if (SPCINT_fn(&XPTR, &XSP) == OK) { DTCL.a.i = I; goto coerce; }
        if (SPREAL_fn(&XPTR, &XSP) == OK) { DTCL.a.i = R; goto coerce; }
        return intr1();
    }
    if (DTCL.a.i == R && DTCL.v == R) goto do_rr; /* REAL × REAL */
    if (DTCL.a.i == I && DTCL.v == R) { /* INTEGER × REAL */
do_ir: YPTR.a.f = (real_t)YPTR.a.i; YPTR.f = 0; YPTR.v = R; goto do_rr;
    }
    if (DTCL.a.i == R && DTCL.v == I) { /* REAL × INTEGER */
do_ri: XPTR.a.f = (real_t)XPTR.a.i; XPTR.f = 0; XPTR.v = R; goto do_rr;
    }
    if (DTCL.a.i == S && DTCL.v == R) { /* STRING × REAL */
        LOCSP_fn(&XSP, &XPTR);
        if (SPCINT_fn(&XPTR, &XSP) == OK) goto do_ir;
        if (SPREAL_fn(&XPTR, &XSP) == OK) goto do_rr;
        return intr1();
    }
    if (DTCL.a.i == R && DTCL.v == S) { /* REAL × STRING */
        LOCSP_fn(&YSP, &YPTR);
        if (SPCINT_fn(&YPTR, &YSP) == OK) goto do_ri;
        if (SPREAL_fn(&YPTR, &YSP) == OK) goto do_rr;
        return intr1();
    }
    return intr1();
do_ii: /* ── INTEGER × INTEGER operations ── */
    switch (SCL.a.i) {
    case 1: /* ADD */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.i += YPTR.a.i;
        if (MATH_ERROR()) return aerror();
        break;
    case 2: /* DIV */
        if (YPTR.a.i == 0) return aerror();
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.i /= YPTR.a.i;
        if (MATH_ERROR()) return aerror();
        break;
    case 3: /* EXPOP (integer) */
        if (!expint(&ZPTR, &XPTR, &YPTR)) return aerror();
        break;
    case 4: /* MPY */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.i *= YPTR.a.i;
        if (MATH_ERROR()) return aerror();
        break;
    case 5: /* SUB */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.i -= YPTR.a.i;
        if (MATH_ERROR()) return aerror();
        break;
    case 6: return (XPTR.a.i == YPTR.a.i) ? OK : FAIL; /* EQ  */
    case 7: return (XPTR.a.i >= YPTR.a.i) ? OK : FAIL; /* GE  */
    case 8: return (XPTR.a.i > YPTR.a.i) ? OK : FAIL; /* GT  */
    case 9: return (XPTR.a.i <= YPTR.a.i) ? OK : FAIL; /* LE  */
    case 10: return (XPTR.a.i < YPTR.a.i) ? OK : FAIL; /* LT  */
    case 11: return (XPTR.a.i != YPTR.a.i) ? OK : FAIL; /* NE  */
    case 12: /* REMDR */
        if (YPTR.a.i == 0) return aerror();
        CLR_MATH_ERROR();
        WPTR = XPTR; WPTR.a.i /= YPTR.a.i;
        if (MATH_ERROR()) return aerror();
        ZPTR = XPTR; ZPTR.a.i -= WPTR.a.i * YPTR.a.i;
        break;
    default: return intr1();
    }
    ARTHCL.a.i++;
    return OK;
do_rr: /* ── REAL × REAL operations ── */
    switch (SCL.a.i) {
    case 1: /* ADD */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.f += YPTR.a.f;
        if (RMATH_ERROR(ZPTR.a.f)) return aerror();
        break;
    case 2: /* DIV */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.f /= YPTR.a.f;
        if (RMATH_ERROR(ZPTR.a.f)) return aerror();
        break;
    case 3: /* EXPOP (real) */
        if (!exreal(&ZPTR, &XPTR, &YPTR)) return aerror();
        break;
    case 4: /* MPY */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.f *= YPTR.a.f;
        if (RMATH_ERROR(ZPTR.a.f)) return aerror();
        break;
    case 5: /* SUB */
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.f -= YPTR.a.f;
        if (RMATH_ERROR(ZPTR.a.f)) return aerror();
        break;
    case 6: return (XPTR.a.f == YPTR.a.f) ? OK : FAIL; /* EQ  */
    case 7: return (XPTR.a.f >= YPTR.a.f) ? OK : FAIL; /* GE  */
    case 8: return (XPTR.a.f > YPTR.a.f) ? OK : FAIL; /* GT  */
    case 9: return (XPTR.a.f <= YPTR.a.f) ? OK : FAIL; /* LE  */
    case 10: return (XPTR.a.f < YPTR.a.f) ? OK : FAIL; /* LT  */
    case 11: return (XPTR.a.f != YPTR.a.f) ? OK : FAIL; /* NE  */
    case 12: return intr1(); /* REMDR undefined for REAL */
    default: return intr1();
    }
    ARTHCL.a.i++;
    return OK;
}

/*====================================================================================================================*/
/* ── Entry point shims: set SCL, call ARITH_fn ───────────────────────── */
RESULT_t ADD_fn(void)   { SCL.a.i = 1;  return ARITH_fn(); }
RESULT_t DIV_fn(void)   { SCL.a.i = 2;  return ARITH_fn(); }
RESULT_t EXPOP_fn(void) { SCL.a.i = 3;  return ARITH_fn(); }
RESULT_t MPY_fn(void)   { SCL.a.i = 4;  return ARITH_fn(); }
RESULT_t SUB_fn(void)   { SCL.a.i = 5;  return ARITH_fn(); }
RESULT_t EQ_fn(void)    { SCL.a.i = 6;  return ARITH_fn(); }
RESULT_t GE_fn(void)    { SCL.a.i = 7;  return ARITH_fn(); }
RESULT_t GT_fn(void)    { SCL.a.i = 8;  return ARITH_fn(); }
RESULT_t LE_fn(void)    { SCL.a.i = 9;  return ARITH_fn(); }
RESULT_t LT_fn(void)    { SCL.a.i = 10; return ARITH_fn(); }
RESULT_t NE_fn(void)    { SCL.a.i = 11; return ARITH_fn(); }
RESULT_t REMDR_fn(void) { SCL.a.i = 12; return ARITH_fn(); }

/* ════════════════════════════════════════════════════════════════════════
 * INTGER_fn — INTEGER(X) conversion function
 * v311.sil INTGER (line ~3050)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t INTGER_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    if (XPTR.v == I) return OK;
    if (XPTR.v != S) return FAIL;
    LOCSP_fn(&XSP, &XPTR);
    return SPCINT_fn(&XPTR, &XSP);
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * MNS_fn — unary negation (-X)
 * v311.sil MNS (line ~3068)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t MNS_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    if (XPTR.v == I) {
        CLR_MATH_ERROR();
        ZPTR = XPTR; ZPTR.a.i = -ZPTR.a.i;
        if (MATH_ERROR()) return aerror();
        ARTHCL.a.i++;
        return OK;
    }
    if (XPTR.v == S) {
        LOCSP_fn(&XSP, &XPTR);
        if (SPCINT_fn(&XPTR, &XSP) == OK) {
            CLR_MATH_ERROR();
            ZPTR = XPTR; ZPTR.a.i = -ZPTR.a.i;
            if (MATH_ERROR()) return aerror();
            ARTHCL.a.i++;
            return OK;
        }
        if (SPREAL_fn(&XPTR, &XSP) == FAIL) return intr1();
    } /* fall through to REAL case */
    if (XPTR.v == R) {
        ZPTR = XPTR; ZPTR.a.f = -ZPTR.a.f;
        ARTHCL.a.i++;
        return OK;
    }
    return intr1();
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * PLS_fn — unary plus (+X)
 * v311.sil PLS (line ~3091)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t PLS_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    ZPTR = XPTR; /* ARGVAL leaves result in XPTR per §8 convention;
                      result goes in ZPTR; PLS uses ZPTR as output (SIL: RCALL ZPTR,ARGVAL) */
    if (ZPTR.v == I || ZPTR.v == R) { ARTHCL.a.i++; return OK; }
    if (ZPTR.v == S) {
        LOCSP_fn(&XSP, &ZPTR);
        if (SPCINT_fn(&ZPTR, &XSP) == OK) { ARTHCL.a.i++; return OK; }
        if (SPREAL_fn(&ZPTR, &XSP) == OK) { ARTHCL.a.i++; return OK; }
        return intr1();
    }
    return intr1();
}
