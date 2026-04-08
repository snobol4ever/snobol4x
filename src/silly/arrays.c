/*
 * sil_arrays.c — Arrays, Tables, and Defined Data (v311.sil §14 4644–5267)
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
#include "arrays.h"
#include "argval.h"
#include "arena.h"
#include "strings.h"
#include "symtab.h"

/* External stubs — use signatures from headers where already declared */
extern RESULT_t INVOKE_fn(void);
extern RESULT_t INTVAL_fn(void);
/* VARVUP_fn, GENVUP_fn, FINDEX_fn declared in sil_argval.h / sil_arena.h / sil_symtab.h */
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
    for (int i = 0; i < total_elems; i++) { /* Fill element cells with initial value TPTR
                                              * Oracle: XPTR walks from blk+(2+ndim)*DESCR, writing at XPTR+DESCR
                                              * → elements land at slots 3+ndim .. 2+ndim+total */
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
        /* Oracle: element slots start at 3+ndim (PUTDC XPTR,DESCR with XPTR at blk+(2+ndim)*DESCR) */
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
