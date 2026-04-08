/*
 * argval.h — argument evaluation procedures (v311.sil §8)
 *
 * Faithful C translation of v311.sil §8 lines 2679–2922:
 *   ARGVAL  — evaluate one argument from object code stream → XPTR
 *   EXPVAL  — evaluate unevaluated expression (saves/restores interp state)
 *   EXPEVL  — EXPVAL entry with SCL=0 (expression value context)
 *   INTVAL  — evaluate argument, coerce to INTEGER → XPTR
 *   PATVAL  — evaluate argument, coerce to PATTERN → XPTR
 *   VARVAL  — evaluate argument, coerce to STRING → XPTR
 *   VARVUP  — VARVAL with case-folding (honours &CASE)
 *   VPXPTR  — case-fold XPTR to upper-case string
 *   XYARGS  — evaluate argument pair → XPTR (first), YPTR (second)
 *
 * All advance OCICL by DESCR before reading the next descriptor from
 * the object code stream at OCBSCL+OCICL.
 *
 * Dependencies (forward-declared extern — resolved by later milestones):
 *   INVOKE_fn  (§7)   — dispatch function call
 *   PUTIN_fn   (§15)  — perform input association
 *   CONVE_fn   (§19)  — convert to EXPRESSION type
 *   CONVAR_fn  (§5)   — allocate scratch space (arena.h)
 *   GNVARI_fn  (§5)   — intern integer as string variable
 *   GNVARS_fn  (§5)   — intern raw C string
 *   GENVAR_fn  (§5)   — intern specifier as string variable
 *   BLOCK_fn   (§5)   — allocate block
 *
 * Return convention: RESULT_t (FAIL=0, OK=1).
 * Result descriptor lives in XPTR (or ZPTR for EXPVAL exit 3).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M6
 */

#ifndef SIL_ARGVAL_H
#define SIL_ARGVAL_H

#include "types.h"

/* ── ARGVAL — evaluate one argument ─────────────────────────────────── */
/*
 * v311.sil ARGVAL (line 2679):
 *   Advance OCICL; fetch descriptor from object code stream.
 *   If FNC: call INVOKE, handle 3 exits.
 *   If &INPUT active: check input association list (INATL).
 *   Dereference name → value in XPTR.
 *   Returns OK (result in XPTR) or FAIL.
 */
RESULT_t ARGVAL_fn(void);

/* ── EXPVAL — evaluate unevaluated expression ────────────────────────── */
/*
 * v311.sil EXPVAL (line 2699):
 *   Saves full interpreter state (OCBSCL OCICL PATBCL PATICL WPTR XCL YCL TCL
 *   MAXLEN LENFCL PDLPTR PDLHED NAMICL NHEDCL + specifiers HEADSP TSP TXSP XSP).
 *   Evaluates the expression object code pointed to by XPTR.
 *   Restores state on all exits.
 *   Exit 1 (FAIL): returns FAIL.
 *   Exit 2 (RTXNAM): result in XPTR, returns OK.
 *   Exit 3 (RTZPTR): result in ZPTR, returns OK (caller checks ZPTR).
 */
RESULT_t EXPVAL_fn(void);

/* ── EXPEVL — expression value context entry for EXPVAL ─────────────── */
/*
 * v311.sil EXPEVL (line 2757): EXPVAL with SCL=0 (expression context).
 */
RESULT_t EXPEVL_fn(void);

/* ── INTVAL — evaluate argument, coerce to INTEGER ───────────────────── */
/*
 * v311.sil INTVAL (line 2739):
 *   Like ARGVAL but coerces result to INTEGER.
 *   STRING → SPCINT → if fail → SPREAL → RLINT (truncate real to int).
 *   REAL → RLINT directly.
 *   Returns OK (result in XPTR, type I) or FAIL.
 */
RESULT_t INTVAL_fn(void);

/* ── PATVAL — evaluate argument, coerce to PATTERN ──────────────────── */
/*
 * v311.sil PATVAL (line 2797):
 *   Like ARGVAL but coerces result to PATTERN type.
 *   STRING/PATTERN: return as-is.
 *   INTEGER: GNVARI → string.
 *   REAL: REALST → GENVAR → string.
 *   EXPRESSION: wrap in STARPT pattern node.
 *   Returns OK (result in XPTR, type P or S) or FAIL.
 */
RESULT_t PATVAL_fn(void);

/* ── VARVAL — evaluate argument, coerce to STRING ────────────────────── */
/*
 * v311.sil VARVAL (line 2861):
 *   Like ARGVAL but coerces result to STRING.
 *   INTEGER → GNVARI → string variable.
 *   Other non-STRING types → INTR1 (illegal data type).
 *   Returns OK (result in XPTR, type S) or FAIL.
 */
RESULT_t VARVAL_fn(void);

/* ── VARVUP — VARVAL with case-folding ───────────────────────────────── */
/*
 * v311.sil VARVUP (line 2900) [PLB28][PLB29]:
 *   Calls VARVAL, then if &CASE!=0 folds to upper-case via VPXPTR.
 *   Returns OK (result in XPTR) or FAIL.
 */
RESULT_t VARVUP_fn(void);

/* ── VPXPTR — case-fold XPTR string to upper-case ────────────────────── */
/*
 * v311.sil VPXPTR (line 2909) [PLB29]:
 *   Allocates scratch at FRSGPT, copies+raises XPTR string to upper case,
 *   interns the upper-case version via GNVARS, stores back in XPTR.
 *   Returns OK (result in XPTR) always (null string returns unchanged).
 */
RESULT_t VPXPTR_fn(void);

/* ── XYARGS — evaluate argument pair ────────────────────────────────── */
/*
 * v311.sil XYARGS (line 2916):
 *   Evaluates two consecutive arguments from object code stream.
 *   First result → XPTR, second → YPTR.
 *   Returns OK or FAIL.
 */
RESULT_t XYARGS_fn(void);

#endif /* SIL_ARGVAL_H */
