/*
 * pl_runtime.h — Prolog interpreter runtime API
 *
 * FI-5: declarations for all Prolog symbols moved from scrip.c to pl_runtime.c.
 * Include this in scrip.c (replaces the inline Prolog fwd-decl block).
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6 (FI-5, 2026-04-14)
 */
#ifndef DRIVER_PL_RUNTIME_H
#define DRIVER_PL_RUNTIME_H

#include "../../ir/ir.h"
#include "../../frontend/snobol4/scrip_cc.h"
#include "../../frontend/prolog/prolog_driver.h"
#include "../../frontend/prolog/term.h"
#include "../../frontend/prolog/prolog_runtime.h"   /* Trail */

/*------------------------------------------------------------------------
 * Constants / types
 *------------------------------------------------------------------------*/
#define PL_PRED_TABLE_SIZE_FWD 256

typedef struct Pl_PredEntry_t {
    const char *key; EXPR_t *choice; struct Pl_PredEntry_t *next;
} Pl_PredEntry;

typedef struct { Pl_PredEntry *buckets[PL_PRED_TABLE_SIZE_FWD]; } Pl_PredTable;

/*------------------------------------------------------------------------
 * Globals (defined in pl_runtime.c)
 *------------------------------------------------------------------------*/
extern Pl_PredTable  g_pl_pred_table;
extern Trail         g_pl_trail;       /* non-static: also used by pl_broker.c */
extern int           g_pl_cut_flag;    /* non-static: also used by pl_broker.c */
extern Term        **g_pl_env;
extern int           g_pl_active;

/*------------------------------------------------------------------------
 * Functions (defined in pl_runtime.c)
 *------------------------------------------------------------------------*/
EXPR_t *pl_pred_table_lookup(Pl_PredTable *pt, const char *key);
void    pl_pred_table_insert(Pl_PredTable *pt, const char *key, EXPR_t *choice);
EXPR_t *pl_pred_table_lookup_global(const char *key);

Term  **pl_env_new(int n);
Term   *pl_unified_term_from_expr(EXPR_t *e, Term **env);

int     is_pl_user_call(EXPR_t *goal);
int     interp_exec_pl_builtin(EXPR_t *goal, Term **env);
void    pl_execute_program_unified(Program *prog);

#endif /* DRIVER_PL_RUNTIME_H */
