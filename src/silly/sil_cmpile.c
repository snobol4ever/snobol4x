/*
 * sil_cmpile.c — Statement compilation (v311.sil §6 CMPILE line 1554)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §6 CMPILE.
 *
 * Covers:
 *   CMPILE / CMPIL0 / CMPILO / CMPILC / CMPILA — label + subject parse
 *   CMPSUB / CMPSB1 / CMPATN / CMPAT2 / CMPAT1 / CMPTGO — pattern phase
 *   CMPFRM / CMPASP / CMPFT — assignment / replacement phase
 *   CMPNGO / CMPGO / CMPGG / CMPUGO — goto phase
 *   CERR1..CERR12 / CDIAG — error handlers
 *
 * ELEMNT/EXPR/EXPR1 are in M18d (sil_expr.c); declared extern here.
 * STREAM table calls (LBLTB, GOTOTB, EOSTB) are extern stubs.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18c
 */

#include <string.h>

#include "sil_types.h"
#include "sil_data.h"
#include "sil_cmpile.h"
#include "sil_trepub.h"
#include "sil_forwrd.h"
#include "sil_arena.h"
#include "sil_strings.h"
#include "sil_symtab.h"

/* M18d stubs */
extern RESULT_t ELEMNT_fn(DESCR_t *out);
extern RESULT_t EXPR_fn(DESCR_t *out);
extern RESULT_t EXPR1_fn(DESCR_t *out);

/* Stream tables (§24 data — stubs until translated) */
extern DESCR_t LBLTB;    /* label scan table                             */
extern DESCR_t GOTOTB;   /* goto field scan table                        */

/* STREAM stub */
extern RESULT_t STREAM_fn(SPEC_t *res, SPEC_t *src,
                              DESCR_t *tbl, int *stype_out);

/* Error output */
extern void STPRNT_fn(int32_t key, DESCR_t blk, SPEC_t *sp);
extern DESCR_t ERRBLK;

#define PUTD_B(base_d, off_d, src) \
    memcpy((char*)A2P(D_A(base_d)) + D_A(off_d), &(src), sizeof(DESCR_t))
#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))
#define AEQLIC(d, off, val) \
    (*(int32_t*)((char*)A2P(D_A(d)) + (off)) == (int32_t)(val))

/* Return codes beyond OK/FAIL */
#define RTN3  3   /* statement compiled OK, no goto           */

static inline int deql_fn(DESCR_t a, DESCR_t b)
    { return D_A(a)==D_A(b) && D_V(a)==D_V(b); }
static inline void SETLC_sp(SPEC_t *sp, int32_t n) { sp->l = n; }
static inline void PUTDC_B(DESCR_t base, int32_t off, DESCR_t src)
    { memcpy((char*)A2P(D_A(base))+off, &src, sizeof(DESCR_t)); }

static void cdiag_inner(void);

/* ── CERR helpers — set EMSGCL then fall to CDIAG ───────────────────── */
static void cerr(const char *msg)
{
    SETAC(EMSGCL, (int32_t)(intptr_t)msg); /* EMSGCL holds a pointer to the error string for CDIAG to print */
    cdiag_inner();
}

/*====================================================================================================================*/
/* ── CMPILE — compile one statement ─────────────────────────────────── */
RESULT_t CMPILE_fn(void)
{
    SETAC(BRTYPE, 0);
    MOVD(BOSCL, CMOFCL);
    if (!AEQLC(HIDECL, 0)) INCRA(CSTNCL, 1); /* AEQLC HIDECL,0,CMPIL0 — increment statement number unless hidden */
    { /* CMPIL0: scan label field */
        SPEC_t xsp; int stype;
        RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, &LBLTB, &stype);
        if (rc == FAIL) { cerr(EMSG1); goto stmt_done; }
        SETAC(STYPE, stype);
        if (xsp.l > 0) {
            INCRA(CMOFCL, DESCR); /* Label found */
            PUTD_B(CMBSCL, CMOFCL, BASECL);
            DESCR_t zptr; SUM(zptr, CMBSCL, CMOFCL); /* Check object-code limit */
            if (D_A(zptr) >= D_A(OCLIM)) {
                DESCR_t new_sz; SUM(new_sz, CMOFCL, CODELT); SETVC(new_sz, C); /* Spill: allocate new block, insert GOTG bridge */
                int32_t nb = BLOCK_fn(D_A(new_sz), C);
                if (!nb) return FAIL;
                SETAC(XCL, nb);
                if (!AEQLC(LPTR, 0)) PUTDC_B(LPTR, ATTRIB, XCL);
                memcpy(A2P(nb), (char*)A2P(D_A(CMBSCL)), (size_t)D_A(CMOFCL));
                PUTDC_B(CMBSCL, DESCR, GOTGCL);
                PUTDC_B(CMBSCL, 2*DESCR, LIT1CL);
                PUTDC_B(CMBSCL, 3*DESCR, XCL);
                INCRA(CMBSCL, 3*DESCR);
                SPLIT_fn(D_A(CMBSCL));
                MOVD(CMBSCL, XCL);
                SUM(OCLIM, CMBSCL, new_sz); DECRA(OCLIM, 7*DESCR);
            }
            SETAC(CMOFCL, 0); SETAC(BOSCL, 0); /* CMPILO: zero offsets */
            int32_t loff = GENVUP_fn(&xsp); /* GENVUP for label variable */
            if (!loff) { cerr(EMSG1); goto stmt_done; }
            SETAC(LPTR, loff); SETVC(LPTR, S);
            if (!AEQLIC(LPTR, ATTRIB, 0)) { /* CMPILC: check for previous definition */
                if (!AEQLC(CNSLCL, 0)) { cerr(EMSG2); goto stmt_done; }
            }
            PUTDC_B(LPTR, ATTRIB, CMBSCL);
            if (deql_fn(LPTR, ENDPTR)) return FAIL; /* RTN2 = END seen */                      /* Check for END label */
        }
    }
    if (FORBLK_fn() == FAIL) { cerr(ILLEOS); goto stmt_done; } /* CMPILA: get to next character */
    if (AEQLC(BRTYPE, EOSTYP)) goto cmpngo; /* EOS? → no subject */
    INCRA(CMOFCL, DESCR); /* Insert INIT at current offset */
    PUTD_B(CMBSCL, CMOFCL, INITCL);
    INCRA(CMOFCL, DESCR);
    MOVD(FRNCL, CMOFCL); /* save offset for failure position */
    INCRA(CMOFCL, DESCR);
    PUTD_B(CMBSCL, CMOFCL, LNNOCL);
    INCRA(CMOFCL, DESCR);
    PUTD_B(CMBSCL, CMOFCL, FILENM);
    if (AEQLC(BRTYPE, NBTYP)) goto cmpsub;
    if (AEQLC(BRTYPE, CLNTYP)) { cerr(EMSG3); goto stmt_done; }
    goto cmpgo;
cmpsub:
    if (ELEMNT_fn(&SUBJND) == FAIL) { cdiag_inner(); goto stmt_done; } /* Compile subject */
    if (FORBLK_fn() == FAIL) { cerr(ILLBRK); goto stmt_done; }
    if (AEQLC(BRTYPE, NBTYP)) {
        if (AEQLC(BRTYPE, EQTYP)) goto cmpfrm; /* CMPSB1 */
        if (TREPUB_fn(SUBJND) == FAIL) { cdiag_inner(); goto stmt_done; } /* Publish subject, check for goto */
        if (AEQLC(BRTYPE, CLNTYP)) goto cmpgo;
        if (AEQLC(BRTYPE, EOSTYP)) { cerr(ILLBRK); goto stmt_done; }
        goto cmpngo;
    }
    if (EXPR_fn(&PATND) == FAIL) { cdiag_inner(); goto stmt_done; } /* Pattern: CMPAT2 */
    if (AEQLC(BRTYPE, EQTYP)) goto cmpasp;
    INCRA(CMOFCL, DESCR); /* Emit SCAN + subject + pattern */
    PUTD_B(CMBSCL, CMOFCL, SCANCL);
    if (TREPUB_fn(SUBJND) == FAIL) { cdiag_inner(); goto stmt_done; }
    if (TREPUB_fn(PATND) == FAIL) { cdiag_inner(); goto stmt_done; }
    goto cmptgo;
cmpfrm:
    if (EXPR_fn(&FORMND) == FAIL) { cdiag_inner(); goto stmt_done; } /* Assignment: compile object */
    INCRA(CMOFCL, DESCR);
    PUTD_B(CMBSCL, CMOFCL, ASGNCL);
    if (TREPUB_fn(SUBJND) == FAIL) { cdiag_inner(); goto stmt_done; }
    goto cmpft;
cmpasp:
    if (EXPR_fn(&FORMND) == FAIL) { cdiag_inner(); goto stmt_done; } /* Pattern replacement: compile object */
    INCRA(CMOFCL, DESCR);
    PUTD_B(CMBSCL, CMOFCL, SJSRCL);
    if (TREPUB_fn(SUBJND) == FAIL) { cdiag_inner(); goto stmt_done; }
    if (TREPUB_fn(PATND) == FAIL) { cdiag_inner(); goto stmt_done; }
cmpft:
    if (TREPUB_fn(FORMND) == FAIL) { cdiag_inner(); goto stmt_done; }
cmptgo:
    if (AEQLC(BRTYPE, EOSTYP)) goto cmpngo;
    if (!AEQLC(BRTYPE, CLNTYP)) { cerr(ILLBRK); goto stmt_done; }
    goto cmpgo;
cmpngo:
    SETVA(CSTNCL, CMOFCL); /* No goto: fill in INIT failure arg, done */
    PUTD_B(CMBSCL, FRNCL, CSTNCL);
    SETVC(CSTNCL, I);
    return RTN3;
cmpgo:
    if (FORWRD_fn() == FAIL) { cerr(ILLEOS); goto stmt_done; } /* Goto field */
    if (AEQLC(BRTYPE, EOSTYP)) goto cmpngo;
    if (AEQLC(BRTYPE, NBTYP)) { cerr(ILLBIN); goto stmt_done; }
    {
        SPEC_t xsp; int stype;
        if (STREAM_fn(&xsp, &TEXTSP, &GOTOTB, &stype) == FAIL) {
            cerr(ILLEOS); goto stmt_done;
        }
        SETAC(STYPE, stype);
        MOVD(GOGOCL, GOTLCL); SETAC(GOBRCL, RPTYP); /* Predict GOTL vs GOTG */
        if (D_A(STYPE) == D_A(GTOCL)) { /* direct goto */
            MOVD(GOGOCL, GOTGCL); SETAC(GOBRCL, RBTYP);
        }
    }
    SETVA(CSTNCL, CMOFCL); /* CMPUGO: fill failure, compile goto expression */
    PUTD_B(CMBSCL, FRNCL, CSTNCL);
    SETVC(CSTNCL, I);
    if (EXPR_fn(&GOTOND) == FAIL) { cdiag_inner(); goto stmt_done; }
    if (!AEQLC(BRTYPE, D_A(GOBRCL))) { cerr(ILLBIN); goto stmt_done; }
    INCRA(CMOFCL, DESCR);
    PUTD_B(CMBSCL, CMOFCL, GOGOCL);
    if (TREPUB_fn(GOTOND) == FAIL) { cdiag_inner(); goto stmt_done; }
    if (FORWRD_fn() == FAIL) { cerr(ILLEOS); goto stmt_done; }
    if (!AEQLC(BRTYPE, EOSTYP)) { cerr(ILLBIN); goto stmt_done; }
    return RTN3;
stmt_done:
    return OK;
}

/*====================================================================================================================*/
/* ── CDIAG — compiler diagnostic handler ────────────────────────────── */
/*
 * v311.sil CDIAG line 1848:
 * Inserts ERROR function at statement start, increments error count,
 * prints error marker and message, advances to next statement.
 */
static void cdiag_inner(void)
{
    INCRA(BOSCL, DESCR); PUTD_B(CMBSCL, BOSCL, ERORCL); /* Insert ERROR + statement number + line + file at statement start */
    INCRA(BOSCL, DESCR); PUTD_B(CMBSCL, BOSCL, CSTNCL);
    INCRA(BOSCL, DESCR); PUTD_B(CMBSCL, BOSCL, LNNOCL);
    INCRA(BOSCL, DESCR); PUTD_B(CMBSCL, BOSCL, FILENM);
    MOVD(CMOFCL, BOSCL);
    INCRA(ESAICL, DESCR);
    if (ACOMP(ESAICL, ESALIM) >= 0) { /* ACOMP ESAICL,ESALIM — excessive errors? */
        SETAC(ERRTYP, 17); /* COMP9 → program error */
        return;
    }
    if (!AEQLC(COMPCL, 0)) { /* Build error pointer line ('^' under offending token) */
        int32_t off = (int32_t)(TEXTSP.o);
        if (off > CARDSZ) off = CARDSZ;
        char *dp = ERRBUF + STNOSZ;
        char *sp = INBUF + STNOSZ;
        int32_t len = off - STNOSZ;
        for (int32_t i = 0; i < len; i++)
            *dp++ = (sp[i] == '\t') ? '\t' : ' ';
        *dp++ = '^';
        ERRSP.l = len + 1;
    }
    STPRNT_fn(D_A(IOKEY), ERRBLK, &LNBFSP); /* Print to stderr */
    STPRNT_fn(D_A(IOKEY), ERRBLK, &ERRSP);
    if (!AEQLC(LISTCL, 0)) {
        STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
        STPRNT_fn(D_A(IOKEY), OUTBLK, &ERRSP);
    }
    { /* Build and print error message */
        SPEC_t tsp;
        memcpy(&tsp, A2P(D_A(EMSGCL)), sizeof(SPEC_t)); /* GETSPC TSP,EMSGCL,0 */
        SETLC_sp(&CERRSP, 0);
        LOCSP_fn(&XSP, &FILENM);
        APDSP_fn(&CERRSP, &XSP);
        APDSP_fn(&CERRSP, &COLSP);
        INTSPC_fn(&XSP, &LNNOCL);
        APDSP_fn(&CERRSP, &XSP);
        APDSP_fn(&CERRSP, &COLSP);
        APDSP_fn(&CERRSP, &SPCSP);
        APDSP_fn(&CERRSP, &STARSP);
        APDSP_fn(&CERRSP, &tsp);
        if (!AEQLC(LISTCL, 0)) {
            STPRNT_fn(D_A(IOKEY), OUTBLK, &CERRSP);
            STPRNT_fn(D_A(IOKEY), OUTBLK, &BLSP);
        }
        STPRNT_fn(D_A(IOKEY), ERRBLK, &CERRSP);
        STPRNT_fn(D_A(IOKEY), ERRBLK, &BLSP);
    }
    { int32_t off = GENVAR_fn(&CERRSP); /* Generate &ERRTEXT */
      if (off) { SETAC(ERRTXT, off); SETVC(ERRTXT, S); } }
    if (!AEQLC(UNIT, 0) && !AEQLC(BRTYPE, EOSTYP)) { /* Skip to end of statement if not at EOS */
        SPEC_t xsp; int stype;
        STREAM_fn(&xsp, &TEXTSP, &EOSTB, &stype);
    }
}

/*====================================================================================================================*/
void CDIAG_fn(void) { cdiag_inner(); }

/* end of sil_cmpile.c */
