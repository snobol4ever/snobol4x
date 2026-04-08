/*
 * patval.c — pattern-valued functions and operations (v311.sil §10)
 *
 * Faithful C translation of v311.sil §10 lines 3119–3322.
 * See patval.h for full API documentation.
 *
 * Source oracle: v311.sil §10
 * Reference C:   snobol4-2.3.3/snobol4.c §10 functions
 *                snobol4-2.3.3/lib/pat.c maknod/cpypat/linkor/lvalue
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M7
 */

#include <stddef.h>

#include "types.h"
#include "data.h"
#include "arena.h"
#include "strings.h"
#include "argval.h"
#include "patval.h"

/* ── Forward declarations ────────────────────────────────────────────── */
extern RESULT_t INVOKE_fn(void);   /* §7 */

/* ── Error stubs ─────────────────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
static RESULT_t intr1(void)  { fprintf(stderr,"patval: illegal data type\n");  exit(1); }
static RESULT_t lenerr(void) { fprintf(stderr,"patval: negative length\n");    exit(1); }
static RESULT_t nemo(void)   { fprintf(stderr,"patval: variable not present\n"); exit(1); }
static RESULT_t noname(void) { fprintf(stderr,"patval: null string\n");        exit(1); }

/* ════════════════════════════════════════════════════════════════════════
 * Pattern node primitives — translated from lib/pat.c
 * All operate on arena offsets (int32_t) not raw pointers.
 * ════════════════════════════════════════════════════════════════════════ */

/* maknod_fn — construct one pattern node in allocated block d2.
 * v311.sil MAKNOD d1,d2,d3,d4,d5,d6 / lib/pat.c maknod()
 *   node[1*DESCR] = d5  (function code)
 *   node[2*DESCR].a = d4.a  (alternate link)
 *   node[2*DESCR].v = d3.a  (successor offset — note: stored in V)
 *   node[3*DESCR].a = d3.a  (min-length)
 *   node[3*DESCR].v = d3.a  (also min-length, per SIL)
 *   node[4*DESCR] = d6  (argument, if non-null)
 *   d1 = d2  (return pointer)
 * NOTE: must store d1 last since d1 may alias d6.                      */
static void maknod_fn(DESCR_t *d1, const DESCR_t *d2, const DESCR_t *d3,
                      const DESCR_t *d4, const DESCR_t *d5, const DESCR_t *d6)
{
    int32_t a2 = d2->a.i;
    *(DESCR_t *)A2P(a2 + 1*DESCR) = *d5;
    ((DESCR_t *)A2P(a2 + 2*DESCR))->a.i = d4->a.i;
    ((DESCR_t *)A2P(a2 + 2*DESCR))->f = 0;
    ((DESCR_t *)A2P(a2 + 2*DESCR))->v = (int32_t)d3->a.i;
    ((DESCR_t *)A2P(a2 + 3*DESCR))->a.i = d3->a.i;
    ((DESCR_t *)A2P(a2 + 3*DESCR))->f = 0;
    ((DESCR_t *)A2P(a2 + 3*DESCR))->v = (int32_t)d3->a.i;
    if (d6) *(DESCR_t *)A2P(a2 + 4*DESCR) = *d6;
    *d1 = *d2; /* last — d1 may alias d6 */
}

/*====================================================================================================================*/
/* linkor_fn — link alternation chains.
 * v311.sil LINKOR d1,d2 / lib/pat.c linkor()
 * Walk the alternate-link chain at d1->a.i, find tail (zero link),
 * point it at d2->a.i.                                                 */
static void linkor_fn(const DESCR_t *d1, const DESCR_t *d2)
{
    int32_t a = d1->a.i;
    int32_t i = 0;
    for (;;) {
        int32_t j = ((DESCR_t *)A2P(a + 2*DESCR + i))->a.i;
        if (j == 0) break;
        i = j;
    }
    ((DESCR_t *)A2P(a + 2*DESCR + i))->a.i = d2->a.i;
}

/*====================================================================================================================*/
/* lvalue_fn — compute minimum match length across alternation chain.
 * v311.sil LVALUE d1,d2 / lib/pat.c lvalue()                          */
static void lvalue_fn(DESCR_t *d1, const DESCR_t *d2)
{
    int32_t a = d2->a.i;
    int32_t offset = ((DESCR_t *)A2P(a + 2*DESCR))->a.i;
    int32_t i = ((DESCR_t *)A2P(a + 3*DESCR))->a.i;
    while (offset != 0) {
        int32_t j = ((DESCR_t *)A2P(a + offset + 3*DESCR))->a.i;
        if (j < i) i = j;
        offset = ((DESCR_t *)A2P(a + offset + 2*DESCR))->a.i;
    }
    d1->a.i = i; d1->f = 0; d1->v = 0;
}

/*====================================================================================================================*/
/* cpypat_fn — copy pattern block with offset fixup.
 * v311.sil CPYPAT d1,d2,d3,d4,d5,d6 / lib/pat.c cpypat()
 * d1=dest base, d2=src base, d3=src offset adjust,
 * d4=dest write offset, d5=successor-link fixup, d6=size              */
static void cpypat_fn(DESCR_t *d1, const DESCR_t *d2, const DESCR_t *d3,
                      const DESCR_t *d4, const DESCR_t *d5, const DESCR_t *d6)
{
    int32_t r1 = d1->a.i;
    int32_t r2 = d2->a.i;
    int32_t r3 = d6->a.i;
    int32_t a3 = d3->a.i;
    int32_t a4 = d4->a.i;
    int32_t a5 = d5->a.i;
#define F1(X) ((X) == 0 ? 0 : ((X) + a4))
#define F2(X) ((X) == 0 ? a5 : ((X) + a4))
    do {
        int32_t v7 = ((DESCR_t *)A2P(r2 + 1*DESCR))->v;
        *(DESCR_t *)A2P(r1 + 1*DESCR) = *(DESCR_t *)A2P(r2 + 1*DESCR);
        int32_t a8 = ((DESCR_t *)A2P(r2 + 2*DESCR))->a.i;
        int32_t v8 = ((DESCR_t *)A2P(r2 + 2*DESCR))->v;
        ((DESCR_t *)A2P(r1 + 2*DESCR))->a.i = F1(a8);
        ((DESCR_t *)A2P(r1 + 2*DESCR))->f = 0;
        ((DESCR_t *)A2P(r1 + 2*DESCR))->v = F2(v8);
        int32_t a9 = ((DESCR_t *)A2P(r2 + 3*DESCR))->a.i;
        int32_t v9 = ((DESCR_t *)A2P(r2 + 3*DESCR))->v;
        ((DESCR_t *)A2P(r1 + 3*DESCR))->a.i = a9 + a3;
        ((DESCR_t *)A2P(r1 + 3*DESCR))->f = 0;
        ((DESCR_t *)A2P(r1 + 3*DESCR))->v = v9 + a3;
        if (v7 == 3)
            *(DESCR_t *)A2P(r1 + 4*DESCR) = *(DESCR_t *)A2P(r2 + 4*DESCR);
        r1 += (v7 + 1) * DESCR;
        r2 += (v7 + 1) * DESCR;
        r3 -= (v7 + 1) * DESCR;
    } while (r3 > 0);
    d1->a.i = r1;
#undef F1
#undef F2
}

/*====================================================================================================================*/
/* ── Fetch next OC descriptor (used by ATOP/NAM/DOL) ────────────────── */
static DESCR_t oc_fetch(void)
{
    OCICL.a.i += DESCR;
    return *(DESCR_t *)A2P(OCBSCL.a.i + OCICL.a.i);
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * CHARZ / ABNSND common path — character-set pattern node builder
 * v311.sil CHARZ/ABNSND (lines ~3143–3160)
 *
 * Stack on entry (top first): function-descriptor YCL, min-length ZCL
 * (CHARZ pushes CHARCL=1; ABNSND uses whatever was pushed before).
 * Evaluates argument via ARGVAL, coerces STRING/EXPR/INTEGER,
 * allocates LNODSZ block, calls maknod.
 * ════════════════════════════════════════════════════════════════════════ */
static RESULT_t charz_abnsnd(const DESCR_t *ycl, const DESCR_t *zcl)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    if (XPTR.v == I) { /* coerce INTEGER → string via GNVARI */
        int32_t off = GNVARI_fn(XPTR.a.i);
        if (off == 0) return FAIL;
        XPTR.a.i = off; XPTR.f = 0; XPTR.v = S;
    } else if (XPTR.v != S && XPTR.v != E) {
        return intr1();
    }
    DESCR_t nulvcl = NULVCL; /* PATNOD: null string → NONAME error */
    if (XPTR.a.i == nulvcl.a.i && XPTR.v == nulvcl.v) return noname();
    int32_t blk = BLOCK_fn(LNODSZ.a.i, P); /* allocate LNODSZ block and construct node */
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    maknod_fn(&ZPTR, &TPTR, zcl, &ZEROCL, ycl, &XPTR);
    return OK; /* result in ZPTR */
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * LPRTND common path — integer-argument pattern functions
 * v311.sil LPRTND (line ~3172)
 * Stack on entry: function-descriptor YCL
 * ════════════════════════════════════════════════════════════════════════ */
static RESULT_t lprtnd(const DESCR_t *ycl)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    ZCL = ZEROCL; /* default min length = 0 */
    if (XPTR.v == E) goto patnod;
    if (XPTR.v == S) {
        LOCSP_fn(&ZSP, &XPTR);
        if (SPCINT_fn(&XPTR, &ZSP) != OK) return intr1();
    } else if (XPTR.v != I) {
        return intr1();
    }
    if (XPTR.a.i < 0) return lenerr(); /* LPRTNI: check non-negative; if LEN, use value as min-length */
    if (!DEQL(*ycl, LNTHCL)) ZCL.a.i = XPTR.a.i; /* MOVA ZCL,XPTR: non-LEN fns use N as min-length; LEN skips (ZCL stays 0) */
patnod:
    { /* null string check — XPTR with zero arena offset and STRING type */
        DESCR_t nulvcl = NULVCL;
        if (XPTR.a.i == nulvcl.a.i && XPTR.v == nulvcl.v) return noname();
    }
    int32_t blk = BLOCK_fn(LNODSZ.a.i, P);
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    maknod_fn(&ZPTR, &TPTR, &ZCL, &ZEROCL, ycl, &XPTR);
    return OK;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * Character-set pattern entry points
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t ANY_fn(void)    { return charz_abnsnd(&ANYCCL, &CHARCL); }
RESULT_t NOTANY_fn(void) { return charz_abnsnd(&NNYCCL, &CHARCL); }
RESULT_t SPAN_fn(void)   { return charz_abnsnd(&SPNCCL, &CHARCL); }
RESULT_t BREAK_fn(void)  { return charz_abnsnd(&BRKCCL, &ZEROCL); }
RESULT_t BREAKX_fn(void) { return charz_abnsnd(&BRXCCL, &ZEROCL); }

/* ════════════════════════════════════════════════════════════════════════
 * Integer-argument pattern entry points
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t LEN_fn(void)  { return lprtnd(&LNTHCL); }
RESULT_t POS_fn(void)  { return lprtnd(&POSICL);  }
RESULT_t RPOS_fn(void) { return lprtnd(&RPSICL); }
RESULT_t RTAB_fn(void) { return lprtnd(&RTBCL);  }
RESULT_t TAB_fn(void)  { return lprtnd(&TBCL);   }

/* ════════════════════════════════════════════════════════════════════════
 * ARBNO_fn — ARBNO(P)
 * v311.sil ARBNO (line ~3185)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t ARBNO_fn(void)
{
    if (PATVAL_fn() == FAIL) return FAIL;
    if (XPTR.v == S) { /* coerce STRING → single-char pattern node */
        LOCSP_fn(&TSP, &XPTR);
        TMVAL.a.i = TSP.l; TMVAL.f = 0; TMVAL.v = 0;
        int32_t blk = BLOCK_fn(LNODSZ.a.i, P);
        if (blk == 0) return FAIL;
        TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
        maknod_fn(&XPTR, &TPTR, &TMVAL, &ZEROCL, &CHRCL, &XPTR);
    } else if (XPTR.v != P) {
        return intr1();
    }
    XSIZ.a.i = x_bksize(XPTR.a.i); XSIZ.f = 0; XSIZ.v = 0; /* ARBP: compute sizes and assemble ARBNO block */
    TSIZ.a.i = XSIZ.a.i + ARBSIZ.a.i;
    TSIZ.f = 0; TSIZ.v = P;
    int32_t blk = BLOCK_fn(TSIZ.a.i, P);
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    ZPTR = TPTR;
    TSIZ.a.i = x_bksize(ARHEAD.a.i); TSIZ.f = 0; TSIZ.v = 0; /* copy ARHEAD, then pattern, then ARTAIL, then ARBACK */
    DESCR_t zero = ZEROCL;
    cpypat_fn(&TPTR, &ARHEAD, &zero, &zero, &zero, &TSIZ);
    ZSIZ.a.i = XSIZ.a.i + TSIZ.a.i;
    cpypat_fn(&TPTR, &XPTR, &zero, &TSIZ, &ZSIZ, &XSIZ);
    TSIZ.a.i = NODSIZ.a.i + NODSIZ.a.i;
    cpypat_fn(&TPTR, &ARTAIL, &zero, &ZSIZ, &zero, &TSIZ);
    ZSIZ.a.i = TSIZ.a.i + ZSIZ.a.i;
    cpypat_fn(&TPTR, &ARBACK, &zero, &ZSIZ, &TSIZ, &TSIZ);
    return OK; /* result in ZPTR */
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * ATOP_fn — @X cursor capture
 * v311.sil ATOP (line ~3237)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t ATOP_fn(void)
{
    YPTR = oc_fetch();
    if (D_F(YPTR) & FNC) {
        INCL = YPTR;
        switch (INVOKE_fn()) {
        default:
            if (YPTR.v != E) return nemo();
            break;
        }
    }
    int32_t blk = BLOCK_fn(LNODSZ.a.i, P);
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    maknod_fn(&ZPTR, &TPTR, &ZEROCL, &ZEROCL, &ATOPCL, &YPTR);
    return OK;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * NAM_fn / DOL_fn — X.Y and X$Y value-assignment operators
 * v311.sil NAM/DOL (line ~3253)
 * ════════════════════════════════════════════════════════════════════════ */
static RESULT_t nam_dol(const DESCR_t *op_cl)
{
    if (PATVAL_fn() == FAIL) return FAIL; /* get pattern for first argument */
    YPTR = oc_fetch(); /* get second argument from OC stream */
    if (D_F(YPTR) & FNC) {
        DESCR_t saved_xptr = XPTR;
        INCL = YPTR;
        int rc = INVOKE_fn();
        XPTR = saved_xptr; /* NAM4: restore first argument */
        if (rc == FAIL) return FAIL;
        if (rc == OK) goto nam3; /* OK = exit 2: name result → restore XPTR (oracle: NAM4), join NAM3 */
        if (YPTR.v != E) return nemo(); /* OK path: only EXPRESSION valid */
    }
nam3:;
    if (XPTR.v == S) { /* NAM3: coerce STRING first-arg to pattern node if needed */
        LOCSP_fn(&TSP, &XPTR); /* NAMV: convert string to pattern */
        TMVAL.a.i = TSP.l; TMVAL.f = 0; TMVAL.v = 0;
        int32_t blk = BLOCK_fn(LNODSZ.a.i, P);
        if (blk == 0) return FAIL;
        TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
        maknod_fn(&XPTR, &TPTR, &TMVAL, &ZEROCL, &CHRCL, &XPTR);
    } else if (XPTR.v != P) {
        return intr1();
    }
    int32_t blk; /* NAMP: build the compound naming pattern */
    blk = BLOCK_fn(SNODSZ.a.i, P); /* allocate naming node (SNODSZ) */
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    maknod_fn(&WPTR, &TPTR, &ZEROCL, &ZEROCL, &NMECL, NULL);
    blk = BLOCK_fn(LNODSZ.a.i, P); /* allocate backup node (LNODSZ) */
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    DESCR_t tval = *op_cl;
    maknod_fn(&YPTR, &TPTR, &ZEROCL, &ZEROCL, &tval, &YPTR);
    XSIZ.a.i = x_bksize(XPTR.a.i); XSIZ.f = 0; XSIZ.v = 0; /* compute sizes */
    YSIZ.a.i = XSIZ.a.i + NODSIZ.a.i;
    TSIZ.a.i = x_bksize(YPTR.a.i); TSIZ.f = 0; TSIZ.v = 0;
    ZSIZ.a.i = YSIZ.a.i + TSIZ.a.i;
    ZSIZ.v = P;
    blk = BLOCK_fn(ZSIZ.a.i, P); /* allocate final block */
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    ZPTR = TPTR;
    lvalue_fn(&TVAL, &XPTR); /* copy three sub-patterns with offset fixup */
    DESCR_t nodsiz = NODSIZ;
    cpypat_fn(&TPTR, &WPTR, &TVAL, &ZEROCL, &nodsiz, &nodsiz);
    cpypat_fn(&TPTR, &XPTR, &ZEROCL, &nodsiz, &YSIZ, &XSIZ);
    cpypat_fn(&TPTR, &YPTR, &ZEROCL, &YSIZ, &ZEROCL, &TSIZ);
    return OK;
}

/*====================================================================================================================*/
RESULT_t NAM_fn(void) { return nam_dol(&ENMECL); }
RESULT_t DOL_fn(void) { return nam_dol(&ENMICL); }

/* ════════════════════════════════════════════════════════════════════════
 * OR_fn — X | Y alternation
 * v311.sil OR (line ~3293)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t OR_fn(void)
{
    if (PATVAL_fn() == FAIL) return FAIL; /* get first argument, save; get second into YPTR */
    DESCR_t first = XPTR;
    if (PATVAL_fn() == FAIL) return FAIL;
    YPTR = XPTR;
    XPTR = first;
    if (XPTR.v == S) { /* coerce STRING X to single-char pattern node */
        LOCSP_fn(&XSP, &XPTR);
        TMVAL.a.i = XSP.l; TMVAL.f = 0; TMVAL.v = 0;
        int32_t blkx = BLOCK_fn(LNODSZ.a.i, P);
        if (blkx == 0) return FAIL;
        TPTR.a.i = blkx; TPTR.f = 0; TPTR.v = P;
        maknod_fn(&XPTR, &TPTR, &TMVAL, &ZEROCL, &CHRCL, &XPTR);
    }
    if (YPTR.v == S) { /* coerce STRING Y to single-char pattern node */
        LOCSP_fn(&YSP, &YPTR);
        TMVAL.a.i = YSP.l; TMVAL.f = 0; TMVAL.v = 0;
        int32_t blky = BLOCK_fn(LNODSZ.a.i, P);
        if (blky == 0) return FAIL;
        TPTR.a.i = blky; TPTR.f = 0; TPTR.v = P;
        maknod_fn(&YPTR, &TPTR, &TMVAL, &ZEROCL, &CHRCL, &YPTR);
    }
    if (XPTR.v != P || YPTR.v != P) return intr1();
    XSIZ.a.i = x_bksize(XPTR.a.i); XSIZ.f = 0; XSIZ.v = 0; /* ORPP: assemble alternation block */
    YSIZ.a.i = x_bksize(YPTR.a.i); YSIZ.f = 0; YSIZ.v = 0;
    TSIZ.a.i = XSIZ.a.i + YSIZ.a.i; TSIZ.f = 0; TSIZ.v = P;
    int32_t blk = BLOCK_fn(TSIZ.a.i, P);
    if (blk == 0) return FAIL;
    TPTR.a.i = blk; TPTR.f = 0; TPTR.v = P;
    ZPTR = TPTR;
    DESCR_t zero = ZEROCL;
    cpypat_fn(&TPTR, &XPTR, &zero, &zero, &zero, &XSIZ);
    cpypat_fn(&TPTR, &YPTR, &zero, &XSIZ, &zero, &YSIZ);
    linkor_fn(&ZPTR, &XSIZ);
    return OK;
}

/* ── Scalar-ABI wrappers for scan.c ─────────────────────────────────
 * scan.c was written against raw int32_t offset args (old SIL ABI).
 * These thin wrappers bridge between the two conventions.
 * Exported (non-static); declared in patval.h.
 */

/* maknod_scalar — allocate one pattern node into *out_descr.
 *   blk_off : arena offset of block base (BLOCK_fn result)
 *   len_val : length field (integer value, stored in d3 slot)
 *   alt_val : alternate/link field (integer value, d4 slot)
 *   fn_idx  : function-index integer (d5 slot — e.g. SCAN_IDX_CHR)
 *   arg_off : argument arena offset (d6 slot)
 */
void maknod_scalar(DESCR_t *out, int32_t blk_off,
                   int32_t len_val, int32_t alt_val,
                   int32_t fn_idx, int32_t arg_off)
{
    DESCR_t d2 = ZEROCL; d2.a.i = blk_off;
    DESCR_t d3 = ZEROCL; d3.a.i = len_val;
    DESCR_t d4 = ZEROCL; d4.a.i = alt_val;
    DESCR_t d5 = ZEROCL; d5.a.i = fn_idx;
    DESCR_t d6 = ZEROCL; d6.a.i = arg_off;
    maknod_fn(out, &d2, &d3, &d4, &d5, &d6);
}

/* lvalue_scalar — return minimum match length for pattern at pat_off. */
int32_t lvalue_scalar(int32_t pat_off)
{
    DESCR_t d1 = ZEROCL;
    DESCR_t d2 = ZEROCL; d2.a.i = pat_off;
    lvalue_fn(&d1, &d2);
    return d1.a.i;
}

/* cpypat_scalar — copy count pattern nodes from src_off+src_base into
 *   dst_off+dst_base, applying link fixup of link_val.
 */
void cpypat_scalar(int32_t dst_off, int32_t src_off,
                   int32_t link_val, int32_t dst_base,
                   int32_t src_base, int32_t count)
{
    DESCR_t d1 = ZEROCL; d1.a.i = dst_off;
    DESCR_t d2 = ZEROCL; d2.a.i = src_off;
    DESCR_t d3 = ZEROCL; d3.a.i = link_val;
    DESCR_t d4 = ZEROCL; d4.a.i = dst_base;
    DESCR_t d5 = ZEROCL; d5.a.i = src_base;
    DESCR_t d6 = ZEROCL; d6.a.i = count;
    cpypat_fn(&d1, &d2, &d3, &d4, &d5, &d6);
}
