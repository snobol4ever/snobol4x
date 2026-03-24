/*
 * prolog_lower.c — Prolog ClauseAST -> sno2c IR lowering
 *
 * Takes a PlProgram (list of PlClause) and produces a Program* whose
 * STMT_t nodes carry the Prolog IR node kinds added to EKind in sno2c.h.
 *
 * Pipeline:
 *   1. Group clauses by functor/arity key -> one E_CHOICE per predicate
 *   2. For each PlClause -> one E_CLAUSE child of the E_CHOICE
 *   3. Lower each Term* in head args and body goals -> EXPR_t*
 *   4. Assign variable slots per clause (VarScope reused from parser)
 *   5. Emit E_TRAIL_MARK / E_TRAIL_UNWIND sentinels around each clause
 *
 * Term -> EXPR_t lowering:
 *   TT_ATOM     -> E_QLIT  (sval = atom name)
 *   TT_INT      -> E_ILIT  (ival = value)
 *   TT_FLOAT    -> E_FLIT  (dval = value)
 *   TT_VAR      -> E_VART  (sval = "_V<slot>", ival = slot)
 *   TT_COMPOUND -> E_FNC   (sval = functor name, children = lowered args)
 *                  special: =/2  -> E_UNIFY
 *                           is/2 -> E_FNC("is") with arithmetic rhs
 *                           !/0  -> E_CUT
 *                           ,/2  -> flattened into body (done in parser)
 */

#include "prolog_lower.h"
#include "prolog_atom.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Functor/arity key for grouping clauses
 * ======================================================================= */
typedef struct {
    int functor;   /* atom_id */
    int arity;
} PredKey;

static int pred_key_eq(PredKey a, PredKey b) {
    return a.functor == b.functor && a.arity == b.arity;
}

/* Extract predicate key from a clause head Term* */
static PredKey key_of_head(Term *head) {
    PredKey k = {-1, 0};
    if (!head) return k;
    head = term_deref(head);
    if (!head) return k;
    if (head->tag == TT_ATOM) {
        k.functor = head->atom_id;
        k.arity   = 0;
    } else if (head->tag == TT_COMPOUND) {
        k.functor = head->compound.functor;
        k.arity   = head->compound.arity;
    }
    return k;
}

/* =========================================================================
 * Term -> EXPR_t lowering
 * ======================================================================= */

static EXPR_t *lower_term(Term *t);

/* Build functor/arity string like "foo/2" for E_CHOICE sval */
static char *pred_str(int functor, int arity) {
    const char *fn = prolog_atom_name(functor);
    if (!fn) fn = "?";
    char buf[256];
    snprintf(buf, sizeof buf, "%s/%d", fn, arity);
    return strdup(buf);
}

static EXPR_t *lower_term(Term *t) {
    t = term_deref(t);
    if (!t) {
        EXPR_t *e = expr_new(E_QLIT);
        e->sval = strdup("[]");
        return e;
    }

    switch (t->tag) {
        case TT_ATOM: {
            /* ! -> E_CUT */
            if (t->atom_id == ATOM_CUT) return expr_new(E_CUT);
            /* Atom in a goal position -> E_FNC/0 (nl, true, fail, halt, etc.) */
            EXPR_t *e = expr_new(E_FNC);
            const char *nm = prolog_atom_name(t->atom_id);
            e->sval = strdup(nm ? nm : "");
            return e;
        }
        case TT_INT: {
            EXPR_t *e = expr_new(E_ILIT);
            e->ival = t->ival;
            return e;
        }
        case TT_FLOAT: {
            EXPR_t *e = expr_new(E_FLIT);
            e->dval = t->fval;
            return e;
        }
        case TT_VAR: {
            EXPR_t *e = expr_new(E_VART);
            int slot = t->saved_slot;  /* -1 = anonymous wildcard */
            char buf[32];
            if (slot < 0) {
                snprintf(buf, sizeof buf, "_anon");
            } else {
                snprintf(buf, sizeof buf, "_V%d", slot);
            }
            e->sval = strdup(buf);
            e->ival = slot;  /* -1 for wildcard, >=0 for real var */
            return e;
        }
        case TT_COMPOUND: {
            const char *fn = prolog_atom_name(t->compound.functor);
            if (!fn) fn = "?";
            int arity = t->compound.arity;

            /* =/2 -> E_UNIFY */
            int eq_id = prolog_atom_intern("=");
            if (t->compound.functor == eq_id && arity == 2) {
                EXPR_t *e = expr_new(E_UNIFY);
                expr_add_child(e, lower_term(t->compound.args[0]));
                expr_add_child(e, lower_term(t->compound.args[1]));
                return e;
            }

            /* Arithmetic operators within is/2 rhs */
            struct { const char *name; EKind kind; } arith[] = {
                { "+", E_ADD }, { "-", E_SUB }, { "*", E_MPY },
                { "/", E_DIV }, { "//", E_DIV }, { NULL, 0 }
            };
            if (arity == 2) {
                for (int i = 0; arith[i].name; i++) {
                    if (strcmp(fn, arith[i].name) == 0) {
                        EXPR_t *e = expr_new(arith[i].kind);
                        expr_add_child(e, lower_term(t->compound.args[0]));
                        expr_add_child(e, lower_term(t->compound.args[1]));
                        return e;
                    }
                }
            }

            /* ,/2 — conjunction: flatten right-spine into n-ary E_FNC(",")
             * e.g. (A,(B,(C,D))) -> E_FNC(",") [A, B, C, D]  */
            int comma_id = prolog_atom_intern(",");
            if (t->compound.functor == comma_id && arity == 2) {
                EXPR_t *e = expr_new(E_FNC);
                e->sval = strdup(",");
                /* walk the right spine collecting all conjuncts */
                Term *cur = t;
                while (cur && cur->tag == TT_COMPOUND &&
                       cur->compound.functor == comma_id &&
                       cur->compound.arity == 2) {
                    expr_add_child(e, lower_term(cur->compound.args[0]));
                    cur = term_deref(cur->compound.args[1]);
                }
                if (cur) expr_add_child(e, lower_term(cur));
                return e;
            }

            /* ;/2 — disjunction: flatten right-spine into n-ary E_FNC(";")
             * e.g. (A;(B;C)) -> E_FNC(";") [A, B, C]  */
            int semi_id = prolog_atom_intern(";");
            if (t->compound.functor == semi_id && arity == 2) {
                EXPR_t *e = expr_new(E_FNC);
                e->sval = strdup(";");
                Term *cur = t;
                while (cur && cur->tag == TT_COMPOUND &&
                       cur->compound.functor == semi_id &&
                       cur->compound.arity == 2) {
                    expr_add_child(e, lower_term(cur->compound.args[0]));
                    cur = term_deref(cur->compound.args[1]);
                }
                if (cur) expr_add_child(e, lower_term(cur));
                return e;
            }

            /* ->/2 — if-then: flatten Then-part into n-ary children.
             * E_FNC("->") layout: children[0] = Cond, children[1..] = Then goals.
             * (Then conjunction flattened, same as E_CLAUSE body goals.)
             * Every then-step is visible at top level for the emitter. */
            int arrow_id = prolog_atom_intern("->");
            if (t->compound.functor == arrow_id && arity == 2) {
                EXPR_t *e = expr_new(E_FNC);
                e->sval = strdup("->");
                /* children[0] = Cond */
                expr_add_child(e, lower_term(t->compound.args[0]));
                /* children[1..] = Then goals (flatten right-spine conjunction) */
                Term *then_part = term_deref(t->compound.args[1]);
                while (then_part && then_part->tag == TT_COMPOUND &&
                       then_part->compound.functor == comma_id &&
                       then_part->compound.arity == 2) {
                    expr_add_child(e, lower_term(then_part->compound.args[0]));
                    then_part = term_deref(then_part->compound.args[1]);
                }
                if (then_part) expr_add_child(e, lower_term(then_part));
                return e;
            }

            /* General compound / goal -> E_FNC */
            EXPR_t *e = expr_new(E_FNC);
            e->sval = strdup(fn);
            for (int i = 0; i < arity; i++)
                expr_add_child(e, lower_term(t->compound.args[i]));
            return e;
        }
        case TT_REF:
            return lower_term(t->ref);
        default: {
            EXPR_t *e = expr_new(E_QLIT);
            e->sval = strdup("?");
            return e;
        }
    }
}

/* =========================================================================
 * Lower one PlClause -> E_CLAUSE EXPR_t
 *
 * E_CLAUSE layout:
 *   sval   = "functor/arity"
 *   ival   = n_vars (EnvLayout.n_vars)
 *   dval   = (double)n_args
 *   children[0..n_args-1]  = lowered head argument terms
 *   children[n_args..]     = lowered body goals
 *   (E_TRAIL_MARK and E_TRAIL_UNWIND are added as sentinel children
 *    around the head-unify region by the emitter; lower just records
 *    ival = trail_mark_slot = n_vars)
 * ======================================================================= */
static EXPR_t *lower_clause(PlClause *cl, PredKey key) {
    EXPR_t *ec = expr_new(E_CLAUSE);
    ec->sval = pred_str(key.functor, key.arity);

    /* Count distinct variable slots via full recursive Term walk */
    int max_slot = -1;

    /* Recursive helper using explicit stack to avoid C VLA issues */
    #define TERM_STACK_MAX 512
    Term *stk[TERM_STACK_MAX];
    int  stk_top = 0;

    #define PUSH_TERM(t_) do { \
        Term *_pt = term_deref(t_); \
        if (_pt && stk_top < TERM_STACK_MAX) stk[stk_top++] = _pt; \
    } while(0)

    #define WALK_ALL(root_) do { \
        stk_top = 0; \
        PUSH_TERM(root_); \
        while (stk_top > 0) { \
            Term *_cur = stk[--stk_top]; \
            if (!_cur) continue; \
            if (_cur->tag == TT_VAR && _cur->saved_slot > max_slot) \
                max_slot = _cur->saved_slot; \
            if (_cur->tag == TT_COMPOUND) \
                for (int _wi = 0; _wi < _cur->compound.arity; _wi++) \
                    PUSH_TERM(_cur->compound.args[_wi]); \
        } \
    } while(0)

    /* Walk head */
    if (cl->head) WALK_ALL(cl->head);

    /* Walk each body goal */
    for (int i = 0; i < cl->nbody; i++)
        if (cl->body[i]) WALK_ALL(cl->body[i]);
    int n_vars = max_slot + 1;
    ec->ival = n_vars;              /* EnvLayout.n_vars */
    ec->dval = (double)key.arity;  /* EnvLayout.n_args */

    /* Add head argument nodes */
    if (cl->head) {
        Term *h = term_deref(cl->head);
        if (h && h->tag == TT_COMPOUND) {
            for (int i = 0; i < h->compound.arity; i++)
                expr_add_child(ec, lower_term(h->compound.args[i]));
        }
        /* arity 0: head is just an atom, no arg children */
    }

    /* Add body goal nodes */
    for (int i = 0; i < cl->nbody; i++)
        expr_add_child(ec, lower_term(cl->body[i]));

    return ec;
}

/* =========================================================================
 * prolog_lower — main entry point
 * ======================================================================= */
Program *prolog_lower(PlProgram *pl_prog) {
    Program *prog = calloc(1, sizeof(Program));

    /* ---- Pass 1: collect all predicate keys in order of first appearance */
    #define MAX_PREDS 512
    PredKey  keys[MAX_PREDS];
    EXPR_t  *choices[MAX_PREDS];   /* one E_CHOICE per predicate */
    int      nkeys = 0;

    for (PlClause *cl = pl_prog->head; cl; cl = cl->next) {
        if (!cl->head) continue; /* skip directives for now */
        PredKey k = key_of_head(cl->head);
        if (k.functor < 0) continue;

        /* Find or create entry */
        int found = -1;
        for (int i = 0; i < nkeys; i++)
            if (pred_key_eq(keys[i], k)) { found = i; break; }

        if (found < 0) {
            if (nkeys >= MAX_PREDS) {
                fprintf(stderr, "prolog_lower: too many predicates\n");
                continue;
            }
            keys[nkeys] = k;
            choices[nkeys] = expr_new(E_CHOICE);
            choices[nkeys]->sval = pred_str(k.functor, k.arity);
            found = nkeys++;
        }

        /* Lower this clause and append to the choice */
        EXPR_t *ec = lower_clause(cl, k);
        expr_add_child(choices[found], ec);
    }

    /* ---- Pass 2: emit directives as plain E_FNC STMT_t nodes */
    for (PlClause *cl = pl_prog->head; cl; cl = cl->next) {
        if (!cl->head && cl->nbody > 0) {
            /* directive: :- goal */
            STMT_t *s = stmt_new();
            s->subject = lower_term(cl->body[0]);
            s->lineno  = cl->lineno;
            if (!prog->head) prog->head = s;
            else             prog->tail->next = s;
            prog->tail = s;
            prog->nstmts++;
        }
    }

    /* ---- Pass 3: emit one STMT_t per E_CHOICE */
    for (int i = 0; i < nkeys; i++) {
        STMT_t *s = stmt_new();
        s->subject = choices[i];
        s->lineno  = 0;
        if (!prog->head) prog->head = s;
        else             prog->tail->next = s;
        prog->tail = s;
        prog->nstmts++;
    }

    return prog;
}

/* =========================================================================
 * prolog_lower_pretty — IR dump for diagnostics
 * ======================================================================= */
static void expr_dump(EXPR_t *e, int indent, FILE *out) {
    if (!e) { fprintf(out, "%*s<null>\n", indent, ""); return; }
    const char *kname = "?";
    switch (e->kind) {
        case E_CHOICE:      kname = "E_CHOICE";      break;
        case E_CLAUSE:      kname = "E_CLAUSE";      break;
        case E_UNIFY:       kname = "E_UNIFY";       break;
        case E_CUT:         kname = "E_CUT";         break;
        case E_TRAIL_MARK:  kname = "E_TRAIL_MARK";  break;
        case E_TRAIL_UNWIND:kname = "E_TRAIL_UNWIND";break;
        case E_FNC:         kname = "E_FNC";         break;
        case E_QLIT:        kname = "E_QLIT";        break;
        case E_ILIT:        kname = "E_ILIT";        break;
        case E_FLIT:        kname = "E_FLIT";        break;
        case E_VART:        kname = "E_VART";        break;
        case E_ADD:         kname = "E_ADD";         break;
        case E_SUB:         kname = "E_SUB";         break;
        case E_MPY:         kname = "E_MPY";         break;
        case E_DIV:         kname = "E_DIV";         break;
        default: break;
    }
    fprintf(out, "%*s%s", indent, "", kname);
    if (e->sval) fprintf(out, "  sval=%s", e->sval);
    if (e->ival) fprintf(out, "  ival=%ld", e->ival);
    if (e->kind == E_CLAUSE)
        fprintf(out, "  n_vars=%ld  n_args=%.0f", e->ival, e->dval);
    fprintf(out, "\n");
    for (int i = 0; i < e->nchildren; i++)
        expr_dump(e->children[i], indent + 2, out);
}

void prolog_lower_pretty(Program *prog, FILE *out) {
    for (STMT_t *s = prog->head; s; s = s->next) {
        fprintf(out, "--- stmt (line %d) ---\n", s->lineno);
        expr_dump(s->subject, 2, out);
    }
}
