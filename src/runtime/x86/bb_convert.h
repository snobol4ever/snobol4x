/*============================================================================================================================
 * bb_convert.h — Converters between spec_t (SNOBOL4 cursor) and DESCR_t (universal value)
 *
 * U-2: bridge between the old bb_box_fn ABI (spec_t) and the new univ_box_fn ABI (DESCR_t).
 * Used by bb_broker.c (U-3) and stmt_exec.c Phase 3 (U-9) during the migration.
 * At U-5 (all SNOBOL4 boxes return DESCR_t) these become thin identity helpers.
 *
 * Include order: include bb_box.h and snobol4.h before this header.
 *============================================================================================================================*/

#ifndef BB_CONVERT_H
#define BB_CONVERT_H

#include "bb_box.h"
#include "snobol4.h"

/*------------------------------------------------------------------------------------------------------------------------------
 * descr_from_spec — wrap a spec_t substring match as a DESCR_t DT_S value.
 * spec_empty (σ==NULL) maps to FAILDESCR.
 * The σ pointer and δ length are preserved verbatim in .s and .slen.
 *----------------------------------------------------------------------------------------------------------------------------*/
static inline DESCR_t descr_from_spec(spec_t s) {
    if (spec_is_empty(s)) return FAILDESCR;
    return (DESCR_t){ .v = DT_S, .slen = (uint32_t)s.δ, .s = s.σ };
}

/*------------------------------------------------------------------------------------------------------------------------------
 * spec_from_descr — extract a spec_t from a DESCR_t DT_S value.
 * Non-DT_S or NULL .s maps to spec_empty (failure sentinel).
 * Used by Phase 3 in stmt_exec.c to recover σ/δ for cursor arithmetic.
 *----------------------------------------------------------------------------------------------------------------------------*/
static inline spec_t spec_from_descr(DESCR_t d) {
    if (IS_FAIL_fn(d) || d.v != DT_S || !d.s) return spec_empty;
    return spec(d.s, (int)d.slen);
}

#endif /* BB_CONVERT_H */
