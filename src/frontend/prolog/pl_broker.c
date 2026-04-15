/*======================================================================================================================
 * pl_broker.c — Prolog Byrd Box Broker  (GOAL-PROLOG-BB-BYRD)
 *
 * S-BB-1: bb_node_t (in pl_broker.h) + pl_exec_goal().
 *         Leaf boxes (pl_box_true/fail/builtin) added S-BB-2.
 *         CAT / OR / CUT boxes added S-BB-3 through S-BB-6.
 *         Routing real interp_eval calls through broker: S-BB-7/S-BB-8.
 *         U-11: pl_exec_goal removed — callers use bb_broker(BB_ONCE) directly.
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
#include "frontend/prolog/pl_interp.h"   /* g_pl_trail, pl_env_new, pl_unified_term_from_expr */

/*----------------------------------------------------------------------------------------------------------------------
 * Internal γ sentinel — NULVCL (non-fail DESCR_t); U-5: bb_box_fn now returns DESCR_t.
 * ω is FAILDESCR.  spec_t is no longer used by Prolog boxes.
 *--------------------------------------------------------------------------------------------------------------------*/
static inline DESCR_t pl_gamma(void) { return NULVCL; }

/*======================================================================================================================
 * S-BB-2 — Leaf boxes
 *====================================================================================================================*/

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_true — γ on α, ω on β  (succeed exactly once, no retry)
 * State: eps_t (one 'done' int) — reuses fence_t discipline.
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct { int fired; } pl_true_t;

static DESCR_t pl_true_fn(void *zeta, int entry) {
    pl_true_t *ζ = zeta;
    if (entry == α) { ζ->fired = 1; return NULVCL; }
    return FAILDESCR;   /* β → ω */
}

bb_node_t pl_box_true(void) {
    pl_true_t *ζ = calloc(1, sizeof(pl_true_t));
    return (bb_node_t){ pl_true_fn, ζ, 0 };
}

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_fail — ω on α and β  (always fail, no solution)
 *--------------------------------------------------------------------------------------------------------------------*/
static DESCR_t pl_fail_fn(void *zeta, int entry) {
    (void)zeta; (void)entry;
    return FAILDESCR;
}

bb_node_t pl_box_fail(void) {
    return (bb_node_t){ pl_fail_fn, NULL, 0 };
}

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_builtin — α calls interp_exec_pl_builtin(); β returns ω
 * Builtins are deterministic (succeed or fail once; no retry).
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct { EXPR_t *goal; Term **env; } pl_builtin_t;

static DESCR_t pl_builtin_fn(void *zeta, int entry) {
    pl_builtin_t *ζ = zeta;
    if (entry == α) {
        int ok = interp_exec_pl_builtin(ζ->goal, ζ->env);
        return ok ? NULVCL : FAILDESCR;
    }
    return FAILDESCR;   /* β → ω: builtins have no retry */
}

bb_node_t pl_box_builtin(EXPR_t *goal, Term **env) {
    pl_builtin_t *ζ = malloc(sizeof(pl_builtin_t));
    ζ->goal = goal;
    ζ->env  = env;
    return (bb_node_t){ pl_builtin_fn, ζ, 0 };
}

/*======================================================================================================================
 * S-BB-3 — pl_box_cat (conjunction / AND-box)
 *
 * Sequences two sub-goals left then right with full backtracking.
 * Mirrors bb_seq() in bb_boxes.c but without positional spec_cat:
 *   γ is pl_gamma() — a non-empty sentinel; extent is ignored.
 *
 * Protocol (matches bb_seq discipline):
 *   α:  call left(α)
 *       left γ  → call right(α)
 *           right γ  → return γ
 *           right ω  → call left(β), retry right from α
 *       left ω  → return ω
 *   β:  call right(β)
 *       right γ  → return γ
 *       right ω  → call left(β)
 *           left γ  → call right(α), loop
 *           left ω  → return ω
 *====================================================================================================================*/
typedef struct { bb_node_t left; bb_node_t right; } pl_cat_t;

static DESCR_t pl_cat_fn(void *zeta, int entry) {
    pl_cat_t *ζ = zeta;
    DESCR_t lr, rr;

    if (entry == α)                                 goto CAT_α;
    /* entry == β */                                goto CAT_β;

CAT_α:  lr = ζ->left.fn(ζ->left.ζ, α);
        if (IS_FAIL_fn(lr))                      goto left_ω;
                                                    goto left_γ;
CAT_β:  rr = ζ->right.fn(ζ->right.ζ, β);
        if (IS_FAIL_fn(rr))                      goto right_ω;
                                                    goto right_γ;
left_γ: rr = ζ->right.fn(ζ->right.ζ, α);
        if (IS_FAIL_fn(rr))                      goto right_ω;
                                                    goto right_γ;
left_ω:                                             return FAILDESCR;
right_γ:                                            return NULVCL;
right_ω: lr = ζ->left.fn(ζ->left.ζ, β);
        if (IS_FAIL_fn(lr))                      goto left_ω;
                                                    goto left_γ;
}

/* pl_box_cat — build a CAT box sequencing left then right */
bb_node_t pl_box_cat(bb_node_t left, bb_node_t right) {
    pl_cat_t *ζ = malloc(sizeof(pl_cat_t));
    ζ->left  = left;
    ζ->right = right;
    return (bb_node_t){ pl_cat_fn, ζ, 0 };
}

/* pl_box_cat_list — fold a GoalBox array left-to-right into a CAT chain.
 * n==0 → true (empty conjunction), n==1 → goals[0], n>=2 → nested CATs. */
bb_node_t pl_box_cat_list(bb_node_t *goals, int n) {
    if (n == 0) return pl_box_true();
    if (n == 1) return goals[0];
    /* fold left: cat(cat(cat(g0,g1),g2),...) */
    bb_node_t acc = pl_box_cat(goals[0], goals[1]);
    for (int i = 2; i < n; i++) acc = pl_box_cat(acc, goals[i]);
    return acc;
}

/*======================================================================================================================
 * S-BB-4 — pl_box_clause (one Horn clause as a Byrd box)
 *
 * Given one E_CLAUSE node and a caller arg-term array (caller_args[0..arity-1]),
 * builds:
 *   head_unify_box  CAT  body_goal_box_0  CAT  ...  CAT  body_goal_box_n
 *
 * E_CLAUSE layout (from prolog_lower.c):
 *   ec->ival        = n_vars  (EnvLayout.n_vars)
 *   ec->dval        = (double)arity
 *   ec->children[0..arity-1]   = head argument IR terms
 *   ec->children[arity..]      = body goal IR nodes
 *
 * Head-unify box:
 *   α: allocate fresh cenv = pl_env_new(n_vars)
 *      for each head arg i: unify(caller_args[i], pl_unified_term_from_expr(head_i, cenv))
 *      on success: store cenv in ζ, return γ
 *      on fail:    trail_unwind, free cenv, return ω
 *   β: trail_unwind to ζ->head_mark; free ζ->cenv; return ω
 *      (head unification is not re-entrant — β always fails)
 *
 * Body goal boxes:
 *   E_FNC builtin  → pl_box_builtin(goal, cenv)     [cenv captured at head-unify time]
 *   E_FNC user call → pl_box_choice_call(goal, cenv) [S-BB-5: forward-declared below]
 *   E_UNIFY        → pl_box_unify(goal, cenv)
 *   E_CUT          → pl_box_cut()
 *   others         → pl_box_true() (safe no-op; refined in later steps)
 *
 * The head box and body boxes are chained with pl_box_cat_list.
 *====================================================================================================================*/

/* Forward declaration for S-BB-5 user-call box (not yet implemented) */
bb_node_t pl_box_choice_call(EXPR_t *goal, Term **env);   /* defined in S-BB-5 */

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_unify — E_UNIFY leaf box: α unifies children[0] and children[1] via trail; β undoes.
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct { EXPR_t *node; Term **env; int mark; int fired; } pl_unify_t;

static DESCR_t pl_unify_fn(void *zeta, int entry) {
    pl_unify_t *ζ = zeta;
    if (entry == α) {
        ζ->mark  = trail_mark(&g_pl_trail);
        ζ->fired = 0;
        if (!ζ->node || ζ->node->nchildren < 2) return FAILDESCR;
        Term *t1 = pl_unified_term_from_expr(ζ->node->children[0], ζ->env);
        Term *t2 = pl_unified_term_from_expr(ζ->node->children[1], ζ->env);
        if (!unify(t1, t2, &g_pl_trail)) { trail_unwind(&g_pl_trail, ζ->mark); return FAILDESCR; }
        ζ->fired = 1;
        return NULVCL;
    }
    /* β: undo unification — not re-entrant */
    if (ζ->fired) { trail_unwind(&g_pl_trail, ζ->mark); ζ->fired = 0; }
    return FAILDESCR;
}

static bb_node_t pl_box_unify(EXPR_t *node, Term **env) {
    pl_unify_t *ζ = calloc(1, sizeof(pl_unify_t));
    ζ->node = node; ζ->env = env;
    return (bb_node_t){ pl_unify_fn, ζ, 0 };
}

/*----------------------------------------------------------------------------------------------------------------------
 * Head-unify box state and function
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct {
    Term   **caller_args;   /* caller's arg array (caller owns — we do not free) */
    int      arity;
    EXPR_t  *ec;            /* the E_CLAUSE node (head args live in children[0..arity-1]) */
    Term   **cenv;          /* allocated on α, freed on β */
    int      n_vars;
    int      head_mark;     /* trail mark saved before head unification */
    int      fired;
} pl_head_unify_t;

static DESCR_t pl_head_unify_fn(void *zeta, int entry) {
    pl_head_unify_t *ζ = zeta;
    if (entry == α) {
        ζ->head_mark = trail_mark(&g_pl_trail);
        ζ->cenv      = pl_env_new(ζ->n_vars);
        ζ->fired     = 0;
        for (int i = 0; i < ζ->arity && i < ζ->ec->nchildren; i++) {
            Term *ha = pl_unified_term_from_expr(ζ->ec->children[i], ζ->cenv);
            Term *ca = (ζ->caller_args && i < ζ->arity) ? ζ->caller_args[i] : term_new_var(i);
            if (!unify(ca, ha, &g_pl_trail)) {
                trail_unwind(&g_pl_trail, ζ->head_mark);
                if (ζ->cenv) { free(ζ->cenv); ζ->cenv = NULL; }
                return FAILDESCR;
            }
        }
        ζ->fired = 1;
        return NULVCL;
    }
    /* β: head unification is not re-entrant */
    if (ζ->fired) {
        trail_unwind(&g_pl_trail, ζ->head_mark);
        if (ζ->cenv) { free(ζ->cenv); ζ->cenv = NULL; }
        ζ->fired = 0;
    }
    return FAILDESCR;
}

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_goal_from_ir — build a bb_node_t for one IR body goal node.
 * env is the clause's cenv (populated after head unification succeeds).
 * For user calls (S-BB-5), we forward to pl_box_choice_call.
 *--------------------------------------------------------------------------------------------------------------------*/
static int pl_is_builtin_goal(EXPR_t *g);                              /* forward */
static bb_node_t pl_box_alt(bb_node_t left, bb_node_t right);       /* forward */

bb_node_t pl_box_goal_from_ir(EXPR_t *g, Term **env) {
    if (!g) return pl_box_true();
    if (g->kind == E_CUT)   return pl_box_cut();
    if (g->kind == E_UNIFY) return pl_box_unify(g, env);
    if (g->kind == E_FNC) {
        if (g->sval && strcmp(g->sval, "true") == 0) return pl_box_true();
        if (g->sval && strcmp(g->sval, "fail") == 0) return pl_box_fail();
        /* `,/N` — conjunction: lowerer produces n-ary node; fold into CAT chain */
        if (g->sval && strcmp(g->sval, ",") == 0 && g->nchildren >= 2) {
            bb_node_t *goals = malloc(g->nchildren * sizeof(bb_node_t));
            for (int i = 0; i < g->nchildren; i++)
                goals[i] = pl_box_goal_from_ir(g->children[i], env);
            bb_node_t result = pl_box_cat_list(goals, g->nchildren);
            free(goals);
            return result;
        }
        /* `;/N` — disjunction or if-then-else; lowerer may produce n-ary node */
        if (g->sval && strcmp(g->sval, ";") == 0 && g->nchildren >= 2) {
            /* if-then-else: first child is (Cond -> Then), second is Else */
            EXPR_t *lc = g->children[0];
            if (lc && lc->kind == E_FNC && lc->sval &&
                strcmp(lc->sval, "->") == 0 && lc->nchildren >= 2) {
                /* Build Cond CAT Then from n-ary -> node */
                bb_node_t *then_goals = malloc(lc->nchildren * sizeof(bb_node_t));
                for (int i = 0; i < lc->nchildren; i++)
                    then_goals[i] = pl_box_goal_from_ir(lc->children[i], env);
                bb_node_t ite_left = pl_box_cat_list(then_goals, lc->nchildren);
                free(then_goals);
                bb_node_t ite_right = pl_box_goal_from_ir(g->children[1], env);
                return pl_box_alt(ite_left, ite_right);
            }
            /* Plain disjunction: fold right into ALT chain */
            bb_node_t acc = pl_box_goal_from_ir(g->children[g->nchildren - 1], env);
            for (int i = g->nchildren - 2; i >= 0; i--)
                acc = pl_box_alt(pl_box_goal_from_ir(g->children[i], env), acc);
            return acc;
        }
        /* `->/N` standalone if-then: CAT chain of all children */
        if (g->sval && strcmp(g->sval, "->") == 0 && g->nchildren >= 2) {
            bb_node_t *goals = malloc(g->nchildren * sizeof(bb_node_t));
            for (int i = 0; i < g->nchildren; i++)
                goals[i] = pl_box_goal_from_ir(g->children[i], env);
            bb_node_t result = pl_box_cat_list(goals, g->nchildren);
            free(goals);
            return result;
        }
        if (pl_is_builtin_goal(g)) return pl_box_builtin(g, env);
        return pl_box_choice_call(g, env);   /* user predicate — S-BB-5 */
    }
    return pl_box_true();   /* safe fallback for unrecognised node kinds */
}

/* pl_is_builtin_goal — mirrors is_pl_user_call() logic (inverse) */
static int pl_is_builtin_goal(EXPR_t *g) {
    if (!g || g->kind != E_FNC || !g->sval) return 0;
    static const char *builtins[] = {
        "true","fail","halt","nl","write","writeln","print","writeq","write_canonical","tab","is",
        "<",">","=<",">=","=:=","=\\=","=","\\=","==","\\==",
        "@<","@>","@=<","@>=",
        "var","nonvar","atom","integer","float","compound","atomic","callable","is_list",
        "functor","arg","=..","\\+","not","findall",
        "assert","assertz","asserta","retract","retractall","abolish",
        "atom_length","atom_concat","atom_chars","atom_codes",
        "sort","msort","compare","@<","@>","@=<","@>=",
        "succ","plus","format",
        "numbervars","char_type",
        "nv_get","nv_set",
        "term_string","number_codes","number_chars","char_code","upcase_atom","downcase_atom",
        "copy_term","atomic_list_concat","concat_atom","string_to_atom",
        "nb_setval","nb_getval","aggregate_all","throw","catch",
        "phrase",
        NULL
    };
    for (int i = 0; builtins[i]; i++)
        if (strcmp(g->sval, builtins[i]) == 0) return 1;
    return 0;
}

/*----------------------------------------------------------------------------------------------------------------------
/* Forward declaration — defined after pl_is_builtin_goal */
bb_node_t pl_box_goal_from_ir(EXPR_t *g, Term **env);  /* forward */

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_deferred_env — wraps a body goal box whose env is not yet known at
 * construction time.  env_ptr points to a Term** that will be filled in by
 * the head-unify box α before this box is ever called.
 * On each call, rebuilds the inner box from the IR node using *env_ptr,
 * then delegates to it.  This is correct because pl_box_clause guarantees
 * the CAT chain calls head_box α before any body box.
 *--------------------------------------------------------------------------------------------------------------------*/
typedef struct {
    EXPR_t    *goal_node;   /* IR node for this body goal */
    Term    ***env_ptr;     /* &hζ->cenv — filled in by head-unify α */
    bb_node_t inner;       /* built lazily on first α */
    int        built;
} pl_deferred_env_t;

static DESCR_t pl_deferred_env_fn(void *zeta, int entry) {
    pl_deferred_env_t *ζ = zeta;
    if (entry == α || !ζ->built) {
        /* (re)build inner box using the now-valid *env_ptr */
        ζ->inner = pl_box_goal_from_ir(ζ->goal_node, *ζ->env_ptr);
        ζ->built  = 1;
        return ζ->inner.fn(ζ->inner.ζ, α);
    }
    return ζ->inner.fn(ζ->inner.ζ, β);
}

static bb_node_t pl_box_deferred_env(EXPR_t *goal_node, Term ***env_ptr) {
    pl_deferred_env_t *ζ = calloc(1, sizeof(pl_deferred_env_t));
    ζ->goal_node = goal_node;
    ζ->env_ptr   = env_ptr;
    return (bb_node_t){ pl_deferred_env_fn, ζ, 0 };
}

/*----------------------------------------------------------------------------------------------------------------------
 * pl_box_clause — the public constructor (S-BB-4, cenv timing fixed)
 *
 * Builds: head_unify_box CAT body_goal_0 CAT ... CAT body_goal_n
 * Body goal boxes use pl_box_deferred_env so they resolve hζ->cenv at
 * call time (after head-unify α has allocated and populated cenv).
 *--------------------------------------------------------------------------------------------------------------------*/
bb_node_t pl_box_clause(EXPR_t *ec, Term **caller_args, int arity) {
    if (!ec || ec->kind != E_CLAUSE) return pl_box_fail();
    int n_vars = (int)ec->ival;
    int nbody  = ec->nchildren - arity;
    if (nbody < 0) nbody = 0;

    pl_head_unify_t *hζ = calloc(1, sizeof(pl_head_unify_t));
    hζ->caller_args = caller_args;
    hζ->arity       = arity;
    hζ->ec          = ec;
    hζ->n_vars      = n_vars;
    bb_node_t head_box = (bb_node_t){ pl_head_unify_fn, hζ, 0 };

    if (nbody == 0) return head_box;   /* fact: head only */

    /* Body boxes use &hζ->cenv so they pick up cenv after head-unify α fires */
    int total = 1 + nbody;
    bb_node_t *boxes = malloc(total * sizeof(bb_node_t));
    boxes[0] = head_box;
    for (int i = 0; i < nbody; i++)
        boxes[1 + i] = pl_box_deferred_env(ec->children[arity + i], &hζ->cenv);
    bb_node_t result = pl_box_cat_list(boxes, total);
    free(boxes);
    return result;
}

/*======================================================================================================================
 * S-BB-6 — pl_box_cut (FENCE analog for Prolog cut)
 *
 * α: set g_pl_cut_flag = 1; return γ
 * β: return ω — no backtrack past cut, matching FENCE discipline in stmt_exec.c
 *====================================================================================================================*/
static DESCR_t pl_cut_fn(void *zeta, int entry) {
    (void)zeta;
    if (entry == α) { g_pl_cut_flag = 1; return NULVCL; }
    return FAILDESCR;
}

bb_node_t pl_box_cut(void) {
    return (bb_node_t){ pl_cut_fn, NULL, 0 };
}

/*======================================================================================================================
 * pl_box_alt — disjunction (;/2) box: try left, on ω try right (once each)
 *
 * α: try left(α); if γ → return γ; else try right(α).
 * β: left already done — try right(β); if γ → return γ; else ω.
 * This gives ; the same semantics as bb_alt in stmt_exec.c.
 *====================================================================================================================*/
typedef struct { bb_node_t left; bb_node_t right; int phase; } pl_alt_zeta_t;
/* phase: 0=left not tried, 1=left failed try right α, 2=right active */

static DESCR_t pl_alt_fn(void *zeta, int entry) {
    pl_alt_zeta_t *ζ = (pl_alt_zeta_t *)zeta;
    DESCR_t r;
    if (entry == α) {
        r = ζ->left.fn(ζ->left.ζ, α);
        if (!IS_FAIL_fn(r)) { ζ->phase = 0; return r; }   /* left γ */
        /* left ω — try right */
        ζ->phase = 2;
        return ζ->right.fn(ζ->right.ζ, α);
    }
    /* β: retry whichever branch is active */
    if (ζ->phase == 0) {
        /* left is still active — retry it */
        r = ζ->left.fn(ζ->left.ζ, β);
        if (!IS_FAIL_fn(r)) return r;
        /* left exhausted — fall through to right */
        ζ->phase = 2;
        return ζ->right.fn(ζ->right.ζ, α);
    }
    /* right is active */
    return ζ->right.fn(ζ->right.ζ, β);
}

static bb_node_t pl_box_alt(bb_node_t left, bb_node_t right) {
    pl_alt_zeta_t *ζ = malloc(sizeof(pl_alt_zeta_t));
    ζ->left = left; ζ->right = right; ζ->phase = 0;
    return (bb_node_t){ pl_alt_fn, ζ, 0 };
}

/*======================================================================================================================
 * pl_box_seq — conjunction (,/2) as a CAT-box alias
 * Conjunctions in the body are already handled by pl_box_cat_list in pl_box_clause,
 * but `,/2` appearing as an E_FNC goal node needs this explicit builder.
 *====================================================================================================================*/
/*======================================================================================================================
 * S-BB-5 — pl_box_choice and pl_box_choice_call (OR-box over predicate clauses)
 *
 * OR-box over all E_CLAUSE children of one E_CHOICE IR node.
 * State: { clauses[], nclause, ci, trail_mark, cur clause box }
 *   α: trail_mark(); try clause[0](α).
 *   β: retry cur clause(β); on ω → trail_unwind, ci++, try clause[ci](α).
 *   ci == nclause or g_pl_cut_flag: trail_unwind; return ω.
 *====================================================================================================================*/
typedef struct {
    EXPR_t    **clauses;
    int         nclause;
    int         ci;
    int         trail_mark;
    Term      **caller_args;
    int         arity;
    bb_node_t  cur;
    int         cur_active;
} pl_choice_t;

static DESCR_t pl_choice_fn(void *zeta, int entry) {
    pl_choice_t *ζ = zeta;
    DESCR_t r;

    if (entry == β && ζ->cur_active) {
        r = ζ->cur.fn(ζ->cur.ζ, β);
        if (!IS_FAIL_fn(r)) return NULVCL;
        trail_unwind(&g_pl_trail, ζ->trail_mark);
        ζ->ci++;
        ζ->cur_active = 0;
    } else if (entry == α) {
        ζ->trail_mark = trail_mark(&g_pl_trail);
        ζ->ci         = 0;
        ζ->cur_active = 0;
    }

    while (ζ->ci < ζ->nclause && !g_pl_cut_flag) {
        EXPR_t *ec = ζ->clauses[ζ->ci];
        if (!ec || ec->kind != E_CLAUSE) { ζ->ci++; continue; }
        trail_unwind(&g_pl_trail, ζ->trail_mark);
        ζ->cur        = pl_box_clause(ec, ζ->caller_args, ζ->arity);
        ζ->cur_active = 1;
        r = ζ->cur.fn(ζ->cur.ζ, α);
        if (!IS_FAIL_fn(r)) return NULVCL;
        ζ->ci++;
        ζ->cur_active = 0;
    }

    trail_unwind(&g_pl_trail, ζ->trail_mark);
    return FAILDESCR;
}

bb_node_t pl_box_choice(EXPR_t *choice_node, Term **caller_args, int arity) {
    if (!choice_node || choice_node->kind != E_CHOICE) return pl_box_fail();
    pl_choice_t *ζ = calloc(1, sizeof(pl_choice_t));
    ζ->clauses     = choice_node->children;
    ζ->nclause     = choice_node->nchildren;
    ζ->caller_args = caller_args;
    ζ->arity       = arity;
    return (bb_node_t){ pl_choice_fn, ζ, 0 };
}

bb_node_t pl_box_choice_call(EXPR_t *goal, Term **env) {
    if (!goal || !goal->sval) return pl_box_fail();
    int arity = goal->nchildren;
    char key[256];
    snprintf(key, sizeof key, "%s/%d", goal->sval, arity);
    EXPR_t *choice = pl_pred_table_lookup_global(key);
    if (!choice) return pl_box_fail();
    Term **caller_args = arity ? malloc(arity * sizeof(Term *)) : NULL;
    /* Wildcard fix (PL-10): anonymous _ has E_VAR ival==-1.
     * pl_unified_term_from_expr returns term_new_var(-1) for these, but
     * bind() skips trail_push for slot==-1, so trail_unwind() cannot reset
     * the binding between clauses and the OR-box stops after clause 1.
     * Fix: give each wildcard a unique positive slot so bind() trails it
     * and trail_unwind() resets it properly on each retry. */
    static int g_wildcard_slot = 100000;
    for (int i = 0; i < arity; i++) {
        EXPR_t *ch = goal->children[i];
        if (ch && ch->kind == E_VAR && (int)ch->ival == -1)
            caller_args[i] = term_new_var(g_wildcard_slot++);
        else
            caller_args[i] = pl_unified_term_from_expr(ch, env);
    }
    return pl_box_choice(choice, caller_args, arity);
}
