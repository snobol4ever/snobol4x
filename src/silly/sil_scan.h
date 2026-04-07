/*
 * sil_scan.h — Pattern Matching Procedures (v311.sil §11)
 *
 * Faithful C translation of v311.sil §11 lines 3323–4239.
 * SCAN, SJSR, SCNR — top-level entry points for pattern matching
 * and 27 XPROC sub-procedures dispatched via PATBRA / SELBRA.
 *
 * The SIL scanner dispatches via a 35-entry branch table (PATBRA SELBRA).
 * In C each XPROC becomes a static function in sil_scan.c; the dispatch
 * table is a function-pointer array indexed by PTBRCL (the pattern node's
 * function-code descriptor index, zero-based).
 *
 * Control flow rules (from §INFO):
 *   - Zero gotos. Zero computed BRANCHes.
 *   - SIL RCALL → C function call with typed params and return.
 *   - Pattern backtracking: C call stack + setjmp/longjmp here only.
 *   - SIL BRANCH SALT / SALF → longjmp into scan_ctx jmp_buf.
 *   - Each XPROC sub-procedure corresponds to one scan_fn_t entry.
 *
 * PATBRA dispatch index → function:
 *   0=ANYC  1=ARBF  2=ARBN  3=ATP   4=CHR   5=BAL   6=BALF  7=BRKC
 *   8=BRKX  9=BRKXF 10=DNME 11=DNME1 12=EARB 13=DSAR 14=ENME 15=ENMI
 *  16=FARB  17=FNME  18=LNTH 19=NME  20=NNYC 21=ONAR 22=ONRF 23=POSI
 *  24=RPSI  25=RTB   26=FAIL 27=SALF 28=SCOK 29=SCON 30=SPNC 31=STAR
 *  32=TB    33=RTNUL3 34=FNCE 35=SUCF
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M8
 */

#ifndef SIL_SCAN_H
#define SIL_SCAN_H

#include "sil_types.h"

/* ── Top-level procedures ────────────────────────────────────────────── */

/* SCAN  — Pattern matching (subject = pattern, no replacement)          */
/* v311.sil SCAN line 3327                                               */
RESULT_t SCAN_fn(void);

/* SJSR  — Pattern matching with replacement                             */
/* v311.sil SJSR line 3376                                               */
RESULT_t SJSR_fn(void);

/* SCNR  — Basic scanning procedure (inner engine, called by SCAN/SJSR) */
/* v311.sil SCNR line 3441                                               */
RESULT_t SCNR_fn(void);

/* ── Scan context — scanner state shared across sub-procedures ────────
 *
 * SIL's scanner communicates between SCNR/SCIN and the 27 XPROCs via
 * named globals (PATBCL, PATICL, TXSP, XSP, HEADSP, LENFCL, PDLPTR …).
 * In C we keep those same globals (declared in sil_data.h) and use a
 * Scan_ctx for the setjmp recovery points only.
 *
 * Two longjmp targets matching the two SIL failure paths:
 *   SALT  — length failure    (BRANCH SALT,SCNR)
 *   SALF  — non-length failure (BRANCH SALF,SCNR)
 *
 * The scan context is allocated on the C stack inside SCNR_fn and a
 * pointer is stored in scan_ctx_g so sub-procedures can reach it.
 */

#include <setjmp.h>

typedef struct {
    jmp_buf salt_jmp;   /* SALT — length failure recovery  */
    jmp_buf salf_jmp;   /* SALF — non-length failure       */
    jmp_buf scok_jmp;   /* SCOK — successful match         */
    jmp_buf fail_jmp;   /* FAIL — global scan failure      */
} Scan_ctx;

extern Scan_ctx *scan_ctx_g;   /* pointer to current innermost Scan_ctx */

/* ── PATBRA dispatch table ───────────────────────────────────────────── */

/* Number of entries in the SELBRA table (v311.sil PATBRA line 3581)    */
#define SCAN_DISPATCH_SZ  36

typedef void (*scan_fn_t)(void);

/* Populated in sil_scan.c; index = pattern node function-code value    */
extern scan_fn_t scan_dispatch[SCAN_DISPATCH_SZ];

/* Dispatch indices matching SELBRA list order in v311.sil              */
#define SCAN_IDX_ANYC    0
#define SCAN_IDX_ARBF    1
#define SCAN_IDX_ARBN    2
#define SCAN_IDX_ATP     3
#define SCAN_IDX_CHR     4
#define SCAN_IDX_BAL     5
#define SCAN_IDX_BALF    6
#define SCAN_IDX_BRKC    7
#define SCAN_IDX_BRKX    8
#define SCAN_IDX_BRKXF   9
#define SCAN_IDX_DNME   10
#define SCAN_IDX_DNME1  11
#define SCAN_IDX_EARB   12
#define SCAN_IDX_DSAR   13
#define SCAN_IDX_ENME   14
#define SCAN_IDX_ENMI   15
#define SCAN_IDX_FARB   16
#define SCAN_IDX_FNME   17
#define SCAN_IDX_LNTH   18
#define SCAN_IDX_NME    19
#define SCAN_IDX_NNYC   20
#define SCAN_IDX_ONAR   21
#define SCAN_IDX_ONRF   22
#define SCAN_IDX_POSI   23
#define SCAN_IDX_RPSI   24
#define SCAN_IDX_RTB    25
#define SCAN_IDX_FAIL   26
#define SCAN_IDX_SALF   27
#define SCAN_IDX_SCOK   28
#define SCAN_IDX_SCON   29
#define SCAN_IDX_SPNC   30
#define SCAN_IDX_STAR   31
#define SCAN_IDX_TB     32
#define SCAN_IDX_RTNUL3 33
#define SCAN_IDX_FNCE   34
#define SCAN_IDX_SUCF   35

/* ── Error codes set into ERRTYP before calling SCERST ──────────────── */
#define SCAN_ERR_ILLEGAL_TYPE   1   /* illegal data type                */
#define SCAN_ERR_NULL_STRING    4   /* null argument to pattern fn      */
#define SCAN_ERR_UNDEF_EXPR     8   /* undef expression result (E3.4.4) */
#define SCAN_ERR_NEGATIVE      14   /* negative number argument         */
#define SCAN_ERR_STR_OVERFLOW  15   /* string overflow (&MAXLNGTH)      */

#endif /* SIL_SCAN_H */
