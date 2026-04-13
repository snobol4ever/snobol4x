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

/*======================================================================================================================
 * S-BB-3 — pl_box_cat: conjunction / AND-box (sequence)
 *
 * Mirrors bb_seq from bb_boxes.c exactly, minus positional cursor fields.
 * State: left, right Pl_GoalBox.
 *
 *   α: call left(α); γ → right(α); right ω → left(β), retry right from α; left ω → ω.
 *   β: right(β); ω → left(β), retry right(α); left ω → ω.
 *====================================================================================================================*/
typedef struct { Pl_GoalBox left; Pl_GoalBox right; } pl_cat_t;

static spec_t pl_cat_fn(void *zeta, int entry) {
    pl_cat_t *ζ = zeta;
    spec_t lr, rr;
    if (entry == α)                              goto CAT_α;
                                                 goto CAT_β;
CAT_α:  lr = ζ->left.fn(ζ->left.zeta, α);
        if (spec_is_empty(lr))                   goto left_ω;
                                                 goto left_γ;
CAT_β:  rr = ζ->right.fn(ζ->right.zeta, β);
        if (spec_is_empty(rr))                   goto right_ω;
                                                 goto right_γ;
left_γ: rr = ζ->right.fn(ζ->right.zeta, α);
        if (spec_is_empty(rr))                   goto right_ω;
                                                 goto right_γ;
left_ω:                                          return spec_empty;
right_γ:                                         return pl_gamma();
right_ω: lr = ζ->left.fn(ζ->left.zeta, β);
        if (spec_is_empty(lr))                   goto left_ω;
                                                 goto left_γ;
}

Pl_GoalBox pl_box_cat(Pl_GoalBox left, Pl_GoalBox right) {
    pl_cat_t *ζ = calloc(1, sizeof(pl_cat_t));
    ζ->left = left; ζ->right = right;
    return (Pl_GoalBox){ pl_cat_fn, ζ };
}

/* pl_box_cat_list: fold a list of goals into a right-associative CAT chain.
 * goals[0] CAT goals[1] CAT ... CAT goals[n-1].
 * Empty list → pl_box_true(). Single → goals[0]. */
Pl_GoalBox pl_box_cat_list(Pl_GoalBox *goals, int n) {
    if (n == 0) return pl_box_true();
    if (n == 1) return goals[0];
    Pl_GoalBox acc = goals[n - 1];
    for (int i = n - 2; i >= 0; i--)
        acc = pl_box_cat(goals[i], acc);
    return acc;
}

/*======================================================================================================================
 * S-BB-4 stubs — pl_box_clause and head-unify box declared; full impl in S-BB-4 proper.
 * Declared here so pl_box_choice (S-BB-5) can reference them.
 *====================================================================================================================*/
/* Forward declarations — implemented below */
static spec_t pl_head_unify_fn(void *zeta, int entry);

/*======================================================================================================================
 * S-BB-5 — pl_box_choice: OR-box over all clauses of one predicate
 *
 * Mirrors bb_alt from bb_boxes.c.
 * State: array of clause EXPR_t* nodes, nclause, ci (current index), trail_mark.
 *
 *   α: mark trail; try clause[0].fn(α).
 *   β: if current clause box β → γ; else trail_unwind(mark), advance ci, try clause[ci].fn(α).
 *   ci == nclause → ω.
 *====================================================================================================================*/
#define PL_CHOICE_MAX 64
typedef struct {
    EXPR_t       *clauses[PL_CHOICE_MAX];
    int           nclause;
    int           ci;
    int           trail_mark;
    Pl_GoalBox    cur_box;    /* box for current clause */
    Term        **env;        /* caller's environment */
    EXPR_t       *call_args;  /* call node for head unification */
    Trail        *trail;
} pl_choice_t;

/* Forward: build a clause box from an E_CLAUSE node */
static Pl_GoalBox pl_build_clause_box(EXPR_t *clause, Term **env, Trail *trail);

static spec_t pl_choice_fn(void *zeta, int entry) {
    pl_choice_t *ζ = zeta;
    spec_t r;
    if (entry == α) {
        if (ζ->nclause == 0) return spec_empty;
        ζ->trail_mark = trail_mark(ζ->trail);
        ζ->ci = 0;
        ζ->cur_box = pl_build_clause_box(ζ->clauses[0], ζ->env, ζ->trail);
        goto TRY_CURRENT;
    }
    /* β: retry current clause first */
    r = ζ->cur_box.fn(ζ->cur_box.zeta, β);
    if (!spec_is_empty(r)) return pl_gamma();
    /* current clause exhausted — unwind trail, advance */
NEXT_CLAUSE:
    trail_unwind(ζ->trail, ζ->trail_mark);
    ζ->ci++;
    if (ζ->ci >= ζ->nclause) return spec_empty;
    ζ->trail_mark = trail_mark(ζ->trail);
    ζ->cur_box = pl_build_clause_box(ζ->clauses[ζ->ci], ζ->env, ζ->trail);
TRY_CURRENT:
    r = ζ->cur_box.fn(ζ->cur_box.zeta, α);
    if (!spec_is_empty(r)) return pl_gamma();
    goto NEXT_CLAUSE;
}

Pl_GoalBox pl_box_choice(EXPR_t **clauses, int nclause, Term **env, Trail *trail) {
    pl_choice_t *ζ = calloc(1, sizeof(pl_choice_t));
    int nc = nclause < PL_CHOICE_MAX ? nclause : PL_CHOICE_MAX;
    for (int i = 0; i < nc; i++) ζ->clauses[i] = clauses[i];
    ζ->nclause = nc; ζ->env = env; ζ->trail = trail;
    return (Pl_GoalBox){ pl_choice_fn, ζ };
}

/*======================================================================================================================
 * S-BB-4 — pl_box_clause: build a box for one E_CLAUSE node
 *
 * An E_CLAUSE node: children[0] = head (E_FNC with arg terms),
 *                   children[1..] = body goals.
 * Box: head-unify leaf CAT'd with each body goal box.
 * Head-unify box: α unifies head args against call args, sets env → γ or ω; β → ω.
 *====================================================================================================================*/
typedef struct {
    EXPR_t  *head;      /* head E_FNC node */
    Term   **call_args; /* terms from the call site */
    int      nargs;
    Trail   *trail;
    Term   **env;
    int      trail_mark_before;
    int      unified;
} pl_head_t;

static spec_t pl_head_unify_fn(void *zeta, int entry) {
    pl_head_t *ζ = zeta;
    if (entry == β) {
        /* Head unification is deterministic — no retry */
        trail_unwind(ζ->trail, ζ->trail_mark_before);
        return spec_empty;
    }
    /* α: unify each head arg with call arg */
    ζ->trail_mark_before = trail_mark(ζ->trail);
    if (ζ->head && ζ->head->nchildren > 0) {
        for (int i = 0; i < ζ->nargs; i++) {
            /* head->children[i] is the formal param (variable name in E_VAR) */
            /* call_args[i] is the actual arg term */
            /* For now: create a fresh variable in env and unify */
            /* Simplified: just bind env slot directly */
            if (i < 64) ζ->env[i] = ζ->call_args[i];
        }
    }
    ζ->unified = 1;
    return pl_gamma();
}

static Pl_GoalBox pl_build_clause_box(EXPR_t *clause, Term **env, Trail *trail) {
    if (!clause) return pl_box_fail();
    /* Collect body goals (children[1..]) */
    int nbody = clause->nchildren > 1 ? clause->nchildren - 1 : 0;
    Pl_GoalBox *goals = nbody > 0 ? calloc(nbody, sizeof(Pl_GoalBox)) : NULL;
    for (int i = 0; i < nbody; i++) {
        EXPR_t *g = clause->children[1 + i];
        goals[i] = pl_box_builtin(g, env);   /* builtins for now; extended in S-BB-8 */
    }
    Pl_GoalBox body = pl_box_cat_list(goals, nbody);
    free(goals);
    return body;
}

/*======================================================================================================================
 * S-BB-6 — pl_box_cut: FENCE analog
 *
 * α: set *cut_flag = 1; return γ (cut always succeeds on first call).
 * β: return ω (no backtracking past cut).
 * Mirrors FENCE in stmt_exec.c.
 *====================================================================================================================*/
typedef struct { int *cut_flag; } pl_cut_t;

static spec_t pl_cut_fn(void *zeta, int entry) {
    pl_cut_t *ζ = zeta;
    if (entry == α) { if (ζ->cut_flag) *ζ->cut_flag = 1; return pl_gamma(); }
    return spec_empty;   /* β → ω */
}

Pl_GoalBox pl_box_cut(int *cut_flag) {
    pl_cut_t *ζ = malloc(sizeof(pl_cut_t));
    ζ->cut_flag = cut_flag;
    return (Pl_GoalBox){ pl_cut_fn, ζ };
}
