/*============================================================= sync_monitor.h
 * In-process sync monitor — IM-2
 *
 * ExecSnapshot captures all mutable global state that the three executors
 * (IR, SM, JIT) share. exec_snapshot_take() captures; exec_snapshot_restore()
 * resets the world to that state so the next executor starts clean.
 *
 * In production: nothing here is called — zero overhead.
 *==========================================================================*/
#ifndef SYNC_MONITOR_H
#define SYNC_MONITOR_H

#include "runtime/x86/snobol4.h"          /* NvPair, DESCR_t */
#include "runtime/interp/icn_runtime.h"   /* IcnFrame, icn_frame_depth */
#include "runtime/interp/pl_runtime.h"    /* Trail, trail_mark */

/*------------------------------------------------------------------------
 * ExecSnapshot — flat capture of all mutable inter-executor state
 *----------------------------------------------------------------------*/
typedef struct {
    /* NV store snapshot */
    NvPair  *nv_pairs;
    int      nv_count;

    /* Keyword globals */
    int64_t  kw_stcount;
    int64_t  kw_stlimit;
    int64_t  kw_anchor;

    /* ICN frame stack depth (frame locals snapshotted in IM-10) */
    int      icn_frame_depth;

    /* Prolog trail mark — restore by unwinding to this position */
    int      pl_trail_mark;

    /* IM-8: last statement success/fail flag (-1 = unknown/IR) */
    int      last_ok;

    /* IM-9: sequence of source labels reached, one entry per statement executed.
     * label_path[i] = label of the (i+1)th statement executed, or NULL if unlabelled.
     * label_path_n  = number of entries (= steps executed by this executor).\
     * Strings are not owned — they point into STMT_t / SM_Program storage. */
    const char **label_path;
    int          label_path_n;
    int          label_path_cap;

    /* IM-10: ICN/Raku frame-local variables.
     * Flat array of NvPair, one per named slot across all active frames.
     * Names are not owned (point into IcnScope string storage).
     * icn_locals_count = 0 when no ICN frames are active. */
    NvPair  *icn_locals;
    int      icn_locals_count;

    /* IM-11: Prolog trail-bound variables.
     * One entry per trail slot that is currently bound (tag==TT_REF).
     * Both name and val_str are owned heap strings. */
    struct PlLocalPair {
        char *name;     /* "_V<saved_slot>" */
        char *val_str;  /* stringified dereffed value */
    } *pl_locals;
    int      pl_locals_count;
} ExecSnapshot;

/*------------------------------------------------------------------------
 * Public interface
 *----------------------------------------------------------------------*/
void exec_snapshot_take(ExecSnapshot *s);
void exec_snapshot_restore(const ExecSnapshot *s);
void exec_snapshot_free(ExecSnapshot *s);

/* IM-6: in-process comparator — runs all three executors step by step.
 * Returns 0 if IR/SM/JIT agree on all statements, else first diverging stmt#.
 * verbose: 0=silent on agreement, 1=per-stmt progress, 2=full diff. */
struct Program_s;   /* forward decl — scrip_cc.h typedef'd as Program */
typedef struct Program_s Program_im6_fwd;
/* Use void* to avoid pulling in scrip_cc.h here; sync_monitor.c casts. */
int sync_monitor_run(void *prog, int verbose);

#endif /* SYNC_MONITOR_H */
