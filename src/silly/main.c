/*
 * main.c — Program initialization and main loop (v311.sil §2+§3+§21)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "types.h"
#include "data.h"
#include "arena.h"
#include "strings.h"
#include "symtab.h"
#include "forwrd.h"   /* NEWCRD_fn, CODSKP_fn */
#include "cmpile.h"   /* CMPILE_fn */
#include "interp.h"   /* INTERP_fn */
#include "errors.h"   /* END_fn, FTLEND_fn, SYSCUT_fn, COMP1_fn etc. */

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
    { /* SPCNVT/SPCNV1/SPCNV2: walk initialization specifier lists (v311.sil 969-977)
       * Outer loop: YCL counts down through INITLS sublists (step=DESCR).
       * Inner loop: XCL counts down through each sublist's (spec,dest) pairs (step=2*DESCR).
       * For each non-zero ZPTR entry: call GENVAR on the spec, write result back.
       * Oracle C: L_SPCNVT/L_SPCNV1/L_SPCNV2 with D_A(XCL)=D_V(D_A(XPTR)); > 0 guards. */
        DESCR_t ycl, xcl, xptr, zptr;
        D_A(ycl) = (int32_t)((DESCR_t *)A2P(D_A(INITLS)))->v; /* GETSIZ YCL,INITLS */
        while (D_A(ycl) > 0) {                                  /* ACOMPC YCL,0,SPCNVT */
            memcpy(&xptr, A2P(D_A(INITLS) + D_A(ycl)), sizeof(DESCR_t)); /* GETD XPTR,INITLS,YCL */
            D_A(xcl) = (int32_t)((DESCR_t *)A2P(D_A(xptr)))->v; /* GETSIZ XCL,XPTR */
            while (D_A(xcl) > 0) {                               /* SPCNV1/ACOMPC XCL,0 */
                memcpy(&zptr, A2P(D_A(xptr) + D_A(xcl)), sizeof(DESCR_t)); /* GETD ZPTR,XPTR,XCL */
                if (D_A(zptr) != 0) {                            /* AEQLC ZPTR,0,,SPCNV2 */
                    SPEC_t sp; memcpy(&sp, A2P(D_A(zptr)), sizeof(SPEC_t)); /* RCALL ZPTR,GENVAR,ZPTR */
                    int32_t off = GENVAR_fn(&sp);
                    SETAC(zptr, off);
                    memcpy(A2P(D_A(xptr) + D_A(xcl)), &zptr, sizeof(DESCR_t)); /* PUTD XPTR,XCL,ZPTR */
                }
                D_A(xcl) -= 2*DESCR;                             /* SPCNV2: DECRA XCL,2*DESCR */
            }
            D_A(ycl) -= DESCR;                                   /* DECRA YCL,DESCR */
        }
    }
    while (D_A(INITB) < D_A(INITE)) { /* INITB / INITE: convert remaining init block */
        DESCR_t xptr, yptr, zptr;
        GETDC_B(xptr, INITB, 0);
        int32_t off = genvar_from_descr(xptr);
        if (!off) { SETAC(ERRTYP, 20); FTLEND_fn(); } /* GENVAR fail → fatal, per oracle RTYPTR */
        SETAC(yptr, off); SETVC(yptr, S);
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
xlatrd:
    if (!AEQLC(LISTCL, 0)) /* XLATRD: optionally print previous line */
        STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
xlatrn:
    TEXTSP = NEXTSP; /* XLATRN: read a card */
    if (STREAD_fn(&TEXTSP, UNIT) == FAIL) {
        extern int sread_last_eof;
        if (sread_last_eof) { FILCHK_fn(); goto xlatrn; } /* IO_EOF → XLATIN */
        else { extern void COMP1_fn(void); COMP1_fn(); return; } /* IO_ERR → COMP1 */
    }
    D_A(TMVAL) = TEXTSP.l + STNOSZ;
    LNBFSP.l = D_A(TMVAL);
    INCRA(LNNOCL, 1);
xlatnx:
    /* XLATNX: classify card type */
    { int st2;
      RESULT_t crc = STREAM_fn(&xsp, &TEXTSP, &CARDTB, &st2);
      SETAC(STYPE, st2);
      if (crc == FAIL) {
          if (st2 == 0) { extern void COMP3_fn(void); COMP3_fn(); return; } /* ST_ERROR */
          goto xlatrd; /* ST_EOS: blank card → re-read */
      }
    }
    { RESULT_t nr = NEWCRD_fn(); if (nr == OK) goto xlatrd; } /* RTN1=reread; RTN2/RTN3(FAIL)=proceed to CMPILE */
    { /* CMPILE: RTN1=fatal(COMP3), RTN2=END reached(fall), RTN3=continue(XLATNX) */
        RESULT_t rc = CMPILE_fn();
        if (rc == FAIL) { /* RTN1: fatal compile error → COMP3 */
            extern void COMP3_fn(void); COMP3_fn(); return;
        }
        if (rc == NRETURN) goto xlatnx; /* RTN3: statement compiled, read more */
        /* RTN2: END statement — fall through to XLATP */
    }
    /* XLATP: insert END function, optionally print last line */
    INCRA(CMOFCL, DESCR);
    PUTD_B(CMBSCL, CMOFCL, ENDCL);
    if (!AEQLC(LISTCL, 0))
        STPRNT_fn(D_A(IOKEY), OUTBLK, &LNBFSP);
    if (AEQLC(STYPE, EOSTYP)) goto xlaend;
    /* Stream past any trailing blanks on END card */
    if (STREAM_fn(&xsp, &TEXTSP, &IBLKTB, &stype) == FAIL) goto xlaend; /* ST_EOS or error → done */
    SETAC(STYPE, stype);
    if (AEQLC(STYPE, EOSTYP)) goto xlaend;
    if (!AEQLC(STYPE, NBTYP)) { extern void COMP7_fn(void); COMP7_fn(); goto xlaend; }
    /* Parse optional END label */
    if (STREAM_fn(&xsp, &TEXTSP, &LBLTB, &stype) == FAIL) { extern void COMP7_fn(void); COMP7_fn(); goto xlaend; }
    SETAC(STYPE, stype);
    { int32_t xptr = GENVUP_fn(&xsp);
      if (!xptr) { extern void COMP7_fn(void); COMP7_fn(); goto xlaend; }
      SETAC(XPTR, xptr);
      DESCR_t attr; memcpy(&attr, A2P(xptr + ATTRIB), sizeof attr);
      if (!attr.a.i) { extern void COMP7_fn(void); COMP7_fn(); goto xlaend; }
      memcpy(&OCBSCL, &attr, sizeof attr); /* GETDC OCBSCL,XPTR,ATTRIB */
    }
    if (AEQLC(STYPE, EOSTYP)) goto xlaend;
    /* XLATP: second IBLKTB stream — check for trailing junk */
    if (STREAM_fn(&xsp, &TEXTSP, &IBLKTB, &stype) != FAIL)
        { extern void COMP7_fn(void); COMP7_fn(); } /* trailing junk = error */
xlaend:
    if (!AEQLC(ESAICL, 0)) { /* XLAEND: check compilation errors */
        XCALL_OUTPUT_fmt(PUNCH, "ERRORS DETECTED IN SOURCE PROGRAM\n\n");
        if (!AEQLC(NERRCL, 0)) { /* -NOERROR set: abort with RETCOD=1 → FTLEN2 */
            SETAC(RETCOD, 1);
            FTLEND_fn(); return;
        }
        /* NERRCL==0 (-NOERROR NOT set): continue to XLATND (execute anyway) */
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
    /* Oracle: INTERP case 1,2,3 → MAIN1 (error); 4/5/6 = RETURN/NRETURN/FRETURN → normal */
    switch ((int)irc) {
    case 1: case 2: case 3: MAIN1_fn(); break;
    default: break; /* 4=RETURN, 5=NRETURN, 6=FRETURN — normal top-level exit */
    }
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
    if (argc >= 2) {
        if (!freopen(argv[1], "r", stdin)) {
            fprintf(stderr, "silly-snobol4: cannot open %s\n", argv[1]);
            return 1;
        }
    }
    signal(SIGINT, sighandler); /* Install signal handlers */
    signal(SIGTERM, sighandler);
#ifdef SIGFPE
    signal(SIGFPE, sighandler);
#endif
    arena_init(); /* Initialise the arena */
    data_init(); /* Initialise all static data */
    BEGIN_fn(); /* Run the interpreter */
    compile_loop();
    return (int)D_A(RETCOD);
}
