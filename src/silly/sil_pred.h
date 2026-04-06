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

#include "sil_types.h"

SIL_result DIFFER_fn(void);
SIL_result FUNCTN_fn(void);
SIL_result IDENT_fn(void);
SIL_result LABEL_fn(void);
SIL_result LABELC_fn(void);
SIL_result LEQ_fn(void);
SIL_result LGE_fn(void);
SIL_result LGT_fn(void);
SIL_result LLE_fn(void);
SIL_result LLT_fn(void);
SIL_result LNE_fn(void);
SIL_result NEG_fn(void);
SIL_result QUES_fn(void);
SIL_result CHAR_fn(void);
SIL_result LPAD_fn(void);
SIL_result RPAD_fn(void);

#endif /* SIL_PRED_H */
