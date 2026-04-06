/*
 * bb_flat.h — Flat-Glob Invariant Pattern Emitter (M-DYN-FLAT)
 *
 * bb_build_flat(p) walks an entire invariant PATND_t tree and emits
 * ALL sub-box code into ONE contiguous buffer with direct jmps between
 * boxes — no call/ret, no bb_seq trampoline, no per-node heap zeta.
 *
 * Layout:
 *   [entry]  cmp esi,0; je PAT_α; jmp PAT_β
 *   [PAT_α]  first box α code → jmp next_α or jmp PAT_ω
 *   [PAT_β]  first box β code → jmp prev_β or jmp PAT_ω
 *   ...      all sub-boxes inlined flat, wired by direct jmps
 *   [PAT_γ]  rax=σ, rdx=δ; ret
 *   [PAT_ω]  xor eax,eax; xor edx,edx; ret
 *   [data]   mutable slots: Δ_save values, matched.δ accumulators
 *
 * r10 = &Δ (global cursor pointer), loaded once at entry.
 * All cursor reads/writes use [r10] directly — no per-box reload.
 *
 * Only invariant patterns are eligible (patnd_is_invariant() == 1).
 * Variant nodes (XDSAR, XVAR, XATP, XFNME, XNME, XFARB, XSTAR)
 * fall back to bb_build_binary_node() trampoline path.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Sprint:  RT-129 / M-DYN-FLAT
 */

#ifndef BB_FLAT_H
#define BB_FLAT_H

#include "bb_pool.h"
#include "../snobol4/snobol4.h"
#include "../boxes/shared/bb_box.h"

/*
 * Build a flat-globbed blob for an entire invariant PATND_t tree.
 * Returns NULL if p is variant or allocation fails — caller uses
 * bb_build_binary_node() trampoline fallback.
 */
bb_box_fn bb_build_flat(PATND_t *p);

#endif /* BB_FLAT_H */
