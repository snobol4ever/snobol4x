/*
 * sil_pred.h — Predicate functions (v311.sil §18 lines 6102–6321)
 *
 * Faithful C translation of:
 *   DIFFER FUNCTN IDENT LABEL LABELC
 *   LEQ LGE LGT LLE LLT LNE
 *   NEG QUES
 *   CHAR LPAD RPAD
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M11
 */

#ifndef SIL_PRED_H
#define SIL_PRED_H

#include "types.h"

RESULT_t DIFFER_fn(void);
RESULT_t FUNCTN_fn(void);
RESULT_t IDENT_fn(void);
RESULT_t LABEL_fn(void);
RESULT_t LABELC_fn(void);
RESULT_t LEQ_fn(void);
RESULT_t LGE_fn(void);
RESULT_t LGT_fn(void);
RESULT_t LLE_fn(void);
RESULT_t LLT_fn(void);
RESULT_t LNE_fn(void);
RESULT_t NEG_fn(void);
RESULT_t QUES_fn(void);
RESULT_t CHAR_fn(void);
RESULT_t LPAD_fn(void);
RESULT_t RPAD_fn(void);

#endif /* SIL_PRED_H */
