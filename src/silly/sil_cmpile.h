/*
 * sil_cmpile.h — Statement compilation (v311.sil §6 CMPILE)
 *
 * CMPILE_fn — compile one statement from TEXTSP into object code.
 * CDIAG_fn  — compiler diagnostic handler (error recovery).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18c
 */

#ifndef SIL_CMPILE_H
#define SIL_CMPILE_H

#include "sil_types.h"

/* CMPILE — compile one SNOBOL4 statement.
 * Exits: OK=normal, FAIL=END statement seen, 3=statement done (RTN3). */
RESULT_t CMPILE_fn(void);

/* CDIAG — compiler diagnostic: emit error, insert ERROR function. */
void CDIAG_fn(void);

#endif /* SIL_CMPILE_H */
