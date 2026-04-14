/*
 * pl_runtime.c — Prolog interpreter runtime
 *
 * FI-5: extracted from src/driver/scrip.c.
 * Contains: pl_pred_table_*, pl_cp_stack, pl_trail_*, pl_unify_*,
 *           pl_box_choice, pl_ helpers, interp_exec_pl_builtin.
 *
 * g_pl_trail / g_pl_cut_flag remain non-static (used by pl_broker.c).
 * interp_eval / interp_eval_pat / execute_program stay in scrip.c.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6 (FI-5, 2026-04-14)
 */
#include "pl_runtime.h"
#include "../../ir/ir.h"
#include "../../frontend/snobol4/scrip_cc.h"
#include "../../frontend/prolog/prolog_driver.h"
#include "../../frontend/prolog/term.h"
#include "../../frontend/prolog/prolog_runtime.h"
#include "../../frontend/prolog/prolog_atom.h"
#include "../../frontend/prolog/prolog_builtin.h"
#include "../../frontend/prolog/pl_broker.h"
#include "../../runtime/x86/bb_broker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

extern DESCR_t interp_eval(EXPR_t *e);
extern DESCR_t interp_eval_pat(EXPR_t *e);

/* Globals declared in pl_runtime.h */
Pl_PredTable  g_pl_pred_table;
Trail         g_pl_trail;
int           g_pl_cut_flag = 0;
Term        **g_pl_env      = NULL;
int           g_pl_active   = 0;

#define PL_PRED_TABLE_SIZE PL_PRED_TABLE_SIZE_FWD

unsigned pl_pred_hash(const char *s) {
    unsigned h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h % PL_PRED_TABLE_SIZE;
}
void pl_pred_table_insert(Pl_PredTable *pt, const char *key, EXPR_t *choice) {
    unsigned h = pl_pred_hash(key);
    Pl_PredEntry *e = malloc(sizeof(Pl_PredEntry));
    e->key = key; e->choice = choice; e->next = pt->buckets[h]; pt->buckets[h] = e;
}
EXPR_t *pl_pred_table_lookup(Pl_PredTable *pt, const char *key) {
    for (Pl_PredEntry *e = pt->buckets[pl_pred_hash(key)]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->choice;
    return NULL;
}

/* pl_pred_table_lookup_global — non-static wrapper for pl_broker.c (pl_interp.h) */
EXPR_t *pl_pred_table_lookup_global(const char *key) {
    return pl_pred_table_lookup(&g_pl_pred_table, key);
}

/*---- Choice point stack ----*/
#define PL_CP_STACK_MAX 4096
typedef struct {
    jmp_buf     jb;
    Pl_PredTable *pt;
    const char *key;
    int         arity;
    Trail      *trail;
    int         trail_mark;
    int         next_clause;
    int         cut;
} Pl_ChoicePoint;
static Pl_ChoicePoint pl_cp_stack[PL_CP_STACK_MAX];
static int            pl_cp_top = 0;

Term **pl_env_new(int n) {
    if (n <= 0) return NULL;
    Term **env = malloc(n * sizeof(Term *));
    for (int i = 0; i < n; i++) env[i] = term_new_var(i);
    return env;
}

/*---- Continuation type ----*/
/*---- Forward declarations ----*/
Term *pl_unified_term_from_expr(EXPR_t *e, Term **env);
static Term *pl_unified_deep_copy(Term *t);
int          interp_exec_pl_builtin(EXPR_t *goal, Term **env);



/*---- pl_unified_term_from_expr ----*/
Term *pl_unified_term_from_expr(EXPR_t *e, Term **env) {
    if (!e) return term_new_atom(prolog_atom_intern("[]"));
    switch (e->kind) {
        case E_QLIT: return term_new_atom(prolog_atom_intern(e->sval ? e->sval : ""));
        case E_ILIT: return term_new_int((long)e->ival);
        case E_FLIT: return term_new_float(e->dval);
        case E_VAR:  return (env && e->ival >= 0) ? env[e->ival] : term_new_var(e->ival);
        case E_ADD: case E_SUB: case E_MUL: case E_DIV: case E_MOD: {
            /* arithmetic ops used as terms (e.g. K-V): wrap as compound */
            const char *op = e->kind==E_ADD?"+":e->kind==E_SUB?"-":e->kind==E_MUL?"*":e->kind==E_DIV?"/":"%";
            int atom = prolog_atom_intern(op);
            Term *args2[2]; args2[0]=pl_unified_term_from_expr(e->children[0],env); args2[1]=pl_unified_term_from_expr(e->children[1],env);
            return term_new_compound(atom, 2, args2);
        }
        case E_FNC: {
            int arity = e->nchildren;
            int atom  = prolog_atom_intern(e->sval ? e->sval : "f");
            if (arity == 0) return term_new_atom(atom);
            Term **args = malloc(arity * sizeof(Term *));
            for (int i = 0; i < arity; i++) args[i] = pl_unified_term_from_expr(e->children[i], env);
            Term *t = term_new_compound(atom, arity, args);
            free(args);
            return t;
        }
        default: return term_new_atom(prolog_atom_intern("?"));
    }
}

/*---- pl_unified_deep_copy ----*/
static Term *pl_unified_deep_copy(Term *t) {
    t = term_deref(t);
    if (!t || t->tag == TT_VAR) return term_new_atom(prolog_atom_intern("_"));
    if (t->tag == TT_ATOM)  return term_new_atom(t->atom_id);
    if (t->tag == TT_INT)   return term_new_int(t->ival);
    if (t->tag == TT_FLOAT) return term_new_float(t->fval);
    if (t->tag == TT_COMPOUND) {
        Term **args = malloc(t->compound.arity * sizeof(Term *));
        for (int i = 0; i < t->compound.arity; i++) args[i] = pl_unified_deep_copy(t->compound.args[i]);
        Term *r = term_new_compound(t->compound.functor, t->compound.arity, args);
        free(args);
        return r;
    }
    return term_new_atom(prolog_atom_intern("_"));
}

/*---- pl_unified_eval_arith ----*/
static long pl_unified_eval_arith(EXPR_t *e, Term **env) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ILIT: return (long)e->ival;
        case E_FLIT: return (long)e->dval;
        case E_VAR: { Term *t = term_deref(env && e->ival >= 0 ? env[e->ival] : NULL);
                      return (t && t->tag == TT_INT) ? t->ival : 0; }
        case E_ADD: return pl_unified_eval_arith(e->children[0],env) + pl_unified_eval_arith(e->children[1],env);
        case E_SUB: return pl_unified_eval_arith(e->children[0],env) - pl_unified_eval_arith(e->children[1],env);
        case E_MUL: return pl_unified_eval_arith(e->children[0],env) * pl_unified_eval_arith(e->children[1],env);
        case E_DIV: { long d=pl_unified_eval_arith(e->children[1],env); return d?pl_unified_eval_arith(e->children[0],env)/d:0; }
        case E_MOD: { long d=pl_unified_eval_arith(e->children[1],env); return d?pl_unified_eval_arith(e->children[0],env)%d:0; }
        case E_FNC: {
            const char *fn = e->sval ? e->sval : "";
            if (strcmp(fn,"mod")==0&&e->nchildren==2){long d=pl_unified_eval_arith(e->children[1],env);return d?pl_unified_eval_arith(e->children[0],env)%d:0;}
            if (strcmp(fn,"abs")==0&&e->nchildren==1){long v=pl_unified_eval_arith(e->children[0],env);return v<0?-v:v;}
            if (strcmp(fn,"max")==0&&e->nchildren==2){long a=pl_unified_eval_arith(e->children[0],env),b=pl_unified_eval_arith(e->children[1],env);return a>b?a:b;}
            if (strcmp(fn,"min")==0&&e->nchildren==2){long a=pl_unified_eval_arith(e->children[0],env),b=pl_unified_eval_arith(e->children[1],env);return a<b?a:b;}
            if (strcmp(fn,"rem")==0&&e->nchildren==2){long d=pl_unified_eval_arith(e->children[1],env);return d?pl_unified_eval_arith(e->children[0],env)%d:0;}
            Term *t=term_deref(pl_unified_term_from_expr(e,env));
            return (t&&t->tag==TT_INT)?t->ival:0;
        }
        default: return 0;
    }
}

/*---- is_pl_user_call ----*/
int is_pl_user_call(EXPR_t *goal) {
    if (!goal || goal->kind != E_FNC || !goal->sval) return 0;
    static const char *builtins[] = {
        "true","fail","halt","nl","write","writeln","print","tab","is",
        "<",">","=<",">=","=:=","=\\=","=","\\=","==","\\==",
        "@<","@>","@=<","@>=",
        "var","nonvar","atom","integer","float","compound","atomic","callable","is_list",
        "functor","arg","=..","\\+","not",",",";","->","findall",
        "assert","assertz","asserta","retract","retractall","abolish",
        "nv_get","nv_set",
        NULL
    };
    for (int i = 0; builtins[i]; i++) if (strcmp(goal->sval, builtins[i]) == 0) return 0;
    return 1;
}

/*---- interp_exec_pl_builtin — execute one Prolog builtin goal ----*/
/* Uses file-scope globals g_pl_trail, g_pl_cut_flag, g_pl_pred_table, g_pl_env.
 * Returns 1=success, 0=fail. Called by pl_box_builtin in pl_broker.c. */
int interp_exec_pl_builtin(EXPR_t *goal, Term **env) {
    if (!goal) return 1;
    Trail *trail = &g_pl_trail;
    int *cut_flag = &g_pl_cut_flag;
    switch (goal->kind) {
        case E_UNIFY: {
            Term *t1=pl_unified_term_from_expr(goal->children[0],env);
            Term *t2=pl_unified_term_from_expr(goal->children[1],env);
            int mark=trail_mark(trail);
            if (!unify(t1,t2,trail)){trail_unwind(trail,mark);return 0;}
            return 1;
        }
        case E_CUT: if (cut_flag) *cut_flag=1; return 1;
        case E_TRAIL_MARK: case E_TRAIL_UNWIND: return 1;
        case E_FNC: {
            const char *fn = goal->sval ? goal->sval : "true";
            int arity = goal->nchildren;
            if (strcmp(fn,"true")==0&&arity==0) return 1;
            if (strcmp(fn,"fail")==0&&arity==0) return 0;
            if (strcmp(fn,"halt")==0&&arity==0) exit(0);
            if (strcmp(fn,"halt")==0&&arity==1){Term *t=term_deref(pl_unified_term_from_expr(goal->children[0],env));exit(t&&t->tag==TT_INT?(int)t->ival:0);}
            if (strcmp(fn,"nl")==0&&arity==0){putchar('\n');return 1;}
            if (strcmp(fn,"write")==0&&arity==1){pl_write(pl_unified_term_from_expr(goal->children[0],env));return 1;}
            if (strcmp(fn,"writeln")==0&&arity==1){pl_write(pl_unified_term_from_expr(goal->children[0],env));putchar('\n');return 1;}
            if (strcmp(fn,"print")==0&&arity==1){pl_write(pl_unified_term_from_expr(goal->children[0],env));return 1;}
            if (strcmp(fn,"tab")==0&&arity==1){
                Term *t=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                long n=(t&&t->tag==TT_INT)?t->ival:0;
                for(long i=0;i<n;i++) putchar(' ');
                return 1;
            }
            if (strcmp(fn,"is")==0&&arity==2){
                long val=pl_unified_eval_arith(goal->children[1],env);
                Term *lhs=pl_unified_term_from_expr(goal->children[0],env);
                int mark=trail_mark(trail);
                if(!unify(lhs,term_new_int(val),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* arithmetic comparisons */
            { struct{const char *n;int op;}cmps[]={{"<",0},{">",1},{"=<",2},{">=",3},{"=:=",4},{"=\\=",5},{NULL,0}};
              for(int ci=0;cmps[ci].n;ci++) if(strcmp(fn,cmps[ci].n)==0&&arity==2){
                  long a=pl_unified_eval_arith(goal->children[0],env),b=pl_unified_eval_arith(goal->children[1],env);
                  switch(cmps[ci].op){case 0:return a<b;case 1:return a>b;case 2:return a<=b;case 3:return a>=b;case 4:return a==b;case 5:return a!=b;}
              }
            }
            if (strcmp(fn,"=")==0&&arity==2){
                int mark=trail_mark(trail);
                if(!unify(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            if (strcmp(fn,"\\=")==0&&arity==2){
                int mark=trail_mark(trail);
                int ok=unify(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),trail);
                trail_unwind(trail,mark);return !ok;
            }
            if (strcmp(fn,"==")==0&&arity==2){
                Term *t1=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                Term *t2=term_deref(pl_unified_term_from_expr(goal->children[1],env));
                if(!t1||!t2)return t1==t2;
                if(t1->tag!=t2->tag)return 0;
                if(t1->tag==TT_ATOM)return t1->atom_id==t2->atom_id;
                if(t1->tag==TT_INT) return t1->ival==t2->ival;
                if(t1->tag==TT_VAR) return t1==t2;
                return 0;
            }
            if (strcmp(fn,"\\==")==0&&arity==2){
                Term *t1=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                Term *t2=term_deref(pl_unified_term_from_expr(goal->children[1],env));
                if(!t1||!t2)return t1!=t2;
                if(t1->tag!=t2->tag)return 1;
                if(t1->tag==TT_ATOM)return t1->atom_id!=t2->atom_id;
                if(t1->tag==TT_INT) return t1->ival!=t2->ival;
                if(t1->tag==TT_VAR) return t1!=t2;
                return 1;
            }
            /* type tests */
            if (arity==1){
                Term *t=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                if(strcmp(fn,"var"     )==0)return !t||t->tag==TT_VAR;
                if(strcmp(fn,"nonvar"  )==0)return  t&&t->tag!=TT_VAR;
                if(strcmp(fn,"atom"    )==0)return  t&&t->tag==TT_ATOM;
                if(strcmp(fn,"integer" )==0)return  t&&t->tag==TT_INT;
                if(strcmp(fn,"float"   )==0)return  t&&t->tag==TT_FLOAT;
                if(strcmp(fn,"compound")==0)return  t&&t->tag==TT_COMPOUND;
                if(strcmp(fn,"atomic"  )==0)return  t&&(t->tag==TT_ATOM||t->tag==TT_INT||t->tag==TT_FLOAT);
                if(strcmp(fn,"callable")==0)return  t&&(t->tag==TT_ATOM||t->tag==TT_COMPOUND);
                if(strcmp(fn,"is_list" )==0){
                    int nil=prolog_atom_intern("[]"),dot=prolog_atom_intern(".");
                    for(Term *c=t;;){c=term_deref(c);if(!c)return 0;if(c->tag==TT_ATOM&&c->atom_id==nil)return 1;if(c->tag!=TT_COMPOUND||c->compound.arity!=2||c->compound.functor!=dot)return 0;c=c->compound.args[1];}
                }
            }
            /* ,/N conjunction — run each child goal in sequence */
            if (strcmp(fn,",")==0){
                for(int i=0;i<goal->nchildren;i++){
                    EXPR_t *g=goal->children[i];
                    if(!g) continue;
                    int ok = is_pl_user_call(g) ? ({
                        char key[256]; snprintf(key,sizeof key,"%s/%d",g->sval?g->sval:"",g->nchildren);
                        EXPR_t *ch=pl_pred_table_lookup(&g_pl_pred_table,key);
                        int r=0;
                        if(ch){ int ca=g->nchildren; Term **cargs=ca?malloc(ca*sizeof(Term*)):NULL;
                                 for(int a=0;a<ca;a++) cargs[a]=pl_unified_term_from_expr(g->children[a],env);
                                 Term **sv=g_pl_env; g_pl_env=cargs;
                                 DESCR_t rd=interp_eval(ch); g_pl_env=sv; if(cargs)free(cargs);
                                 r=!IS_FAIL_fn(rd); }
                        r; }) : interp_exec_pl_builtin(g, env);
                    if(!ok) return 0;
                }
                return 1;
            }
            /* ;/N disjunction */
            if (strcmp(fn,";")==0&&arity>=2){
                EXPR_t *left=goal->children[0],*right=goal->children[1];
                /* if-then-else: (Cond -> Then ; Else) */
                if(left&&left->kind==E_FNC&&left->sval&&strcmp(left->sval,"->")==0&&left->nchildren>=2){
                    int mark=trail_mark(trail); int cut2=0;
                    if(interp_exec_pl_builtin(left->children[0],env)){
                        for(int i=1;i<left->nchildren;i++) if(!interp_exec_pl_builtin(left->children[i],env)) return 0;
                        return 1;
                    }
                    trail_unwind(trail,mark);
                    return interp_exec_pl_builtin(right,env);
                }
                /* plain disjunction */
                {int mark=trail_mark(trail);
                 if(interp_exec_pl_builtin(left,env)) return 1;
                 trail_unwind(trail,mark);
                 return interp_exec_pl_builtin(right,env);}
            }
            /* ->/N if-then */
            if (strcmp(fn,"->")==0&&arity>=2){
                if(!interp_exec_pl_builtin(goal->children[0],env)) return 0;
                for(int i=1;i<goal->nchildren;i++) if(!interp_exec_pl_builtin(goal->children[i],env)) return 0;
                return 1;
            }
            /* \+/not */
            if ((strcmp(fn,"\\+")==0||strcmp(fn,"not")==0)&&arity==1){
                int mark=trail_mark(trail);
                int ok=interp_exec_pl_builtin(goal->children[0],env);
                trail_unwind(trail,mark);return !ok;
            }
            /* functor/3 */
            if (strcmp(fn,"functor")==0&&arity==3){
                int mark=trail_mark(trail);
                if(!pl_functor(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),pl_unified_term_from_expr(goal->children[2],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* arg/3 */
            if (strcmp(fn,"arg")==0&&arity==3){
                int mark=trail_mark(trail);
                if(!pl_arg(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),pl_unified_term_from_expr(goal->children[2],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* =../2 */
            if (strcmp(fn,"=..")==0&&arity==2){
                int mark=trail_mark(trail);
                if(!pl_univ(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* assert/assertz/asserta/retract/retractall/abolish — stubs */
            if ((strcmp(fn,"assert")==0||strcmp(fn,"assertz")==0||strcmp(fn,"asserta")==0)&&arity==1) return 1;
            if ((strcmp(fn,"retract")==0||strcmp(fn,"retractall")==0||strcmp(fn,"abolish")==0)&&arity==1) return 1;
            /* U-23: nv_get(+Name, -Val) / nv_set(+Name, +Val) -- SNO NV store bridge */
            if (strcmp(fn,"nv_get")==0&&arity==2) {
                Term *nm=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                if (!nm || nm->tag != TT_ATOM) return 0;
                const char *nm_str = prolog_atom_name(nm->atom_id);
                DESCR_t dv = NV_GET_fn(nm_str);
                Term *val_t = IS_FAIL_fn(dv) ? term_new_atom(ATOM_NIL) :
                              (dv.v==DT_I) ? term_new_int(dv.i) :
                              term_new_atom(prolog_atom_intern(dv.s ? dv.s : ""));
                Term *lhs=pl_unified_term_from_expr(goal->children[1],env);
                int mark=trail_mark(trail);
                if(!unify(lhs,val_t,trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            if (strcmp(fn,"nv_set")==0&&arity==2) {
                Term *nm=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                Term *vl=term_deref(pl_unified_term_from_expr(goal->children[1],env));
                if (!nm || nm->tag != TT_ATOM) return 0;
                const char *nm_str = prolog_atom_name(nm->atom_id);
                const char *vl_str = (vl && vl->tag==TT_ATOM) ? prolog_atom_name(vl->atom_id) : NULL;
                DESCR_t dv = (vl && vl->tag==TT_INT) ? INTVAL(vl->ival) :
                             vl_str                   ? STRVAL(vl_str) : NULVCL;
                NV_SET_fn(nm_str, dv);
                return 1;
            }
            /* findall/3 — collect ALL solutions via bb_broker retry loop */
            if (strcmp(fn,"findall")==0&&arity==3){
                EXPR_t *tmpl_expr=goal->children[0];
                EXPR_t *goal_expr=goal->children[1];
                EXPR_t *list_expr=goal->children[2];
                Term **solutions=NULL; int nsol=0,sol_cap=0;
                /* Isolate in sub-trail so bindings don't leak to parent */
                Trail fa_trail; trail_init(&fa_trail);
                Trail saved_global_trail=g_pl_trail;  /* save by value — NOT pointer (self-alias bug) */
                g_pl_trail=fa_trail;
                /* Build a box for the goal and drive α/β to exhaustion */
                bb_node_t goal_box=pl_box_goal_from_ir(goal_expr,env);
                DESCR_t fa_r=goal_box.fn(goal_box.ζ,α);
                while(!IS_FAIL_fn(fa_r)){
                    Term *snap=pl_unified_deep_copy(pl_unified_term_from_expr(tmpl_expr,env));
                    if(nsol>=sol_cap){sol_cap=sol_cap?sol_cap*2:8;solutions=realloc(solutions,sol_cap*sizeof(Term*));}
                    solutions[nsol++]=snap;
                    fa_r=goal_box.fn(goal_box.ζ,β);
                }
                g_pl_trail=saved_global_trail;  /* restore parent trail */
                int nil_id=prolog_atom_intern("[]"),dot_id=prolog_atom_intern(".");
                Term *lst=term_new_atom(nil_id);
                for(int i=nsol-1;i>=0;i--){Term *a2[2];a2[0]=solutions[i];a2[1]=lst;lst=term_new_compound(dot_id,2,a2);}
                free(solutions);
                Term *list_term=pl_unified_term_from_expr(list_expr,env);
                int u_mark=trail_mark(trail);
                if(!unify(list_term,lst,trail)){trail_unwind(trail,u_mark);return 0;}
                return 1;
            }
            fprintf(stderr,"prolog: undefined predicate %s/%d\n",fn,arity);
            return 0;
        }
        default: return 1;
    }
}

