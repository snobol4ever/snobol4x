/*
 * func.h — Other functions (v311.sil §19 lines 6322–7037)
 *
 * Faithful C translation of §19 procedures.
 * Complex procs (OPSYN, CONVERT, DMP/DUMP, ARG/LOCAL/FIELDS, CNVTA/CNVAT)
 * are stubbed pending deeper infrastructure.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M12
 */

#ifndef SIL_FUNC_H
#define SIL_FUNC_H

#include "types.h"

RESULT_t APPLY_fn(void);
RESULT_t ARGINT_fn(void);
RESULT_t ARG_fn(void);
RESULT_t LOCAL_fn(void);
RESULT_t FIELDS_fn(void);
RESULT_t CLEAR_fn(void);
RESULT_t CMA_fn(void);
RESULT_t COLECT_fn(void);
RESULT_t COPY_fn(void);
RESULT_t CNVRT_fn(void);
RESULT_t CODER_fn(void);
RESULT_t CONVE_fn(void);
RESULT_t CONVR_fn(void);
RESULT_t CONIR_fn(void);
RESULT_t CONRI_fn(void);
RESULT_t CNVIV_fn(void);
RESULT_t CNVVI_fn(void);
RESULT_t CNVRTS_fn(void);
RESULT_t DATE_fn(void);
RESULT_t DT_fn(void);
RESULT_t DMP_fn(void);
RESULT_t DUMP_fn(void);
RESULT_t DUPL_fn(void);
RESULT_t OPSYN_fn(void);
RESULT_t RPLACE_fn(void);
RESULT_t REVERS_fn(void);
RESULT_t SIZE_fn(void);
RESULT_t SUBSTR_fn(void);
RESULT_t TIME_fn(void);
RESULT_t TRIM_fn(void);
RESULT_t VDIFFR_fn(void);

#endif /* SIL_FUNC_H */
