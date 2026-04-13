/*
 * icon_interp.c -- Icon IR interpreter
 * Mirrors icn_emit_file/icn_emit_expr in emit_x64.c one-to-one.
 * Generator support: icn_collect() drives E_TO/binops as value streams
 * (cross-product), mirroring the alpha/beta Byrd box wiring in the emitter.
 */
#include "icon_interp.h"
#include "icon_lex.h"
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

/* ── Scan state (mirrors icn_subject/icn_pos BSS in emitter) ─────────────── */
static const char *g_scan_subj = NULL;
static int         g_scan_pos  = 0;   /* 1-based, 0 = no scan active */
#define SCAN_STACK_MAX 16
static struct { const char *subj; int pos; } g_scan_stack[SCAN_STACK_MAX];
static int g_scan_depth = 0;

/* ── Loop-break machinery (mirrors loop_push/loop_pop in emitter) ─────────── */
#define LOOP_STACK_MAX 16
static int g_loop_break[LOOP_STACK_MAX];  /* 1 = break signalled */
static int g_loop_depth = 0;
#define LOOP_PUSH() do { if(g_loop_depth<LOOP_STACK_MAX) g_loop_break[g_loop_depth++]=0; } while(0)
#define LOOP_POP()  do { if(g_loop_depth>0) g_loop_depth--; } while(0)
#define LOOP_BREAK_SET() do { if(g_loop_depth>0) g_loop_break[g_loop_depth-1]=1; } while(0)
#define LOOP_BREAK_CHK() (g_loop_depth>0 && g_loop_break[g_loop_depth-1])

/* ── Return value for early return from procedures ───────────────────────── */
static IcnVal g_return_val;
static int    g_returning = 0;  /* 1 = return in progress */

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

            /* ── Scan-context builtins — mirror icn_any/many/upto/find/match/move/tab ── */
            if (!strcmp(fn,"any") && nargs==1 && g_scan_pos>0) {
                IcnVal cs; icn_exec(e->children[1],env,nenv,&cs);
                const char *s=g_scan_subj; int p=g_scan_pos-1; /* 0-based */
                if (!s||!cs.sval||p>=(int)strlen(s)) return 0;
                if (!strchr(cs.sval, s[p])) return 0;
                g_scan_pos++; *out=icn_int(g_scan_pos); return 1;
            }
            if (!strcmp(fn,"many") && nargs==1 && g_scan_pos>0) {
                IcnVal cs; icn_exec(e->children[1],env,nenv,&cs);
                const char *s=g_scan_subj; int p=g_scan_pos-1;
                if (!s||!cs.sval||p>=(int)strlen(s)||!strchr(cs.sval,s[p])) return 0;
                while(p<(int)strlen(s) && strchr(cs.sval,s[p])) p++;
                g_scan_pos=p+1; *out=icn_int(g_scan_pos); return 1;
            }
            if (!strcmp(fn,"upto") && nargs==1 && g_scan_pos>0) {
                IcnVal cs; icn_exec(e->children[1],env,nenv,&cs);
                const char *s=g_scan_subj; int p=g_scan_pos-1;
                if (!s||!cs.sval) return 0;
                while(p<(int)strlen(s) && !strchr(cs.sval,s[p])) p++;
                if(p>=(int)strlen(s)) return 0;
                g_scan_pos=p+1; *out=icn_int(g_scan_pos); return 1;
            }
            if (!strcmp(fn,"move") && nargs==1 && g_scan_pos>0) {
                IcnVal n_; icn_exec(e->children[1],env,nenv,&n_);
                int newp = g_scan_pos + (int)n_.ival;
                if (!g_scan_subj||newp<1||newp>(int)strlen(g_scan_subj)+1) return 0;
                int old=g_scan_pos; g_scan_pos=newp;
                *out=icn_str(strndup(g_scan_subj+old-1,(int)n_.ival)); return 1;
            }
            if (!strcmp(fn,"tab") && nargs==1 && g_scan_pos>0) {
                IcnVal n_; icn_exec(e->children[1],env,nenv,&n_);
                int newp=(int)n_.ival;
                if (!g_scan_subj||newp<g_scan_pos||newp>(int)strlen(g_scan_subj)+1) return 0;
                int old=g_scan_pos; g_scan_pos=newp;
                *out=icn_str(strndup(g_scan_subj+old-1,newp-old)); return 1;
            }
            if (!strcmp(fn,"find") && nargs==2) {
                IcnVal s1,s2; icn_exec(e->children[1],env,nenv,&s1); icn_exec(e->children[2],env,nenv,&s2);
                const char *needle=s1.sval?s1.sval:""; const char *hay=s2.sval?s2.sval:"";
                char *p=strstr(hay,needle);
                if(!p) return 0;
                *out=icn_int((long)(p-hay)+1); return 1;
            }
            if (!strcmp(fn,"match") && nargs>0 && g_scan_pos>0) {
                IcnVal s1; icn_exec(e->children[1],env,nenv,&s1);
                const char *needle=s1.sval?s1.sval:"";
                const char *hay=g_scan_subj?g_scan_subj:"";
                int p=g_scan_pos-1; int nl=strlen(needle);
                if(strncmp(hay+p,needle,nl)!=0) return 0;
                g_scan_pos+=nl; *out=icn_int(g_scan_pos); return 1;
            }
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

        /* ── E_RETURN: return [expr] from procedure ─────────────────────── */
        case E_RETURN: {
            if (e->nchildren > 0) icn_exec(e->children[0], env, nenv, &g_return_val);
            else g_return_val = icn_null();
            g_returning = 1;
            *out = g_return_val; return 1;
        }

        /* ── E_LOOP_BREAK: break [expr] from loop ───────────────────────── */
        case E_LOOP_BREAK: {
            if (e->nchildren > 0) icn_exec(e->children[0], env, nenv, out);
            else *out = icn_null();
            LOOP_BREAK_SET();
            return 1;
        }

        /* ── E_NOT: not E — succeed iff E fails ────────────────────────── */
        case E_NOT: {
            IcnVal v;
            int r = (e->nchildren > 0) ? icn_exec(e->children[0], env, nenv, &v) : 0;
            if (r) return 0;
            *out = icn_null(); return 1;
        }

        /* ── E_REPEAT: repeat body — loop until break ───────────────────── */
        case E_REPEAT: {
            EXPR_t *body = (e->nchildren > 0) ? e->children[0] : NULL;
            LOOP_PUSH();
            while (!LOOP_BREAK_CHK()) {
                if (!body) continue;
                IcnVal v; icn_exec(body, env, nenv, &v);
                if (g_returning) break;
            }
            LOOP_POP();
            *out = icn_null(); return 1;
        }

        /* ── E_UNTIL: until cond do body ────────────────────────────────── */
        case E_UNTIL: {
            EXPR_t *cond = (e->nchildren > 0) ? e->children[0] : NULL;
            EXPR_t *body = (e->nchildren > 1) ? e->children[1] : NULL;
            LOOP_PUSH();
            while (!LOOP_BREAK_CHK()) {
                IcnVal cv;
                if (cond && icn_exec(cond, env, nenv, &cv)) break; /* cond succeeded → exit */
                if (g_returning) break;
                if (body) { IcnVal bv; icn_exec(body, env, nenv, &bv); }
                if (g_returning || LOOP_BREAK_CHK()) break;
            }
            LOOP_POP();
            *out = icn_null(); return 1;
        }

        /* ── E_AUGOP: x op:= y  ─────────────────────────────────────────── */
        case E_AUGOP: {
            if (e->nchildren < 2) { *out = icn_null(); return 0; }
            EXPR_t *lhs = e->children[0];
            EXPR_t *rhs_e = e->children[1];
            IcnVal lval, rval;
            if (!icn_exec(lhs, env, nenv, &lval)) return 0;
            if (!icn_exec(rhs_e, env, nenv, &rval)) return 0;
            /* Map ival (IcnTkKind) to binop index */
            int op = -1; int is_mod=0; int is_cat=0;
            IcnTkKind tk=(IcnTkKind)e->ival;
            if      (tk==TK_AUGPLUS)   op=0;
            else if (tk==TK_AUGMINUS)  op=1;
            else if (tk==TK_AUGSTAR)   op=2;
            else if (tk==TK_AUGSLASH)  op=3;
            else if (tk==TK_AUGMOD)  { op=3; is_mod=1; }
            else if (tk==TK_AUGCONCAT){ op=0; is_cat=1; }
            else op=0;
            IcnVal result;
            if (is_cat) {
                char *buf = malloc(512);
                snprintf(buf, 512, "%s%s",
                    lval.is_str?(lval.sval?lval.sval:""):"",
                    rval.is_str?(rval.sval?rval.sval:""):"");
                result = icn_str(buf);
            } else if (is_mod) {
                long d = rval.ival; result = d ? icn_int(lval.ival % d) : icn_int(0);
            } else {
                int ok = icn_binop(op, lval, rval, &result);
                if (!ok) return 0;
            }
            /* Store back to lhs variable */
            if (lhs->kind == E_VAR) {
                int slot = (int)lhs->ival;
                if (slot >= 0 && slot < nenv) env[slot] = result;
            }
            *out = result; return 1;
        }

        /* ── String relational ops (E_LEQ == etc.) ────────────────────────
         * Mirror emit_seq / emit_strrelop in emitter.
         * Succeed and return rhs when comparison holds. */
        case E_LEQ: { /* == string eq */
            IcnVal l, r;
            if (!icn_exec(e->children[0],env,nenv,&l)) return 0;
            if (!icn_exec(e->children[1],env,nenv,&r)) return 0;
            const char *ls = l.is_str?(l.sval?l.sval:""):"";
            const char *rs = r.is_str?(r.sval?r.sval:""):"";
            if (strcmp(ls,rs)!=0) return 0;
            *out = r; return 1;
        }
        case E_LNE: { /* ~== string ne */
            IcnVal l, r;
            if (!icn_exec(e->children[0],env,nenv,&l)) return 0;
            if (!icn_exec(e->children[1],env,nenv,&r)) return 0;
            const char *ls = l.is_str?(l.sval?l.sval:""):"";
            const char *rs = r.is_str?(r.sval?r.sval:""):"";
            if (strcmp(ls,rs)==0) return 0;
            *out = r; return 1;
        }
        case E_LLT: case E_LLE: case E_LGT: case E_LGE: {
            IcnVal l, r;
            if (!icn_exec(e->children[0],env,nenv,&l)) return 0;
            if (!icn_exec(e->children[1],env,nenv,&r)) return 0;
            const char *ls = l.is_str?(l.sval?l.sval:""):"";
            const char *rs = r.is_str?(r.sval?r.sval:""):"";
            int cmp = strcmp(ls,rs);
            int ok = (e->kind==E_LLT)?(cmp<0):(e->kind==E_LLE)?(cmp<=0):
                     (e->kind==E_LGT)?(cmp>0):(cmp>=0);
            if (!ok) return 0;
            *out = r; return 1;
        }

        /* ── E_SCAN: s ? expr — string scanning ─────────────────────────── */
        case E_SCAN: {
            if (e->nchildren < 1) { *out = icn_null(); return 0; }
            IcnVal subj;
            if (!icn_exec(e->children[0], env, nenv, &subj)) return 0;
            /* Push scan state */
            if (g_scan_depth < SCAN_STACK_MAX) {
                g_scan_stack[g_scan_depth].subj = g_scan_subj;
                g_scan_stack[g_scan_depth].pos  = g_scan_pos;
                g_scan_depth++;
            }
            g_scan_subj = subj.is_str ? (subj.sval ? subj.sval : "") : "";
            g_scan_pos  = 1;
            int r = 0;
            if (e->nchildren >= 2) r = icn_exec(e->children[1], env, nenv, out);
            else { *out = icn_null(); r = 1; }
            /* Pop scan state */
            if (g_scan_depth > 0) {
                g_scan_depth--;
                g_scan_subj = g_scan_stack[g_scan_depth].subj;
                g_scan_pos  = g_scan_stack[g_scan_depth].pos;
            }
            return r;
        }

        /* ── E_ITERATE: !E generate list/string elements ─────────────────── */
        case E_ITERATE: {
            /* Simplified: for lists not yet implemented, just fail */
            *out = icn_null(); return 0;
        }
        /* E_IF: if cond then E2 [else E3] — cond is a goal (succeeds/fails) */
        case E_IF: {
            if (e->nchildren < 1) { *out=icn_null(); return 1; }
            EXPR_t *cond  = e->children[0];
            EXPR_t *thenb = (e->nchildren>1) ? e->children[1] : NULL;
            EXPR_t *elseb = (e->nchildren>2) ? e->children[2] : NULL;
            IcnVal cv;
            if (icn_exec(cond, env, nenv, &cv)) {
                if (thenb) return icn_exec(thenb, env, nenv, out);
                *out=cv; return 1;
            } else {
                if (elseb) return icn_exec(elseb, env, nenv, out);
                *out=icn_null(); return 0;
            }
        }
        /* E_GLOBAL: local/global decl — no-op at runtime */
        case E_GLOBAL:
            *out=icn_null(); return 1;
        /* E_KEYWORD: &subject, &pos */
        case E_KEYWORD: {
            if (!e->sval){*out=icn_null();return 1;}
            if (strcmp(e->sval,"subject")==0) { *out=icn_str(g_scan_subj?g_scan_subj:""); return 1; }
            if (strcmp(e->sval,"pos")==0)     { *out=icn_int(g_scan_pos); return 1; }
            *out=icn_null(); return 1;
        }
        default:
            *out=icn_null(); return 1;
    }
}

/* =========================================================================
 * Scope: name→slot map for a single procedure frame.
 * Built once per call from param names + local decl names found in body.
 * ========================================================================= */
#define SCOPE_MAX 64
typedef struct { const char *name; int slot; } ScopeEntry;
typedef struct { ScopeEntry e[SCOPE_MAX]; int n; } Scope;

static int scope_add(Scope *sc, const char *name) {
    if (!name) return -1;
    for (int i=0;i<sc->n;i++) if(strcmp(sc->e[i].name,name)==0) return sc->e[i].slot;
    if (sc->n >= SCOPE_MAX) return -1;
    int slot = sc->n;
    sc->e[sc->n].name = name; sc->e[sc->n].slot = slot; sc->n++;
    return slot;
}
static int scope_get(const Scope *sc, const char *name) {
    if (!name) return -1;
    for (int i=0;i<sc->n;i++) if(strcmp(sc->e[i].name,name)==0) return sc->e[i].slot;
    return -1;
}
static void scope_patch(Scope *sc, EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_GLOBAL) {
        for (int i=0;i<e->nchildren;i++) if(e->children[i]&&e->children[i]->sval)
            scope_add(sc, e->children[i]->sval);
        return;
    }
    if (e->kind == E_VAR && e->sval) { int s=scope_get(sc,e->sval); if(s>=0) e->ival=s; }
    for (int i=0;i<e->nchildren;i++) scope_patch(sc, e->children[i]);
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

    /* Build name→slot scope: params first, then locals from E_GLOBAL decls */
    Scope sc; sc.n=0;
    for(int i=0;i<nparams&&i<ICN_ENV_MAX;i++) {
        EXPR_t *pn=proc->children[1+i];
        if(pn&&pn->sval) scope_add(&sc, pn->sval);
    }
    int body_start=1+nparams;
    int nbody=proc->nchildren-body_start;
    for(int i=0;i<nbody;i++) {
        EXPR_t *stmt=proc->children[body_start+i];
        if(stmt&&stmt->kind==E_GLOBAL)
            for(int j=0;j<stmt->nchildren;j++) if(stmt->children[j]&&stmt->children[j]->sval)
                scope_add(&sc, stmt->children[j]->sval);
    }
    for(int i=0;i<nbody;i++) scope_patch(&sc, proc->children[body_start+i]);

    /* Load param values */
    for(int i=0;i<nparams&&i<nargs&&i<ICN_ENV_MAX;i++) env[i]=args[i];

    /* Execute body; stop on return — mirrors emit_choice proc body α/β */
    IcnVal v=icn_null();
    int prev_returning=g_returning; g_returning=0;
    for(int i=0;i<nbody;i++) {
        EXPR_t *stmt=proc->children[body_start+i];
        if(stmt&&stmt->kind==E_GLOBAL) continue;
        icn_exec(stmt,env,nenv,&v);
        if (g_returning) { v=g_return_val; g_returning=0; break; }
    }
    g_returning=prev_returning;
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
