/*
 * raku_lower.c — Tiny-Raku AST (RakuNode) → unified IR (EXPR_t) lowering pass
 *
 * Walks a RakuNode tree produced by raku_parse.c and produces an EXPR_t tree
 * using canonical EKind names from ir/ir.h.  After this pass the RakuNode tree
 * is no longer needed.
 *
 * Mapping policy (RK-3, 2026-04-14):
 *   RK_INT        → E_ILIT
 *   RK_FLOAT      → E_FLIT
 *   RK_STR        → E_QLIT
 *   RK_INTERP_STR → E_CAT chain (split on $var boundaries, RK-12)
 *   RK_VAR_SCALAR → E_VAR  (strip leading $)
 *   RK_VAR_ARRAY  → E_VAR  (strip leading @)
 *   RK_ADD        → E_ADD, RK_SUBTRACT→E_SUB, RK_MUL→E_MUL
 *   RK_DIV        → E_DIV, RK_MOD→E_MOD
 *   RK_NEG        → E_MNS
 *   RK_STRCAT     → E_CAT  (string concatenation ~)
 *   RK_EQ→E_EQ, RK_NE→E_NE, RK_LT→E_LT, RK_GT→E_GT, RK_LE→E_LE, RK_GE→E_GE
 *   RK_SEQ→E_LEQ, RK_SNE→E_LNE  (string eq/ne)
 *   RK_AND        → E_SEQ (short-circuit: seq of two goal-directed exprs)
 *   RK_OR         → E_ALT
 *   RK_NOT        → E_NOT
 *   RK_MY_SCALAR  → E_ASSIGN(E_VAR(name), rhs)
 *   RK_ASSIGN     → E_ASSIGN(E_VAR(name), rhs)
 *   RK_SAY        → E_FNC("write",  [expr])   (reuse Icon write builtin)
 *   RK_PRINT      → E_FNC("writes", [expr])   (no newline variant)
 *   RK_TAKE       → E_SUSPEND(expr)
 *   RK_GATHER     → E_ITERATE(body_seq)        (BB_PUMP)
 *   RK_FOR range  → E_EVERY(E_TO(lo,hi), body)
 *   RK_FOR array  → E_EVERY(E_ITERATE(E_VAR(arr)), body)
 *   RK_IF         → E_IF(cond, then [,else])
 *   RK_GIVEN      → nested E_IF chain (RK-13 given/when smart-match)
 *   RK_WHILE      → E_WHILE(cond, body)
 *   RK_BLOCK      → E_SEQ_EXPR(stmts...)
 *   RK_SUBDEF     → E_FNC(name, params_ignored, body)
 *   RK_CALL       → E_FNC(name, args...)
 *   RK_RETURN     → E_RETURN([expr])
 *   RK_RANGE      → E_TO(lo, hi)   (inclusive)
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */

#include "raku_lower.h"
#include "raku_ast.h"
#include "../../ir/ir.h"
#include "../snobol4/scrip_cc.h"   /* expr_new, expr_add_child, expr_unary,
                                      expr_binary, intern; EXPR_T_DEFINED */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*========================================================================
 * Forward declaration
 *========================================================================*/
static EXPR_t *lower_node(const RakuNode *n);

/*========================================================================
 * Helpers
 *========================================================================*/

/* strip_sigil — return pointer past leading $ or @ in a var name.
 * If no sigil, return s unchanged.  Result points into s (no alloc). */
static const char *strip_sigil(const char *s) {
    if (s && (s[0] == '$' || s[0] == '@')) return s + 1;
    return s;
}

/* leaf_sval — allocate a leaf node with a string payload. */
static EXPR_t *leaf_sval(EKind k, const char *s) {
    EXPR_t *e = expr_new(k);
    e->sval = intern(s);
    return e;
}

/* var_node — E_VAR with sigil stripped. */
static EXPR_t *var_node(const char *name) {
    return leaf_sval(E_VAR, strip_sigil(name));
}

/* fnc_node — E_FNC with name in sval; caller adds children.
 * NOTE: For call nodes (not proc defs), children[0] must be a name E_VAR
 * node — icn_interp_eval reads fn name from children[0]->sval.
 * Use make_call() for calls; use fnc_node()+manual layout for proc defs. */
static EXPR_t *fnc_node(const char *name) {
    return leaf_sval(E_FNC, name);
}

/* make_call — build E_FNC call node with correct icon_lower layout:
 *   sval=name, children[0]=E_VAR(name), children[1..]=args.
 * icn_interp_eval reads fn name from children[0]->sval. */
static EXPR_t *make_call(const char *name) {
    EXPR_t *e = leaf_sval(E_FNC, name);
    EXPR_t *nnode = expr_new(E_VAR);
    nnode->sval = intern(name);
    expr_add_child(e, nnode);   /* children[0] = name node */
    return e;
}

/* lower_block_children — lower every statement in an RK_BLOCK into dst. */
static void lower_block_children(EXPR_t *dst, const RakuNode *block) {
    if (!block || block->kind != RK_BLOCK || !block->children) return;
    for (int i = 0; i < block->children->count; i++)
        expr_add_child(dst, lower_node(block->children->items[i]));
}

/* lower_block — lower an RK_BLOCK node → E_SEQ_EXPR containing its stmts. */
static EXPR_t *lower_block(const RakuNode *block) {
    EXPR_t *seq = expr_new(E_SEQ_EXPR);
    lower_block_children(seq, block);
    return seq;
}

/*========================================================================
 * Main lowering dispatch
 *========================================================================*/

static EXPR_t *lower_node(const RakuNode *n) {
    if (!n) return NULL;
    EXPR_t *e;

    switch (n->kind) {

    /*-- Literals ----------------------------------------------------------*/
    case RK_INT:
        e = expr_new(E_ILIT);
        e->ival = n->ival;
        return e;

    case RK_FLOAT:
        e = expr_new(E_FLIT);
        e->dval = n->dval;
        return e;

    case RK_STR:
        return leaf_sval(E_QLIT, n->sval ? n->sval : "");

    /*-- Interpolated double-quoted string: "hello $name" → E_CAT chain --*/
    case RK_INTERP_STR: {
        const char *s = n->sval ? n->sval : "";
        int len = (int)strlen(s);
        /* Walk string; split on $ident boundaries.
         * Build left-associative E_CAT: ((seg0 ~ seg1) ~ seg2) ...      */
        EXPR_t *result = NULL;
        char litbuf[4096];
        int  litpos = 0;
        int  i = 0;
        while (i < len) {
            if (s[i] == '$' && i+1 < len &&
                (s[i+1] == '_' || (s[i+1] >= 'A' && s[i+1] <= 'Z') ||
                                   (s[i+1] >= 'a' && s[i+1] <= 'z'))) {
                /* flush pending literal */
                if (litpos > 0) {
                    litbuf[litpos] = '\0';
                    EXPR_t *lit = leaf_sval(E_QLIT, litbuf);
                    result = result ? expr_binary(E_CAT, result, lit) : lit;
                    litpos = 0;
                }
                /* consume $identifier */
                i++; /* skip $ */
                char vname[256]; int vlen = 0;
                while (i < len && (s[i] == '_' ||
                       (s[i] >= 'A' && s[i] <= 'Z') ||
                       (s[i] >= 'a' && s[i] <= 'z') ||
                       (s[i] >= '0' && s[i] <= '9'))) {
                    if (vlen < 255) vname[vlen++] = s[i];
                    i++;
                }
                vname[vlen] = '\0';
                EXPR_t *var = leaf_sval(E_VAR, vname);
                result = result ? expr_binary(E_CAT, result, var) : var;
            } else {
                if (litpos < 4095) litbuf[litpos++] = s[i];
                i++;
            }
        }
        /* flush trailing literal */
        if (litpos > 0) {
            litbuf[litpos] = '\0';
            EXPR_t *lit = leaf_sval(E_QLIT, litbuf);
            result = result ? expr_binary(E_CAT, result, lit) : lit;
        }
        return result ? result : leaf_sval(E_QLIT, "");
    }

    /*-- Variables ---------------------------------------------------------*/
    case RK_VAR_SCALAR:
    case RK_VAR_ARRAY:
    case RK_IDENT:
        return var_node(n->sval);

    /*-- Arithmetic --------------------------------------------------------*/
    case RK_ADD:      return expr_binary(E_ADD, lower_node(n->left), lower_node(n->right));
    case RK_SUBTRACT: return expr_binary(E_SUB, lower_node(n->left), lower_node(n->right));
    case RK_MUL:      return expr_binary(E_MUL, lower_node(n->left), lower_node(n->right));
    case RK_DIV:      return expr_binary(E_DIV, lower_node(n->left), lower_node(n->right));
    case RK_MOD:      return expr_binary(E_MOD, lower_node(n->left), lower_node(n->right));
    case RK_NEG:      return expr_unary (E_MNS, lower_node(n->left));
    case RK_NOT:      return expr_unary (E_NOT, lower_node(n->left));

    /*-- String concat (Raku ~) -------------------------------------------*/
    case RK_STRCAT:
        return expr_binary(E_CAT, lower_node(n->left), lower_node(n->right));

    /*-- Numeric comparisons -----------------------------------------------*/
    case RK_EQ: return expr_binary(E_EQ, lower_node(n->left), lower_node(n->right));
    case RK_NE: return expr_binary(E_NE, lower_node(n->left), lower_node(n->right));
    case RK_LT: return expr_binary(E_LT, lower_node(n->left), lower_node(n->right));
    case RK_GT: return expr_binary(E_GT, lower_node(n->left), lower_node(n->right));
    case RK_LE: return expr_binary(E_LE, lower_node(n->left), lower_node(n->right));
    case RK_GE: return expr_binary(E_GE, lower_node(n->left), lower_node(n->right));

    /*-- String comparisons (eq / ne) --------------------------------------*/
    case RK_SEQ: return expr_binary(E_LEQ, lower_node(n->left), lower_node(n->right));
    case RK_SNE: return expr_binary(E_LNE, lower_node(n->left), lower_node(n->right));

    /*-- Logic (short-circuit) ---------------------------------------------*/
    case RK_AND: return expr_binary(E_SEQ, lower_node(n->left), lower_node(n->right));
    case RK_OR:  return expr_binary(E_ALT, lower_node(n->left), lower_node(n->right));

    /*-- Range (standalone) ------------------------------------------------*/
    case RK_RANGE:
    case RK_RANGE_EX:
        /* inclusive range; RK_RANGE_EX (..^) is future — treat as inclusive */
        return expr_binary(E_TO, lower_node(n->left), lower_node(n->right));

    /*-- Assignment: my $x = expr  /  $x = expr ---------------------------*/
    case RK_MY_SCALAR:
    case RK_MY_ARRAY:
    case RK_ASSIGN: {
        EXPR_t *lhs = var_node(n->sval);
        EXPR_t *rhs = lower_node(n->left);   /* rhs expression */
        return expr_binary(E_ASSIGN, lhs, rhs);
    }

    /*-- say / print -------------------------------------------------------*/
    case RK_SAY: {
        /* say expr → E_FNC call: children[0]=E_VAR("write"), children[1]=expr */
        e = make_call("write");
        expr_add_child(e, lower_node(n->left));   /* children[1] = arg */
        return e;
    }
    case RK_PRINT: {
        /* print expr → E_FNC call: children[0]=E_VAR("writes"), children[1]=expr */
        e = make_call("writes");
        expr_add_child(e, lower_node(n->left));   /* children[1] = arg */
        return e;
    }

    /*-- take expr  →  E_SUSPEND(expr) ------------------------------------*/
    case RK_TAKE:
        return expr_unary(E_SUSPEND, lower_node(n->left));

    /*-- return [expr]  →  E_RETURN([expr]) --------------------------------*/
    case RK_RETURN:
        e = expr_new(E_RETURN);
        if (n->left) expr_add_child(e, lower_node(n->left));
        return e;

    /*-- gather { block }  →  E_ITERATE(E_SEQ_EXPR(body)) -----------------
     * gather maps to BB_PUMP — same broker mode as Icon every/generator.
     * The body of gather is lowered as a sequence; E_ITERATE drives it.   */
    case RK_GATHER: {
        EXPR_t *body = lower_block(n->left);   /* n->left = body block */
        return expr_unary(E_ITERATE, body);
    }

    /*-- for RANGE -> $v body  →  explicit while-loop with counter ----------
     *
     * icn_interp_eval's E_EVERY(E_TO) path iterates but never assigns the
     * loop counter to an env slot — so $v would always read zero.
     * We lower to an explicit while-loop instead:
     *
     *   for lo..hi -> $v { body }
     *   →
     *   E_SEQ_EXPR(
     *     E_ASSIGN($v, lo),
     *     E_WHILE(E_LE($v, hi),
     *       E_SEQ_EXPR(body_stmts..., E_ASSIGN($v, E_ADD($v, 1))))
     *   )
     *
     * This uses only E_ASSIGN/E_WHILE/E_LE/E_ADD — all handled by
     * icn_interp_eval — and gives the body correct $v at each iteration.
     *
     * for @arr -> $v: emit E_EVERY(E_ITERATE(arr), body) as before;
     * array iteration variable binding is a Phase 2 enhancement.          */
    case RK_FOR: {
        const RakuNode *iter = n->left;        /* range or array expr */
        const char     *var  = n->sval;        /* "$v" or NULL → "$_" */
        const RakuNode *body = n->extra;       /* body block */
        if (!var) var = "$_";
        /* strip sigil for env slot name */
        const char *vname = (var[0]=='$'||var[0]=='@') ? var+1 : var;

        if (iter && (iter->kind == RK_RANGE || iter->kind == RK_RANGE_EX)) {
            /* Explicit counting while-loop */
            EXPR_t *lo_expr = lower_node(iter->left);
            EXPR_t *hi_expr = lower_node(iter->right);

            /* E_ASSIGN($v, lo) — initialise counter */
            EXPR_t *init = expr_binary(E_ASSIGN, leaf_sval(E_VAR, vname), lo_expr);

            /* E_LE($v, hi) — loop condition */
            EXPR_t *cond = expr_binary(E_LE, leaf_sval(E_VAR, vname), hi_expr);

            /* loop body: original stmts + increment */
            EXPR_t *wbody = expr_new(E_SEQ_EXPR);
            lower_block_children(wbody, body);
            /* E_ASSIGN($v, E_ADD($v, 1)) — increment */
            EXPR_t *one  = expr_new(E_ILIT); one->ival = 1;
            EXPR_t *incr = expr_binary(E_ADD, leaf_sval(E_VAR, vname), one);
            expr_add_child(wbody, expr_binary(E_ASSIGN, leaf_sval(E_VAR, vname), incr));

            EXPR_t *wloop = expr_binary(E_WHILE, cond, wbody);

            /* Wrap init + while in E_SEQ_EXPR */
            EXPR_t *seq = expr_new(E_SEQ_EXPR);
            expr_add_child(seq, init);
            expr_add_child(seq, wloop);
            return seq;
        }

        /* for @arr -> $v: use E_EVERY(E_ITERATE) — variable binding Phase 2 */
        EXPR_t *gen;
        if (iter && (iter->kind == RK_VAR_ARRAY || iter->kind == RK_VAR_SCALAR)) {
            gen = expr_unary(E_ITERATE, var_node(iter->sval));
        } else {
            gen = lower_node(iter);
        }
        EXPR_t *loop_body = expr_new(E_SEQ_EXPR);
        lower_block_children(loop_body, body);
        return expr_binary(E_EVERY, gen, loop_body);
    }

    /*-- if cond then [else] -----------------------------------------------*/
    case RK_IF: {
        e = expr_new(E_IF);
        expr_add_child(e, lower_node(n->left));    /* condition */
        expr_add_child(e, lower_block(n->right));  /* then block */
        if (n->extra) expr_add_child(e, lower_block(n->extra)); /* else */
        return e;
    }

    /*-- given $x { when v { } ... default { } }  →  nested E_IF chain ---
     * Each when becomes E_IF(E_EQ/E_LEQ(topic,val), body, next).
     * RK_WHEN is only produced inside RK_GIVEN; lower_node handles it
     * individually only if the list walk calls it directly.              */
    case RK_GIVEN: {
        EXPR_t *topic = lower_node(n->left);   /* evaluate topic once */
        /* Wrap topic in a temp-assign so it's evaluated once.
         * Simplification: inline topic expr into each comparison —
         * safe because topic is a simple var in well-formed given stmts. */
        int nwhens = n->children ? n->children->count : 0;
        /* Build right-to-left: innermost = default or NULL */
        EXPR_t *chain = NULL;
        if (n->right) {  /* default block */
            chain = lower_block(n->right);
        }
        for (int i = nwhens - 1; i >= 0; i--) {
            RakuNode *w = n->children->items[i];  /* RK_WHEN node */
            RakuNode *val = w->left;
            /* Choose comparison: string literal → E_LEQ, else → E_EQ */
            EKind cmp = (val && (val->kind == RK_STR || val->kind == RK_INTERP_STR))
                        ? E_LEQ : E_EQ;
            EXPR_t *cond = expr_binary(cmp, lower_node(n->left), lower_node(val));
            EXPR_t *then = lower_block(w->right);
            EXPR_t *eif  = expr_new(E_IF);
            expr_add_child(eif, cond);
            expr_add_child(eif, then);
            if (chain) expr_add_child(eif, chain);
            chain = eif;
        }
        /* If no whens and no default, return empty seq */
        return chain ? chain : expr_new(E_SEQ_EXPR);
    }

    case RK_WHEN:
        /* Standalone RK_WHEN outside given — treat as if(true){body} */
        return lower_block(n->right);

    /*-- while cond body ---------------------------------------------------*/
    case RK_WHILE:
        return expr_binary(E_WHILE, lower_node(n->left), lower_block(n->right));

    /*-- block → E_SEQ_EXPR ------------------------------------------------*/
    case RK_BLOCK:
        return lower_block(n);

    /*-- expr; statement wrapper -------------------------------------------*/
    case RK_EXPR_STMT:
        return lower_node(n->left);

    /*-- sub name(params) body  →  E_FNC matching icon_lower layout -------
     * Layout (must match icn_call_proc in scrip.c):
     *   e->sval=name, e->ival=nparams, children[0]=E_VAR name-node,
     *   children[1..np]=E_VAR param nodes, children[np+1..]=body stmts.
     * RK-8: wire params so icn_call_proc populates the frame correctly. */
    case RK_SUBDEF: {
        const char *sname = n->sval ? n->sval : "<anon>";
        e = fnc_node(sname);
        int np = n->children ? n->children->count : 0;
        e->ival = np;
        /* children[0]: name node */
        EXPR_t *sname_node = expr_new(E_VAR);
        sname_node->sval = intern(sname);
        expr_add_child(e, sname_node);
        /* children[1..np]: param E_VAR nodes (names for scope patching) */
        if (n->children) {
            for (int i = 0; i < np; i++) {
                RakuNode *pn = n->children->items[i];
                EXPR_t *pe = expr_new(E_VAR);
                pe->sval = intern(strip_sigil(pn->sval ? pn->sval : ""));
                expr_add_child(e, pe);
            }
        }
        /* children[np+1..]: body statements directly */
        lower_block_children(e, n->right);
        return e;
    }

    /*-- name(args)  →  E_FNC call layout: children[0]=name-node, [1..]=args */
    case RK_CALL: {
        const char *cname = n->sval ? n->sval : "<unknown>";
        e = make_call(cname);   /* adds children[0] = E_VAR(name) */
        if (n->children) {
            for (int i = 0; i < n->children->count; i++)
                expr_add_child(e, lower_node(n->children->items[i]));
        }
        return e;
    }

    /*-- IDIV (integer division) — lower as DIV for now -------------------*/
    case RK_IDIV:
        return expr_binary(E_DIV, lower_node(n->left), lower_node(n->right));

    default:
        fprintf(stderr, "raku_lower: unhandled RakuKind %d at line %d\n",
                (int)n->kind, n->lineno);
        return expr_new(E_NUL);
    }
}

/*========================================================================
 * Public API
 *========================================================================*/

/* raku_lower_stmt — lower one top-level statement/sub node → EXPR_t. */
EXPR_t *raku_lower_stmt(const RakuNode *n) {
    return lower_node(n);
}

/* raku_lower_file — lower a top-level program block.
 *
 * A Tiny-Raku file is an RK_BLOCK of statements.  Sub definitions become
 * individual E_FNC entries.  All remaining top-level statements are wrapped
 * in a synthetic "main" E_FNC so the driver can call them uniformly.
 *
 * stmts[0..count-1] are the individual statements from the top-level block.
 * Returns malloc'd EXPR_t** of length *out_count.  Caller owns result.      */
EXPR_t **raku_lower_file(RakuNode **stmts, int count, int *out_count) {
    /* First pass: count subs vs top-level stmts */
    int nsubs = 0;
    for (int i = 0; i < count; i++)
        if (stmts[i] && stmts[i]->kind == RK_SUBDEF) nsubs++;
    int has_main = (count - nsubs) > 0;

    int total = nsubs + (has_main ? 1 : 0);
    EXPR_t **result = malloc(sizeof(EXPR_t *) * (total + 1));
    if (!result) { *out_count = 0; return NULL; }

    int out = 0;

    /* Emit subs first */
    for (int i = 0; i < count; i++) {
        if (!stmts[i] || stmts[i]->kind != RK_SUBDEF) continue;
        result[out++] = lower_node(stmts[i]);
    }

    /* Wrap non-sub statements in synthetic "main" E_FNC.
     * Must match icon_lower E_FNC layout (icn_call_proc expects):
     *   sval="main", ival=0, children[0]=E_VAR("main"), children[1..]=stmts */
    if (has_main) {
        EXPR_t *main_fnc = fnc_node("main");
        main_fnc->ival = 0;
        EXPR_t *mname_node = expr_new(E_VAR);
        mname_node->sval = intern("main");
        expr_add_child(main_fnc, mname_node);   /* children[0]: name node */
        for (int i = 0; i < count; i++) {       /* children[1..]: body stmts */
            if (!stmts[i] || stmts[i]->kind == RK_SUBDEF) continue;
            expr_add_child(main_fnc, lower_node(stmts[i]));
        }
        result[out++] = main_fnc;
    }

    result[out] = NULL;   /* sentinel */
    *out_count = out;
    return result;
}
