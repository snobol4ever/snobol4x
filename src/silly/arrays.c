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
#include <math.h>

#include "types.h"
#include "data.h"
#include "errors.h"
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
/* DATDEF_fn needs: STREAM_fn, VARATB, LOCSP_fn, GENVUP_fn, error() */
extern RESULT_t   STREAM_fn(SPEC_t *sp1, SPEC_t *sp2, DESCR_t *tbl_descr, int *stype_out);
extern DESCR_t    VARATB;
extern void       LOCSP_fn(SPEC_t *sp, const DESCR_t *dp);
extern int32_t    GENVUP_fn(const SPEC_t *sp);
extern void       error(int);

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
/*
 * v311.sil §14 line 4748.  Three-way faithful translation:
 *
 * VARVAL → get prototype string into XPTR.
 * SETAC DATACL,0           — D_A(DATACL)=0: prototype-end not yet seen.
 * LOCSP XSP,XPTR           — XSP = specifier of XPTR string.
 * STREAM YSP,XSP,VARATB    — break on '(' → YSP = name part.
 * AEQLC STYPE,LPTYP,PROTER — must have stopped on '('.
 * GENVUP(YSPPTR) → XPTR    — intern the data-type name.
 * FINDEX(XPTR)   → ZCL     — get/create fn descriptor.
 * INCRV DATSEG,1            — bump data-type code.
 * check DATSEG == DATSIZ   — too many types → INTR27.
 * MOVD YCL,ZEROCL           — field count = 0.
 * AUGATL(DTATL, DATSEG, XPTR) — add (code,name) to data-type list.
 * PSTACK WPTR               — record stack depth (wptr_idx = ar_top).
 * PUSH(DATSEG); PUSH(XPTR)  — save code and name on local stack.
 *
 * DATA3 loop (field scan):
 *   FSHRTN XSP,1            — consume the break char '(' or ','.
 *   if DATACL != 0 → DAT5  — prototype end already signalled.
 *   STREAM YSP,XSP,VARATB  — break on next ',' or ')'.
 *   SELBRA STYPE:
 *     LPTYP(1) → PROTER     — unexpected '(' = error.
 *     RPTYP(3) → DATA6      — ')' = set DATACL=1, join DATA4.
 *   DATA4:
 *     LEQLC YSP,0 → DATA3  — zero-length field name: skip.
 *     GENVUP(YSPPTR)→XPTR  — intern field name.
 *     PUSH XPTR             — save field name.
 *     FINDEX(XPTR)→XCL     — get/create field fn descriptor.
 *     GETDC WCL,XCL,0       — WCL = procedure slot of descriptor.
 *     DEQL WCL,FLDCL → DAT6 — if already FIELD, go DAT6.
 *     GETDC ZPTR,XCL,DESCR  — ZPTR = field-def block pointer.
 *     MULTC TCL,YCL,DESCR   — TCL = field_index * DESCR.
 *     AUGATL(ZPTR, DATSEG, TCL) → ZPTR.
 *   DAT7: PUTDC XCL,DESCR,ZPTR; INCRA YCL,1; → DATA3.
 *   DATA6: SETAC DATACL,1; → DATA4.
 *
 * DAT5 (end of prototype):
 *   LEQLC XSP,0,PROTER       — prototype must be fully consumed.
 *   AEQLC YCL,0,,PROTER      — must have ≥1 field.
 *   SETVA DATCL,YCL           — DATCL.v = YCL.a (field count as type-code).
 *   PUTDC ZCL,0,DATCL         — store procedure descriptor.
 *   MULTC YCL,YCL,DESCR; INCRA YCL,2*DESCR — total block bytes.
 *   MOVV YCL,DATSEG            — YCL.v = DATSEG.v (data type code).
 *   BLOCK(YCL)→ZPTR           — allocate definition block.
 *   INCRA WPTR,DESCR           — skip DATSEG slot, point at XPTR slot.
 *   MOVBLK ZPTR,WPTR,YCL      — copy (nfields+2)*DESCR bytes from stack.
 *   PUTDC ZCL,DESCR,ZPTR      — store definition block in fn descriptor.
 *   → RETNUL.
 *
 * DAT6: PUTDC XCL,0,FLDCL; BLOCK(TWOCL)→ZPTR;
 *       PUTDC ZPTR,DESCR,DATSEG; MULTC TCL,YCL,DESCR; PUTDC ZPTR,2*DESCR,TCL;
 *       → DAT7.
 */
RESULT_t DATDEF_fn(void)
{
    /* VARVAL → XPTR */
    if (VARVAL_fn() == FAIL) return FAIL;
    /* SETAC DATACL,0 */
    D_A(DATACL) = 0; D_F(DATACL) = 0; D_V(DATACL) = 0;
    /* LOCSP XSP,XPTR */
    LOCSP_fn(&XSP, &XPTR);

    /* STREAM YSP,XSP,VARATB,PROTER,PROTER — break on '(' */
    { int stype = 0;
      RESULT_t sr = STREAM_fn(&YSP, &XSP, &VARATB, &stype);
      if (sr == FAIL) return FAIL; /* ST_EOS = PROTER */
      if (D_A(STYPE) != LPTYP) return FAIL; /* not '(' = PROTER */
    }

    /* GENVUP(YSPPTR) → XPTR — intern data-type name */
    { int32_t off = GENVUP_fn(&YSP);
      if (!off) return FAIL;
      SETAC(XPTR, off); SETVC(XPTR, S);
    }

    /* FINDEX(XPTR) → ZCL */
    { int32_t foff = FINDEX_fn(&XPTR);
      if (!foff) return FAIL;
      SETAC(ZCL, foff); D_F(ZCL) = 0; SETVC(ZCL, 0);
    }

    /* INCRV DATSEG,1 */
    D_V(DATSEG)++;
    /* VEQLC DATSEG,DATSIZ,,INTR27 */
    if ((int32_t)D_V(DATSEG) == DATSIZ) { error(27); return FAIL; }

    /* MOVD YCL,ZEROCL — field count = 0 */
    MOVD(YCL, ZEROCL);

    /* AUGATL(DTATL, DATSEG, XPTR) — add data-type pair (code, name) */
    { int32_t newlist = AUGATL_fn(D_A(DTATL), DATSEG, XPTR);
      if (!newlist) return FAIL;
      SETAC(DTATL, newlist);
    }

    /* PSTACK WPTR — record stack depth before pushes */
    int wptr_idx = ar_top;   /* our PSTACK equivalent */

    /* PUSH(DATSEG); PUSH(XPTR) — save code and name */
    ar_push(DATSEG);
    ar_push(XPTR);

data3:;
    /* FSHRTN XSP,1 — consume the break character */
    if (XSP.l > 0) { XSP.l--; XSP.o++; }

    /* AEQLC DATACL,0,DAT5 — if DATACL != 0, go to end-of-prototype */
    if (D_A(DATACL) != 0) goto dat5;

    /* STREAM YSP,XSP,VARATB — break on ',' or ')' */
    { int stype2 = 0;
      RESULT_t sr2 = STREAM_fn(&YSP, &XSP, &VARATB, &stype2);
      if (sr2 == FAIL) return FAIL; /* ST_EOS = PROTER */
      int st = D_A(STYPE);
      if (st == LPTYP) return FAIL;           /* '(' unexpected = PROTER */
      if (st == RPTYP) goto data6;            /* ')' = end of prototype */
      /* fall-through: CMATYP or other = DATA4 */
    }

data4:;
    /* LEQLC YSP,0,,DATA3 — zero-length field name: skip */
    if (YSP.l == 0) goto data3;

    /* GENVUP(YSPPTR) → XPTR — intern field name */
    { int32_t foff2 = GENVUP_fn(&YSP);
      if (!foff2) return FAIL;
      SETAC(XPTR, foff2); SETVC(XPTR, S);
    }

    /* PUSH XPTR — save field name on local stack */
    ar_push(XPTR);

    /* FINDEX(XPTR) → XCL — get/create field fn descriptor */
    { int32_t xoff = FINDEX_fn(&XPTR);
      if (!xoff) { ar_pop(); return FAIL; }
      SETAC(XCL, xoff); D_F(XCL) = 0; SETVC(XCL, 0);
    }

    /* GETDC WCL,XCL,0 — WCL = procedure slot (slot 0 of descriptor block) */
    memcpy(&WCL, A2P(D_A(XCL)), sizeof(DESCR_t));

    /* DEQL WCL,FLDCL,DAT6 — if procedure is FIELD, goto DAT6 */
    if (deql(WCL, FLDCL)) goto dat6;

    /* GETDC ZPTR,XCL,DESCR — ZPTR = field-def block (slot 1) */
    memcpy(&ZPTR, (char*)A2P(D_A(XCL)) + DESCR, sizeof(DESCR_t));

    /* MULTC TCL,YCL,DESCR — TCL = field_index * DESCR */
    MOVD(TCL, YCL); D_A(TCL) *= DESCR; D_F(TCL) = D_V(TCL) = 0;

    /* AUGATL(ZPTR, DATSEG, TCL) → ZPTR */
    { int32_t newz = AUGATL_fn(D_A(ZPTR), DATSEG, TCL);
      if (!newz) return FAIL;
      SETAC(ZPTR, newz);
    }

dat7:;
    /* PUTDC XCL,DESCR,ZPTR — store updated def-block in field descriptor slot 1 */
    memcpy((char*)A2P(D_A(XCL)) + DESCR, &ZPTR, sizeof(DESCR_t));
    /* INCRA YCL,1 — bump field count */
    D_A(YCL)++;
    goto data3;

data6:;
    /* SETAC DATACL,1 — note end of prototype */
    D_A(DATACL) = 1;
    goto data4;

dat5:;
    /* LEQLC XSP,0,PROTER — prototype must be fully consumed */
    if (XSP.l != 0) return FAIL;
    /* AEQLC YCL,0,,PROTER — must have at least one field */
    if (D_A(YCL) == 0) return FAIL;

    /* SETVA DATCL,YCL — DATCL.v = field count */
    D_V(DATCL) = (int16_t)D_A(YCL);

    /* PUTDC ZCL,0,DATCL — store procedure descriptor in fn block slot 0 */
    memcpy(A2P(D_A(ZCL)), &DATCL, sizeof(DESCR_t));

    /* MULTC YCL,YCL,DESCR; INCRA YCL,2*DESCR — total copy size */
    D_A(YCL) *= DESCR;
    D_A(YCL) += 2 * DESCR;
    D_F(YCL) = D_V(YCL) = 0;

    /* MOVV YCL,DATSEG — YCL.v = data type code */
    D_V(YCL) = D_V(DATSEG);

    /* BLOCK(YCL) → ZPTR — allocate definition block */
    { int32_t blk = BLOCK_fn(D_A(YCL), (int)D_V(YCL));
      if (!blk) return FAIL;
      SETAC(ZPTR, blk); D_F(ZPTR) = 0; D_V(ZPTR) = D_V(YCL);
    }

    /* INCRA WPTR,DESCR — skip the DATSEG slot, point at XPTR slot.
     * Oracle: WPTR.a was cstack-1 (0-based top before push),
     * after INCRA it skips the first push (DATSEG) to reach XPTR.
     * Our equivalent: wptr_idx+1 = index of XPTR in ar_stk. */
    int copy_from = wptr_idx + 1; /* skip DATSEG slot (wptr_idx+0) */

    /* MOVBLK ZPTR,WPTR,YCL — copy (nfields+2) DESCRs from stack into block.
     * Oracle copies: XPTR (name), then field XPTRs pushed during loop.
     * YCL = (nfields+2)*DESCR bytes total (nfields+1 names + 1 slot for something).
     * We copy ar_stk[copy_from .. ar_top-1] → block slots 0..n. */
    { int n = ar_top - copy_from;
      for (int i = 0; i < n; i++)
          memcpy((char*)A2P(D_A(ZPTR)) + i * DESCR,
                 &ar_stk[copy_from + i], sizeof(DESCR_t));
    }

    /* PUTDC ZCL,DESCR,ZPTR — store definition block in fn descriptor slot 1 */
    memcpy((char*)A2P(D_A(ZCL)) + DESCR, &ZPTR, sizeof(DESCR_t));

    /* Restore stack — pop everything pushed since PSTACK */
    ar_top = wptr_idx;

    /* BRANCH RETNUL */
    MOVD(XPTR, NULVCL); return OK;

dat6:;
    /* PUTDC XCL,0,FLDCL — mark as FIELD procedure */
    memcpy(A2P(D_A(XCL)), &FLDCL, sizeof(DESCR_t));

    /* BLOCK(TWOCL) → ZPTR — allocate 2-slot field definition block */
    { int32_t blk2 = BLOCK_fn(D_A(TWOCL), 0);
      if (!blk2) return FAIL;
      SETAC(ZPTR, blk2); D_F(ZPTR) = D_V(ZPTR) = 0;
    }

    /* PUTDC ZPTR,DESCR,DATSEG — store data type code in slot 1 */
    memcpy((char*)A2P(D_A(ZPTR)) + DESCR, &DATSEG, sizeof(DESCR_t));

    /* MULTC TCL,YCL,DESCR */
    MOVD(TCL, YCL); D_A(TCL) *= DESCR; D_F(TCL) = D_V(TCL) = 0;

    /* PUTDC ZPTR,2*DESCR,TCL — store field offset in slot 2 */
    memcpy((char*)A2P(D_A(ZPTR)) + 2*DESCR, &TCL, sizeof(DESCR_t));

    goto dat7;
}

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
/* ── RSORT / SORT — v311.sil §14 lines 5004–5271 ────────────────────── */
/*
 * Three-way sync: v311.sil 5004, snobol4.c L_RSORT/L_SORT, this file.
 *
 * RSORT sets SCL=1 (reverse); SORT sets SCL=0 (forward), both fall into SORT1.
 * SORT1: get first arg (array or table). Table → convert to array via ICNVTA_fn.
 * Build index table of row-pointers at tail of new array, shell-sort the index,
 * then unravel into new array. Return new sorted array via XPTR.
 *
 * Register map (mirrors snobol4.c exactly):
 *   SCL   = 1/0 RSORT/SORT flag
 *   WCL   = arg count (from INCL), later col-index scratch
 *   WPTR  = copy of first-arg DESCR (to detect table vs array)
 *   XPTR  = array (new on array path, converted on table path)
 *   YPTR  = pointer to first data element − 1 (base of rows)
 *   ZPTR  = pointer to sort-column first element
 *   YCL   = number of columns (1 for vector)
 *   ZCL   = number of rows  (address units = count * DESCR)
 *   XCL   = scratch / dimension count
 *   TCL   = K swap-flag / scratch
 *   TPTR  = index table − 1 (in new array tail)
 *   A3PTR = field-block pointer (0 if none)
 *   A4PTR = column advance (rows for arrays, −DESCR for tables)
 *   A5PTR = offset: sort-element → first element in row
 *   A6PTR = G (shell gap), then column counter during unravel
 *   A7PTR = I (inner-loop index)
 *   LPTR  = J (= I + G), then row counter during unravel
 *   F1PTR, F2PTR = index-table entry pointers during compare/swap
 *   A1PTR, A2PTR = actual element values being compared
 *   DTCL  = data-type pair for fast dispatch
 */

/* ── Inline helpers ──────────────────────────────────────────────────── */

/* INTRL dst,src — convert INTEGER DESCR to REAL in-place (v311.sil INTRL) */
#define SORT_INTRL(d) do { D_R(d) = (float)D_A(d); D_F(d) = 0; D_V(d) = R; } while(0)

/* NANCHK x,yes,no — jump to yes if x is NaN, else no.
 * In our code we use isnan(D_R(d)) directly.                            */

/* GETSIZ dst,blk — dst = data-body size of block at blk */
#define SORT_GETSIZ(dst, blk_d) \
    do { D_A(dst) = x_bkdata(D_A(blk_d)); D_F(dst) = D_V(dst) = 0; } while(0)

/* MOVBLK dst_off, src_off, nbytes — raw arena block copy */
#define SORT_MOVBLK(dst_off, src_off, nbytes) \
    memcpy(A2P(dst_off), A2P(src_off), (size_t)(nbytes))

/* MOVDIC dst_d, dst_off, src_d, src_off — copy one DESCR between arena cells */
#define SORT_MOVDIC(dst_d, dst_off_i, src_d, src_off_i) \
    memcpy((char*)A2P(D_A(dst_d))+(dst_off_i), \
           (char*)A2P(D_A(src_d))+(src_off_i), sizeof(DESCR_t))

/* RESETF d,flag — clear flag bit */
#define SORT_RESETF(d, flag) do { D_F(d) &= (uint8_t)~(flag); } while(0)

/* PCOMP a,b,gt,eq,lt — pointer (offset) three-way: D_A compare */
/* Used inline as conditional gotos below. */

/* RCOMP a,b,gt,eq,lt — real three-way compare (NaN already handled above) */
/* Used inline as conditional gotos below. */

/* ── rlint for SORT3A — real → integer, overflow → INTR30 ───────────── */
static RESULT_t sort_rlint(DESCR_t *dp)
{
    float f = D_R(*dp);
    if (f >= 2147483648.0f || f < -2147483648.0f) { INTR30_fn(); return FAIL; }
    D_A(*dp) = (int32_t)f;
    D_F(*dp) = 0;
    D_V(*dp) = I;
    return OK;
}

/* ── Main entry: RSORT ───────────────────────────────────────────────── */
static RESULT_t sort_body(int reverse);  /* forward decl */

RESULT_t RSORT_fn(void) { return sort_body(1); }
RESULT_t SORT_fn(void)  { return sort_body(0); }

static RESULT_t sort_body(int reverse)
{
    /* RSORT: SCL=1; SORT: SCL=0 */
    D_A(SCL) = reverse ? 1 : 0; D_F(SCL) = D_V(SCL) = 0;

/* ── SORT1: get arg count, first argument ────────────────────────────── */
    /* SETAV WCL,INCL — get arg count from INCL.v */
    SETAV(WCL, INCL);
    /* PUSH (WCL,SCL) */
    ar_push(WCL); ar_push(SCL);
    /* RCALL XPTR,ARGVAL,,FAIL */
    if (ARGVAL_fn() == FAIL) { ar_top -= 2; return FAIL; }
    /* MOVD WPTR,XPTR */
    MOVD(WPTR, XPTR);
    /* VEQLC XPTR,A,,SORT2 */
    if (VEQLC(XPTR, A)) goto sort2;
    /* VEQLC XPTR,T,NONARY */
    if (!VEQLC(XPTR, T)) { ar_top -= 2; INTR30_fn(); return FAIL; } /* NONARY */
    /* RCALL XPTR,ICNVTA,XPTR,(FAIL) — convert table to array */
    MOVD(ZPTR, XPTR); /* ICNVTA expects table in ZPTR */
    if (ICNVTA_fn() == FAIL) { ar_top -= 2; return FAIL; }
    /* fall into SORT2 */

/* ── SORT2: unpack dimensions ────────────────────────────────────────── */
sort2:;
    /* POP (SCL,WCL) */
    SCL = ar_pop(); WCL = ar_pop();
    /* GETDC XCL,XPTR,2*DESCR — number of dimensions */
    GETDC_B(XCL, XPTR, 2*DESCR);
    /* MULTC YPTR,XCL,DESCR — convert to address units */
    D_A(YPTR) = D_A(XCL) * DESCR; D_F(YPTR) = D_V(YPTR) = 0;
    /* SUM YPTR,XPTR,YPTR */
    SUM(YPTR, XPTR, YPTR);
    /* INCRA YPTR,2*DESCR */
    INCRA(YPTR, 2*DESCR);
    /* MOVD YCL,ONECL — assume vector: 1 column */
    MOVD(YCL, ONECL);
    /* ACOMPC XCL,2,INTR30,,SORT3 — if ndim<2 jump SORT3; if >2 INTR30 */
    if (D_A(XCL) < 2)  goto sort3;
    if (D_A(XCL) > 2)  { INTR30_fn(); return FAIL; }
    /* 2-d: GETDC YCL,XPTR,3*DESCR; SETAV YCL,YCL */
    GETDC_B(YCL, XPTR, 3*DESCR);
    D_A(YCL) = D_V(YCL); D_F(YCL) = D_V(YCL) = 0;

/* ── SORT3: get column length ────────────────────────────────────────── */
sort3:;
    /* GETDC ZCL,YPTR,0; SETAV ZCL,ZCL */
    GETDC_B(ZCL, YPTR, 0);
    D_A(ZCL) = D_V(ZCL); D_F(ZCL) = D_V(ZCL) = 0;
    /* MULTC ZCL,ZCL,DESCR */
    D_A(ZCL) *= DESCR;
    /* MOVD ZPTR,YPTR — default: sort column = first column */
    MOVD(ZPTR, YPTR);
    /* ACOMPC WCL,2,ARGNER,,SORT5 — if arg count < 2 jump SORT5 */
    if (D_A(WCL) < 2) goto sort5;
    if (D_A(WCL) > 2) { INTR30_fn(); return FAIL; } /* ARGNER */
    /* 2 args: save everything, get second argument */
    ar_push(WPTR); ar_push(XPTR); ar_push(YPTR); ar_push(ZPTR);
    ar_push(XCL);  ar_push(YCL);  ar_push(ZCL);  ar_push(SCL);
    if (ARGVAL_fn() == FAIL) {
        ar_top -= 8; return FAIL;
    }
    SCL  = ar_pop(); ZCL  = ar_pop(); YCL  = ar_pop(); XCL  = ar_pop();
    ZPTR = ar_pop(); YPTR = ar_pop(); XPTR = ar_pop(); WPTR = ar_pop();
    /* MOVD A3PTR,ZEROCL — assume no Field arg */
    MOVD(A3PTR, ZEROCL);
    /* Dispatch on type of second arg: I→SORT3C, S→SORT3B, R→SORT3A, else INTR30 */
    if (VEQLC(WCL, I)) goto sort3c;
    if (VEQLC(WCL, S)) goto sort3b;
    if (VEQLC(WCL, R)) goto sort3a;
    INTR30_fn(); return FAIL;

sort3a:; /* SORT3A: RLINT WCL — real → int */
    if (sort_rlint(&WCL) == FAIL) return FAIL;
    goto sort3c;

sort3b:; /* SORT3B: string — try int, try real, else field function */
    LOCSP_fn(&XSP, &WCL);
    if (SPCINT_fn(&WCL, &XSP) == OK) goto sort3c;
    if (SPREAL_fn(&WCL, &XSP) == OK) goto sort3a;
    /* Possible field function — must be 1-d array */
    if (D_A(XCL) != 1) { INTR30_fn(); return FAIL; }
    /* LOCAPV WCL,FNCPL,WCL,INTR30 — find in function pair list */
    { int32_t fp = locapv_fn(D_A(FNCPL), &WCL);
      if (!fp) { INTR30_fn(); return FAIL; }
      D_A(WCL) = fp; D_F(WCL) = D_V(WCL) = 0; }
    /* GETDC WCL,WCL,DESCR — function descriptor */
    GETDC_B(WCL, WCL, DESCR);
    /* GETDC TCL,WCL,0 — procedure definition */
    GETDC_B(TCL, WCL, 0);
    /* AEQL TCL,FLDCL,INTR30 — must be field function */
    if (!DEQL(TCL, FLDCL)) { INTR30_fn(); return FAIL; }
    /* GETDC A3PTR,WCL,DESCR — field block */
    GETDC_B(A3PTR, WCL, DESCR);
    /* MOVD WCL,ONECL — sort on column 1 */
    MOVD(WCL, ONECL);

sort3c:; /* SORT3C: validate column index */
    /* AEQLC XCL,1,SORT4, — if 2-d jump SORT4 */
    if (D_A(XCL) != 1) goto sort4;
    /* 1-d: second arg must equal 1 */
    if (D_A(WCL) == 1) goto sort5;
    INTR30_fn(); return FAIL;

sort4:; /* SORT4: subtract lower bound of 2nd dimension */
    { DESCR_t tcl_tmp; GETDC_B(tcl_tmp, XPTR, 3*DESCR);
      D_A(WCL) -= D_A(tcl_tmp); }
    /* SORT4A: range-check column offset */
    if (D_A(WCL) < 0)           { INTR30_fn(); return FAIL; }
    if (D_A(WCL) >= D_A(YCL))   { INTR30_fn(); return FAIL; }
    /* MULT WCL,WCL,ZCL — offset * column length */
    { int64_t prod = (int64_t)D_A(WCL) * D_A(ZCL);
      if (prod > 2147483647LL || prod < -2147483648LL) { INTR30_fn(); return FAIL; }
      D_A(WCL) = (int32_t)prod; }
    /* SUM ZPTR,YPTR,WCL — ptr to sort column */
    SUM(ZPTR, YPTR, WCL);

/* ── SORT5: build index table ────────────────────────────────────────── */
sort5:;
    D_A(XCL) = 0; D_F(XCL) = D_V(XCL) = 0;
    if (VEQLC(WPTR, A)) goto sorta;

    /* ── Table path ──────────────────────────────────────────────────── */
    /* GETSIZ TCL,XPTR */
    SORT_GETSIZ(TCL, XPTR);
    /* SUM TPTR,XPTR,TCL */
    SUM(TPTR, XPTR, TCL);
    /* SUBTRT TPTR,TPTR,ZCL */
    SUBTRT(TPTR, TPTR, ZCL);
    /* SETAC A4PTR,-DESCR */
    D_A(A4PTR) = (int32_t)(-DESCR); D_F(A4PTR) = D_V(A4PTR) = 0;
    /* SETAC A5PTR,0 */
    D_A(A5PTR) = 0; D_F(A5PTR) = D_V(A5PTR) = 0;
    /* AEQL ZPTR,YPTR,,SORTT1 — if sorting on column 1 */
    if (D_A(ZPTR) == D_A(YPTR)) goto sortt1;
    /* SORT(Table,2): A5PTR = -DESCR */
    D_A(A5PTR) = (int32_t)(-DESCR);

sortt1:;
    /* GETSIZ WCL,WPTR — size of next table extent */
    SORT_GETSIZ(WCL, WPTR);
    /* DECRA WCL,2*DESCR */
    DECRA(WCL, 2*DESCR);
    /* SUM WCL,WPTR,WCL — end-of-extent pointer */
    SUM(WCL, WPTR, WCL);

sortt2:;
    { DESCR_t tcl_val; GETDC_B(tcl_val, WPTR, DESCR); /* GETDC TCL,WPTR,DESCR — value entry */
      if (!DEQL(tcl_val, NULVCL)) {           /* DEQL TCL,NULVCL,,SORTT3 — skip null */
          INCRA(XCL, DESCR);                  /* INCRA XCL,DESCR */
          /* SUM A6PTR,WPTR,A5PTR; INCRA A6PTR,2*DESCR */
          D_A(A6PTR) = D_A(WPTR) + D_A(A5PTR); D_F(A6PTR) = D_V(A6PTR) = 0;
          INCRA(A6PTR, 2*DESCR);
          /* PUTD TPTR,XCL,A6PTR — store index entry */
          PUTD_B(TPTR, XCL, A6PTR);
          /* AEQL XCL,ZCL,,SORTGO */
          if (D_A(XCL) == D_A(ZCL)) goto sortgo;
      }
    }
    /* SORTT3: INCRA WPTR,2*DESCR */
    INCRA(WPTR, 2*DESCR);
    /* AEQL WCL,WPTR,SORTT2 — more in extent? */
    if (D_A(WCL) != D_A(WPTR)) goto sortt2;
    /* GETDC WPTR,WCL,2*DESCR — link to next extent */
    GETDC_B(WPTR, WCL, 2*DESCR);
    goto sortt1;

sorta:; /* ── Array path ─────────────────────────────────────────────── */
    /* GETSIZ WCL,WPTR; SETVC WCL,A */
    SORT_GETSIZ(WCL, WPTR);
    D_V(WCL) = A;
    /* RCALL XPTR,BLOCK,WCL — allocate new array of same size */
    ar_push(WCL);
    { int32_t blk = BLOCK_fn(D_A(WCL), A);
      (void)ar_pop();
      if (!blk) { INTR30_fn(); return FAIL; }
      D_A(XPTR) = blk; D_F(XPTR) = 0; D_V(XPTR) = A; }
    /* SETAC A4PTR,4*DESCR; MOVBLK XPTR,WPTR,A4PTR — copy 4-DESCR header */
    D_A(A4PTR) = 4*DESCR; D_F(A4PTR) = D_V(A4PTR) = 0;
    SORT_MOVBLK(D_A(XPTR), D_A(WPTR), 4*DESCR);
    /* SUM TPTR,XPTR,WCL */
    SUM(TPTR, XPTR, WCL);
    /* SUBTRT TPTR,TPTR,ZCL */
    SUBTRT(TPTR, TPTR, ZCL);
    /* SUBTRT A5PTR,ZPTR,YPTR; RESETF A5PTR,PTR */
    SUBTRT(A5PTR, ZPTR, YPTR);
    SORT_RESETF(A5PTR, PTR);
    /* MOVA A4PTR,ZCL — column advance = no. rows */
    MOVA(A4PTR, ZCL);

sorta1:;
    INCRA(XCL, DESCR);          /* INCRA XCL,DESCR */
    INCRA(ZPTR, DESCR);         /* INCRA ZPTR,DESCR */
    PUTD_B(TPTR, XCL, ZPTR);   /* PUTD TPTR,XCL,ZPTR */
    if (D_A(XCL) != D_A(ZCL)) goto sorta1;

/* ── SORTGO / SORT6: shell-sort the index table ──────────────────────── */
sortgo:;
    MOVA(A6PTR, ZCL);  /* G = N (in address units) */

sort6:;
    if (D_A(A6PTR) <= DESCR) goto sort12;          /* done if G <= DESCR */
    D_A(A6PTR) /= D_A(TWOCL);                      /* G = G / 2 */
    D_A(A6PTR) *= DESCR; D_F(A6PTR) = D_V(A6PTR) = 0;
    /* XCL = M = N - G */
    D_A(XCL) = D_A(ZCL) - D_A(A6PTR); D_F(XCL) = D_V(XCL) = 0;

sort7:;
    MOVD(TCL, ZEROCL);                              /* K = 0 */
    D_A(A7PTR) = DESCR; D_F(A7PTR) = D_V(A7PTR) = 0;  /* I = DESCR */

sort8:;
    /* J = I + G */
    D_A(LPTR) = D_A(A7PTR) + D_A(A6PTR); D_F(LPTR) = D_V(LPTR) = 0;
    /* F1PTR = index[I], F2PTR = index[J] */
    GETD_B(F1PTR, TPTR, A7PTR);
    GETD_B(F2PTR, TPTR, LPTR);
    /* A1PTR = *F1PTR, A2PTR = *F2PTR */
    { DESCR_t tmp; memcpy(&tmp, A2P(D_A(F1PTR)), sizeof(DESCR_t)); A1PTR = tmp; }
    { DESCR_t tmp; memcpy(&tmp, A2P(D_A(F2PTR)), sizeof(DESCR_t)); A2PTR = tmp; }

sort9:; /* Compare A1PTR and A2PTR */
    /* SETAV DTCL,A1PTR; MOVV DTCL,A2PTR */
    SETAV(DTCL, A1PTR);
    MOVV(DTCL, A2PTR);
    if (DEQL(DTCL, VVDTP)) goto cvv;
    if (DEQL(DTCL, IIDTP)) goto cii;
    if (DEQL(DTCL, RIDTP)) goto cri;
    if (DEQL(DTCL, IRDTP)) goto cirx;
    if (DEQL(DTCL, RRDTP)) goto crr;
    goto coth;

cvv:; /* STRING vs STRING — LEXCMP */
    LOCSP_fn(&XSP, &A1PTR);
    LOCSP_fn(&YSP, &A2PTR);
    { int cmp = LEXCMP_fn(&XSP, &YSP);
      if (cmp < 0)  goto cmplt;
      if (cmp == 0) goto cmpeq;
      goto cmpgt; }

cirx:; /* INTEGER / REAL — convert int to real, then CRR */
    SORT_INTRL(A1PTR);
    goto crr;

cri:; /* REAL / INTEGER — convert int to real, then CRR */
    SORT_INTRL(A2PTR);
    goto crr;

cii:; /* INTEGER / INTEGER */
    if (D_A(A1PTR) < D_A(A2PTR)) goto cmplt;
    if (D_A(A1PTR) == D_A(A2PTR)) goto cmpeq;
    goto cmpgt;

crr:; /* REAL / REAL */
    if (isnan(D_R(A1PTR))) goto crr1;
    if (isnan(D_R(A2PTR))) goto cmplt;   /* R1 OK, R2 NaN → R1 < R2 */
    if (D_R(A1PTR) < D_R(A2PTR)) goto cmplt;
    if (D_R(A1PTR) == D_R(A2PTR)) goto cmpeq;
    goto cmpgt;

crr1:; /* R1 NaN — check R2 */
    if (isnan(D_R(A2PTR))) goto cmpeq;
    goto cmpgt;

cmpeq:; /* Elements equal — compare array position (ascending always) */
    if (D_A(F1PTR) <= D_A(F2PTR)) goto cmpnxt;
    goto swap;

cmpgt:; /* A1 > A2 */
    if (D_A(SCL) == 0) goto swap;   /* SORT: swap when gt */
    goto cmpnxt;                     /* RSORT: keep when gt */

cmplt:; /* A1 < A2 */
    if (D_A(SCL) == 0) goto cmpnxt; /* SORT: keep when lt */
    /* RSORT: fall into swap */

swap:;
    PUTD_B(TPTR, A7PTR, F2PTR);
    PUTD_B(TPTR, LPTR,  F1PTR);
    D_A(TCL)++;                      /* K++ */

cmpnxt:;
    if (D_A(A7PTR) >= D_A(XCL)) goto sort11;
    INCRA(A7PTR, DESCR);
    goto sort8;

sort11:;
    if (D_A(TCL) == 0) goto sort6;
    goto sort7;

coth:; /* Other / mixed data types — check field block */
    if (D_A(A3PTR) == 0) goto coth2;
    /* VCOMPC A1PTR,DATSTA,,,COTH0 — if A1 not user type, try A2 */
    if (D_V(A1PTR) < DATSTA) goto coth0;
    { D_V(DT1CL) = D_V(A1PTR);
      int32_t assoc = locapt_fn(D_A(A3PTR), &DT1CL);
      if (!assoc) { INTR1_fn(); return FAIL; }
      /* GETDC ZPTR,ZPTR,2*DESCR — offset in data structure */
      DT1CL.a.i = assoc; DT1CL.f = 0; DT1CL.v = 0;
      GETDC_B(ZPTR, DT1CL, 2*DESCR);
      D_A(A1PTR) += D_A(ZPTR);
      { DESCR_t tmp; memcpy(&tmp, (char*)A2P(D_A(A1PTR)) + DESCR, sizeof(DESCR_t)); A1PTR = tmp; }
      goto sort9; }

coth0:;
    if (D_V(A2PTR) < DATSTA) goto coth2;
    { D_V(DT1CL) = D_V(A2PTR);
      int32_t assoc = locapt_fn(D_A(A3PTR), &DT1CL);
      if (!assoc) { INTR1_fn(); return FAIL; }
      DT1CL.a.i = assoc; DT1CL.f = 0; DT1CL.v = 0;
      GETDC_B(ZPTR, DT1CL, 2*DESCR);
      D_A(A2PTR) += D_A(ZPTR);
      { DESCR_t tmp; memcpy(&tmp, (char*)A2P(D_A(A2PTR)) + DESCR, sizeof(DESCR_t)); A2PTR = tmp; }
      goto sort9; }

coth2:; /* Types differ or both non-user: compare types as integers */
    if (D_V(A1PTR) == D_V(A2PTR)) goto coth1;
    D_A(A1PTR) = D_V(A1PTR); D_F(A1PTR) = D_V(A1PTR) = 0;
    D_A(A2PTR) = D_V(A2PTR); D_F(A2PTR) = D_V(A2PTR) = 0;
coth1:; /* PCOMP — compare addresses */
    if (D_A(A1PTR) < D_A(A2PTR)) goto cmplt;
    if (D_A(A1PTR) == D_A(A2PTR)) goto cmpeq;
    goto cmpgt;

/* ── SORT12: unravel index table into new array ──────────────────────── */
sort12:;
    /* GETDC XCL,XPTR,2*DESCR — ndim */
    GETDC_B(XCL, XPTR, 2*DESCR);
    /* MULTC WPTR,XCL,DESCR */
    D_A(WPTR) = D_A(XCL) * DESCR; D_F(WPTR) = D_V(WPTR) = 0;
    /* SUM WPTR,XPTR,WPTR */
    SUM(WPTR, XPTR, WPTR);
    D_F(WPTR) = D_F(XPTR); D_V(WPTR) = D_V(XPTR);
    /* INCRA WPTR,3*DESCR — skip header + first row */
    INCRA(WPTR, 3*DESCR);
    /* MOVD LPTR,ONECL; MULTC LPTR,LPTR,DESCR */
    MOVD(LPTR, ONECL);
    D_A(LPTR) *= DESCR; D_F(LPTR) = D_V(LPTR) = 0;

sort13:;
    /* GETD ZPTR,TPTR,LPTR — source sort element pointer */
    GETD_B(ZPTR, TPTR, LPTR);
    /* SUBTRT ZPTR,ZPTR,A5PTR — first element in row */
    SUBTRT(ZPTR, ZPTR, A5PTR);
    /* MOVD YPTR,WPTR */
    MOVD(YPTR, WPTR);
    /* MOVA A6PTR,YCL; MULTC A6PTR,A6PTR,DESCR */
    MOVA(A6PTR, YCL);
    D_A(A6PTR) *= DESCR; D_F(A6PTR) = D_V(A6PTR) = 0;

sort14:; /* Move all elements in this row */
    /* MOVDIC YPTR,0,ZPTR,0 */
    SORT_MOVDIC(YPTR, 0, ZPTR, 0);
    /* SUM ZPTR,ZPTR,A4PTR — advance source by column-stride */
    SUM(ZPTR, ZPTR, A4PTR);
    /* SUM YPTR,YPTR,ZCL — advance dest by no. rows */
    SUM(YPTR, YPTR, ZCL);
    /* DECRA A6PTR,DESCR; AEQLC A6PTR,0,SORT14, */
    DECRA(A6PTR, DESCR);
    if (D_A(A6PTR) != 0) goto sort14;

    INCRA(WPTR, DESCR);          /* INCRA WPTR,DESCR — next row in new array */
    INCRA(LPTR, DESCR);          /* INCRA LPTR,DESCR */
    /* PCOMP LPTR,ZCL,,SORT13,SORT13 — while LPTR <= ZCL */
    if (D_A(LPTR) <= D_A(ZCL)) goto sort13;

    /* Clear scratch pointers — PUSH/POP ZEROCL×4 into YPTR,ZPTR,F1PTR,F2PTR */
    MOVD(YPTR,  ZEROCL);
    MOVD(ZPTR,  ZEROCL);
    MOVD(F1PTR, ZEROCL);
    MOVD(F2PTR, ZEROCL);
    /* BRANCH RTXPTR — return XPTR (new sorted array) */
    return OK;
}

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
