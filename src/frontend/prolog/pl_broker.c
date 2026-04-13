/*======================================================================================================================
 * pl_broker.c — Prolog Byrd Box Broker  (GOAL-PROLOG-BB-BYRD)
 *
 * S-BB-1: Pl_GoalBox (in pl_broker.h) + pl_exec_goal().
 *         Leaf boxes (pl_box_true/fail/builtin) added S-BB-2.
 *         CAT / OR / CUT boxes added S-BB-3 through S-BB-6.
 *         Routing real interp_eval calls through broker: S-BB-7/S-BB-8.
 *
 * Reuses bb_box_fn ABI from bb_box.h unchanged:
 *   entry α (0): first call  — attempt goal
 *   entry β (1): retry call  — next solution or ω
 *   spec_is_empty(result) → ω (failure)
 *   !spec_is_empty(result) → γ (success)
 *
 * Prolog boxes do not use the positional extent of spec_t (σ/δ).
 * γ is signalled by returning pl_gamma() — a non-empty sentinel.
 * Retry responsibility belongs to the OR-box (S-BB-5), not the broker.
 *====================================================================================================================*/

#include "frontend/prolog/pl_broker.h"

/*----------------------------------------------------------------------------------------------------------------------
 * Internal γ sentinel — non-empty spec; extent ignored by all Prolog boxes.
 * σ points at a static byte so it is never NULL (spec_is_empty checks σ==NULL).
 *--------------------------------------------------------------------------------------------------------------------*/
static const char s_pl_gamma_byte = '\0';
static inline spec_t pl_gamma(void) { return spec(&s_pl_gamma_byte, 1); }

/*======================================================================================================================
 * pl_exec_goal — top-level broker entry point  (S-BB-1)
 *
 * Calls root.fn(root.zeta, α).
 * Returns 1 on γ (goal succeeded), 0 on ω (goal failed).
 *
 * No scan loop — Prolog is not positional.  Retry is the OR-box's job (S-BB-5).
 *====================================================================================================================*/
int pl_exec_goal(Pl_GoalBox root) {
    if (pl_goalbox_is_null(root)) return 0;
    spec_t r = root.fn(root.zeta, α);
    return !spec_is_empty(r);
}

/*======================================================================================================================
 * S-BB-2 — Leaf boxes
 *====================================================================================================================*/

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_true — γ on α, ω on β  (succeed exactly once, no retry)
 * State: eps_t (one 'done' int) — reuses fence_t discipline.
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct { int fired; } pl_true_t;

static spec_t pl_true_fn(void *zeta, int entry) {
    pl_true_t *ζ = zeta;
    if (entry == α) { ζ->fired = 1; return spec(&s_pl_gamma_byte, 1); }
    return spec_empty;   /* β → ω */
}

Pl_GoalBox pl_box_true(void) {
    pl_true_t *ζ = calloc(1, sizeof(pl_true_t));
    return (Pl_GoalBox){ pl_true_fn, ζ };
}

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_fail — ω on α and β  (always fail, no solution)
 *--------------------------------------------------------------------------------------------------------------------*/
static spec_t pl_fail_fn(void *zeta, int entry) {
    (void)zeta; (void)entry;
    return spec_empty;
}

Pl_GoalBox pl_box_fail(void) {
    return (Pl_GoalBox){ pl_fail_fn, NULL };
}

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_builtin — α calls interp_exec_pl_builtin(); β returns ω
 * Builtins are deterministic (succeed or fail once; no retry).
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct { EXPR_t *goal; Term **env; } pl_builtin_t;

static spec_t pl_builtin_fn(void *zeta, int entry) {
    pl_builtin_t *ζ = zeta;
    if (entry == α) {
        int ok = interp_exec_pl_builtin(ζ->goal, ζ->env);
        return ok ? spec(&s_pl_gamma_byte, 1) : spec_empty;
    }
    return spec_empty;   /* β → ω: builtins have no retry */
}

Pl_GoalBox pl_box_builtin(EXPR_t *goal, Term **env) {
    pl_builtin_t *ζ = malloc(sizeof(pl_builtin_t));
    ζ->goal = goal;
    ζ->env  = env;
    return (Pl_GoalBox){ pl_builtin_fn, ζ };
}
