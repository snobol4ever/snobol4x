/*
 * asgn.c — Assignment and core operations (v311.sil §17 lines 5828–6101)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §17.
 * Covers: ASGN ASGNV ASGNVV ASGNVN ASGNV1 ASGNC ASGNCV ASGNVP ASGNIC
 *         CONCAT CON4I CON4R CON5 CON5I CON5R CON7 CONVV CONVP CONPV CONPP
 *         IND INDV
 *         KEYWRD KEYN KEYV KEYC KEYT
 *         LIT
 *         NAME
 *         STR
 *
 * Return conventions (SIL exits → C):
 *   RTYPTR  → return OK, result in YPTR
 *   RTZPTR  → return OK, result in ZPTR
 *   RTXPTR  → return OK, result in XPTR (exit 3)
 *   RTXNAM  → return OK, result in XPTR (exit 2, "by name")
 *   FAIL    → return FAIL
 *
 * Since our callers read the result from wherever the proc says, we
 * normalise: OK means result is in XPTR (callers adapt).  For procs
 * that return via RTZPTR/RTYPTR we copy into XPTR before returning OK.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M10
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "asgn.h"
#include "arena.h"
#include "strings.h"
#include "symtab.h"
#include "argval.h"
#include "patval.h"
#include "errors.h" /* INTR1_fn */

/* External stubs resolved at link time */
extern RESULT_t INVOKE_fn(void);
extern RESULT_t INTVAL_fn(void);
extern RESULT_t XYARGS_fn(void);
extern RESULT_t PUTIN_fn(DESCR_t zptr, DESCR_t wptr);
extern void       PUTOUT_fn(DESCR_t yptr, DESCR_t val);
extern RESULT_t TRPHND_fn(DESCR_t atptr);
extern RESULT_t VPXPTR_fn(void);
extern RESULT_t maknod_fn(DESCR_t *out, int32_t blk_off,
                              int32_t len, int32_t alt,
                              int32_t fn_idx, int32_t arg_off);
extern void       cpypat_fn(int32_t dst, int32_t src,
                              int32_t link, int32_t dbase,
                              int32_t sbase, int32_t count);
extern int32_t    lvalue_fn(int32_t pat_off);
/* CODSKP_fn: declared in symtab.h as void(int32_t) */

/* Small operand stack for ASGN */
static DESCR_t asgn_stk[16];
static int     optop_asgn = 0;
static inline void    opush_asgn(DESCR_t d) { asgn_stk[optop_asgn++] = d; }
static inline DESCR_t opop_asgn(void)        { return asgn_stk[--optop_asgn]; }

/* Arena helpers */
#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + D_A(off_d), sizeof(DESCR_t))

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i),    sizeof(DESCR_t))

#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),    sizeof(DESCR_t))

/* DEQL — full descriptor equality (A and V fields) */
static inline int deql(DESCR_t a, DESCR_t b)
{
    return D_A(a) == D_A(b) && D_V(a) == D_V(b);
}

/*====================================================================================================================*/
/* ── ASGN — X = Y ────────────────────────────────────────────────────── */
RESULT_t ASGN_fn(void)
{
    INCRA(OCICL, DESCR); /* INCRA OCICL,DESCR; GETD XPTR,OCBSCL,OCICL */
    GETD_B(XPTR, OCBSCL, OCICL);
    if (!TESTF(XPTR, FNC)) { /* TESTF XPTR,FNC,,ASGNC */
        if (VEQLC(XPTR, K)) { /* ASGNV: VEQLC XPTR,K,,ASGNIC */
            opush_asgn(XPTR); /* ASGNIC: keyword subject — get integer value */
            if (INTVAL_fn() == FAIL) { optop_asgn--; return FAIL; }
            MOVD(YPTR, XPTR); /* save INTVAL result; oracle: RCALL YPTR,INTVAL */
            XPTR = opop_asgn();
            goto asgnvv;
        }
        INCRA(OCICL, DESCR); /* INCRA OCICL,DESCR; GETD YPTR */
        GETD_B(YPTR, OCBSCL, OCICL);
        if (!TESTF(YPTR, FNC)) { /* TESTF YPTR,FNC,,ASGNCV */
            if (!AEQLC(INSW, 0)) { /* ASGNVN: check &INPUT */
                int32_t assoc = locapv_fn(D_A(INATL), &YPTR);
                if (assoc) {
                    DESCR_t zptr; SETAC(zptr, assoc);
                    GETDC_B(zptr, zptr, DESCR);
                    RESULT_t rc = PUTIN_fn(zptr, YPTR);
                    if (rc == OK) goto asgnvv;
                } /* FAIL from PUTIN: fall to ASGNV1 */
            }
            GETDC_B(YPTR, YPTR, DESCR); /* ASGNV1: GETDC YPTR,YPTR,DESCR */
        } else {
            opush_asgn(XPTR); /* ASGNCV: PUSH XPTR; RCALL YPTR,INVOKE,(YPTR),(FAIL,ASGNVP) */
            INCL = YPTR;
            RESULT_t rci = INVOKE_fn();
            if (rci == FAIL) {
                optop_asgn--; /* discard saved XPTR */
                return FAIL;  /* case1: INVOKE failed → BRANCH(FAIL) */
            }
            /* case2: success → ASGNVP: POP XPTR; BRANCH ASGNVN */
            XPTR = opop_asgn();           /* ASGNVP: POP XPTR (restore subject) */
            /* ASGNVN: check &INPUT, else ASGNV1: GETDC YPTR,YPTR,DESCR */
            if (!AEQLC(INSW, 0)) {
                int32_t ia = locapv_fn(D_A(INATL), &YPTR);
                if (ia) {
                    DESCR_t zptr; SETAC(zptr, ia);
                    GETDC_B(zptr, zptr, DESCR);
                    RESULT_t pr = PUTIN_fn(zptr, YPTR);
                    if (pr == OK) goto asgnvv;
                }
            }
            GETDC_B(YPTR, YPTR, DESCR); /* ASGNV1: get value */
        }
    } else {
        /* ASGNC: subject side is function — INVOKE(XPTR),(FAIL,ASGNV,NEMO) */
        INCL = XPTR;
        RESULT_t rc = INVOKE_fn();
        if (rc == FAIL) return FAIL;
        if (rc == NEMO) return NEMO;
        if (VEQLC(XPTR, K)) { /* ASGNV: now XPTR holds evaluated subject; get object side */
            opush_asgn(XPTR);
            if (INTVAL_fn() == FAIL) { optop_asgn--; return FAIL; }
            MOVD(YPTR, XPTR); /* save INTVAL result */
            XPTR = opop_asgn();
            goto asgnvv;
        }
        INCRA(OCICL, DESCR);
        GETD_B(YPTR, OCBSCL, OCICL);
        if (!TESTF(YPTR, FNC)) {
            GETDC_B(YPTR, YPTR, DESCR);
        } else {
            opush_asgn(XPTR); /* ASGNCV: PUSH XPTR; RCALL YPTR,INVOKE,(YPTR),(FAIL,ASGNVP) */
            INCL = YPTR;
            RESULT_t rci2 = INVOKE_fn();
            if (rci2 == FAIL) {
                optop_asgn--;
                return FAIL;
            }
            /* success → ASGNVP: POP XPTR; ASGNVN; ASGNV1 */
            XPTR = opop_asgn();
            if (!AEQLC(INSW, 0)) {
                int32_t ia = locapv_fn(D_A(INATL), &YPTR);
                if (ia) {
                    DESCR_t zptr; SETAC(zptr, ia);
                    GETDC_B(zptr, zptr, DESCR);
                    RESULT_t pr = PUTIN_fn(zptr, YPTR);
                    if (pr == OK) goto asgnvv;
                }
            }
            GETDC_B(YPTR, YPTR, DESCR);
        }
    }
asgnvv:
    PUTDC_B(XPTR, DESCR, YPTR); /* PUTDC XPTR,DESCR,YPTR — perform assignment */
    if (!AEQLC(OUTSW, 0)) { /* &OUTPUT check */
        int32_t assoc = locapv_fn(D_A(OUTATL), &XPTR);
        if (assoc) {
            DESCR_t zptr; SETAC(zptr, assoc);
            GETDC_B(zptr, zptr, DESCR);
            PUTOUT_fn(zptr, YPTR);
        }
    }
    if (ACOMPC(TRAPCL, 0) <= 0) { /* ASGN1: ACOMPC TRAPCL,0,,RTYPTR — skip trace if <=0 [PLB32] */
        int32_t assoc = locapt_fn(D_A(TVALL), &XPTR);
        if (assoc) {
            DESCR_t save_yptr = YPTR;
            SETAC(ATPTR, assoc);
            TRPHND_fn(ATPTR);
            MOVD(YPTR, save_yptr);
        }
    }
    MOVD(XPTR, YPTR); /* RTYPTR: return YPTR as value */
    return OK;
}

/*====================================================================================================================*/
/* ── CONCAT — X Y (concatenation) ───────────────────────────────────── */
RESULT_t CONCAT_fn(void)
{
    if (XYARGS_fn() == FAIL) return FAIL; /* RCALL ,XYARGS,,FAIL */
    if (deql(XPTR, NULVCL)) { MOVD(XPTR, YPTR); return OK; } /* DEQL XPTR,NULVCL,,RTYPTR — null first → return second */
    if (deql(YPTR, NULVCL)) { return OK; } /* DEQL YPTR,NULVCL,,RTXPTR — null second → return first */
    switch (D_V(XPTR)) { /* Coerce XPTR to string/pattern */
    case S: case P: break;
    case I:
        { SPEC_t zsp; INTSPC_fn(&zsp, &XPTR);
          int32_t off = GENVAR_fn(&zsp);
          if (!off) return FAIL;
          SETAC(XPTR, off); SETVC(XPTR, S); }
        break;
    case R:
        { REALST_fn(&REALSP, &XPTR);
          int32_t off = GENVAR_fn(&REALSP);
          if (!off) return FAIL;
          SETAC(XPTR, off); SETVC(XPTR, S); }
        break;
    case E:
        {
            int32_t blk = BLOCK_fn(D_A(STARSZ), P);
            if (!blk) return FAIL;
            memcpy(A2P(blk), A2P(D_A(STRPAT)), (size_t)(D_A(STARSZ)));
            DESCR_t blk_d; SETAC(blk_d, blk);
            PUTDC_B(blk_d, 4*DESCR, XPTR); /* oracle: PUTDC TPTR,4*DESCR,XPTR */
            SETAC(XPTR, blk); SETVC(XPTR, P);
        }
        break;
    default: return FAIL;
    }
    switch (D_V(YPTR)) { /* Coerce YPTR to string/pattern */
    case S: case P: break;
    case I:
        { SPEC_t zsp; INTSPC_fn(&zsp, &YPTR);
          int32_t off = GENVAR_fn(&zsp);
          if (!off) return FAIL;
          SETAC(YPTR, off); SETVC(YPTR, S); }
        break;
    case R:
        { REALST_fn(&REALSP, &YPTR);
          int32_t off = GENVAR_fn(&REALSP);
          if (!off) return FAIL;
          SETAC(YPTR, off); SETVC(YPTR, S); }
        break;
    case E:
        {
            int32_t blk = BLOCK_fn(D_A(STARSZ), P);
            if (!blk) return FAIL;
            memcpy(A2P(blk), A2P(D_A(STRPAT)), (size_t)(D_A(STARSZ)));
            DESCR_t blk_d; SETAC(blk_d, blk);
            PUTDC_B(blk_d, 4*DESCR, YPTR); /* oracle: PUTDC TPTR,4*DESCR,YPTR */
            SETAC(YPTR, blk); SETVC(YPTR, P);
        }
        break;
    default: return FAIL;
    }
    SETAV(DTCL, XPTR); MOVV(DTCL, YPTR); /* CON7: dispatch on type pair */
    if (deql(DTCL, VVDTP)) { /* STRING-STRING: CONVV */
        SPEC_t xsp, ysp;
        LOCSP_fn(&xsp, &XPTR);
        LOCSP_fn(&ysp, &YPTR);
        int32_t xlen = xsp.l, ylen = ysp.l, total = xlen + ylen;
        SETAC(XCL, total);
        if (ACOMP(XCL, MLENCL) > 0) return FAIL; /* INTR8 */
        int32_t soff = CONVAR_fn(total);
        if (!soff) return FAIL;
        DESCR_t zd; SETAC(zd, soff); SETVC(zd, S);
        SPEC_t tsp; LOCSP_fn(&tsp, &zd); tsp.l = 0;
        APDSP_fn(&tsp, &xsp);
        APDSP_fn(&tsp, &ysp);
        int32_t goff = GNVARS_fn((const char*)A2P(tsp.a) + tsp.o, total); /* GENVSZ: GNVARS with total length */
        if (!goff) return FAIL;
        SETAC(ZPTR, goff); SETVC(ZPTR, S);
        MOVD(XPTR, ZPTR); return OK;
    }
    if (deql(DTCL, VPDTP)) { /* STRING-PATTERN (CONVP): wrap string in CHR node then concat */
        SPEC_t tsp; LOCSP_fn(&tsp, &XPTR);
        int32_t tlen = tsp.l;
        int32_t blk = BLOCK_fn(D_A(LNODSZ), P);
        if (!blk) return FAIL;
        maknod_fn(&XPTR, blk, tlen, 0, 4 /*CHR*/, D_A(XPTR));
    } /* fall into CONPP */
    if (deql(DTCL, PVDTP)) { /* PATTERN-STRING (CONPV): wrap YPTR string in CHR node */
        SPEC_t tsp; LOCSP_fn(&tsp, &YPTR);
        int32_t tlen = tsp.l;
        int32_t blk = BLOCK_fn(D_A(LNODSZ), P);
        if (!blk) return FAIL;
        maknod_fn(&YPTR, blk, tlen, 0, 4 /*CHR*/, D_A(YPTR));
    } /* fall into CONPP */
    { /* PATTERN-PATTERN (CONPP): concatenate two patterns */
        int32_t xsz = x_bksize(D_A(XPTR));
        int32_t ysz = x_bksize(D_A(YPTR));
        int32_t tot = xsz + ysz;
        SETAC(TSIZ, tot); SETVC(TSIZ, P);
        int32_t pblk = BLOCK_fn(tot, P);
        if (!pblk) return FAIL;
        SETAC(TPTR, pblk);
        int32_t lv_y = lvalue_fn(D_A(YPTR));
        cpypat_fn(pblk, D_A(XPTR), lv_y, 0, 0, xsz);
        cpypat_fn(pblk, D_A(YPTR), 0, xsz, 0, ysz);
        SETAC(ZPTR, pblk); SETVC(ZPTR, P);
        MOVD(XPTR, ZPTR); return OK;
    }
}

/*====================================================================================================================*/
/* ── IND — $X (indirect reference) ──────────────────────────────────── */
RESULT_t IND_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL;
    switch (D_V(XPTR)) {
    case N: return OK; /* RTXNAM — return by name  */
    case K: return OK; /* KEYWORD is like NAME      */
    case S: break; /* INDV: string              */
    case I:
        { /* GENVIX: RCALL XPTR,GNVARI,XPTR,RTXNAM */
            int32_t off = GNVARI_fn(D_A(XPTR));
            if (!off) return FAIL;
            SETAC(XPTR, off); SETVC(XPTR, S);
        }
        break;
    default: INTR1_fn(); return FAIL; /* illegal data type */
    }
    if (AEQLC(XPTR, 0)) return FAIL; /* NONAME */
    if (AEQLC(CASECL, 0)) return OK;       /* CASECL==0 → RTXNAM (no folding) */
    return VPXPTR_fn();                     /* CASECL!=0 → fold then look up */
}

/*====================================================================================================================*/
/* ── KEYWRD — &X (keyword reference) ────────────────────────────────── */
RESULT_t KEYWRD_fn(void)
{
    INCRA(OCICL, DESCR); /* INCRA OCICL,DESCR; GETD XPTR,OCBSCL,OCICL */
    GETD_B(XPTR, OCBSCL, OCICL);
    if (TESTF(XPTR, FNC)) { /* TESTF XPTR,FNC,,KEYC */
        opush_asgn(XPTR);
        INCL = XPTR;
        RESULT_t rc = INVOKE_fn();
        if (rc == FAIL) { optop_asgn--; return FAIL; }
        if (rc == NEMO) { optop_asgn--; return NEMO; }
        /* case2: INVOKE succeeded, result in XPTR → fall to KEYN */
        (void)opop_asgn(); /* discard saved — result already in XPTR */
    }
    { /* KEYN: look up on unprotected list */
        int32_t assoc = locapv_fn(D_A(KNATL), &XPTR);
        if (assoc) {
            SETAC(XPTR, assoc);
            /* BLOCKS variant: GETDC YPTR,XPTR,DESCR — check value type
             * If integer: KEYN1 → set K; else set N (any value, return by name) */
            DESCR_t yptr; GETDC_B(yptr, XPTR, DESCR);
            SETVC(XPTR, (D_V(yptr) == I) ? K : N);
            return OK; /* RTXNAM */
        }
    }
    { /* KEYV: look up on protected list */
        int32_t assoc = locapv_fn(D_A(KVATL), &XPTR);
        if (!assoc) return FAIL; /* UNKNKW */
        SETAC(ATPTR, assoc);
        GETDC_B(ZPTR, ATPTR, DESCR);
        MOVD(XPTR, ZPTR);
        return OK; /* RTZPTR → normalised to XPTR */
    }
}

/*====================================================================================================================*/
/* ── LIT — 'X' (literal push) ───────────────────────────────────────── */
RESULT_t LIT_fn(void)
{
    INCRA(OCICL, DESCR); /* INCRA OCICL,DESCR; GETD ZPTR,OCBSCL,OCICL; RTZPTR */
    GETD_B(ZPTR, OCBSCL, OCICL);
    MOVD(XPTR, ZPTR);
    return OK;
}

/*====================================================================================================================*/
/* ── NAME — .X (unary name operator) ────────────────────────────────── */
RESULT_t NAME_fn(void)
{
    INCRA(OCICL, DESCR); /* INCRA OCICL,DESCR; GETD ZPTR,OCBSCL,OCICL */
    GETD_B(ZPTR, OCBSCL, OCICL);
    if (!TESTF(ZPTR, FNC)) { /* TESTF ZPTR,FNC,RTZPTR — plain variable: return by name */
        MOVD(XPTR, ZPTR); return OK;
    }
    /* INVOKE(ZPTR): case1=FAIL, case2=RTZPTR (success), case3=NEMO */
    INCL = ZPTR;
    RESULT_t rc = INVOKE_fn();
    if (rc == FAIL) return FAIL;
    if (rc == NEMO) return NEMO;
    /* success: result already in ZPTR */
    MOVD(XPTR, ZPTR); return OK;
}

/*====================================================================================================================*/
/* ── STR — *X (unevaluated expression) ──────────────────────────────── */
RESULT_t STR_fn(void)
{
    SUM(ZPTR, OCBSCL, OCICL); /* SUM ZPTR,OCBSCL,OCICL — pointer to current code position */
    CODSKP_fn(D_A(ONECL)); /* RCALL ,CODSKP,(ONECL) — skip one nesting level */
    SETVC(ZPTR, E); /* SETVC ZPTR,E — mark as EXPRESSION */
    MOVD(XPTR, ZPTR);
    return OK;
}
