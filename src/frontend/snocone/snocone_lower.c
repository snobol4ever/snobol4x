/*
 * snocone_lower.c -- Snocone postfix ScPToken[] → EXPR_t/STMT_t IR  (Sprint SC2)
 *
 * Ported from:
 *   snobol4jvm  snocone_emitter.clj  (operator table, emit-binary logic)
 *   snobol4dotnet SnoconeParser.cs   (shunting-yard reference)
 *
 * The postfix stream from snocone_parse() is a standard RPN expression.
 * We maintain an EXPR_t* operand stack.  When we hit SNOCONE_NEWLINE we
 * pop the top expression and assemble a STMT_t.
 *
 * Assignment detection:
 *   SNOCONE_ASSIGN in postfix: pop rhs, pop lhs.
 *   If lhs is a simple E_VAR or E_KEYWORD the STMT_t is:
 *       subject=lhs  replacement=rhs  (no pattern field)
 *   This matches OUTPUT = 'hello', x = expr, etc.
 *
 * Pattern match detection (future — Sprint SC4):
 *   If subject is a variable and a pattern field would be present,
 *   pattern match stmts use SNOBOL4's  subject ? pattern = replace
 *   which maps to STMT_t subject/pattern/replacement.  Not emitted here.
 */

#include "snocone_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Operand stack
 * ------------------------------------------------------------------------- */
#define STACK_MAX 1024

typedef struct {
    EXPR_t *v[STACK_MAX];
    int     top;   /* index of next free slot */
} ExprStack;

static void  es_push(ExprStack *s, EXPR_t *e) {
    if (s->top >= STACK_MAX) { fprintf(stderr, "snocone_lower: stack overflow\n"); exit(1); }
    s->v[s->top++] = e;
}
static EXPR_t *es_pop(ExprStack *s) {
    if (s->top <= 0) { fprintf(stderr, "snocone_lower: stack underflow\n"); return expr_new(E_NUL); }
    return s->v[--s->top];
}
static EXPR_t *es_peek(ExprStack *s) {
    if (s->top <= 0) return NULL;
    return s->v[s->top - 1];
}

/* ---------------------------------------------------------------------------
 * Helper: build a 2-arg E_FNC node
 * ------------------------------------------------------------------------- */
static EXPR_t *make_fnc2(const char *name, EXPR_t *l, EXPR_t *r) {
    EXPR_t *e  = expr_new(E_FNC);
    e->sval    = strdup(name);
    expr_add_child(e, l);
    expr_add_child(e, r);
    return e;
}

/* Helper: build a 1-arg E_FNC node */
static EXPR_t *make_fnc1(const char *name, EXPR_t *arg) {
    EXPR_t *e  = expr_new(E_FNC);
    e->sval    = strdup(name);
    expr_add_child(e, arg);
    return e;
}

/* ---------------------------------------------------------------------------
 * Lower one postfix token onto the operand stack
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */
static int lower_token(const ScPToken *tok, ExprStack *s,
                       const char *filename, int *nerrors)
{
    (void)filename;

    /* ---- Operands ---- */
    switch ((int)tok->kind) {
    case SNOCONE_INTEGER: {
        EXPR_t *e = expr_new(E_ILIT);
        e->ival   = strtol(tok->text, NULL, 10);
        es_push(s, e);
        return 0;
    }
    case SNOCONE_REAL: {
        EXPR_t *e = expr_new(E_FLIT);
        e->dval   = strtod(tok->text, NULL);
        es_push(s, e);
        return 0;
    }
    case SNOCONE_STRING: {
        EXPR_t *e = expr_new(E_QLIT);
        /* tok->text includes surrounding quotes — strip them */
        int len = (int)strlen(tok->text);
        if (len >= 2 && (tok->text[0] == '\'' || tok->text[0] == '"')) {
            e->sval = strndup(tok->text + 1, len - 2);
        } else {
            e->sval = strdup(tok->text);
        }
        es_push(s, e);
        return 0;
    }
    case SNOCONE_IDENT: {
        EXPR_t *e = expr_new(E_VAR);
        e->sval   = strdup(tok->text);
        es_push(s, e);
        return 0;
    }

    /* ---- Binary arithmetic ---- */
    case SNOCONE_PLUS: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        EXPR_t *e = expr_binary(E_ADD, l, r);
        es_push(s, e); return 0;
    }
    case SNOCONE_MINUS:
        if (tok->is_unary) {
            EXPR_t *operand = es_pop(s);
            EXPR_t *e = expr_unary(E_MNS, operand);
            es_push(s, e); return 0;
        } else {
            EXPR_t *r = es_pop(s), *l = es_pop(s);
            EXPR_t *e = expr_binary(E_SUB, l, r);
            es_push(s, e); return 0;
        }
    case SNOCONE_STAR:
        if (tok->is_unary) {
            /* unary * = indirect reference */
            EXPR_t *operand = es_pop(s);
            EXPR_t *e = expr_unary(E_INDIRECT, operand);
            es_push(s, e); return 0;
        } else {
            EXPR_t *r = es_pop(s), *l = es_pop(s);
            EXPR_t *e = expr_binary(E_MUL, l, r);
            es_push(s, e); return 0;
        }
    case SNOCONE_SLASH: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        EXPR_t *e = expr_binary(E_DIV, l, r);
        es_push(s, e); return 0;
    }
    case SNOCONE_CARET: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        EXPR_t *e = expr_binary(E_POW, l, r);
        es_push(s, e); return 0;
    }

    /* ---- String / pattern composition ---- */
    case SNOCONE_CONCAT: {
        /* && blank concat → E_CAT (value-context string concat) */
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        EXPR_t *e = expr_binary(E_CAT, l, r);
        es_push(s, e); return 0;
    }
    case SNOCONE_PIPE: {
        /* single | also string concat in SNOBOL4 context */
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        EXPR_t *e = expr_binary(E_CAT, l, r);
        es_push(s, e); return 0;
    }
    case SNOCONE_OR: {
        /* || string concatenation (same as | and && in value context) → E_CAT */
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        EXPR_t *e = expr_binary(E_CAT, l, r);
        es_push(s, e); return 0;
    }
    case SNOCONE_PERIOD: {
        /* . conditional capture: expr . var → E_CAPT_COND_ASGN(left=expr, right=var) */
        EXPR_t *var  = es_pop(s);
        EXPR_t *expr = es_pop(s);
        EXPR_t *e    = expr_binary(E_CAPT_COND_ASGN, expr, var);
        es_push(s, e); return 0;
    }
    case SNOCONE_DOLLAR:
        if (tok->is_unary) {
            /* unary $ = indirect lvalue (E_INDIRECT used as assignment target) */
            EXPR_t *operand = es_pop(s);
            EXPR_t *e = expr_unary(E_INDIRECT, operand);
            es_push(s, e); return 0;
        } else {
            /* binary $ = immediate capture: expr $ var → E_CAPT_IMMED_ASGN */
            EXPR_t *var  = es_pop(s);
            EXPR_t *expr = es_pop(s);
            EXPR_t *e    = expr_binary(E_CAPT_IMMED_ASGN, expr, var);
            es_push(s, e); return 0;
        }
    case SNOCONE_AT: {
        /* @var — cursor position capture */
        EXPR_t *var = es_pop(s);
        EXPR_t *e   = expr_unary(E_CAPT_CURSOR, var);
        es_push(s, e); return 0;
    }
    case SNOCONE_AMPERSAND: {
        /* unary & — keyword reference: &IDENT → E_KEYWORD */
        EXPR_t *operand = es_pop(s);
        EXPR_t *e = expr_new(E_KEYWORD);
        /* operand is E_VAR with the keyword name */
        e->sval = operand ? strdup(operand->sval ? operand->sval : "") : strdup("");
        /* free the wrapper E_VAR node we just consumed */
        free(operand);
        es_push(s, e); return 0;
    }
    case SNOCONE_TILDE: {
        /* ~ logical negation → NOT(expr) */
        EXPR_t *operand = es_pop(s);
        es_push(s, make_fnc1("NOT", operand));
        return 0;
    }
    case SNOCONE_QUESTION:
        if (tok->is_unary) {
            /* unary ? = DIFFER(x) or just return x — treat as DIFFER(x,"") */
            EXPR_t *operand = es_pop(s);
            es_push(s, make_fnc1("DIFFER", operand));
        } else {
            /* binary ? — alternation-like — map to DIFFER(a,b) per snocone.sc */
            EXPR_t *r = es_pop(s), *l = es_pop(s);
            es_push(s, make_fnc2("DIFFER", l, r));
        }
        return 0;

    /* ---- Comparison operators → function calls ---- */
    case SNOCONE_EQ: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("EQ", l, r)); return 0;
    }
    case SNOCONE_NE: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("NE", l, r)); return 0;
    }
    case SNOCONE_LT: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LT", l, r)); return 0;
    }
    case SNOCONE_GT: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("GT", l, r)); return 0;
    }
    case SNOCONE_LE: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LE", l, r)); return 0;
    }
    case SNOCONE_GE: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("GE", l, r)); return 0;
    }
    case SNOCONE_STR_IDENT: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("IDENT", l, r)); return 0;
    }
    case SNOCONE_STR_DIFFER: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("DIFFER", l, r)); return 0;
    }
    case SNOCONE_STR_LT: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LLT", l, r)); return 0;
    }
    case SNOCONE_STR_GT: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LGT", l, r)); return 0;
    }
    case SNOCONE_STR_LE: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LLE", l, r)); return 0;
    }
    case SNOCONE_STR_GE: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LGE", l, r)); return 0;
    }
    case SNOCONE_STR_EQ: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LEQ", l, r)); return 0;
    }
    case SNOCONE_STR_NE: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("LNE", l, r)); return 0;
    }
    case SNOCONE_PERCENT: {
        EXPR_t *r = es_pop(s), *l = es_pop(s);
        es_push(s, make_fnc2("REMDR", l, r)); return 0;
    }

    /* ---- Assignment: pop rhs, pop lhs, push E_ASSIGN ---- */
    case SNOCONE_ASSIGN: {
        EXPR_t *rhs = es_pop(s);
        EXPR_t *lhs = es_pop(s);
        EXPR_t *e   = expr_binary(E_ASSIGN, lhs, rhs);
        es_push(s, e);
        return 0;
    }

    /* ---- Function call: pop nargs args + name from stack ---- */
    case SNOCONE_CALL: {
        int     nargs = tok->arg_count;
        EXPR_t *fn    = expr_new(E_FNC);
        /* args on stack postfix: arg0 pushed first → arg(n-1) on top */
        /* collect into temp array to reverse, then add as children */
        EXPR_t **tmp = nargs > 0 ? malloc(nargs * sizeof(EXPR_t *)) : NULL;
        for (int k = nargs - 1; k >= 0; k--)
            tmp[k] = es_pop(s);
        EXPR_t *name_node = es_pop(s);
        fn->sval = name_node ? strdup(name_node->sval ? name_node->sval : "") : strdup("");
        free(name_node);
        for (int k = 0; k < nargs; k++) expr_add_child(fn, tmp[k]);
        if (tmp) free(tmp);
        es_push(s, fn);
        return 0;
    }

    /* ---- Array ref: a[i] → E_IDX ---- */
    case SNOCONE_ARRAY_REF: {
        int     nargs = tok->arg_count;
        EXPR_t *base  = expr_new(E_IDX);
        EXPR_t **tmp  = nargs > 0 ? malloc(nargs * sizeof(EXPR_t *)) : NULL;
        for (int k = nargs - 1; k >= 0; k--)
            tmp[k] = es_pop(s);
        EXPR_t *name_node = es_pop(s);
        base->sval = name_node ? strdup(name_node->sval ? name_node->sval : "") : strdup("");
        free(name_node);
        /* children[0] = base (represented by sval only, no node), children[1..] = indices */
        for (int k = 0; k < nargs; k++) expr_add_child(base, tmp[k]);
        if (tmp) free(tmp);
        es_push(s, base);
        return 0;
    }

    /* ---- Statement terminators handled by caller ---- */
    case SNOCONE_NEWLINE:
    case SNOCONE_SEMICOLON:
    case SNOCONE_EOF:
        /* handled in the main loop */
        return 0;

    /* ---- Keywords not relevant to expression lowering ---- */
    case SNOCONE_KW_IF:
    case SNOCONE_KW_ELSE:
    case SNOCONE_KW_WHILE:
    case SNOCONE_KW_DO:
    case SNOCONE_KW_FOR:
    case SNOCONE_KW_RETURN:
    case SNOCONE_KW_FRETURN:
    case SNOCONE_KW_NRETURN:
    case SNOCONE_KW_GO:
    case SNOCONE_KW_TO:
    case SNOCONE_KW_PROCEDURE:
    case SNOCONE_KW_STRUCT:
        /* Control-flow keywords — Sprint SC3 will handle these via a
         * higher-level pass over snocone_parse output.  For now, skip. */
        return 0;

    default:
        fprintf(stderr, "snocone_lower: unhandled token kind %d ('%s') at line %d\n",
                (int)tok->kind, tok->text ? tok->text : "", tok->line);
        (*nerrors)++;
        return -1;
    }
}

/* ---------------------------------------------------------------------------
 * Assemble a STMT_t from the top of the stack after SNOCONE_NEWLINE
 * ------------------------------------------------------------------------- */
static STMT_t *assemble_stmt(ExprStack *s, int lineno) {
    EXPR_t *top = es_peek(s);
    if (!top) return NULL;   /* blank line */

    STMT_t *st = stmt_new();
    st->lineno = lineno;

    if (top->kind == E_ASSIGN) {
        /* Assignment: subject = lhs, replacement = rhs */
        es_pop(s);
        st->subject     = expr_left(top);
        st->replacement = expr_right(top);
        st->has_eq      = 1;
        free(top);   /* free E_ASSIGN shell only */
    } else {
        /* Expression-only statement (output, pattern match, etc.) */
        es_pop(s);
        st->subject = top;
    }

    return st;
}

/* ---------------------------------------------------------------------------
 * snocone_lower — main entry point
 * ------------------------------------------------------------------------- */
ScLowerResult snocone_lower(const ScPToken *ptoks, int count, const char *filename) {
    ScLowerResult result = { NULL, 0 };
    Program *prog = calloc(1, sizeof(Program));
    result.prog   = prog;

    ExprStack stack = { .top = 0 };
    int       last_line = 1;

    for (int i = 0; i < count; i++) {
        const ScPToken *tok = &ptoks[i];

        if (tok->kind == SNOCONE_NEWLINE || tok->kind == SNOCONE_SEMICOLON) {
            /* End of logical statement — assemble if anything on stack */
            if (stack.top > 0) {
                STMT_t *st = assemble_stmt(&stack, last_line);
                if (st) {
                    if (!prog->head) {
                        prog->head = prog->tail = st;
                    } else {
                        prog->tail->next = st;
                        prog->tail = st;
                    }
                    prog->nstmts++;
                }
                /* If there are leftover operands (shouldn't happen), discard */
                stack.top = 0;
            }
            if (tok->line) last_line = tok->line;
            continue;
        }

        if (tok->kind == SNOCONE_EOF) break;

        last_line = tok->line ? tok->line : last_line;
        lower_token(tok, &stack, filename, &result.nerrors);
    }

    /* Flush any trailing expression (no final newline) */
    if (stack.top > 0) {
        STMT_t *st = assemble_stmt(&stack, last_line);
        if (st) {
            if (!prog->head) {
                prog->head = prog->tail = st;
            } else {
                prog->tail->next = st;
                prog->tail = st;
            }
            prog->nstmts++;
        }
    }

    return result;
}

/* ---------------------------------------------------------------------------
 * sc_lower_free
 * ------------------------------------------------------------------------- */
static void free_expr(EXPR_t *e) {
    if (!e) return;
    for (int i = 0; i < e->nchildren; i++) free_expr(e->children[i]);
    if (e->children) free(e->children);
    free(e->sval);
    free(e);
}

static void free_stmt(STMT_t *st) {
    if (!st) return;
    free_stmt(st->next);
    free_expr(st->subject);
    free_expr(st->pattern);
    free_expr(st->replacement);
    free(st->label);
    free(st->go);
    free(st);
}

void sc_lower_free(ScLowerResult *r) {
    if (!r || !r->prog) return;
    free_stmt(r->prog->head);
    free(r->prog);
    r->prog = NULL;
}
