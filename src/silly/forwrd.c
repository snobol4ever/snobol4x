/*
 * forwrd.c — Text scanning / card processing (v311.sil §4+§6)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil.
 *
 * CODSKP: fully translated — walks object code skipping N items.
 * FORWRD/FORBLK: translated structure; STREAM calls are platform stubs.
 * NEWCRD: card-type dispatch + listing helpers fully translated.
 * FILCHK: include-stack pop / file-change fully translated.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18b
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "forwrd.h"
#include "arena.h"
#include "strings.h"
/* Platform / table stubs */
/* STREAM_fn: scan TEXTSP using table tbl, set STYPE, advance TEXTSP.
 * Returns OK if a break was found, FAIL on end-of-input (calls err_fn). */
extern RESULT_t STREAM_fn(SPEC_t *res, SPEC_t *src,
                              DESCR_t *tbl, int *stype_out);
extern DESCR_t FRWDTB;    /* forward scan table (skip to nonblank)        */
extern DESCR_t IBLKTB;    /* initial blank table (skip blanks)            */
extern DESCR_t CARDTB;    /* card-type classification table               */
extern DESCR_t LBLTB;     /* label scan table                             */
extern DESCR_t LBLXTB;    /* control-card command scan table              */
extern DESCR_t ELEMTB;    /* element scan table (for CTLADV/CASE)         */
extern DESCR_t INTGTB;    /* integer scan table (for PLUSOPS/LINE)        */
extern RESULT_t STREAD_fn(SPEC_t *sp, DESCR_t unit); /* platform read  */
extern void       STPRNT_fn(int32_t key, DESCR_t blk, SPEC_t *sp);
extern void       XCALL_XRAISP(SPEC_t *sp);  /* uppercase in place       */
extern void       XCALL_IO_PAD(SPEC_t *sp, int32_t width);
extern RESULT_t XCALL_IO_FILE(DESCR_t unit, SPEC_t *fname_out);
extern void       XCALL_OUTPUT_ERR(void);     /* compiler error output    */
extern RESULT_t XCALL_XINCLD(DESCR_t unit, SPEC_t *fname); /* include  */
extern int32_t    GENVAR_fn(const SPEC_t *sp);
extern RESULT_t SPCINT_fn(DESCR_t *dst, const SPEC_t *sp);

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),  sizeof(DESCR_t))
#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + D_A(off_d), sizeof(DESCR_t))

static DESCR_t fw_stk[16];
static int fw_top = 0;
static inline void    fw_push(DESCR_t d) { fw_stk[fw_top++] = d; }
static inline DESCR_t fw_pop(void)        { return fw_stk[--fw_top]; }

static inline int sp_lexcmp(SPEC_t a, SPEC_t b)
{
    return LEXCMP_fn(&a, &b);
}

/*====================================================================================================================*/
static inline void SETLC_sp(SPEC_t *sp, int32_t n) { sp->l = n; }
static RESULT_t forrun(void);  /* forward */

/* ── CODSKP — skip n items in object code ────────────────────────────── */
/*
 * v311.sil CODSKP line 1116:
 *   Walk object code at OCBSCL/OCICL, skipping n top-level items.
 *   Each function descriptor embeds its argument count in its V field;
 *   for each function seen we recurse (or iterate) to skip its args.
 */
void CODSKP_fn(int32_t n)
{
    while (n > 0) {
        INCRA(OCICL, DESCR);
        GETD_B(XCL, OCBSCL, OCICL);
        if (TESTF(XCL, FNC)) {
            int32_t nargs = D_V(XCL); /* Function: recurse to skip its arguments */
            fw_push(YCL);
            CODSKP_fn(nargs);
            YCL = fw_pop();
        }
        n--;
    }
}

/*====================================================================================================================*/
/* ── FORWRD — advance to next non-whitespace character ──────────────── */
/*
 * v311.sil FORWRD line 2214:
 *   STREAM XSP,TEXTSP,FRWDTB,COMP3,FORRUN
 *   On break: set BRTYPE from STYPE, return OK.
 *   On run-out: read next card (FORRUN), then retry.
 */
RESULT_t FORWRD_fn(void)
{
    SPEC_t xsp;
    int stype;
    while (1) {
        RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &stype);
        if (rc == OK) {
            SETAC(BRTYPE, stype);
            return OK; /* FORJRN */
        }
        rc = forrun(); /* FORRUN: read next card */
        if (rc == FAIL) return FAIL; /* COMP1/COMP3 */
    }
}

/*====================================================================================================================*/
/* ── FORBLK — advance past blanks to nonblank ───────────────────────── */
/*
 * v311.sil FORBLK line 2241 (subentry of FORWRD):
 *   STREAM XSP,TEXTSP,IBLKTB,RTN1,FORRUN,FORJRN
 *   Reads blanks from TEXTSP until nonblank found.
 */
RESULT_t FORBLK_fn(void)
{
    /* v311.sil: STREAM XSP,TEXTSP,IBLKTB,RTN1,FORRUN,FORJRN
     * oracle snobol4.c FORBLK:
     *   ST_ERROR (STREAM FAIL, stype=0)      → RTN1  (error return)
     *   ST_EOS   (STREAM OK,   stype=EOSTYP) → FORRUN (read next card, loop)
     *   ST_STOP  (STREAM OK,   stype=NBTYP)  → FORJRN (nonblank found, return OK)
     *
     * IBLKTB fires:
     *   [1] AC_STOP,  put=EOSTYP  → OK, stype=EOSTYP → FORRUN
     *   FRWDTB[6] AC_STOPSH, put=NBTYP → OK, stype=NBTYP → FORJRN
     */
    SPEC_t xsp;
    int stype;
    while (1) {
        RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, &IBLKTB, &stype);
        if (rc == FAIL) return FAIL;    /* ST_ERROR → RTN1 */
        if (stype == EOSTYP) {          /* ST_EOS → FORRUN: read next card, loop */
            SETAC(BRTYPE, stype);
            rc = forrun();
            if (rc == FAIL) return FAIL;
            continue;
        }
        return OK;                      /* ST_STOP/NBTYP → FORJRN: nonblank found */
    }
}

/*====================================================================================================================*/
/* forrun — internal: read a new card, handle listing, set BRTYPE=EOS on EOF */
static RESULT_t forrun(void)
{
    if (AEQLC(UNIT, 0)) { /* Check for input stream */
        MOVD(BRTYPE, EOSCL);
        return OK; /* FOREOS */
    }
    if (!AEQLC(LISTCL, 0)) { /* Print listing if enabled */
        STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
    }
    TEXTSP = NEXTSP; /* SETSP TEXTSP,NEXTSP — switch to next-line buffer */
    RESULT_t rc = STREAD_fn(&TEXTSP, UNIT); /* Read new card */
    if (rc == FAIL) {
        extern int sread_last_eof;
        if (sread_last_eof) return FILCHK_fn(); /* IO_EOF → XLATIN/FILCHK */
        else { extern void COMP1_fn(void); COMP1_fn(); return FAIL; } /* IO_ERR → COMP1 */
    }
    D_A(TMVAL) = TEXTSP.l + STNOSZ; /* Update line-buffer length for listing */
    LNBFSP.l = D_A(TMVAL);
    INCRA(LNNOCL, 1);
    SPEC_t xsp; int stype; /* Classify card type */
    rc = STREAM_fn(&xsp, &TEXTSP, &CARDTB, &stype);
    if (rc == FAIL) {
        if (stype == 0) return FAIL; /* ST_ERROR → COMP3 */
        return forrun();             /* ST_EOS → blank card, loop (L_FORRN0) */
    }
    return NEWCRD_fn(); /* NEWCRD */
}

/*====================================================================================================================*/
/* ── NEWCRD — process card image after type classification ───────────── */
/*
 * v311.sil NEWCRD line 2272:
 *   SELBRA STYPE,(,CMTCRD,CTLCRD,CNTCRD)
 *   Normal card (STYPE=0) → insert statement number in listing.
 *   Comment  (STYPE=CMTTYP=2) → blank the listing number fields.
 *   Control  (STYPE=CTLTYP=3) → parse and execute control command.
 *   Continue (STYPE=CNTTYP=4) → delete leading '-', update listing.
 */
RESULT_t NEWCRD_fn(void)
{
    int32_t stype = D_A(STYPE);
    if (stype == CMTTYP) {
        if (!AEQLC(LISTCL, 0)) { /* Comment card */
            SETLC_sp(&LNOSP, 0); SETLC_sp(&RNOSP, 0);
            APDSP_fn(&LNOSP, &BLNSP);
            APDSP_fn(&RNOSP, &BLNSP);
        }
        return OK; /* RTN1 — don't advance further */
    }
    if (stype == CNTTYP) {
        TEXTSP.o++; TEXTSP.l--; /* Continue card: strip leading '-' */
        if (!AEQLC(LISTCL, 0)) {
            INTSPC_fn(&TSP, &CSTNCL);
            if (!AEQLC(LLIST, 0)) {
                XCALL_IO_PAD(&LNBFSP, CARDSZ + DSTSZ);
                SETLC_sp(&RNOSP, 0);
                APDSP_fn(&RNOSP, &TSP);
            } else {
                SETLC_sp(&LNOSP, 0);
                APDSP_fn(&LNOSP, &TSP);
            }
        }
        return FAIL; /* RTN2 — continue-card: proceed to CMPILE (oracle: falls through, not re-read) */
    }
    if (stype == CTLTYP) {
        /* CTLCRD: v311.sil line 2313
         * FSHRTN TEXTSP,1 — delete control character
         * STREAM XSP,TEXTSP,FRWDTB,COMP3,CMTCRD — get to nonblank
         * AEQLC STYPE,NBTYP,CMTCRD — verify nonbreak
         * STREAM XSP,TEXTSP,LBLXTB,CMTCLR,CMTCLR — break out command
         * XRAISP XSP — uppercase
         * LEXCMP chain ...
         */
        TEXTSP.o++; TEXTSP.l--; /* FSHRTN TEXTSP,1 */
        SPEC_t xsp; int st2;
        if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL) goto cmtclr;
        if (st2 != NBTYP) goto cmtclr;
        if (STREAM_fn(&xsp, &TEXTSP, &LBLXTB, &st2) == FAIL) goto cmtclr;
        XCALL_XRAISP(&xsp);
        if (LEXCMP_fn(&xsp, &UNLSP_sp) == 0) { SETAC(LISTCL, 0); goto cmtret1; } /* LEXCMP chain — compare xsp against each command string */
        if (LEXCMP_fn(&xsp, &LISTSP_sp) == 0) {
            SETAC(LISTCL, 1); SETAC(HIDECL, 0); /* LIST: turn on listing, clear HIDE, check for LEFT/RIGHT */
            if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL) goto cmtclr;
            if (st2 != NBTYP) goto cmtclr;
            if (STREAM_fn(&xsp, &TEXTSP, &LBLXTB, &st2) == FAIL) goto cmtclr;
            XCALL_XRAISP(&xsp);
            if (LEXCMP_fn(&xsp, &LEFTSP_sp) == 0) SETAC(LLIST, 1);
            else SETAC(LLIST, 0);
            goto cmtclr;
        }
        if (LEXCMP_fn(&xsp, &EJCTSP_sp) == 0) {
            if (!AEQLC(LISTCL, 0)) STPRNT_fn(D_A(IOKEY), OUTBLK, &BLSP);
            goto cmtclr;
        }
        if (LEXCMP_fn(&xsp, &ERORSP_sp) == 0) { SETAC(NERRCL, 0); goto cmtclr; }
        if (LEXCMP_fn(&xsp, &NERRSP_sp) == 0) { SETAC(NERRCL, 1); goto cmtclr; }
        if (LEXCMP_fn(&xsp, &HIDESP_sp) == 0) { SETAC(HIDECL, 1); goto cmtret1; }
        if (LEXCMP_fn(&xsp, &CASESP_sp) == 0) {
            if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL) goto case1; /* -CASE [n]: optional integer → CASECL */
            if (st2 != NBTYP) goto case1;
            if (STREAM_fn(&xsp, &TEXTSP, &ELEMTB, &st2) == FAIL) goto case1;
            if (st2 == ILITYP) { SPCINT_fn(&CASECL, &xsp); goto cmtclr; }
            case1: SETAC(CASECL, 0); goto cmtclr;
        }
        if (LEXCMP_fn(&xsp, &INCLSP_sp) == 0 ||
            LEXCMP_fn(&xsp, &COPYSP_sp) == 0) {
            RESULT_t ictmp = CTLADV_fn(&xsp); /* INCLUDE / COPY filename */
            if (ictmp == FAIL) { SETAC(ERRTYP, 29); return FAIL; }
            if (XCALL_XINCLD(UNIT, &xsp) == FAIL) { SETAC(ERRTYP, 30); return FAIL; }
            TRIMSP_fn(&xsp, &xsp);
            int32_t iblk = BLOCK_fn((int32_t)(4*DESCR), B);
            if (!iblk) return FAIL;
            DESCR_t xcl2; SETAC(xcl2, iblk);
            GETDC_B(XCL, xcl2, DESCR); /* XCL = old INCSTK */
            memcpy(A2P(iblk + DESCR), &INCSTK, sizeof(DESCR_t));
            memcpy(A2P(iblk + 2*DESCR), &LNNOCL, sizeof(DESCR_t));
            memcpy(A2P(iblk + 3*DESCR), &FILENM, sizeof(DESCR_t));
            SETAC(INCSTK, iblk);
            int32_t fvar = GENVAR_fn(&xsp);
            if (fvar) { SETAC(FILENM, fvar); SETVC(FILENM, S); }
            SETAC(LNNOCL, 0);
            goto cmtclr;
        }
        if (LEXCMP_fn(&xsp, &SPITSP_sp) == 0) {
            if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL) goto plsop2; /* PLUSOPS [n] */
            if (st2 != NBTYP) goto plsop2;
            if (STREAM_fn(&xsp, &TEXTSP, &INTGTB, &st2) == FAIL) goto plsop1;
            if (st2 == ILITYP) { plsop1: SPCINT_fn(&SPITCL, &xsp); goto cmtclr; }
            plsop2: SETAC(SPITCL, 0); goto cmtclr;
        }
        if (LEXCMP_fn(&xsp, &EXECSP_sp) == 0) { SETAC(EXECCL, 1); goto cmtclr; }
        if (LEXCMP_fn(&xsp, &NEXESP_sp) == 0) { SETAC(EXECCL, 0); goto cmtclr; }
        if (LEXCMP_fn(&xsp, &LINESP_sp) == 0) {
            if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL) goto comp12; /* -LINE lineno ["filenm"] */
            if (st2 != NBTYP) goto comp12;
            if (STREAM_fn(&xsp, &TEXTSP, &INTGTB, &st2) == FAIL) goto comp12;
            if (st2 != ILITYP) goto comp12;
            if (SPCINT_fn(&LNNOCL, &xsp) == FAIL) goto comp12;
            DECRA(LNNOCL, 1);
            RESULT_t ltmp = CTLADV_fn(&xsp);
            if (ltmp != FAIL) {
                int32_t fvar2 = GENVAR_fn(&xsp);
                if (fvar2) { SETAC(FILENM, fvar2); SETVC(FILENM, S); }
            }
            goto cmtclr;
            comp12: SETAC(ERRTYP, 31); return FAIL;
        }
        goto cmtclr; /* None of the above — no-op (BRANCH CMTCLR) */
    cmtret1:
        return OK; /* RTN1 */
    cmtclr:
        SETLC_sp(&LNOSP, 0); SETLC_sp(&RNOSP, 0); /* Clear listing number fields (CMTCLR) */
        APDSP_fn(&LNOSP, &BLNSP);
        APDSP_fn(&RNOSP, &BLNSP);
        return OK;
    }
    if (!AEQLC(LISTCL, 0)) { /* Normal card (stype == 0 or other): update listing statement number */
        MOVD(XCL, CSTNCL); INCRA(XCL, 1);
        INTSPC_fn(&TSP, &XCL);
        if (!AEQLC(LLIST, 0)) {
            XCALL_IO_PAD(&LNBFSP, CARDSZ + DSTSZ);
            SETLC_sp(&RNOSP, 0);
            APDSP_fn(&RNOSP, &TSP);
        } else {
            SETLC_sp(&LNOSP, 0);
            APDSP_fn(&LNOSP, &TSP);
        }
    }
    return FAIL; /* RTN3 — normal card: proceed to CMPILE (oracle: falls through, not re-read) */
}

/*====================================================================================================================*/
/* ── CTLADV — advance to quoted filename on control card ─────────────── */
/*
 * v311.sil CTLADV line 2430:
 *   STREAM XSP,TEXTSP,FRWDTB  — advance to arg
 *   AEQLC  STYPE,NBTYP,RTN1   — no arg → RTN1 (FAIL)
 *   STREAM XSP,TEXTSP,ELEMTB  — get value
 *   AEQLC  STYPE,QLITYP,RTN2  — not quoted → RTN2 (partial OK)
 *   FSHRTN XSP,1              — remove leading quote
 *   SHORTN XSP,1              — remove trailing quote
 *   RTN3                      — OK, result in XSP
 */
RESULT_t CTLADV_fn(SPEC_t *out)
{
    SPEC_t xsp; int st2;
    if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL) return FAIL; /* RTN1 */
    if (st2 != NBTYP) return FAIL; /* RTN1 */
    if (STREAM_fn(&xsp, &TEXTSP, &ELEMTB, &st2) == FAIL) {
        *out = xsp; return OK; /* RTN2 */
    }
    if (st2 != QLITYP) { *out = xsp; return OK; } /* RTN2 */
    xsp.o++; xsp.l--; /* FSHRTN: remove leading quote  */
    xsp.l--; /* SHORTN: remove trailing quote */
    *out = xsp;
    return OK; /* RTN3 */
}

/*====================================================================================================================*/
/* ── FILCHK — handle EOF in compilation ─────────────────────────────── */
/*
 * v311.sil FILCHK line 2350:
 *   If INCSTK != 0: pop include stack, restore LNNOCL/FILENM, RTN2.
 *   Else: query I/O system for next file; RTN1 if none.
 */
RESULT_t FILCHK_fn(void)
{
    if (!AEQLC(INCSTK, 0)) {
        GETDC_B(LNNOCL, INCSTK, 2*DESCR); /* Pop include stack */
        GETDC_B(FILENM, INCSTK, 3*DESCR);
        GETDC_B(INCSTK, INCSTK, DESCR);
        return OK; /* RTN2 */
    }
    SPEC_t fname_sp; /* Query I/O for file change */
    if (XCALL_IO_FILE(UNIT, &fname_sp) == FAIL) return FAIL; /* RTN1 */
    int32_t off = GENVAR_fn(&fname_sp);
    if (off) { SETAC(FILENM, off); SETVC(FILENM, S); }
    SETAC(LNNOCL, 0);
    return OK; /* RTN1 — new file ready */
}

/*====================================================================================================================*/
/* end of forwrd.c */
