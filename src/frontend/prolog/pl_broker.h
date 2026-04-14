#ifndef PL_BROKER_H
#define PL_BROKER_H
/*======================================================================================================================
 * pl_broker.h — Prolog Byrd Box Broker (GOAL-PROLOG-BB-BYRD, GOAL-UNIFIED-BROKER U-8)
 *
 * U-8: Pl_GoalBox → bb_node_t (unified with SNOBOL4 / Icon boxes).
 *      U-11: pl_exec_goal removed — callers use bb_broker(root, BB_ONCE, NULL, NULL) directly.
 *
 * All Prolog goal boxes share the universal Byrd box signature:
 *   DESCR_t (*bb_box_fn)(void *zeta, int entry)
 *
 * Same four-signal protocol:
 *   entry == α (0): first call  — attempt goal
 *   entry == β (1): retry call  — next solution or ω
 *   IS_FAIL_fn(result) → ω (failure)
 *   !IS_FAIL_fn(result) → γ (success)
 *
 * Prolog boxes do not use the positional extent of DESCR_t.
 * γ is signalled by returning NULVCL — a non-fail sentinel.
 * Retry belongs to the OR-box (pl_box_choice), not the broker.
 *--------------------------------------------------------------------------------------------------------------------*/

#include "runtime/x86/bb_broker.h"         /* bb_box_fn, bb_node_t, BrokerMode, bb_broker, DESCR_t, FAILDESCR, IS_FAIL_fn, α, β */
#include "frontend/prolog/prolog_runtime.h"
#include "frontend/snobol4/scrip_cc.h"      /* EXPR_t — needed for pl_box_builtin */
#include "frontend/prolog/prolog_builtin.h" /* interp_exec_pl_builtin */

/*----------------------------------------------------------------------------------------------------------------------
 * Leaf box constructors (U-8: return bb_node_t, was Pl_GoalBox)
 * U-11: pl_exec_goal removed — callers use bb_broker(root, BB_ONCE, NULL, NULL) directly.
 *--------------------------------------------------------------------------------------------------------------------*/

/* pl_box_true — γ on α, ω on β (succeed exactly once) */
bb_node_t pl_box_true(void);

/* pl_box_fail — ω on α and β (always fail) */
bb_node_t pl_box_fail(void);

/* pl_box_builtin — α calls interp_exec_pl_builtin(goal, env); β returns ω */
bb_node_t pl_box_builtin(EXPR_t *goal, Term **env);

/*----------------------------------------------------------------------------------------------------------------------
 * CAT box constructors
 *--------------------------------------------------------------------------------------------------------------------*/

/* pl_box_cat — AND-box: left γ → right α; right ω → left β; mirrors bb_seq */
bb_node_t pl_box_cat(bb_node_t left, bb_node_t right);

/* pl_box_goal_from_ir — build a bb_node_t for any goal IR node (E_FNC, E_UNIFY, E_CUT, ...) */
bb_node_t pl_box_goal_from_ir(EXPR_t *g, Term **env);

/* pl_box_cat_list — fold goals[0..n-1] into a left-associative CAT chain. */
bb_node_t pl_box_cat_list(bb_node_t *goals, int n);

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_clause: one Horn clause as a Byrd box
 *--------------------------------------------------------------------------------------------------------------------*/
bb_node_t pl_box_clause(EXPR_t *ec, Term **caller_args, int arity);

/*----------------------------------------------------------------------------------------------------------------------
 * OR-box constructors
 *--------------------------------------------------------------------------------------------------------------------*/

/* pl_box_choice — OR-box over all E_CLAUSE children of an E_CHOICE node. */
bb_node_t pl_box_choice(EXPR_t *choice_node, Term **caller_args, int arity);

/* pl_box_choice_call — build an OR-box for an E_FNC user-predicate call goal. */
bb_node_t pl_box_choice_call(EXPR_t *goal, Term **env);

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_cut: FENCE analog for Prolog cut
 * α: set g_pl_cut_flag=1, return γ.  β: return ω.
 *--------------------------------------------------------------------------------------------------------------------*/
bb_node_t pl_box_cut(void);

#endif /* PL_BROKER_H */
