/*
 * interp.c — Interpreter executive (v311.sil §7 lines 2520–2678)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §7.
 *
 * BRANIC (v311.sil indirect branch macro):
 *   SIL: BRANIC INCL,0 — branch through function descriptor in INCL.
 *   C:   look up INCL.a in invoke_table[], call the function pointer.
 *
 * INVOKE exit codes (v311.sil RRTURN exits):
 *   1 = normal success (continue)
 *   2 = success, push result
 *   3 = return value (RTZPTR / RTYPTR etc.)
 *   4 = FAIL
 *   5 = NRETURN (return by name)
 *   6 = RETURN  (return by value, GOTL path)
 *
 * INTERP loop uses exits 1–6 of INVOKE:
 *   1,2,3 = continue (result in XPTR / implicit)
 *   4     = failure → jump to FRTNCL
 *   5,6   = nested return (handled by DEFFNC_fn)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M19
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "interp.h"
#include "argval.h"
#include "trace.h"
#include "symtab.h"   /* locapt_fn */
#include "errors.h"   /* INTR4_fn, INTR5_fn, ARGNER_fn, etc. */

static inline int deql(DESCR_t a, DESCR_t b)
    { return D_A(a)==D_A(b) && D_V(a)==D_V(b); }

#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d))+D_A(off_d), sizeof(DESCR_t))
#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d))+(off_i),    sizeof(DESCR_t))

/* ── Invoke dispatch table ───────────────────────────────────────────── */
#define INVOKE_TABLE_SZ  512

typedef struct {
    invoke_fn_t fn;
    int32_t     nargs;
} Invoke_entry;

static Invoke_entry invoke_table[INVOKE_TABLE_SZ];

void invoke_table_register(int32_t idx, invoke_fn_t fn, int32_t nargs)
{
    if ((uint32_t)idx < INVOKE_TABLE_SZ) {
        invoke_table[idx].fn = fn;
        invoke_table[idx].nargs = nargs;
    }
}

/*====================================================================================================================*/
/* ── Aliases for error targets used in §7 ───────────────────────────── */
#define intr4()   INTR4_fn()
#define intr5()   INTR5_fn()
#define cnterr()  CNTERR_fn()
#define cfterr()  CFTERR_fn()
#define exex()    EXEX_fn()

/* ── BASE ────────────────────────────────────────────────────────────── */
/*
 * v311.sil BASE line 2522:
 *   SUM OCBSCL,OCBSCL,OCICL   — advance base by offset
 *   SETAC OCICL,0              — zero offset
 */
RESULT_t BASE_fn(void)
{
    SUM(OCBSCL, OCBSCL, OCICL);
    SETAC(OCICL, 0);
    return OK; /* RTNUL3 */
}

/*====================================================================================================================*/
/* ── GOTG — direct goto :<X> ─────────────────────────────────────────── */
/*
 * v311.sil GOTG line 2531:
 *   RCALL OCBSCL,ARGVAL,,INTR5  — evaluate goto arg into OCBSCL
 *   VEQLC OCBSCL,C,INTR4        — must be CODE type
 *   SETAC OCICL,0               — zero offset
 */
RESULT_t GOTG_fn(void)
{
    if (ARGVAL_fn() == FAIL) { intr5(); return FAIL; }
    if (!VEQLC(XPTR, C)) { intr4(); return FAIL; }
    MOVD(OCBSCL, XPTR);
    SETAC(OCICL, 0);
    return OK; /* RTNUL3 */
}

/*====================================================================================================================*/
/* ── GOTL — label goto :(X) ──────────────────────────────────────────── */
/*
 * v311.sil GOTL line 2541:
 *   Reads label variable from object code. Handles special labels:
 *   RETCL(6=RETURN) FRETCL(4=FAIL) NRETCL(5=NRETURN) ABORCL SCNTCL CONTCL.
 *   Normal label: GETDC OCBSCL,XPTR,ATTRIB; SETAC OCICL,0.
 */
RESULT_t GOTL_fn(void)
{
    INCRA(OCICL, DESCR);
    GETD_B(XPTR, OCBSCL, OCICL);
    if (TESTF(XPTR, FNC)) { /* GOTLC: computed label — evaluate */
        RESULT_t rc = INVOKE_fn();
        if (rc == FAIL)   { intr5(); return FAIL; } /* case1: FAIL → INTR5 */
        if (rc == NEMO)   { intr4(); return FAIL; } /* case3: name → INTR4 */
        /* case2: success — fall through, check type */
        if (!VEQLC(XPTR, S)) { intr4(); return FAIL; }
    }
    if (D_A(TRAPCL) > 0) { /* Label trace */
        int32_t assoc = locapt_fn(D_A(TLABL), &XPTR);
        if (assoc) {
            DESCR_t save_x = XPTR;
            SETAC(ATPTR, assoc);
            TRPHND_fn(ATPTR);
            MOVD(XPTR, save_x);
        }
    }
    if (deql(XPTR, RETCL)) return VRETURN; /* RETURN  */                                          /* Special label dispatch */
    if (deql(XPTR, FRETCL)) return FAIL; /* FRETURN */
    if (deql(XPTR, NRETCL)) return NRETURN; /* NRETURN */
    if (deql(XPTR, ABORCL)) {
        if (AEQLC(XOCBSC, 0)) { cnterr(); return FAIL; }
        MOVD(ERRTYP, XERRTY); MOVD(FILENM, XFILEN);
        MOVD(LNNOCL, XLNNOC); MOVD(STNOCL, XSTNOC);
        SETAC(ERRTYP, 255); return FAIL; /* FTLEND path — fatal termination */
    }
    if (deql(XPTR, SCNTCL)) {
        if (!AEQLC(FATLCL, 0)) { cfterr(); return FAIL; }  /* IP-3: FATLCL!=0 → CFTERR */
        MOVD(FRTNCL, XOCICL);
        goto restore_and_go;
    }
    if (deql(XPTR, CONTCL)) {
        if (!AEQLC(FATLCL, 0)) { cfterr(); return FAIL; }  /* IP-3: FATLCL!=0 → CFTERR */
        MOVD(FRTNCL, XFRTNC);
restore_and_go:
        if (AEQLC(XOCBSC, 0)) { cnterr(); return FAIL; }
        MOVD(OCBSCL, XOCBSC);
        MOVD(FILENM, XFILEN); MOVD(LNNOCL, XLNNOC);
        MOVD(STNOCL, XSTNOC); MOVD(LSFLNM, XLSFLN);
        MOVD(LSLNCL, XLSLNC); MOVD(LSTNCL, XLNNOC);
        SETAC(XOCBSC, 0);
        if (AEQLC(ERRTYP, 0)) return OK;   /* IP-4: ERRTYP==0 → END0 (clean exit) */
        MOVD(ERRTYP, XERRTY);
        return FAIL;
    }
    GETDC_B(OCBSCL, XPTR, ATTRIB); /* Normal label: get code base from ATTRIB field */
    if (AEQLC(OCBSCL, 0)) { intr4(); return FAIL; }
    SETAC(OCICL, 0);
    return OK; /* RTNUL3 */
}

/*====================================================================================================================*/
/* ── GOTO — internal goto ────────────────────────────────────────────── */
/*
 * v311.sil GOTO line 2603:
 *   INCRA OCICL,DESCR
 *   GETD OCICL,OCBSCL,OCICL   — load new OCICL from object code
 */
RESULT_t GOTO_fn(void)
{
    INCRA(OCICL, DESCR);
    GETD_B(OCICL, OCBSCL, OCICL);
    return OK; /* RTNUL3 */
}

/*====================================================================================================================*/
/* ── INIT — statement initialisation ────────────────────────────────── */
/*
 * v311.sil INIT line 2612:
 *   Updates &LASTNO/&LASTFILE/&LASTLINE.
 *   Loads &STNO/FRTNCL/&LINE/&FILE from object code.
 *   Increments &STEXEC; checks &STLIMIT; checks &TRACE.
 */
RESULT_t INIT_fn(void)
{
    MOVD(LSTNCL, STNOCL);
    MOVA(LSFLNM, FILENM);
    MOVA(LSLNCL, LNNOCL);
    if (!AEQLC(UINTCL, 0)) { USRINT_fn(); return FAIL; } /* Check user interrupt */
    INCRA(OCICL, DESCR); GETD_B(XCL, OCBSCL, OCICL); /* Load statement number, failure offset, line, file from object code */
    MOVA(STNOCL, XCL);
    D_A(FRTNCL) = D_V(XCL); /* oracle: D_A(FRTNCL)=D_V(XCL) — V field holds failure offset */
    INCRA(OCICL, DESCR); GETD_B(LNNOCL, OCBSCL, OCICL);
    INCRA(OCICL, DESCR); GETD_B(FILENM, OCBSCL, OCICL);
    INCRA(EXN2CL, 1); /* &STEXEC */
    if (D_A(EXLMCL) < 0) goto init_done; /* &STLIMIT < 0 means unlimited */
    if (D_A(EXNOCL) >= D_A(EXLMCL)) { exex(); return FAIL; }
    INCRA(EXNOCL, 1); /* &STCOUNT */
init_done:
    if (D_A(TRAPCL) > 0) { /* &TRACE checks */
        int32_t assoc = locapt_fn(D_A(TKEYL), &STNOKY); /* Check for breakpoint  XCALLC chk_break — stub */
        if (assoc) { SETAC(ATPTR, assoc); TRPHND_fn(ATPTR); }
        assoc = locapt_fn(D_A(TKEYL), &STCTKY);
        if (assoc) { SETAC(ATPTR, assoc); TRPHND_fn(ATPTR); }
    }
    return OK; /* RTNUL3 */
}

/*====================================================================================================================*/
/* ── INTERP — interpreter main loop ─────────────────────────────────── */
/*
 * v311.sil INTERP line 2636:
 *   Loop: advance OCICL, load descriptor.
 *   If not FNC: push onto operand stack (literal value), continue.
 *   If FNC: call INVOKE.
 *   INVOKE exits: 1-3=continue, 4=failure→FRTNCL, 5-6=nested return.
 */
RESULT_t INTERP_fn(void)
{
    while (1) {
        INCRA(OCICL, DESCR);
        GETD_B(XPTR, OCBSCL, OCICL);
        if (!TESTF(XPTR, FNC)) {
            continue; /* Literal value — push onto operand stack and continue  (The operand stack is managed implicitly through INCL/ARGVAL) */
        }
        RESULT_t rc = INVOKE_fn(); /* Call via INVOKE */
        switch ((int)rc) {
        case OK: /* exits 1,2,3 — continue */
            continue;
        case FAIL: /* exit 4 — failure */
            MOVD(OCICL, FRTNCL);
            INCRA(FALCL, 1); /* &STFCOUNT */
            if (D_A(TRAPCL) > 0) { /* &TRACE check on failure */
                int32_t assoc = locapt_fn(D_A(TKEYL), &FALKY);
                if (assoc) { SETAC(ATPTR, assoc); TRPHND_fn(ATPTR); }
            }
            continue;
        case 5: /* NRETURN — caller handles */
            return NRETURN;
        case 6: /* RETURN — caller handles */
            return VRETURN;
        default:
            return rc;
        }
    }
}

/*====================================================================================================================*/
/* ── INVOKE — procedure invocation dispatch ──────────────────────────── */
/*
 * v311.sil INVOKE line 2652:
 *   POP INCL           — get function descriptor from stack
 *   GETDC XPTR,INCL,0  — get procedure descriptor
 *   VEQL INCL,XPTR     — check argument counts match
 *   BRANIC INCL,0      — indirect branch through function table
 *
 * In C: the function pointer is stored in invoke_table[D_A(INCL)].
 * Argument count is in D_V(INCL); checked against D_V(XPTR).
 */
RESULT_t INVOKE_fn(void)
{
    GETDC_B(XPTR, INCL, 0); /* procedure descriptor */     /* INCL already loaded by caller (from object code stream) */
    if (D_V(INCL) != D_V(XPTR)) { /* VEQL INCL,XPTR — check arg counts (V fields) */
        if (TESTF(XPTR, FNC)) { /* INVK2: TESTF XPTR,FNC,ARGNER,INVK1 */
        } else { /* variable argument function — pass as-is */
            SETAC(ERRTYP, 25); return FAIL; /* ARGNER: incorrect number of arguments (v311.sil line 10344) */
        }
    }
    int32_t idx = D_A(INCL); /* INVK1: BRANIC INCL,0 — indirect call */
    if ((uint32_t)idx >= INVOKE_TABLE_SZ || !invoke_table[idx].fn) {
        SETAC(ERRTYP, 13); return FAIL; /* undefined function */
    }
    return invoke_table[idx].fn();
}

/*====================================================================================================================*/
/* end of interp.c */
