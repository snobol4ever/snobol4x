/*
 * mock_includes.h — hardcoded SNOBOL4 .inc library functions
 *
 * These are C implementations of the .inc files used by beauty.sno:
 *   global.inc  — global variables (digits, tab, nl, ucase/lcase alphabets)
 *   is.inc      — IsSpitbol, IsSnobol4, IsType
 *   FENCE.inc   — FENCE primitive (already in pattern engine)
 *   io.inc      — io(), fileName(), input_/output_ channels
 *   case.inc    — lwr(), upr()
 *   assign.inc  — assign() — assignment during pattern matching
 *   MATCH_fn.inc   — MATCH_fn(), notmatch()
 *   counter.inc — nPush/nPop/nInc/nDec/nTop  (already in snobol4.c)
 *   stack.inc   — Push/Pop/Top/InitStack  (already in snobol4.c)
 *   tree.inc    — DT_DATA(tree), t/v/n/c accessors  (already in snobol4.c)
 *   ShiftReduce.inc — Shift(), Reduce(), etc.
 *   TDump.inc   — TDump() — tree dump  (debug, no-op ok)
 *   Gen.inc     — Gen/GenTab/GenSetCont/IncLevel/DecLevel/SetLevel/GetLevel
 *   Qize.inc    — Qize() — quote a string
 *   ReadWrite.inc — file I/O helpers
 *   XDump.inc   — XDump() — debug (no-op ok)
 *   semantic.inc — semantic helpers
 *   omega.inc   — TV/TW/TX/TY/TZ  (tracing, no-op when doDebug==0)
 *   trace.inc   — T8Trace, T8Pos  (no-op when doDebug==0)
 */

#ifndef SNOBOL4_INC_H
#define SNOBOL4_INC_H

#include "snobol4.h"

/* -------------------------------------------------------------------------
 * Initialise all inc-layer globals and register all inc functions
 * in the global function table. Call once before program().
 * ---------------------------------------------------------------------- */
void inc_init(void);

/* -------------------------------------------------------------------------
 * case.inc: lwr(s), upr(s)
 * ---------------------------------------------------------------------- */
DESCR_t lwr(DESCR_t s);
DESCR_t upr(DESCR_t s);

/* -------------------------------------------------------------------------
 * assign.inc: assign(name, expression)
 * Used as a unevaluated expression during pattern matching.
 * assign() always succeeds; sets $name = expression.
 * ---------------------------------------------------------------------- */
DESCR_t assign_fn(DESCR_t name, DESCR_t expression);

/* -------------------------------------------------------------------------
 * MATCH_fn.inc: MATCH_fn(subject, pattern) — succeeds if subject matches pattern
 * notmatch(subject, pattern)         — succeeds if subject does NOT MATCH_fn
 * ---------------------------------------------------------------------- */
DESCR_t match_fn(DESCR_t subject, DESCR_t pattern);
DESCR_t notmatch_fn(DESCR_t subject, DESCR_t pattern);

/* -------------------------------------------------------------------------
 * io.inc: io(name, mode), file open/close helpers
 * ---------------------------------------------------------------------- */
DESCR_t io_fn(DESCR_t name, DESCR_t mode);

/* -------------------------------------------------------------------------
 * Gen.inc: output generation with indentation
 * ---------------------------------------------------------------------- */
DESCR_t Gen(DESCR_t STRVAL_fn, DESCR_t outNm);
DESCR_t GenTab(DESCR_t pos);
DESCR_t GenSetCont(DESCR_t cont);
DESCR_t IncLevel(DESCR_t delta);
DESCR_t DecLevel(DESCR_t delta);
DESCR_t SetLevel(DESCR_t level);
DESCR_t GetLevel(void);

/* -------------------------------------------------------------------------
 * Qize.inc: Qize(s) — return "'s'" (single-quote wrapped string)
 * ---------------------------------------------------------------------- */
DESCR_t Qize(DESCR_t s);

/* -------------------------------------------------------------------------
 * ShiftReduce.inc: Shift(x), Reduce(tag, n), etc.
 * ---------------------------------------------------------------------- */
DESCR_t Shift(DESCR_t x);
DESCR_t Reduce(DESCR_t tag, DESCR_t n);

/* -------------------------------------------------------------------------
 * TDump / XDump: debug tree/value dumps (no-op unless doDebug > 0)
 * ---------------------------------------------------------------------- */
DESCR_t TDump(DESCR_t x);
DESCR_t XDump(DESCR_t x);

/* -------------------------------------------------------------------------
 * omega/trace: TV, TW, TX, TY, TZ, T8Trace, T8Pos
 * All are no-ops when doDebug == 0 (which it is in production).
 * ---------------------------------------------------------------------- */
DESCR_t TV(DESCR_t lvl, DESCR_t pat, DESCR_t name);
DESCR_t TW(DESCR_t lvl, DESCR_t pat, DESCR_t name);
DESCR_t TX(DESCR_t lvl, DESCR_t pat, DESCR_t name);
DESCR_t TY(DESCR_t lvl, DESCR_t name, DESCR_t pat);
DESCR_t TZ(DESCR_t lvl, DESCR_t name, DESCR_t pat);
DESCR_t T8Trace(DESCR_t lvl, DESCR_t STRVAL_fn, DESCR_t ofs);
DESCR_t T8Pos(DESCR_t ofs, DESCR_t map);

/* -------------------------------------------------------------------------
 * Additional helpers used in beauty.sno
 * ---------------------------------------------------------------------- */
DESCR_t LEQ(DESCR_t a, DESCR_t b);   /* lexicographic LE */
DESCR_t LGT(DESCR_t a, DESCR_t b);   /* lexicographic GT */
DESCR_t LGE(DESCR_t a, DESCR_t b);
DESCR_t LLT(DESCR_t a, DESCR_t b);
DESCR_t LLE(DESCR_t a, DESCR_t b);
DESCR_t LNE(DESCR_t a, DESCR_t b);

/* ss() and pp() are defined in beautiful.c (compiled from beauty.sno) */

#endif /* SNOBOL4_INC_H */
