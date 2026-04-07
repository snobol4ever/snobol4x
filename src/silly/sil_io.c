/*
 * sil_io.c — I/O functions (v311.sil §15 lines 5268–5465)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §15.
 * Platform I/O calls (STREAD, STPRNT, IO_OPENI, IO_OPENO, IO_SEEK,
 * BKSPCE/ENFILE/REWIND macros) are wrapped as extern stubs resolved
 * by the platform layer (sil_platform.c, not yet written).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M15
 */

#include <string.h>
#include <stdio.h>

#include "sil_types.h"
#include "sil_data.h"
#include "sil_io.h"
#include "sil_argval.h"
#include "sil_arena.h"
#include "sil_strings.h"
#include "sil_symtab.h"
#include "sil_asgn.h"   /* IND_fn */

/* Platform I/O stubs — resolved by sil_platform.c */
extern RESULT_t XCALL_IO_OPENI(DESCR_t unit, SPEC_t *fname,
                                  SPEC_t *opts, DESCR_t *recl_out);
extern RESULT_t XCALL_IO_OPENO(DESCR_t unit, SPEC_t *fname, SPEC_t *fmt);
extern RESULT_t XCALL_IO_SEEK(DESCR_t unit, DESCR_t off, DESCR_t whence);
extern RESULT_t STREAD_fn(SPEC_t *sp, DESCR_t unit);   /* reads into sp */
extern void       STPRNT_fn(int32_t key, DESCR_t blk, SPEC_t *sp);
extern void       XCALL_BKSPCE(DESCR_t unit);
extern void       XCALL_ENFILE(DESCR_t unit);
extern void       XCALL_REWIND(DESCR_t unit);
/* AUGATL_fn declared in sil_symtab.h as int32_t(int32_t, DESCR_t, DESCR_t) */
extern RESULT_t DTREP_fn2(DESCR_t *out, DESCR_t obj);

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),  sizeof(DESCR_t))

static DESCR_t io_stk[16];
static int io_top = 0;
static inline void    io_push(DESCR_t d) { io_stk[io_top++] = d; }
static inline DESCR_t io_pop(void)        { return io_stk[--io_top]; }

/* ── READ — INPUT(V,U,O,N) ───────────────────────────────────────────── */
RESULT_t READ_fn(void)
{
    if (IND_fn() == FAIL) return FAIL; /* IND — get variable */
    io_push(XPTR);
    if (INTVAL_fn() == FAIL) { io_top--; return FAIL; } /* unit */
    io_push(XPTR);
    if (VARVAL_fn() == FAIL) { io_top -= 2; return FAIL; } /* options */
    MOVD(YPTR, io_pop()); /* unit */
    XPTR = io_pop(); /* variable */
    if (ACOMPC(YPTR, 0) == 0) SETAC(YPTR, UNITI); /* default unit? */
    io_push(YPTR); io_push(ZPTR); /* unit, options */
    if (VARVAL_fn() == FAIL) { io_top -= 2; return FAIL; } /* optional filename */
    MOVD(TPTR, XPTR);
    MOVD(ZPTR, io_pop()); MOVD(YPTR, io_pop()); /* opts, unit */
    LOCSP_fn(&XSP, &TPTR);
    LOCSP_fn(&ZSP, &ZPTR);
    MOVD(ZPTR, ZEROCL);
    if (XCALL_IO_OPENI(YPTR, &XSP, &ZSP, &ZPTR) == FAIL) return FAIL; /* IO_OPENI — tell I/O about filename; fills ZPTR with recl */
    if (ACOMPC(ZPTR, 0) == 0) {
        int32_t assoc = locapt_fn(D_A(INSATL), &YPTR); /* defaulted length — check INSATL for stored default */
        if (assoc) { SETAC(ZPTR, assoc); }
        else MOVD(ZPTR, DFLSIZ);
    }
    int32_t ioblk = BLOCK_fn(D_A(IOBLSZ), B); /* Allocate I/O block */
    if (!ioblk) return FAIL;
    SETAC(TPTR, ioblk);
    PUTDC_B(TPTR, DESCR, YPTR); /* unit */
    PUTDC_B(TPTR, 2*DESCR, ZPTR); /* format/recl */
    int32_t assoc = locapv_fn(D_A(INATL), &XPTR); /* Link into input attribute list */
    if (assoc) {
        SETAC(ZPTR, assoc);
        PUTDC_B(ZPTR, DESCR, TPTR);
    } else {
        AUGATL_fn(D_A(INATL), TPTR, XPTR);
    }
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── PRINT — OUTPUT(V,U,O,N) ─────────────────────────────────────────── */
RESULT_t PRINT_fn(void)
{
    if (IND_fn() == FAIL) return FAIL;
    io_push(XPTR);
    if (INTVAL_fn() == FAIL) { io_top--; return FAIL; }
    io_push(XPTR);
    if (VARVAL_fn() == FAIL) { io_top -= 2; return FAIL; }
    io_push(XPTR);
    if (VARVAL_fn() == FAIL) { io_top -= 3; return FAIL; }
    MOVD(TPTR, XPTR);
    MOVD(ZPTR, io_pop()); MOVD(YPTR, io_pop()); XPTR = io_pop();
    LOCSP_fn(&XSP, &TPTR);
    LOCSP_fn(&ZSP, &ZPTR);
    if (XCALL_IO_OPENO(YPTR, &XSP, &ZSP) == FAIL) return FAIL;
    if (ACOMPC(YPTR, 0) == 0) SETAC(YPTR, UNITO);
    if (AEQLC(ZPTR, 0)) {
        int32_t assoc = locapt_fn(D_A(OTSATL), &YPTR);
        if (assoc) { SETAC(ZPTR, assoc); }
        else MOVD(ZPTR, DFLFST);
    }
    int32_t ioblk = BLOCK_fn(D_A(IOBLSZ), B);
    if (!ioblk) return FAIL;
    SETAC(TPTR, ioblk);
    PUTDC_B(TPTR, DESCR, YPTR);
    PUTDC_B(TPTR, 2*DESCR, ZPTR);
    int32_t assoc = locapv_fn(D_A(OUTATL), &XPTR);
    if (assoc) {
        SETAC(ZPTR, assoc);
        PUTDC_B(ZPTR, DESCR, TPTR);
    } else {
        AUGATL_fn(D_A(OUTATL), TPTR, XPTR);
    }
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── BKSPCE / ENDFL / REWIND / SET ──────────────────────────────────── */
static RESULT_t ioop(int32_t op)
{
    io_push(SCL);
    if (INTVAL_fn() == FAIL) { io_top--; return FAIL; }
    MOVD(XCL, XPTR);
    SCL = io_pop();
    if (ACOMPC(XCL, 0) <= 0) return FAIL; /* UNTERR */
    switch (op) {
    case 1: XCALL_BKSPCE(XCL); break;
    case 2: XCALL_ENFILE(XCL); break;
    case 3: XCALL_REWIND(XCL); break;
    case 4:
        io_push(XCL);
        if (INTVAL_fn() == FAIL) { io_top--; return FAIL; }
        io_push(XPTR);
        if (INTVAL_fn() == FAIL) { io_top -= 2; return FAIL; }
        MOVD(YPTR, XPTR);
        XPTR = io_pop(); XCL = io_pop();
        return XCALL_IO_SEEK(XCL, XPTR, YPTR);
    }
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
RESULT_t BKSPCE_fn(void) { return ioop(1); }
RESULT_t ENDFL_fn(void)  { return ioop(2); }
RESULT_t REWIND_fn(void) { return ioop(3); }
RESULT_t SET_fn(void)    { return ioop(4); }

/* ── DETACH(V) ───────────────────────────────────────────────────────── */
RESULT_t DETACH_fn(void)
{
    if (IND_fn() == FAIL) return FAIL;
    int32_t ai = locapv_fn(D_A(INATL), &XPTR); /* Clear input association */
    if (ai) {
        DESCR_t z; MOVD(z, ZEROCL);
        memcpy((char*)A2P(D_A(INATL)) + ai + DESCR, &z, sizeof(DESCR_t));
        memcpy((char*)A2P(D_A(INATL)) + ai + 2*DESCR, &z, sizeof(DESCR_t));
    }
    int32_t ao = locapv_fn(D_A(OUTATL), &XPTR); /* Clear output association */
    if (ao) {
        DESCR_t z; MOVD(z, ZEROCL);
        memcpy((char*)A2P(D_A(OUTATL)) + ao + DESCR, &z, sizeof(DESCR_t));
        memcpy((char*)A2P(D_A(OUTATL)) + ao + 2*DESCR, &z, sizeof(DESCR_t));
    }
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── PUTIN — internal input procedure ───────────────────────────────── */
RESULT_t PUTIN_fn(DESCR_t blk, DESCR_t var)
{
    MOVD(IO1PTR, blk); MOVD(IO2PTR, var);
    GETDC_B(IO3PTR, IO1PTR, DESCR); /* unit */
    GETDC_B(IO1PTR, IO1PTR, 2*DESCR); /* recl */
    if (!AEQLC(IO1PTR, VLRECL)) { /* Pre-allocate if fixed record length */
        int32_t soff = CONVAR_fn(D_A(IO1PTR));
        if (!soff) return FAIL;
        SETAC(IO4PTR, soff); SETVC(IO4PTR, S);
        LOCSP_fn(&IOSP, &IO4PTR);
    }
    INCRA(RSTAT, 1);
    if (STREAD_fn(&IOSP, IO3PTR) == FAIL) return FAIL; /* STREAD — platform read into IOSP */
    if (!AEQLC(TRIMCL, 0)) TRIMSP_fn(&IOSP, &IOSP); /* TRIM if &TRIM set */
    D_A(IO1PTR) = IOSP.l; /* Check &MAXLNGTH */
    if (ACOMP(IO1PTR, MLENCL) > 0) return FAIL;
    if (VEQLC(IO2PTR, K)) { /* Keyword variable? */
        if (SPCINT_fn(&IO1PTR, &IOSP) == FAIL) return FAIL;
        goto putin2;
    }
    if (!AEQLC(XCL, VLRECL)) { /* Intern string */
        int32_t off = GNVARS_fn((const char*)A2P(IOSP.a) + IOSP.o, IOSP.l); /* fixed recl: intern in-place */
        if (!off) return FAIL;
        SETAC(IO1PTR, off); SETVC(IO1PTR, S);
    } else {
        int32_t off = GENVAR_fn(&IOSP);
        if (!off) return FAIL;
        SETAC(IO1PTR, off); SETVC(IO1PTR, S);
    }
putin2:
    PUTDC_B(IO2PTR, DESCR, IO1PTR);
    MOVD(XPTR, IO1PTR); return OK;
}

/*====================================================================================================================*/
/* ── PUTOUT — internal output procedure ─────────────────────────────── */
void PUTOUT_fn(DESCR_t blk, DESCR_t val)
{
    MOVD(IO1PTR, blk); MOVD(IO2PTR, val);
    switch (D_V(IO2PTR)) {
    case S:
        LOCSP_fn(&IOSP, &IO2PTR);
        break;
    case I:
        INTSPC_fn(&IOSP, &IO2PTR);
        break;
    default:
        if (DTREP_fn2(&IO2PTR, IO2PTR) == FAIL) return; /* DTREP: get data-type representation string */
        memcpy(&IOSP, A2P(D_A(IO2PTR)), sizeof(SPEC_t)); /* GETSPC IOSP,IO2PTR,0 */
        break;
    }
    STPRNT_fn(D_A(IOKEY), IO1PTR, &IOSP);
    if (!AEQLC(IOKEY, 0)) return; /* COMP6 error */
    INCRA(WSTAT, 1);
}
