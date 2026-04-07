/*
 * sil_arrays.h — Arrays, Tables, and Defined Data Objects (v311.sil §14)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M13
 */

#ifndef SIL_ARRAYS_H
#define SIL_ARRAYS_H

#include "sil_types.h"

RESULT_t ARRAY_fn(void);    /* ARRAY(P,V)  — create array              */
RESULT_t ASSOC_fn(void);    /* TABLE(N,M)  — create table              */
RESULT_t DATDEF_fn(void);   /* DATA(P)     — define data type          */
RESULT_t PROTO_fn(void);    /* PROTOTYPE(A)— return prototype          */
RESULT_t FREEZE_fn(void);   /* FREEZE(T)   — freeze table              */
RESULT_t THAW_fn(void);     /* THAW(T)     — thaw table                */
RESULT_t ITEM_fn(void);     /* array/table reference                   */
RESULT_t DEFDAT_fn(void);   /* create defined data object              */
RESULT_t FIELD_fn(void);    /* field accessor procedure                */
RESULT_t RSORT_fn(void);    /* RSORT(T,C)  — reverse sort (stub)      */
RESULT_t SORT_fn(void);     /* SORT(T,C)   — sort (stub)              */

/* Internal: ASSOCE — initialise a new table extent */
RESULT_t ASSOCE_fn(DESCR_t size, DESCR_t ext);

#endif /* SIL_ARRAYS_H */
