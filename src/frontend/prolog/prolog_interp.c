/*
 * prolog_interp.c — Prolog IR interpreter (Byrd box four-port model)
 *
 * Direct C-execution mirror of prolog_emit.c.  Every emit_* function has
 * a matching pl_interp_* function with identical structure; instead of
 * emitting C text with goto labels we execute the same control flow
 * directly in C using recursive calls and integer return codes.
 *
 * Four-port model (Byrd 1980 / Proebsting 1996):
 *   α  entry        — initial call, try clause 0
 *   β  retry        — re-entry after backtrack, try clause N
 *   γ  succeed      — departure on success  → return clause_idx >= 0
 *   ω  fail         — all clauses exhausted → return -1
 *
 * Resumable predicate call convention (mirrors emit_choice's _r functions):
 *   pl_call(pt, key, arity, args, trail, start)
 *     start == 0   → α (fresh call)
 *     start == N   → β (retry from clause N)
 *     returns >= 0 → γ (success; caller passes returns+1 on next retry)
 *     returns -1   → ω (all clauses exhausted / predicate not found)
 *
 * Body goal execution (mirrors emit_goal / emit_body):
 *   pl_exec_goal(goal, env, pred_table, trail, cut_flag*)
 *     returns 1 → γ   returns 0 → ω
 *
 * Entry point: pl_execute_program(prog)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "scrip_cc.h"
#include "prolog_interp.h"
#include "prolog_runtime.h"
/* unify() declared in prolog_runtime.h */
#include "prolog_atom.h"
#include "prolog_builtin.h"
#include "term.h"

/*===========================================================================
 * Predicate table — maps "functor/arity" → E_CHOICE*
 *=========================================================================*/
#define PRED_TABLE_SIZE 256
typedef struct PredEntry { const char *key; EXPR_t *choice; struct PredEntry *next; } PredEntry;
typedef struct { PredEntry *buckets[PRED_TABLE_SIZE]; } PredTable;

static unsigned pred_hash(const char *s) {
    unsigned h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h % PRED_TABLE_SIZE;
}
static void pred_table_insert(PredTable *pt, const char *key, EXPR_t *choice) {
    unsigned h = pred_hash(key);
    PredEntry *e = malloc(sizeof(PredEntry));
    e->key = key; e->choice = choice; e->next = pt->buckets[h]; pt->buckets[h] = e;
}
static EXPR_t *pred_table_lookup(PredTable *pt, const char *key) {
    for (PredEntry *e = pt->buckets[pred_hash(key)]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->choice;
    return NULL;
}

/*===========================================================================
 * Choice point stack — enables recursive backtracking (β re-entry)
 * Each user call pushes a CP; on failure we longjmp back and try next clause.
 *=========================================================================*/
#define CP_STACK_MAX 4096
typedef struct {
    jmp_buf     jb;           /* longjmp target for β re-entry              */
    PredTable  *pt;
    const char *key;
    int         arity;
    Term      **args;         /* live arg pointers (from caller's env slots) */
    Trail      *trail;
    int         trail_mark;   /* trail position at α entry                  */
    int         next_clause;  /* which clause to try on β                   */
    int         cut;          /* 1 = cut fired, no more β allowed            */
} ChoicePoint;

static ChoicePoint cp_stack[CP_STACK_MAX];
static int         cp_top = 0;   /* index of next free slot                 */


static Term **env_new(int n) {
    if (n <= 0) return NULL;
    Term **env = malloc(n * sizeof(Term *));
    for (int i = 0; i < n; i++) env[i] = term_new_var(i);
    return env;
}

/*===========================================================================
 * Forward declarations
 *=========================================================================*/
static int pl_exec_goal(EXPR_t *goal, Term **env, PredTable *pt,
                        Trail *trail, int *cut_flag);
static int pl_exec_body(EXPR_t **goals, int ngoals, Term **env,
                        PredTable *pt, Trail *trail, int *cut_flag);
static int pl_call(PredTable *pt, const char *key, int arity,
                   Term **args, Trail *trail, int start);

/*===========================================================================
 * pl_term_from_expr — mirrors emit_term_val()
 *=========================================================================*/
static Term *pl_term_from_expr(EXPR_t *e, Term **env) {
    if (!e) return term_new_atom(prolog_atom_intern("[]"));
    switch (e->kind) {
        case E_QLIT: return term_new_atom(prolog_atom_intern(e->sval ? e->sval : ""));
        case E_ILIT: return term_new_int((long)e->ival);
        case E_FLIT: return term_new_float(e->dval);
        case E_VAR:  return (env && e->ival >= 0) ? env[e->ival] : term_new_var(e->ival);
        case E_FNC: {
            int arity = e->nchildren;
            int atom  = prolog_atom_intern(e->sval ? e->sval : "f");
            if (arity == 0) return term_new_atom(atom);
            Term **args = malloc(arity * sizeof(Term *));
            for (int i = 0; i < arity; i++) args[i] = pl_term_from_expr(e->children[i], env);
            Term *t = term_new_compound(atom, arity, args);
            free(args);
            return t;
        }
        default: return term_new_atom(prolog_atom_intern("?"));
    }
}

/*===========================================================================
 * pl_eval_arith_expr — mirrors emit_arith_expr()
 *=========================================================================*/
static long pl_eval_arith_expr(EXPR_t *e, Term **env) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ILIT: return (long)e->ival;
        case E_FLIT: return (long)e->dval;
        case E_VAR: { Term *t = term_deref(env && e->ival >= 0 ? env[e->ival] : NULL);
                      return (t && t->tag == TT_INT) ? t->ival : 0; }
        case E_ADD:  return pl_eval_arith_expr(e->children[0], env) + pl_eval_arith_expr(e->children[1], env);
        case E_SUB:  return pl_eval_arith_expr(e->children[0], env) - pl_eval_arith_expr(e->children[1], env);
        case E_MUL:  return pl_eval_arith_expr(e->children[0], env) * pl_eval_arith_expr(e->children[1], env);
        case E_DIV:  { long d = pl_eval_arith_expr(e->children[1], env);
                       return d ? pl_eval_arith_expr(e->children[0], env) / d : 0; }
        case E_MOD:  { long d = pl_eval_arith_expr(e->children[1], env);
                       return d ? pl_eval_arith_expr(e->children[0], env) % d : 0; }
        case E_FNC: {
            const char *fn = e->sval ? e->sval : "";
            if (strcmp(fn,"mod")==0 && e->nchildren==2) { long d=pl_eval_arith_expr(e->children[1],env); return d?pl_eval_arith_expr(e->children[0],env)%d:0; }
            if (strcmp(fn,"abs")==0 && e->nchildren==1) { long v=pl_eval_arith_expr(e->children[0],env); return v<0?-v:v; }
            if (strcmp(fn,"max")==0 && e->nchildren==2) { long a=pl_eval_arith_expr(e->children[0],env),b=pl_eval_arith_expr(e->children[1],env); return a>b?a:b; }
            if (strcmp(fn,"min")==0 && e->nchildren==2) { long a=pl_eval_arith_expr(e->children[0],env),b=pl_eval_arith_expr(e->children[1],env); return a<b?a:b; }
            if (strcmp(fn,"rem")==0 && e->nchildren==2) { long d=pl_eval_arith_expr(e->children[1],env); return d?pl_eval_arith_expr(e->children[0],env)%d:0; }
            /* named var: deref */
            Term *t = term_deref(pl_term_from_expr(e, env));
            return (t && t->tag == TT_INT) ? t->ival : 0;
        }
        default: return 0;
    }
}

/*===========================================================================
 * is_user_call — mirrors emit.c's is_user_call()
 *=========================================================================*/
static int is_user_call(EXPR_t *goal) {
    if (!goal || goal->kind != E_FNC || !goal->sval) return 0;
    static const char *builtins[] = {
        "true","fail","halt","nl","write","writeln","print","tab","is",
        "<",">","=<",">=","=:=","=\\=","=","\\=","==","\\==",
        "@<","@>","@=<","@>=",
        "var","nonvar","atom","integer","float","compound","atomic","callable","is_list",
        "functor","arg","=..","\\+","not",",",";","->",
        NULL
    };
    for (int i = 0; builtins[i]; i++) if (strcmp(goal->sval, builtins[i]) == 0) return 0;
    return 1;
}

/*===========================================================================
 * pl_exec_goal — mirrors emit_goal()
 * Returns 1 (γ) or 0 (ω).
 *=========================================================================*/
static int pl_exec_goal(EXPR_t *goal, Term **env, PredTable *pt,
                        Trail *trail, int *cut_flag) {
    if (!goal) return 1;

    switch (goal->kind) {

        case E_UNIFY: {
            Term *t1 = pl_term_from_expr(goal->children[0], env);
            Term *t2 = pl_term_from_expr(goal->children[1], env);
            int mark = trail_mark(trail);
            if (!unify(t1, t2, trail)) { trail_unwind(trail, mark); return 0; }
            return 1;
        }

        case E_CUT:
            if (cut_flag) *cut_flag = 1;
            return 1;

        case E_TRAIL_MARK:
        case E_TRAIL_UNWIND:
            return 1;

        case E_FNC: {
            const char *fn = goal->sval ? goal->sval : "true";
            int arity = goal->nchildren;

            if (strcmp(fn,"true")==0 && arity==0) return 1;
            if (strcmp(fn,"fail")==0 && arity==0) return 0;
            if (strcmp(fn,"halt")==0 && arity==0) exit(0);
            if (strcmp(fn,"halt")==0 && arity==1) {
                Term *t = term_deref(pl_term_from_expr(goal->children[0], env));
                exit(t && t->tag==TT_INT ? (int)t->ival : 0);
            }
            if (strcmp(fn,"nl")==0 && arity==0) { putchar('\n'); return 1; }
            if (strcmp(fn,"write")==0 && arity==1) {
                pl_write(pl_term_from_expr(goal->children[0], env)); return 1;
            }
            if (strcmp(fn,"writeln")==0 && arity==1) {
                pl_write(pl_term_from_expr(goal->children[0], env)); putchar('\n'); return 1;
            }
            if (strcmp(fn,"print")==0 && arity==1) {
                pl_write(pl_term_from_expr(goal->children[0], env)); return 1;
            }
            if (strcmp(fn,"tab")==0 && arity==1) {
                Term *t = term_deref(pl_term_from_expr(goal->children[0], env));
                long n = (t && t->tag==TT_INT) ? t->ival : 0;
                for (long i=0;i<n;i++) putchar(' ');
                return 1;
            }
            if (strcmp(fn,"is")==0 && arity==2) {
                long val = pl_eval_arith_expr(goal->children[1], env);
                Term *lhs = pl_term_from_expr(goal->children[0], env);
                int mark = trail_mark(trail);
                if (!unify(lhs, term_new_int(val), trail)) { trail_unwind(trail, mark); return 0; }
                return 1;
            }
            /* arithmetic comparisons */
            {
                struct { const char *n; int op; } cmps[] = {
                    {"<",0},{">",1},{"=<",2},{">=",3},{"=:=",4},{"=\\=",5},{NULL,0}
                };
                for (int ci=0; cmps[ci].n; ci++) {
                    if (strcmp(fn,cmps[ci].n)==0 && arity==2) {
                        long a=pl_eval_arith_expr(goal->children[0],env);
                        long b=pl_eval_arith_expr(goal->children[1],env);
                        switch(cmps[ci].op){case 0:return a<b;case 1:return a>b;case 2:return a<=b;case 3:return a>=b;case 4:return a==b;case 5:return a!=b;}
                    }
                }
            }
            if (strcmp(fn,"=")==0 && arity==2) {
                int mark=trail_mark(trail);
                if (!unify(pl_term_from_expr(goal->children[0],env), pl_term_from_expr(goal->children[1],env), trail)) { trail_unwind(trail,mark); return 0; }
                return 1;
            }
            if (strcmp(fn,"\\=")==0 && arity==2) {
                int mark=trail_mark(trail);
                int ok=unify(pl_term_from_expr(goal->children[0],env), pl_term_from_expr(goal->children[1],env), trail);
                trail_unwind(trail,mark); return !ok;
            }
            if (strcmp(fn,"==")==0 && arity==2) {
                Term *t1=term_deref(pl_term_from_expr(goal->children[0],env));
                Term *t2=term_deref(pl_term_from_expr(goal->children[1],env));
                if (!t1||!t2) return t1==t2;
                if (t1->tag!=t2->tag) return 0;
                if (t1->tag==TT_ATOM) return t1->atom_id==t2->atom_id;
                if (t1->tag==TT_INT)  return t1->ival==t2->ival;
                if (t1->tag==TT_VAR)  return t1==t2;
                return 0;
            }
            if (strcmp(fn,"\\==")==0 && arity==2) {
                Term *t1=term_deref(pl_term_from_expr(goal->children[0],env));
                Term *t2=term_deref(pl_term_from_expr(goal->children[1],env));
                if (!t1||!t2) return t1!=t2;
                if (t1->tag!=t2->tag) return 1;
                if (t1->tag==TT_ATOM) return t1->atom_id!=t2->atom_id;
                if (t1->tag==TT_INT)  return t1->ival!=t2->ival;
                if (t1->tag==TT_VAR)  return t1!=t2;
                return 1;
            }
            /* type tests */
            if (arity==1) {
                Term *t=term_deref(pl_term_from_expr(goal->children[0],env));
                if (strcmp(fn,"var"     )==0) return !t||t->tag==TT_VAR;
                if (strcmp(fn,"nonvar"  )==0) return  t&&t->tag!=TT_VAR;
                if (strcmp(fn,"atom"    )==0) return  t&&t->tag==TT_ATOM;
                if (strcmp(fn,"integer" )==0) return  t&&t->tag==TT_INT;
                if (strcmp(fn,"float"   )==0) return  t&&t->tag==TT_FLOAT;
                if (strcmp(fn,"compound")==0) return  t&&t->tag==TT_COMPOUND;
                if (strcmp(fn,"atomic"  )==0) return  t&&(t->tag==TT_ATOM||t->tag==TT_INT||t->tag==TT_FLOAT);
                if (strcmp(fn,"callable")==0) return  t&&(t->tag==TT_ATOM||t->tag==TT_COMPOUND);
                if (strcmp(fn,"is_list" )==0) {
                    int nil=prolog_atom_intern("[]"), dot=prolog_atom_intern(".");
                    for (Term *c=t;;) {
                        c=term_deref(c); if (!c) return 0;
                        if (c->tag==TT_ATOM&&c->atom_id==nil) return 1;
                        if (c->tag!=TT_COMPOUND||c->compound.arity!=2||c->compound.functor!=dot) return 0;
                        c=c->compound.args[1];
                    }
                }
            }
            /* ,/N conjunction — lowerer flattens right-spine to n-ary E_FNC(",") */
            if (strcmp(fn,",")==0) {
                return pl_exec_body(goal->children, goal->nchildren, env, pt, trail, cut_flag);
            }
            /* ;/N disjunction — lowerer may flatten but we only need children[0]/[1] */
            if (strcmp(fn,";")==0 && arity>=2) {
                EXPR_t *left=goal->children[0], *right=goal->children[1];
                if (left&&left->kind==E_FNC&&left->sval&&strcmp(left->sval,"->")==0&&left->nchildren>=2) {
                    /* (Cond -> Then ; Else) */
                    Trail save=*trail; int mark=trail_mark(trail); int cut2=0;
                    if (pl_exec_goal(left->children[0],env,pt,trail,&cut2)) {
                        int nthen=left->nchildren-1;
                        return pl_exec_body(left->children+1,nthen,env,pt,trail,cut_flag);
                    }
                    trail_unwind(trail,mark); *trail=save;
                    return pl_exec_goal(right,env,pt,trail,cut_flag);
                }
                { Trail save=*trail; int mark=trail_mark(trail); int cut2=0;
                  if (pl_exec_goal(left,env,pt,trail,&cut2)) return 1;
                  trail_unwind(trail,mark); *trail=save;
                  return pl_exec_goal(right,env,pt,trail,cut_flag); }
            }
            /* ->/N if-then — children[0]=Cond, children[1..]=Then goals */
            if (strcmp(fn,"->")==0 && arity>=2) {
                int cut2=0;
                if (!pl_exec_goal(goal->children[0],env,pt,trail,&cut2)) return 0;
                int nthen=goal->nchildren-1;
                return pl_exec_body(goal->children+1,nthen,env,pt,trail,cut_flag);
            }
            /* \+/1 negation-as-failure */
            if ((strcmp(fn,"\\+")==0||strcmp(fn,"not")==0) && arity==1) {
                Trail save=*trail; int mark=trail_mark(trail); int cut2=0;
                int ok=pl_exec_goal(goal->children[0],env,pt,trail,&cut2);
                trail_unwind(trail,mark); *trail=save;
                return !ok;
            }
            /* functor/3 */
            if (strcmp(fn,"functor")==0 && arity==3) {
                int mark=trail_mark(trail);
                if (!pl_functor(pl_term_from_expr(goal->children[0],env),
                                pl_term_from_expr(goal->children[1],env),
                                pl_term_from_expr(goal->children[2],env), trail))
                { trail_unwind(trail,mark); return 0; }
                return 1;
            }
            /* arg/3 */
            if (strcmp(fn,"arg")==0 && arity==3) {
                int mark=trail_mark(trail);
                if (!pl_arg(pl_term_from_expr(goal->children[0],env),
                            pl_term_from_expr(goal->children[1],env),
                            pl_term_from_expr(goal->children[2],env), trail))
                { trail_unwind(trail,mark); return 0; }
                return 1;
            }
            /* =../2 univ */
            if (strcmp(fn,"=..")==0 && arity==2) {
                int mark=trail_mark(trail);
                if (!pl_univ(pl_term_from_expr(goal->children[0],env),
                             pl_term_from_expr(goal->children[1],env), trail))
                { trail_unwind(trail,mark); return 0; }
                return 1;
            }
            /* user-defined call */
            if (is_user_call(goal)) {
                char key[256]; snprintf(key,sizeof key,"%s/%d",fn,arity);
                Term **args=malloc(arity*sizeof(Term*));
                for (int i=0;i<arity;i++) args[i]=pl_term_from_expr(goal->children[i],env);
                int mark=trail_mark(trail);
                int r=pl_call(pt,key,arity,args,trail,0);
                free(args);
                if (r<0) { trail_unwind(trail,mark); return 0; }
                return 1;
            }
            fprintf(stderr,"prolog: undefined predicate %s/%d\n",fn,arity);
            return 0;
        }

        default: return 1;
    }
}

/*===========================================================================
 * pl_exec_clause — mirrors emit_clause()
 *=========================================================================*/
static int pl_exec_clause(EXPR_t *ec, int n_args, Term **call_args,
                          PredTable *pt, Trail *trail, int *cut_flag) {
    if (!ec||ec->kind!=E_CLAUSE) return 0;
    int n_vars=(int)ec->ival;
    Term **env=env_new(n_vars);
    int head_mark=trail_mark(trail);
    for (int i=0;i<n_args&&i<ec->nchildren;i++) {
        Term *head_arg=pl_term_from_expr(ec->children[i],env);
        if (!unify(call_args[i],head_arg,trail)) {
            trail_unwind(trail,head_mark); free(env); return 0;
        }
    }
    int nbody=ec->nchildren-n_args;
    EXPR_t **body=ec->children+n_args;
    int ok = nbody==0 ? 1 : pl_exec_body(body,nbody,env,pt,trail,cut_flag);
    free(env);
    return ok;
}

/*===========================================================================
 * pl_call — CP-stack based four-port predicate dispatcher
 *
 * On α (start==0): push a ChoicePoint onto cp_stack, setjmp into it.
 *   Try each clause in order.  After a clause succeeds, return 1 to the
 *   continuation.  If the continuation later fails it longjmps back here
 *   (via pl_fail_to_cp), we unwind trail and try the next clause.
 *   When all clauses are exhausted, pop the CP and return 0 (ω).
 *
 * cut support: if clause_cut fires, we clear next_clause = nclauses so
 *   that a subsequent longjmp finds no more clauses and pops.
 *=========================================================================*/


/*===========================================================================
 * pl_exec_body / pl_call — CP-stack four-port dispatcher
 *
 * ARCHITECTURE (mirrors prolog_emit.c emit_body exactly):
 *
 * The emitter compiles each predicate to a _r function.  Inside emit_body,
 * for a user call U followed by suffix S:
 *
 *   retry_N:
 *     trail_unwind(..., mark);
 *     _cr = U_r(args, &_trail, _cs);       ← tries one clause of U
 *     if (_cr < 0) goto outer_ω;
 *     _cs = _cr + 1;
 *     <S goals inlined here, with ω = retry_N>   ← suffix ON THE STACK
 *
 * The key: S is inlined after the call, so its ω jumps back to retry_N.
 * The C call stack naturally handles recursion: each recursive U call has
 * its own retry_N on its own stack frame.
 *
 * The interpreter mirrors this via a CONTINUATION parameter:
 *   pl_exec_body_k(goals, ngoals, env, pt, trail, cut_flag, cont)
 *
 * cont is a Cont_t — a function pointer + closure called when the goal
 * list succeeds.  For user calls, the suffix becomes the continuation.
 * On failure, longjmp pops back to the innermost CP.
 *
 * This keeps the inner member CP live while the suffix (write,nl,fail)
 * executes — because the suffix is called INSIDE the inner while-loop,
 * not after it returns.
 *=========================================================================*/

/* Longjmp to innermost CP on failure. */
static void pl_fail_to_cp(void) {
    if (cp_top == 0) return;
    longjmp(cp_stack[cp_top-1].jb, 1);
}

/* ---- Continuation type ---- */
typedef struct Cont_t Cont_t;
struct Cont_t {
    int (*fn)(Cont_t *self);  /* call this when goals succeed */
    /* payload follows in subclasses (struct embedding) */
};

/* Terminal continuation: just return 1 (γ). */
static int cont_done_fn(Cont_t *self) { (void)self; return 1; }
static Cont_t cont_done_val = { cont_done_fn };
static Cont_t *cont_done = &cont_done_val;

/* ---- Forward declarations ---- */
static int pl_exec_body_k(EXPR_t **goals, int ngoals, Term **env,
                          PredTable *pt, Trail *trail, int *cut_flag,
                          Cont_t *cont);

/* ---- Body continuation: run a goal list then invoke outer cont ---- */
typedef struct {
    Cont_t      base;
    EXPR_t    **goals;
    int         ngoals;
    Term      **env;
    PredTable  *pt;
    Trail      *trail;
    int        *cut_flag;
    Cont_t     *next;        /* outer continuation */
} Body_cont;

static int body_cont_fn(Cont_t *self) {
    Body_cont *bc = (Body_cont *)self;
    return pl_exec_body_k(bc->goals, bc->ngoals, bc->env,
                          bc->pt, bc->trail, bc->cut_flag, bc->next);
}

/*---------------------------------------------------------------------------
 * pl_exec_body_k — execute goal list, call cont on success.
 *
 * For user calls at goals[0]: push CP, setjmp, loop over clauses.
 * Each clause attempt: unify head, run body with a Body_cont that chains
 * into (goals+1, cont) — keeping THIS CP live while the entire suffix runs.
 * On failure from anywhere inside, longjmp re-enters the setjmp, advancing
 * to the next clause.
 *-------------------------------------------------------------------------*/
static int pl_exec_body_k(EXPR_t **goals, int ngoals, Term **env,
                          PredTable *pt, Trail *trail, int *cut_flag,
                          Cont_t *cont) {
    if (ngoals == 0) return cont->fn(cont);

    EXPR_t *g = goals[0];

    if (is_user_call(g)) {
        const char *fn = g->sval; int arity = g->nchildren;
        char key[256]; snprintf(key, sizeof key, "%s/%d", fn, arity);
        EXPR_t *choice = pred_table_lookup(pt, key);
        if (!choice) { fprintf(stderr, "prolog: undefined predicate %s\n", key); return 0; }
        int nclauses = choice->nchildren;

        if (cp_top >= CP_STACK_MAX) { fprintf(stderr, "prolog: CP stack overflow\n"); return 0; }
        ChoicePoint *cp = &cp_stack[cp_top++];
        cp->pt          = pt;
        cp->key         = key;
        cp->arity       = arity;
        cp->trail       = trail;
        cp->trail_mark  = trail_mark(trail);
        cp->next_clause = 0;
        cp->cut         = 0;

        /* setjmp re-entry point — longjmp from suffix failure lands here */
        setjmp(cp->jb);

        int result = 0;
        while (!cp->cut && cp->next_clause < nclauses) {
            int ci = cp->next_clause;
            trail_unwind(trail, cp->trail_mark);

            /* Rebuild call args — trail unwind reset var bindings */
            Term **args = malloc(arity * sizeof(Term *));
            for (int i = 0; i < arity; i++) args[i] = pl_term_from_expr(g->children[i], env);
            cp->next_clause = ci + 1;

            EXPR_t *ec = choice->children[ci];
            if (!ec || ec->kind != E_CLAUSE) { free(args); continue; }

            /* Unify head args into fresh clause env */
            int n_vars = (int)ec->ival;
            Term **cenv = env_new(n_vars);
            int head_mark = trail_mark(trail);
            int head_ok = 1;
            for (int i = 0; i < arity && i < ec->nchildren; i++) {
                Term *ha = pl_term_from_expr(ec->children[i], cenv);
                if (!unify(args[i], ha, trail)) { head_ok = 0; break; }
            }
            free(args);
            if (!head_ok) { trail_unwind(trail, head_mark); free(cenv); continue; }

            /* Build continuation: clause body → (goals+1 in caller env) → cont */
            /* The suffix (goals+1) runs in the CALLER's env.                   */
            Body_cont suffix_bc;
            suffix_bc.base.fn = body_cont_fn;
            suffix_bc.goals   = goals + 1;
            suffix_bc.ngoals  = ngoals - 1;
            suffix_bc.env     = env;         /* caller env for suffix */
            suffix_bc.pt      = pt;
            suffix_bc.trail   = trail;
            suffix_bc.cut_flag = cut_flag;
            suffix_bc.next    = cont;

            /* Execute clause body in clause env; suffix is the continuation  */
            int nbody = ec->nchildren - arity;
            EXPR_t **body = ec->children + arity;
            int clause_cut = 0;
            int ok;
            if (nbody == 0) {
                ok = suffix_bc.base.fn(&suffix_bc.base);
            } else {
                ok = pl_exec_body_k(body, nbody, cenv, pt, trail, &clause_cut,
                                    &suffix_bc.base);
            }
            free(cenv);
            if (clause_cut) { if (cut_flag) *cut_flag = 1; result = ok; break; }
            if (ok) { result = 1; break; }
            /* Continuation failed — loop (or longjmp re-enters setjmp above) */
        }

        cp_top--;
        return result;
    }

    /* Deterministic goal */
    if (!pl_exec_goal(g, env, pt, trail, cut_flag)) return 0;
    return pl_exec_body_k(goals + 1, ngoals - 1, env, pt, trail, cut_flag, cont);
}

/* Public wrappers matching original signatures */
static int pl_exec_body(EXPR_t **goals, int ngoals, Term **env,
                        PredTable *pt, Trail *trail, int *cut_flag) {
    return pl_exec_body_k(goals, ngoals, env, pt, trail, cut_flag, cont_done);
}

/* pl_call — single-shot predicate call, no suffix continuation.
 * Used by pl_exec_goal for user calls inside builtins (\\+, etc.). */
static int pl_call(PredTable *pt, const char *key, int arity,
                   Term **args, Trail *trail, int start) {
    EXPR_t *choice = pred_table_lookup(pt, key);
    if (!choice) { fprintf(stderr, "prolog: undefined predicate %s\n", key); return -1; }
    int nclauses = choice->nchildren;
    int mark = trail_mark(trail);
    for (int ci = start; ci < nclauses; ci++) {
        if (ci > start) trail_unwind(trail, mark);
        EXPR_t *ec = choice->children[ci]; if (!ec) continue;
        int clause_cut = 0;
        if (pl_exec_clause(ec, arity, args, pt, trail, &clause_cut)) return ci;
        if (clause_cut) break;
    }
    trail_unwind(trail, mark);
    return -1;
}


/*===========================================================================
 * pl_execute_program — entry point
 *=========================================================================*/
void pl_execute_program(Program *prog) {
    if (!prog) return;
    prolog_atom_init();
    PredTable pt; memset(&pt, 0, sizeof pt);
    for (STMT_t *s = prog->head; s; s = s->next) {
        EXPR_t *subj = s->subject;
        if (subj && (subj->kind == E_CHOICE || subj->kind == E_CLAUSE) && subj->sval)
            pred_table_insert(&pt, subj->sval, subj);
    }
    Trail trail; trail_init(&trail);
    pl_call(&pt, "main/0", 0, NULL, &trail, 0);
}
