/*
 * snobol4_inc.h — hardcoded SNOBOL4 .inc library functions
 *
 * These are C implementations of the .inc files used by beauty.sno:
 *   global.inc  — global variables (digits, tab, nl, ucase/lcase alphabets)
 *   is.inc      — IsSpitbol, IsSnobol4, IsType
 *   FENCE.inc   — FENCE primitive (already in pattern engine)
 *   io.inc      — io(), fileName(), input_/output_ channels
 *   case.inc    — lwr(), upr()
 *   assign.inc  — assign() — assignment during pattern matching
 *   mtch.inc   — mtch(), notmatch()
 *   counter.inc — nPush/nPop/nInc/nDec/nTop  (already in snobol4.c)
 *   stack.inc   — Push/Pop/Top/InitStack  (already in snobol4.c)
 *   tree.inc    — DATA(tree), t/v/n/c accessors  (already in snobol4.c)
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
SnoVal lwr(SnoVal s);
SnoVal upr(SnoVal s);

/* -------------------------------------------------------------------------
 * assign.inc: assign(name, expression)
 * Used as a unevaluated expression during pattern matching.
 * assign() always succeeds; sets $name = expression.
 * ---------------------------------------------------------------------- */
SnoVal assign_fn(SnoVal name, SnoVal expression);

/* -------------------------------------------------------------------------
 * mtch.inc: mtch(subject, pattern) — succeeds if subject matches pattern
 * notmatch(subject, pattern)         — succeeds if subject does NOT mtch
 * ---------------------------------------------------------------------- */
SnoVal match_fn(SnoVal subject, SnoVal pattern);
SnoVal notmatch_fn(SnoVal subject, SnoVal pattern);

/* -------------------------------------------------------------------------
 * io.inc: io(name, mode), file open/close helpers
 * ---------------------------------------------------------------------- */
SnoVal io_fn(SnoVal name, SnoVal mode);

/* -------------------------------------------------------------------------
 * Gen.inc: output generation with indentation
 * ---------------------------------------------------------------------- */
SnoVal Gen(SnoVal strv, SnoVal outNm);
SnoVal GenTab(SnoVal pos);
SnoVal GenSetCont(SnoVal cont);
SnoVal IncLevel(SnoVal delta);
SnoVal DecLevel(SnoVal delta);
SnoVal SetLevel(SnoVal level);
SnoVal GetLevel(void);

/* -------------------------------------------------------------------------
 * Qize.inc: Qize(s) — return "'s'" (single-quote wrapped string)
 * ---------------------------------------------------------------------- */
SnoVal Qize(SnoVal s);

/* -------------------------------------------------------------------------
 * ShiftReduce.inc: Shift(x), Reduce(tag, n), etc.
 * ---------------------------------------------------------------------- */
SnoVal Shift(SnoVal x);
SnoVal Reduce(SnoVal tag, SnoVal n);

/* -------------------------------------------------------------------------
 * TDump / XDump: debug tree/value dumps (no-op unless doDebug > 0)
 * ---------------------------------------------------------------------- */
SnoVal TDump(SnoVal x);
SnoVal XDump(SnoVal x);

/* -------------------------------------------------------------------------
 * omega/trace: TV, TW, TX, TY, TZ, T8Trace, T8Pos
 * All are no-ops when doDebug == 0 (which it is in production).
 * ---------------------------------------------------------------------- */
SnoVal TV(SnoVal lvl, SnoVal pat, SnoVal name);
SnoVal TW(SnoVal lvl, SnoVal pat, SnoVal name);
SnoVal TX(SnoVal lvl, SnoVal pat, SnoVal name);
SnoVal TY(SnoVal lvl, SnoVal name, SnoVal pat);
SnoVal TZ(SnoVal lvl, SnoVal name, SnoVal pat);
SnoVal T8Trace(SnoVal lvl, SnoVal strv, SnoVal ofs);
SnoVal T8Pos(SnoVal ofs, SnoVal map);

/* -------------------------------------------------------------------------
 * Additional helpers used in beauty.sno
 * ---------------------------------------------------------------------- */
SnoVal LEQ(SnoVal a, SnoVal b);   /* lexicographic LE */
SnoVal LGT(SnoVal a, SnoVal b);   /* lexicographic GT */
SnoVal LGE(SnoVal a, SnoVal b);
SnoVal LLT(SnoVal a, SnoVal b);
SnoVal LLE(SnoVal a, SnoVal b);
SnoVal LNE(SnoVal a, SnoVal b);

/* ss() and pp() are defined in beautiful.c (compiled from beauty.sno) */

#endif /* SNOBOL4_INC_H */
