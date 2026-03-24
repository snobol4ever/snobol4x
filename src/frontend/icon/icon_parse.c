/*
 * icon_parse.c — Tiny-ICON recursive-descent parser
 *
 * Grammar (explicit-semicolon Icon, Tier 0 + procedure shell):
 *
 *   file       := proc* EOF
 *   proc       := 'procedure' IDENT '(' params ')' ';' stmt* 'end'
 *   params     := (IDENT (',' IDENT)*)?
 *   stmt       := expr ';'
 *              |  'every' expr [do_clause] ';'
 *              |  'if' expr 'then' expr ['else' expr] ';'
 *              |  'while' expr [do_clause] ';'
 *              |  'until' expr [do_clause] ';'
 *              |  'repeat' expr ';'
 *              |  'return' [expr] ';'
 *              |  'suspend' expr [do_clause] ';'
 *              |  'fail' ';'
 *              |  'break' [expr] ';'
 *              |  'next' ';'
 *              |  'local' IDENT (',' IDENT)* ';'
 *   do_clause  := 'do' expr
 *
 *   expr       := assign_expr
 *   assign_expr:= alt_expr (':=' | '<-' | ':=:' | augop) assign_expr
 *              |  alt_expr
 *   alt_expr   := to_expr ('|' to_expr)*
 *   to_expr    := and_expr ('to' and_expr ('by' and_expr)?)?
 *   and_expr   := rel_expr ('&' rel_expr)*
 *   rel_expr   := concat_expr (relop concat_expr)*
 *   concat_expr:= add_expr ('||' add_expr | '|||' add_expr)*
 *   add_expr   := mul_expr (('+' | '-') mul_expr)*
 *   mul_expr   := unary_expr (('*' | '/' | '%') unary_expr)*
 *   unary_expr := ('-' | '!' | '\\' | '~' | 'not') unary_expr
 *              |  limit_expr
 *   limit_expr := postfix_expr ('\\' unary_expr)?
 *   postfix_expr:= primary ('[' expr ']' | '.' IDENT | '(' args ')' )*
 *   primary    := INT | REAL | STRING | CSET | IDENT
 *              |  '(' expr ')'
 *              |  '&' IDENT   (keyword)
 */

#include "icon_parse.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

static void parser_error(IcnParser *p, const char *msg) {
    if (!p->had_error) {
        snprintf(p->errmsg, sizeof(p->errmsg),
                 "line %d: %s (got %s)", p->cur.line, msg, icn_tk_name(p->cur.kind));
        p->had_error = 1;
    }
}

static IcnToken advance(IcnParser *p) {
    p->cur  = p->peek;
    p->peek = icn_lex_next(p->lex);
    return p->cur;
}

static int check(IcnParser *p, IcnTkKind kind) {
    return p->cur.kind == kind;
}

static int match(IcnParser *p, IcnTkKind kind) {
    if (p->cur.kind == kind) { advance(p); return 1; }
    return 0;
}

static int expect(IcnParser *p, IcnTkKind kind, const char *ctx) {
    if (p->cur.kind == kind) { advance(p); return 1; }
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: expected %s", ctx, icn_tk_name(kind));
    parser_error(p, msg);
    return 0;
}

/* =========================================================================
 * Forward declarations
 * ======================================================================= */
static IcnNode *parse_expr(IcnParser *p);
static IcnNode *parse_stmt(IcnParser *p);
static IcnNode *parse_do_clause(IcnParser *p);

/* =========================================================================
 * Expression parsing (recursive descent)
 * ======================================================================= */

static IcnNode *parse_primary(IcnParser *p) {
    int line = p->cur.line;
    IcnToken t = p->cur;

    if (t.kind == TK_INT) {
        advance(p);
        return icn_leaf_int(line, t.val.ival);
    }
    if (t.kind == TK_REAL) {
        advance(p);
        return icn_leaf_real(line, t.val.fval);
    }
    if (t.kind == TK_STRING) {
        advance(p);
        return icn_leaf_str(ICN_STR, line, t.val.sval.data, t.val.sval.len);
    }
    if (t.kind == TK_CSET) {
        advance(p);
        return icn_leaf_str(ICN_CSET, line, t.val.sval.data, t.val.sval.len);
    }
    if (t.kind == TK_IDENT) {
        advance(p);
        return icn_leaf_str(ICN_VAR, line, t.val.sval.data, t.val.sval.len);
    }
    if (t.kind == TK_AND) {
        /* &keyword */
        advance(p);
        if (p->cur.kind != TK_IDENT) { parser_error(p, "expected keyword name after &"); return NULL; }
        IcnToken kw = p->cur; advance(p);
        char name[256]; snprintf(name, sizeof(name), "&%s", kw.val.sval.data);
        return icn_leaf_str(ICN_VAR, line, name, strlen(name));
    }
    if (t.kind == TK_LPAREN) {
        advance(p);
        IcnNode *inner = parse_expr(p);
        expect(p, TK_RPAREN, "grouped expression");
        return inner;
    }
    if (t.kind == TK_FAIL) {
        advance(p);
        return icn_node_new(ICN_FAIL, line, 0);
    }
    if (t.kind == TK_BREAK) {
        advance(p);
        IcnNode *val = NULL;
        if (!check(p, TK_SEMICOL) && !check(p, TK_RPAREN) && !check(p, TK_EOF))
            val = parse_expr(p);
        return icn_node_new(ICN_BREAK, line, val ? 1 : 0, val);
    }
    if (t.kind == TK_NEXT) {
        advance(p);
        return icn_node_new(ICN_NEXT, line, 0);
    }

    parser_error(p, "expected expression");
    advance(p); /* skip bad token */
    return NULL;
}

static IcnNode *parse_postfix(IcnParser *p) {
    IcnNode *n = parse_primary(p);
    if (!n) return NULL;
    for (;;) {
        int line = p->cur.line;
        if (check(p, TK_LPAREN)) {
            /* function call: fn(arg, ...) */
            advance(p);
            /* collect arguments */
            IcnNode **args = NULL;
            int nargs = 0, cap = 0;
            if (!check(p, TK_RPAREN)) {
                do {
                    IcnNode *arg = parse_expr(p);
                    if (!arg) break;
                    if (nargs + 1 > cap) {
                        cap = cap ? cap*2 : 4;
                        args = realloc(args, cap * sizeof(IcnNode*));
                    }
                    args[nargs++] = arg;
                } while (match(p, TK_COMMA));
            }
            expect(p, TK_RPAREN, "function call");
            /* Build ICN_CALL: children = [fn, arg1, arg2, ...] */
            IcnNode **children = malloc((1 + nargs) * sizeof(IcnNode*));
            children[0] = n;
            for (int i = 0; i < nargs; i++) children[i+1] = args[i];
            free(args);
            IcnNode *call = calloc(1, sizeof(IcnNode));
            call->kind = ICN_CALL; call->line = line;
            call->nchildren = 1 + nargs;
            call->children = children;
            n = call;
        } else if (check(p, TK_LBRACK)) {
            advance(p);
            IcnNode *idx = parse_expr(p);
            expect(p, TK_RBRACK, "subscript");
            n = icn_node_new(ICN_SUBSCRIPT, line, 2, n, idx);
        } else if (check(p, TK_DOT)) {
            advance(p);
            if (p->cur.kind != TK_IDENT) { parser_error(p, "expected field name"); break; }
            IcnToken fname = p->cur; advance(p);
            IcnNode *field = icn_leaf_str(ICN_VAR, line, fname.val.sval.data, fname.val.sval.len);
            n = icn_node_new(ICN_FIELD, line, 2, n, field);
        } else {
            break;
        }
    }
    return n;
}

static IcnNode *parse_unary(IcnParser *p);

static IcnNode *parse_limit(IcnParser *p) {
    IcnNode *n = parse_postfix(p);
    if (!n) return NULL;
    if (check(p, TK_BACKSLASH)) {
        int line = p->cur.line;
        advance(p);
        IcnNode *lim = parse_unary(p);
        n = icn_node_new(ICN_LIMIT, line, 2, n, lim);
    }
    return n;
}

static IcnNode *parse_unary(IcnParser *p) {
    int line = p->cur.line;
    if (check(p, TK_MINUS)) {
        advance(p);
        IcnNode *inner = parse_unary(p);
        return icn_node_new(ICN_NEG, line, 1, inner);
    }
    if (check(p, TK_BANG)) {
        advance(p);
        IcnNode *inner = parse_unary(p);
        return icn_node_new(ICN_BANG, line, 1, inner);
    }
    if (check(p, TK_BACKSLASH)) {
        advance(p);
        IcnNode *inner = parse_unary(p);
        return icn_node_new(ICN_NOT, line, 1, inner); /* \E = succeed if E fails */
    }
    if (check(p, TK_NOT)) {
        advance(p);
        IcnNode *inner = parse_unary(p);
        return icn_node_new(ICN_NOT, line, 1, inner);
    }
    return parse_limit(p);
}

static IcnNode *parse_mul(IcnParser *p) {
    IcnNode *n = parse_unary(p);
    if (!n) return NULL;
    for (;;) {
        int line = p->cur.line;
        IcnKind kind;
        if      (check(p, TK_STAR))  kind = ICN_MUL;
        else if (check(p, TK_SLASH)) kind = ICN_DIV;
        else if (check(p, TK_MOD))   kind = ICN_MOD;
        else break;
        advance(p);
        IcnNode *rhs = parse_unary(p);
        n = icn_node_new(kind, line, 2, n, rhs);
    }
    return n;
}

static IcnNode *parse_add(IcnParser *p) {
    IcnNode *n = parse_mul(p);
    if (!n) return NULL;
    for (;;) {
        int line = p->cur.line;
        IcnKind kind;
        if      (check(p, TK_PLUS))  kind = ICN_ADD;
        else if (check(p, TK_MINUS)) kind = ICN_SUB;
        else break;
        advance(p);
        IcnNode *rhs = parse_mul(p);
        n = icn_node_new(kind, line, 2, n, rhs);
    }
    return n;
}

static IcnNode *parse_concat(IcnParser *p) {
    IcnNode *n = parse_add(p);
    if (!n) return NULL;
    for (;;) {
        int line = p->cur.line;
        IcnKind kind;
        if      (check(p, TK_LCONCAT)) kind = ICN_LCONCAT;
        else if (check(p, TK_CONCAT))  kind = ICN_CONCAT;
        else break;
        advance(p);
        IcnNode *rhs = parse_add(p);
        n = icn_node_new(kind, line, 2, n, rhs);
    }
    return n;
}

static int is_relop(IcnTkKind k) {
    return k==TK_LT || k==TK_LE || k==TK_GT || k==TK_GE ||
           k==TK_EQ || k==TK_NEQ ||
           k==TK_SLT || k==TK_SLE || k==TK_SGT || k==TK_SGE ||
           k==TK_SEQ || k==TK_SNE;
}
static IcnKind relop_kind(IcnTkKind k) {
    switch (k) {
        case TK_LT:  return ICN_LT;  case TK_LE:  return ICN_LE;
        case TK_GT:  return ICN_GT;  case TK_GE:  return ICN_GE;
        case TK_EQ:  return ICN_EQ;  case TK_NEQ: return ICN_NE;
        case TK_SLT: return ICN_SLT; case TK_SLE: return ICN_SLE;
        case TK_SGT: return ICN_SGT; case TK_SGE: return ICN_SGE;
        case TK_SEQ: return ICN_SEQ; case TK_SNE: return ICN_SNE;
        default:     return ICN_EQ;
    }
}

static IcnNode *parse_rel(IcnParser *p) {
    IcnNode *n = parse_concat(p);
    if (!n) return NULL;
    while (is_relop(p->cur.kind)) {
        int line = p->cur.line;
        IcnKind kind = relop_kind(p->cur.kind);
        advance(p);
        IcnNode *rhs = parse_concat(p);
        n = icn_node_new(kind, line, 2, n, rhs);
    }
    return n;
}

static IcnNode *parse_and(IcnParser *p) {
    IcnNode *n = parse_rel(p);
    if (!n) return NULL;
    if (!check(p, TK_AND)) return n;
    /* Flatten into a single n-ary ICN_AND node */
    int line = p->cur.line;
    IcnNode *and_node = icn_node_new(ICN_AND, line, 1, n);
    while (check(p, TK_AND)) {
        advance(p);
        IcnNode *rhs = parse_rel(p);
        icn_node_append(and_node, rhs);
    }
    return and_node;
}

static IcnNode *parse_to(IcnParser *p) {
    IcnNode *n = parse_and(p);
    if (!n) return NULL;
    if (check(p, TK_TO)) {
        int line = p->cur.line;
        advance(p);
        IcnNode *limit = parse_and(p);
        if (check(p, TK_BY)) {
            advance(p);
            IcnNode *step = parse_and(p);
            n = icn_node_new(ICN_TO_BY, line, 3, n, limit, step);
        } else {
            n = icn_node_new(ICN_TO, line, 2, n, limit);
        }
    }
    return n;
}

static IcnNode *parse_alt(IcnParser *p) {
    IcnNode *n = parse_to(p);
    if (!n) return NULL;
    if (!check(p, TK_BAR)) return n;
    /* Flatten into a single n-ary ICN_ALT node */
    int line = p->cur.line;
    IcnNode *alt_node = icn_node_new(ICN_ALT, line, 1, n);
    while (check(p, TK_BAR)) {
        advance(p);
        IcnNode *rhs = parse_to(p);
        icn_node_append(alt_node, rhs);
    }
    return alt_node;
}

static int is_augop(IcnTkKind k) {
    return k==TK_AUGPLUS || k==TK_AUGMINUS || k==TK_AUGSTAR ||
           k==TK_AUGSLASH || k==TK_AUGMOD  || k==TK_AUGCONCAT;
}

static IcnNode *parse_assign(IcnParser *p) {
    IcnNode *n = parse_alt(p);
    if (!n) return NULL;
    int line = p->cur.line;
    if (check(p, TK_ASSIGN)) {
        advance(p);
        IcnNode *rhs = parse_assign(p);
        return icn_node_new(ICN_ASSIGN, line, 2, n, rhs);
    }
    if (check(p, TK_REVASSIGN)) {
        advance(p);
        IcnNode *rhs = parse_assign(p);
        return icn_node_new(ICN_ASSIGN, line, 2, n, rhs); /* treat reversible as assign for now */
    }
    if (check(p, TK_SWAP)) {
        advance(p);
        IcnNode *rhs = parse_assign(p);
        return icn_node_new(ICN_SWAP, line, 2, n, rhs);
    }
    if (is_augop(p->cur.kind)) {
        IcnTkKind aug = p->cur.kind; advance(p);
        IcnNode *rhs = parse_assign(p);
        IcnNode *op = icn_node_new(ICN_AUGOP, line, 2, n, rhs);
        op->val.ival = (long)aug;
        return op;
    }
    if (check(p, TK_QMARK)) {
        advance(p);
        IcnNode *rhs = parse_assign(p);
        return icn_node_new(ICN_SCAN, line, 2, n, rhs);
    }
    return n;
}

static IcnNode *parse_expr(IcnParser *p) {
    int line = p->cur.line;
    /* Control expressions — valid anywhere an expression is expected */
    if (check(p, TK_RETURN)) {
        advance(p);
        IcnNode *val = NULL;
        if (!check(p, TK_SEMICOL) && !check(p, TK_RPAREN) &&
            !check(p, TK_EOF)  && !check(p, TK_THEN) &&
            !check(p, TK_ELSE) && !check(p, TK_DO))
            val = parse_expr(p);
        if (val) return icn_node_new(ICN_RETURN, line, 1, val);
        return icn_node_new(ICN_RETURN, line, 0);
    }
    if (check(p, TK_FAIL)) {
        advance(p);
        return icn_node_new(ICN_FAIL, line, 0);
    }
    if (check(p, TK_SUSPEND)) {
        advance(p);
        IcnNode *val = parse_expr(p);
        IcnNode *body = parse_do_clause(p);
        if (body) return icn_node_new(ICN_SUSPEND, line, 2, val, body);
        return icn_node_new(ICN_SUSPEND, line, 1, val);
    }
    if (check(p, TK_BREAK)) { advance(p); return icn_node_new(ICN_BREAK, line, 0); }
    if (check(p, TK_NEXT))  { advance(p); return icn_node_new(ICN_NEXT,  line, 0); }
    return parse_assign(p);
}

/* =========================================================================
 * Statement parsing
 * ======================================================================= */

static IcnNode *parse_do_clause(IcnParser *p) {
    if (check(p, TK_DO)) { advance(p); return parse_expr(p); }
    return NULL;
}

static IcnNode *parse_stmt(IcnParser *p) {
    int line = p->cur.line;

    if (check(p, TK_EVERY)) {
        advance(p);
        IcnNode *gen = parse_expr(p);
        IcnNode *body = parse_do_clause(p);
        expect(p, TK_SEMICOL, "every statement");
        if (body) return icn_node_new(ICN_EVERY, line, 2, gen, body);
        return icn_node_new(ICN_EVERY, line, 1, gen);
    }
    if (check(p, TK_IF)) {
        advance(p);
        IcnNode *cond = parse_expr(p);
        expect(p, TK_THEN, "if/then");
        IcnNode *thenb = parse_expr(p);
        IcnNode *elseb = NULL;
        if (match(p, TK_ELSE)) elseb = parse_expr(p);
        expect(p, TK_SEMICOL, "if statement");
        if (elseb) return icn_node_new(ICN_IF, line, 3, cond, thenb, elseb);
        return icn_node_new(ICN_IF, line, 2, cond, thenb);
    }
    if (check(p, TK_WHILE)) {
        advance(p);
        IcnNode *cond = parse_expr(p);
        IcnNode *body = parse_do_clause(p);
        expect(p, TK_SEMICOL, "while statement");
        if (body) return icn_node_new(ICN_WHILE, line, 2, cond, body);
        return icn_node_new(ICN_WHILE, line, 1, cond);
    }
    if (check(p, TK_UNTIL)) {
        advance(p);
        IcnNode *cond = parse_expr(p);
        IcnNode *body = parse_do_clause(p);
        expect(p, TK_SEMICOL, "until statement");
        if (body) return icn_node_new(ICN_UNTIL, line, 2, cond, body);
        return icn_node_new(ICN_UNTIL, line, 1, cond);
    }
    if (check(p, TK_REPEAT)) {
        advance(p);
        IcnNode *body = parse_expr(p);
        expect(p, TK_SEMICOL, "repeat statement");
        return icn_node_new(ICN_REPEAT, line, 1, body);
    }
    if (check(p, TK_RETURN)) {
        advance(p);
        IcnNode *val = NULL;
        if (!check(p, TK_SEMICOL)) val = parse_expr(p);
        expect(p, TK_SEMICOL, "return statement");
        if (val) return icn_node_new(ICN_RETURN, line, 1, val);
        return icn_node_new(ICN_RETURN, line, 0);
    }
    if (check(p, TK_SUSPEND)) {
        advance(p);
        IcnNode *val = parse_expr(p);
        IcnNode *body = parse_do_clause(p);
        expect(p, TK_SEMICOL, "suspend statement");
        if (body) return icn_node_new(ICN_SUSPEND, line, 2, val, body);
        return icn_node_new(ICN_SUSPEND, line, 1, val);
    }
    if (check(p, TK_FAIL)) {
        advance(p);
        expect(p, TK_SEMICOL, "fail statement");
        return icn_node_new(ICN_FAIL, line, 0);
    }
    if (check(p, TK_LOCAL)) {
        int line2 = p->cur.line; advance(p);
        IcnNode **locs = NULL; int nlocs = 0, lcap = 0;
        while (!check(p, TK_SEMICOL) && !check(p, TK_EOF)) {
            if (p->cur.kind == TK_IDENT) {
                IcnNode *v = icn_leaf_str(ICN_VAR, p->cur.line,
                    p->cur.val.sval.data, p->cur.val.sval.len);
                if (nlocs+1 > lcap) { lcap=lcap?lcap*2:4; locs=realloc(locs,lcap*sizeof(IcnNode*)); }
                locs[nlocs++] = v; advance(p);
            }
            if (!match(p, TK_COMMA)) break;
        }
        match(p, TK_SEMICOL);
        IcnNode *ld = calloc(1, sizeof(IcnNode));
        ld->kind = ICN_GLOBAL; ld->line = line2;
        ld->nchildren = nlocs; ld->children = locs;
        return ld;
    }
    /* Expression statement */
    IcnNode *e = parse_expr(p);
    expect(p, TK_SEMICOL, "expression statement");
    return e;
}

/* =========================================================================
 * Procedure parsing
 * ======================================================================= */

static IcnNode *parse_proc(IcnParser *p) {
    int line = p->cur.line;
    expect(p, TK_PROCEDURE, "procedure");
    if (p->cur.kind != TK_IDENT) { parser_error(p, "expected procedure name"); return NULL; }
    IcnToken name_tok = p->cur; advance(p);

    /* params -- collect as ICN_VAR nodes */
    IcnNode **params = NULL; int nparams = 0, pcap = 0;
    expect(p, TK_LPAREN, "procedure params");
    while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
        if (p->cur.kind == TK_IDENT) {
            IcnNode *pv = icn_leaf_str(ICN_VAR, p->cur.line,
                p->cur.val.sval.data, p->cur.val.sval.len);
            if (nparams+1 > pcap) { pcap=pcap?pcap*2:4; params=realloc(params,pcap*sizeof(IcnNode*)); }
            params[nparams++] = pv; advance(p);
        }
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_RPAREN, "procedure params");
    expect(p, TK_SEMICOL, "after procedure header");

    /* body: stmts until 'end' */
    IcnNode **stmts = NULL;
    int nstmts = 0, cap = 0;
    while (!check(p, TK_END) && !check(p, TK_EOF) && !p->had_error) {
        IcnNode *s = parse_stmt(p);
        if (s) {
            if (nstmts + 1 > cap) { cap = cap ? cap*2 : 8; stmts = realloc(stmts, cap * sizeof(IcnNode*)); }
            stmts[nstmts++] = s;
        }
    }
    expect(p, TK_END, "end of procedure");

    /* Build ICN_PROC:
     * child[0]          = proc name (ICN_VAR)
     * child[1..nparams] = param names (ICN_VAR)
     * child[nparams+1..]= body stmts
     * proc->val.ival    = nparams
     */
    IcnNode *proc_name = icn_leaf_str(ICN_VAR, line, name_tok.val.sval.data, name_tok.val.sval.len);
    int total = 1 + nparams + nstmts;
    IcnNode **children = malloc(total * sizeof(IcnNode*));
    children[0] = proc_name;
    for (int i = 0; i < nparams; i++) children[1+i] = params[i];
    for (int i = 0; i < nstmts; i++) children[1+nparams+i] = stmts[i];
    free(params); free(stmts);

    IcnNode *proc = calloc(1, sizeof(IcnNode));
    proc->kind = ICN_PROC; proc->line = line;
    proc->val.ival = nparams;
    proc->nchildren = total;
    proc->children = children;
    return proc;
}

/* =========================================================================
 * Public API
 * ======================================================================= */

void icn_parse_init(IcnParser *p, IcnLexer *lex) {
    memset(p, 0, sizeof(*p));
    p->lex  = lex;
    /* Prime the two-token window */
    p->cur  = icn_lex_next(lex);
    p->peek = icn_lex_next(lex);
}

IcnNode **icn_parse_file(IcnParser *p, int *out_count) {
    IcnNode **procs = NULL;
    int n = 0, cap = 0;
    while (!check(p, TK_EOF) && !p->had_error) {
        IcnNode *proc = NULL;
        if (check(p, TK_PROCEDURE))
            proc = parse_proc(p);
        else if (check(p, TK_GLOBAL)) {
            advance(p);
            while (!check(p, TK_SEMICOL) && !check(p, TK_EOF)) advance(p);
            match(p, TK_SEMICOL);
        } else {
            parser_error(p, "expected 'procedure' or 'global'");
            break;
        }
        if (proc) {
            if (n + 1 > cap) { cap = cap ? cap*2 : 4; procs = realloc(procs, cap * sizeof(IcnNode*)); }
            procs[n++] = proc;
        }
    }
    *out_count = n;
    return procs;
}

IcnNode *icn_parse_expr(IcnParser *p) {
    return parse_expr(p);
}
