/*
 * extern.h — External functions (v311.sil §13 lines 4471–4643)
 *
 * LOAD(P,L)  — load an external function from library
 * UNLOAD(F)  — unload an external function
 * LNKFNC     — invoke linked external function (dispatch stub)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M17
 */

#ifndef SIL_EXTERN_H
#define SIL_EXTERN_H

#include "types.h"

RESULT_t LOAD_fn(void);    /* LOAD(P,L) — load external function        */
RESULT_t UNLOAD_fn(void);  /* UNLOAD(F) — unload external function      */
RESULT_t LNKFNC_fn(void);  /* invoke loaded external function           */

#endif /* SIL_EXTERN_H */
