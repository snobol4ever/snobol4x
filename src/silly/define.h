/*
 * define.h — Defined functions (v311.sil §12 lines 4240–4470)
 *
 * DEFINE(P,E) — define a user function from prototype string.
 * DEFFNC      — invoke a defined function (called by INVOKE dispatch).
 *
 * Both require STREAM/VARATB (prototype scanner) and INTERP (M19).
 * They are stubbed here; the stubs compile clean and return FAIL until
 * those milestones are in place.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M14
 */

#ifndef SIL_DEFINE_H
#define SIL_DEFINE_H

#include "types.h"

/* DEFINE(P,E) — parse prototype and register function definition       */
RESULT_t DEFINE_fn(void);

/* DEFFNC — invoke a defined function (called by INVOKE dispatch table) */
RESULT_t DEFFNC_fn(void);

#endif /* SIL_DEFINE_H */
