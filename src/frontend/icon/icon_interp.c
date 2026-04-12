/*
 * icon_interp.c -- Icon IR interpreter
 * Mirrors icn_emit_file/icn_emit_expr in emit_x64.c one-to-one.
 * Generator support: icn_collect() drives E_TO/binops as value streams
 * (cross-product), mirroring the alpha/beta Byrd box wiring in the emitter.
 */
#include "icon_interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int is_str; long ival; const char *sval; } IcnVal;
static IcnVal icn_int(long v)        { IcnVal r={0,v,NULL}; return r; }
static IcnVal icn_str(const char *s) { IcnVal r={1,0,s};    return r; }
static IcnVal icn_null(void)         { IcnVal r={0,0,NULL}; return r; }

static void icn_write(IcnVal v) {
    if (v.is_str) printf("%s\n", v.sval ? v.sval : "");
    else          printf("%ld\n", v.ival);
}

#define MAX_PROCS 128
static struct { const char *name; EXPR_t *proc; } g_procs[MAX_PROCS];
static int g_nprocs = 0;
static EXPR_t *icn_lookup(const char *n) {
    for (int i=0;i<g_nprocs;i++) if(strcmp(g_procs[i].name,n)==0) return g_procs[i].proc;
    return NULL;
}

#define ICN_ENV_MAX 64
static int  icn_exec(EXPR_t *e, IcnVal *env, int nenv, IcnVal *out);
static int  icn_call(EXPR_t *proc, IcnVal *args, int nargs, IcnVal *out);

/* =========================================================================
 * icn_binop -- apply binary op; returns 1=success 0=fail (for comparisons)
 * Mirrors the comparison/arithmetic logic in icn_emit_expr.
 * ========================================================================= */
static int icn_binop(int op, IcnVal l, IcnVal r, IcnVal *res) {
    switch(op) {
        case 0: *res=icn_int(l.ival+r.ival); return 1;
        case 1: *res=icn_int(l.ival-r.ival); return 1;
        case 2: *res=icn_int(l.ival*r.ival); return 1;
        case 3: *res=r.ival?icn_int(l.ival/r.ival):icn_int(0); return 1;
        /* Comparisons succeed and return RHS (Icon semantics) */
        case 4: if(l.ival< r.ival){*res=r;return 1;} return 0;
        case 5: if(l.ival<=r.ival){*res=r;return 1;} return 0;
        case 6: if(l.ival> r.ival){*res=r;return 1;} return 0;
        case 7: if(l.ival>=r.ival){*res=r;return 1;} return 0;
        case 8: if(l.ival==r.ival){*res=r;return 1;} return 0;
        case 9: if(l.ival!=r.ival){*res=r;return 1;} return 0;
    }
    *res=icn_null(); return 1;
}

/* =========================================================================
 * icn_collect -- drive e as a generator, collect all values into out[0..cap-1]
 * Returns count of values.
 * Mirrors the alpha/beta Byrd box wiring: E_TO loops, binops cross-product.
 * ========================================================================= */
#define MAX_GEN 4096
static int icn_collect(EXPR_t *e, IcnVal *env, int nenv, IcnVal *out, int cap) {
    if (!e || cap <= 0) return 0;
    int n = 0;

    /* E_TO: i to j -- generate i,i+1,...,j
     * lo and hi may themselves be generators (e.g. (1 to 2) to (2 to 3)) */
    if (e->kind == E_TO && e->nchildren >= 2) {
        IcnVal lobuf[MAX_GEN], hibuf[MAX_GEN];
        int nlo=icn_collect(e->children[0],env,nenv,lobuf,MAX_GEN);
        if (!nlo){IcnVal v;if(icn_exec(e->children[0],env,nenv,&v)){lobuf[0]=v;nlo=1;}}
        int nhi=icn_collect(e->children[1],env,nenv,hibuf,MAX_GEN);
        if (!nhi){IcnVal v;if(icn_exec(e->children[1],env,nenv,&v)){hibuf[0]=v;nhi=1;}}
        for (int li=0;li<nlo&&n<cap;li++)
            for (int hi_i=0;hi_i<nhi&&n<cap;hi_i++)
                for (long i=lobuf[li].ival; i<=hibuf[hi_i].ival&&n<cap; i++)
                    out[n++]=icn_int(i);
        return n;
    }
    /* E_TO_BY: i to j by k */
    if (e->kind == E_TO_BY && e->nchildren >= 3) {
        IcnVal lo,hi,st;
        icn_exec(e->children[0],env,nenv,&lo);
        icn_exec(e->children[1],env,nenv,&hi);
        icn_exec(e->children[2],env,nenv,&st);
        long s=st.ival?st.ival:1;
        if(s>0) for(long i=lo.ival;i<=hi.ival&&n<cap;i+=s) out[n++]=icn_int(i);
        else    for(long i=lo.ival;i>=hi.ival&&n<cap;i+=s) out[n++]=icn_int(i);
        return n;
    }
    /* Binary op: cross-product of lhs x rhs generators */
    int op=-1;
    switch(e->kind){
        case E_ADD:op=0;break; case E_SUB:op=1;break;
        case E_MUL:op=2;break; case E_DIV:op=3;break;
        case E_LT: op=4;break; case E_LE: op=5;break;
        case E_GT: op=6;break; case E_GE: op=7;break;
        case E_EQ: op=8;break; case E_NE: op=9;break;
        default:break;
    }
    if (op>=0 && e->nchildren>=2) {
        IcnVal lbuf[MAX_GEN], rbuf[MAX_GEN];
        int nl=icn_collect(e->children[0],env,nenv,lbuf,MAX_GEN);
        if (!nl) { IcnVal v; if(icn_exec(e->children[0],env,nenv,&v)){lbuf[0]=v;nl=1;} }
        int nr=icn_collect(e->children[1],env,nenv,rbuf,MAX_GEN);
        if (!nr) { IcnVal v; if(icn_exec(e->children[1],env,nenv,&v)){rbuf[0]=v;nr=1;} }
        for (int li=0;li<nl&&n<cap;li++)
            for (int ri=0;ri<nr&&n<cap;ri++) {
                IcnVal res; if(icn_binop(op,lbuf[li],rbuf[ri],&res)) out[n++]=res;
            }
        return n;
    }
    /* Scalar: evaluate once */
    IcnVal v;
    if (icn_exec(e,env,nenv,&v)) out[n++]=v;
    return n;
}

/* =========================================================================
 * icn_exec -- evaluate one EXPR_t node, return 1=success 0=fail
 * Mirrors icn_emit_expr() in emit_x64.c.
 * ========================================================================= */
static int icn_exec(EXPR_t *e, IcnVal *env, int nenv, IcnVal *out) {
    if (!e) { *out=icn_null(); return 1; }
    switch (e->kind) {
        case E_ILIT: *out=icn_int((long)e->ival); return 1;
        case E_QLIT: *out=icn_str(e->sval?e->sval:""); return 1;
        case E_CSET: *out=icn_str(e->sval?e->sval:""); return 1;
        case E_NUL:  *out=icn_null(); return 1;
        case E_FLIT: *out=icn_int((long)e->dval); return 1;

        case E_VAR: {
            int slot=(int)e->ival;
            *out=(slot>=0&&slot<nenv)?env[slot]:icn_null(); return 1;
        }
        case E_ASSIGN: {
            if (e->nchildren<2){*out=icn_null();return 1;}
            IcnVal rhs;
            if (!icn_exec(e->children[1],env,nenv,&rhs)) return 0;
            if (e->children[0]->kind==E_VAR) {
                int slot=(int)e->children[0]->ival;
                if(slot>=0&&slot<nenv) env[slot]=rhs;
            }
            *out=rhs; return 1;
        }
        case E_MNS: {
            IcnVal v; if(!icn_exec(e->children[0],env,nenv,&v)) return 0;
            *out=icn_int(-v.ival); return 1;
        }
        case E_CAT: case E_LCONCAT: {
            IcnVal l,r;
            icn_exec(e->children[0],env,nenv,&l);
            icn_exec(e->children[1],env,nenv,&r);
            char *buf=malloc(512);
            snprintf(buf,512,"%s%s",
                l.is_str?(l.sval?l.sval:""):"",
                r.is_str?(r.sval?r.sval:""):"");
            *out=icn_str(buf); return 1;
        }
        /* Arithmetic/comparison: scalar context (first value only) */
        case E_ADD: case E_SUB: case E_MUL: case E_DIV:
        case E_LT:  case E_LE:  case E_GT:  case E_GE:
        case E_EQ:  case E_NE: {
            int op; switch(e->kind){
                case E_ADD:op=0;break; case E_SUB:op=1;break;
                case E_MUL:op=2;break; case E_DIV:op=3;break;
                case E_LT: op=4;break; case E_LE: op=5;break;
                case E_GT: op=6;break; case E_GE: op=7;break;
                case E_EQ: op=8;break; default:op=9;break;
            }
            IcnVal l,r;
            if(!icn_exec(e->children[0],env,nenv,&l)) return 0;
            if(!icn_exec(e->children[1],env,nenv,&r)) return 0;
            return icn_binop(op,l,r,out);
        }
        /* E_TO scalar: return lo value */
        case E_TO: case E_TO_BY: {
            if(e->nchildren<1){*out=icn_null();return 1;}
            return icn_exec(e->children[0],env,nenv,out);
        }
        /* E_EVERY -- drive generator; mirror of emit_every */
        case E_EVERY: {
            if (e->nchildren<1){*out=icn_null();return 1;}
            EXPR_t *gen  = e->children[0];
            EXPR_t *body = (e->nchildren>1)?e->children[1]:NULL;

            /* every write(gen): special case -- collect gen, write each */
            if (gen->kind==E_FNC && gen->nchildren>=2 &&
                gen->children[0]->sval &&
                strcmp(gen->children[0]->sval,"write")==0) {
                IcnVal buf[MAX_GEN]; int n=0;
                EXPR_t *arg=gen->children[1];
                n=icn_collect(arg,env,nenv,buf,MAX_GEN);
                if (!n) { IcnVal v; if(icn_exec(arg,env,nenv,&v)){buf[0]=v;n=1;} }
                for (int i=0;i<n;i++) icn_write(buf[i]);
                *out=icn_null(); return 1;
            }
            /* General every: collect gen values, run body for each */
            IcnVal buf[MAX_GEN]; int n=icn_collect(gen,env,nenv,buf,MAX_GEN);
            if (!n) { IcnVal v; if(icn_exec(gen,env,nenv,&v)){buf[0]=v;n=1;} }
            IcnVal bv=icn_null();
            for (int i=0;i<n;i++)
                if (body) icn_exec(body,env,nenv,&bv);
            *out=icn_null(); return 1;
        }
        /* E_WHILE -- while E [do body]; mirror of emit_while */
        case E_WHILE: {
            IcnVal cond,bv;
            while (icn_exec(e->children[0],env,nenv,&cond))
                if (e->nchildren>1) icn_exec(e->children[1],env,nenv,&bv);
            *out=icn_null(); return 1;
        }
        /* E_SUSPEND -- yield (simplified: return value) */
        case E_SUSPEND: {
            if (e->nchildren>0) return icn_exec(e->children[0],env,nenv,out);
            *out=icn_null(); return 1;
        }
        /* E_SEQ: execute children left-to-right, return last */
        case E_SEQ: {
            IcnVal v=icn_null();
            for (int i=0;i<e->nchildren;i++) icn_exec(e->children[i],env,nenv,&v);
            *out=v; return 1;
        }
        /* E_FNC -- function call: children[0]=name(E_VAR), children[1..]=args */
        case E_FNC: {
            if (e->nchildren<1){*out=icn_null();return 1;}
            const char *fn=e->children[0]->sval;
            if (!fn){*out=icn_null();return 0;}
            int nargs=e->nchildren-1;

            /* Builtins -- mirror of emit_call */
            if (!strcmp(fn,"write")) {
                if (nargs==0){printf("\n");*out=icn_null();return 1;}
                IcnVal a; if(!icn_exec(e->children[1],env,nenv,&a)) return 0;
                icn_write(a); *out=a; return 1;
            }
            if (!strcmp(fn,"writes")) {
                if (nargs==0){*out=icn_null();return 1;}
                IcnVal a; icn_exec(e->children[1],env,nenv,&a);
                if(a.is_str) printf("%s",a.sval?a.sval:"");
                else         printf("%ld",a.ival);
                *out=a; return 1;
            }
            if (!strcmp(fn,"read"))  {*out=icn_null();return 1;}
            if (!strcmp(fn,"stop"))  {exit(0);}

            /* User procedure */
            EXPR_t *proc=icn_lookup(fn);
            if (proc) {
                IcnVal args[ICN_ENV_MAX];
                for(int i=0;i<nargs&&i<ICN_ENV_MAX;i++)
                    icn_exec(e->children[i+1],env,nenv,&args[i]);
                return icn_call(proc,args,nargs,out);
            }
            fprintf(stderr,"icon: undefined '%s'\n",fn);
            return 0;
        }
        default:
            *out=icn_null(); return 1;
    }
}

/* =========================================================================
 * icn_call -- call a user procedure; mirror of emit_choice proc body
 * proc->children[0] = E_VAR(name), proc->ival = nparams
 * proc->children[1..nparams] = param nodes
 * proc->children[nparams+1..] = body statements
 * ========================================================================= */
static int icn_call(EXPR_t *proc, IcnVal *args, int nargs, IcnVal *out) {
    IcnVal env[ICN_ENV_MAX]; int nenv=ICN_ENV_MAX;
    memset(env,0,sizeof env);
    int nparams=(int)proc->ival;
    for(int i=0;i<nparams&&i<nargs&&i<ICN_ENV_MAX;i++) env[i]=args[i];
    int body_start=1+nparams;
    int nbody=proc->nchildren-body_start;
    IcnVal v=icn_null();
    for(int i=0;i<nbody;i++) icn_exec(proc->children[body_start+i],env,nenv,&v);
    *out=v; return 1;
}

/* =========================================================================
 * icon_execute_program -- top-level; mirror of icn_emit_file
 * ========================================================================= */
void icon_execute_program(Program *prog) {
    g_nprocs=0;
    for (STMT_t *st=prog->head; st; st=st->next) {
        EXPR_t *proc=st->subject;
        if (!proc||proc->kind!=E_FNC||proc->nchildren<1) continue;
        const char *name=proc->children[0]->sval;
        if (!name) continue;
        if (g_nprocs<MAX_PROCS){ g_procs[g_nprocs].name=name; g_procs[g_nprocs].proc=proc; g_nprocs++; }
    }
    EXPR_t *main_p=icn_lookup("main");
    if (!main_p){fprintf(stderr,"icon: no main\n");return;}
    IcnVal result; icn_call(main_p,NULL,0,&result);
}
