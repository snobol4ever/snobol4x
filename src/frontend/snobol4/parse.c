/*
 * parse.c — hand-rolled SNOBOL4 recursive-descent parser for scrip-cc
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

#include "scrip_cc.h"
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

/* binop / unop — thin wrappers; all node construction goes through
 * expr_binary / expr_unary which call expr_add_child internally. */
static EXPR_t *binop(EKind k, EXPR_t *l, EXPR_t *r) {
    return expr_binary(k, l, r);
}
static EXPR_t *unop(EKind k, EXPR_t *operand) {
    return expr_unary(k, operand);
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

/* Parse comma-separated expression list; append each arg as a child of `node`. */
static void parse_arglist(Lex *lx, EXPR_t *node) {
    skip_ws(lx);
    if (lex_peek(lx).kind != T_RPAREN &&
        lex_peek(lx).kind != T_RBRACKET &&
        lex_peek(lx).kind != T_RANGLE &&
        lex_peek(lx).kind != T_EOF) {
        EXPR_t *e = parse_expr0(lx);
        expr_add_child(node, e ? e : expr_new(E_NUL));

        while (lex_peek(lx).kind == T_COMMA) {
            lex_next(lx); /* consume , */
            skip_ws(lx);
            TokKind k = lex_peek(lx).kind;
            if (k==T_RPAREN||k==T_RBRACKET||k==T_RANGLE||k==T_EOF) {
                expr_add_child(node, expr_new(E_NUL)); /* trailing comma */
                break;
            }
            EXPR_t *a = parse_expr0(lx);
            expr_add_child(node, a ? a : expr_new(E_NUL));
        }
    }
    skip_ws(lx);
}

static EXPR_t *parse_expr17(Lex *lx) {
    Token t = lex_peek(lx);

    /* Grouped expression or alternation-group: ( expr , ... ) */
    if (t.kind == T_LPAREN) {
        lex_next(lx); skip_ws(lx);
        EXPR_t *inner = parse_expr0(lx);
        skip_ws(lx);
        if (lex_peek(lx).kind == T_COMMA) {
            /* (expr, expr, ...) — alternation group: flat n-ary E_ALT */
            EXPR_t *alt = expr_new(E_ALT);
            expr_add_child(alt, inner);
            while (lex_peek(lx).kind == T_COMMA) {
                lex_next(lx); skip_ws(lx);
                EXPR_t *r = parse_expr0(lx);
                expr_add_child(alt, r ? r : expr_new(E_NUL));
                skip_ws(lx);
            }
            if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
            /* unwrap single-child E_ALT (degenerate case) */
            if (alt->nchildren == 1) { EXPR_t *tmp = alt->children[0]; free(alt->children); free(alt); return tmp; }
            return alt;
        }
        skip_ws(lx);
        if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
        return inner;
    }

    /* String literal */
    if (t.kind == T_STR) {
        lex_next(lx);
        EXPR_t *e = expr_new(E_QLIT); (e)->sval = (char *)(t.sval); return e;
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
        EXPR_t *e = expr_new(E_KW); (e)->sval = (char *)(t.sval); return e;
    }

    /* Identifier: bare name, or function call NAME(...) */
    if (t.kind == T_IDENT) {
        lex_next(lx);
        if (lex_peek(lx).kind == T_LPAREN) {
            lex_next(lx); /* consume '(' */
            EXPR_t *e = expr_new(E_FNC);
            e->sval = (char *)(t.sval);
            parse_arglist(lx, e);   /* args become children[0..n-1] */
            if (lex_peek(lx).kind==T_RPAREN) lex_next(lx);
            return e;
        }
        EXPR_t *e = expr_new(E_VAR); (e)->sval = (char *)(t.sval); return e;
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
        EXPR_t *tmp_node = expr_new(E_NUL); /* temp container for indices */
        parse_arglist(lx, tmp_node);
        if (lex_peek(lx).kind==close) lex_next(lx);

        EXPR_t *idx = expr_new(E_IDX);
        expr_add_child(idx, e);     /* children[0] = base expression */
        for (int _ii = 0; _ii < tmp_node->nchildren; _ii++)
            expr_add_child(idx, tmp_node->children[_ii]);
        if (tmp_node->children) free(tmp_node->children);
        free(tmp_node);
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
        case T_AT:     uk=E_CAPT_CUR;    break;
        case T_TILDE:  uk=E_INDR; break;  /* ~ is NOT in scrip-cc.h so map to DEREF for now */
        case T_QMARK:  uk=E_CAPT_COND;  break;  /* unary ? = interrogation */
        case T_AMP:    uk=E_OPSYN;break;
        case T_PLUS:   lex_next(lx); return parse_expr14(lx); /* unary + is identity */
        case T_MINUS:  uk=E_NEG;   break;
        case T_STAR:   uk=E_DEFER; break;   /* *X = deferred pattern ref */
        case T_STAR:   uk=E_DEFER; break;  /* *X = deferred pattern ref */
        case T_DOLLAR: uk=E_INDR; break;  /* $X = indirect reference */
        case T_DOT:    uk=E_CAPT_COND;  break;  /* .X = name */
        case T_BANG:   uk=E_POW;   break;  /* !X = definable unary */
        case T_PCT:    uk=E_DIV;   break;  /* %X = definable unary */
        case T_SLASH:  uk=E_DIV;   break;  /* /X = definable unary */
        case T_HASH:   uk=E_MPY;   break;  /* #X = definable unary */
        case T_EQ:     uk=E_ASSIGN;break;  /* =X = definable unary */
        case T_PIPE:   uk=E_ALT;   break;  /* |X = definable unary */
        default:       return parse_expr15(lx);
    }
    TokKind op_tok = t.kind;
    lex_next(lx);
    EXPR_t *operand = parse_expr14(lx);
    if (!operand) {
        snoc_error(lx->lineno, "expected operand after unary operator");
        return expr_new(E_NUL);
    }
    /* emit.c contract for E_INDR:
     *   *X  (deferred ref):  e->left = operand, e->right = NULL
     *   $X  (indirect):      e->left = NULL,    e->right = operand
     * All other unary ops use e->left (via unop). */
    /* All unary operators (including $X and *X) use children[0] = operand.
     * The old left/right distinction is gone — backends use expr_left(). */
    return unop(uk, operand);
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
        /* ~ 'tag': emit E_CAPT_COND(child=l, tag=r) so emit_byrd generates Shift(tag, matched) */
        l = binop(E_CAPT_COND, l, r);
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
            EKind k = (op==T_DOLLAR) ? E_CAPT_IMM : E_CAPT_COND;
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
    static const EKind   kinds[] = { E_POW,   E_POW,  E_POW      };
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
    static const EKind   kinds[] = { E_CAPT_CUR };
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

    /* Build flat n-ary E_SEQ node (fixup_val_tree reclassifies to E_CONCAT if value context) */
    EXPR_t *e = expr_new(E_SEQ);
    for (int i = 0; i < n; i++) expr_add_child(e, items[i]);
    free(items);
    return e;
}

/* ── expr3 — | (alternation, n-ary) ─────────────────────────────────────── */
/* snoX3 = nInc() snoExpr4 FENCE($'|' snoX3 | ε) */
static EXPR_t *parse_expr3(Lex *lx) {
    EXPR_t *first = parse_expr4(lx);
    if (!first) return NULL;
    /* peek ahead — if no | follows, return the single child as-is */
    LexMark m3check = lex_mark(lx);
    int has_pipe = 0;
    if (lex_peek(lx).kind == T_WS) {
        lex_next(lx);
        if (lex_peek(lx).kind == T_PIPE) has_pipe = 1;
        lex_restore(lx, m3check);
    }
    if (!has_pipe) return first;

    /* Build flat n-ary E_ALT */
    EXPR_t *e = expr_new(E_ALT);
    expr_add_child(e, first);
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
        expr_add_child(e, r ? r : expr_new(E_NUL));
    }
    return e;
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
        return binop(E_ASSIGN, l, r);
    }
    if (k == T_QMARK) {
        lex_next(lx); skip_ws(lx);
        EXPR_t *r = parse_expr0(lx);
        return binop(E_CAPT_COND, l, r);
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

/* ── M-G4-SPLIT-SEQ-CONCAT: post-parse E_SEQ context fixup ──────────────────
 * parse_expr4 emits E_SEQ for all juxtaposition-concat sites.
 * After parsing, we know context for s->subject (always value).
 * s->replacement is ambiguous: it may be a pure value expression OR a pattern
 * expression (e.g. PAT = " the " ARB . OUTPUT ("of"|"a")).
 *
 * fixup_val_tree : walk a value-context tree — rename E_SEQ
 *                  to E_CONCAT (pure string concat, cannot fail).
 *
 * repl_is_pat_tree: lightweight check — true if tree contains any node that is
 *   unambiguously pattern-only: E_ARB, E_ARBNO, E_CAPT_COND, E_CAPT_IMM, E_CAPT_CUR, E_DEFER.
 *   (E_FNC pattern-function detection is left to the emitter's expr_is_pattern_expr.)
 *   If true, do NOT apply fixup_val_tree to s->replacement — leave as E_SEQ.
 */
static void fixup_val_tree(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_SEQ) e->kind = E_CONCAT;   /* value concat, cannot fail */
    for (int i = 0; i < e->nchildren; i++) fixup_val_tree(e->children[i]);
}

static int repl_is_pat_tree(EXPR_t *e) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ARB: case E_ARBNO:
        case E_CAPT_COND: case E_CAPT_IMM: case E_CAPT_CUR: case E_DEFER:
            return 1;
        default: break;
    }
    for (int i = 0; i < e->nchildren; i++)
        if (repl_is_pat_tree(e->children[i])) return 1;
    return 0;
}

Program *parse_program(LineArray *lines) {
    Program *prog = calloc(1, sizeof *prog);

    for (int i=0; i<lines->n; i++) {
        SnoLine *sl = &lines->a[i];
        STMT_t *s;

        /* ---- EXPORT / IMPORT control lines (LP-4) ----
         * EXPORT appears as label="EXPORT", body=<name>
         * IMPORT appears as label="IMPORT", body=<lang>.<name>
         * These are not SNOBOL4 statements — consume and skip. */
        if (sl->label && strcasecmp(sl->label, "EXPORT") == 0) {
            if (sl->body && sl->body[0]) {
                ExportEntry *e = calloc(1, sizeof *e);
                e->name = strdup(sl->body);
                /* uppercase the name to match SNOBOL4 convention */
                for (char *p = e->name; *p; p++)
                    *p = (char)toupper((unsigned char)*p);
                e->next = prog->exports;
                prog->exports = e;
            }
            continue;
        }
        if (sl->label && strcasecmp(sl->label, "IMPORT") == 0) {
            if (sl->body && sl->body[0]) {
                /* body formats:
                 *   assembly.METHOD           — two-part (preferred, no lang prefix)
                 *   lang.assembly.METHOD      — three-part (legacy, lang ignored)
                 * Assembly name preserves case (CLR is case-sensitive).
                 * METHOD is uppercased to match EXPORT convention.
                 */
                char *dot1 = strchr(sl->body, '.');
                ImportEntry *e = calloc(1, sizeof *e);
                if (dot1) {
                    char *dot2 = strchr(dot1 + 1, '.');
                    if (dot2) {
                        /* three-part: lang . assembly . method — lang ignored */
                        e->lang   = strndup(sl->body, (size_t)(dot1 - sl->body));
                        e->name   = strndup(dot1 + 1, (size_t)(dot2 - dot1 - 1));
                        e->method = strdup(dot2 + 1);
                    } else {
                        /* two-part: assembly . method */
                        e->lang   = strdup("");
                        e->name   = strndup(sl->body, (size_t)(dot1 - sl->body));
                        e->method = strdup(dot1 + 1);
                    }
                    /* method name preserved verbatim — caller must match case */
                } else {
                    /* bare name: use as both assembly and method */
                    e->lang   = strdup("");
                    e->name   = strdup(sl->body);
                    e->method = strdup(sl->body);
                }
                e->next = prog->imports;
                prog->imports = e;
            }
            continue;
        }
        /* ---- end EXPORT/IMPORT ---- */

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
            /* M-G4-SPLIT-SEQ-CONCAT: fix value-context E_SEQ→E_CONCAT.
             * s->subject is always value context.
             * s->replacement is value context UNLESS it is a pattern expression
             * (e.g. PAT = " the " ARB . OUTPUT ...) — guard with repl_is_pat_tree.
             * s->pattern is pattern context — E_SEQ already correct, no action. */
            fixup_val_tree(s->subject);
            if (s->replacement && !repl_is_pat_tree(s->replacement))
                fixup_val_tree(s->replacement);
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
