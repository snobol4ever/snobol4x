/*============================================================= sync_monitor.c
 * In-process sync monitor — IM-2, IM-6
 *
 * exec_snapshot_take()    — capture all mutable executor state
 * exec_snapshot_restore() — reset world to captured state
 * exec_snapshot_free()    — release heap storage in snapshot
 * sync_monitor_run()      — IM-6: compare IR/SM/JIT statement by statement
 *==========================================================================*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sync_monitor.h"
#include "runtime/x86/snobol4.h"
#include "runtime/interp/icn_runtime.h"
#include "runtime/interp/pl_runtime.h"
#include "runtime/x86/sm_lower.h"
#include "runtime/x86/sm_prog.h"
#include "runtime/x86/sm_interp.h"
#include "runtime/x86/sm_codegen.h"
#include "runtime/x86/sm_image.h"
#include "interp.h"
#include "frontend/snobol4/scrip_cc.h"  /* Program, STMT_t */

/*------------------------------------------------------------------------
 * exec_snapshot_take — capture current mutable state
 *----------------------------------------------------------------------*/
void exec_snapshot_take(ExecSnapshot *s) {
    if (!s) return;

    /* NV store */
    s->nv_count = nv_snapshot(&s->nv_pairs);

    /* Keyword globals */
    s->kw_stcount      = kw_stcount;
    s->kw_stlimit      = kw_stlimit;
    s->kw_anchor       = kw_anchor;

    /* ICN frame depth (locals captured in IM-10) */
    s->icn_frame_depth = icn_frame_depth;

    /* Prolog trail mark */
    s->pl_trail_mark   = trail_mark(&g_pl_trail);
}

/*------------------------------------------------------------------------
 * exec_snapshot_restore — reset world to captured state
 *
 * Order matters:
 *   1. NV store reset + replay (nv_restore calls nv_reset internally)
 *   2. Keyword globals
 *   3. ICN frame stack unwind to depth 0
 *   4. Prolog trail unwind to captured mark
 *----------------------------------------------------------------------*/
void exec_snapshot_restore(const ExecSnapshot *s) {
    if (!s) return;

    /* 1. NV store */
    nv_restore(s->nv_pairs, s->nv_count);

    /* 2. Keywords */
    kw_stcount = s->kw_stcount;
    kw_stlimit = s->kw_stlimit;
    kw_anchor  = s->kw_anchor;

    /* 3. ICN frame stack — unwind to depth 0 */
    icn_frame_depth = 0;

    /* 4. Prolog trail — unwind to mark */
    trail_unwind(&g_pl_trail, s->pl_trail_mark);
}

/*------------------------------------------------------------------------
 * exec_snapshot_free — release heap storage
 *----------------------------------------------------------------------*/
void exec_snapshot_free(ExecSnapshot *s) {
    if (!s) return;
    free(s->nv_pairs);
    s->nv_pairs = NULL;
    s->nv_count = 0;
}

/*------------------------------------------------------------------------
 * snap_diff — compare two NV snapshots; print differences to stderr.
 * Returns number of differing variables.
 *----------------------------------------------------------------------*/
static int snap_diff(const ExecSnapshot *a, const char *a_name,
                     const ExecSnapshot *b, const char *b_name,
                     int verbose) {
    int ndiff = 0;

    /* Check every variable in a against b */
    for (int i = 0; i < a->nv_count; i++) {
        const char *name = a->nv_pairs[i].name;
        DESCR_t     va   = a->nv_pairs[i].val;
        /* Find matching name in b */
        DESCR_t     vb   = NULVCL;
        int found = 0;
        for (int j = 0; j < b->nv_count; j++) {
            if (strcmp(b->nv_pairs[j].name, name) == 0) {
                vb = b->nv_pairs[j].val; found = 1; break;
            }
        }
        /* Compare by string representation */
        const char *sa = VARVAL_fn(va);
        const char *sb = found ? VARVAL_fn(vb) : "";
        if (!sa) sa = "";
        if (!sb) sb = "";
        if (strcmp(sa, sb) != 0) {
            ndiff++;
            if (verbose)
                fprintf(stderr, "    %-12s  %s=%-20s  %s=%s\n",
                        name, a_name, sa, b_name, sb);
        }
    }
    /* Also check variables present in b but not in a */
    for (int j = 0; j < b->nv_count; j++) {
        const char *name = b->nv_pairs[j].name;
        int found = 0;
        for (int i = 0; i < a->nv_count; i++) {
            if (strcmp(a->nv_pairs[i].name, name) == 0) { found = 1; break; }
        }
        if (!found) {
            const char *sb = VARVAL_fn(b->nv_pairs[j].val);
            if (!sb) sb = "";
            if (*sb) {   /* only report if b's value is non-empty */
                ndiff++;
                if (verbose)
                    fprintf(stderr, "    %-12s  %s=%-20s  %s=%s\n",
                            name, a_name, "", b_name, sb);
            }
        }
    }
    return ndiff;
}

/*------------------------------------------------------------------------
 * IM-6: sync_monitor_run — compare IR / SM / JIT statement by statement.
 *
 * For each statement N = 1..nstmts:
 *   - restore baseline
 *   - run IR  to N, snapshot ir_snap
 *   - restore baseline
 *   - run SM  to N, snapshot sm_snap
 *   - restore baseline
 *   - run JIT to N, snapshot jit_snap
 *   - compare all three; on first divergence print report and return N
 *
 * Returns 0 if all three agree throughout.
 * Returns statement number of first divergence (1-based) otherwise.
 *
 * verbose: 0=silent on agreement, 1=print per-stmt progress, 2=full diff.
 *----------------------------------------------------------------------*/
int sync_monitor_run(void *prog_arg, int verbose) {
    Program *prog = (Program *)prog_arg;
    /* ── Build SM_Program once ── */
    SM_Program *sm_prog = sm_lower(prog);
    if (!sm_prog) { fprintf(stderr, "sync_monitor: sm_lower failed\n"); return -1; }

    /* ── Initialise JIT image once ── */
    if (sm_image_init() != 0) {
        fprintf(stderr, "sync_monitor: sm_image_init failed\n");
        sm_prog_free(sm_prog); return -1;
    }
    if (sm_codegen(sm_prog) != 0) {
        fprintf(stderr, "sync_monitor: sm_codegen failed\n");
        sm_prog_free(sm_prog); return -1;
    }

    /* ── Take baseline snapshot (clean state after polyglot_init) ── */
    ExecSnapshot baseline = {0};
    exec_snapshot_take(&baseline);

    int diverge_at = 0;
    int nstmts = prog->nstmts;

    for (int n = 1; n <= nstmts; n++) {
        ExecSnapshot ir_snap  = {0};
        ExecSnapshot sm_snap  = {0};
        ExecSnapshot jit_snap = {0};

        /* ── IR run to step n ── */
        exec_snapshot_restore(&baseline);
        execute_program_steps(prog, n);
        exec_snapshot_take(&ir_snap);

        /* ── SM run to step n ── */
        exec_snapshot_restore(&baseline);
        SM_State sm_st; sm_state_init(&sm_st);
        sm_interp_run_steps(sm_prog, &sm_st, n);
        exec_snapshot_take(&sm_snap);

        /* ── JIT run to step n ── */
        exec_snapshot_restore(&baseline);
        SM_State jit_st; sm_state_init(&jit_st);
        sm_jit_run_steps(sm_prog, &jit_st, n);
        exec_snapshot_take(&jit_snap);

        /* ── Compare ── */
        int ir_sm  = snap_diff(&ir_snap,  "IR",  &sm_snap,  "SM",  0);
        int ir_jit = snap_diff(&ir_snap,  "IR",  &jit_snap, "JIT", 0);

        if (ir_sm || ir_jit) {
            /* Find label for this statement */
            const char *lbl = (prog->stmts && n <= nstmts && prog->stmts[n-1].label)
                              ? prog->stmts[n-1].label : "-";
            fprintf(stderr, "DIVERGE at stmt %d [label: %s]\n", n, lbl);
            if (ir_sm) {
                fprintf(stderr, "  IR vs SM (%d var(s) differ):\n", ir_sm);
                snap_diff(&ir_snap, "IR", &sm_snap, "SM", 1);
            }
            if (ir_jit) {
                fprintf(stderr, "  IR vs JIT (%d var(s) differ):\n", ir_jit);
                snap_diff(&ir_snap, "IR", &jit_snap, "JIT", 1);
            }
            diverge_at = n;
            exec_snapshot_free(&ir_snap);
            exec_snapshot_free(&sm_snap);
            exec_snapshot_free(&jit_snap);
            break;
        }

        if (verbose >= 1)
            fprintf(stderr, "  stmt %4d/%d: IR=SM=JIT agree\n", n, nstmts);

        exec_snapshot_free(&ir_snap);
        exec_snapshot_free(&sm_snap);
        exec_snapshot_free(&jit_snap);
    }

    if (!diverge_at && verbose >= 1)
        fprintf(stderr, "sync_monitor: all %d statements agree across IR/SM/JIT\n", nstmts);

    exec_snapshot_free(&baseline);
    sm_prog_free(sm_prog);
    return diverge_at;
}
