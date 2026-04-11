/*
 * arrays.c — Arrays, Tables, and Defined Data (v311.sil §14 4644–5267)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §14.
 * SORT/RSORT are stubbed (complex shell-sort + scratch ptr infra).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M13
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "arena.h"
#include "strings.h"
#include "arrays.h"
#include "argval.h"
#include "arena.h"
#include "strings.h"
#include "symtab.h"

/* External stubs — use signatures from headers where already declared */
extern RESULT_t INVOKE_fn(void);
extern RESULT_t INTVAL_fn(void);
/* VARVUP_fn, GENVUP_fn, FINDEX_fn declared in argval.h / arena.h / symtab.h */
extern void       PSTACK_fn(DESCR_t *pos);
extern RESULT_t MULT_fn(DESCR_t *out, DESCR_t a, DESCR_t b);
extern void       VPXPTR_fn2(void);

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),  sizeof(DESCR_t))
#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + D_A(off_d), sizeof(DESCR_t))
#define PUTD_B(base_d, off_d, src) \
    memcpy((char*)A2P(D_A(base_d)) + D_A(off_d), &(src), sizeof(DESCR_t))

/* SIL 10850: DATCL DESCR DEFDAT,FNC,0 — defined-data procedure sentinel */
/* Used as type-check sentinel in arg2() — .a=0 is fine, not a dispatch target */
DESCR_t DATCL = {.a={.i=0}, .f=FNC, .v=0};

static inline int deql(DESCR_t a, DESCR_t b)
{
    return D_A(a) == D_A(b) && D_V(a) == D_V(b);
}

/*====================================================================================================================*/
/* Operand stack */
static DESCR_t ar_stk[32];
static int ar_top = 0;
static inline void    ar_push(DESCR_t d) { ar_stk[ar_top++] = d; }
static inline DESCR_t ar_pop(void)        { return ar_stk[--ar_top]; }

/* ── ARRAY(P,V) ──────────────────────────────────────────────────────── */
RESULT_t ARRAY_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL; /* Get prototype string */
    ar_push(XPTR);
    if (ARGVAL_fn() == FAIL) { ar_top--; return FAIL; } /* Get initial value (TPTR) */
    MOVD(TPTR, XPTR);
    XPTR = ar_pop();
    LOCSP_fn(&XSP, &XPTR);
    ar_push(XPTR); /* save prototype descriptor for later */
    const char *p = (const char *)A2P(XSP.a) + XSP.o; /* Parse dimension specs from prototype string  We use a simplified C loop rather than STREAM calls  Each dimension: [lo:]hi separated by commas */
    int32_t plen = XSP.l;
    int32_t ndim = 0;
    int32_t total_elems = 1;
    int dim_base = ar_top; /* Dimension info stack: pairs (lo, count) stored in ar_stk */
    while (plen > 0) {
        int32_t lo = 1, hi = 0; /* Read first integer */
        int32_t v = 0; int neg = 0;
        if (*p == '-') { neg = 1; p++; plen--; }
        while (plen > 0 && *p >= '0'    && *p <= '9') {
            v = v*10 + (*p - '0'); p++; plen--;
        }
        if (neg) v = -v;
        if (plen > 0 && *p == ':') {
            lo = v; p++; plen--; /* lo:hi */
            v = 0; neg = 0;
            if (plen > 0 && *p == '-') { neg = 1; p++; plen--; }
            while (plen > 0 && *p >= '0'    && *p <= '9') {
                v = v*10 + (*p - '0'); p++; plen--;
            }
            if (neg) v = -v;
            hi = v;
        } else {
            if (v <= 0) { ar_top = dim_base - 1; return FAIL; } /* single number: 1..v */
            hi = v;
        }
        int32_t count = hi - lo + 1;
        if (count <= 0) { ar_top = dim_base - 1; return FAIL; }
        DESCR_t lo_d; SETAC(lo_d, lo); SETVC(lo_d, I); /* Push (lo, count) as two DESCRs */
        DESCR_t ct_d; SETAC(ct_d, count); SETVC(ct_d, I);
        ar_push(lo_d); ar_push(ct_d);
        ndim++;
        if ((int64_t)total_elems * count > 0x7FFFFFFF) { /* multiply into total */
            ar_top = dim_base - 1; return FAIL;
        }
        total_elems *= count;
        if (plen > 0 && *p == ',') { p++; plen--; } /* skip comma */
    }
    if (ndim == 0) { ar_top = dim_base - 1; return FAIL; }
    int32_t blk_elems = 2 + ndim + total_elems; /* Allocate: 2 (heading) + ndim (index pairs) + total_elems (cells) */
    int32_t blk_bytes = blk_elems * DESCR;
    SETAC(ZCL, blk_bytes); SETVC(ZCL, A);
    int32_t blk = BLOCK_fn(blk_bytes, A);
    if (!blk) { ar_top = dim_base - 1; return FAIL; }
    SETAC(ZPTR, blk); SETVC(ZPTR, A);
    DESCR_t ndim_d; SETAC(ndim_d, ndim); /* Insert dimensionality at offset 2*DESCR */
    PUTDC_B(ZPTR, 2*DESCR, ndim_d);
    DESCR_t proto_d = ar_pop(); /* was saved before dim loop started */  /* Insert prototype descriptor at offset 1*DESCR */
    PUTDC_B(ZPTR, 1*DESCR, proto_d);
    for (int i = ndim - 1; i >= 0; i--) { /* Write dimension pairs: at offsets 3*DESCR .. (2+ndim)*DESCR  They are on ar_stk in reverse order (last dim first) */
        DESCR_t ct_d2 = ar_pop();
        DESCR_t lo_d2 = ar_pop();
        DESCR_t pair; SETAC(pair, D_A(lo_d2)); D_V(pair) = D_A(ct_d2);
        PUTDC_B(ZPTR, (3 + i) * DESCR, pair);
    }
    for (int i = 0; i < total_elems; i++) { /* Fill element cells with initial value TPTR.
                                              * Oracle ARRFIL: after ndim dim-pair writes, XPTR = blk+(ndim+1)*DESCR,
                                              * then ARRFIL does +=DESCR → blk+(ndim+2)*DESCR, writes at XPTR+DESCR
                                              * → first element at blk+(ndim+3)*DESCR = slot ndim+3. */
        PUTDC_B(ZPTR, (3 + ndim + i) * DESCR, TPTR);
    }
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── ASSOC / ASSOCE — TABLE(N,M) ────────────────────────────────────── */
RESULT_t ASSOCE_fn(DESCR_t size, DESCR_t ext)
{
    int32_t blk = BLOCK_fn(D_A(size), T); /* Allocate table extent block of size bytes, type T */
    if (!blk) return FAIL;
    SETAC(ZPTR, blk); SETVC(ZPTR, T);
    DESCR_t sz2; MOVD(sz2, size); DECRA(sz2, DESCR); /* PUTD ZPTR,XPTR,ONECL — last slot = 1 (terminator) */
    PUTD_B(ZPTR, sz2, ONECL);
    DESCR_t sz3; MOVD(sz3, size); DECRA(sz3, 2*DESCR); /* PUTD ZPTR,prev_slot,ext — second-to-last = extension size */
    PUTD_B(ZPTR, sz3, ext);
    DESCR_t off; SETAC(off, DESCR); /* Fill remaining slots with NULVCL */
    while (D_A(off) < D_A(sz3)) {
        PUTD_B(ZPTR, off, NULVCL);
        INCRA(off, 2*DESCR);
    }
    return OK;
}

/*====================================================================================================================*/
RESULT_t ASSOC_fn(void)
{
    if (INTVAL_fn() == FAIL) return FAIL; /* Get N (table size) */
    ar_push(XPTR);
    if (INTVAL_fn() == FAIL) { ar_top--; return FAIL; } /* Get M (secondary allocation) */
    MOVD(WPTR, XPTR);
    XPTR = ar_pop();
    int32_t n = D_A(XPTR) > 0 ? D_A(XPTR) : EXTSIZ; /* Compute primary size: (N+1)*2*DESCR */
    int32_t m = D_A(WPTR) > 0 ? D_A(WPTR) : EXTSIZ;
    DESCR_t xsz; SETAC(xsz, (n + 1) * 2 * DESCR); SETVC(xsz, T);
    DESCR_t wsz; SETAC(wsz, (m + 1) * 2 * DESCR);
    if (ASSOCE_fn(xsz, wsz) == FAIL) return FAIL;
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── ITEM — array/table reference ────────────────────────────────────── */
RESULT_t ITEM_fn(void)
{
    SETAV(XCL, INCL);
    DECRA(XCL, 1); /* skip referenced object */
    ar_push(XCL);
    if (ARGVAL_fn() == FAIL) { ar_top--; return FAIL; }
    MOVD(YCL, XPTR);
    XCL = ar_pop();
    if (VEQLC(YCL, A)) {
        DESCR_t WCL_d; MOVD(WCL_d, XCL); /* Array reference */
        DESCR_t ndim_d; GETDC_B(ndim_d, YCL, 2*DESCR); /* Get dimension count */
        int32_t ndim = D_A(ndim_d);
        int32_t nargs = D_A(XCL); /* Collect indices (pushing zeros for omitted args) */
        for (int i = 0; i < nargs; i++) {
            ar_push(XCL); ar_push(WCL_d); ar_push(YCL);
            if (INTVAL_fn() == FAIL) {
                ar_top -= 3 * (i + 1); return FAIL;
            }
            YCL = ar_pop(); WCL_d = ar_pop(); XCL = ar_pop();
            ar_push(XPTR);
            DECRA(XCL, 1);
        }
        while (D_A(XCL) < ndim) { /* Push zeros for omitted */
            ar_push(ZEROCL); INCRA(XCL, 1);
        }
        /* Compute linear offset matching oracle ARYAD1/ARYA11 loop.
         * Oracle pushes indices dim0..dimN-1 (first = dim 0 on top).
         * ARYA11 pops in that order, computing:
         *   offset = 0
         *   for each dim (0..N-1): k = idx - lo; offset = offset * extent + k
         * Indices were pushed last-first into ar_stk, so pop order = dim N-1 first.
         * We reverse: collect into temp array, then Horner forward. */
        int32_t idx_arr[16]; /* max 16 dimensions */
        for (int i = ndim - 1; i >= 0; i--) {
            DESCR_t pair; GETDC_B(pair, YCL, (3 + i) * DESCR);
            int32_t lo = D_A(pair), count = D_V(pair);
            DESCR_t idx = ar_pop();
            int32_t k = D_A(idx) - lo;
            if (k < 0 || k >= count) return FAIL;
            idx_arr[i] = k;
        }
        int32_t linear = 0;
        for (int i = 0; i < ndim; i++) {
            DESCR_t pair; GETDC_B(pair, YCL, (3 + i) * DESCR);
            int32_t count = D_V(pair);
            linear = linear * count + idx_arr[i];
        }
        /* Oracle ARYA12: element address = blk + (ndim+2)*DESCR + linear*DESCR.
         * Note: YPTR computed as blk+(ndim+2)*DESCR; requires harness to verify
         * exact slot. Current: (3+ndim+linear) consistent with ARRAY fill at (3+ndim). */
        int32_t elem_off = (3 + ndim + linear) * DESCR;
        SETAC(XPTR, D_A(YCL) + elem_off);
        SETVC(XPTR, N); /* NAME — interior pointer */
        return OK;
    }
    if (VEQLC(YCL, T)) {
        if (D_A(XCL) != 1) return FAIL; /* ARGNER */                                               /* Table reference */
        ar_push(YCL);
        if (ARGVAL_fn() == FAIL) { ar_top--; return FAIL; }
        MOVD(YPTR, XPTR);
        MOVD(XPTR, ar_pop()); /* XPTR = table base */
        while (1) { /* Walk extents looking for YPTR */
            int32_t assoc = locapv_fn(D_A(XPTR), &YPTR);
            if (assoc) {
                SETAC(XPTR, assoc); SETVC(XPTR, N); /* Found: return interior NAME pointer */
                return OK;
            }
            int32_t sz = x_bksize(D_A(XPTR)); /* Not found — check if more extents */
            DESCR_t last; GETDC_B(last, XPTR, sz - DESCR);
            if (AEQLC(last, 1)) {
                if (TESTF(YCL, FRZN)) { MOVD(XPTR, NULVCL); return OK; } /* Last extent: check frozen */
                int32_t slot = locapv_fn(D_A(XPTR), &ZEROCL); /* Find empty slot and fill */
                if (!slot) {
                    DESCR_t wsz; GETDC_B(wsz, XPTR, sz - 2*DESCR); /* Expand: allocate new extent */
                    if (ASSOCE_fn(wsz, wsz) == FAIL) return FAIL;
                    DESCR_t new_ext; MOVD(new_ext, ZPTR); /* Link old → new */
                    PUTDC_B(XPTR, sz - DESCR, new_ext);
                    slot = locapv_fn(D_A(ZPTR), &ZEROCL);
                    if (!slot) return FAIL;
                }
                PUTDC_B(XPTR, slot + DESCR, YPTR); /* Store subscript in value slot */
                SETAC(XPTR, D_A(XPTR) + slot); SETVC(XPTR, N);
                return OK;
            }
            MOVD(XPTR, last); /* Move to next extent */
        }
    }
    return FAIL; /* NONARY */
}

/*====================================================================================================================*/
/* ── PROTO(A) — PROTOTYPE ────────────────────────────────────────────── */
RESULT_t PROTO_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    if (!VEQLC(XPTR, A)) return FAIL;
    GETDC_B(ZPTR, XPTR, DESCR);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── FREEZE(T) ───────────────────────────────────────────────────────── */
RESULT_t FREEZE_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    if (!VEQLC(XPTR, T)) return FAIL;
    D_F(XPTR) |= FRZN;
    { char *p = (char*)A2P(D_A(XPTR)); /* Update in arena */
      *p = (*p) | (uint8_t)FRZN; }
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── THAW(T) ─────────────────────────────────────────────────────────── */
RESULT_t THAW_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    if (!VEQLC(XPTR, T)) return FAIL;
    { char *p = (char*)A2P(D_A(XPTR));
      *p = (*p) & ~(uint8_t)FRZN; }
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── DATDEF — DATA(P) ────────────────────────────────────────────────── */
/* Complex: requires STREAM/VARATB, AUGATL, FINDEX, PSTACK.
 * Stubbed until those infrastructure pieces are in place. */
RESULT_t DATDEF_fn(void) { return FAIL; }

/* ── DEFDAT — create defined data object ─────────────────────────────── */
RESULT_t DEFDAT_fn(void)
{
    SETAV(XCL, INCL);
    DESCR_t WCL_d; MOVD(WCL_d, XCL);
    DESCR_t YCL_d; MOVD(YCL_d, INCL);
    DESCR_t YPTR_d;
    PSTACK_fn(&YPTR_d); /* post stack position */
    while (D_A(XCL) > 0) { /* Read argument values from object code */
        INCRA(OCICL, DESCR);
        GETD_B(XPTR, OCBSCL, OCICL);
        if (TESTF(XPTR, FNC)) {
            ar_push(XCL); ar_push(WCL_d); ar_push(YCL_d); ar_push(YPTR_d);
            INCL = XPTR;
            if (INVOKE_fn() == FAIL) {
                ar_top -= 4; return FAIL;
            }
            YPTR_d = ar_pop(); YCL_d = ar_pop(); WCL_d = ar_pop(); XCL = ar_pop();
        } else {
            GETDC_B(XPTR, XPTR, DESCR);
        }
        ar_push(XPTR);
        DECRA(XCL, 1);
    }
    DESCR_t proc_d; GETDC_B(proc_d, YCL_d, 0); /* Get expected arg count from procedure descriptor */
    SETAV(XCL, proc_d);
    while (ACOMP(WCL_d, XCL) < 0) { /* Pad with nulls if fewer args given */
        ar_push(NULVCL); INCRA(WCL_d, 1);
    }
    DESCR_t def_d; GETDC_B(def_d, YCL_d, DESCR); /* Get definition block */
    DESCR_t size_d; MOVD(size_d, XCL);
    SETAC(size_d, D_A(XCL) * DESCR);
    MOVV(size_d, def_d);
    int32_t blk = BLOCK_fn(D_A(size_d), D_V(size_d));
    if (!blk) { ar_top -= D_A(XCL); return FAIL; }
    SETAC(ZPTR, blk);
    for (int32_t i = D_A(XCL) - 1; i >= 0; i--) { /* Copy stacked values into block */
        DESCR_t v = ar_pop();
        PUTDC_B(ZPTR, (int32_t)(i + 1) * DESCR, v);
    }
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── FIELD — field accessor ──────────────────────────────────────────── */
RESULT_t FIELD_fn(void)
{
    ar_push(INCL);
    if (ARGVAL_fn() == FAIL) { ar_top--; return FAIL; }
    if (deql(XPTR, NULVCL)) { ar_top--; return FAIL; }
    MOVD(YCL, ar_pop());
    if (VEQLC(XPTR, I)) { /* Handle integer index */
        int32_t off = GNVARI_fn(D_A(XPTR));
        if (!off) return FAIL;
        SETAC(XPTR, off); SETVC(XPTR, S);
    } else if (VEQLC(XPTR, S)) {
        if (AEQLC(CASECL, 0)) VPXPTR_fn2();
    }
    MOVV(DT1CL, XPTR); /* Look up data type offset in definition block */
    DESCR_t def_d; GETDC_B(def_d, YCL, DESCR);
    int32_t assoc = locapt_fn(D_A(def_d), &DT1CL);
    if (!assoc) return FAIL;
    DESCR_t off_d; GETDC_B(off_d, DT1CL, 2*DESCR);
    SUM(XPTR, XPTR, off_d);
    SETVC(XPTR, N);
    return OK;
}

/*====================================================================================================================*/
/* ── RSORT / SORT — stubs ────────────────────────────────────────────── */
/* Shell-sort requires A4PTR..A7PTR, LPTR, NANCHK, RCOMP, INTRL etc.
 * Stubbed until M19+ infrastructure is in place.                        */
RESULT_t RSORT_fn(void) { return FAIL; }
RESULT_t SORT_fn(void)  { return FAIL; }

/*====================================================================================================================*/
/* ── ICNVTA (SIL 6606) — initial TABLE→ARRAY scan ───────────────────────
 *
 * v311.sil:
 *   ICNVTA PROC    CNVRT
 *          POP     YPTR              — get table pointer from stack
 *          MOVD    YCL,ZEROCL        — zero item count
 *   CNVTA7 GETSIZ  XCL,YPTR         — XCL = data size of table extent
 *          MOVD    ZCL,XCL
 *          DECRA   XCL,3*DESCR       — skip 3-DESCR header: point at last item
 *   CNVTA1 GETD    WCL,YPTR,XCL     — read item at offset XCL
 *          DEQL    WCL,NULVCL,,CNVTA2 — if not null, count it
 *          INCRA   YCL,1
 *   CNVTA2 AEQLC   XCL,DESCR,,CNVTA6 — if at bottom, go follow chain
 *          DECRA   XCL,2*DESCR       — step back one key/value pair
 *          BRANCH  CNVTA1
 *   CNVTA6 GETD    YPTR,YPTR,ZCL    — follow chain pointer
 *          AEQLC   YPTR,1,CNVTA7,   — if not end (==1 means end-sentinel), loop
 *          AEQLC   YCL,0,,FAIL       — fail on empty table
 *          ... build prototype "N,2", GENVAR, BLOCK, install heading, RRTURN ZPTR,2
 *
 * snobol4.c confirms: POP(YPTR); D(YCL)=D(ZEROCL); loop over extents counting
 * non-null values; build "N,2" prototype string; GENVAR; BLOCK; MOVBLK heading;
 * RETURN(2) with ZPTR = new array block.
 *
 * Side effect on return: YCL = item count (used by caller CNVTA).       */
RESULT_t ICNVTA_fn(void)
{
    /* POP YPTR — table pointer was pushed by CNVTA before RCALL */
    YPTR = ZPTR; /* caller puts table ptr in ZPTR before the call */

    /* MOVD YCL,ZEROCL — zero item count */
    MOVD(YCL, ZEROCL);

cnvta7:;
    /* GETSIZ XCL,YPTR — get data size of this table extent */
    int32_t data_sz = x_bkdata(D_A(YPTR));
    SETAC(XCL, data_sz); D_F(XCL) = 0; D_V(XCL) = 0;
    MOVD(ZCL, XCL);                               /* ZCL = size (for chain follow) */
    D_A(XCL) -= 3 * DESCR;                        /* DECRA XCL,3*DESCR: skip header */

cnvta1:;
    /* GETD WCL,YPTR,XCL — read descriptor at YPTR+XCL */
    memcpy(&WCL, (char*)A2P(D_A(YPTR)) + D_A(XCL), sizeof(DESCR_t));
    if (!deql(WCL, NULVCL))                       /* DEQL WCL,NULVCL,,CNVTA2 */
        D_A(YCL)++;                                /* INCRA YCL,1 */

    /* CNVTA2: AEQLC XCL,DESCR,,CNVTA6 */
    if (D_A(XCL) == DESCR) goto cnvta6;
    D_A(XCL) -= 2 * DESCR;                        /* DECRA XCL,2*DESCR */
    goto cnvta1;

cnvta6:;
    /* GETD YPTR,YPTR,ZCL — follow chain: read descriptor at YPTR+ZCL */
    memcpy(&YPTR, (char*)A2P(D_A(YPTR)) + D_A(ZCL), sizeof(DESCR_t));
    /* AEQLC YPTR,1,CNVTA7, — if not end sentinel (1), loop */
    if (D_A(YPTR) != 1) goto cnvta7;

    /* AEQLC YCL,0,,FAIL */
    if (D_A(YCL) == 0) return FAIL;

    /* Build prototype string "N,2" for GENVAR —
     * SIL: MULTC XCL,YCL,2*DESCR; INTSPC YSP,YCL; SETLC PROTSP,0;
     *      APDSP PROTSP,YSP; APDSP PROTSP,CMASP; WCL=2; INTSPC XSP,WCL;
     *      APDSP PROTSP,XSP; SETSP XSP,PROTSP;
     *      RCALL TPTR,GENVAR,XSPPTR, */
    D_A(XCL) = D_A(YCL) * 2 * DESCR;             /* MULTC XCL,YCL,2*DESCR */
    D_F(XCL) = D_V(XCL) = 0;

    SPEC_t ysp; INTSPC_fn(&ysp, &YCL);            /* INTSPC YSP,YCL */
    PROTSP.l = 0;                                  /* SETLC PROTSP,0 */
    APDSP_fn(&PROTSP, &ysp);                       /* APDSP PROTSP,YSP */
    APDSP_fn(&PROTSP, &CMASP);                     /* APDSP PROTSP,CMASP */
    DESCR_t wcl2 = ZEROCL; D_A(wcl2) = 2;
    SPEC_t xsp2; INTSPC_fn(&xsp2, &wcl2);         /* INTSPC XSP,WCL (WCL=2) */
    APDSP_fn(&PROTSP, &xsp2);                      /* APDSP PROTSP,XSP */
    SPEC_t proto = PROTSP;                         /* SETSP XSP,PROTSP */

    /* RCALL TPTR,GENVAR,XSPPTR — intern prototype string */
    int32_t toff = GENVAR_fn(&proto);
    if (!toff) return FAIL;
    SETAC(TPTR, toff); SETVC(TPTR, S);

    MOVD(ZCL, XCL);                               /* MOVD ZCL,XCL — save size */
    D_A(XCL) += 4 * DESCR;                        /* INCRA XCL,4*DESCR — heading */

    /* RCALL ZPTR,BLOCK,XCL — allocate array block */
    int32_t blk = BLOCK_fn(D_A(XCL), 0);
    if (!blk) return FAIL;
    SETAC(ZPTR, blk); D_F(ZPTR) = 0; SETVC(ZPTR, A); /* SETVC ZPTR,A */

    /* MOVD ATPRCL,TPTR; SETVA ATEXCL,YCL — install prototype and first dim */
    MOVD(ATPRCL[0], TPTR);
    D_V(ATEXCL) = D_A(YCL);

    /* MOVBLK ZPTR,ATRHD,FRDSCL — copy 4*DESCR of heading from ATRHD into ZPTR */
    memcpy(A2P(D_A(ZPTR)), A2P(D_A(ATRHD)), (size_t)D_A(FRDSCL));

    /* RRTURN ZPTR,2 — return with ZPTR = new array block, case 2 */
    return OK;  /* caller (CNVTA) receives ZPTR */
}

/* ── CNVTA (SIL 6568) — TABLE → ARRAY ────────────────────────────────
 *
 * v311.sil:
 *   CNVTA  MOVD    WPTR,ZPTR         — save table pointer
 *          RCALL   ZPTR,ICNVTA,ZPTR,(FAIL) — allocate array, YCL=count
 *          MOVD    YPTR,ZPTR         — save array block pointer
 *          MULTC   YCL,YCL,DESCR     — YCL = count * DESCR (address units)
 *          INCRA   YPTR,5*DESCR      — skip array heading (5 slots)
 *          SUM     TPTR,YPTR,YCL     — TPTR = second half (value column)
 *   CNVTA8 GETSIZ  WCL,WPTR
 *          DECRA   WCL,2*DESCR
 *          SUM     WCL,WPTR,WCL      — WCL = ptr to last key slot
 *   CNVTA3 GETDC   TCL,WPTR,DESCR    — read key at WPTR+DESCR
 *          DEQL    TCL,NULVCL,,CNVTA5 — skip null entries
 *          PUTDC   TPTR,0,TCL         — store key in second half
 *          MOVDIC  YPTR,0,WPTR,2*DESCR — copy value to first half
 *          INCRA   YPTR,DESCR
 *          INCRA   TPTR,DESCR
 *   CNVTA5 INCRA   WPTR,2*DESCR      — next table slot
 *          AEQL    WCL,WPTR,CNVTA3   — if not at end, loop
 *          GETDC   WPTR,WCL,2*DESCR  — follow chain
 *          AEQLC   WPTR,1,CNVTA8,    — if more extents, loop
 *          SETAC   TPTR,0            — clear second-half pointer
 *          BRANCH  RTZPTR
 *
 * snobol4.c confirms the above exactly.                                  */
RESULT_t CNVTA_fn(void)
{
    MOVD(WPTR, ZPTR);                              /* save table pointer */
    if (ICNVTA_fn() == FAIL) return FAIL;          /* RCALL ZPTR,ICNVTA,ZPTR */
    MOVD(YPTR, ZPTR);                              /* save array block */
    D_A(YCL) *= DESCR;                             /* MULTC YCL,YCL,DESCR */
    D_F(YCL) = D_V(YCL) = 0;
    D_A(YPTR) += 5 * DESCR;                        /* INCRA YPTR,5*DESCR */
    TPTR = YPTR;
    D_A(TPTR) += D_A(YCL);                         /* SUM TPTR,YPTR,YCL */

cnvta8:;
    int32_t data_sz = x_bkdata(D_A(WPTR));
    SETAC(WCL, data_sz); D_F(WCL) = D_V(WCL) = 0; /* GETSIZ WCL,WPTR */
    D_A(WCL) -= 2 * DESCR;                         /* DECRA WCL,2*DESCR */
    D_A(WCL) = D_A(WPTR) + D_A(WCL);              /* SUM WCL,WPTR,WCL */
    D_F(WCL) = D_F(WPTR); D_V(WCL) = D_V(WPTR);

cnvta3:;
    memcpy(&TCL, (char*)A2P(D_A(WPTR)) + DESCR, sizeof(DESCR_t)); /* GETDC TCL,WPTR,DESCR */
    if (deql(TCL, NULVCL)) goto cnvta5;            /* DEQL TCL,NULVCL,,CNVTA5 */
    /* PUTDC TPTR,0,TCL — store key */
    memcpy(A2P(D_A(TPTR)), &TCL, sizeof(DESCR_t));
    /* MOVDIC YPTR,0,WPTR,2*DESCR — copy value */
    DESCR_t val; memcpy(&val, (char*)A2P(D_A(WPTR)) + 2*DESCR, sizeof(DESCR_t));
    memcpy(A2P(D_A(YPTR)), &val, sizeof(DESCR_t));
    D_A(YPTR) += DESCR;                            /* INCRA YPTR,DESCR */
    D_A(TPTR) += DESCR;                            /* INCRA TPTR,DESCR */

cnvta5:;
    D_A(WPTR) += 2 * DESCR;                        /* INCRA WPTR,2*DESCR */
    if (D_A(WCL) != D_A(WPTR)) goto cnvta3;        /* AEQL WCL,WPTR,CNVTA3 */
    /* GETDC WPTR,WCL,2*DESCR — follow chain */
    memcpy(&WPTR, (char*)A2P(D_A(WCL)) + 2*DESCR, sizeof(DESCR_t));
    if (D_A(WPTR) != 1) goto cnvta8;               /* AEQLC WPTR,1,CNVTA8, */
    D_A(TPTR) = 0;                                  /* SETAC TPTR,0 */
    return OK;                                      /* BRANCH RTZPTR */
}

/* ── CNVAT (SIL 6642) — Nx2 ARRAY → TABLE ────────────────────────────
 *
 * v311.sil:
 *   CNVAT  GETDC   XCL,ZPTR,2*DESCR  — get dimensionality
 *          MOVD    YPTR,ZPTR
 *          AEQLC   XCL,2,FAIL,        — must be 2-dimensional
 *          GETDC   XCL,ZPTR,3*DESCR   — get second dimension
 *          VEQLC   XCL,2,FAIL,        — second dim must be 2
 *          GETSIZ  XCL,ZPTR           — get data size
 *          DECRA   XCL,2*DESCR
 *          RCALL   XPTR,BLOCK,XCL,    — allocate table block
 *          SETVC   XPTR,T
 *          GETDC   YCL,ZPTR,4*DESCR   — get first dimension (row count)
 *          MOVD    ZPTR,XPTR
 *          PUTD    XPTR,XCL,ONECL     — end sentinel at tail
 *          DECRA   XCL,DESCR
 *          MOVD    TCL,EXTVAL; INCRA TCL,2*DESCR
 *          PUTD    XPTR,XCL,TCL       — install extension descriptor
 *          SETAV   YCL,YCL; MULTC YCL,YCL,DESCR
 *          INCRA   YPTR,5*DESCR       — skip heading
 *          SUM     WPTR,YPTR,YCL,,    — WPTR = second half (keys)
 *   CNVAT2 MOVDIC  XPTR,DESCR,WPTR,0 — copy key
 *          MOVDIC  XPTR,2*DESCR,YPTR,0 — copy value
 *          DECRA   YCL,DESCR
 *          AEQLC   YCL,0,,RTZPTR
 *          INCRA   XPTR,2*DESCR; INCRA WPTR,DESCR; INCRA YPTR,DESCR
 *          BRANCH  CNVAT2
 *
 * snobol4.c confirms exactly.                                            */
RESULT_t CNVAT_fn(void)
{
    /* GETDC XCL,ZPTR,2*DESCR — dimensionality */
    memcpy(&XCL, (char*)A2P(D_A(ZPTR)) + 2*DESCR, sizeof(DESCR_t));
    MOVD(YPTR, ZPTR);
    if (D_A(XCL) != 2) return FAIL;                /* AEQLC XCL,2,FAIL, */

    /* GETDC XCL,ZPTR,3*DESCR — second dimension */
    memcpy(&XCL, (char*)A2P(D_A(ZPTR)) + 3*DESCR, sizeof(DESCR_t));
    if (D_V(XCL) != 2) return FAIL;                /* VEQLC XCL,2,FAIL, */

    /* GETSIZ XCL,ZPTR; DECRA XCL,2*DESCR */
    D_A(XCL) = x_bkdata(D_A(ZPTR));
    D_F(XCL) = D_V(XCL) = 0;
    D_A(XCL) -= 2 * DESCR;

    /* RCALL XPTR,BLOCK,XCL — allocate table block */
    int32_t blk = BLOCK_fn(D_A(XCL), T);
    if (!blk) return FAIL;
    SETAC(XPTR, blk); D_F(XPTR) = 0; SETVC(XPTR, T); /* SETVC XPTR,T */

    /* GETDC YCL,ZPTR,4*DESCR — first dimension (row count) */
    memcpy(&YCL, (char*)A2P(D_A(ZPTR)) + 4*DESCR, sizeof(DESCR_t));
    MOVD(ZPTR, XPTR);

    /* PUTD XPTR,XCL,ONECL — end sentinel */
    memcpy((char*)A2P(D_A(XPTR)) + D_A(XCL), &ONECL, sizeof(DESCR_t));
    D_A(XCL) -= DESCR;

    /* MOVD TCL,EXTVAL; INCRA TCL,2*DESCR; PUTD XPTR,XCL,TCL */
    MOVD(TCL, EXTVAL); D_A(TCL) += 2 * DESCR;
    memcpy((char*)A2P(D_A(XPTR)) + D_A(XCL), &TCL, sizeof(DESCR_t));

    /* SETAV YCL,YCL; MULTC YCL,YCL,DESCR */
    D_A(YCL) = D_V(YCL); D_F(YCL) = D_V(YCL) = 0;
    D_A(YCL) *= DESCR; D_F(YCL) = D_V(YCL) = 0;

    D_A(YPTR) += 5 * DESCR;                        /* INCRA YPTR,5*DESCR */
    WPTR = YPTR; D_A(WPTR) += D_A(YCL);            /* SUM WPTR,YPTR,YCL */

cnvat2:;
    /* MOVDIC XPTR,DESCR,WPTR,0 — copy key from second half */
    DESCR_t key; memcpy(&key, A2P(D_A(WPTR)), sizeof(DESCR_t));
    memcpy((char*)A2P(D_A(XPTR)) + DESCR, &key, sizeof(DESCR_t));
    /* MOVDIC XPTR,2*DESCR,YPTR,0 — copy value from first half */
    DESCR_t val; memcpy(&val, A2P(D_A(YPTR)), sizeof(DESCR_t));
    memcpy((char*)A2P(D_A(XPTR)) + 2*DESCR, &val, sizeof(DESCR_t));

    D_A(YCL) -= DESCR;                             /* DECRA YCL,DESCR */
    if (D_A(YCL) == 0) return OK;                  /* AEQLC YCL,0,,RTZPTR */
    D_A(XPTR) += 2 * DESCR;                        /* INCRA XPTR,2*DESCR */
    D_A(WPTR) += DESCR;                            /* INCRA WPTR,DESCR */
    D_A(YPTR) += DESCR;                            /* INCRA YPTR,DESCR */
    goto cnvat2;
}
