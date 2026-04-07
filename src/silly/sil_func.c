/*
 * sil_func.c — Other functions (v311.sil §19 lines 6322–7037)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M12
 */

#include <string.h>
#include <time.h>

#include "sil_types.h"
#include "sil_data.h"
#include "sil_func.h"
#include "sil_argval.h"
#include "sil_arena.h"
#include "sil_strings.h"
#include "sil_symtab.h"

/* External stubs — use signatures from headers where declared */
extern RESULT_t INVOKE_fn(void);
extern RESULT_t INTVAL_fn(void);
extern RESULT_t XYARGS_fn(void);
/* GC_fn declared in sil_arena.h as int32_t(int32_t) */
/* FINDEX_fn declared in sil_symtab.h as int32_t(DESCR_t*) */
/* DTREP_fn declared in sil_symtab.h as SPEC_t*(DESCR_t*) */
extern void       CODSKP_fn(int32_t n);  /* declared in sil_symtab.h */
extern RESULT_t EXPR_fn(void);
extern RESULT_t CMPILE_fn(void);
extern RESULT_t TREPUB_fn(DESCR_t node);
extern void       XCALL_DATE(SPEC_t *sp, DESCR_t arg);
extern void       XCALL_MSTIME(DESCR_t *out);
extern void       XCALL_SBREAL(DESCR_t *out, DESCR_t a, DESCR_t b);
extern void       XCALL_RPLACE(SPEC_t *dst, SPEC_t *tbl, SPEC_t *rep);
extern void       XCALL_REVERSE(SPEC_t *dst, SPEC_t *src);
extern void       XCALL_XSUBSTR(SPEC_t *dst, SPEC_t *src, int32_t off);
extern void       STPRNT_fn(int32_t key, void *blk, SPEC_t *sp);
extern RESULT_t ICNVTA_fn(DESCR_t tbl);

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))

#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),  sizeof(DESCR_t))

#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + D_A(off_d), sizeof(DESCR_t))

#define PUTD_B(base_d, off_d, src) \
    memcpy((char*)A2P(D_A(base_d)) + D_A(off_d), &(src), sizeof(DESCR_t))

#define GETAC_B(dst_i, base_d, off_i) \
    memcpy(&(dst_i), (char*)A2P(D_A(base_d)) + (off_i), sizeof(int32_t))

static inline int deql(DESCR_t a, DESCR_t b)
{
    return D_A(a) == D_A(b) && D_V(a) == D_V(b);
}

/*====================================================================================================================*/
/* Small operand stack */
static DESCR_t fn_stk[16];
static int fn_top = 0;
static inline void    fn_push(DESCR_t d) { fn_stk[fn_top++] = d; }
static inline DESCR_t fn_pop(void)        { return fn_stk[--fn_top]; }

/* GENVRZ: RCALL ZPTR,GENVAR,ZSPPTR,RTZPTR — intern ZSP into ZPTR then return */
static RESULT_t genvrz(void)
{
    SPEC_t zsp; LOCSP_fn(&zsp, &ZPTR);
    int32_t off = GENVAR_fn(&zsp);
    if (!off) return FAIL;
    SETAC(ZPTR, off); SETVC(ZPTR, S);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* GENVSZ: RCALL ZPTR,GNVARS,XCL,RTZPTR — intern TSP of length XCL */
static RESULT_t genvsz(SPEC_t *tsp)
{
    int32_t off = GNVARS_fn((const char*)A2P(tsp->a) + tsp->o, D_A(XCL));
    if (!off) return FAIL;
    SETAC(ZPTR, off); SETVC(ZPTR, S);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── SIZE(S) ─────────────────────────────────────────────────────────── */
RESULT_t SIZE_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    LOCSP_fn(&XSP, &XPTR);
    SETAC(ZPTR, XSP.l); SETVC(ZPTR, I);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── TRIM(S) ─────────────────────────────────────────────────────────── */
RESULT_t TRIM_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    LOCSP_fn(&ZSP, &XPTR);
    TRIMSP_fn(&ZSP, &ZSP);
    return genvrz();
}

/*====================================================================================================================*/
/* ── VDIFFR(X,Y) ─────────────────────────────────────────────────────── */
RESULT_t VDIFFR_fn(void)
{
    if (XYARGS_fn() == FAIL) return FAIL;
    if (deql(XPTR, YPTR)) return FAIL;
    return OK; /* return XPTR — already in XPTR */
}

/*====================================================================================================================*/
/* ── DUPL(S,N) ───────────────────────────────────────────────────────── */
RESULT_t DUPL_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    fn_push(XPTR);
    if (INTVAL_fn() == FAIL) { fn_top--; return FAIL; }
    MOVD(YPTR, XPTR); XPTR = fn_pop();
    if (D_A(YPTR) == 0) { MOVD(XPTR, NULVCL); return OK; } /* ACOMPC YPTR,0,,RETNUL,FAIL */
    if (D_A(YPTR) < 0) return FAIL;
    LOCSP_fn(&XSP, &XPTR);
    D_A(XCL) = XSP.l;
    { /* MULT XCL,XCL,YPTR */
        int64_t prod = (int64_t)D_A(XCL) * D_A(YPTR);
        if (prod > D_A(MLENCL)) return FAIL; /* INTR8 */
        D_A(XCL) = (int32_t)prod;
    }
    if (D_A(XCL) > D_A(MLENCL)) return FAIL;
    int32_t soff = CONVAR_fn(D_A(XCL));
    if (!soff) return FAIL;
    SETAC(ZPTR, soff); SETVC(ZPTR, S);
    LOCSP_fn(&TSP, &ZPTR); TSP.l = 0;
    while (D_A(YPTR) > 0) {
        APDSP_fn(&TSP, &XSP);
        DECRA(YPTR, 1);
    }
    return genvsz(&TSP);
}

/*====================================================================================================================*/
/* ── REVERS(S) ───────────────────────────────────────────────────────── */
RESULT_t REVERS_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    LOCSP_fn(&XSP, &XPTR);
    D_A(ZPTR) = XSP.l;
    if (D_A(ZPTR) == 0) { MOVD(XPTR, NULVCL); return OK; }
    MOVA(XCL, ZPTR);
    int32_t soff = CONVAR_fn(D_A(XCL));
    if (!soff) return FAIL;
    SETAC(ZPTR, soff); SETVC(ZPTR, S);
    LOCSP_fn(&TSP, &ZPTR);
    XCALL_REVERSE(&TSP, &XSP);
    return genvsz(&TSP);
}

/*====================================================================================================================*/
/* ── REPLACE(S1,S2,S3) ───────────────────────────────────────────────── */
RESULT_t RPLACE_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    fn_push(XPTR);
    if (VARVAL_fn() == FAIL) { fn_top--; return FAIL; }
    fn_push(XPTR);
    if (VARVAL_fn() == FAIL) { fn_top -= 2; return FAIL; }
    MOVD(ZPTR, XPTR);
    YPTR = fn_pop(); XPTR = fn_pop();
    if (AEQLC(XPTR, 0)) return OK; /* AEQLC XPTR,0,,RTXPTR — null subject: return as-is */
    LOCSP_fn(&YSP, &YPTR);
    LOCSP_fn(&ZSP, &ZPTR);
    if (ZSP.l != YSP.l) return FAIL; /* LCOMP ZSP,YSP,FAIL,,FAIL — must be same length */
    if (AEQLC(YPTR, 0)) return FAIL;
    LOCSP_fn(&XSP, &XPTR);
    D_A(XCL) = XSP.l;
    int32_t soff = CONVAR_fn(D_A(XCL));
    if (!soff) return FAIL;
    SETAC(ZPTR, soff); SETVC(ZPTR, S);
    LOCSP_fn(&TSP, &ZPTR); TSP.l = 0;
    APDSP_fn(&TSP, &XSP);
    XCALL_RPLACE(&TSP, &YSP, &ZSP);
    return genvsz(&TSP);
}

/*====================================================================================================================*/
/* ── SUBSTR(S,P,L) ───────────────────────────────────────────────────── */
RESULT_t SUBSTR_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    fn_push(XPTR);
    if (INTVAL_fn() == FAIL) { fn_top--; return FAIL; }
    fn_push(XPTR);
    if (INTVAL_fn() == FAIL) { fn_top -= 2; return FAIL; }
    MOVD(ZPTR, XPTR);
    YPTR = fn_pop(); XPTR = fn_pop();
    if (D_A(YPTR) < 1) return FAIL; /* ACOMPC YPTR,1,,,FAIL — position must be >= 1 */
    DECRA(YPTR, 1); /* make zero-based */
    LOCSP_fn(&XSP, &XPTR);
    D_A(WPTR) = XSP.l;
    SUBTRT(WPTR, WPTR, YPTR);
    if (ACOMP(ZPTR, WPTR) > 0) return FAIL;
    if (D_A(ZPTR) < 0) return FAIL; /* ACOMPC ZPTR,0,SSNOFX,,FAIL */
    if (D_A(ZPTR) == 0) MOVA(ZPTR, WPTR);
    if (D_A(ZPTR) == 0) { MOVD(XPTR, NULVCL); return OK; }
    MOVA(XCL, ZPTR);
    int32_t soff = CONVAR_fn(D_A(XCL));
    if (!soff) return FAIL;
    SETAC(ZPTR, soff); SETVC(ZPTR, S);
    LOCSP_fn(&TSP, &ZPTR);
    XCALL_XSUBSTR(&TSP, &XSP, D_A(YPTR));
    return genvsz(&TSP);
}

/*====================================================================================================================*/
/* ── DATE() ──────────────────────────────────────────────────────────── */
RESULT_t DATE_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    XCALL_DATE(&ZSP, XPTR);
    return genvrz();
}

/*====================================================================================================================*/
/* ── DT(X) — DATATYPE(X) ─────────────────────────────────────────────── */
RESULT_t DT_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    MOVD(A2PTR, XPTR);
    MOVV(DT1CL, A2PTR);
    {
        int32_t assoc = locapt_fn(D_A(DTATL), &DT1CL);
        if (!assoc) {
            MOVD(A3PTR, EXTPTR); /* DTEXTN: external data type */
        } else {
            SETAC(A3PTR, assoc);
            GETDC_B(A3PTR, A3PTR, 2*DESCR);
        }
    }
    MOVD(XPTR, A3PTR); return OK;
}

/*====================================================================================================================*/
/* ── TIME() ──────────────────────────────────────────────────────────── */
RESULT_t TIME_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    XCALL_MSTIME(&ZPTR);
    XCALL_SBREAL(&ZPTR, ZPTR, ETMCL);
    SETVC(ZPTR, R);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── COLLECT(N) ──────────────────────────────────────────────────────── */
RESULT_t COLECT_fn(void)
{
    if (INTVAL_fn() == FAIL) return FAIL;
    if (ACOMPC(XPTR, 0) < 0) { SETAC(ERRTYP, 14); return FAIL; } /* ACOMPC XPTR,0,,,LENERR — <0 only */
    if (GC_fn(D_A(XPTR)) < 0) return FAIL;
    SETVC(ZPTR, I);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── COPY(X) ─────────────────────────────────────────────────────────── */
RESULT_t COPY_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    switch (D_V(XPTR)) { /* types that cannot be copied */
    case S: case I: case R: case N: case K: case E: case T:
        return FAIL;
    default: break;
    }
    int32_t sz = x_bksize(D_A(XPTR));
    SETAC(XCL, sz); MOVV(XCL, XPTR);
    int32_t blk = BLOCK_fn(sz, D_V(XPTR));
    if (!blk) return FAIL;
    SETAC(ZPTR, blk);
    memcpy(A2P(blk), A2P(D_A(XPTR)), (size_t)sz);
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── CLEAR() ─────────────────────────────────────────────────────────── */
RESULT_t CLEAR_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    SETAC(DMPPTR, D_A(OBLIST_arr[0]) - DESCR); /* OBLIST-DESCR */    /* Walk every bin in OBLIST, null every variable */
    while (1) {
        if (D_A(DMPPTR) >= D_A(OBEND)) { MOVD(XPTR, NULVCL); return OK; } /* PCOMP DMPPTR,OBEND,RETNUL */
        INCRA(DMPPTR, DESCR);
        MOVD(YPTR, DMPPTR);
        while (1) { /* Walk chain */
            int32_t next;
            GETAC_B(next, YPTR, LNKFLD);
            if (next == 0) break;
            SETAC(YPTR, next);
            PUTDC_B(YPTR, DESCR, NULVCL);
        }
    }
}

/*====================================================================================================================*/
/* ── CMA — (e1,e2,...,en) selection ─────────────────────────────────── */
RESULT_t CMA_fn(void)
{
    SETAV(ZCL, INCL); /* number of arguments */
    while (D_A(ZCL) > 0) {
        fn_push(ZCL); fn_push(OCBSCL); fn_push(OCICL);
        RESULT_t rc = ARGVAL_fn();
        MOVD(OCICL, fn_pop());
        MOVD(OCBSCL, fn_pop());
        MOVD(ZCL, fn_pop());
        if (rc == OK) {
            CODSKP_fn(D_A(ZCL)); /* success — skip remaining args */
            return OK;
        }
        DECRA(ZCL, 1);
        CODSKP_fn(D_A(ONECL));
    }
    return FAIL;
}

/*====================================================================================================================*/
/* ── APPLY(F,A1,...,AN) ───────────────────────────────────────────────── */
RESULT_t APPLY_fn(void)
{
    SETAV(XCL, INCL);
    DECRA(XCL, 1);
    if (ACOMPC(XCL, 1) < 0) { SETAC(ERRTYP, 10); return FAIL; }
    fn_push(XCL);
    if (VARVUP_fn() == FAIL) { fn_top--; return FAIL; }
    XCL = fn_pop();
    int32_t assoc = locapv_fn(D_A(FNCPL), &XPTR);
    if (!assoc) return FAIL; /* UNDF */
    SETAC(XPTR, assoc);
    GETDC_B(INCL, XPTR, DESCR);
    D_V(INCL) = D_A(XCL); /* SETVA INCL,XCL — insert actual arg count */
    if (INVOKE_fn() == FAIL) return FAIL;
    MOVD(ZPTR, XPTR); /* oracle: RCALL ZPTR,INVOKE — result returned via RTZPTR */
    MOVD(XPTR, ZPTR);
    return OK;
}

/*====================================================================================================================*/
/* ── ARG(F,N) / LOCAL(F,N) / FIELDS(F,N) — stubs ─────────────────────
 * These require detailed knowledge of function definition block layout
 * (DEFFNC block, MULTC, GETSIZ for defined vs external).
 * Stubbed: return FAIL until the interpreter's INVOKE path is in place. */
RESULT_t ARG_fn(void)    { return FAIL; }
RESULT_t LOCAL_fn(void)  { return FAIL; }
RESULT_t FIELDS_fn(void) { return FAIL; }

/* ── DMP(N) / DUMP() — stub ──────────────────────────────────────────
 * Full implementation needs STPRNT, DTREP, formatted I/O.
 * Returns null (no-op) for now. */
RESULT_t DMP_fn(void)
{
    if (INTVAL_fn() == FAIL) return FAIL;
    if (AEQLC(XPTR, 0)) { MOVD(XPTR, NULVCL); return OK; }
    MOVD(XPTR, NULVCL); return OK; /* stub: no actual dump */
}
/*====================================================================================================================*/
RESULT_t DUMP_fn(void) { MOVD(XPTR, NULVCL); return OK; }

/* ── CONVERT(X,T) / CODE(S) — stubs ─────────────────────────────────
 * Require compiler re-entry (CMPILE, EXPR, TREPUB) and type-conversion
 * infrastructure. Stubbed until M19 (interpreter/compiler). */
RESULT_t CNVRT_fn(void) { return FAIL; }
RESULT_t CODER_fn(void) { return FAIL; }

/* ── OPSYN(F1,F2,N) — stub ───────────────────────────────────────────
 * Requires operator-table streams (BIOPTB, UNOPTB etc.) not yet built. */
RESULT_t OPSYN_fn(void) { return FAIL; }
