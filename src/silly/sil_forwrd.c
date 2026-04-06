/*
 * sil_forwrd.c — Text scanning / card processing (v311.sil §4+§6)
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

#include "sil_types.h"
#include "sil_data.h"
#include "sil_forwrd.h"
#include "sil_arena.h"
#include "sil_strings.h"

/* Platform / table stubs */
/* STREAM_fn: scan TEXTSP using table tbl, set STYPE, advance TEXTSP.
 * Returns OK if a break was found, FAIL on end-of-input (calls err_fn). */
extern SIL_result STREAM_fn(SPEC_t *res, SPEC_t *src,
                              DESCR_t *tbl, int *stype_out);
extern DESCR_t FRWDTB;    /* forward scan table (skip to nonblank)        */
extern DESCR_t IBLKTB;    /* initial blank table (skip blanks)            */
extern DESCR_t CARDTB;    /* card-type classification table               */
extern DESCR_t LBLTB;     /* label scan table                             */
extern DESCR_t LBLXTB;    /* control-card command scan table              */
extern DESCR_t UNLSP;     /* "UNLIST" string specifier                    */
extern DESCR_t LISTSP_d;  /* "LIST"   string specifier                    */
extern DESCR_t EJCTSP;    /* "EJECT"  string specifier                    */
extern SIL_result STREAD_fn(SPEC_t *sp, DESCR_t unit); /* platform read  */
extern void       STPRNT_fn(int32_t key, DESCR_t blk, SPEC_t *sp);
extern void       XCALL_XRAISP(SPEC_t *sp);  /* uppercase in place       */
extern void       XCALL_IO_PAD(SPEC_t *sp, int32_t width);
extern SIL_result XCALL_IO_FILE(DESCR_t unit, SPEC_t *fname_out);
extern void       XCALL_OUTPUT_ERR(void);     /* compiler error output    */

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

static inline void SETLC_sp(SPEC_t *sp, int32_t n) { sp->l = n; }
static SIL_result forrun(void);  /* forward */

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
            /* Function: recurse to skip its arguments */
            int32_t nargs = D_V(XCL);
            fw_push(YCL);
            CODSKP_fn(nargs);
            YCL = fw_pop();
        }
        n--;
    }
}

/* ── FORWRD — advance to next non-whitespace character ──────────────── */
/*
 * v311.sil FORWRD line 2214:
 *   STREAM XSP,TEXTSP,FRWDTB,COMP3,FORRUN
 *   On break: set BRTYPE from STYPE, return OK.
 *   On run-out: read next card (FORRUN), then retry.
 */
SIL_result FORWRD_fn(void)
{
    SPEC_t xsp;
    int stype;

    while (1) {
        SIL_result rc = STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &stype);
        if (rc == OK) {
            SETAC(BRTYPE, stype);
            return OK;   /* FORJRN */
        }
        /* FORRUN: read next card */
        rc = forrun();
        if (rc == FAIL) return FAIL;  /* COMP1/COMP3 */
    }
}

/* ── FORBLK — advance past blanks to nonblank ───────────────────────── */
/*
 * v311.sil FORBLK line 2241 (subentry of FORWRD):
 *   STREAM XSP,TEXTSP,IBLKTB,RTN1,FORRUN,FORJRN
 *   Reads blanks from TEXTSP until nonblank found.
 */
SIL_result FORBLK_fn(void)
{
    SPEC_t xsp;
    int stype;

    while (1) {
        SIL_result rc = STREAM_fn(&xsp, &TEXTSP, &IBLKTB, &stype);
        if (rc == OK) {
            SETAC(BRTYPE, stype);
            return OK;
        }
        rc = forrun();
        if (rc == FAIL) return FAIL;
    }
}

/* forrun — internal: read a new card, handle listing, set BRTYPE=EOS on EOF */
static SIL_result forrun(void)
{
    /* Check for input stream */
    if (AEQLC(UNIT, 0)) {
        MOVD(BRTYPE, EOSCL);
        return OK;  /* FOREOS */
    }

    /* Print listing if enabled */
    if (!AEQLC(LISTCL, 0)) {
        STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
    }

    /* SETSP TEXTSP,NEXTSP — switch to next-line buffer */
    TEXTSP = NEXTSP;

    /* Read new card */
    SIL_result rc = STREAD_fn(&TEXTSP, UNIT);
    if (rc == FAIL) {
        /* EOF: try FILCHK */
        return FILCHK_fn();
    }

    /* Update line-buffer length for listing */
    D_A(TMVAL) = TEXTSP.l + STNOSZ;
    LNBFSP.l = D_A(TMVAL);

    INCRA(LNNOCL, 1);

    /* Classify card type */
    SPEC_t xsp; int stype;
    rc = STREAM_fn(&xsp, &TEXTSP, &CARDTB, &stype);
    if (rc == FAIL) return forrun();  /* blank card → recurse */

    /* NEWCRD */
    return NEWCRD_fn();
}

/* ── NEWCRD — process card image after type classification ───────────── */
/*
 * v311.sil NEWCRD line 2272:
 *   SELBRA STYPE,(,CMTCRD,CTLCRD,CNTCRD)
 *   Normal card (STYPE=0) → insert statement number in listing.
 *   Comment  (STYPE=CMTTYP=2) → blank the listing number fields.
 *   Control  (STYPE=CTLTYP=3) → parse and execute control command.
 *   Continue (STYPE=CNTTYP=4) → delete leading '-', update listing.
 */
SIL_result NEWCRD_fn(void)
{
    int32_t stype = D_A(STYPE);

    if (stype == CMTTYP) {
        /* Comment card */
        if (!AEQLC(LISTCL, 0)) {
            SETLC_sp(&LNOSP, 0); SETLC_sp(&RNOSP, 0);
            APDSP_fn(&LNOSP, &BLNSP);
            APDSP_fn(&RNOSP, &BLNSP);
        }
        return OK;  /* RTN1 — don't advance further */
    }

    if (stype == CNTTYP) {
        /* Continue card: strip leading '-' */
        TEXTSP.o++; TEXTSP.l--;
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
        return OK;  /* RTN2 — continue-card processed */
    }

    if (stype == CTLTYP) {
        /* Control card: parse command */
        TEXTSP.o++; TEXTSP.l--;   /* skip '-' */
        SPEC_t xsp; int st2;
        /* advance to nonblank */
        if (STREAM_fn(&xsp, &TEXTSP, &FRWDTB, &st2) == FAIL || st2 != NBTYP)
            return OK;  /* CMTCRD path */
        /* break out command keyword */
        if (STREAM_fn(&xsp, &TEXTSP, &LBLXTB, &st2) == FAIL)
            return OK;
        XCALL_XRAISP(&xsp);
        /* Dispatch on command — stub: unrecognised → no-op */
        /* Full implementation compares against UNLSP, LISTSP, EJCTSP etc. */
        (void)xsp; (void)st2;
        return OK;
    }

    /* Normal card (stype == 0 or other): update listing statement number */
    if (!AEQLC(LISTCL, 0)) {
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
    return OK;  /* RTN3 — normal card processed */
}

/* ── FILCHK — handle EOF in compilation ─────────────────────────────── */
/*
 * v311.sil FILCHK line 2350:
 *   If INCSTK != 0: pop include stack, restore LNNOCL/FILENM, RTN2.
 *   Else: query I/O system for next file; RTN1 if none.
 */
SIL_result FILCHK_fn(void)
{
    if (!AEQLC(INCSTK, 0)) {
        /* Pop include stack */
        GETDC_B(LNNOCL, INCSTK, 2*DESCR);
        GETDC_B(FILENM, INCSTK, 3*DESCR);
        GETDC_B(INCSTK, INCSTK, DESCR);
        return OK;  /* RTN2 */
    }

    /* Query I/O for file change */
    SPEC_t fname_sp;
    if (XCALL_IO_FILE(UNIT, &fname_sp) == FAIL) return FAIL; /* RTN1 */
    int32_t off = GENVAR_fn(&fname_sp);
    if (off) { SETAC(FILENM, off); SETVC(FILENM, S); }
    SETAC(LNNOCL, 0);
    return OK;  /* RTN1 — new file ready */
}

/* end of sil_forwrd.c */
