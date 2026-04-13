#ifndef PL_BROKER_H
#define PL_BROKER_H
/*======================================================================================================================
 * pl_broker.h — Prolog Byrd Box Broker (GOAL-PROLOG-BB-BYRD)
 *
 * Pl_GoalBox is a live box instance: a bb_box_fn pointer + opaque state.
 * It reuses the bb_box_fn ABI from bb_box.h unchanged:
 *
 *   entry == α (0): first call — attempt goal
 *   entry == β (1): retry call — next solution or ω
 *   return spec_is_empty() == true  → ω (failure)
 *   return spec_is_empty() == false → γ (success)
 *
 * Prolog does not use the positional extent of spec_t (σ/δ fields).
 * Success is signalled by returning any non-empty spec; failure by spec_empty.
 * The broker calls root.fn(root.zeta, α) and returns 1 (γ) or 0 (ω).
 *
 * Box constructors are defined in pl_broker.c.
 *--------------------------------------------------------------------------------------------------------------------*/

#include "runtime/x86/bb_box.h"   /* spec_t, bb_box_fn, α, β, spec_empty, spec_is_empty */
#include "frontend/prolog/prolog_runtime.h"
#include "scrip_cc.h"              /* EXPR_t — needed for pl_box_builtin */
#include "frontend/prolog/prolog_builtin.h" /* interp_exec_pl_builtin */

/*----------------------------------------------------------------------------------------------------------------------
 * Pl_GoalBox — a live, callable Prolog goal box
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct {
    bb_box_fn  fn;     /* box function — called with (zeta, entry)        */
    void      *zeta;   /* opaque per-instance state (heap-allocated)      */
} Pl_GoalBox;

/* Null/empty sentinel — fn == NULL means "no box" */
static inline int pl_goalbox_is_null(Pl_GoalBox b) { return b.fn == NULL; }

/*----------------------------------------------------------------------------------------------------------------------
 * pl_exec_goal — top-level broker entry point
 *
 * Calls root.fn(root.zeta, α).
 * Returns 1 if γ (success), 0 if ω (failure).
 * No scan loop — Prolog is not positional; retry is the caller box's job.
 *--------------------------------------------------------------------------------------------------------------------*/
int pl_exec_goal(Pl_GoalBox root);

/*----------------------------------------------------------------------------------------------------------------------
 * Leaf box constructors
 *--------------------------------------------------------------------------------------------------------------------*/

/* pl_box_true — γ on α, ω on β (succeed exactly once) */
Pl_GoalBox pl_box_true(void);

/* pl_box_fail — ω on α and β (always fail) */
Pl_GoalBox pl_box_fail(void);

/* pl_box_builtin — defined in S-BB-2; declared here for forward ref */
/* Signature finalised when interp_exec_pl_builtin linkage is resolved. */

/*----------------------------------------------------------------------------------------------------------------------
 * S-BB-2 Leaf box constructors
 *--------------------------------------------------------------------------------------------------------------------*/

/* pl_box_true — γ on α, ω on β (succeed exactly once) */
Pl_GoalBox pl_box_true(void);

/* pl_box_fail — ω on α and β (always fail) */
Pl_GoalBox pl_box_fail(void);

/* pl_box_builtin — α calls interp_exec_pl_builtin(goal, env); β returns ω */
Pl_GoalBox pl_box_builtin(EXPR_t *goal, Term **env);


/*----------------------------------------------------------------------------------------------------------------------
 * S-BB-3: CAT / AND-box (conjunction)
 *--------------------------------------------------------------------------------------------------------------------*/
Pl_GoalBox pl_box_cat(Pl_GoalBox left, Pl_GoalBox right);
Pl_GoalBox pl_box_cat_list(Pl_GoalBox *goals, int n);

/*----------------------------------------------------------------------------------------------------------------------
 * S-BB-4: Clause box (head-unify + body goals)
 *--------------------------------------------------------------------------------------------------------------------*/
/* pl_build_clause_box is file-scope static in pl_broker.c; not exported */

/*----------------------------------------------------------------------------------------------------------------------
 * S-BB-5: OR-box / choice over clauses
 *--------------------------------------------------------------------------------------------------------------------*/
Pl_GoalBox pl_box_choice(EXPR_t **clauses, int nclause, Term **env, Trail *trail);

/*----------------------------------------------------------------------------------------------------------------------
 * S-BB-6: CUT box (FENCE analog)
 *--------------------------------------------------------------------------------------------------------------------*/
Pl_GoalBox pl_box_cut(int *cut_flag);

#endif /* PL_BROKER_H */
