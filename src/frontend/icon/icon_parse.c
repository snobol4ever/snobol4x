/*
 * icon_parse.c — Tiny-ICON recursive-descent parser → IR direct
 *
 * Grammar (explicit-semicolon Icon, Tier 0 + procedure shell):
 *
 *   file       := (proc | record | global)* EOF
 *   proc       := 'procedure' IDENT '(' params ')' stmt* 'end'
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
 *
 * FI-2: Produces EXPR_t / STMT_t directly — IcnNode/icon_ast eliminated.
 * Authors: Claude Sonnet 4.6 (FI-2, 2026-04-14)
 */

#include "icon_parse.h"
#include "../../ir/ir.h"
#include "../snobol4/scrip_cc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* =========================================================================
 * Internal helpers — mirrors icon_lower.c helpers, now inline in parser
 * ======================================================================= */

static EXPR_t *e_leaf_sval(EKind k, const char *s, int len) {
    EXPR_t *e = expr_new(k);
    if (len >= 0) e->sval = intern_n(s, len);
    else          e->sval = intern(s);
    return e;
}

static EXPR_t *e_unary(EKind k, EXPR_t *child) {
    return expr_unary(k, child);
}

static EXPR_t *e_binary(EKind k, EXPR_t *left, EXPR_t *right) {
    return expr_binary(k, left, right);
}

/* =========================================================================
 * Parser machinery
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

static int check(IcnParser *p, IcnTkKind kind) { return p->cur.kind == kind; }

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
static EXPR_t *parse_expr(IcnParser *p);
static EXPR_t *parse_stmt(IcnParser *p);
static EXPR_t *parse_do_clause(IcnParser *p);
static EXPR_t *parse_block_or_expr(IcnParser *p);

/* =========================================================================
 * Append helper — add expr child to n-ary node
 * ======================================================================= */
static void push_child(EXPR_t *parent, EXPR_t *child) {
    expr_add_child(parent, child);
}

/* =========================================================================
 * Expression parsing (recursive descent) → EXPR_t direct
 * ======================================================================= */

static EXPR_t *parse_primary(IcnParser *p) {
    int line = p->cur.line;
    IcnToken t = p->cur;

    if (t.kind == TK_INT) {
        advance(p);
        EXPR_t *e = expr_new(E_ILIT);
        e->ival = t.val.ival;
        return e;
    }
    if (t.kind == TK_REAL) {
        advance(p);
        EXPR_t *e = expr_new(E_FLIT);
        e->dval = t.val.fval;
        return e;
    }
    if (t.kind == TK_STRING) {
        advance(p);
        return e_leaf_sval(E_QLIT, t.val.sval.data, (int)t.val.sval.len);
    }
    if (t.kind == TK_CSET) {
        advance(p);
        return e_leaf_sval(E_CSET, t.val.sval.data, (int)t.val.sval.len);
    }
    if (t.kind == TK_IDENT) {
        advance(p);
        return e_leaf_sval(E_VAR, t.val.sval.data, (int)t.val.sval.len);
    }
    if (t.kind == TK_AND) {
        /* &keyword */
        advance(p);
        const char *kwname = NULL;
        if (p->cur.kind == TK_IDENT) {
            kwname = p->cur.val.sval.data;
        } else {
            kwname = icn_tk_name(p->cur.kind);
            int ok = 1;
            for (const char *c = kwname; *c; c++)
                if (!isalpha((unsigned char)*c) && *c != '_') { ok = 0; break; }
            if (!ok) kwname = NULL;
        }
        if (!kwname) { parser_error(p, "expected keyword name after &"); return NULL; }
        char name[256]; snprintf(name, sizeof(name), "&%s", kwname);
        advance(p);
        return e_leaf_sval(E_VAR, name, -1);
    }
    if (t.kind == TK_LPAREN) {
        advance(p);
        EXPR_t *first = parse_expr(p);
        if (check(p, TK_SEMICOL)) {
            /* (E1; E2; ...) — expression sequence → E_SEQ_EXPR */
            EXPR_t *seq = expr_new(E_SEQ_EXPR);
            push_child(seq, first);
            while (check(p, TK_SEMICOL)) {
                advance(p);
                if (check(p, TK_RPAREN)) break;
                push_child(seq, parse_expr(p));
            }
            expect(p, TK_RPAREN, "sequence expression");
            return seq;
        }
        expect(p, TK_RPAREN, "grouped expression");
        return first;
    }
    if (t.kind == TK_LBRACK) {
        /* [e1, e2, ...] — list constructor → E_MAKELIST */
        advance(p);
        EXPR_t *lst = expr_new(E_MAKELIST);
        if (!check(p, TK_RBRACK)) {
            push_child(lst, parse_expr(p));
            while (check(p, TK_COMMA)) {
                advance(p);
                if (check(p, TK_RBRACK)) break;
                push_child(lst, parse_expr(p));
            }
        }
        expect(p, TK_RBRACK, "list literal");
        return lst;
    }
    if (t.kind == TK_FAIL) {
        advance(p);
        return expr_new(E_FAIL);
    }
    if (t.kind == TK_BREAK) {
        advance(p);
        EXPR_t *e = expr_new(E_LOOP_BREAK);
        if (!check(p, TK_SEMICOL) && !check(p, TK_RPAREN) && !check(p, TK_EOF))
            push_child(e, parse_expr(p));
        return e;
    }
    if (t.kind == TK_NEXT) {
        advance(p);
        return expr_new(E_LOOP_NEXT);
    }
    if (t.kind == TK_CASE) {
        return parse_expr(p);
    }
    if (t.kind == TK_LBRACE) {
        return parse_block_or_expr(p);
    }

    parser_error(p, "expected expression");
    advance(p);
    return NULL;
}

static EXPR_t *parse_postfix(IcnParser *p) {
    EXPR_t *n = parse_primary(p);
    if (!n) return NULL;
    for (;;) {
        int line = p->cur.line; (void)line;
        if (check(p, TK_LPAREN)) {
            advance(p);
            /* E_FNC: child[0]=callee, child[1..]=args */
            EXPR_t *call = expr_new(E_FNC);
            push_child(call, n);
            if (!check(p, TK_RPAREN)) {
                do {
                    EXPR_t *arg;
                    if (check(p, TK_COMMA) || check(p, TK_RPAREN))
                        arg = e_leaf_sval(E_VAR, "&null", -1);
                    else {
                        arg = parse_expr(p);
                        if (!arg) break;
                    }
                    push_child(call, arg);
                } while (match(p, TK_COMMA));
            }
            expect(p, TK_RPAREN, "function call");
            n = call;
        } else if (check(p, TK_LBRACK)) {
            advance(p);
            EXPR_t *idx = parse_expr(p);
            if (check(p, TK_COLON)) {
                advance(p);
                EXPR_t *hi = parse_expr(p);
                expect(p, TK_RBRACK, "section");
                EXPR_t *sec = expr_new(E_SECTION);
                push_child(sec, n); push_child(sec, idx); push_child(sec, hi);
                n = sec;
            } else if (check(p, TK_PLUSCOLON)) {
                advance(p);
                EXPR_t *len = parse_expr(p);
                expect(p, TK_RBRACK, "section+:");
                EXPR_t *sec = expr_new(E_SECTION_PLUS);
                push_child(sec, n); push_child(sec, idx); push_child(sec, len);
                n = sec;
            } else if (check(p, TK_MINUSCOLON)) {
                advance(p);
                EXPR_t *len = parse_expr(p);
                expect(p, TK_RBRACK, "section-:");
                EXPR_t *sec = expr_new(E_SECTION_MINUS);
                push_child(sec, n); push_child(sec, idx); push_child(sec, len);
                n = sec;
            } else {
                expect(p, TK_RBRACK, "subscript");
                n = e_binary(E_IDX, n, idx);
            }
        } else if (check(p, TK_DOT)) {
            advance(p);
            if (p->cur.kind != TK_IDENT) { parser_error(p, "expected field name"); break; }
            IcnToken fname = p->cur; advance(p);
            /* E_FIELD: sval=field name, child[0]=object */
            EXPR_t *fe = expr_new(E_FIELD);
            fe->sval = intern_n(fname.val.sval.data, (int)fname.val.sval.len);
            push_child(fe, n);
            n = fe;
        } else {
            break;
        }
    }
    return n;
}

static EXPR_t *parse_unary(IcnParser *p);

static EXPR_t *parse_limit(IcnParser *p) {
    EXPR_t *n = parse_postfix(p);
    if (!n) return NULL;
    if (check(p, TK_BACKSLASH)) {
        advance(p);
        EXPR_t *lim = parse_unary(p);
        n = e_binary(E_LIMIT, n, lim);
    }
    return n;
}

static EXPR_t *parse_unary(IcnParser *p) {
    int line = p->cur.line; (void)line;
    if (check(p, TK_MINUS))     { advance(p); return e_unary(E_MNS,        parse_unary(p)); }
    if (check(p, TK_PLUS))      { advance(p); return e_unary(E_PLS,        parse_unary(p)); }
    if (check(p, TK_BANG))      { advance(p); return e_unary(E_ITERATE,    parse_unary(p)); }
    if (check(p, TK_STAR))      { advance(p); return e_unary(E_SIZE,       parse_unary(p)); }
    if (check(p, TK_BACKSLASH)) { advance(p); return e_unary(E_NONNULL,    parse_unary(p)); }
    if (check(p, TK_SLASH))     { advance(p); return e_unary(E_NULL,       parse_unary(p)); }
    if (check(p, TK_NOT))       { advance(p); return e_unary(E_NOT,        parse_unary(p)); }
    if (check(p, TK_QMARK))     { advance(p); return e_unary(E_RANDOM,     parse_unary(p)); }
    if (check(p, TK_TILDE))     { advance(p); return e_unary(E_CSET_COMPL, parse_unary(p)); }
    if (check(p, TK_EQ)) {
        /* =E — scan match: rewrite as match(E) call */
        advance(p);
        EXPR_t *inner = parse_unary(p);
        EXPR_t *call = expr_new(E_FNC);
        push_child(call, e_leaf_sval(E_VAR, "match", -1));
        push_child(call, inner);
        return call;
    }
    return parse_limit(p);
}

static EXPR_t *parse_pow(IcnParser *p) {
    EXPR_t *n = parse_unary(p);
    if (!n) return NULL;
    if (check(p, TK_CARET)) {
        advance(p);
        EXPR_t *rhs = parse_pow(p);   /* right-associative */
        n = e_binary(E_POW, n, rhs);
    }
    return n;
}

static EXPR_t *parse_mul(IcnParser *p) {
    EXPR_t *n = parse_pow(p);
    if (!n) return NULL;
    for (;;) {
        EKind k;
        if      (check(p, TK_STAR))  k = E_MUL;
        else if (check(p, TK_SLASH)) k = E_DIV;
        else if (check(p, TK_MOD))   k = E_MOD;
        else break;
        advance(p);
        n = e_binary(k, n, parse_pow(p));
    }
    return n;
}

static EXPR_t *parse_add(IcnParser *p) {
    EXPR_t *n = parse_mul(p);
    if (!n) return NULL;
    for (;;) {
        EKind k;
        if      (check(p, TK_PLUS))  k = E_ADD;
        else if (check(p, TK_MINUS)) k = E_SUB;
        else break;
        advance(p);
        n = e_binary(k, n, parse_mul(p));
    }
    return n;
}

static EXPR_t *parse_cset(IcnParser *p) {
    EXPR_t *n = parse_add(p);
    if (!n) return NULL;
    for (;;) {
        EKind k;
        if      (check(p, TK_PLUSPLUS))   k = E_CSET_UNION;
        else if (check(p, TK_MINUSMINUS)) k = E_CSET_DIFF;
        else if (check(p, TK_STARSTAR))   k = E_CSET_INTER;
        else if (check(p, TK_BANG)) {
            /* binary !: E1 ! E2 → E_BANG_BINARY */
            advance(p);
            n = e_binary(E_BANG_BINARY, n, parse_add(p));
            continue;
        }
        else break;
        advance(p);
        n = e_binary(k, n, parse_add(p));
    }
    return n;
}

static EXPR_t *parse_concat(IcnParser *p) {
    EXPR_t *n = parse_cset(p);
    if (!n) return NULL;
    for (;;) {
        EKind k;
        if      (check(p, TK_LCONCAT)) k = E_LCONCAT;
        else if (check(p, TK_CONCAT))  k = E_CAT;
        else break;
        advance(p);
        n = e_binary(k, n, parse_cset(p));
    }
    return n;
}

static int is_relop(IcnTkKind k) {
    return k==TK_LT || k==TK_LE || k==TK_GT || k==TK_GE ||
           k==TK_EQ || k==TK_NEQ ||
           k==TK_SLT || k==TK_SLE || k==TK_SGT || k==TK_SGE ||
           k==TK_SEQ || k==TK_SNE;
}

static EKind relop_ekind(IcnTkKind k) {
    switch (k) {
        case TK_LT:  return E_LT;   case TK_LE:  return E_LE;
        case TK_GT:  return E_GT;   case TK_GE:  return E_GE;
        case TK_EQ:  return E_EQ;   case TK_NEQ: return E_NE;
        case TK_SLT: return E_LLT;  case TK_SLE: return E_LLE;
        case TK_SGT: return E_LGT;  case TK_SGE: return E_LGE;
        case TK_SEQ: return E_LEQ;  case TK_SNE: return E_LNE;
        default:     return E_EQ;
    }
}

static EXPR_t *parse_rel(IcnParser *p) {
    EXPR_t *n = parse_concat(p);
    if (!n) return NULL;
    while (is_relop(p->cur.kind)) {
        EKind k = relop_ekind(p->cur.kind);
        advance(p);
        n = e_binary(k, n, parse_concat(p));
    }
    return n;
}

static EXPR_t *parse_and(IcnParser *p) {
    EXPR_t *n = parse_rel(p);
    if (!n) return NULL;
    if (!check(p, TK_AND)) return n;
    /* n-ary E_SEQ (conjunction, same Byrd-box semantics as & in Icon) */
    EXPR_t *seq = expr_new(E_SEQ);
    push_child(seq, n);
    while (check(p, TK_AND)) {
        advance(p);
        push_child(seq, parse_rel(p));
    }
    return seq;
}

static EXPR_t *parse_to(IcnParser *p) {
    EXPR_t *n = parse_and(p);
    if (!n) return NULL;
    while (check(p, TK_TO)) {
        advance(p);
        EXPR_t *limit = parse_and(p);
        if (check(p, TK_BY)) {
            advance(p);
            EXPR_t *step = parse_and(p);
            EXPR_t *tby = expr_new(E_TO_BY);
            push_child(tby, n); push_child(tby, limit); push_child(tby, step);
            n = tby;
        } else {
            n = e_binary(E_TO, n, limit);
        }
    }
    return n;
}

static EXPR_t *parse_alt(IcnParser *p) {
    EXPR_t *n = parse_to(p);
    if (!n) return NULL;
    if (!check(p, TK_BAR)) return n;
    /* n-ary E_ALTERNATE */
    EXPR_t *alt = expr_new(E_ALTERNATE);
    push_child(alt, n);
    while (check(p, TK_BAR)) {
        advance(p);
        push_child(alt, parse_to(p));
    }
    return alt;
}

static int is_augop(IcnTkKind k) {
    return k==TK_AUGPLUS || k==TK_AUGMINUS || k==TK_AUGSTAR ||
           k==TK_AUGSLASH || k==TK_AUGMOD  || k==TK_AUGPOW  || k==TK_AUGCONCAT ||
           k==TK_AUGCSET_UNION || k==TK_AUGCSET_DIFF || k==TK_AUGCSET_INTER ||
           k==TK_AUGSCAN ||
           k==TK_AUGEQ    || k==TK_AUGSEQ  ||
           k==TK_AUGLT    || k==TK_AUGLE   || k==TK_AUGGT  || k==TK_AUGGE || k==TK_AUGNE ||
           k==TK_AUGSLT   || k==TK_AUGSLE  || k==TK_AUGSGT || k==TK_AUGSGE || k==TK_AUGSNE;
}

static EXPR_t *parse_assign(IcnParser *p) {
    EXPR_t *n = parse_alt(p);
    if (!n) return NULL;
    if (check(p, TK_ASSIGN)) {
        advance(p);
        return e_binary(E_ASSIGN, n, parse_assign(p));
    }
    if (check(p, TK_REVASSIGN)) {
        advance(p);
        return e_binary(E_ASSIGN, n, parse_assign(p));
    }
    if (check(p, TK_SWAP)) {
        advance(p);
        return e_binary(E_SWAP, n, parse_assign(p));
    }
    if (check(p, TK_VALSWAP)) {
        advance(p);
        return e_binary(E_SWAP, n, parse_assign(p));
    }
    if (check(p, TK_IDENTICAL)) {
        advance(p);
        return e_binary(E_IDENTICAL, n, parse_assign(p));
    }
    if (check(p, TK_NOTIDENT)) {
        advance(p);
        return e_unary(E_NOT, e_binary(E_IDENTICAL, n, parse_assign(p)));
    }
    if (is_augop(p->cur.kind)) {
        IcnTkKind aug = p->cur.kind; advance(p);
        EXPR_t *rhs = parse_assign(p);
        EXPR_t *op = expr_new(E_AUGOP);
        op->ival = (long)aug;
        push_child(op, n); push_child(op, rhs);
        return op;
    }
    if (check(p, TK_QMARK)) {
        advance(p);
        return e_binary(E_SCAN, n, parse_block_or_expr(p));
    }
    return n;
}

static EXPR_t *parse_expr(IcnParser *p) {
    int line = p->cur.line; (void)line;
    /* Control expressions valid anywhere */
    if (check(p, TK_RETURN)) {
        advance(p);
        EXPR_t *e = expr_new(E_RETURN);
        if (!check(p, TK_SEMICOL) && !check(p, TK_RPAREN) &&
            !check(p, TK_EOF)  && !check(p, TK_THEN) &&
            !check(p, TK_ELSE) && !check(p, TK_DO))
            push_child(e, parse_expr(p));
        return e;
    }
    if (check(p, TK_FAIL)) {
        advance(p);
        return expr_new(E_FAIL);
    }
    if (check(p, TK_SUSPEND)) {
        advance(p);
        EXPR_t *e = expr_new(E_SUSPEND);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        return e;
    }
    if (check(p, TK_BREAK)) { advance(p); return expr_new(E_LOOP_BREAK); }
    if (check(p, TK_NEXT))  { advance(p); return expr_new(E_LOOP_NEXT); }
    if (check(p, TK_IF)) {
        advance(p);
        EXPR_t *e = expr_new(E_IF);
        push_child(e, parse_expr(p));
        match(p, TK_SEMICOL);
        expect(p, TK_THEN, "if/then");
        push_child(e, parse_block_or_expr(p));
        match(p, TK_SEMICOL);
        if (match(p, TK_ELSE)) push_child(e, parse_block_or_expr(p));
        return e;
    }
    if (check(p, TK_EVERY)) {
        advance(p);
        EXPR_t *e = expr_new(E_EVERY);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        return e;
    }
    if (check(p, TK_WHILE)) {
        advance(p);
        EXPR_t *e = expr_new(E_WHILE);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        return e;
    }
    if (check(p, TK_UNTIL)) {
        advance(p);
        EXPR_t *e = expr_new(E_UNTIL);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        return e;
    }
    if (check(p, TK_REPEAT)) {
        advance(p);
        EXPR_t *e = expr_new(E_REPEAT);
        push_child(e, parse_block_or_expr(p));
        return e;
    }
    if (check(p, TK_CASE)) {
        advance(p);
        EXPR_t *e = expr_new(E_CASE);
        push_child(e, parse_expr(p));     /* dispatch expr */
        expect(p, TK_OF, "case expression");
        expect(p, TK_LBRACE, "case body");
        while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
            if (check(p, TK_DEFAULT)) {
                advance(p);
                expect(p, TK_COLON, "case default");
                push_child(e, parse_expr(p));
                match(p, TK_SEMICOL);
                break;
            }
            push_child(e, parse_expr(p));      /* case value */
            expect(p, TK_COLON, "case clause");
            push_child(e, parse_expr(p));      /* case result */
            match(p, TK_SEMICOL);
        }
        expect(p, TK_RBRACE, "case body end");
        return e;
    }
    return parse_assign(p);
}

/* =========================================================================
 * Block / do-clause helpers
 * ======================================================================= */

static EXPR_t *parse_block_or_expr(IcnParser *p) {
    if (!check(p, TK_LBRACE)) return parse_expr(p);
    advance(p);
    EXPR_t *seq = expr_new(E_SEQ_EXPR);
    int nc = 0;
    while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        EXPR_t *s = parse_stmt(p);
        if (!s) break;
        push_child(seq, s);
        nc++;
    }
    expect(p, TK_RBRACE, "compound block");
    if (nc == 1) {
        /* unwrap single-child seq — steal child, free wrapper */
        EXPR_t *only = seq->children[0];
        seq->nchildren = 0;
        /* expr_free(seq) would free children too; just free the node shell */
        free(seq);
        return only;
    }
    return seq;
}

static EXPR_t *parse_do_clause(IcnParser *p) {
    if (check(p, TK_DO)) { advance(p); return parse_block_or_expr(p); }
    return NULL;
}

/* =========================================================================
 * Statement parsing
 * ======================================================================= */

static EXPR_t *parse_stmt(IcnParser *p) {
    if (check(p, TK_EVERY)) {
        advance(p);
        EXPR_t *e = expr_new(E_EVERY);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_IF)) {
        advance(p);
        EXPR_t *e = expr_new(E_IF);
        push_child(e, parse_expr(p));
        match(p, TK_SEMICOL);
        expect(p, TK_THEN, "if/then");
        push_child(e, parse_block_or_expr(p));
        match(p, TK_SEMICOL);
        if (match(p, TK_ELSE)) push_child(e, parse_block_or_expr(p));
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_WHILE)) {
        advance(p);
        EXPR_t *e = expr_new(E_WHILE);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_UNTIL)) {
        advance(p);
        EXPR_t *e = expr_new(E_UNTIL);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_REPEAT)) {
        advance(p);
        EXPR_t *e = expr_new(E_REPEAT);
        push_child(e, parse_block_or_expr(p));
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_RETURN)) {
        advance(p);
        EXPR_t *e = expr_new(E_RETURN);
        if (!check(p, TK_SEMICOL)) push_child(e, parse_expr(p));
        expect(p, TK_SEMICOL, "return statement");
        return e;
    }
    if (check(p, TK_SUSPEND)) {
        advance(p);
        EXPR_t *e = expr_new(E_SUSPEND);
        push_child(e, parse_expr(p));
        EXPR_t *body = parse_do_clause(p);
        if (body) push_child(e, body);
        expect(p, TK_SEMICOL, "suspend statement");
        return e;
    }
    if (check(p, TK_FAIL)) {
        advance(p);
        expect(p, TK_SEMICOL, "fail statement");
        return expr_new(E_FAIL);
    }
    if (check(p, TK_INITIAL)) {
        advance(p);
        EXPR_t *e = expr_new(E_INITIAL);
        push_child(e, parse_block_or_expr(p));
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_CASE)) {
        EXPR_t *e = parse_expr(p);
        match(p, TK_SEMICOL);
        return e;
    }
    if (check(p, TK_LOCAL) || check(p, TK_STATIC)) {
        advance(p);
        EXPR_t *e = expr_new(E_GLOBAL);
        while (!check(p, TK_SEMICOL) && !check(p, TK_EOF)) {
            if (p->cur.kind == TK_IDENT) {
                push_child(e, e_leaf_sval(E_VAR, p->cur.val.sval.data, (int)p->cur.val.sval.len));
                advance(p);
            }
            if (!match(p, TK_COMMA)) break;
        }
        match(p, TK_SEMICOL);
        return e;
    }
    /* Expression statement */
    EXPR_t *e = parse_expr(p);
    if (!check(p, TK_RBRACE) && !check(p, TK_EOF) &&
        !check(p, TK_END)    && !check(p, TK_ELSE) && !check(p, TK_THEN) &&
        !check(p, TK_RETURN) && !check(p, TK_SUSPEND))
        expect(p, TK_SEMICOL, "expression statement");
    else
        match(p, TK_SEMICOL);
    return e;
}

/* =========================================================================
 * Record declaration
 *   record Name(field1, field2, ...)
 * Produces E_RECORD node: sval=type name, children=E_VAR field nodes
 * ======================================================================= */

static EXPR_t *parse_record(IcnParser *p) {
    expect(p, TK_RECORD, "record");
    if (p->cur.kind != TK_IDENT) { parser_error(p, "expected record name"); return NULL; }
    IcnToken name_tok = p->cur; advance(p);
    EXPR_t *e = expr_new(E_RECORD);
    e->sval = intern_n(name_tok.val.sval.data, (int)name_tok.val.sval.len);
    expect(p, TK_LPAREN, "record fields");
    while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
        if (p->cur.kind == TK_IDENT) {
            push_child(e, e_leaf_sval(E_VAR, p->cur.val.sval.data, (int)p->cur.val.sval.len));
            advance(p);
        }
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_RPAREN, "record fields");
    match(p, TK_SEMICOL);
    return e;
}

/* =========================================================================
 * Procedure parsing → E_FNC
 *
 * E_FNC layout (matches icon_lower.c ICN_PROC case, expected by icn_call_proc):
 *   e->sval          = proc name
 *   e->ival          = nparams
 *   e->children[0]   = E_VAR (proc name)
 *   e->children[1..nparams] = E_VAR param nodes
 *   e->children[nparams+1..] = body EXPR_t statements
 * ======================================================================= */

static EXPR_t *parse_proc(IcnParser *p) {
    expect(p, TK_PROCEDURE, "procedure");
    if (p->cur.kind != TK_IDENT) { parser_error(p, "expected procedure name"); return NULL; }
    IcnToken name_tok = p->cur; advance(p);

    /* params */
    EXPR_t **params = NULL; int nparams = 0, pcap = 0;
    expect(p, TK_LPAREN, "procedure params");
    while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
        if (p->cur.kind == TK_IDENT) {
            if (nparams+1 > pcap) { pcap = pcap ? pcap*2 : 4; params = realloc(params, pcap*sizeof(EXPR_t*)); }
            params[nparams++] = e_leaf_sval(E_VAR, p->cur.val.sval.data, (int)p->cur.val.sval.len);
            advance(p);
            if (check(p, TK_LBRACK)) { advance(p); match(p, TK_RBRACK); break; }
        }
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_RPAREN, "procedure params");

    /* body stmts */
    EXPR_t **stmts = NULL; int nstmts = 0, scap = 0;
    while (!check(p, TK_END) && !check(p, TK_EOF) && !p->had_error) {
        EXPR_t *s = parse_stmt(p);
        if (s) {
            if (nstmts+1 > scap) { scap = scap ? scap*2 : 8; stmts = realloc(stmts, scap*sizeof(EXPR_t*)); }
            stmts[nstmts++] = s;
        }
    }
    expect(p, TK_END, "end of procedure");

    /* Build E_FNC */
    EXPR_t *proc = expr_new(E_FNC);
    proc->sval = intern_n(name_tok.val.sval.data, (int)name_tok.val.sval.len);
    proc->ival = nparams;
    /* child[0]: name node */
    push_child(proc, e_leaf_sval(E_VAR, proc->sval, -1));
    /* children[1..nparams]: param nodes */
    for (int i = 0; i < nparams; i++) push_child(proc, params[i]);
    /* children[nparams+1..]: body stmts */
    for (int i = 0; i < nstmts; i++) push_child(proc, stmts[i]);
    free(params); free(stmts);
    return proc;
}

/* =========================================================================
 * Public API
 * ======================================================================= */

void icn_parse_init(IcnParser *p, IcnLexer *lex) {
    memset(p, 0, sizeof(*p));
    p->lex  = lex;
    p->cur  = icn_lex_next(lex);
    p->peek = icn_lex_next(lex);
}

Program *icn_parse_file(IcnParser *p) {
    Program *prog = calloc(1, sizeof(Program));
    while (!check(p, TK_EOF) && !p->had_error) {
        EXPR_t *top = NULL;
        if (check(p, TK_PROCEDURE)) {
            top = parse_proc(p);
        } else if (check(p, TK_RECORD)) {
            top = parse_record(p);
        } else if (check(p, TK_GLOBAL)) {
            advance(p);
            top = expr_new(E_GLOBAL);
            while (!check(p, TK_SEMICOL) && !check(p, TK_EOF)) {
                if (p->cur.kind == TK_IDENT) {
                    push_child(top, e_leaf_sval(E_VAR, p->cur.val.sval.data, (int)p->cur.val.sval.len));
                    advance(p);
                }
                if (!match(p, TK_COMMA)) break;
            }
            match(p, TK_SEMICOL);
        } else {
            parser_error(p, "expected 'procedure', 'record', or 'global'");
            break;
        }
        if (top) {
            STMT_t *st = calloc(1, sizeof(STMT_t));
            st->subject = top;
            st->lineno  = 0;
            st->lang    = LANG_ICN;
            if (!prog->head) prog->head = prog->tail = st;
            else           { prog->tail->next = st; prog->tail = st; }
            prog->nstmts++;
        }
    }
    return prog;
}

EXPR_t *icn_parse_expr(IcnParser *p) {
    return parse_expr(p);
}
