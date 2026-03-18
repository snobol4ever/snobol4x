/*
 * parse.c — hand-rolled SNOBOL4 recursive-descent parser for sno2c
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
 *   snoExpr4  → parse_expr4   CONCAT_fn (whitespace-separated, n-ary)
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

#include "sno2c.h"
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static EXPR_t *binop(EKind k, EXPR_t *l, EXPR_t *r) {
    EXPR_t *e = expr_new(k); e->left=l; e->right=r; return e;
}

static EXPR_t *unop(EKind k, EXPR_t *operand) {
    EXPR_t *e = expr_new(k); e->left=operand; return e;
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

static EXPR_t *parse_expr0(Lex *lx);
static EXPR_t *parse_expr2(Lex *lx);
static EXPR_t *parse_expr3(Lex *lx);
static EXPR_t *parse_expr4(Lex *lx);
static EXPR_t *parse_expr5(Lex *lx);
static EXPR_t *parse_expr6(Lex *lx);
static EXPR_t *parse_expr7(Lex *lx);
static EXPR_t *parse_expr8(Lex *lx);
static EXPR_t *parse_expr9(Lex *lx);
static EXPR_t *parse_expr10(Lex *lx);
static EXPR_t *parse_expr11(Lex *lx);
static EXPR_t *parse_expr12(Lex *lx);
static EXPR_t *parse_expr13(Lex *lx);
static EXPR_t *parse_expr14(Lex *lx);
static EXPR_t *parse_expr15(Lex *lx);
static EXPR_t *parse_expr17(Lex *lx);

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

/* Parse comma-separated expression list into args/nargs on an EXPR_t node. */
static void parse_arglist(Lex *lx, EXPR_t ***args_out, int *nargs_out) {
    /* Dynamic array */
    int cap=4, n=0;
    EXPR_t **args = malloc(cap * sizeof *args);

    skip_ws(lx);
    if (lex_peek(lx).kind != T_RPAREN &&
        lex_peek(lx).kind != T_RBRACKET &&
        lex_peek(lx).kind != T_RANGLE &&
        lex_peek(lx).kind != T_EOF) {
        /* parse first arg (may be empty — SNOBOL4 allows omitted args) */
        EXPR_t *e = parse_expr0(lx);
        if (n >= cap) { cap*=2; args=realloc(args,cap*sizeof*args); }
        args[n++] = e ? e : expr_new(E_NULV);

        while (lex_peek(lx).kind == T_COMMA) {
            lex_next(lx); /* consume , */
            skip_ws(lx);
            TokKind k = lex_peek(lx).kind;
            if (k==T_RPAREN||k==T_RBRACKET||k==T_RANGLE||k==T_EOF) {
                /* trailing comma — PUSH_fn null arg */
                if (n>=cap){cap*=2;args=realloc(args,cap*sizeof*args);}
                args[n++] = expr_new(E_NULV);
                break;
            }
            EXPR_t *a = parse_expr0(lx);
            if (n>=cap){cap*=2;args=realloc(args,cap*sizeof*args);}
            args[n++] = a ? a : expr_new(E_NULV);
        }
    }
    skip_ws(lx);
    *args_out  = args;
    *nargs_out = n;
}

static EXPR_t *parse_expr17(Lex *lx) {
    Token t = lex_peek(lx);

    /* Grouped expression or alternation-group: ( expr , ... ) */
    if (t.kind == T_LPAREN) {
        lex_next(lx); skip_ws(lx);
        EXPR_t *inner = parse_expr0(lx);
        skip_ws(lx);
        if (lex_peek(lx).kind == T_COMMA) {
            /* (expr, expr, ...) — alternation group */
            EXPR_t *alt = inner;
            while (lex_peek(lx).kind == T_COMMA) {
                lex_next(lx); skip_ws(lx);
                EXPR_t *r = parse_expr0(lx);
                alt = binop(E_OR, alt, r);
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
        EXPR_t *e = expr_new(E_QLIT); e->sval = t.sval; return e;
    }

    /* Real literal */
    if (t.kind == T_REAL) {
        lex_next(lx);
        EXPR_t *e = expr_new(E_FLIT); e->dval = t.dval; return e;
    }

    /* Integer literal */
    if (t.kind == T_INT) {
        lex_next(lx);
        EXPR_t *e = expr_new(E_ILIT); e->ival = t.ival; return e;
    }

    /* Keyword variable &NAME */
    if (t.kind == T_KEYWORD) {
        lex_next(lx);
        EXPR_t *e = expr_new(E_KW); e->sval = t.sval; return e;
    }

    /* Identifier: bare name, or function call NAME(...) */
    if (t.kind == T_IDENT) {
        lex_next(lx);
        if (lex_peek(lx).kind == T_LPAREN) {
            lex_next(lx); /* consume '(' */
            EXPR_t **args; int nargs;
            parse_arglist(lx, &args, &nargs);
            if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
            EXPR_t *e = expr_new(E_FNC);
            e->sval=t.sval; e->args=args; e->nargs=nargs;
            return e;
        }
        EXPR_t *e = expr_new(E_VART); e->sval = t.sval; return e;
    }

    /* Nothing matched */
    return NULL;
}

/* ── expr15 — postfix subscript ──────────────────────────────────────────── */
/*
 * snoExpr15 = snoExpr17 FENCE( '[' snoExprList ']' | '<' snoExprList '>' )*
 */
static EXPR_t *parse_expr15(Lex *lx) {
    EXPR_t *e = parse_expr17(lx);
    if (!e) return NULL;

    for (;;) {
        TokKind open = lex_peek(lx).kind;
        TokKind close;
        if      (open==T_LBRACKET) close=T_RBRACKET;
        else if (open==T_LANGLE)   close=T_RANGLE;
        else break;

        lex_next(lx); /* consume open bracket */
        EXPR_t **args; int nargs;
        parse_arglist(lx, &args, &nargs);
        if (lex_peek(lx).kind==close) lex_next(lx);

        EXPR_t *idx = expr_new(E_IDX);
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
static EXPR_t *parse_expr14(Lex *lx) {
    Token t = lex_peek(lx);
    EKind uk;
    switch (t.kind) {
        case T_AT:     uk=E_ATP;    break;
        case T_TILDE:  uk=E_INDR; break;  /* ~ is NOT in sno2c.h so map to DEREF for now */
        case T_QMARK:  uk=E_NAM;  break;  /* unary ? = interrogation */
        case T_AMP:    uk=E_OPSYN;break;
        case T_PLUS:   lex_next(lx); return parse_expr14(lx); /* unary + is identity */
        case T_MINUS:  uk=E_MNS;   break;
        case T_STAR:   uk=E_INDR; break;  /* *X = unevaluated expression */
        case T_DOLLAR: uk=E_INDR; break;  /* $X = indirect reference */
        case T_DOT:    uk=E_NAM;  break;  /* .X = name */
        case T_BANG:   uk=E_EXPOP;   break;  /* !X = definable unary */
        case T_PCT:    uk=E_DIV;   break;  /* %X = definable unary */
        case T_SLASH:  uk=E_DIV;   break;  /* /X = definable unary */
        case T_HASH:   uk=E_MPY;   break;  /* #X = definable unary */
        case T_EQ:     uk=E_ASGN;break;  /* =X = definable unary */
        case T_PIPE:   uk=E_OR;   break;  /* |X = definable unary */
        default:       return parse_expr15(lx);
    }
    TokKind op_tok = t.kind;
    lex_next(lx);
    EXPR_t *operand = parse_expr14(lx);
    if (!operand) {
        snoc_error(lx->lineno, "expected operand after unary operator");
        return expr_new(E_NULV);
    }
    /* emit.c contract for E_INDR:
     *   *X  (deferred ref):  e->left = operand, e->right = NULL
     *   $X  (indirect):      e->left = NULL,    e->right = operand
     * All other unary ops use e->left (via unop). */
    if (uk == E_INDR && op_tok == T_DOLLAR) {
        EXPR_t *e = expr_new(E_INDR);
        e->right = operand;   /* $X — indirect: right holds operand */
        return e;
    }
    return unop(uk, operand);  /* *X and all others: left holds operand */
}

/* ── expr13 — ~ binary ───────────────────────────────────────────────────── */
/* snoExpr13 = snoExpr14 FENCE($'~' snoExpr13 | ε) */
static EXPR_t *parse_expr13(Lex *lx) {
    EXPR_t *l = parse_expr14(lx);
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
        EXPR_t *r = parse_expr13(lx);
        /* ~ 'tag': emit E_NAM(child=l, tag=r) so emit_byrd generates Shift(tag, matched) */
        l = binop(E_NAM, l, r);
    }
    return l;
}

/* ── expr12 — $ . binary ─────────────────────────────────────────────────── */
/* snoExpr12 = snoExpr13 (($'$' | $'.') snoExpr13)* — left-assoc (Gimpel §5.2) */
static EXPR_t *parse_expr12(Lex *lx) {
    EXPR_t *l = parse_expr13(lx);
    if (!l) return NULL;
    for (;;) {
        LexMark m12 = lex_mark(lx);
        if (lex_peek(lx).kind != T_WS) break;
        lex_next(lx); /* consume WS */
        TokKind op = lex_peek(lx).kind;
        if (op == T_DOLLAR || op == T_DOT) {
            lex_next(lx); skip_ws(lx);
            EXPR_t *r = parse_expr13(lx); /* left-associative: only one level */
            EKind k = (op==T_DOLLAR) ? E_DOL : E_NAM;
            l = binop(k, l, r);
        } else {
            lex_restore(lx, m12);
            break;
        }
    }
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
typedef EXPR_t *(*ParseFn)(Lex *);

static EXPR_t *parse_lbin(Lex *lx, ParseFn next_fn,
                         const TokKind *ops, const EKind *ekinds, int nops) {
    EXPR_t *l = next_fn(lx);
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
        /* T_STAR is binary only when followed by WS on both sides (a * b).
         * If the token after * is not WS, it's unary *foo — not ours. */
        if (k == T_STAR) {
            LexMark m2 = lex_mark(lx);
            lex_next(lx); /* consume * tentatively */
            TokKind k2 = lex_peek(lx).kind;
            lex_restore(lx, m2);
            if (k2 != T_WS) {
                lex_restore(lx, m); /* restore before WS — let CONCAT_fn handle it */
                break;
            }
        }
        lex_next(lx); /* consume operator */
        skip_ws(lx);  /* consume trailing WS of binary op */
        EXPR_t *r = next_fn(lx);
        l = binop(ekinds[found], l, r);
    }
    return l;
}

/* ── Right-associative binary level helper ───────────────────────────────── */
static EXPR_t *parse_rbin(Lex *lx, ParseFn next_fn,
                         const TokKind *ops, const EKind *ekinds, int nops) {
    EXPR_t *l = next_fn(lx);
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
    EXPR_t *r = parse_rbin(lx, next_fn, ops, ekinds, nops); /* recurse right */
    return binop(ekinds[found], l, r);
}

/* ── expr11 — ^ ! ** (right-assoc) ──────────────────────────────────────── */
static EXPR_t *parse_expr11(Lex *lx) {
    static const TokKind ops[]   = { T_CARET, T_BANG, T_STARSTAR };
    static const EKind   kinds[] = { E_EXPOP,   E_EXPOP,  E_EXPOP      };
    /* ** must be checked before * — handled in lexer (T_STARSTAR) */
    return parse_rbin(lx, parse_expr12, ops, kinds, 3);
}

/* ── expr10 — % ──────────────────────────────────────────────────────────── */
static EXPR_t *parse_expr10(Lex *lx) {
    static const TokKind ops[]   = { T_PCT  };
    static const EKind   kinds[] = { E_DIV  };   /* % maps to DIV slot (user-definable) */
    return parse_lbin(lx, parse_expr11, ops, kinds, 1);
}

/* ── expr9 — * ───────────────────────────────────────────────────────────── */
static EXPR_t *parse_expr9(Lex *lx) {
    static const TokKind ops[]   = { T_STAR };
    static const EKind   kinds[] = { E_MPY  };
    return parse_lbin(lx, parse_expr10, ops, kinds, 1);
}

/* ── expr8 — / ───────────────────────────────────────────────────────────── */
static EXPR_t *parse_expr8(Lex *lx) {
    static const TokKind ops[]   = { T_SLASH };
    static const EKind   kinds[] = { E_DIV   };
    return parse_lbin(lx, parse_expr9, ops, kinds, 1);
}

/* ── expr7 — # ───────────────────────────────────────────────────────────── */
static EXPR_t *parse_expr7(Lex *lx) {
    static const TokKind ops[]   = { T_HASH };
    static const EKind   kinds[] = { E_MPY  };   /* # = user-definable binary, map to MUL slot */
    return parse_lbin(lx, parse_expr8, ops, kinds, 1);
}

/* ── expr6 — + - ─────────────────────────────────────────────────────────── */
static EXPR_t *parse_expr6(Lex *lx) {
    static const TokKind ops[]   = { T_PLUS, T_MINUS };
    static const EKind   kinds[] = { E_ADD,  E_SUB   };
    return parse_lbin(lx, parse_expr7, ops, kinds, 2);
}

/* ── expr5 — @ ───────────────────────────────────────────────────────────── */
static EXPR_t *parse_expr5(Lex *lx) {
    static const TokKind ops[]   = { T_AT };
    static const EKind   kinds[] = { E_ATP };
    return parse_lbin(lx, parse_expr6, ops, kinds, 1);
}

/* ── expr4 — concatenation (whitespace-separated) ───────────────────────── */
/*
 * snoX4 = nInc() snoExpr5 FENCE(snoWhite snoX4 | ε)
 *
 * After parse_expr5 returns, if the next token is T_WS and the token after
 * that is an atom (not a binary operator), it's a CONCAT_fn.
 *
 * "Not a binary operator" = not one of the operators that parse_expr5..12
 * consume after WS.  Those are: @ + - # / * % ^ ! ** $ . ~ (and = ? & | for
 * higher levels).  Everything else (identifier, literal, unary-prefix, '(')
 * is a CONCAT_fn continuation.
 *
 * We implement this by trying parse_expr5 after WS; if the first token is a
 * binary operator, we put the WS back and stop.
 */

/* Returns 1 if tok can start a new CONCAT_fn item (i.e., is not a binary op). */
static int is_concat_start(TokKind k) {
    switch (k) {
        /* binary operators that must be surrounded by WS */
        /* NOTE: T_STAR excluded — after whitespace, * is always unary
         * (deferred pattern ref *foo). Binary * needs spaces: a * b. */
        case T_AT: case T_PLUS: case T_MINUS: case T_HASH:
        case T_SLASH: case T_PCT: case T_CARET:
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

static EXPR_t *parse_expr4(Lex *lx) {
    EXPR_t *first = parse_expr5(lx);
    if (!first) return NULL;

    /* Collect CONCAT_fn items */
    int cap=4, n=1;
    EXPR_t **items = malloc(cap * sizeof *items);
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
        EXPR_t *next = parse_expr5(lx);
        if (!next) {
            lex_restore(lx, mc);
            break;
        }
        if (n >= cap) { cap*=2; items=realloc(items,cap*sizeof*items); }
        items[n++] = next;
    }

    if (n == 1) { free(items); return first; }

    /* Build left-associative chain of E_CONC */
    EXPR_t *e = items[0];
    for (int i=1; i<n; i++) e = binop(E_CONC, e, items[i]);
    free(items);
    return e;
}

/* ── expr3 — | (alternation, n-ary) ─────────────────────────────────────── */
/* snoX3 = nInc() snoExpr4 FENCE($'|' snoX3 | ε) */
static EXPR_t *parse_expr3(Lex *lx) {
    EXPR_t *l = parse_expr4(lx);
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
        EXPR_t *r = parse_expr4(lx);
        l = binop(E_OR, l, r);
    }
    return l;
}

/* ── expr2 — & ───────────────────────────────────────────────────────────── */
/* snoExpr2 = snoExpr3 FENCE($'&' snoExpr2 | ε) */
static EXPR_t *parse_expr2(Lex *lx) {
    static const TokKind ops[]   = { T_AMP    };
    static const EKind   kinds[] = { E_OPSYN };
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
static EXPR_t *parse_expr0(Lex *lx) {
    EXPR_t *l = parse_expr2(lx);
    if (!l) return NULL;
    LexMark m0 = lex_mark(lx);
    if (lex_peek(lx).kind != T_WS) return l;
    lex_next(lx);
    TokKind k = lex_peek(lx).kind;
    if (k == T_EQ) {
        lex_next(lx); skip_ws(lx);
        EXPR_t *r = parse_expr0(lx);
        return binop(E_ASGN, l, r);
    }
    if (k == T_QMARK) {
        lex_next(lx); skip_ws(lx);
        EXPR_t *r = parse_expr0(lx);
        return binop(E_NAM, l, r);
    }
    lex_restore(lx, m0);
    return l;
}

/* Public expression entry point */
static EXPR_t *parse_expr(Lex *lx) {
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
 * For $(expr) we parse the full expression and store it as E_INDR on a
 * special "$COMPUTED" label name — the emitter already handles this.
 * We return the label as a string for simple cases, or "$COMPUTED" for
 * computed cases (the STMT_t.go field carries the expr separately — but since
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
            /* $(expr) — computed goto: capture expression text for emitter */
            int depth=1;
            lex_next(lx); /* consume '(' */
            /* scan raw source to grab the expression text */
            int start = lx->pos;
            int end = lx->pos; /* will be updated to just before closing ) */
            while (!lex_at_end(lx) && depth > 0) {
                int before = lx->pos;
                Token tok = lex_next(lx);
                if (tok.kind==T_LPAREN) depth++;
                else if (tok.kind==T_RPAREN) {
                    depth--;
                    if (depth == 0) { end = before; break; } /* end = pos before ')' */
                }
            }
            /* Store as "$COMPUTED:expr_text" so emitter can emit dispatch */
            int elen = end - start;
            char *buf = malloc(12 + elen + 1);
            memcpy(buf, "$COMPUTED:", 10);
            memcpy(buf + 10, lx->src + start, elen);
            buf[10 + elen] = '\0';
            label = buf;
        } else if (lex_peek(lx).kind == T_STR) {
            /* $'literal' — e.g. :F($'pp_,1') — treat as computed goto
             * with a constant string expression.  Wrap as $COMPUTED:'literal'
             * so the emitter calls sno_computed_goto at runtime. */
            Token n2 = lex_next(lx);
            const char *lit = n2.sval ? n2.sval : "";
            /* Store as $COMPUTED:'lit' */
            char *buf = malloc(12 + strlen(lit) + 4);
            sprintf(buf, "$COMPUTED:'%s'", lit);
            label = buf;
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
 * Statement parser — one SnoLine → one STMT_t
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

static STMT_t *parse_body_field(const char *body, int lineno) {
    if (!body || !*body) return NULL;

    Lex lx_obj = {0}, *lx = &lx_obj;
    lex_open_str(lx, body, (int)strlen(body), lineno);
    skip_ws(lx);

    STMT_t *s = stmt_new();
    s->lineno = lineno;

    /* Subject — parsed at unary level (snoExpr14) */
    s->subject = parse_expr14(lx);

    /* After subject: WS then one of:
         '=' → assignment (no pattern)
         expr → pattern (requires WS between subject and pattern)
         nothing → invocation
    */
    /* After subject: WS or '?' introduce the pattern field.
     * '?' is the interrogation verb: S ? P means match P against S,
     * same semantics as S P but explicit — no replacement allowed.
     * Forms: "S ? P"  (no WS before ?)
     *        "S  ? P" (WS before ?, WS after ?)  ← beauty.sno style
     */
    int have_ws   = (lex_peek(lx).kind == T_WS);
    int have_qmark = 0;

    if (have_ws) {
        lex_next(lx); skip_ws(lx); /* consume WS */
        /* After WS: check if ? is the verb (e.g. "LINE  ?  PAT") */
        if (lex_peek(lx).kind == T_QMARK) {
            have_qmark = 1;
            lex_next(lx); skip_ws(lx); /* consume '?' and trailing WS */
        }
    } else if (lex_peek(lx).kind == T_QMARK) {
        /* Direct ? with no leading WS */
        have_qmark = 1;
        lex_next(lx); skip_ws(lx);
    }

    if (have_ws || have_qmark) {
        if (have_ws && !have_qmark && lex_peek(lx).kind == T_EQ) {
            /* subject = [replacement] — only valid after plain WS, not '?' */
            lex_next(lx); /* consume '=' */
            s->has_eq = 1;
            skip_ws(lx);
            if (!lex_at_end(lx))
                s->replacement = parse_expr(lx);
        } else if (!lex_at_end(lx)) {
            /* subject WS pattern [= replacement]  — or —  subject ? pattern
             * Use parse_expr3 (not parse_expr0) so trailing '=' is NOT
             * consumed as an assignment operator inside the pattern. */
            s->pattern = parse_expr3(lx);  /* includes |, excludes = and ? */

            if (lex_peek(lx).kind == T_WS) {
                /* Allow = replacement after both WS and ? separators */
                lex_next(lx); skip_ws(lx);
                if (lex_peek(lx).kind == T_EQ) {
                    lex_next(lx); skip_ws(lx);
                    s->has_eq = 1;
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
        STMT_t *s;

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

/* Public wrapper: parse a SNOBOL4 expression from a string.
 * Used by the emitter for computed gotos ($COMPUTED:expr_text). */
EXPR_t *parse_expr_from_str(const char *src) {
    if (!src || !*src) return NULL;
    Lex lx = {0};
    lex_open_str(&lx, src, (int)strlen(src), 0);
    /* Do NOT call lex_next() to prime — lex_peek() is self-priming.
     * Calling lex_next() here discards the first token. */
    return parse_expr0(&lx);
}
