/*
 * emit_x64_snocone.c — Snocone → STMT_t/EXPR_t IR lowering  (SC-1)
 *
 * Snocone-specific lowering only.  All NASM emission is handled by the
 * shared emit_x64.c, exactly as emit_x64_icon.c and emit_x64_prolog.c do.
 *
 * Pipeline:
 *   source text
 *     → snocone_lex()      (snocone_lex.c)
 *     → snocone_parse()    (snocone_parse.c)
 *     → [this file] expression lowering (RPN → EXPR_t)
 *     → [this file] CF lowering (token stream → STMT_t list with labels/gotos)
 *     → emit_x64.c         (shared NASM emission — untouched)
 *
 * Replaces: snocone_lower.c + snocone_cf.c (both deleted as part of SC-1).
 *
 * Public entry point:
 *   Program *emit_x64_snocone_compile(const char *source, const char *filename);
 *
 * Operator table (from bconv[] in snocone.sc / snocone.snobol4):
 *   &&  → E_CONCAT (blank concat)
 *   ||  → E_CONCAT (pattern alternation — value context; same IR node)
 *   |   → E_CONCAT
 *   ==  → EQ(a,b)    !=  → NE(a,b)
 *   <   → LT(a,b)    >   → GT(a,b)
 *   <=  → LE(a,b)    >=  → GE(a,b)
 *   ::  → IDENT(a,b) :!: → DIFFER(a,b)
 *   :==: → LEQ      :!=: → LNE
 *   :>:  → LGT      :<:  → LLT
 *   :>=: → LGE      :<=: → LLE
 *   %   → REMDR(a,b)
 *   ^   → E_POW (right-assoc)
 *   .   → E_CAPT_COND    $  → E_CAPT_IMM
 *   unary *  → E_INDR
 *   unary ~  → NOT(x)
 *   unary ?  → DIFFER(x)
 *   &name    → E_KW
 *   @var     → E_CAPT_CUR
 *
 * Control-flow lowering (for loop separator note):
 *   Our for uses ; separators: for (init; cond; step)
 *   The original Koenig spec uses , — our implementation uses ; (C-style).
 *   Corpus tests must match this.
 *
 * Extensions over original Koenig Snocone (SC-1):
 *   goto  — C-style one-word form only (go to two-word removed SC-1)
 *   break — exit innermost loop (while/do-while/for)
 *   continue — next iteration of innermost loop
 *   Loop label stack: brk/cont pushed on entry, popped on exit.
 *   For loop: lab_step inserted before step so continue lands correctly.
 *   No new IR node types — break/continue lower to plain emit_goto().
 */

#define _POSIX_C_SOURCE 200809L
#include "emit_x64_snocone.h"
#include "snocone_lex.h"
#include "snocone_parse.h"
#include "scrip_cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Expression lowering  (formerly snocone_lower.c)
 * ======================================================================= */

#define EXPR_STACK_MAX 1024

typedef struct {
    EXPR_t *v[EXPR_STACK_MAX];
    int     top;
} ExprStack;

static void es_push(ExprStack *s, EXPR_t *e) {
    if (s->top >= EXPR_STACK_MAX) {
        fprintf(stderr, "emit_x64_snocone: expression stack overflow\n"); exit(1);
    }
    s->v[s->top++] = e;
}
static EXPR_t *es_pop(ExprStack *s) {
    if (s->top <= 0) {
        fprintf(stderr, "emit_x64_snocone: expression stack underflow\n");
        return expr_new(E_NUL);
    }
    return s->v[--s->top];
}
static EXPR_t *es_peek(ExprStack *s) {
    return s->top > 0 ? s->v[s->top - 1] : NULL;
}

static EXPR_t *make_fnc2(const char *name, EXPR_t *l, EXPR_t *r) {
    EXPR_t *e = expr_new(E_FNC);
    e->sval   = strdup(name);
    expr_add_child(e, l);
    expr_add_child(e, r);
    return e;
}
static EXPR_t *make_fnc1(const char *name, EXPR_t *arg) {
    EXPR_t *e = expr_new(E_FNC);
    e->sval   = strdup(name);
    expr_add_child(e, arg);
    return e;
}

static int lower_token(const ScPToken *tok, ExprStack *s,
                       const char *filename, int *nerrors)
{
    (void)filename;
    switch ((int)tok->kind) {

    /* ---- Literals & identifiers ---- */
    case SNOCONE_INTEGER: {
        EXPR_t *e = expr_new(E_ILIT);
        e->ival   = strtol(tok->text, NULL, 10);
        es_push(s, e); return 0;
    }
    case SNOCONE_REAL: {
        EXPR_t *e = expr_new(E_FLIT);
        e->dval   = strtod(tok->text, NULL);
        es_push(s, e); return 0;
    }
    case SNOCONE_STRING: {
        EXPR_t *e = expr_new(E_QLIT);
        int len = (int)strlen(tok->text);
        if (len >= 2 && (tok->text[0] == '\'' || tok->text[0] == '"'))
            e->sval = strndup(tok->text + 1, len - 2);
        else
            e->sval = strdup(tok->text);
        es_push(s, e); return 0;
    }
    case SNOCONE_IDENT: {
        EXPR_t *e = expr_new(E_VAR);
        e->sval   = strdup(tok->text);
        es_push(s, e); return 0;
    }

    /* ---- Arithmetic ---- */
    case SNOCONE_PLUS: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, expr_binary(E_ADD, l, r)); return 0;
    }
    case SNOCONE_MINUS:
        if (tok->is_unary) {
            es_push(s, expr_unary(E_NEG, es_pop(s))); return 0;
        } else {
            EXPR_t *r = es_pop(s), *l = es_pop(s);
            es_push(s, expr_binary(E_SUB, l, r)); return 0;
        }
    case SNOCONE_STAR:
        if (tok->is_unary) {
            es_push(s, expr_unary(E_INDR, es_pop(s))); return 0;
        } else {
            EXPR_t *r = es_pop(s), *l = es_pop(s);
            es_push(s, expr_binary(E_MPY, l, r)); return 0;
        }
    case SNOCONE_SLASH: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, expr_binary(E_DIV, l, r)); return 0;
    }
    case SNOCONE_CARET: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, expr_binary(E_POW, l, r)); return 0;
    }

    /* ---- String / pattern composition ---- */
    case SNOCONE_CONCAT:   /* && → blank concat */
    case SNOCONE_PIPE: {   /* |  → blank concat in value context */
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, expr_binary(E_CONCAT, l, r)); return 0;
    }
    case SNOCONE_OR: {     /* || → pattern alternation → E_ALT */
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, expr_binary(E_ALT, l, r)); return 0;
    }
    case SNOCONE_PERIOD: {
        EXPR_t *var = es_pop(s), *expr = es_pop(s);
        es_push(s, expr_binary(E_CAPT_COND, expr, var)); return 0;
    }
    case SNOCONE_DOLLAR:
        if (tok->is_unary) {
            es_push(s, expr_unary(E_INDR, es_pop(s))); return 0;
        } else {
            EXPR_t *var = es_pop(s), *expr = es_pop(s);
            es_push(s, expr_binary(E_CAPT_IMM, expr, var)); return 0;
        }
    case SNOCONE_AT: {
        es_push(s, expr_unary(E_CAPT_CUR, es_pop(s))); return 0;
    }
    case SNOCONE_AMPERSAND: {
        EXPR_t *operand = es_pop(s);
        EXPR_t *e = expr_new(E_KW);
        e->sval = strdup(operand && operand->sval ? operand->sval : "");
        free(operand);
        es_push(s, e); return 0;
    }
    case SNOCONE_TILDE: {
        es_push(s, make_fnc1("NOT", es_pop(s))); return 0;
    }
    case SNOCONE_QUESTION:
        if (tok->is_unary) {
            es_push(s, make_fnc1("DIFFER", es_pop(s)));
        } else {
            /* binary: subject ? pattern  →  E_MATCH (scan/pattern-match IR node) */
            EXPR_t *r = es_pop(s), *l = es_pop(s);
            es_push(s, expr_binary(E_MATCH, l, r));
        }
        return 0;

    /* ---- Numeric comparisons → function calls ---- */
    case SNOCONE_EQ: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("EQ",l,r));  return 0; }
    case SNOCONE_NE: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("NE",l,r));  return 0; }
    case SNOCONE_LT: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LT",l,r));  return 0; }
    case SNOCONE_GT: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("GT",l,r));  return 0; }
    case SNOCONE_LE: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LE",l,r));  return 0; }
    case SNOCONE_GE: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("GE",l,r));  return 0; }

    /* ---- String comparisons → function calls ---- */
    case SNOCONE_STR_IDENT:  { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("IDENT", l,r));  return 0; }
    case SNOCONE_STR_DIFFER: { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("DIFFER",l,r)); return 0; }
    case SNOCONE_STR_LT:     { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LLT",l,r));    return 0; }
    case SNOCONE_STR_GT:     { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LGT",l,r));    return 0; }
    case SNOCONE_STR_LE:     { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LLE",l,r));    return 0; }
    case SNOCONE_STR_GE:     { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LGE",l,r));    return 0; }
    case SNOCONE_STR_EQ:     { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LEQ",l,r));    return 0; }
    case SNOCONE_STR_NE:     { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("LNE",l,r));    return 0; }
    case SNOCONE_PERCENT:    { EXPR_t *r=es_pop(s),*l=es_pop(s); es_push(s,make_fnc2("REMDR",l,r));  return 0; }

    /* ---- Compound assignments: x OP= rhs → x = x OP rhs ---- */
    case SNOCONE_PLUS_ASSIGN: {
        EXPR_t *rhs = es_pop(s), *lhs = es_pop(s);
        /* duplicate lhs for the operation */
        EXPR_t *lhs2 = expr_new(E_VAR);
        lhs2->sval = strdup(lhs->sval ? lhs->sval : "");
        EXPR_t *op  = expr_binary(E_ADD, lhs2, rhs);
        es_push(s, expr_binary(E_ASSIGN, lhs, op)); return 0;
    }
    case SNOCONE_MINUS_ASSIGN: {
        EXPR_t *rhs = es_pop(s), *lhs = es_pop(s);
        EXPR_t *lhs2 = expr_new(E_VAR); lhs2->sval = strdup(lhs->sval ? lhs->sval : "");
        es_push(s, expr_binary(E_ASSIGN, lhs, expr_binary(E_SUB, lhs2, rhs))); return 0;
    }
    case SNOCONE_STAR_ASSIGN: {
        EXPR_t *rhs = es_pop(s), *lhs = es_pop(s);
        EXPR_t *lhs2 = expr_new(E_VAR); lhs2->sval = strdup(lhs->sval ? lhs->sval : "");
        es_push(s, expr_binary(E_ASSIGN, lhs, expr_binary(E_MPY, lhs2, rhs))); return 0;
    }
    case SNOCONE_SLASH_ASSIGN: {
        EXPR_t *rhs = es_pop(s), *lhs = es_pop(s);
        EXPR_t *lhs2 = expr_new(E_VAR); lhs2->sval = strdup(lhs->sval ? lhs->sval : "");
        es_push(s, expr_binary(E_ASSIGN, lhs, expr_binary(E_DIV, lhs2, rhs))); return 0;
    }
    case SNOCONE_PERCENT_ASSIGN: {
        EXPR_t *rhs = es_pop(s), *lhs = es_pop(s);
        EXPR_t *lhs2 = expr_new(E_VAR); lhs2->sval = strdup(lhs->sval ? lhs->sval : "");
        es_push(s, expr_binary(E_ASSIGN, lhs, make_fnc2("REMDR", lhs2, rhs))); return 0;
    }
    case SNOCONE_CARET_ASSIGN: {
        EXPR_t *rhs = es_pop(s), *lhs = es_pop(s);
        EXPR_t *lhs2 = expr_new(E_VAR); lhs2->sval = strdup(lhs->sval ? lhs->sval : "");
        es_push(s, expr_binary(E_ASSIGN, lhs, expr_binary(E_POW, lhs2, rhs))); return 0;
    }

    /* ---- Assignment ---- */
    case SNOCONE_ASSIGN: {
        /* X =;  has only LHS on stack (no RHS tokens before semicolon).
         * Detect by checking stack depth: if 1 item, it IS the lhs, rhs = null. */
        EXPR_t *rhs, *lhs;
        if (s->top >= 2) {
            rhs = es_pop(s); lhs = es_pop(s);
        } else {
            lhs = es_pop(s); rhs = expr_new(E_NUL);
        }
        es_push(s, expr_binary(E_ASSIGN, lhs, rhs)); return 0;
    }

    /* ---- Function call ---- */
    case SNOCONE_CALL: {
        int nargs = tok->arg_count;
        EXPR_t *fn = expr_new(E_FNC);
        EXPR_t **tmp = nargs > 0 ? malloc(nargs * sizeof(EXPR_t *)) : NULL;
        for (int k = nargs - 1; k >= 0; k--) tmp[k] = es_pop(s);
        EXPR_t *name_node = es_pop(s);
        fn->sval = strdup(name_node && name_node->sval ? name_node->sval : "");
        free(name_node);
        for (int k = 0; k < nargs; k++) expr_add_child(fn, tmp[k]);
        free(tmp);
        es_push(s, fn); return 0;
    }

    /* ---- Array ref ---- */
    case SNOCONE_ARRAY_REF: {
        /* Build E_IDX matching the SNOBOL4 parser convention:
         *   children[0] = base array (E_VAR node)
         *   children[1..n] = index args
         * The emitter (emit_x64.c) guards on nchildren >= 2 and uses
         * children[0] for the array, children[1] for the key. */
        int nargs = tok->arg_count;
        EXPR_t *base = expr_new(E_IDX);
        EXPR_t **tmp = nargs > 0 ? malloc(nargs * sizeof(EXPR_t *)) : NULL;
        for (int k = nargs - 1; k >= 0; k--) tmp[k] = es_pop(s);
        EXPR_t *name_node = es_pop(s);
        /* Make the array-name node children[0] (keep it as E_VAR) */
        expr_add_child(base, name_node);
        for (int k = 0; k < nargs; k++) expr_add_child(base, tmp[k]);
        free(tmp);
        es_push(s, base); return 0;
    }

    /* ---- Statement terminators / control-flow keywords (handled by CF pass) ---- */
    case SNOCONE_NEWLINE:
    case SNOCONE_SEMICOLON:
    case SNOCONE_EOF:
    case SNOCONE_KW_IF:
    case SNOCONE_KW_ELSE:
    case SNOCONE_KW_WHILE:
    case SNOCONE_KW_DO:
    case SNOCONE_KW_FOR:
    case SNOCONE_KW_RETURN:
    case SNOCONE_KW_FRETURN:
    case SNOCONE_KW_NRETURN:
    case SNOCONE_KW_GO:       /* kept for enum slot only — not in keyword table */
    case SNOCONE_KW_TO:       /* kept for enum slot only — not in keyword table */
    case SNOCONE_KW_GOTO:
    case SNOCONE_KW_BREAK:
    case SNOCONE_KW_CONTINUE:
    case SNOCONE_KW_PROCEDURE:
    case SNOCONE_KW_STRUCT:
    case SNOCONE_KW_THEN:
        return 0;

    default:
        fprintf(stderr, "emit_x64_snocone: unhandled token kind %d ('%s')\n",
                (int)tok->kind, tok->text ? tok->text : "");
        (*nerrors)++;
        return -1;
    }
}

/* sc_pat_concat_to_seq — rewrite E_CONCAT → E_SEQ in a pattern tree in-place.
 * Snocone uses && (E_CONCAT) for pattern sequence; the shared emit_x64 pattern
 * emitter only knows E_SEQ.  This rewrite is Snocone-local and does not touch
 * any other frontend's IR. */
static void sc_pat_concat_to_seq(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_CONCAT) e->kind = E_SEQ;
    for (int i = 0; i < e->nchildren; i++)
        sc_pat_concat_to_seq(e->children[i]);
}

/* Assemble a STMT_t from the top of the expression stack */
static STMT_t *assemble_stmt(ExprStack *s, int lineno) {
    EXPR_t *top = es_peek(s);
    if (!top) return NULL;
    STMT_t *st = stmt_new();
    st->lineno = lineno;
    if (top->kind == E_ASSIGN) {
        es_pop(s);
        EXPR_t *lhs = expr_left(top);
        EXPR_t *rhs = expr_right(top);
        free(top);
        /* X ? pat = repl  lowers to ASSIGN(MATCH(X, pat), repl).
         * Unwrap so subject/pattern/replacement are set correctly. */
        if (lhs && lhs->kind == E_MATCH) {
            st->subject     = expr_left(lhs);
            st->pattern     = expr_right(lhs);
            sc_pat_concat_to_seq(st->pattern);
            st->replacement = rhs;
            st->has_eq      = 1;
            free(lhs);
        } else {
            st->subject     = lhs;
            st->replacement = rhs;
            st->has_eq      = 1;
        }
    } else if (top->kind == E_MATCH) {
        /* X ? pat  (no replacement) — unwrap into subject + pattern */
        es_pop(s);
        st->subject = expr_left(top);
        st->pattern = expr_right(top);
        sc_pat_concat_to_seq(st->pattern);
        free(top);
    } else {
        es_pop(s);
        st->subject = top;
    }
    return st;
}

/* =========================================================================
 * CF state  (formerly snocone_cf.c)
 * ======================================================================= */

#define LOOP_STACK_MAX 64

typedef struct {
    char *brk;   /* label after loop end — target of break */
    char *cont;  /* label for next iteration — target of continue */
} LoopFrame;

typedef struct {
    const SnoconeToken *toks;
    int                 count;
    int                 pos;
    const char         *filename;
    int                 nerrors;
    int                 label_ctr;
    Program            *prog;
    char               *fname;          /* current function name for return */
    LoopFrame           loop_stack[LOOP_STACK_MAX];
    int                 loop_depth;
} CfState;

/* ---- helpers ---- */

static char *sc_newlab(CfState *st) {
    char buf[32];
    snprintf(buf, sizeof buf, "L.%d", ++st->label_ctr);
    return strdup(buf);
}

static const SnoconeToken *sc_cur(CfState *st) {
    if (st->pos < st->count) return &st->toks[st->pos];
    static SnoconeToken eof_tok = { SNOCONE_EOF, NULL, 0 };
    return &eof_tok;
}
static void sc_advance(CfState *st) {
    if (st->pos < st->count) st->pos++;
}
static void sc_skip_nl(CfState *st) {
    while (st->pos < st->count &&
           (st->toks[st->pos].kind == SNOCONE_NEWLINE ||
            st->toks[st->pos].kind == SNOCONE_SEMICOLON))
        st->pos++;
}
static int sc_consume_kw(CfState *st, SnoconeKind k) {
    sc_skip_nl(st);
    if (sc_cur(st)->kind == k) { sc_advance(st); return 1; }
    return 0;
}

static void sc_prog_append(CfState *st, STMT_t *s) {
    if (!s) return;
    s->next = NULL;
    if (!st->prog->head) st->prog->head = st->prog->tail = s;
    else { st->prog->tail->next = s; st->prog->tail = s; }
    st->prog->nstmts++;
}

static void sc_emit_label(CfState *st, const char *lab) {
    STMT_t *s = stmt_new();
    s->label = strdup(lab);
    sc_prog_append(st, s);
}

static void sc_emit_goto(CfState *st, const char *target) {
    STMT_t *s = stmt_new();
    s->go = sgoto_new();
    s->go->uncond = strdup(target);
    sc_prog_append(st, s);
}

static void sc_emit_end(CfState *st) {
    STMT_t *s = stmt_new();
    s->is_end = 1;
    s->label  = strdup("END");
    sc_prog_append(st, s);
}

/* ---- loop stack helpers ---- */

static void sc_loop_push(CfState *st, const char *brk, const char *cont) {
    if (st->loop_depth >= LOOP_STACK_MAX) {
        fprintf(stderr, "%s: loops nested too deeply (max %d)\n",
                st->filename, LOOP_STACK_MAX);
        st->nerrors++;
        return;
    }
    st->loop_stack[st->loop_depth].brk  = strdup(brk);
    st->loop_stack[st->loop_depth].cont = strdup(cont);
    st->loop_depth++;
}

static void sc_loop_pop(CfState *st) {
    if (st->loop_depth <= 0) return;
    st->loop_depth--;
    free(st->loop_stack[st->loop_depth].brk);
    free(st->loop_stack[st->loop_depth].cont);
    st->loop_stack[st->loop_depth].brk  = NULL;
    st->loop_stack[st->loop_depth].cont = NULL;
}

/* ---- expression clause compiler (calls expression lowering on a token segment) ---- */

static STMT_t *sc_compile_expr(CfState *st, SnoconeKind stop_kind) {
    int start = st->pos;
    int depth = 0;
    while (st->pos < st->count) {
        SnoconeKind k = st->toks[st->pos].kind;
        if (k == SNOCONE_NEWLINE || k == SNOCONE_SEMICOLON || k == SNOCONE_EOF) break;
        if (k == SNOCONE_LPAREN || k == SNOCONE_LBRACKET) depth++;
        if (k == SNOCONE_RPAREN || k == SNOCONE_RBRACKET) {
            if (depth == 0 && stop_kind == SNOCONE_RPAREN) break;
            if (depth > 0) depth--;
        }
        if (stop_kind != SNOCONE_EOF && stop_kind != SNOCONE_RPAREN && k == stop_kind) break;
        st->pos++;
    }
    int seg_len = st->pos - start;
    if (seg_len == 0) return NULL;

    ScParseResult pr = snocone_parse(st->toks + start, seg_len);
    if (pr.count == 0) { sc_parse_free(&pr); return NULL; }

    int cap = pr.count + 1;
    ScPToken *buf = malloc(cap * sizeof(ScPToken));
    memcpy(buf, pr.tokens, pr.count * sizeof(ScPToken));
    ScPToken nl; memset(&nl, 0, sizeof nl);
    nl.kind = SNOCONE_NEWLINE; nl.text = (char *)"\n";
    buf[pr.count] = nl;

    /* --- inline expression lowering (no separate call) --- */
    Program   *eprog   = calloc(1, sizeof(Program));
    ExprStack  estack  = { .top = 0 };
    int        enerrs  = 0;
    int        last_ln = 1;

    for (int i = 0; i < cap; i++) {
        const ScPToken *tok = &buf[i];
        if (tok->kind == SNOCONE_NEWLINE || tok->kind == SNOCONE_SEMICOLON) {
            if (estack.top > 0) {
                STMT_t *s = assemble_stmt(&estack, last_ln);
                if (s) {
                    if (!eprog->head) eprog->head = eprog->tail = s;
                    else { eprog->tail->next = s; eprog->tail = s; }
                    eprog->nstmts++;
                }
                estack.top = 0;
            }
            continue;
        }
        if (tok->kind == SNOCONE_EOF) break;
        last_ln = tok->line ? tok->line : last_ln;
        lower_token(tok, &estack, st->filename, &enerrs);
    }
    free(buf);
    sc_parse_free(&pr);

    if (enerrs > 0) { st->nerrors += enerrs; free(eprog); return NULL; }
    STMT_t *s = eprog->head;
    if (s) eprog->head = eprog->tail = NULL;
    free(eprog);
    return s;
}

static STMT_t *sc_compile_paren_expr(CfState *st) {
    sc_skip_nl(st);
    if (sc_cur(st)->kind != SNOCONE_LPAREN) {
        fprintf(stderr, "%s: expected '('\n", st->filename);
        st->nerrors++; return NULL;
    }
    sc_advance(st); /* consume ( */
    int start = st->pos, depth = 1;
    while (st->pos < st->count && depth > 0) {
        SnoconeKind k = st->toks[st->pos].kind;
        if (k == SNOCONE_LPAREN) depth++;
        if (k == SNOCONE_RPAREN) depth--;
        st->pos++;
    }
    int inner_len = st->pos - start - 1; /* exclude close paren */
    if (inner_len <= 0) return NULL;

    ScParseResult pr = snocone_parse(st->toks + start, inner_len);
    if (pr.count == 0) { sc_parse_free(&pr); return NULL; }

    int cap = pr.count + 1;
    ScPToken *buf = malloc(cap * sizeof(ScPToken));
    memcpy(buf, pr.tokens, pr.count * sizeof(ScPToken));
    ScPToken nl; memset(&nl, 0, sizeof nl);
    nl.kind = SNOCONE_NEWLINE; nl.text = (char *)"\n";
    buf[pr.count] = nl;

    Program   *eprog  = calloc(1, sizeof(Program));
    ExprStack  estack = { .top = 0 };
    int        enerrs = 0;

    for (int i = 0; i < cap; i++) {
        const ScPToken *tok = &buf[i];
        if (tok->kind == SNOCONE_NEWLINE || tok->kind == SNOCONE_SEMICOLON) {
            if (estack.top > 0) {
                STMT_t *s = assemble_stmt(&estack, 1);
                if (s) {
                    if (!eprog->head) eprog->head = eprog->tail = s;
                    else { eprog->tail->next = s; eprog->tail = s; }
                    eprog->nstmts++;
                }
                estack.top = 0;
            }
            continue;
        }
        if (tok->kind == SNOCONE_EOF) break;
        lower_token(tok, &estack, st->filename, &enerrs);
    }
    free(buf);
    sc_parse_free(&pr);

    if (enerrs > 0) { st->nerrors += enerrs; free(eprog); return NULL; }
    STMT_t *s = eprog->head;
    if (s) eprog->head = eprog->tail = NULL;
    free(eprog);
    return s;
}

static void sc_emit_cond(CfState *st, STMT_t *cond_s,
                         const char *s_lab, const char *f_lab) {
    if (!cond_s) cond_s = stmt_new();
    cond_s->go = sgoto_new();
    if (s_lab) cond_s->go->onsuccess = strdup(s_lab);
    if (f_lab) cond_s->go->onfailure = strdup(f_lab);
    sc_prog_append(st, cond_s);
}

/* forward decls */
static void sc_do_stmt(CfState *st);
static void sc_do_block(CfState *st);

static void sc_do_body(CfState *st) {
    sc_skip_nl(st);
    if (sc_cur(st)->kind == SNOCONE_LBRACE) sc_do_block(st);
    else sc_do_stmt(st);
}

static void sc_do_block(CfState *st) {
    sc_skip_nl(st);
    if (sc_cur(st)->kind != SNOCONE_LBRACE) { sc_do_stmt(st); return; }
    sc_advance(st); /* consume { */
    sc_skip_nl(st);
    while (sc_cur(st)->kind != SNOCONE_RBRACE && sc_cur(st)->kind != SNOCONE_EOF) {
        sc_do_stmt(st);
        sc_skip_nl(st);
    }
    if (sc_cur(st)->kind == SNOCONE_RBRACE) sc_advance(st);
}

static void sc_do_return(CfState *st, SnoconeKind ret_kind) {
    sc_skip_nl(st);
    SnoconeKind next = sc_cur(st)->kind;
    int has_expr = (next != SNOCONE_NEWLINE && next != SNOCONE_SEMICOLON &&
                    next != SNOCONE_RBRACE  && next != SNOCONE_EOF);
    if (has_expr && ret_kind != SNOCONE_KW_FRETURN) {
        STMT_t *val_s = sc_compile_expr(st, SNOCONE_EOF);
        if (val_s && st->fname && val_s->subject) {
            EXPR_t *lhs = expr_new(E_VAR);
            lhs->sval = strdup(st->fname);
            STMT_t *asgn = stmt_new();
            asgn->subject     = lhs;
            asgn->replacement = val_s->subject;
            asgn->has_eq      = 1;
            val_s->subject    = NULL;
            free(val_s);
            sc_prog_append(st, asgn);
        } else if (val_s) {
            sc_prog_append(st, val_s);
        }
    }
    STMT_t *ret_s = stmt_new();
    ret_s->go = sgoto_new();
    ret_s->go->uncond = strdup(
        ret_kind == SNOCONE_KW_FRETURN ? "FRETURN" :
        ret_kind == SNOCONE_KW_NRETURN ? "NRETURN" : "RETURN");
    sc_prog_append(st, ret_s);
}

static void sc_do_procedure(CfState *st) {
    sc_skip_nl(st);
    if (sc_cur(st)->kind != SNOCONE_IDENT) {
        fprintf(stderr, "%s: expected procedure name\n", st->filename);
        st->nerrors++; return;
    }
    char *fname = strdup(sc_cur(st)->text);
    sc_advance(st);

    /* argument list */
    char args_buf[256] = "";
    sc_skip_nl(st);
    if (sc_cur(st)->kind == SNOCONE_LPAREN) {
        sc_advance(st);
        int first = 1;
        sc_skip_nl(st);
        while (sc_cur(st)->kind != SNOCONE_RPAREN && sc_cur(st)->kind != SNOCONE_EOF) {
            if (!first && sc_cur(st)->kind == SNOCONE_COMMA) sc_advance(st);
            sc_skip_nl(st);
            if (sc_cur(st)->kind == SNOCONE_IDENT) {
                if (!first) strncat(args_buf, ",", sizeof args_buf - strlen(args_buf) - 1);
                strncat(args_buf, sc_cur(st)->text, sizeof args_buf - strlen(args_buf) - 1);
                sc_advance(st);
                first = 0;
            } else break;
            sc_skip_nl(st);
        }
        if (sc_cur(st)->kind == SNOCONE_RPAREN) sc_advance(st);
    }

    /* optional locals list — second paren group before { */
    char locals_buf[256] = "";
    sc_skip_nl(st);
    if (sc_cur(st)->kind == SNOCONE_LPAREN) {
        sc_advance(st);
        int first = 1;
        sc_skip_nl(st);
        while (sc_cur(st)->kind != SNOCONE_RPAREN && sc_cur(st)->kind != SNOCONE_EOF) {
            if (!first && sc_cur(st)->kind == SNOCONE_COMMA) sc_advance(st);
            sc_skip_nl(st);
            if (sc_cur(st)->kind == SNOCONE_IDENT) {
                if (!first) strncat(locals_buf, ",", sizeof locals_buf - strlen(locals_buf) - 1);
                strncat(locals_buf, sc_cur(st)->text, sizeof locals_buf - strlen(locals_buf) - 1);
                sc_advance(st);
                first = 0;
            } else break;
            sc_skip_nl(st);
        }
        if (sc_cur(st)->kind == SNOCONE_RPAREN) sc_advance(st);
    }

    /* jump around body */
    char *end_lab = malloc(strlen(fname) + 8);
    sprintf(end_lab, "%s.END", fname);
    sc_emit_goto(st, end_lab);

    /* DEFINE call */
    {
        char def_buf[512];
        snprintf(def_buf, sizeof def_buf, "DEFINE('%s(%s)%s')",
                 fname, args_buf, locals_buf);
        EXPR_t *arg  = expr_new(E_QLIT);
        arg->sval    = strdup(def_buf + 8);  /* strip DEFINE(' prefix for sval */
        /* Actually emit as E_FNC("DEFINE", E_QLIT("fname(args)locals")) */
        char inner[512];
        snprintf(inner, sizeof inner, "%s(%s)%s", fname, args_buf, locals_buf);
        EXPR_t *qlit = expr_new(E_QLIT);
        qlit->sval   = strdup(inner);
        EXPR_t *call = expr_new(E_FNC);
        call->sval   = strdup("DEFINE");
        expr_add_child(call, qlit);
        free(arg);
        STMT_t *def_s  = stmt_new();
        def_s->subject = call;
        sc_prog_append(st, def_s);
    }

    sc_emit_label(st, fname);

    char *outer_fname = st->fname;
    st->fname = fname;

    sc_skip_nl(st);
    sc_do_body(st);

    st->fname = outer_fname;

    {
        STMT_t *ret_s = stmt_new();
        ret_s->go = sgoto_new();
        ret_s->go->uncond = strdup("RETURN");
        sc_prog_append(st, ret_s);
    }

    sc_emit_label(st, end_lab);
    free(end_lab);
    free(fname);
}

static void sc_do_struct(CfState *st) {
    /* struct name { field, field, ... }
     * Emits: DATA('name(field,field,...)') */
    sc_skip_nl(st);
    if (sc_cur(st)->kind != SNOCONE_IDENT) {
        fprintf(stderr, "%s: expected struct name\n", st->filename);
        st->nerrors++; return;
    }
    char *sname = strdup(sc_cur(st)->text);
    sc_advance(st);

    /* collect field names inside { } */
    char fields_buf[512] = "";
    sc_skip_nl(st);
    if (sc_cur(st)->kind == SNOCONE_LBRACE) {
        sc_advance(st);
        int first = 1;
        sc_skip_nl(st);
        while (sc_cur(st)->kind != SNOCONE_RBRACE && sc_cur(st)->kind != SNOCONE_EOF) {
            if (!first && sc_cur(st)->kind == SNOCONE_COMMA) sc_advance(st);
            sc_skip_nl(st);
            if (sc_cur(st)->kind == SNOCONE_IDENT) {
                if (!first) strncat(fields_buf, ",", sizeof fields_buf - strlen(fields_buf) - 1);
                strncat(fields_buf, sc_cur(st)->text, sizeof fields_buf - strlen(fields_buf) - 1);
                sc_advance(st);
                first = 0;
            } else break;
            sc_skip_nl(st);
        }
        if (sc_cur(st)->kind == SNOCONE_RBRACE) sc_advance(st);
    }

    /* emit DATA('name(fields)') */
    char inner[512];
    snprintf(inner, sizeof inner, "%s(%s)", sname, fields_buf);
    EXPR_t *qlit = expr_new(E_QLIT);
    qlit->sval   = strdup(inner);
    EXPR_t *call = expr_new(E_FNC);
    call->sval   = strdup("DATA");
    expr_add_child(call, qlit);
    STMT_t *def_s  = stmt_new();
    def_s->subject = call;
    sc_prog_append(st, def_s);

    free(sname);
}

/* ---- main statement dispatcher ---- */

static void sc_do_stmt(CfState *st) {
    sc_skip_nl(st);
    SnoconeKind k = sc_cur(st)->kind;

    /* ---- if ( cond ) [then] body [else body] ---- */
    if (k == SNOCONE_KW_IF) {
        sc_advance(st);
        STMT_t *cond_s = sc_compile_paren_expr(st);

        char *lab_true  = sc_newlab(st);
        char *lab_false = sc_newlab(st);
        char *lab_end   = sc_newlab(st);

        sc_emit_cond(st, cond_s, lab_true, lab_false);

        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_KW_THEN) sc_advance(st);

        sc_emit_label(st, lab_true);
        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_LBRACE) {
            sc_do_block(st);
        } else {
            STMT_t *body_s = sc_compile_expr(st, SNOCONE_KW_ELSE);
            if (body_s) sc_prog_append(st, body_s);
        }

        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_KW_ELSE) {
            sc_emit_goto(st, lab_end);
            sc_emit_label(st, lab_false);
            sc_advance(st);
            sc_do_body(st);
            sc_emit_label(st, lab_end);
        } else {
            sc_emit_label(st, lab_false);
        }

        free(lab_true); free(lab_false); free(lab_end);
        return;
    }

    /* ---- while ( cond ) body ---- */
    if (k == SNOCONE_KW_WHILE) {
        sc_advance(st);
        char *lab_start = sc_newlab(st);
        char *lab_body  = sc_newlab(st);
        char *lab_end   = sc_newlab(st);

        sc_emit_label(st, lab_start);
        STMT_t *cond_s = sc_compile_paren_expr(st);
        sc_emit_cond(st, cond_s, lab_body, lab_end);

        sc_emit_label(st, lab_body);
        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_KW_DO) sc_advance(st);

        sc_loop_push(st, lab_end, lab_start);
        sc_do_body(st);
        sc_loop_pop(st);

        sc_emit_goto(st, lab_start);
        sc_emit_label(st, lab_end);

        free(lab_start); free(lab_body); free(lab_end);
        return;
    }

    /* ---- do body while ( cond ) ---- */
    if (k == SNOCONE_KW_DO) {
        sc_advance(st);
        char *lab_start = sc_newlab(st);
        char *lab_end   = sc_newlab(st);

        sc_emit_label(st, lab_start);

        sc_loop_push(st, lab_end, lab_start);
        sc_do_body(st);
        sc_loop_pop(st);

        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_KW_WHILE) {
            sc_advance(st);
            STMT_t *cond_s = sc_compile_paren_expr(st);
            sc_emit_cond(st, cond_s, lab_start, lab_end);
        }
        sc_emit_label(st, lab_end);

        free(lab_start); free(lab_end);
        return;
    }

    /* ---- for ( init ; cond ; step ) body ---- */
    if (k == SNOCONE_KW_FOR) {
        sc_advance(st);
        sc_skip_nl(st);
        if (sc_cur(st)->kind != SNOCONE_LPAREN) { st->nerrors++; return; }
        sc_advance(st); /* consume ( */

        STMT_t *init_s = sc_compile_expr(st, SNOCONE_SEMICOLON);
        if (sc_cur(st)->kind == SNOCONE_SEMICOLON) sc_advance(st);
        STMT_t *cond_s = sc_compile_expr(st, SNOCONE_SEMICOLON);
        if (sc_cur(st)->kind == SNOCONE_SEMICOLON) sc_advance(st);
        STMT_t *step_s = sc_compile_expr(st, SNOCONE_RPAREN);
        if (sc_cur(st)->kind == SNOCONE_RPAREN) sc_advance(st);

        char *lab_test = sc_newlab(st);
        char *lab_body = sc_newlab(st);
        char *lab_step = sc_newlab(st);   /* continue target */
        char *lab_end  = sc_newlab(st);

        if (init_s) sc_prog_append(st, init_s);
        sc_emit_label(st, lab_test);
        sc_emit_cond(st, cond_s, lab_body, lab_end);
        sc_emit_label(st, lab_body);

        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_KW_DO) sc_advance(st);

        sc_loop_push(st, lab_end, lab_step);
        sc_do_body(st);
        sc_loop_pop(st);

        sc_emit_label(st, lab_step);        /* continue lands here */
        if (step_s) sc_prog_append(st, step_s);
        sc_emit_goto(st, lab_test);
        sc_emit_label(st, lab_end);

        free(lab_test); free(lab_body); free(lab_step); free(lab_end);
        return;
    }

    /* ---- goto label  (C-style — only form supported) ---- */
    if (k == SNOCONE_KW_GOTO) {
        sc_advance(st);
        sc_skip_nl(st);
        if (sc_cur(st)->kind == SNOCONE_IDENT) {
            char *target = strdup(sc_cur(st)->text);
            sc_advance(st);
            sc_emit_goto(st, target);
            free(target);
        } else { st->nerrors++; }
        return;
    }

    /* ---- break ---- */
    if (k == SNOCONE_KW_BREAK) {
        sc_advance(st);
        if (st->loop_depth == 0) {
            fprintf(stderr, "%s: break outside loop\n", st->filename);
            st->nerrors++; return;
        }
        sc_emit_goto(st, st->loop_stack[st->loop_depth - 1].brk);
        return;
    }

    /* ---- continue ---- */
    if (k == SNOCONE_KW_CONTINUE) {
        sc_advance(st);
        if (st->loop_depth == 0) {
            fprintf(stderr, "%s: continue outside loop\n", st->filename);
            st->nerrors++; return;
        }
        sc_emit_goto(st, st->loop_stack[st->loop_depth - 1].cont);
        return;
    }

    /* ---- procedure ---- */
    if (k == SNOCONE_KW_PROCEDURE) {
        sc_advance(st);
        sc_do_procedure(st);
        return;
    }

    /* ---- struct ---- */
    if (k == SNOCONE_KW_STRUCT) {
        sc_advance(st);
        sc_do_struct(st);
        return;
    }

    /* ---- return / freturn / nreturn ---- */
    if (k == SNOCONE_KW_RETURN || k == SNOCONE_KW_FRETURN || k == SNOCONE_KW_NRETURN) {
        sc_advance(st);
        sc_do_return(st, k);
        return;
    }

    /* ---- { block } ---- */
    if (k == SNOCONE_LBRACE) { sc_do_block(st); return; }

    /* ---- } end-of-block ---- */
    if (k == SNOCONE_RBRACE) return;

    /* ---- expression / assignment statement ---- */
    if (k != SNOCONE_EOF && k != SNOCONE_NEWLINE && k != SNOCONE_SEMICOLON) {
        STMT_t *s = sc_compile_expr(st, SNOCONE_EOF);
        if (s) sc_prog_append(st, s);
        sc_skip_nl(st);
        return;
    }
    if (k == SNOCONE_NEWLINE || k == SNOCONE_SEMICOLON) sc_advance(st);
}

/* =========================================================================
 * Public entry point
 * ======================================================================= */

Program *emit_x64_snocone_compile(const char *source, const char *filename) {
    if (!filename) filename = "<stdin>";

    ScTokenArray ta = snocone_lex(source);

    CfState st;
    memset(&st, 0, sizeof st);
    st.toks     = ta.tokens;
    st.count    = ta.count;
    st.filename = filename;
    st.prog     = calloc(1, sizeof(Program));

    sc_skip_nl(&st);
    while (sc_cur(&st)->kind != SNOCONE_EOF) {
        sc_do_stmt(&st);
        sc_skip_nl(&st);
    }

    sc_emit_end(&st);
    sc_tokens_free(&ta);

    if (st.nerrors > 0)
        fprintf(stderr, "emit_x64_snocone_compile: %d error(s) in %s\n",
                st.nerrors, filename);

    return st.prog;
}
