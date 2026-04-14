/*
 * interp.h — IR tree-walk interpreter public interface
 *
 * Declarations for everything in interp.c that is referenced by
 * scrip.c or polyglot.c (after FI-7).
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-14
 * PURPOSE: GOAL-FULL-INTEGRATION FI-6 — interp loop extracted from scrip.c
 */

#ifndef INTERP_H
#define INTERP_H

#include <stdint.h>
#include <setjmp.h>
#include "frontend/snobol4/scrip_cc.h"  /* EXPR_t, STMT_t, Program, DESCR_t */

/* ── Diagnostic flags (set in main, read by execute_program / sm_interp) ── */
extern int g_opt_trace;
extern int g_opt_dump_bb;

/* ── Program state ─────────────────────────────────────────────────────── */
extern Program *g_prog;     /* current program (set before execute_program) */
extern int      g_polyglot; /* 1 when running a fenced polyglot .scrip file */

/* ── ScripModule registry — lives in scrip.c; execute_program uses it (FI-7 will move) ── */
#define SCRIP_MOD_MAX 64
typedef struct {
    int          lang;
    const char  *name;
    STMT_t      *first;
    STMT_t      *last;
    int          nstmts;
    int          sno_label_start;
    int          sno_label_count;
    int          icn_proc_start;
    int          icn_proc_count;
} ScripModule;
typedef struct {
    ScripModule  mods[SCRIP_MOD_MAX];
    int          nmod;
    int          main_mod;
} ScripModuleRegistry;
extern ScripModuleRegistry g_registry;
/* polyglot_init declared in polyglot.h — forward ref for execute_program */
void polyglot_init(Program *prog, uint32_t lang_mask);
uint32_t polyglot_lang_mask(Program *prog);

/* ── Label table ───────────────────────────────────────────────────────── */
extern int label_count;     /* needed by polyglot_init for sno_label_start */
void    label_table_build(Program *prog);
STMT_t *label_lookup(const char *name);
void    prescan_defines(Program *prog);

/* ── Core interpreter entry points ────────────────────────────────────── */
DESCR_t interp_eval    (EXPR_t *e);
DESCR_t interp_eval_pat(EXPR_t *e);
void    execute_program(Program *prog);
void    execute_program_steps(Program *prog, int n);  /* IM-3: step-limited run */
void    ir_dump_program(Program *prog, FILE *f);

/* IM-3: IR step-limit globals */
extern int     g_ir_step_limit;
extern int     g_ir_steps_done;
extern jmp_buf g_ir_step_jmp;

/* ── Hook functions registered in main() ──────────────────────────────── */
DESCR_t _builtin_IDENT  (DESCR_t *args, int nargs);
DESCR_t _builtin_DIFFER (DESCR_t *args, int nargs);
DESCR_t _builtin_EVAL   (DESCR_t *args, int nargs);
DESCR_t _builtin_CODE   (DESCR_t *args, int nargs);
DESCR_t _builtin_DATA   (DESCR_t *args, int nargs);
DESCR_t _builtin_print  (DESCR_t *args, int nargs);
DESCR_t _usercall_hook  (const char *name, DESCR_t *args, int nargs);
int     _label_exists_fn(const char *name);
DESCR_t _eval_str_impl_fn(const char *s);
DESCR_t _eval_pat_impl_fn(DESCR_t pat);

#endif /* INTERP_H */
