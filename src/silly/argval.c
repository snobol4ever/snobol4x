/*
 * sil_argval.c — argument evaluation procedures (v311.sil §8)
 *
 * Faithful C translation of v311.sil §8 lines 2679–2922.
 * See sil_argval.h for full API documentation.
 *
 * Source oracle: v311.sil §8
 * Reference C:   snobol4-2.3.3/snobol4.c ARGVAL/XYARGS/INTVAL/PATVAL/VARVAL/EXPVAL
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M6
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "arena.h"
#include "strings.h"
#include "symtab.h"
#include "argval.h"

/* ── Forward declarations — later milestones ────────────────────────── */
extern RESULT_t INVOKE_fn(void);   /* §7 — dispatch function call      */
extern RESULT_t PUTIN_fn(void);    /* §15 — perform input association   */
extern RESULT_t CONVE_fn(void);    /* §19 — convert to EXPRESSION type  */

/* ── Internal helpers ────────────────────────────────────────────────── */

/* Inline RLINT: truncate real DESCR to integer in-place.
 * v311.sil RLINT macro / snobol4.c L_INTRI.
 * Returns OK on success, FAIL on overflow/nan.                          */
static RESULT_t rlint(DESCR_t *dp)
{
    /* RLINT macro: convert REAL to INTEGER, fail on overflow (INTR1).
     * Oracle: CLR_MATH_ERROR; cast; if MATH_ERROR → INTR1.
     * We check bounds explicitly — (float)(int32_t) round-trips safely
     * only if value is within [-2^31, 2^31-1]. */
    float f = dp->a.f;
    if (f >= 2147483648.0f || f < -2147483648.0f) return FAIL; /* INTR1 */
    dp->a.i = (int32_t)f;
    dp->f = 0;
    dp->v = I;
    return OK;
}

/*====================================================================================================================*/
/* Fetch next descriptor from object code stream (OCBSCL + OCICL).
 * Advances OCICL by DESCR first (mirrors INCRA OCICL,DESCR / GETD).    */
static DESCR_t oc_fetch(void)
{
    OCICL.a.i += DESCR;
    return *(DESCR_t *)A2P(OCBSCL.a.i + OCICL.a.i);
}

/*====================================================================================================================*/
/* Dereference a name descriptor: get value at offset DESCR from block.
 * Mirrors SIL GETDC XPTR,XPTR,DESCR.                                   */
static void deref_name(DESCR_t *dp)
{
    *dp = *(DESCR_t *)A2P(dp->a.i + DESCR);
}

/*====================================================================================================================*/
/* Check input association list (INATL) for dp.
 * Mirrors SIL LOCAPV ZPTR,INATL,XPTR.
 * Returns 1 if found (ZPTR set), 0 if not.                             */
static int check_input_assoc(const DESCR_t *dp)
{
    if (INSW.a.i == 0) return 0;
    return locapv_fn(P2A(&INATL), (DESCR_t *)dp) != 0;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * ARGVAL_fn — evaluate one argument from object code stream
 * v311.sil ARGVAL (line 2679)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t ARGVAL_fn(void)
{
    XPTR = oc_fetch();
    if (D_F(XPTR) & FNC) {
        switch (INVOKE_fn()) { /* function descriptor — call INVOKE */
        case FAIL: return FAIL;
        case OK: goto argv1; /* exit 2: name returned, dereference  */
        default: return OK; /* exit 3: value returned directly      */
        }
    }
argv1:
    if (check_input_assoc(&XPTR)) {
        ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
        switch (PUTIN_fn()) { /* RCALL XPTR,PUTIN,(ZPTR,XPTR),(FAIL,RTXNAM) */
        case FAIL: return FAIL;
        default: return OK; /* RTXNAM: value in XPTR */
        }
    }
    deref_name(&XPTR);
    return OK;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * EXPVAL_fn — evaluate unevaluated expression
 * v311.sil EXPVAL (line 2699)
 * Saves/restores full interpreter state around sub-evaluation.
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t EXPVAL_fn(void)
{
    DESCR_t sv_ocbscl = OCBSCL, sv_ocicl = OCICL; /* save interpreter state — mirrors SIL PUSH list */
    DESCR_t sv_patbcl = PATBCL, sv_paticl = PATICL;
    DESCR_t sv_wptr = WPTR, sv_xcl = XCL;
    DESCR_t sv_ycl = YCL, sv_tcl = TCL;
    DESCR_t sv_maxlen = MAXLEN, sv_lenfcl = LENFCL;
    DESCR_t sv_pdlptr = PDLPTR, sv_pdlhed = PDLHED;
    DESCR_t sv_namicl = NAMICL, sv_nhedcl = NHEDCL;
    SPEC_t sv_headsp = HEADSP, sv_tsp = TSP;
    SPEC_t sv_txsp = TXSP, sv_xsp = XSP;
    int scl_entry = SCL.a.i; /* save entry indicator */
    RESULT_t rc;
    OCBSCL = XPTR; /* set up new code base from XPTR */
    OCICL.a.i = DESCR;
    PDLHED = PDLPTR;
    NHEDCL = NAMICL;
    { /* fetch first descriptor */
        DESCR_t d = *(DESCR_t *)A2P(OCBSCL.a.i + OCICL.a.i);
        XPTR = d;
    }
    if (D_F(XPTR) & FNC) {
        int saved_scl = SCL.a.i;
        switch (INVOKE_fn()) {
        case FAIL:
            SCL.a.i = 1;
            break;
        case OK:
            SCL.a.i = saved_scl; /* exit 2: came back as name */
            if (SCL.a.i == 0) {
                deref_name(&XPTR); /* EXPEVL entry: just get value */
                SCL.a.i = 2;
            } else {
                ZPTR = XPTR;
                SCL.a.i = 3;
            }
            break;
        default:
            SCL.a.i = saved_scl; /* exit 3: value direct */
            if (SCL.a.i == 0) {
                SCL.a.i = 2;
            } else {
                ZPTR = XPTR;
                SCL.a.i = 3;
            }
            break;
        }
    } else {
        if (scl_entry == 0) { /* not a function */
            if (INSW.a.i != 0 && check_input_assoc(&XPTR)) { /* EXPEVL path */
                ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
                switch (PUTIN_fn()) {
                case FAIL: SCL.a.i = 1; break;
                default: SCL.a.i = 2; break;
                }
            } else {
                deref_name(&XPTR);
                SCL.a.i = 2;
            }
        } else {
            if (INSW.a.i != 0 && check_input_assoc(&XPTR)) {
                ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
                switch (PUTIN_fn()) {
                case FAIL: SCL.a.i = 1; break;
                default: SCL.a.i = 2; break;
                }
            } else {
                deref_name(&XPTR);
                SCL.a.i = 2;
            }
        }
    }
    OCBSCL = sv_ocbscl; OCICL = sv_ocicl; /* restore interpreter state */
    PATBCL = sv_patbcl; PATICL = sv_paticl;
    WPTR = sv_wptr; XCL = sv_xcl;
    YCL = sv_ycl; TCL = sv_tcl;
    MAXLEN = sv_maxlen; LENFCL = sv_lenfcl;
    PDLPTR = sv_pdlptr; PDLHED = sv_pdlhed;
    NAMICL = sv_namicl; NHEDCL = sv_nhedcl;
    HEADSP = sv_headsp; TSP = sv_tsp;
    TXSP = sv_txsp; XSP = sv_xsp;
    switch (SCL.a.i) {
    case 1: rc = FAIL; break;
    case 2: rc = OK; break; /* result in XPTR */
    default: rc = OK; break; /* result in ZPTR — caller checks */
    }
    return rc;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * EXPEVL_fn — expression value context (SCL=0 entry to EXPVAL)
 * v311.sil EXPEVL (line 2757)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t EXPEVL_fn(void)
{
    SCL.a.i = 0;
    return EXPVAL_fn();
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * INTVAL_fn — evaluate argument, coerce to INTEGER
 * v311.sil INTVAL (line 2739)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t INTVAL_fn(void)
{
    XPTR = oc_fetch();
    if (D_F(XPTR) & FNC) {
        switch (INVOKE_fn()) {
        case FAIL: return FAIL;
        case OK: goto intv1;
        default: goto intv2;
        }
    }
intv1:
    if (check_input_assoc(&XPTR)) {
        ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
        if (PUTIN_fn() == FAIL) return FAIL;
    } else {
        deref_name(&XPTR);
    }
intv2:
    if (XPTR.v == I) return OK;
    if (XPTR.v == R) return rlint(&XPTR);
    if (XPTR.v == S) {
        LOCSP_fn(&XSP, &XPTR);
        if (SPCINT_fn(&XPTR, &XSP) == OK) return OK;
        if (SPREAL_fn(&XPTR, &XSP) == OK) return rlint(&XPTR);
    }
    return FAIL; /* INTR1 — caller handles type error */
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * PATVAL_fn — evaluate argument, coerce to PATTERN
 * v311.sil PATVAL (line 2797)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t PATVAL_fn(void)
{
    XPTR = oc_fetch();
    if (D_F(XPTR) & FNC) {
        switch (INVOKE_fn()) {
        case FAIL: return FAIL;
        case OK: goto patv1;
        default: goto patv3;
        }
    }
patv1:
    if (check_input_assoc(&XPTR)) {
        ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
        switch (PUTIN_fn()) {
        case FAIL: return FAIL;
        default: break;
        }
    } else {
        deref_name(&XPTR);
    }
patv3:
    if (XPTR.v == P || XPTR.v == S) return OK;
    if (XPTR.v == I) {
        int32_t off = GNVARI_fn(XPTR.a.i); /* GENVIX: GNVARI → string */
        if (off == 0) return FAIL;
        XPTR.a.i = off; XPTR.f = 0; XPTR.v = S;
        return OK;
    }
    if (XPTR.v == R) {
        REALST_fn(&XSP, &XPTR); /* PATVR: REALST → GENVAR */
        int32_t off = GENVAR_fn(&XSP);
        if (off == 0) return FAIL;
        XPTR.a.i = off; XPTR.f = 0; XPTR.v = S;
        return OK;
    }
    if (XPTR.v == E) {
        int32_t blk = BLOCK_fn(STARSZ.a.i, P); /* wrap in STARPT expression pattern node */
        if (blk == 0) return FAIL;
        memcpy(A2P(blk), A2P(STRPAT.a.i), (size_t)STARSZ.a.i); /* copy STRPAT template into new block */
        *(DESCR_t *)A2P(blk + 4 * DESCR) = XPTR; /* insert expression descriptor at offset 4*DESCR */
        XPTR.a.i = blk; XPTR.f = 0; XPTR.v = P;
        return OK;
    }
    return FAIL; /* INTR1 */
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * VARVAL_fn — evaluate argument, coerce to STRING
 * v311.sil VARVAL (line 2861)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t VARVAL_fn(void)
{
    XPTR = oc_fetch();
    if (D_F(XPTR) & FNC) {
        switch (INVOKE_fn()) {
        case FAIL: return FAIL;
        case OK: goto varv1;
        default: goto varv2;
        }
    }
varv1:
    if (check_input_assoc(&XPTR)) {
        ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
        switch (PUTIN_fn()) {
        case FAIL: return FAIL;
        default: break;
        }
    } else {
        deref_name(&XPTR);
    }
varv2:
    if (XPTR.v == S) return OK; /* VEQLC S → RTXNAM */
    if (XPTR.v == I) { /* VEQLC I → GENVIX */
        int32_t off = GNVARI_fn(XPTR.a.i);
        if (off == 0) return FAIL;
        XPTR.a.i = off; XPTR.f = 0; XPTR.v = S;
        return OK;
    }
    return FAIL; /* INTR1 — all other types (including R) are errors */
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * VARVUP_fn — VARVAL with case-folding
 * v311.sil VARVUP (line 2900) [PLB28][PLB29]
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t VARVUP_fn(void)
{
    if (VARVAL_fn() == FAIL) return FAIL;
    if (CASECL.a.i == 0) return OK; /* case-sensitive: no fold */
    return VPXPTR_fn();
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * VPXPTR_fn — case-fold XPTR string to upper-case variable
 * v311.sil VPXPTR (line 2909) [PLB29]
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t VPXPTR_fn(void)
{
    LOCSP_fn(&SPECR1, &XPTR);
    if (SPECR1.l == 0) return OK; /* null string: return unchanged */
    int32_t len = SPECR1.l; /* allocate scratch space at FRSGPT for upper-case copy */
    int32_t scratch = CONVAR_fn(len);
    if (scratch == 0) return FAIL;
    LOCSP_fn(&XSP, &FRSGPT);
    const char *src = SP_PTR(&SPECR1); /* copy and raise to upper-case */
    char *dst = SP_PTR(&XSP);
    int raised = 0;
    for (int32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 'a'    && c <= 'z') { dst[i] = (char)(c - 32); raised++; }
        else dst[i] = (char)c;
    }
    if (!raised) return OK; /* no lower-case chars: return unchanged */
    *(DESCR_t *)A2P(scratch + DESCR) = *(DESCR_t *)A2P(XPTR.a.i + DESCR); /* copy value and label fields from original */
    *(DESCR_t *)A2P(scratch + ATTRIB) = *(DESCR_t *)A2P(XPTR.a.i + ATTRIB);
    int32_t off = GNVARS_fn(dst, len); /* intern the upper-case string */
    if (off == 0) return FAIL;
    XPTR.a.i = off; XPTR.f = 0; XPTR.v = S;
    return OK;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * XYARGS_fn — evaluate argument pair
 * v311.sil XYARGS (line 2916)
 * ════════════════════════════════════════════════════════════════════════ */
RESULT_t XYARGS_fn(void)
{
    int pass = 0; /* SCL: 0=first arg, 1=second arg */
next_arg:
    OCICL.a.i += DESCR;
    YPTR = *(DESCR_t *)A2P(OCBSCL.a.i + OCICL.a.i);
    if (D_F(YPTR) & FNC) {
        DESCR_t saved_scl = SCL;
        DESCR_t saved_xptr = XPTR;
        SCL.a.i = pass;
        switch (INVOKE_fn()) {
        case FAIL:
            SCL = saved_scl;
            XPTR = saved_xptr;
            return FAIL;
        case OK:
            SCL = saved_scl; /* exit 2: returned as name — dereference and continue */
            XPTR = saved_xptr;
            if (INSW.a.i != 0 && check_input_assoc(&YPTR)) {
                ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
                if (PUTIN_fn() == FAIL) return FAIL;
            } else {
                deref_name(&YPTR);
            }
            break;
        default:
            SCL = saved_scl;
            XPTR = saved_xptr;
            break;
        }
    } else {
        if (INSW.a.i != 0 && check_input_assoc(&YPTR)) {
            ZPTR = *(DESCR_t *)A2P(ZPTR.a.i + DESCR);
            if (PUTIN_fn() == FAIL) return FAIL;
        } else {
            deref_name(&YPTR);
        }
    }
    if (pass != 0) {
        return OK; /* second arg done — XPTR=first, YPTR=second, RTN2 */
    }
    XPTR = YPTR; /* first arg done → save in XPTR, fetch second */
    pass = 1;
    goto next_arg;
}
