/*
 * sil_pred.c — Predicate functions (v311.sil §18 lines 6102–6321)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §18.
 *
 * Return convention:
 *   RETNUL  → OK, result = NULVCL (null string)
 *   RTXPTR  → OK, result in XPTR
 *   FAIL    → FAIL
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M11
 */

#include <string.h>

#include "sil_types.h"
#include "sil_data.h"
#include "sil_pred.h"
#include "sil_argval.h"
#include "sil_arena.h"
#include "sil_strings.h"
#include "sil_symtab.h"

/* External stubs */
extern RESULT_t XYARGS_fn(void);
/* CODSKP_fn declared in sil_symtab.h as void(int32_t) */
extern void       PAD_fn(int32_t dir, SPEC_t *out, SPEC_t *subj, SPEC_t *pad);

static RESULT_t rpad_common(void);  /* forward */

/* ATTRIB: offset of label field in string block (v311.sil: EQU 2*DESCR) */
#define ATTRIB  (2 * DESCR)

/* Error codes */
#define ERR_ILLEGAL_ARG   10   /* INTR30 */
#define ERR_NEGATIVE      14   /* LENERR */

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))

/* deql — full descriptor equality */
static inline int deql(DESCR_t a, DESCR_t b)
{
    return D_A(a) == D_A(b) && D_V(a) == D_V(b);
}

/*====================================================================================================================*/
/* Small operand stack for procs that PUSH/POP */
static DESCR_t pred_stk[8];
static int pred_top = 0;
static inline void   pred_push(DESCR_t d) { pred_stk[pred_top++] = d; }
static inline DESCR_t pred_pop(void)       { return pred_stk[--pred_top]; }

/* ── DIFFER(X,Y) ─────────────────────────────────────────────────────── */
RESULT_t DIFFER_fn(void)
{
    if (XYARGS_fn() == FAIL) return FAIL;
    if (deql(XPTR, YPTR)) { MOVD(XPTR, NULVCL); return OK; } /* DEQL XPTR,YPTR,RETNUL,FAIL — equal→null, differ→FAIL */
    return FAIL;
}

/*====================================================================================================================*/
/* ── FUNCTN(X) ───────────────────────────────────────────────────────── */
RESULT_t FUNCTN_fn(void)
{
    if (VARVUP_fn() == FAIL) return FAIL;
    {
        int32_t assoc = locapv_fn(D_A(FNCPL), &XPTR);
        if (!assoc) return FAIL;
        SETAC(XPTR, assoc);
        GETDC_B(XPTR, XPTR, DESCR); /* get function descriptor */
        GETDC_B(XPTR, XPTR, 0); /* get link descriptor     */
        if (deql(XPTR, UNDFCL)) { MOVD(XPTR, NULVCL); return OK; } /* AEQL XPTR,UNDFCL,RETNUL,FAIL */
        return FAIL;
    }
}

/*====================================================================================================================*/
/* ── IDENT(X,Y) ──────────────────────────────────────────────────────── */
RESULT_t IDENT_fn(void)
{
    if (XYARGS_fn() == FAIL) return FAIL;
    if (deql(XPTR, YPTR)) return FAIL; /* DEQL XPTR,YPTR,FAIL,RETNUL — equal→FAIL, differ→null */
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── LABEL(X) ────────────────────────────────────────────────────────── */
RESULT_t LABEL_fn(void)
{
    if (VARVUP_fn() == FAIL) return FAIL;
    if (AEQLC(XPTR, 0)) return FAIL;
    GETDC_B(XPTR, XPTR, ATTRIB);
    if (AEQLC(XPTR, 0)) { MOVD(XPTR, NULVCL); return OK; }
    return FAIL;
}

/*====================================================================================================================*/
/* ── LABELC(X) ───────────────────────────────────────────────────────── */
RESULT_t LABELC_fn(void)
{
    if (VARVUP_fn() == FAIL) return FAIL;
    if (AEQLC(XPTR, 0)) return FAIL;
    GETDC_B(XPTR, XPTR, ATTRIB);
    if (AEQLC(XPTR, 0)) return OK; /* RTXPTR — return CODE value */
    return FAIL;
}

/*====================================================================================================================*/
/* ── Lexicographic comparison helpers ───────────────────────────────── */
/* VARVAL×2 pattern shared by LEQ/LGE/LGT/LLE/LLT/LNE */
static int lex_eval2(void)
{
    if (VARVAL_fn() == FAIL) return -2;
    pred_push(XPTR);
    if (VARVAL_fn() == FAIL) { pred_top--; return -2; }
    YPTR = XPTR;
    XPTR = pred_pop();
    return 0;
}

/*====================================================================================================================*/
/* ── LEQ(X,Y) — lexicographic equal ─────────────────────────────────── */
RESULT_t LEQ_fn(void)
{
    if (lex_eval2() == -2) return FAIL;
    if (deql(XPTR, YPTR)) return FAIL;
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── LNE(X,Y) — lexicographic not-equal ─────────────────────────────── */
RESULT_t LNE_fn(void)
{
    if (lex_eval2() == -2) return FAIL;
    if (deql(XPTR, YPTR)) { MOVD(XPTR, NULVCL); return OK; }
    return FAIL;
}

/*====================================================================================================================*/
/* Shared body for LGE/LGT/LLE/LLT — returns lexcmp result or INT_MIN on fail */
static int lex_compare(void)
{
    if (lex_eval2() == -2) return -999;
    if (deql(XPTR, YPTR)) return 0; /* identical → 0 */
    if (AEQLC(XPTR, 0)) return -1; /* null X < anything */
    if (AEQLC(YPTR, 0)) return 1; /* null Y < anything means X > Y */
    SPEC_t xsp, ysp;
    LOCSP_fn(&xsp, &XPTR);
    LOCSP_fn(&ysp, &YPTR);
    return LEXCMP_fn(&xsp, &ysp);
}

/*====================================================================================================================*/
/* ── LGE(X,Y) — X >= Y ──────────────────────────────────────────────── */
RESULT_t LGE_fn(void)
{
    int c = lex_compare();
    if (c == -999) return FAIL;
    if (c >= 0) { MOVD(XPTR, NULVCL); return OK; }
    return FAIL;
}

/*====================================================================================================================*/
/* ── LGT(X,Y) — X > Y ───────────────────────────────────────────────── */
RESULT_t LGT_fn(void)
{
    int c = lex_compare();
    if (c == -999) return FAIL;
    if (c > 0) { MOVD(XPTR, NULVCL); return OK; }
    return FAIL;
}

/*====================================================================================================================*/
/* ── LLE(X,Y) — X <= Y ──────────────────────────────────────────────── */
RESULT_t LLE_fn(void)
{
    int c = lex_compare();
    if (c == -999) return FAIL;
    if (c <= 0) { MOVD(XPTR, NULVCL); return OK; }
    return FAIL;
}

/*====================================================================================================================*/
/* ── LLT(X,Y) — X < Y ───────────────────────────────────────────────── */
RESULT_t LLT_fn(void)
{
    int c = lex_compare();
    if (c == -999) return FAIL;
    if (c < 0) { MOVD(XPTR, NULVCL); return OK; }
    return FAIL;
}

/*====================================================================================================================*/
/* ── NEG — \X (unary negation: succeed if argument fails) ───────────── */
RESULT_t NEG_fn(void)
{
    DESCR_t save_base = OCBSCL, save_idx = OCICL;
    RESULT_t rc = ARGVAL_fn(); /* RCALL ,ARGVAL,,(,FAIL) — fail on success */
    MOVD(OCBSCL, save_base);
    MOVD(OCICL, save_idx);
    if (rc == OK) return FAIL; /* argument succeeded → NEG fails */
    CODSKP_fn(D_A(ONECL)); /* argument failed → skip it and return null */
    MOVD(XPTR, NULVCL);
    return OK;
}

/*====================================================================================================================*/
/* ── QUES — ?X (interrogation: succeed/fail as argument does) ────────── */
RESULT_t QUES_fn(void)
{
    RESULT_t rc = ARGVAL_fn();
    if (rc == FAIL) return FAIL;
    MOVD(XPTR, NULVCL);
    return OK;
}

/*====================================================================================================================*/
/* ── CHAR(N) ─────────────────────────────────────────────────────────── */
RESULT_t CHAR_fn(void)
{
    if (INTVAL_fn() == FAIL) return FAIL;
    MOVD(XCL, XPTR); /* INTVAL result → XCL */
    if (ACOMPC(XCL, 0) <= 0) { SETAC(ERRTYP, ERR_NEGATIVE); return FAIL; } /* ACOMPC XCL,0,,,LENERR — must be > 0 */
    if (ACOMPC(XCL, 256) >= 0) { SETAC(ERRTYP, ERR_ILLEGAL_ARG); return FAIL; } /* ACOMPC XCL,256,INTR30,INTR30 — must be < 256 */
    { /* RCALL XPTR,CONVAR,ONECL — allocate 1-char space */
        int32_t soff = CONVAR_fn(1);
        if (!soff) return FAIL;
        SETAC(XPTR, soff); SETVC(XPTR, S);
    }
    LOCSP_fn(&XSP, &XPTR); /* LOCSP XSP,XPTR; store char */
    {
        char *p = (char*)A2P(XSP.a) + XSP.o;
        *p = (char)(D_A(XCL) & 0xFF);
    }
    { /* RCALL XPTR,GNVARS,ONECL — intern the 1-char string */
        char ch = (char)(D_A(XCL) & 0xFF);
        int32_t off = GNVARS_fn(&ch, 1);
        if (!off) return FAIL;
        SETAC(XPTR, off); SETVC(XPTR, S);
    }
    return OK;
}

/*====================================================================================================================*/
/* ── LPAD(S,N,C) / RPAD(S,N,C) ──────────────────────────────────────── */
RESULT_t LPAD_fn(void) { SETAC(SCL, 0); return rpad_common(); }
RESULT_t RPAD_fn(void) { SETAC(SCL, 1); return rpad_common(); }

static RESULT_t rpad_common(void)
{
    pred_push(SCL);
    if (VARVAL_fn() == FAIL) { pred_top--; return FAIL; } /* Get string arg */
    pred_push(XPTR);
    if (INTVAL_fn() == FAIL) { pred_top -= 2; return FAIL; } /* Get integer length */
    MOVD(ZPTR, XPTR);
    pred_push(ZPTR);
    if (VARVAL_fn() == FAIL) { pred_top -= 3; return FAIL; } /* Get pad character */
    MOVD(WPTR, XPTR);
    ZPTR = pred_pop(); XPTR = pred_pop(); SCL = pred_pop();
    if (ACOMP(ZPTR, MLENCL) > 0) { SETAC(ERRTYP, 8); return FAIL; } /* ACOMP ZPTR,MLENCL,INTR8 */
    if (ACOMP(ZEROCL, ZPTR) > 0) { SETAC(ERRTYP, ERR_NEGATIVE); return FAIL; } /* ACOMP ZEROCL,ZPTR,LENERR — negative length */
    LOCSP_fn(&VSP, &WPTR); /* pad character spec */
    LOCSP_fn(&XSP, &XPTR); /* subject string spec */
    D_A(YPTR) = XSP.l; /* GETLG YPTR,XSP */
    if (ACOMP(YPTR, ZPTR) >= 0) return OK; /* return XPTR unchanged */  /* ACOMP YPTR,ZPTR,RTXPTR,RTXPTR — already long enough? */
    MOVA(XCL, ZPTR); /* Allocate result */
    {
        int32_t soff = CONVAR_fn(D_A(XCL));
        if (!soff) return FAIL;
        SETAC(ZPTR, soff); SETVC(ZPTR, S);
    }
    LOCSP_fn(&TSP, &ZPTR);
    PAD_fn(D_A(SCL), &TSP, &XSP, &VSP); /* PAD_fn(dir, out, subj, pad) */
    { /* GENVSZ: intern result */
        int32_t off = GNVARS_fn((const char*)A2P(TSP.a) + TSP.o, D_A(XCL));
        if (!off) return FAIL;
        SETAC(XPTR, off); SETVC(XPTR, S);
    }
    return OK;
}
