/*
 * func.c — Other functions (v311.sil §19 lines 6322–7037)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M12
 */

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "types.h"
#include "data.h"
#include "func.h"
#include "argval.h"
#include "arena.h"
#include "arrays.h"
#include "strings.h"
#include "symtab.h"
#include "errors.h" /* ARGNER_fn, INTR1_fn, LENERR_fn */

/* External stubs — use signatures from headers where declared */
extern RESULT_t INVOKE_fn(void);
extern RESULT_t INTVAL_fn(void);
extern RESULT_t XYARGS_fn(void);
/* GC_fn declared in arena.h as int32_t(int32_t) */
/* FINDEX_fn declared in symtab.h as int32_t(DESCR_t*) */
static int spec_eq(SPEC_t a, SPEC_t b); /* defined below, used by CNVRT_fn + OPSYN_fn */
/* DTREP_fn declared in symtab.h as SPEC_t*(DESCR_t*) */
extern void       CODSKP_fn(int32_t n);  /* declared in symtab.h */
extern RESULT_t EXPR_fn(void);
extern RESULT_t CMPILE_fn(void);
extern RESULT_t TREPUB_fn(DESCR_t node);
extern void       XCALL_DATE(SPEC_t *sp, DESCR_t arg);
extern void       XCALL_MSTIME(DESCR_t *out);
extern void       XCALL_SBREAL(DESCR_t *out, DESCR_t a, DESCR_t b);
extern void       XCALL_RPLACE(SPEC_t *dst, SPEC_t *tbl, SPEC_t *rep);
extern void       XCALL_REVERSE(SPEC_t *dst, SPEC_t *src);
extern void       XCALL_XSUBSTR(SPEC_t *dst, SPEC_t *src, int32_t off, int32_t len);
extern void       STPRNT_fn(int32_t key, DESCR_t blk, SPEC_t *sp);
/* ICNVTA_fn, CNVTA_fn, CNVAT_fn — TABLE↔ARRAY conversions — in arrays.c */

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
    { /* MULT XCL,XCL,YPTR,AERROR; ACOMP XCL,MLENCL,INTR8 */
        int64_t prod = (int64_t)D_A(XCL) * D_A(YPTR);
        if (prod > D_A(MLENCL)) return FAIL; /* INTR8 (also catches int32 overflow → AERROR) */
        D_A(XCL) = (int32_t)prod;
    }
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
    XCALL_XSUBSTR(&TSP, &XSP, D_A(YPTR), D_A(ZPTR));
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
    if (ACOMPC(XPTR, 0) < 0) { LENERR_fn(); return FAIL; }
    int32_t free_space = GC_fn(D_A(XPTR));
    if (free_space < 0) return FAIL;
    SETAC(ZPTR, free_space); SETVC(ZPTR, I);  /* FN-3: load result into ZPTR */
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── COPY(X) ─────────────────────────────────────────────────────────── */
RESULT_t COPY_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    switch (D_V(XPTR)) { /* types that cannot be copied */
    case S: case I: case R: case N: case K: case E: case T:
        INTR1_fn(); return FAIL;
    default: break;
    }
    int32_t sz = x_bkdata(D_A(XPTR));  /* GETSIZ XCL,XPTR — D_V of block header */
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
    SETAC(DMPPTR, D_A(OBPTR) - DESCR); /* SETAC DMPPTR,OBLIST-DESCR — OBPTR.a=OBLIST */
    while (1) {
        if (D_A(DMPPTR) > D_A(OBEND)) { MOVD(XPTR, NULVCL); return OK; } /* PCOMP DMPPTR,OBEND,RETNUL */
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
    if (ACOMPC(XCL, 1) < 0) { ARGNER_fn(); return FAIL; }
    fn_push(XCL);
    if (VARVUP_fn() == FAIL) { fn_top--; return FAIL; }
    XCL = fn_pop();
    int32_t assoc = locapv_fn(D_A(FNCPL), &XPTR);
    if (!assoc) { UNDF_fn(); return FAIL; } /* UNDF — undefined function */
    SETAC(XPTR, assoc);
    GETDC_B(INCL, XPTR, DESCR);
    D_V(INCL) = D_A(XCL); /* SETVA INCL,XCL — insert actual arg count */
    switch (INVOKE_fn()) {
    case FAIL: return FAIL;        /* exit 1: FAIL */
    case NEMO: MOVD(XPTR, ZPTR); return OK; /* exit 3: RTZPTR — value in ZPTR */
    default:   MOVD(XPTR, ZPTR); return OK; /* exit 2: RTXNAM — name in ZPTR → XPTR */
    }
}

/*====================================================================================================================*/
/* ── ARG2 — shared body for ARG/LOCAL/FIELDS/ARGINT ─────────────────── */
/* SIL ARG2 (line 6368): entered with XPTR=fn-name, XCL=arg-number,
 * stack holding (ZCL=proc-type-check, ALCL=entry-indicator).
 * ARG: PUSH(ONECL,DEFCL)  → ZCL=ONECL, ALCL=DEFCL
 * LOCAL: PUSH(ONECL,ZEROCL,DEFCL) → extra ALCL=DEFCL pop after ARG4
 * FIELDS: PUSH(ZEROCL,ZEROCL,DATCL) → ZCL=ZEROCL, ALCL=DATCL */
static RESULT_t arg2(void)
{
    int32_t assoc = locapv_fn(D_A(FNCPL), &XPTR);
    if (!assoc) { INTR30_fn(); return FAIL; }
    SETAC(XPTR, assoc);
    GETDC_B(XPTR, XPTR, DESCR);   /* get function descriptor */
    GETDC_B(YCL,  XPTR, 0);       /* get procedure descriptor */
    GETDC_B(XPTR, XPTR, DESCR);   /* get definition block */
    ZCL  = fn_pop();               /* restore proc-type-check indicator */
    ALCL = fn_pop();               /* restore entry indicator */
    if (D_A(YCL) != D_A(ZCL)) { INTR30_fn(); return FAIL; } /* AEQL YCL,ZCL,INTR30 */
    D_A(XCL) *= DESCR; D_F(XCL) = D_V(XCL) = 0; /* MULTC XCL,XCL,DESCR */
    D_A(XCL) += 2*DESCR;          /* INCRA XCL,2*DESCR — skip prototype */
    D_A(YCL)  = D_V(YCL); D_F(YCL) = D_V(YCL) = 0; /* SETAV YCL,YCL — arg count from V-field */
    D_A(YCL) *= DESCR;            /* MULTC YCL,YCL,DESCR */
    if (D_A(ALCL) == 0) {
        /* ARG4: defined function path */
        D_A(ZCL) = D_V(*(DESCR_t*)A2P(D_A(XPTR))); /* GETSIZ ZCL,XPTR */
        ALCL = fn_pop();          /* restore entry indicator (LOCAL's extra push) */
        if (D_A(ALCL) != 0)
            D_A(XCL) += D_A(YCL); /* SUM XCL,XCL,YCL — skip formal args for LOCAL */
    } else {
        /* Non-ARG4: INCRA YCL,2*DESCR; MOVD ZCL,YCL */
        D_A(YCL) += 2*DESCR;
        MOVD(ZCL, YCL);
    }
    /* ARG5 */
    if (D_A(XCL) > D_A(ZCL)) return FAIL; /* ACOMP XCL,ZCL,FAIL */
    GETD_B(ZPTR, XPTR, XCL);      /* GETD ZPTR,XPTR,XCL — get the descriptor */
    MOVD(XPTR, ZPTR);
    return OK;                     /* RTZPTR */
}

/*====================================================================================================================*/
/* ── ARGINT — alternate ARG entry used for CALL tracing (SIL 6349) ───── */
/* POP(XPTR,XCL); PUSH(ONECL,DEFCL); BRANCH ARG2                         */
RESULT_t ARGINT_fn(void)
{
    XPTR = fn_pop(); /* POP XPTR */
    XCL  = fn_pop(); /* POP XCL  */
    fn_push(ONECL);  /* PUSH(ONECL,DEFCL) */
    fn_push(DEFCL);
    return arg2();   /* BRANCH ARG2 */
}

/*====================================================================================================================*/
/* ── ARG(F,N) ────────────────────────────────────────────────────────── */
RESULT_t ARG_fn(void)
{
    fn_push(ONECL); /* PUSH(ONECL,DEFCL) */
    fn_push(DEFCL);
    /* ARG1: VARVUP → XPTR; INTVAL → XCL; verify XCL > 0 */
    if (VARVUP_fn() == FAIL) { fn_top -= 2; return FAIL; }
    fn_push(XPTR);
    if (INTVAL_fn() == FAIL) { fn_top -= 3; return FAIL; }
    MOVD(XCL, XPTR);
    if (ACOMP(ZEROCL, XCL) >= 0) { fn_top -= 3; return FAIL; } /* must be > 0 */
    XPTR = fn_pop();
    return arg2();
}

/*====================================================================================================================*/
/* ── LOCAL(F,N) ──────────────────────────────────────────────────────── */
RESULT_t LOCAL_fn(void)
{
    fn_push(ONECL); fn_push(ZEROCL); fn_push(DEFCL); /* PUSH(ONECL,ZEROCL,DEFCL) */
    if (VARVUP_fn() == FAIL) { fn_top -= 3; return FAIL; }
    fn_push(XPTR);
    if (INTVAL_fn() == FAIL) { fn_top -= 4; return FAIL; }
    MOVD(XCL, XPTR);
    if (ACOMP(ZEROCL, XCL) >= 0) { fn_top -= 4; return FAIL; }
    XPTR = fn_pop();
    return arg2();
}

/*====================================================================================================================*/
/* ── FIELDS(F,N) ─────────────────────────────────────────────────────── */
RESULT_t FIELDS_fn(void)
{
    fn_push(ZEROCL); fn_push(ZEROCL); fn_push(DATCL); /* PUSH(ZEROCL,ZEROCL,DATCL) */
    if (VARVUP_fn() == FAIL) { fn_top -= 3; return FAIL; }
    fn_push(XPTR);
    if (INTVAL_fn() == FAIL) { fn_top -= 4; return FAIL; }
    MOVD(XCL, XPTR);
    if (ACOMP(ZEROCL, XCL) >= 0) { fn_top -= 4; return FAIL; }
    XPTR = fn_pop();
    return arg2();
}

/* ── DMP(N) — v311.sil §19 line 6699 ────────────────────────────────
 * INTVAL XPTR → if 0, null (no dump); else fall into DUMP. */
RESULT_t DMP_fn(void)
{
    if (INTVAL_fn() == FAIL) return FAIL;       /* RCALL XPTR,INTVAL,,FAIL */
    if (XPTR.a.i == 0) { MOVD(XPTR, NULVCL); return OK; } /* AEQLC XPTR,0,,RETNUL */
    return DUMP_fn();                            /* BRANCH DUMP */
}

/* ── DUMP() — v311.sil §19 line 6702 ────────────────────────────────
 * Walk OBLIST bins; for each non-null string value, print "name = value".
 * snobol4.c: DUMP(ret_t retval) lines 9201-9264. */
RESULT_t DUMP_fn(void)
{
    /* SETAC WPTR,OBLIST-DESCR — WPTR = OBPTR.a - DESCR (one slot before first bin) */
    DESCR_t wptr, yptr;
    wptr.a.i = OBPTR.a.i - DESCR;
    wptr.f = 0; wptr.v = 0;

L_DMPB:
    /* PCOMP WPTR,OBEND,RETNUL — if WPTR.a > OBEND.a, done */
    if (wptr.a.i > OBEND.a.i) { MOVD(XPTR, NULVCL); return OK; }
    wptr.a.i += DESCR;                          /* INCRA WPTR,DESCR */
    yptr = wptr;                                /* MOVD YPTR,WPTR */

L_DMPA:
    /* GETAC YPTR,YPTR,LNKFLD — follow link field */
    {
        int32_t link;
        memcpy(&link, A2P(yptr.a.i + LNKFLD), sizeof(int32_t));
        yptr.a.i = link;
    }
    if (yptr.a.i == 0) goto L_DMPB;            /* AEQLC YPTR,0,,DMPB */

    /* GETDC XPTR,YPTR,DESCR — get value (one DESCR past node head) */
    memcpy(&XPTR, A2P(yptr.a.i + DESCR), sizeof(DESCR_t));

    /* DEQL XPTR,NULVCL,,DMPA — skip null values */
    if (deql(XPTR, NULVCL)) goto L_DMPA;

    /* SETLC DMPSP,0 — clear output buffer */
    DMPSP.l = 0;

    /* LOCSP YSP,YPTR — get name specifier from node */
    LOCSP_fn(&YSP, &yptr);

    /* GETLG YCL,YSP; ACOMPC YCL,BUFLEN,DMPOVR,DMPOVR */
    int32_t ycl_name = YSP.l;                  /* YCL = name length — kept for SUM at DMPX */
    if (ycl_name >= BUFLEN) goto L_DMPOVR;

    APDSP_fn(&DMPSP, &YSP);                    /* APDSP DMPSP,YSP */
    APDSP_fn(&DMPSP, &BLEQSP);                 /* APDSP DMPSP,BLEQSP */

    if (XPTR.v == S) goto L_DMPV;              /* VEQLC XPTR,S,,DMPV */
    if (XPTR.v == I) goto L_DMPI;              /* VEQLC XPTR,I,,DMPI */

    /* Else: get type representation via DTREP */
    { SPEC_t *rep = DTREP_fn(&XPTR);           /* RCALL A1PTR,DTREP,XPTR */
      if (rep) YSP = *rep;                     /* GETSPC YSP,A1PTR,0 */
      else     YSP.l = 0; }

L_DMPX: {
    int32_t xcl_val = YSP.l;                   /* GETLG XCL,YSP */
    int32_t ycl_total = ycl_name + xcl_val;    /* SUM YCL,YCL,XCL — name_len + val_len */
    if (ycl_total > BUFLEN) goto L_DMPOVR;     /* ACOMPC YCL,BUFLEN,DMPOVR */
    { DESCR_t xcl; xcl.a.i = xcl_val; xcl.f = 0; xcl.v = 0; (void)xcl; }
    APDSP_fn(&DMPSP, &YSP);                    /* APDSP DMPSP,YSP */
    /* VEQLC XPTR,T,DMPRT — table? */
    if (XPTR.v != T) goto L_DMPRT;
    /* TESTFI XPTR,FRZN,DMPRT — frozen? */
    { DESCR_t *hdr = (DESCR_t *)A2P(XPTR.a.i);
      if (!(hdr->f & FRZN)) goto L_DMPRT; }
    APDSP_fn(&DMPSP, &FRZNSP);                 /* APDSP DMPSP,FRZNSP */
    }

L_DMPRT:
    STPRNT_fn(IOKEY.a.i, OUTBLK, &DMPSP);     /* STPRNT IOKEY,OUTBLK,DMPSP */
    goto L_DMPA;

L_DMPV: {
    SPEC_t ysp2;
    LOCSP_fn(&ysp2, &XPTR);                    /* LOCSP YSP,XPTR */
    int32_t xcl_str = ysp2.l;                  /* GETLG XCL,YSP */
    if (ycl_name + xcl_str > BUFLEN) goto L_DMPOVR; /* SUM YCL,YCL,XCL; ACOMPC */
    APDSP_fn(&DMPSP, &QTSP);                   /* APDSP DMPSP,QTSP */
    APDSP_fn(&DMPSP, &ysp2);                   /* APDSP DMPSP,YSP */
    APDSP_fn(&DMPSP, &QTSP);                   /* APDSP DMPSP,QTSP */
    goto L_DMPRT; }

L_DMPI:
    INTSPC_fn(&YSP, &XPTR);                    /* INTSPC YSP,XPTR */
    goto L_DMPX;

L_DMPOVR:
    /* OUTPUT OUTPUT,PRTOVF — print overflow message */
    { size_t n = strlen(PRTOVF); fwrite(PRTOVF, 1, n, stdout); fputc('\n', stdout); }
    goto L_DMPA;
}

/* ── DMK — v311.sil §19 line 6747 ───────────────────────────────────
 * Dump all keywords from KNLIST pair list.
 * snobol4.c: DMK(ret_t retval) lines 9270-9307. */
RESULT_t DMK_fn(void)
{
    /* OUTPUT OUTPUT,PKEYF — print caption */
    { size_t n = strlen(PKEYF); fwrite(PKEYF, 1, n, stdout); fputc('\n', stdout); }

    /* GETSIZ XCL,KNLIST — XCL.a = KNLIST.v (size of pair list in bytes) */
    DESCR_t xcl;
    xcl.a.i = KNLIST.v;
    xcl.f   = 0; xcl.v = 0;

L_DMPK1: {
    /* GETD XPTR,KNLIST,XCL — get name entry at offset XCL */
    memcpy(&XPTR, A2P(KNLIST.a.i + xcl.a.i), sizeof(DESCR_t));
    xcl.a.i -= DESCR;                          /* DECRA XCL,DESCR */
    /* GETD YPTR,KNLIST,XCL — get value entry */
    memcpy(&YPTR, A2P(KNLIST.a.i + xcl.a.i), sizeof(DESCR_t));

    INTSPC_fn(&YSP, &YPTR);                    /* INTSPC YSP,YPTR (convert integer) */
    LOCSP_fn(&XSP, &XPTR);                     /* LOCSP XSP,XPTR */
    DMPSP.l = 0;                               /* SETLC DMPSP,0 */
    APDSP_fn(&DMPSP, &AMPSP);                  /* APDSP DMPSP,AMPSP */
    APDSP_fn(&DMPSP, &XSP);                    /* APDSP DMPSP,XSP */
    APDSP_fn(&DMPSP, &BLEQSP);                 /* APDSP DMPSP,BLEQSP */

    /* .IF BLOCKS: VEQLC YPTR,S,,DMPKV — string? (BLOCKS only, skip non-BLOCKS) */
    APDSP_fn(&DMPSP, &YSP);                    /* APDSP DMPSP,YSP */
    /* DMPK2: STPRNT IOKEY,OUTBLK,DMPSP */
    STPRNT_fn(IOKEY.a.i, OUTBLK, &DMPSP);
    xcl.a.i -= DESCR;                          /* DECRA XCL,DESCR */
    /* AEQLC XCL,0,DMPK1,RTN1 — if not zero, loop; else return */
    if (xcl.a.i != 0) goto L_DMPK1;
    }

    MOVD(XPTR, NULVCL); return OK;             /* RTN1 */
}

/* ── CONVERT(X,T) / CODE(S) — stubs ─────────────────────────────────
 * Require compiler re-entry (CMPILE, EXPR, TREPUB) and type-conversion
 * infrastructure. Stubbed until M19 (interpreter/compiler). */
/* ── CNVRT_fn — CONVERT(X,T) dispatcher ─────────────────────────────
 * v311.sil §19 lines 6457–6530  ·  snobol4.c CNVRT() lines 8780–8840
 *
 * Dispatches to conversion helpers based on (source-type, target-type) pair.
 * CODE/EXPRESSION conversions (RECOMP/CONVEX paths) require compiler
 * re-entry — stubbed as FAIL until M19.
 */
RESULT_t CNVRT_fn(void)
{
    /* RCALL ZPTR,ARGVAL,,FAIL — get object */
    if (ARGVAL_fn() == FAIL) return FAIL;
    MOVD(ZPTR, XPTR);
    fn_push(ZPTR);

    /* RCALL YPTR,VARVUP,,FAIL — get target type name */
    if (VARVUP_fn() == FAIL) { fn_top--; return FAIL; }
    MOVD(YPTR, XPTR);
    ZPTR = fn_pop();

    /* LOCAPV XPTR,DTATL,YPTR — look up type code in data-type list */
    int32_t pair = locapv_fn(D_A(DTATL), &YPTR);
    if (!pair) goto L_CNV1;

    /* GETDC XPTR,XPTR,DESCR — get type code descriptor from pair+DESCR */
    memcpy(&XPTR, A2P(pair + DESCR), sizeof(DESCR_t));

    /* SETAV DTCL,ZPTR: .a = D_V(ZPTR) (source type) */
    D_A(DTCL) = (int32_t)D_V(ZPTR);
    D_F(DTCL) = D_V(DTCL) = 0;
    /* MOVV DTCL,XPTR: .v = D_V(XPTR) (target type code) */
    D_V(DTCL) = D_V(XPTR);

    /* Dispatch on (source,target) pair */
    if (DEQL(DTCL, IVDTP)) return CNVIV_fn();   /* INTEGER→STRING  */
    if (DEQL(DTCL, VCDTP)) return FAIL;          /* STRING→CODE   (TODO M19) */
    if (DEQL(DTCL, VEDTP)) return FAIL;          /* STRING→EXPR   (TODO M19) */
    if (DEQL(DTCL, VRDTP)) return CONVR_fn();   /* STRING→REAL     */
    if (DEQL(DTCL, RIDTP)) return CONRI_fn();   /* REAL→INTEGER    */
    if (DEQL(DTCL, IRDTP)) return CONIR_fn();   /* INTEGER→REAL    */
    if (DEQL(DTCL, VIDTP)) return CNVVI_fn();   /* STRING→INTEGER  */
    if (DEQL(DTCL, ATDTP)) return CNVAT_fn();   /* ARRAY→TABLE     */
    if (DEQL(DTCL, TADTP)) return CNVTA_fn();   /* TABLE→ARRAY     */
    /* VEQL ZPTR,XPTR,,RTZPTR — idem-conversion (same type) */
    if ((int32_t)D_V(ZPTR) == (int32_t)D_V(XPTR)) { MOVD(XPTR, ZPTR); return OK; }
    /* VEQLC XPTR,S,FAIL,CNVRTS — if target is STRING, get repr */
    if ((int32_t)D_V(XPTR) == S) return CNVRTS_fn();
    return FAIL;

L_CNV1: /* target type not in DTATL — check for "NUMERIC" */
    {   SPEC_t ysp; LOCSP_fn(&ysp, &YPTR);
        /* LEXCMP YSP,NUMSP,INTR1,,INTR1 — compare against literal "NUMERIC" */
        /* NUMSP is a C string (const char[]), not arena-resident: compare directly */
        int32_t numlen = (int32_t)strlen(NUMSP);
        const char *yptr_str = (const char *)A2P(ysp.a) + ysp.o;
        if (ysp.l != numlen || memcmp(yptr_str, NUMSP, (size_t)numlen) != 0)
            { INTR1_fn(); return FAIL; }
        /* NUMERIC target: I and R pass as-is; S → try parse */
        if (D_V(ZPTR) == I) { MOVD(XPTR, ZPTR); return OK; }
        if (D_V(ZPTR) == R) { MOVD(XPTR, ZPTR); return OK; }
        if (D_V(ZPTR) != S) return FAIL;
        SPEC_t zsp; LOCSP_fn(&zsp, &ZPTR);
        if (SPCINT_fn(&ZPTR, &zsp) == OK) { MOVD(XPTR, ZPTR); return OK; }
        if (SPREAL_fn(&ZPTR, &zsp) == OK) { MOVD(XPTR, ZPTR); return OK; }
        return FAIL;
    }
}

/* ── CODER_fn — CODE(S) ──────────────────────────────────────────────
 * v311.sil §19 CODER (line 6530): VARVAL the argument then fall into RECOMP.
 * RECOMP requires compiler re-entry (CMPILE/EXPR/TREPUB). TODO M19. */
RESULT_t CODER_fn(void) { return FAIL; /* TODO M19: compiler re-entry */ }

/* ── OPSYN(F1,F2,N) ─────────────────────────────────────────────────
 * v311.sil §19 lines 6805–6927  ·  snobol4.c OPSYN() lines 9348–9534
 *
 * OPSYN(object, image, type)
 *   type=0: function synonym  — copy image's proc-descriptor pair into object's slot
 *   type=1: unary  operator synonym
 *   type=2: binary operator synonym
 *
 * Infrastructure: FINDEX_fn, STREAM_fn, BIOPTB/UNOPTB/SBIPTB/BBIOPTB/BSBIPTB all live.
 */
extern RESULT_t VARVUP_fn(void);
extern RESULT_t STREAM_fn(SPEC_t *res, SPEC_t *src, DESCR_t *tbl, int *stype_out);
extern DESCR_t  BIOPTB, SBIPTB, UNOPTB, BBIOPTB, BSBIPTB;
extern DESCR_t  STYPE;       /* scanner: put value after STREAM */
extern SPEC_t   EQLSP;       /* "=" spec — used as max-length gate for binary ops */
extern SPEC_t   LPRNSP;      /* "(" spec — appended when probing unary op table   */

/* spec_eq: lexicographic equality of two SPECs (= LEXEQ macro from csnobol4) */
static int spec_eq(SPEC_t a, SPEC_t b) {
    if (a.l != b.l) return 0;
    if (a.l == 0)   return 1;
    const char *pa = (const char *)A2P(a.a) + a.o;
    const char *pb = (const char *)A2P(b.a) + b.o;
    return memcmp(pa, pb, (size_t)a.l) == 0;
}

/* copy_fslot: OPPD — copy two-DESCR proc-descriptor pair from src slot to dst slot */
static void copy_fslot(int32_t dst_off, int32_t src_off)
{
    DESCR_t d0, d1;
    memcpy(&d0, A2P(src_off),          sizeof(DESCR_t));
    memcpy(&d1, A2P(src_off + DESCR),  sizeof(DESCR_t));
    memcpy(A2P(dst_off),         &d0, sizeof(DESCR_t));
    memcpy(A2P(dst_off + DESCR), &d1, sizeof(DESCR_t));
}

RESULT_t OPSYN_fn(void)
{
    /* RCALL XPTR,VARVUP,,FAIL / PUSH / RCALL YPTR,VARVUP,,FAIL / PUSH / RCALL ZPTR,INTVAL,,FAIL / POP */
    if (VARVUP_fn() == FAIL) return FAIL;
    fn_push(XPTR);
    if (VARVUP_fn() == FAIL) { fn_top--; return FAIL; }
    fn_push(XPTR);                          /* XPTR now holds image (YPTR's value) */
    if (INTVAL_fn() == FAIL) { fn_top -= 2; return FAIL; }
    MOVD(ZPTR, XPTR);                       /* ZPTR = type indicator */
    YPTR = fn_pop();                        /* image name descriptor */
    XPTR = fn_pop();                        /* object name descriptor */

    /* AEQLC XPTR,0,,NONAME — object may not be null */
    if (D_A(XPTR) == 0) { NONAME_fn(); return FAIL; }

    int32_t n = D_A(ZPTR);

    if (n != 1 && n != 2 && n != 0) { INTR30_fn(); return FAIL; } /* AEQLC ZPTR,0,INTR30 */

    /* ── N=0: function synonym ───────────────────────────────────── */
    if (n == 0) {
        /* FINDEX object → get/create its function descriptor slot */
        int32_t xslot = FINDEX_fn(&XPTR);  /* RCALL XPTR,FINDEX,XPTR */
        /* UNBF: FINDEX image */
        int32_t yslot = FINDEX_fn(&YPTR);  /* RCALL YPTR,FINDEX,YPTR */
        /* OPPD: copy image slot → object slot */
        copy_fslot(xslot, yslot);
        MOVD(XPTR, NULVCL); return OK;     /* BRANCH RETNUL */
    }

    /* Helper: probe an operator table for a 1-char name + suffix appended to PROTSP.
     * Returns arena offset of the matching STYPE entry, or 0 if not found (EOS/ERROR). */

    /* ── N=1: unary operator synonym ─────────────────────────────── */
    if (n == 1) {
        /* UNYOP: LOCSP XSP,XPTR; LEQLC XSP,1,UNAF — length must be 1 */
        SPEC_t xsp; LOCSP_fn(&xsp, &XPTR);
        if (xsp.l != 1) goto L_UNAF;           /* length != 1 → FINDEX path */

        /* Build ZSP = PROTSP + XSP + LPRNSP, then STREAM against UNOPTB */
        {   SPEC_t zsp = PROTSP; zsp.l = 0;
            APDSP_fn(&zsp, &xsp);
            APDSP_fn(&zsp, &LPRNSP);
            int stype; SPEC_t tsp;
            if (STREAM_fn(&tsp, &zsp, &UNOPTB, &stype) == FAIL) goto L_UNAF;
            MOVD(XPTR, STYPE);                  /* MOVD XPTR,STYPE */
        }
        /* UNCF: probe image the same way */
L_UNCF: {
            SPEC_t ysp; LOCSP_fn(&ysp, &YPTR);
            if (ysp.l != 1) goto L_UNBF;
            SPEC_t zsp = PROTSP; zsp.l = 0;
            APDSP_fn(&zsp, &ysp);
            APDSP_fn(&zsp, &LPRNSP);
            int stype; SPEC_t tsp;
            if (STREAM_fn(&tsp, &zsp, &UNOPTB, &stype) == FAIL) goto L_UNBF;
            MOVD(YPTR, STYPE);
            goto L_OPPD;
        }
L_UNBF: /* image not found as operator — FINDEX it as a function */
        {   int32_t yslot = FINDEX_fn(&YPTR);
            int32_t xslot = D_A(XPTR);         /* XPTR already has the op slot offset */
            copy_fslot(xslot, yslot);
            MOVD(XPTR, NULVCL); return OK;
        }
L_UNAF: /* object not an operator — FINDEX it */
        {   int32_t xslot = FINDEX_fn(&XPTR);
            XPTR.a.i = xslot;                  /* XPTR.a = slot offset for UNCF */
            goto L_UNCF;
        }
    }

    /* ── N=2: binary operator synonym ────────────────────────────── */
    /* BNYOP: LOCSP XSP,XPTR; check len <= EQLSP.l */
    {
        SPEC_t xsp; LOCSP_fn(&xsp, &XPTR);
        if ((int32_t)xsp.l > (int32_t)EQLSP.l) goto L_BNAF;

        /* Build ZSP = PROTSP + XSP + BLSP, select table by SPITCL/BLOKCL */
        SPEC_t zsp = PROTSP; zsp.l = 0;
        APDSP_fn(&zsp, &xsp);
        APDSP_fn(&zsp, &BLSP);

        DESCR_t *optb;
        if (D_A(SPITCL) != 0)
            optb = (D_A(BLOKCL) != 0) ? &BSBIPTB : &SBIPTB;
        else
            optb = (D_A(BLOKCL) != 0) ? &BBIOPTB : &BIOPTB;

        {   int stype; SPEC_t tsp;
            if (STREAM_fn(&tsp, &zsp, optb, &stype) == FAIL || zsp.l != 0) goto L_BNAF;
        }
        MOVD(XPTR, STYPE);

        /* BNCF: probe image */
L_BNCF: {
            SPEC_t ysp; LOCSP_fn(&ysp, &YPTR);
            if ((int32_t)ysp.l > (int32_t)EQLSP.l) goto L_BNBF2;
            SPEC_t zsp2 = PROTSP; zsp2.l = 0;
            APDSP_fn(&zsp2, &ysp);
            APDSP_fn(&zsp2, &BLSP);
            DESCR_t *optb2;
            if (D_A(SPITCL) != 0)
                optb2 = (D_A(BLOKCL) != 0) ? &BSBIPTB : &SBIPTB;
            else
                optb2 = (D_A(BLOKCL) != 0) ? &BBIOPTB : &BIOPTB;
            int stype; SPEC_t tsp;
            if (STREAM_fn(&tsp, &zsp2, optb2, &stype) == FAIL || zsp2.l != 0) goto L_BNBF2;
            MOVD(YPTR, STYPE);
            goto L_OPPD;
        }
L_BNBF2: /* BNBF: image not in binary op table — try unary FINDEX path */
        if (spec_eq(YSP, BLSP)) {
            /* BNCN: image is blank → concatenation */
            MOVD(YPTR, CONCL);
            goto L_OPPD;
        }
        {   int32_t yslot = FINDEX_fn(&YPTR);
            int32_t xslot = D_A(XPTR);
            copy_fslot(xslot, yslot);
            MOVD(XPTR, NULVCL); return OK;
        }
L_BNAF: /* object not in binary op table */
        if (spec_eq(xsp, BLSP)) {
            /* BNCN path: object is blank → concatenation */
            MOVD(XPTR, CONCL);
            goto L_BNCF;
        }
        {   int32_t xslot = FINDEX_fn(&XPTR);
            XPTR.a.i = xslot;
            goto L_BNCF;
        }
    }

L_OPPD: /* copy image proc-descriptor pair → object slot */
    {   int32_t xslot = D_A(XPTR);
        int32_t yslot = D_A(YPTR);
        copy_fslot(xslot, yslot);
        MOVD(XPTR, NULVCL); return OK;     /* BRANCH RETNUL */
    }
}

/* ── CONVE — convert value to EXPRESSION type ────────────────────────
 * v311.sil §19 CONVE (used by EVAL_fn in argval.c).
 * Requires compiler re-entry. Stubbed until M19. */
RESULT_t CONVE_fn(void) { return FAIL; }

/*====================================================================================================================*/
/* ── CONVR (SIL 6546) — STRING → REAL (or INTEGER) conversion ────────
 * v311.sil:
 *   CONVR  LOCSP   ZSP,ZPTR        Get specifier
 *          SPCINT  ZPTR,ZSP,,CONIR  Try conversion to INTEGER first
 *          SPREAL  ZPTR,ZSP,FAIL,RTZPTR
 * snobol4.c: X_LOCSP(ZSP,ZPTR); SPCINT→CONIR; SPREAL→RTZPTR else FAIL  */
RESULT_t CONVR_fn(void)
{
    SPEC_t zsp;
    LOCSP_fn(&zsp, &ZPTR);                        /* LOCSP ZSP,ZPTR        */
    if (SPCINT_fn(&ZPTR, &zsp) == OK) {           /* SPCINT ZPTR,ZSP,,CONIR */
        /* CONIR: INTRL ZPTR,ZPTR — convert integer to real */
        ZPTR.a.f = (float)ZPTR.a.i;              /* INTRL: int → real     */
        ZPTR.f   = 0;
        ZPTR.v   = R;
        return OK;                                /* BRANCH RTZPTR         */
    }
    if (SPREAL_fn(&ZPTR, &zsp) == OK) return OK; /* SPREAL ZPTR,ZSP,FAIL,RTZPTR */
    return FAIL;
}

/* ── CONIR (SIL 6551) — INTEGER → REAL ───────────────────────────────
 * v311.sil:
 *   CONIR  INTRL   ZPTR,ZPTR       Convert INTEGER to REAL
 *          BRANCH  RTZPTR
 * snobol4.c: D_RV(ZPTR)=(real_t)D_A(ZPTR); D_F=0; D_V=R; BRANCH RTZPTR */
RESULT_t CONIR_fn(void)
{
    ZPTR.a.f = (float)ZPTR.a.i;                  /* INTRL ZPTR,ZPTR       */
    ZPTR.f   = 0;
    ZPTR.v   = R;
    return OK;                                    /* BRANCH RTZPTR         */
}

/* ── CONRI (SIL 6554) — REAL → INTEGER ───────────────────────────────
 * v311.sil:
 *   CONRI  RLINT   ZPTR,ZPTR,FAIL,RTZPTR
 * snobol4.c: CLR_MATH_ERROR; cast; if MATH_ERROR →FAIL; else →RTZPTR   */
RESULT_t CONRI_fn(void)
{
    float f = ZPTR.a.f;
    if (f >= 2147483648.0f || f < -2147483648.0f) return FAIL; /* RLINT overflow */
    ZPTR.a.i = (int32_t)f;
    ZPTR.f   = 0;
    ZPTR.v   = I;
    return OK;                                    /* BRANCH RTZPTR         */
}

/* ── CNVIV (SIL 6557) — INTEGER → STRING ─────────────────────────────
 * v311.sil:
 *   CNVIV  RCALL   ZPTR,GNVARI,ZPTR,RTZPTR
 * snobol4.c: SAVSTK; PUSH(ZPTR); GNVARI(ZPTR)==1 →RTZPTR               */
RESULT_t CNVIV_fn(void)
{
    int32_t off = GNVARI_fn(ZPTR.a.i);           /* RCALL ZPTR,GNVARI,ZPTR */
    if (!off) return FAIL;
    SETAC(ZPTR, off);
    SETVC(ZPTR, S);
    return OK;                                    /* BRANCH RTZPTR         */
}

/* ── CNVVI (SIL 6560) — STRING → INTEGER (or REAL→INTEGER via CONRI) ──
 * v311.sil:
 *   CNVVI  LOCSP   ZSP,ZPTR        Get specifier
 *          SPCINT  ZPTR,ZSP,,RTZPTR  Convert STRING to INTEGER
 *          SPREAL  ZPTR,ZSP,FAIL,CONRI  Try conversion to REAL
 * snobol4.c: X_LOCSP; SPCINT→RTZPTR; SPREAL→CONRI else FAIL            */
RESULT_t CNVVI_fn(void)
{
    SPEC_t zsp;
    LOCSP_fn(&zsp, &ZPTR);                        /* LOCSP ZSP,ZPTR        */
    if (SPCINT_fn(&ZPTR, &zsp) == OK) return OK;  /* SPCINT →RTZPTR        */
    if (SPREAL_fn(&ZPTR, &zsp) == OK) return CONRI_fn(); /* SPREAL →CONRI  */
    return FAIL;
}

/* ── CNVRTS (SIL 6564) — get data type representation as string ───────
 * v311.sil:
 *   CNVRTS RCALL   XPTR,DTREP,ZPTR   Get data type representation
 *          GETSPC  ZSP,XPTR,0         Get specifier
 *          BRANCH  GENVRZ             Go generate variable
 * snobol4.c: SAVSTK;PUSH(ZPTR);DTREP(XPTR); _SPEC(ZSP)=_SPEC(D_A(XPTR)); BRANCH GENVRZ */
RESULT_t CNVRTS_fn(void)
{
    SPEC_t *sp = DTREP_fn(&ZPTR);                 /* RCALL XPTR,DTREP,ZPTR */
    if (!sp) return FAIL;
    XPTR.a.i = P2A(sp);
    ZSP = *sp;                                    /* GETSPC ZSP,XPTR,0     */
    return genvrz();                              /* BRANCH GENVRZ          */
}
