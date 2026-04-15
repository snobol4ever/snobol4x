/*
 * snocone_parse.c -- Snocone expression parser  (Sprint SC1)
 *
 * Port of snobol4dotnet/Snobol4.Common/Builder/SnoconeParser.cs.
 * The Clojure snocone_grammar.clj is the spec; C# was the structural model.
 *
 * Algorithm: shunting-yard producing postfix (RPN) ScPToken array.
 * Extensions beyond classic shunting-yard:
 *   - Unary prefix operators: ANY("+-*&@~?.$")
 *   - Function calls:  f(args...) -> f args... SNOCONE_CALL(n)
 *   - Array refs:      a[i]       -> a i...   SNOCONE_ARRAY_REF(n)
 *   - Leading-dot float fixup: .5 -> text rewritten to "0.5"
 *
 * Precedence (lp / rp from bconv in snocone.sc):
 *   SNOCONE_ASSIGN    1/2   right-assoc    SNOCONE_QUESTION  2/2
 *   SNOCONE_PIPE      3/3                  SNOCONE_OR        4/4
 *   SNOCONE_CONCAT    5/5                  comparisons  6/6
 *   SNOCONE_PLUS/-    7/7                  SNOCONE_SLASH/STAR/PERCENT  8/8
 *   SNOCONE_CARET     9/10  right-assoc    SNOCONE_PERIOD/$  10/10
 *
 * Reduce condition (from binop() in snocone.sc):
 *   while existing_op.lp >= incoming_op.rp  -> reduce
 */

#include "snocone_parse.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Precedence table
 * ------------------------------------------------------------------------- */
typedef struct { SnoconeKind kind; int lp; int rp; } PrecEntry;

static const PrecEntry PREC_TABLE[] = {
    { SNOCONE_ASSIGN,          1,  2 },
    { SNOCONE_PLUS_ASSIGN,     1,  2 },
    { SNOCONE_MINUS_ASSIGN,    1,  2 },
    { SNOCONE_STAR_ASSIGN,     1,  2 },
    { SNOCONE_SLASH_ASSIGN,    1,  2 },
    { SNOCONE_PERCENT_ASSIGN,  1,  2 },
    { SNOCONE_CARET_ASSIGN,    1,  2 },
    { SNOCONE_QUESTION,    2,  2 },
    { SNOCONE_PIPE,        3,  3 },
    { SNOCONE_OR,          4,  4 },
    { SNOCONE_CONCAT,      5,  5 },
    { SNOCONE_EQ,          6,  6 },
    { SNOCONE_NE,          6,  6 },
    { SNOCONE_LT,          6,  6 },
    { SNOCONE_GT,          6,  6 },
    { SNOCONE_LE,          6,  6 },
    { SNOCONE_GE,          6,  6 },
    { SNOCONE_STR_IDENT,   6,  6 },
    { SNOCONE_STR_DIFFER,  6,  6 },
    { SNOCONE_STR_LT,      6,  6 },
    { SNOCONE_STR_GT,      6,  6 },
    { SNOCONE_STR_LE,      6,  6 },
    { SNOCONE_STR_GE,      6,  6 },
    { SNOCONE_STR_EQ,      6,  6 },
    { SNOCONE_STR_NE,      6,  6 },
    { SNOCONE_PLUS,        7,  7 },
    { SNOCONE_MINUS,       7,  7 },
    { SNOCONE_SLASH,       8,  8 },
    { SNOCONE_STAR,        8,  8 },
    { SNOCONE_PERCENT,     8,  8 },
    { SNOCONE_CARET,       9, 10 },
    { SNOCONE_PERIOD,     10, 10 },
    { SNOCONE_DOLLAR,     10, 10 },
};
static const int PREC_COUNT = (int)(sizeof(PREC_TABLE)/sizeof(PREC_TABLE[0]));

/* Unary-capable operators: ANY("+-*&@~?.$") */
static const SnoconeKind UNARY_OPS[] = {
    SNOCONE_PLUS, SNOCONE_MINUS, SNOCONE_STAR, SNOCONE_AMPERSAND,
    SNOCONE_AT, SNOCONE_TILDE, SNOCONE_QUESTION, SNOCONE_PERIOD, SNOCONE_DOLLAR,
};
static const int UNARY_COUNT = (int)(sizeof(UNARY_OPS)/sizeof(UNARY_OPS[0]));

/* ---------------------------------------------------------------------------
 * Internal dynamic arrays (output list and operator stack)
 * ------------------------------------------------------------------------- */
typedef struct {
    ScPToken *data;
    int       count;
    int       cap;
} PTokenVec;

static void vec_init(PTokenVec *v) {
    v->data  = NULL;
    v->count = 0;
    v->cap   = 0;
}

static void vec_push(PTokenVec *v, ScPToken t) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->data = realloc(v->data, (size_t)v->cap * sizeof(ScPToken));
    }
    v->data[v->count++] = t;
}

static ScPToken vec_pop(PTokenVec *v) {
    return v->data[--v->count];
}

static ScPToken vec_peek(const PTokenVec *v) {
    return v->data[v->count - 1];
}

/* ---------------------------------------------------------------------------
 * Call-frame stack (tracks open parens/brackets for call/array/group)
 * ------------------------------------------------------------------------- */
typedef enum { FRAME_GROUP, FRAME_CALL, FRAME_ARRAY } FrameKind;

typedef struct {
    FrameKind kind;
    int       arg_count;    /* commas seen so far                   */
    int       output_start; /* output.count when frame was opened   */
    int       line;
} Frame;

typedef struct {
    Frame *data;
    int    count;
    int    cap;
} FrameVec;

static void fvec_init(FrameVec *v) { v->data=NULL; v->count=0; v->cap=0; }

static void fvec_push(FrameVec *v, Frame f) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = realloc(v->data, (size_t)v->cap * sizeof(Frame));
    }
    v->data[v->count++] = f;
}

static Frame fvec_pop(FrameVec *v)  { return v->data[--v->count]; }
static Frame *fvec_top(FrameVec *v) { return v->count ? &v->data[v->count-1] : NULL; }

/* ---------------------------------------------------------------------------
 * Lookup helpers
 * ------------------------------------------------------------------------- */
static int prec_of(SnoconeKind k, int *lp, int *rp) {
    for (int i = 0; i < PREC_COUNT; i++) {
        if (PREC_TABLE[i].kind == k) {
            *lp = PREC_TABLE[i].lp;
            *rp = PREC_TABLE[i].rp;
            return 1;
        }
    }
    return 0;
}

static int is_binary_op(SnoconeKind k) {
    int lp, rp;
    return prec_of(k, &lp, &rp);
}

static int is_unary_op(SnoconeKind k) {
    for (int i = 0; i < UNARY_COUNT; i++)
        if (UNARY_OPS[i] == k) return 1;
    return 0;
}

static int is_operand(SnoconeKind k) {
    return k == SNOCONE_IDENT || k == SNOCONE_INTEGER || k == SNOCONE_REAL || k == SNOCONE_STRING;
}

/* Build a ScPToken from a lexer SnoconeToken — strdup text so caller owns it */
static ScPToken make_pt(const SnoconeToken *t) {
    ScPToken p;
    p.kind      = t->kind;
    p.text      = t->text ? strdup(t->text) : NULL;
    p.line      = t->line;
    p.is_unary  = 0;
    p.arg_count = 0;
    return p;
}

/* Build a synthetic ScPToken (SNOCONE_CALL / SNOCONE_ARRAY_REF) */
static ScPToken make_synthetic(SnoconeKind kind, int arg_count, int line) {
    ScPToken p;
    p.kind      = kind;
    p.text      = strdup((kind == SNOCONE_CALL) ? "()" : "[]");
    p.line      = line;
    p.is_unary  = 0;
    p.arg_count = arg_count;
    return p;
}

/* Is token at position i in unary position?
 * True when: start-of-expression, or previous token was an operator,
 * left-paren/bracket, or comma.  Mirrors C# IsUnaryPosition(). */
static int is_unary_position(const SnoconeToken *toks, int i) {
    if (i == 0) return 1;
    SnoconeKind prev = toks[i-1].kind;
    return prev == SNOCONE_LPAREN   ||
           prev == SNOCONE_LBRACKET ||
           prev == SNOCONE_COMMA    ||
           is_binary_op(prev)  ||
           is_unary_op(prev);
}

/* Drain operator stack to output until a left-delimiter is hit.
 * The delimiter itself is discarded (its text freed). */
static int drain_to_delim(PTokenVec *out, PTokenVec *ops, SnoconeKind delim) {
    while (ops->count > 0 && vec_peek(ops).kind != delim)
        vec_push(out, vec_pop(ops));
    if (ops->count == 0) return 0;
    ScPToken delim_tok = vec_pop(ops);   /* discard delimiter */
    free(delim_tok.text);
    return 1;
}

/* Push one binary operator respecting shunting-yard reduce condition:
 *   while existing_op.lp >= incoming_op.rp  -> reduce */
static void push_binop(PTokenVec *out, PTokenVec *ops, ScPToken tok) {
    int in_lp, in_rp;
    prec_of(tok.kind, &in_lp, &in_rp);
    while (ops->count > 0) {
        ScPToken top = vec_peek(ops);
        int top_lp, top_rp;
        if (!prec_of(top.kind, &top_lp, &top_rp)) break;
        if (top_lp >= in_rp)
            vec_push(out, vec_pop(ops));
        else
            break;
    }
    vec_push(ops, tok);
}

/* ---------------------------------------------------------------------------
 * Forward declaration for mutual recursion in unary handling
 * ------------------------------------------------------------------------- */
static int parse_operand_into(const SnoconeToken *toks, int count, int i,
                               PTokenVec *out, PTokenVec *ops, FrameVec *calls);

/* ---------------------------------------------------------------------------
 * snocone_parse() -- public entry point
 *
 * toks[]  : token array from snocone_lex (SNOCONE_NEWLINE and SNOCONE_EOF already excluded
 *           by the caller, or included — we stop at SNOCONE_EOF/SNOCONE_NEWLINE)
 * count   : number of tokens to consume
 * ------------------------------------------------------------------------- */
ScParseResult snocone_parse(const SnoconeToken *toks, int count) {
    PTokenVec out;   vec_init(&out);
    PTokenVec ops;   vec_init(&ops);
    FrameVec  calls; fvec_init(&calls);

    int i = 0;
    while (i < count) {
        const SnoconeToken *tok = &toks[i];

        /* Skip statement terminators */
        if (tok->kind == SNOCONE_NEWLINE || tok->kind == SNOCONE_EOF) { i++; continue; }

        /* dotck: leading-dot real -> "0.N" text rewrite */
        if (tok->kind == SNOCONE_REAL && tok->text && tok->text[0] == '.') {
            size_t len = strlen(tok->text);
            char *fixed = malloc(len + 2);
            fixed[0] = '0';
            memcpy(fixed + 1, tok->text, len + 1);
            ScPToken pt = make_pt(tok);
            free(pt.text);   /* replace the strdup'd copy with the fixed one */
            pt.text = fixed;
            vec_push(&out, pt);
            i++;
            continue;
        }

        /* ---- Operand ---- */
        if (is_operand(tok->kind)) {
            SnoconeKind next = (i+1 < count) ? toks[i+1].kind : SNOCONE_EOF;

            /* IDENT immediately followed by '(' -> function call */
            if (tok->kind == SNOCONE_IDENT && next == SNOCONE_LPAREN) {
                vec_push(&out, make_pt(tok));
                Frame f = { FRAME_CALL, 0, out.count, tok->line };
                fvec_push(&calls, f);
                /* push the '(' onto op stack as delimiter */
                vec_push(&ops, make_pt(&toks[i+1]));
                i += 2;
                continue;
            }

            /* IDENT immediately followed by '[' -> array ref */
            if (tok->kind == SNOCONE_IDENT && next == SNOCONE_LBRACKET) {
                vec_push(&out, make_pt(tok));
                Frame f = { FRAME_ARRAY, 0, out.count, tok->line };
                fvec_push(&calls, f);
                vec_push(&ops, make_pt(&toks[i+1]));
                i += 2;
                continue;
            }

            /* IDENT immediately followed by '<' -> angle-bracket array ref (SNOBOL4 style) */
            if (tok->kind == SNOCONE_IDENT && next == SNOCONE_LT) {
                vec_push(&out, make_pt(tok));
                Frame f = { FRAME_ARRAY, 0, out.count, tok->line };
                fvec_push(&calls, f);
                vec_push(&ops, make_pt(&toks[i+1]));
                i += 2;
                continue;
            }

            vec_push(&out, make_pt(tok));
            i++;
            continue;
        }

        /* ---- Left paren (grouping) ---- */
        if (tok->kind == SNOCONE_LPAREN) {
            Frame f = { FRAME_GROUP, 0, out.count, tok->line };
            fvec_push(&calls, f);
            vec_push(&ops, make_pt(tok));
            i++;
            continue;
        }

        /* ---- Right paren ---- */
        if (tok->kind == SNOCONE_RPAREN) {
            drain_to_delim(&out, &ops, SNOCONE_LPAREN);
            Frame *fp = fvec_top(&calls);
            if (fp && (fp->kind == FRAME_CALL || fp->kind == FRAME_ARRAY)) {
                Frame f = fvec_pop(&calls);
                if (f.kind == FRAME_CALL) {
                    int has_args = out.count > f.output_start;
                    int n = has_args ? f.arg_count + 1 : 0;
                    vec_push(&out, make_synthetic(SNOCONE_CALL, n, tok->line));
                }
                /* FRAME_ARRAY closed by ']', not ')'; ignore here */
            } else if (fp && fp->kind == FRAME_GROUP) {
                fvec_pop(&calls);
            }
            i++;
            continue;
        }

        /* ---- Left bracket ---- */
        if (tok->kind == SNOCONE_LBRACKET) {
            /* Always an array-subscript: either ident[i] (IDENT case above already
             * pushed FRAME_ARRAY) or expr[i] (call result, parenthesised expr, etc.).
             * Push FRAME_ARRAY so ']' emits SNOCONE_ARRAY_REF. */
            Frame f = { FRAME_ARRAY, 0, out.count, tok->line };
            fvec_push(&calls, f);
            vec_push(&ops, make_pt(tok));
            i++;
            continue;
        }

        /* ---- Right bracket ---- */
        if (tok->kind == SNOCONE_RBRACKET) {
            drain_to_delim(&out, &ops, SNOCONE_LBRACKET);
            Frame *fp = fvec_top(&calls);
            if (fp) {
                Frame f = fvec_pop(&calls);
                if (f.kind == FRAME_ARRAY) {
                    int has_args = out.count > f.output_start;
                    int n = has_args ? f.arg_count + 1 : 0;
                    vec_push(&out, make_synthetic(SNOCONE_ARRAY_REF, n, tok->line));
                }
            }
            i++;
            continue;
        }

        /* ---- Right angle '>' — closes angle-bracket array ref A<i> ---- */
        if (tok->kind == SNOCONE_GT) {
            Frame *fp = fvec_top(&calls);
            if (fp && fp->kind == FRAME_ARRAY) {
                /* closing an angle-bracket array ref opened by IDENT + '<' */
                drain_to_delim(&out, &ops, SNOCONE_LT);
                Frame f = fvec_pop(&calls);
                int has_args = out.count > f.output_start;
                int n = has_args ? f.arg_count + 1 : 0;
                vec_push(&out, make_synthetic(SNOCONE_ARRAY_REF, n, tok->line));
            } else {
                /* not closing an array frame — treat as binary GT operator */
                push_binop(&out, &ops, make_pt(tok));
            }
            i++;
            continue;
        }

        /* ---- Comma ---- */
        if (tok->kind == SNOCONE_COMMA) {
            /* drain to nearest open delimiter */
            while (ops.count > 0 &&
                   vec_peek(&ops).kind != SNOCONE_LPAREN &&
                   vec_peek(&ops).kind != SNOCONE_LBRACKET &&
                   vec_peek(&ops).kind != SNOCONE_LT)
                vec_push(&out, vec_pop(&ops));
            /* increment arg count in topmost call/array frame */
            Frame *fp = fvec_top(&calls);
            if (fp) fp->arg_count++;
            i++;
            continue;
        }

        /* ---- Unary operator (in unary position) ---- */
        if (is_unary_op(tok->kind) && is_unary_position(toks, i)) {
            /* Consume one complete operand, then emit the unary op */
            i++;
            i = parse_operand_into(toks, count, i, &out, &ops, &calls);
            ScPToken upt = make_pt(tok);
            upt.is_unary = 1;
            vec_push(&out, upt);
            continue;
        }

        /* ---- Binary operator ---- */
        if (is_binary_op(tok->kind)) {
            push_binop(&out, &ops, make_pt(tok));
            i++;
            continue;
        }

        /* Unknown token — skip */
        i++;
    }

    /* endexp: drain remaining operators */
    while (ops.count > 0 && vec_peek(&ops).kind != SNOCONE_LPAREN)
        vec_push(&out, vec_pop(&ops));
    /* Free any residual delimiter tokens left on op stack (mismatched parens) */
    while (ops.count > 0) { ScPToken t = vec_pop(&ops); free(t.text); }

    free(ops.data);
    free(calls.data);

    ScParseResult r;
    r.tokens = out.data;
    r.count  = out.count;
    return r;
}

/* ---------------------------------------------------------------------------
 * parse_operand_into -- consume one complete operand starting at toks[i].
 * Used by the unary-operator path.  Returns index after consumed tokens.
 * Mirrors C# ParseOperandInto().
 * ------------------------------------------------------------------------- */
static int parse_operand_into(const SnoconeToken *toks, int count, int i,
                               PTokenVec *out, PTokenVec *ops, FrameVec *calls) {
    if (i >= count) return i;
    const SnoconeToken *tok = &toks[i];

    /* dotck fixup */
    if (tok->kind == SNOCONE_REAL && tok->text && tok->text[0] == '.') {
        size_t len = strlen(tok->text);
        char *fixed = malloc(len + 2);
        fixed[0] = '0';
        memcpy(fixed + 1, tok->text, len + 1);
        ScPToken pt = make_pt(tok);
        free(pt.text);
        pt.text = fixed;
        vec_push(out, pt);
        return i + 1;
    }

    if (is_operand(tok->kind)) {
        /* IDENT immediately followed by '(' -> function call.
         * We must fully consume f(args...) here — including all arguments
         * and the closing ')' — so that when the caller appends the unary op
         * it lands AFTER the SNOCONE_CALL token, not inside the argument list.
         * Strategy: push the frame/delimiter exactly as the main loop would,
         * then run a mini-loop over the interior tokens until the matching ')'
         * is consumed and the frame is closed. */
        if (tok->kind == SNOCONE_IDENT && i + 1 < count &&
            toks[i+1].kind == SNOCONE_LPAREN) {
            vec_push(out, make_pt(tok));
            Frame f = { FRAME_CALL, 0, out->count, tok->line };
            fvec_push(calls, f);
            vec_push(ops, make_pt(&toks[i+1]));
            int j = i + 2;
            int depth = 1; /* track nested parens so we stop at the right ')' */
            while (j < count && depth > 0) {
                SnoconeKind jk = toks[j].kind;
                if (jk == SNOCONE_LPAREN || jk == SNOCONE_LBRACKET) depth++;
                if (jk == SNOCONE_RPAREN || jk == SNOCONE_RBRACKET) depth--;
                if (depth == 0) break; /* this is our closing ')' */
                j++;
            }
            /* j now points at the matching ')'; run the main shunting-yard
             * loop over toks[i+2 .. j] (inclusive) reusing snocone_parse logic.
             * Simplest correct approach: recurse into snocone_parse for the
             * interior, then manually emit SNOCONE_CALL. */

            /* Interior token range: toks[i+2 .. j-1] */
            int interior_start = i + 2;
            int interior_end   = j;          /* exclusive */
            int interior_count = interior_end - interior_start;

            /* Parse the interior as a comma-separated expression list.
             * We need to handle commas as arg separators ourselves.
             * Run the shunting-yard directly over the interior tokens,
             * counting commas at depth 0 for arg_count. */
            int arg_count = 0;
            int has_args  = 0;
            if (interior_count > 0) {
                /* Parse each comma-separated argument via snocone_parse */
                int start = interior_start;
                int inner_depth = 0;
                for (int k = interior_start; k <= interior_end; k++) {
                    int at_end = (k == interior_end);
                    SnoconeKind kk  = at_end ? SNOCONE_EOF : toks[k].kind;
                    if (!at_end) {
                        if (kk == SNOCONE_LPAREN || kk == SNOCONE_LBRACKET) inner_depth++;
                        if (kk == SNOCONE_RPAREN || kk == SNOCONE_RBRACKET) inner_depth--;
                    }
                    int is_sep = at_end || (kk == SNOCONE_COMMA && inner_depth == 0);
                    if (is_sep) {
                        int seg_len = k - start;
                        if (seg_len > 0) {
                            ScParseResult seg = snocone_parse(&toks[start], seg_len);
                            for (int s = 0; s < seg.count; s++)
                                vec_push(out, seg.tokens[s]);
                            /* seg.tokens entries are strdup'd; push transfers
                             * ownership to out, so free only the array */
                            free(seg.tokens);
                            has_args = 1;
                            if (!at_end) arg_count++;
                        }
                        start = k + 1;
                    }
                }
                if (has_args) arg_count++;
            }
            /* Drain the '(' delimiter from ops */
            while (ops->count > 0 && vec_peek(ops).kind != SNOCONE_LPAREN)
                vec_push(out, vec_pop(ops));
            if (ops->count > 0) { ScPToken d = vec_pop(ops); free(d.text); }
            /* Pop the FRAME_CALL we pushed */
            if (calls->count > 0) fvec_pop(calls);
            /* Emit the SNOCONE_CALL synthetic token */
            vec_push(out, make_synthetic(SNOCONE_CALL, arg_count, tok->line));
            return j + 1; /* skip past the closing ')' */
        }
        /* IDENT immediately followed by '[' -> array ref */
        if (tok->kind == SNOCONE_IDENT && i + 1 < count &&
            toks[i+1].kind == SNOCONE_LBRACKET) {
            vec_push(out, make_pt(tok));
            Frame f = { FRAME_ARRAY, 0, out->count, tok->line };
            fvec_push(calls, f);
            vec_push(ops, make_pt(&toks[i+1]));
            return i + 2;
        }
        /* IDENT immediately followed by '<' -> angle-bracket array ref */
        if (tok->kind == SNOCONE_IDENT && i + 1 < count &&
            toks[i+1].kind == SNOCONE_LT) {
            vec_push(out, make_pt(tok));
            Frame f = { FRAME_ARRAY, 0, out->count, tok->line };
            fvec_push(calls, f);
            vec_push(ops, make_pt(&toks[i+1]));
            return i + 2;
        }
        vec_push(out, make_pt(tok));
        return i + 1;
    }

    /* Nested unary */
    if (is_unary_op(tok->kind)) {
        int next = parse_operand_into(toks, count, i + 1, out, ops, calls);
        ScPToken upt = make_pt(tok);
        upt.is_unary = 1;
        vec_push(out, upt);
        return next;
    }

    /* Grouping paren — fully parse the balanced interior so that unary ops
     * like ~(v ? pat) get a complete operand before the unary token is emitted */
    if (tok->kind == SNOCONE_LPAREN) {
        /* find matching close paren */
        int j = i + 1, depth = 1;
        while (j < count && depth > 0) {
            if (toks[j].kind == SNOCONE_LPAREN) depth++;
            if (toks[j].kind == SNOCONE_RPAREN) depth--;
            j++;
        }
        /* toks[i+1 .. j-2] is the interior (j-1 is the closing ')') */
        int interior_start = i + 1;
        int interior_len   = j - 1 - interior_start; /* excludes close paren */
        if (interior_len > 0) {
            ScParseResult inner = snocone_parse(&toks[interior_start], interior_len);
            for (int s = 0; s < inner.count; s++)
                vec_push(out, inner.tokens[s]);
            free(inner.tokens);
        }
        return j; /* skip past closing ')' */
    }

    return i + 1; /* skip unknown */
}

/* ---------------------------------------------------------------------------
 * sc_parse_free — all text is strdup'd so always free it
 * ------------------------------------------------------------------------- */
void sc_parse_free(ScParseResult *r) {
    if (!r) return;
    for (int i = 0; i < r->count; i++)
        free(r->tokens[i].text);
    free(r->tokens);
    r->tokens = NULL;
    r->count  = 0;
}

/* ---------------------------------------------------------------------------
 * sc_ptoken_kind_name
 * ------------------------------------------------------------------------- */
const char *sc_ptoken_kind_name(SnoconeKind kind) {
    if (kind == SNOCONE_CALL)       return "SNOCONE_CALL";
    if (kind == SNOCONE_ARRAY_REF)  return "SNOCONE_ARRAY_REF";
    return sc_kind_name(kind);
}
