/*
 * sil_nmd.h — Naming list commit procedure (v311.sil §17 NMD)
 *
 * Faithful C translation of v311.sil lines 6055–6091:
 *   NMD NMD1 NMD2 NMD3 NMD4 NMD5 NMDIC NAMEXN
 *
 * Called after a successful pattern match to walk the name list
 * (NBSPTR/NAMICL/NHEDCL) and assign captured substrings to their
 * target variables.  Each entry is a (SPEC_t, DESCR_t) pair:
 *   SPEC_t  — captured substring specifier
 *   DESCR_t — target variable descriptor (type S, K, or E)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M9
 */

#ifndef SIL_NMD_H
#define SIL_NMD_H

#include "types.h"

/* NMD_fn — commit all name-list captures since NHEDCL to NAMICL.
 * v311.sil NMD line 6055.
 * Returns OK always (failures are non-fatal, matching SIL RTN2 exit). */
RESULT_t NMD_fn(void);

#endif /* SIL_NMD_H */
