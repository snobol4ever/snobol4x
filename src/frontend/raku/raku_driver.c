/*
 * raku_driver.c — Tiny-Raku direct AST evaluator (Phase 1)
 *
 * Walks the RakuNode* AST and interprets it directly.
 * No IR, no BB_PUMP yet — that's Phase 2 (Rung 6+).
 *
 * Rung 0: say STRING prints. Everything else is a stub.
 * Rung 3: arithmetic, my $x = expr.
 * Rung 4: string concat ~, single-quoted strings.
 * Rung 5: for RANGE -> $var { body }.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#include "raku_driver.h"
#include "raku_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================
 * Value type — simple tagged union for direct eval
 *============================================================*/
typedef enum { VT_INT, VT_FLOAT, VT_STR, VT_NONE } ValType;
typedef struct {
    ValType type;
    long    ival;
    double  dval;
    char   *sval;   /* heap-allocated; eval_node always returns fresh copy */
} Val;

static Val val_int(long v)   { return (Val){ VT_INT,   v,   0.0, NULL }; }
static Val val_float(double v){ return (Val){ VT_FLOAT, 0,   v,   NULL }; }
static Val val_str(const char *s) {
    return (Val){ VT_STR, 0, 0.0, strdup(s) };
}
static Val val_none(void)    { return (Val){ VT_NONE,  0,   0.0, NULL }; }

static void val_free(Val v)  { if (v.type == VT_STR && v.sval) free(v.sval); }

static void val_print(Val v, int newline) {
    switch (v.type) {
        case VT_INT:   printf("%ld",  v.ival); break;
        case VT_FLOAT: printf("%g",   v.dval); break;
        case VT_STR:   printf("%s",   v.sval ? v.sval : ""); break;
        case VT_NONE:  break;
    }
    if (newline) putchar('\n');
}

static long val_to_int(Val v) {
    if (v.type == VT_INT)   return v.ival;
    if (v.type == VT_FLOAT) return (long)v.dval;
    if (v.type == VT_STR && v.sval) return atol(v.sval);
    return 0;
}
static double val_to_float(Val v) {
    if (v.type == VT_FLOAT) return v.dval;
    if (v.type == VT_INT)   return (double)v.ival;
    if (v.type == VT_STR && v.sval) return atof(v.sval);
    return 0.0;
}
static int val_truthy(Val v) {
    if (v.type == VT_INT)   return v.ival != 0;
    if (v.type == VT_FLOAT) return v.dval != 0.0;
    if (v.type == VT_STR)   return v.sval && v.sval[0] != '\0';
    return 0;
}

/*============================================================
 * Variable environment — simple flat symbol table
 *============================================================*/
#define ENV_MAX 256
typedef struct { char *name; Val val; } EnvEntry;
static EnvEntry g_env[ENV_MAX];
static int      g_env_n = 0;

static Val *env_lookup(const char *name) {
    for (int i = g_env_n - 1; i >= 0; i--)
        if (g_env[i].name && strcmp(g_env[i].name, name) == 0)
            return &g_env[i].val;
    return NULL;
}
static void env_set(const char *name, Val v) {
    for (int i = g_env_n - 1; i >= 0; i--)
        if (g_env[i].name && strcmp(g_env[i].name, name) == 0) {
            val_free(g_env[i].val);
            g_env[i].val = v;
            return;
        }
    if (g_env_n < ENV_MAX) {
        g_env[g_env_n].name = strdup(name);
        g_env[g_env_n].val  = v;
        g_env_n++;
    }
}

/*============================================================
 * Forward declarations
 *============================================================*/
static Val  eval_node(RakuNode *n);
static void exec_block(RakuNode *block);

/*============================================================
 * Expression evaluator
 *============================================================*/
static Val eval_node(RakuNode *n) {
    if (!n) return val_none();
    switch (n->kind) {

    /* ── Literals ──────────────────────────────────────────── */
    case RK_INT:   return val_int(n->ival);
    case RK_FLOAT: return val_float(n->dval);
    case RK_STR:   return val_str(n->sval);

    /* ── Variables ─────────────────────────────────────────── */
    case RK_VAR_SCALAR: {
        Val *v = env_lookup(n->sval);
        if (!v) { fprintf(stderr, "raku: undefined variable %s\n", n->sval); return val_none(); }
        /* return a copy */
        if (v->type == VT_STR) return val_str(v->sval);
        return *v;
    }
    case RK_IDENT: {
        Val *v = env_lookup(n->sval);
        if (v) { if (v->type == VT_STR) return val_str(v->sval); return *v; }
        return val_str(n->sval); /* bare word as string */
    }

    /* ── Arithmetic ────────────────────────────────────────── */
    case RK_ADD: { Val l=eval_node(n->left), r=eval_node(n->right);
        Val res = (l.type==VT_FLOAT||r.type==VT_FLOAT)
            ? val_float(val_to_float(l)+val_to_float(r))
            : val_int(val_to_int(l)+val_to_int(r));
        val_free(l); val_free(r); return res; }
    case RK_SUBTRACT: { Val l=eval_node(n->left), r=eval_node(n->right);
        Val res = (l.type==VT_FLOAT||r.type==VT_FLOAT)
            ? val_float(val_to_float(l)-val_to_float(r))
            : val_int(val_to_int(l)-val_to_int(r));
        val_free(l); val_free(r); return res; }
    case RK_MUL: { Val l=eval_node(n->left), r=eval_node(n->right);
        Val res = (l.type==VT_FLOAT||r.type==VT_FLOAT)
            ? val_float(val_to_float(l)*val_to_float(r))
            : val_int(val_to_int(l)*val_to_int(r));
        val_free(l); val_free(r); return res; }
    case RK_DIV: { Val l=eval_node(n->left), r=eval_node(n->right);
        Val res = val_float(val_to_float(l)/val_to_float(r));
        val_free(l); val_free(r); return res; }
    case RK_IDIV: { Val l=eval_node(n->left), r=eval_node(n->right);
        long ri = val_to_int(r);
        Val res = ri ? val_int(val_to_int(l)/ri) : val_int(0);
        val_free(l); val_free(r); return res; }
    case RK_MOD: { Val l=eval_node(n->left), r=eval_node(n->right);
        long ri = val_to_int(r);
        Val res = ri ? val_int(val_to_int(l)%ri) : val_int(0);
        val_free(l); val_free(r); return res; }
    case RK_NEG: { Val v=eval_node(n->left);
        Val res = (v.type==VT_FLOAT) ? val_float(-v.dval) : val_int(-val_to_int(v));
        val_free(v); return res; }

    /* ── String concat ─────────────────────────────────────── */
    case RK_STRCAT: {
        Val l=eval_node(n->left), r=eval_node(n->right);
        char buf[4096];
        char ls[2048]="", rs[2048]="";
        if (l.type==VT_STR)        snprintf(ls,sizeof ls,"%s",l.sval?l.sval:"");
        else if (l.type==VT_INT)   snprintf(ls,sizeof ls,"%ld",l.ival);
        else if (l.type==VT_FLOAT) snprintf(ls,sizeof ls,"%g",l.dval);
        if (r.type==VT_STR)        snprintf(rs,sizeof rs,"%s",r.sval?r.sval:"");
        else if (r.type==VT_INT)   snprintf(rs,sizeof rs,"%ld",r.ival);
        else if (r.type==VT_FLOAT) snprintf(rs,sizeof rs,"%g",r.dval);
        snprintf(buf,sizeof buf,"%s%s",ls,rs);
        val_free(l); val_free(r);
        return val_str(buf);
    }

    /* ── Comparisons ───────────────────────────────────────── */
    case RK_EQ: { Val l=eval_node(n->left),r=eval_node(n->right);
        int ok=(l.type==VT_STR&&r.type==VT_STR)
            ? (l.sval&&r.sval&&strcmp(l.sval,r.sval)==0)
            : val_to_float(l)==val_to_float(r);
        val_free(l);val_free(r); return val_int(ok); }
    case RK_NE: { Val l=eval_node(n->left),r=eval_node(n->right);
        int ok=(l.type==VT_STR&&r.type==VT_STR)
            ? !(l.sval&&r.sval&&strcmp(l.sval,r.sval)==0)
            : val_to_float(l)!=val_to_float(r);
        val_free(l);val_free(r); return val_int(ok); }
    case RK_LT: { Val l=eval_node(n->left),r=eval_node(n->right);
        Val res=val_int(val_to_float(l)<val_to_float(r));
        val_free(l);val_free(r); return res; }
    case RK_GT: { Val l=eval_node(n->left),r=eval_node(n->right);
        Val res=val_int(val_to_float(l)>val_to_float(r));
        val_free(l);val_free(r); return res; }
    case RK_LE: { Val l=eval_node(n->left),r=eval_node(n->right);
        Val res=val_int(val_to_float(l)<=val_to_float(r));
        val_free(l);val_free(r); return res; }
    case RK_GE: { Val l=eval_node(n->left),r=eval_node(n->right);
        Val res=val_int(val_to_float(l)>=val_to_float(r));
        val_free(l);val_free(r); return res; }
    case RK_SEQ: { Val l=eval_node(n->left),r=eval_node(n->right);
        int ok=l.sval&&r.sval&&strcmp(l.sval,r.sval)==0;
        val_free(l);val_free(r); return val_int(ok); }
    case RK_SNE: { Val l=eval_node(n->left),r=eval_node(n->right);
        int ok=!(l.sval&&r.sval&&strcmp(l.sval,r.sval)==0);
        val_free(l);val_free(r); return val_int(ok); }
    case RK_AND: { Val l=eval_node(n->left);
        if (!val_truthy(l)) { val_free(l); return val_int(0); }
        val_free(l); Val r=eval_node(n->right); return r; }
    case RK_OR:  { Val l=eval_node(n->left);
        if (val_truthy(l)) return l;
        val_free(l); Val r=eval_node(n->right); return r; }
    case RK_NOT: { Val v=eval_node(n->left);
        int ok=!val_truthy(v); val_free(v); return val_int(ok); }

    /* ── Range (returns a synthetic "range" value — for exec only) ─ */
    case RK_RANGE:
    case RK_RANGE_EX:
        /* Range is consumed by RK_FOR; as standalone just return left */
        return eval_node(n->left);

    /* ── Assignment ────────────────────────────────────────── */
    case RK_ASSIGN: {
        Val v = eval_node(n->left);
        env_set(n->sval, v);
        if (v.type == VT_STR) return val_str(v.sval);
        return v;
    }

    /* ── Function call ─────────────────────────────────────── */
    case RK_CALL: {
        /* Phase 1: only built-in say/print recognized as calls */
        Val arg = (n->children && n->children->count > 0)
            ? eval_node(n->children->items[0]) : val_none();
        if (strcmp(n->sval, "say") == 0)   { val_print(arg, 1); val_free(arg); return val_none(); }
        if (strcmp(n->sval, "print") == 0) { val_print(arg, 0); val_free(arg); return val_none(); }
        fprintf(stderr, "raku: unknown function '%s'\n", n->sval);
        val_free(arg); return val_none();
    }

    default:
        return val_none();
    }
}

/*============================================================
 * Statement executor
 *============================================================*/
static void exec_block(RakuNode *block) {
    if (!block || block->kind != RK_BLOCK) return;
    RakuList *stmts = block->children;
    if (!stmts) return;
    for (int i = 0; i < stmts->count; i++) {
        RakuNode *s = stmts->items[i];
        if (!s) continue;
        switch (s->kind) {
        case RK_MY_SCALAR:
        case RK_MY_ARRAY: {
            Val v = eval_node(s->left);
            env_set(s->sval, v);
            break;
        }
        case RK_ASSIGN: {
            Val v = eval_node(s->left);
            env_set(s->sval, v);
            break;
        }
        case RK_SAY: {
            Val v = eval_node(s->left);
            val_print(v, 1);
            val_free(v);
            break;
        }
        case RK_PRINT: {
            Val v = eval_node(s->left);
            val_print(v, 0);
            val_free(v);
            break;
        }
        case RK_IF: {
            Val cond = eval_node(s->left);
            if (val_truthy(cond)) exec_block(s->right);
            else if (s->extra)    exec_block(s->extra);
            val_free(cond);
            break;
        }
        case RK_WHILE: {
            for (;;) {
                Val cond = eval_node(s->left);
                int t = val_truthy(cond);
                val_free(cond);
                if (!t) break;
                exec_block(s->right);
            }
            break;
        }
        case RK_FOR: {
            /* for RANGE -> $var { body } */
            RakuNode *iter  = s->left;
            char     *var   = s->sval;    /* "$x" or NULL for $_ */
            RakuNode *body  = s->extra;
            if (!iter || !body) break;

            if (iter->kind == RK_RANGE || iter->kind == RK_RANGE_EX) {
                Val lo = eval_node(iter->left);
                Val hi = eval_node(iter->right);
                long ilo = val_to_int(lo), ihi = val_to_int(hi);
                if (iter->kind == RK_RANGE_EX) ihi--;
                val_free(lo); val_free(hi);
                for (long j = ilo; j <= ihi; j++) {
                    if (var) env_set(var, val_int(j));
                    env_set("$_", val_int(j));
                    exec_block(body);
                }
            } else {
                /* fallback: eval iterable as single value */
                Val v = eval_node(iter);
                if (var) env_set(var, v);
                env_set("$_", v);
                exec_block(body);
            }
            break;
        }
        case RK_EXPR_STMT: {
            Val v = eval_node(s->left);
            val_free(v);
            break;
        }
        default:
            /* RK_SUBTRACT, RK_GATHER, etc. — stubs for now */
            break;
        }
    }
}

/*============================================================
 * Public API
 *============================================================*/
int raku_eval_direct(RakuNode *program) {
    if (!program) return 1;
    exec_block(program);
    return 0;
}

int raku_run_string(const char *src) {
    RakuNode *prog = raku_parse_string(src);
    if (!prog) { fprintf(stderr, "raku: parse failed\n"); return 1; }
    return raku_eval_direct(prog);
}
