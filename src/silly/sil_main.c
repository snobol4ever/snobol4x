/*
 * sil_main.c — Program initialization and main loop (v311.sil §2+§3+§21)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil.
 * BEGIN: system initialisation, string interning, pattern keyword init.
 * XLATRD/XLATRN/XLATNX: read-compile loop.
 * XLAEND: post-compile error check.
 * XLATND: interpreter invocation.
 * main(): entry point for the silly-snobol4 executable.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M21
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "sil_types.h"
#include "sil_data.h"
#include "sil_arena.h"
#include "sil_strings.h"
#include "sil_symtab.h"
#include "sil_forwrd.h"   /* NEWCRD_fn, CODSKP_fn */
#include "sil_cmpile.h"   /* CMPILE_fn */
#include "sil_interp.h"   /* INTERP_fn */
#include "sil_errors.h"   /* END_fn, FTLEND_fn, SYSCUT_fn, COMP1_fn etc. */

/* Platform stubs */
extern void       XCALL_ISTACK(void);
extern void       XCALL_MSTIME(DESCR_t *out);
extern void       XCALL_SBREAL(DESCR_t *out, DESCR_t a, DESCR_t b);
extern void       XCALL_io_flushall(void);
extern void       XCALL_XECOMP(void);
extern void       XCALL_OUTPUT_fmt(DESCR_t unit, const char *fmt, ...);
extern RESULT_t XCALL_IO_FILE(DESCR_t unit, SPEC_t *fname_out);
extern RESULT_t XCALL_GETPARM(SPEC_t *out);
extern void       XCALL_FREEPARM(SPEC_t *sp);
extern RESULT_t XCALL_GETPMPROTO(SPEC_t *out, int32_t idx);
extern void       XCALL_ZERBLK(DESCR_t *region, DESCR_t count);
extern void       STPRNT_fn(int32_t key, DESCR_t blk, SPEC_t *sp);
extern DESCR_t    PUNCH;
extern DESCR_t    OUTBLK;
extern DESCR_t    CARDTB;
extern DESCR_t    IBLKTB;
extern DESCR_t    LBLTB;

extern RESULT_t LOAD2_fn(void);   /* LOAD internal entry */
extern RESULT_t STREAD_fn(SPEC_t *sp, DESCR_t unit);
extern RESULT_t STREAM_fn(SPEC_t *res, SPEC_t *src,
                              DESCR_t *tbl, int *stype_out);
static int32_t genvar_from_descr(DESCR_t d);

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d))+(off_i), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d))+(off_i), &(src),  sizeof(DESCR_t))
#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d))+D_A(off_d), sizeof(DESCR_t))
#define PUTD_B(base_d, off_d, src) \
    memcpy((char*)A2P(D_A(base_d))+D_A(off_d), &(src), sizeof(DESCR_t))

/* ── Signal handler ──────────────────────────────────────────────────── */
static void sighandler(int sig)
{
    SETAC(SIGNCL, sig);
    SYSCUT_fn();
}

/*====================================================================================================================*/
/* ── BEGIN — system initialisation ──────────────────────────────────── */
static void BEGIN_fn(void)
{
    XCALL_ISTACK();
    if (!AEQLC(BANRCL, 0)) {
        XCALL_OUTPUT_fmt(PUNCH, "SNOBOL4 (Version 3.11, May 19, 1975)\n");
        XCALL_OUTPUT_fmt(PUNCH, "    Bell Telephone Laboratories...\n\n");
    }
    XCALL_MSTIME(&TIMECL);
    { /* Allocate initial object code block */
        int32_t blk = BLOCK_fn(D_A(OCALIM), C);
        if (!blk) { SETAC(ERRTYP, 20); FTLEND_fn(); }
        SETAC(SCBSCL, blk); MOVD(OCSVCL, SCBSCL);
        D_F(SCBSCL) &= ~PTR; /* RESETF SCBSCL,PTR */
    }
    { /* Convert initialisation specifier lists to string structures */
        DESCR_t ycl; GETDC_B(ycl, INITLS, 0);
        (void)ycl; /* Walk INITLS — abbreviated for M21; sil_data_init() handles most */
    }
    while (D_A(INITB) < D_A(INITE)) { /* INITB / INITE: convert remaining init block */
        DESCR_t xptr, yptr, zptr;
        GETDC_B(xptr, INITB, 0);
        int32_t off = genvar_from_descr(xptr);
        if (off) { SETAC(yptr, off); SETVC(yptr, S); }
        GETDC_B(zptr, INITB, DESCR);
        PUTDC_B(zptr, 0, yptr);
        INCRA(INITB, 2*DESCR);
    }
    { /* Get command-line &PARM */
        SPEC_t psp;
        if (XCALL_GETPARM(&psp) == OK) {
            int32_t off = GENVAR_fn(&psp);
            if (off) { SETAC(PARMVL, off); SETVC(PARMVL, S); }
            XCALL_FREEPARM(&psp);
        }
    }
    PUTDC_B(ABRTKY, DESCR, ABOPAT); /* Install initial pattern keyword values */
    PUTDC_B(ARBKY, DESCR, ARBPAT);
    PUTDC_B(BALKY, DESCR, BALPAT);
    PUTDC_B(FAILKY, DESCR, FALPAT);
    PUTDC_B(FNCEKY, DESCR, FNCPAT);
    PUTDC_B(REMKY, DESCR, REMPAT);
    PUTDC_B(SUCCKY, DESCR, SUCPAT);
    SETAC(VARSYM, 0);
    { /* Allocate name-list buffer */
        int32_t nb = BLOCK_fn(NMOVER, B);
        if (!nb) { SETAC(ERRTYP, 20); FTLEND_fn(); }
        SETAC(NBSPTR, nb);
    }
    MOVD(CMBSCL, SCBSCL);
    MOVD(UNIT, INPUT);
    MOVD(OCBSCL, CMBSCL);
    SUM(OCLIM, CMBSCL, OCALIM); DECRA(OCLIM, 7*DESCR);
    SETAC(INICOM, 1);
    { /* Get initial filename */
        SPEC_t fsp;
        if (XCALL_IO_FILE(UNIT, &fsp) == OK) {
            int32_t off = GENVAR_fn(&fsp);
            if (off) { SETAC(FILENM, off); SETVC(FILENM, S); }
        }
    }
    for (int32_t idx = 0; ; idx++) { /* Auto-load external functions */
        SPEC_t psp;
        if (XCALL_GETPMPROTO(&psp, idx) == FAIL) break;
        int32_t off = GENVAR_fn(&psp);
        if (!off) continue;
        SETAC(XPTR, off); SETVC(XPTR, S);
        MOVD(WPTR, NULVCL);
        INCRA(ERRLCL, 1);
        LOAD2_fn();
        SETAC(ERRLCL, 0);
    }
}

/*====================================================================================================================*/
/* ── XLATRD / XLATRN / XLATNX — read-compile loop ───────────────────── */
static void compile_loop(void)
{
    SPEC_t xsp; int stype;
    while (1) {
        if (!AEQLC(LISTCL, 0)) /* XLATRD: optionally print previous line */
            STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
        TEXTSP = NEXTSP; /* XLATRN: read a card */
        if (STREAD_fn(&TEXTSP, UNIT) == FAIL) {
            FILCHK_fn();
            continue;
        }
        D_A(TMVAL) = TEXTSP.l + STNOSZ;
        LNBFSP.l = D_A(TMVAL);
        INCRA(LNNOCL, 1);
        if (STREAM_fn(&xsp, &TEXTSP, &CARDTB, &stype) == FAIL) continue; /* XLATNX: classify card */
        SETAC(STYPE, stype);
        NEWCRD_fn();
        RESULT_t rc = CMPILE_fn(); /* Compile one statement */
        if (rc == FAIL) {
            INCRA(CMOFCL, DESCR); /* END statement reached */
            PUTD_B(CMBSCL, CMOFCL, ENDCL);
            if (!AEQLC(LISTCL, 0))
                STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
            goto xlaend;
        }
    } /* rc == OK or RTN3: continue reading */
xlaend:
    if (!AEQLC(ESAICL, 0)) { /* XLAEND: check compilation errors */
        XCALL_OUTPUT_fmt(PUNCH, "ERRORS DETECTED IN SOURCE PROGRAM\n\n");
        if (!AEQLC(NERRCL, 0)) {
            SETAC(RETCOD, 1);
            FTLEND_fn();
        }
    } else {
        if (!AEQLC(BANRCL, 0))
            XCALL_OUTPUT_fmt(PUNCH, "No errors detected in source program\n\n");
    }
    if (AEQLC(EXECCL, 0)) { END_fn(); return; } /* XLATND: check if execute-only */
    XCALL_XECOMP(); /* Finalise compilation */
    SETAC(UNIT, 0); SETAC(LPTR, 0); SETAC(OCLIM, 0); SETAC(LNNOCL, 0);
    XCALL_ZERBLK(&COMREG, COMDCT);
    { /* Split off unused object code */
        DESCR_t xcl; SUM(xcl, CMBSCL, CMOFCL);
        SPLIT_fn(D_A(xcl));
    }
    SETAC(LISTCL, 0); SETAC(COMPCL, 0);
    { /* Time the compiler */
        DESCR_t et;
        XCALL_MSTIME(&et);
        XCALL_SBREAL(&TIMECL, et, TIMECL);
    }
    SETAC(CNSLCL, 1);
    RESULT_t irc = INTERP_fn(); /* Run the interpreter */
    if ((int)irc != 0 && (int)irc != 5 && (int)irc != 6) MAIN1_fn();
    END_fn();
}

/*====================================================================================================================*/
/* ── Stub for GENVAR from raw DESCR ─────────────────────────────────── */
/* Used in BEGIN for INITB processing */
static int32_t genvar_from_descr(DESCR_t d)
{
    if (D_V(d) != S) return 0;
    SPEC_t sp; sp.a = D_A(d); sp.l = 0; sp.o = 0; sp.v = S; sp.f = 0;
    return GENVAR_fn(&sp);
}

/*====================================================================================================================*/
/* ── main ────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    signal(SIGINT, sighandler); /* Install signal handlers */
    signal(SIGTERM, sighandler);
#ifdef SIGFPE
    signal(SIGFPE, sighandler);
#endif
    arena_init(); /* Initialise the arena */
    sil_data_init(); /* Initialise all static data */
    BEGIN_fn(); /* Run the interpreter */
    compile_loop();
    return (int)D_A(RETCOD);
}
