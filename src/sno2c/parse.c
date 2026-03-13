/*
 * parse.c — hand-rolled SNOBOL4 recursive-descent parser for snoc
 *
 * Grammar source: beauty.sno snoExpr0–snoExpr17 + snoStmt
 *
 * One C function per grammar level.  No bison, no conflicts.
 * The T_WS token is the sole binary-vs-unary discriminator:
 *   binary operators require T_WS on both sides  (from beauty.sno $'op' = snoWhite op snoWhite)
 *   unary operators have no leading whitespace requirement
 *
 * Statement parsing (Irony architecture):
 *   lex.c has already split each logical line into (label, body, goto_str).
 *   We open a fresh Lex over each field string independently.
 *
 * Level map (beauty.sno → this file):
 *   snoExpr0  → parse_expr0   =  assignment
 *   snoExpr1  → (folded into parse_expr0 as snoExpr1 is just '?')
 *   snoExpr2  → parse_expr2   &
 *   snoExpr3  → parse_expr3   | (n-ary)
 *   snoExpr4  → parse_expr4   concat (whitespace-separated, n-ary)
 *   snoExpr5  → parse_expr5   @
 *   snoExpr6  → parse_expr6   + -
 *   snoExpr7  → parse_expr7   #
 *   snoExpr8  → parse_expr8   /
 *   snoExpr9  → parse_expr9   *
 *   snoExpr10 → parse_expr10  %
 *   snoExpr11 → parse_expr11  ^ ! **
 *   snoExpr12 → parse_expr12  $ .
 *   snoExpr13 → parse_expr13  ~
 *   snoExpr14 → parse_expr14  unary prefix
 *   snoExpr15 → parse_expr15  postfix subscript
 *   snoExpr17 → parse_expr17  atom
 */

#include "snoc.h"
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static Expr *binop(EKind k, Expr *l, Expr *r) {
    Expr *e = expr_new(k); e->left=l; e->right=r; return e;
}

static Expr *unop(EKind k, Expr *operand) {
    Expr *e = expr_new(k); e->left=operand; return e;
}

/* Consume T_WS tokens (optional gray whitespace inside parens etc.). */
static void skip_ws(Lex *lx) {
    while (lex_peek(lx).kind == T_WS) lex_next(lx);
}

/* Consume one T_WS — mandatory whitespace.  Returns 1 if present, 0 if not. */
static int eat_ws(Lex *lx) {
    if (lex_peek(lx).kind == T_WS) { lex_next(lx); skip_ws(lx); return 1; }
    return 0;
}

/* ── forward declarations ─────────────────────────────────────────────────── */

static Expr *parse_expr0(Lex *lx);
static Expr *parse_expr2(Lex *lx);
static Expr *parse_expr3(Lex *lx);
static Expr *parse_expr4(Lex *lx);
static Expr *parse_expr5(Lex *lx);
static Expr *parse_expr6(Lex *lx);
static Expr *parse_expr7(Lex *lx);
static Expr *parse_expr8(Lex *lx);
static Expr *parse_expr9(Lex *lx);
static Expr *parse_expr10(Lex *lx);
static Expr *parse_expr11(Lex *lx);
static Expr *parse_expr12(Lex *lx);
static Expr *parse_expr13(Lex *lx);
static Expr *parse_expr14(Lex *lx);
static Expr *parse_expr15(Lex *lx);
static Expr *parse_expr17(Lex *lx);

/* ── expr17 — atom ────────────────────────────────────────────────────────── */
/*
 * snoExpr17 = FENCE(
 *   '(' snoGray snoExpr (',' snoXList)? ')' nPop()
 *   | snoFunction '(' snoExprList ')'
 *   | snoId '(' snoExprList ')'
 *   | snoBuiltinVar | snoSpecialNm | snoId
 *   | snoString | snoReal | snoInteger
 * )
 */

/* Parse comma-separated expression list into args/nargs on an Expr node. */
static void parse_arglist(Lex *lx, Expr ***args_out, int *nargs_out) {
    /* Dynamic array */
    int cap=4, n=0;
    Expr **args = malloc(cap * sizeof *args);

    skip_ws(lx);
    if (lex_peek(lx).kind != T_RPAREN &&
        lex_peek(lx).kind != T_RBRACKET &&
        lex_peek(lx).kind != T_RANGLE &&
        lex_peek(lx).kind != T_EOF) {
        /* parse first arg (may be empty — SNOBOL4 allows omitted args) */
        Expr *e = parse_expr0(lx);
        if (n >= cap) { cap*=2; args=realloc(args,cap*sizeof*args); }
        args[n++] = e ? e : expr_new(E_NULL);

        while (lex_peek(lx).kind == T_COMMA) {
            lex_next(lx); /* consume , */
            skip_ws(lx);
            TokKind k = lex_peek(lx).kind;
            if (k==T_RPAREN||k==T_RBRACKET||k==T_RANGLE||k==T_EOF) {
                /* trailing comma — push null arg */
                if (n>=cap){cap*=2;args=realloc(args,cap*sizeof*args);}
                args[n++] = expr_new(E_NULL);
                break;
            }
            Expr *a = parse_expr0(lx);
            if (n>=cap){cap*=2;args=realloc(args,cap*sizeof*args);}
            args[n++] = a ? a : expr_new(E_NULL);
        }
    }
    skip_ws(lx);
    *args_out  = args;
    *nargs_out = n;
}

static Expr *parse_expr17(Lex *lx) {
    Token t = lex_peek(lx);

    /* Grouped expression or alternation-group: ( expr , ... ) */
    if (t.kind == T_LPAREN) {
        lex_next(lx); skip_ws(lx);
        Expr *inner = parse_expr0(lx);
        skip_ws(lx);
        if (lex_peek(lx).kind == T_COMMA) {
            /* (expr, expr, ...) — alternation group */
            Expr *alt = inner;
            while (lex_peek(lx).kind == T_COMMA) {
                lex_next(lx); skip_ws(lx);
                Expr *r = parse_expr0(lx);
                alt = binop(E_ALT, alt, r);
                skip_ws(lx);
            }
            if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
            return alt;
        }
        skip_ws(lx);
        if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
        return inner;
    }

    /* String literal */
    if (t.kind == T_STR) {
        lex_next(lx);
        Expr *e = expr_new(E_STR); e->sval = t.sval; return e;
    }

    /* Real literal */
    if (t.kind == T_REAL) {
        lex_next(lx);
        Expr *e = expr_new(E_REAL); e->dval = t.dval; return e;
    }

    /* Integer literal */
    if (t.kind == T_INT) {
        lex_next(lx);
        Expr *e = expr_new(E_INT); e->ival = t.ival; return e;
    }

    /* Keyword variable &NAME */
    if (t.kind == T_KEYWORD) {
        lex_next(lx);
        Expr *e = expr_new(E_KEYWORD); e->sval = t.sval; return e;
    }

    /* Identifier: bare name, or function call NAME(...) */
    if (t.kind == T_IDENT) {
        lex_next(lx);
        if (lex_peek(lx).kind == T_LPAREN) {
            lex_next(lx); /* consume '(' */
            Expr **args; int nargs;
            parse_arglist(lx, &args, &nargs);
            if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
            Expr *e = expr_new(E_CALL);
            e->sval=t.sval; e->args=args; e->nargs=nargs;
            return e;
        }
        Expr *e = expr_new(E_VAR); e->sval = t.sval; return e;
    }

    /* Nothing matched */
    return NULL;
}

/* ── expr15 — postfix subscript ──────────────────────────────────────────── */
/*
 * snoExpr15 = snoExpr17 FENCE( '[' snoExprList ']' | '<' snoExprList '>' )*
 */
static Expr *parse_expr15(Lex *lx) {
    Expr *e = parse_expr17(lx);
    if (!e) return NULL;

    for (;;) {
        TokKind open = lex_peek(lx).kind;
        TokKind close;
        if      (open==T_LBRACKET) close=T_RBRACKET;
        else if (open==T_LANGLE)   close=T_RANGLE;
        else break;

        lex_next(lx); /* consume open bracket */
        Expr **args; int nargs;
        parse_arglist(lx, &args, &nargs);
        if (lex_peek(lx).kind==close) lex_next(lx);

        Expr *idx = expr_new(E_INDEX);
        idx->left=e; idx->args=args; idx->nargs=nargs;
        e = idx;
    }
    return e;
}

/* ── expr14 — unary prefix ───────────────────────────────────────────────── */
/*
 * snoExpr14 = op snoExpr14 | snoExpr15
 * op = @ ~ ? & + - * $ . ! % / # = |
 *
 * No leading whitespace — if there's whitespace before this call, the
 * binary-operator level already handled it.
 */
static Expr *parse_expr14(Lex *lx) {
    Token t = lex_peek(lx);
    EKind uk;
    switch (t.kind) {
        case T_AT:     uk=E_AT;    break;
        case T_TILDE:  uk=E_DEREF; break;  /* ~ is NOT in snoc.h so map to DEREF for now */
        case T_QMARK:  uk=E_COND;  break;  /* unary ? = interrogation */
        case T_AMP:    uk=E_REDUCE;break;
        case T_PLUS:   lex_next(lx); return parse_expr14(lx); /* unary + is identity */
        case T_MINUS:  uk=E_NEG;   break;
        case T_STAR:   uk=E_DEREF; break;  /* *X = unevaluated expression */
        case T_DOLLAR: uk=E_DEREF; break;  /* $X = indirect reference */
        case T_DOT:    uk=E_COND;  break;  /* .X = name */
        case T_BANG:   uk=E_POW;   break;  /* !X = definable unary */
        case T_PCT:    uk=E_DIV;   break;  /* %X = definable unary */
        case T_SLASH:  uk=E_DIV;   break;  /* /X = definable unary */
        case T_HASH:   uk=E_MUL;   break;  /* #X = definable unary */
        case T_EQ:     uk=E_ASSIGN;break;  /* =X = definable unary */
        case T_PIPE:   uk=E_ALT;   break;  /* |X = definable unary */
        default:       return parse_expr15(lx);
    }
    TokKind op_tok = t.kind;
    lex_next(lx);
    Expr *operand = parse_expr14(lx);
    if (!operand) {
        snoc_error(lx->lineno, "expected operand after unary operator");
        return expr_new(E_NULL);
    }
    /* emit.c contract for E_DEREF:
     *   *X  (deferred ref):  e->left = operand, e->right = NULL
     *   $X  (indirect):      e->left = NULL,    e->right = operand
     * All other unary ops use e->left (via unop). */
    if (uk == E_DEREF && op_tok == T_DOLLAR) {
        Expr *e = expr_new(E_DEREF);
        e->right = operand;   /* $X — indirect: right holds operand */
        return e;
    }
    return unop(uk, operand);  /* *X and all others: left holds operand */
}

/* ── expr13 — ~ binary ───────────────────────────────────────────────────── */
/* snoExpr13 = snoExpr14 FENCE($'~' snoExpr13 | ε) */
static Expr *parse_expr13(Lex *lx) {
    Expr *l = parse_expr14(lx);
    if (!l) return NULL;
    for (;;) {
        /* binary ~ requires WS ~ WS */
        LexMark m13 = lex_mark(lx);
        if (lex_peek(lx).kind != T_WS) break;
        lex_next(lx); /* consume WS */
        if (lex_peek(lx).kind != T_TILDE) {
            lex_restore(lx, m13);
            break;
        }
        lex_next(lx); /* consume ~ */
        skip_ws(lx);  /* consume trailing WS of binary ~ */
        Expr *r = parse_expr13(lx);
        l = binop(E_CONCAT, l, r);  /* ~ is CONCAT in snoc; not in snoc.h, map to E_CONCAT */
    }
    return l;
}

/* ── expr12 — $ . binary ─────────────────────────────────────────────────── */
/* snoExpr12 = snoExpr13 FENCE(($'$' | $'.') snoExpr12 | ε) — right-assoc */
static Expr *parse_expr12(Lex *lx) {
    Expr *l = parse_expr13(lx);
    if (!l) return NULL;
    LexMark m12 = lex_mark(lx);
    if (lex_peek(lx).kind != T_WS) return l;
    lex_next(lx); /* consume WS */
    TokKind op = lex_peek(lx).kind;
    if (op == T_DOLLAR || op == T_DOT) {
        lex_next(lx); skip_ws(lx);
        Expr *r = parse_expr12(lx); /* right-associative */
        EKind k = (op==T_DOLLAR) ? E_IMM : E_COND;
        return binop(k, l, r);
    }
    lex_restore(lx, m12);
    return l;
}

/* ── Generic left-associative binary level helper ────────────────────────── */
/*
 * parse_lbin(lx, next_fn, ops, ekinds, nops)
 * next_fn: function for the tighter-binding level
 * ops:     array of TokKind values that trigger this level
 * ekinds:  corresponding EKind values
 * nops:    length of ops/ekinds arrays
 */
typedef Expr *(*ParseFn)(Lex *);

static Expr *parse_lbin(Lex *lx, ParseFn next_fn,
                         const TokKind *ops, const EKind *ekinds, int nops) {
    Expr *l = next_fn(lx);
    if (!l) return NULL;
    for (;;) {
        LexMark m = lex_mark(lx);
        if (lex_peek(lx).kind != T_WS) break;
        lex_next(lx); /* consume WS */
        TokKind k = lex_peek(lx).kind;
        int found = -1;
        for (int i=0; i<nops; i++) if (k==ops[i]) { found=i; break; }
        if (found < 0) {
            lex_restore(lx, m); /* restore to before WS */
            break;
        }
        lex_next(lx); /* consume operator */
        skip_ws(lx);  /* consume trailing WS of binary op */
        Expr *r = next_fn(lx);
        l = binop(ekinds[found], l, r);
    }
    return l;
}

/* ── Right-associative binary level helper ───────────────────────────────── */
static Expr *parse_rbin(Lex *lx, ParseFn next_fn,
                         const TokKind *ops, const EKind *ekinds, int nops) {
    Expr *l = next_fn(lx);
    if (!l) return NULL;
    LexMark m = lex_mark(lx);
    if (lex_peek(lx).kind != T_WS) return l;
    lex_next(lx); /* consume WS */
    TokKind k = lex_peek(lx).kind;
    int found = -1;
    for (int i=0; i<nops; i++) if (k==ops[i]) { found=i; break; }
    if (found < 0) {
        lex_restore(lx, m);
        return l;
    }
    lex_next(lx); skip_ws(lx);
    Expr *r = parse_rbin(lx, next_fn, ops, ekinds, nops); /* recurse right */
    return binop(ekinds[found], l, r);
}

/* ── expr11 — ^ ! ** (right-assoc) ──────────────────────────────────────── */
static Expr *parse_expr11(Lex *lx) {
    static const TokKind ops[]   = { T_CARET, T_BANG, T_STARSTAR };
    static const EKind   kinds[] = { E_POW,   E_POW,  E_POW      };
    /* ** must be checked before * — handled in lexer (T_STARSTAR) */
    return parse_rbin(lx, parse_expr12, ops, kinds, 3);
}

/* ── expr10 — % ──────────────────────────────────────────────────────────── */
static Expr *parse_expr10(Lex *lx) {
    static const TokKind ops[]   = { T_PCT  };
    static const EKind   kinds[] = { E_DIV  };   /* % maps to DIV slot (user-definable) */
    return parse_lbin(lx, parse_expr11, ops, kinds, 1);
}

/* ── expr9 — * ───────────────────────────────────────────────────────────── */
static Expr *parse_expr9(Lex *lx) {
    static const TokKind ops[]   = { T_STAR };
    static const EKind   kinds[] = { E_MUL  };
    return parse_lbin(lx, parse_expr10, ops, kinds, 1);
}

/* ── expr8 — / ───────────────────────────────────────────────────────────── */
static Expr *parse_expr8(Lex *lx) {
    static const TokKind ops[]   = { T_SLASH };
    static const EKind   kinds[] = { E_DIV   };
    return parse_lbin(lx, parse_expr9, ops, kinds, 1);
}

/* ── expr7 — # ───────────────────────────────────────────────────────────── */
static Expr *parse_expr7(Lex *lx) {
    static const TokKind ops[]   = { T_HASH };
    static const EKind   kinds[] = { E_MUL  };   /* # = user-definable binary, map to MUL slot */
    return parse_lbin(lx, parse_expr8, ops, kinds, 1);
}

/* ── expr6 — + - ─────────────────────────────────────────────────────────── */
static Expr *parse_expr6(Lex *lx) {
    static const TokKind ops[]   = { T_PLUS, T_MINUS };
    static const EKind   kinds[] = { E_ADD,  E_SUB   };
    return parse_lbin(lx, parse_expr7, ops, kinds, 2);
}

/* ── expr5 — @ ───────────────────────────────────────────────────────────── */
static Expr *parse_expr5(Lex *lx) {
    static const TokKind ops[]   = { T_AT };
    static const EKind   kinds[] = { E_AT };
    return parse_lbin(lx, parse_expr6, ops, kinds, 1);
}

/* ── expr4 — concatenation (whitespace-separated) ───────────────────────── */
/*
 * snoX4 = nInc() snoExpr5 FENCE(snoWhite snoX4 | ε)
 *
 * After parse_expr5 returns, if the next token is T_WS and the token after
 * that is an atom (not a binary operator), it's a concat.
 *
 * "Not a binary operator" = not one of the operators that parse_expr5..12
 * consume after WS.  Those are: @ + - # / * % ^ ! ** $ . ~ (and = ? & | for
 * higher levels).  Everything else (identifier, literal, unary-prefix, '(')
 * is a concat continuation.
 *
 * We implement this by trying parse_expr5 after WS; if the first token is a
 * binary operator, we put the WS back and stop.
 */

/* Returns 1 if tok can start a new concat item (i.e., is not a binary op). */
static int is_concat_start(TokKind k) {
    switch (k) {
        /* binary operators that must be surrounded by WS */
        case T_AT: case T_PLUS: case T_MINUS: case T_HASH:
        case T_SLASH: case T_STAR: case T_PCT: case T_CARET:
        case T_BANG: case T_STARSTAR: case T_DOT:
        case T_TILDE: case T_EQ: case T_QMARK: case T_AMP: case T_PIPE:
        /* terminators */
        case T_COMMA: case T_RPAREN: case T_RBRACKET: case T_RANGLE:
        case T_COLON: case T_EOF: case T_ERR:
            return 0;
        default:
            return 1;
    }
}

static Expr *parse_expr4(Lex *lx) {
    Expr *first = parse_expr5(lx);
    if (!first) return NULL;

    /* Collect concat items */
    int cap=4, n=1;
    Expr **items = malloc(cap * sizeof *items);
    items[0] = first;

    for (;;) {
        LexMark mc = lex_mark(lx);
        if (lex_peek(lx).kind != T_WS) break;
        lex_next(lx); /* consume WS tentatively */
        TokKind pk2 = lex_peek(lx).kind;
        if (!is_concat_start(pk2)) {
            lex_restore(lx, mc); /* restore before WS — belongs to higher level */
            break;
        }
        Expr *next = parse_expr5(lx);
        if (!next) {
            lex_restore(lx, mc);
            break;
        }
        if (n >= cap) { cap*=2; items=realloc(items,cap*sizeof*items); }
        items[n++] = next;
    }

    if (n == 1) { free(items); return first; }

    /* Build left-associative chain of E_CONCAT */
    Expr *e = items[0];
    for (int i=1; i<n; i++) e = binop(E_CONCAT, e, items[i]);
    free(items);
    return e;
}

/* ── expr3 — | (alternation, n-ary) ─────────────────────────────────────── */
/* snoX3 = nInc() snoExpr4 FENCE($'|' snoX3 | ε) */
static Expr *parse_expr3(Lex *lx) {
    Expr *l = parse_expr4(lx);
    if (!l) return NULL;
    for (;;) {
        LexMark m3 = lex_mark(lx);
        if (lex_peek(lx).kind != T_WS) break;
        lex_next(lx);
        if (lex_peek(lx).kind != T_PIPE) {
            lex_restore(lx, m3);
            break;
        }
        lex_next(lx); skip_ws(lx);
        Expr *r = parse_expr4(lx);
        l = binop(E_ALT, l, r);
    }
    return l;
}

/* ── expr2 — & ───────────────────────────────────────────────────────────── */
/* snoExpr2 = snoExpr3 FENCE($'&' snoExpr2 | ε) */
static Expr *parse_expr2(Lex *lx) {
    static const TokKind ops[]   = { T_AMP    };
    static const EKind   kinds[] = { E_REDUCE };
    return parse_lbin(lx, parse_expr3, ops, kinds, 1);
}

/* ── expr0 — = assignment and ? scan ────────────────────────────────────── */
/*
 * snoExpr0 = snoExpr1 FENCE($'=' snoExpr0 | ε)
 * snoExpr1 = snoExpr2 FENCE($'?' snoExpr1 | ε)
 *
 * Folded together: expr0 → expr2, then loop on '=' or '?'
 * Both are right-associative.
 */
static Expr *parse_expr0(Lex *lx) {
    Expr *l = parse_expr2(lx);
    if (!l) return NULL;
    LexMark m0 = lex_mark(lx);
    if (lex_peek(lx).kind != T_WS) return l;
    lex_next(lx);
    TokKind k = lex_peek(lx).kind;
    if (k == T_EQ) {
        lex_next(lx); skip_ws(lx);
        Expr *r = parse_expr0(lx);
        return binop(E_ASSIGN, l, r);
    }
    if (k == T_QMARK) {
        lex_next(lx); skip_ws(lx);
        Expr *r = parse_expr0(lx);
        return binop(E_COND, l, r);
    }
    lex_restore(lx, m0);
    return l;
}

/* Public expression entry point */
static Expr *parse_expr(Lex *lx) {
    skip_ws(lx);
    return parse_expr0(lx);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Goto field parser
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * goto_str has already had the leading ':' stripped by lex.c.
 * Format:  [(label)]  [S(label)]  [F(label)]  in any order/combination.
 * 'S'/'F' are ordinary identifiers in the goto field lexer.
 */

/* Parse the label inside a goto target: ( label ) or < label >
 *
 * label can be:
 *   IDENT            — plain label          :(LOOP)
 *   $IDENT           — indirect simple      :($var)
 *   $(expr)          — computed goto        :($('pp_' t))
 *   &KEYWORD         — keyword goto         :(&RETURN)
 *   END              — goto END             :(END)
 *
 * For $(expr) we parse the full expression and store it as E_DEREF on a
 * special "$COMPUTED" label name — the emitter already handles this.
 * We return the label as a string for simple cases, or "$COMPUTED" for
 * computed cases (the Stmt.go field carries the expr separately — but since
 * SnoGoto only stores char* we encode it as the string "$(...)" for now
 * and let the emitter handle it via the existing computed-goto path).
 */
static char *parse_goto_label(Lex *lx) {
    TokKind open = lex_peek(lx).kind;
    TokKind close;
    if      (open==T_LPAREN) close=T_RPAREN;
    else if (open==T_LANGLE) close=T_RANGLE;
    else return NULL;
    lex_next(lx); skip_ws(lx);

    Token t = lex_peek(lx);
    char *label = NULL;

    if (t.kind==T_IDENT || t.kind==T_KEYWORD || t.kind==T_END) {
        lex_next(lx);
        label = (char *)t.sval;
    } else if (t.kind==T_DOLLAR) {
        lex_next(lx);
        if (lex_peek(lx).kind==T_LPAREN) {
            /* $(expr) — computed goto: consume balanced parens, store as $COMPUTED */
            int depth=1;
            lex_next(lx); /* consume '(' */
            /* scan raw source to grab the expression text */
            int start = lx->pos;
            while (!lex_at_end(lx) && depth > 0) {
                Token tok = lex_next(lx);
                if (tok.kind==T_LPAREN) depth++;
                else if (tok.kind==T_RPAREN) depth--;
            }
            /* re-parse the expression properly */
            int end = lx->pos - 1; /* back up past closing ) */
            Lex sub = {0};
            lex_open_str(&sub, lx->src + start, end - start, lx->lineno);
            (void)parse_expr0(&sub); /* parse but discard — emitter uses $COMPUTED */
            label = strdup("$COMPUTED");
        } else {
            /* $IDENT — simple indirect */
            Token n2 = lex_next(lx);
            char buf[512];
            snprintf(buf, sizeof buf, "$%s", n2.sval ? n2.sval : "?");
            label = strdup(buf);
        }
    } else if (t.kind==T_LPAREN) {
        /* nested parens — consume and mark computed */
        int depth=1;
        lex_next(lx);
        while (!lex_at_end(lx) && depth>0) {
            Token tok = lex_next(lx);
            if (tok.kind==T_LPAREN) depth++;
            else if (tok.kind==T_RPAREN) depth--;
        }
        label = strdup("$COMPUTED");
    }

    skip_ws(lx);
    if (lex_peek(lx).kind==close) lex_next(lx);
    return label;
}

static SnoGoto *parse_goto_field(const char *goto_str, int lineno) {
    if (!goto_str || !*goto_str) return NULL;

    Lex lx_obj = {0}, *lx = &lx_obj;
    lex_open_str(lx, goto_str, (int)strlen(goto_str), lineno);
    skip_ws(lx);

    SnoGoto *g = sgoto_new();

    while (!lex_at_end(lx)) {
        Token t = lex_peek(lx);
        if (t.kind==T_IDENT && t.sval) {
            if (strcasecmp(t.sval,"S")==0) {
                lex_next(lx);
                g->onsuccess = parse_goto_label(lx);
                skip_ws(lx); continue;
            }
            if (strcasecmp(t.sval,"F")==0) {
                lex_next(lx);
                g->onfailure = parse_goto_label(lx);
                skip_ws(lx); continue;
            }
        }
        /* unconditional: ( label ) */
        if (t.kind==T_LPAREN || t.kind==T_LANGLE) {
            g->uncond = parse_goto_label(lx);
            skip_ws(lx); continue;
        }
        /* unexpected */
        snoc_error(lineno, "unexpected token in goto field");
        lex_next(lx);
    }

    if (!g->onsuccess && !g->onfailure && !g->uncond) { free(g); return NULL; }
    return g;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Statement parser — one SnoLine → one Stmt
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * body field format (from snoStmt in beauty.sno):
 *
 *   snoExpr14 FENCE(
 *     ε WS ('=' WS snoExpr | ε)          ← subject-only or subject '=' repl
 *   | ($'?' | WS) snoExpr1               ← subject WS pattern
 *       FENCE( WS '=' WS snoExpr | ε )   ←   with optional replacement
 *   | ε                                   ← just the subject
 *   )
 *
 * We parse the body string with a fresh Lex and implement the same FENCE
 * logic as a simple lookahead sequence.
 */

static Stmt *parse_body_field(const char *body, int lineno) {
    if (!body || !*body) return NULL;

    Lex lx_obj = {0}, *lx = &lx_obj;
    lex_open_str(lx, body, (int)strlen(body), lineno);
    skip_ws(lx);

    Stmt *s = stmt_new();
    s->lineno = lineno;

    /* Subject — parsed at unary level (snoExpr14) */
    s->subject = parse_expr14(lx);

    /* After subject: WS then one of:
         '=' → assignment (no pattern)
         expr → pattern (requires WS between subject and pattern)
         nothing → invocation
    */
    if (lex_peek(lx).kind == T_WS) {
        lex_next(lx); skip_ws(lx); /* consume WS */

        if (lex_peek(lx).kind == T_EQ) {
            /* subject = [replacement] */
            lex_next(lx); /* consume '=' */
            skip_ws(lx);
            if (!lex_at_end(lx))
                s->replacement = parse_expr(lx);
        } else if (!lex_at_end(lx)) {
            /* subject WS pattern [= replacement]
             * Use parse_expr2 (not parse_expr0) so trailing '=' is NOT
             * consumed as an assignment operator inside the pattern. */
            s->pattern = parse_expr2(lx);

            if (lex_peek(lx).kind == T_WS) {
                lex_next(lx); skip_ws(lx);
                if (lex_peek(lx).kind == T_EQ) {
                    lex_next(lx); skip_ws(lx);
                    if (!lex_at_end(lx))
                        s->replacement = parse_expr(lx);
                } else {
                    lx->peek = (Token){T_WS, NULL, 0, 0, lineno};
                    lx->peeked = 1;
                }
            }
        }
    }

    return s;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Top-level: convert LineArray → Program
 * ═══════════════════════════════════════════════════════════════════════════ */

Program *parse_program(LineArray *lines) {
    Program *prog = calloc(1, sizeof *prog);

    for (int i=0; i<lines->n; i++) {
        SnoLine *sl = &lines->a[i];
        Stmt *s;

        if (sl->is_end) {
            s = stmt_new();
            s->label   = sl->label ? strdup(sl->label) : strdup("END");
            s->is_end  = 1;
            s->lineno  = sl->lineno;
        } else {
            s = parse_body_field(sl->body, sl->lineno);
            if (!s) s = stmt_new();   /* label-only or blank line */
            s->lineno = sl->lineno;
            if (sl->label) s->label = strdup(sl->label);
            s->go = parse_goto_field(sl->goto_str, sl->lineno);
        }

        s->next = NULL;
        if (!prog->head) prog->head = prog->tail = s;
        else { prog->tail->next = s; prog->tail = s; }
        prog->nstmts++;
    }

    return prog;
}
