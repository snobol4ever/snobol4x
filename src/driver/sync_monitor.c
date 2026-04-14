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
#include "frontend/prolog/term.h"        /* IM-11: Term, TT_REF, term_deref */
#include "frontend/prolog/prolog_atom.h" /* IM-11: prolog_atom_name */

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

    /* IM-11: Prolog trail-bound variables — walk trail stack 0..top-1 */
    s->pl_locals       = NULL;
    s->pl_locals_count = 0;
    {
        int top = trail_mark(&g_pl_trail);
        if (top > 0 && g_pl_trail.stack) {
            s->pl_locals = malloc((size_t)top * sizeof(*s->pl_locals));
            int out = 0;
            for (int ti = 0; ti < top; ti++) {
                Term *var = g_pl_trail.stack[ti];
                if (!var) continue;
                /* Name from saved_slot */
                char nbuf[32];
                snprintf(nbuf, sizeof nbuf, "_V%d", var->saved_slot);
                /* Deref to find bound value */
                Term *val = term_deref(var);
                char vbuf[256];
                if (!val || val->tag == TT_VAR) {
                    snprintf(vbuf, sizeof vbuf, "_");
                } else if (val->tag == TT_ATOM) {
                    const char *aname = prolog_atom_name(val->atom_id);
                    snprintf(vbuf, sizeof vbuf, "%s", aname ? aname : "?");
                } else if (val->tag == TT_INT) {
                    snprintf(vbuf, sizeof vbuf, "%ld", val->ival);
                } else if (val->tag == TT_FLOAT) {
                    snprintf(vbuf, sizeof vbuf, "%g", val->fval);
                } else if (val->tag == TT_COMPOUND) {
                    const char *fn = prolog_atom_name(val->compound.functor);
                    snprintf(vbuf, sizeof vbuf, "%s/%d", fn ? fn : "?", val->compound.arity);
                } else {
                    snprintf(vbuf, sizeof vbuf, "<term>");
                }
                s->pl_locals[out].name    = strdup(nbuf);
                s->pl_locals[out].val_str = strdup(vbuf);
                out++;
            }
            s->pl_locals_count = out;
        }
    }

    /* IM-10: ICN frame locals — walk all active frames, collect named slots */
    s->icn_locals       = NULL;
    s->icn_locals_count = 0;
    if (icn_frame_depth > 0) {
        /* Count total named slots across all frames */
        int total = 0;
        for (int fi = 0; fi < icn_frame_depth; fi++)
            total += icn_frame_stack[fi].sc.n;
        if (total > 0) {
            s->icn_locals = malloc((size_t)total * sizeof(NvPair));
            int out = 0;
            for (int fi = 0; fi < icn_frame_depth; fi++) {
                IcnFrame *f = &icn_frame_stack[fi];
                for (int si = 0; si < f->sc.n; si++) {
                    s->icn_locals[out].name = f->sc.e[si].name;
                    int slot = f->sc.e[si].slot;
                    s->icn_locals[out].val  = (slot >= 0 && slot < f->env_n)
                                              ? f->env[slot] : NULVCL;
                    out++;
                }
            }
            s->icn_locals_count = out;
        }
    }

    /* IM-8: last_ok — unknown until caller sets it after step run */
    s->last_ok = -1;
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
    free(s->label_path);
    s->label_path     = NULL;
    s->label_path_n   = 0;
    s->label_path_cap = 0;
    free(s->icn_locals);
    s->icn_locals       = NULL;
    s->icn_locals_count = 0;
    /* IM-11: free owned Prolog local strings */
    for (int i = 0; i < s->pl_locals_count; i++) {
        free(s->pl_locals[i].name);
        free(s->pl_locals[i].val_str);
    }
    free(s->pl_locals);
    s->pl_locals       = NULL;
    s->pl_locals_count = 0;
}

/*------------------------------------------------------------------------
 * IM-9: label_path_append — record one label entry into a snapshot
 *----------------------------------------------------------------------*/
static void label_path_append(ExecSnapshot *s, const char *lbl) {
    if (s->label_path_n >= s->label_path_cap) {
        int newcap = s->label_path_cap ? s->label_path_cap * 2 : 16;
        s->label_path = realloc(s->label_path, (size_t)newcap * sizeof(const char *));
        s->label_path_cap = newcap;
    }
    s->label_path[s->label_path_n++] = lbl;
}

/*------------------------------------------------------------------------
 * IM-9: label_path_print — print path as [A] → [B] → ... to stderr
 *----------------------------------------------------------------------*/
static void label_path_print(const char *tag, const ExecSnapshot *s) {
    fprintf(stderr, "  %-4s path:", tag);
    int printed = 0;
    for (int i = 0; i < s->label_path_n; i++) {
        if (!s->label_path[i]) continue;
        fprintf(stderr, "%s[%s]", printed ? " \u2192 " : " ", s->label_path[i]);
        printed++;
    }
    if (!printed) fprintf(stderr, " (no labels reached)");
    fprintf(stderr, "\n");
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
 * IM-6 + IM-9: sync_monitor_run — compare IR / SM / JIT statement by statement.
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
 * IM-9: label_path accumulated across iterations (one entry per stmt).
 *   On diverge, the full path to stmt N is printed for each executor.
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

    /* IM-9: build IR label index — walk linked list once to array */
    int nstmts = prog->nstmts;
    const char **ir_labels = calloc((size_t)(nstmts + 1), sizeof(const char *));
    { int i = 1; for (STMT_t *s = prog->head; s && i <= nstmts; s = s->next, i++)
        ir_labels[i] = (s->label && s->label[0]) ? s->label : NULL; }

    /* IM-9: persistent label path accumulators (grow as we step) */
    ExecSnapshot ir_path  = {0};
    ExecSnapshot sm_path  = {0};
    ExecSnapshot jit_path = {0};

    int diverge_at = 0;

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
        sm_snap.last_ok = sm_st.last_ok;

        /* ── JIT run to step n ── */
        exec_snapshot_restore(&baseline);
        SM_State jit_st; sm_state_init(&jit_st);
        sm_jit_run_steps(sm_prog, &jit_st, n);
        exec_snapshot_take(&jit_snap);
        jit_snap.last_ok = jit_st.last_ok;

        /* IM-9: append label for stmt n to each executor's path accumulator.
         * All three share the same source labels (same Program / SM_Program). */
        const char *lbl_n = (n <= sm_prog->stno_count) ? sm_prog->stno_labels[n] : NULL;
        label_path_append(&ir_path,  ir_labels[n]);
        label_path_append(&sm_path,  lbl_n);
        label_path_append(&jit_path, lbl_n);

        /* ── Compare ── */
        int ir_sm      = snap_diff(&ir_snap, "IR", &sm_snap,  "SM",  0);
        int ir_jit     = snap_diff(&ir_snap, "IR", &jit_snap, "JIT", 0);
        int ok_diverge = (sm_snap.last_ok != jit_snap.last_ok);

        if (ir_sm || ir_jit || ok_diverge) {
            /* IM-8: rich diverge header — stmt, label, line */
            const char *hdr_lbl = ir_labels[n] ? ir_labels[n] : "-";
            int lineno = 0;
            { int wi = 1; STMT_t *ws = prog->head;
              for (; ws && wi < n; ws = ws->next, wi++) {}
              if (ws) lineno = ws->lineno; }
            fprintf(stderr, "DIVERGE at stmt %d [label: %s, line %d]\n", n, hdr_lbl, lineno);

            /* IM-9: label paths */
            label_path_print("IR",  &ir_path);
            label_path_print("SM",  &sm_path);
            label_path_print("JIT", &jit_path);

            /* IM-8: per-executor last_ok */
            auto void print_exec_line(const char *tag, const ExecSnapshot *s);
            void print_exec_line(const char *tag, const ExecSnapshot *s) {
                if (s->last_ok < 0) fprintf(stderr, "  %-4s last_ok=?\n", tag);
                else                fprintf(stderr, "  %-4s last_ok=%d\n", tag, s->last_ok);
            }
            print_exec_line("IR",  &ir_snap);
            print_exec_line("SM",  &sm_snap);
            print_exec_line("JIT", &jit_snap);

            /* IM-10: ICN locals — print if any frame is active */
            if (ir_snap.icn_locals_count > 0) {
                fprintf(stderr, "  ICN locals (IR):\n");
                for (int li = 0; li < ir_snap.icn_locals_count; li++) {
                    const char *v = VARVAL_fn(ir_snap.icn_locals[li].val);
                    fprintf(stderr, "    %-16s = %s\n",
                            ir_snap.icn_locals[li].name, v ? v : "");
                }
            }

            /* IM-11: Prolog trail bindings — print if trail is non-empty */
            if (ir_snap.pl_locals_count > 0) {
                fprintf(stderr, "  Prolog bindings (IR):\n");
                for (int pi = 0; pi < ir_snap.pl_locals_count; pi++)
                    fprintf(stderr, "    %-16s = %s\n",
                            ir_snap.pl_locals[pi].name,
                            ir_snap.pl_locals[pi].val_str);
            }

            if (ir_sm) {
                fprintf(stderr, "  IR vs SM (%d var(s) differ):\n", ir_sm);
                snap_diff(&ir_snap, "IR", &sm_snap, "SM", 1);
            }
            if (ir_jit) {
                fprintf(stderr, "  IR vs JIT (%d var(s) differ):\n", ir_jit);
                snap_diff(&ir_snap, "IR", &jit_snap, "JIT", 1);
            }
            if (ok_diverge && !ir_sm && !ir_jit)
                fprintf(stderr, "  SM last_ok=%d vs JIT last_ok=%d (NV agrees)\n",
                        sm_snap.last_ok, jit_snap.last_ok);
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

    free(ir_labels);
    exec_snapshot_free(&ir_path);
    exec_snapshot_free(&sm_path);
    exec_snapshot_free(&jit_path);
    exec_snapshot_free(&baseline);
    sm_prog_free(sm_prog);
    return diverge_at;
}
