/*
 * interp.h — Interpreter executive (v311.sil §7 lines 2520–2678)
 *
 * BASE   — code-basing: advance OCBSCL by OCICL, zero OCICL.
 * GOTG   — direct goto :<X>
 * GOTL   — label goto   :(X)
 * GOTO   — internal goto (load OCICL from object code)
 * INIT   — statement initialisation (update &STNO/&LINE/&FILE, check &STLIMIT)
 * INTERP — interpreter main loop
 * INVOKE — procedure invocation dispatch
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M19
 */

#ifndef SIL_INTERP_H
#define SIL_INTERP_H

#include "types.h"

RESULT_t BASE_fn(void);
RESULT_t GOTG_fn(void);
RESULT_t GOTL_fn(void);
RESULT_t GOTO_fn(void);
RESULT_t INIT_fn(void);
RESULT_t INTERP_fn(void);
RESULT_t INVOKE_fn(void);

/* Invoke-table entry: function pointer + argument count */
typedef RESULT_t (*invoke_fn_t)(void);
void invoke_table_register(int32_t idx, invoke_fn_t fn, int32_t nargs);

#endif /* SIL_INTERP_H */
