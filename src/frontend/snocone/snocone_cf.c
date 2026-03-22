/*
 * sc_cf.c — Snocone control-flow lowering pass  (Sprint SC4-ASM)
 *
 * Walks the flat SnoconeToken[] stream from snocone_lex() and produces a Program*
 * whose STMT_t list uses SNOBOL4-style labeled gotos for all control flow.
 *
 * Mirrors the logic in snocone.sc: nclause() / dostmt() / funct() / dostruct()
 *
 * Architecture:
 *   snocone_lex()  → ScTokenArray (flat token stream)
 *   snocone_cf_compile() → Program* (STMT_t list with labels + go fields)
 *
 * Each Snocone control construct is lowered to labeled SNOBOL4 STMT_t nodes:
 *
 *   while (cond) body  →   Lstart: [cond] :S(Lbody)F(Lend)
 *                           Lbody:  [body]
 *                                   goto Lstart
 *                           Lend:
 *
 *   if (cond) s1 else s2 → [cond] :S(Ls1)F(Ls2)
 *                           Ls1:  [s1]
 *                                 goto Lend
 *                           Ls2:  [s2]
 *                           Lend:
 *
 *   for (init; cond; step) body →
 *                           [init]
 *                           Ltest: [cond] :S(Lbody)F(Lend)
 *                           Lbody: [body]
 *                                  [step]
 *                                  goto Ltest
 *                           Lend:
 *
 *   goto label  →  [empty stmt] :goto label (uncond goto)
 *
 *   procedure f(args) body → function Byrd-box (named pattern / DEFINE)
 *     The DEFINE call is added to a deferred list (like snocone.sc deflist).
 *     The body is emitted between a goto-around and the function label.
 *
 *   return expr  →  [fname = expr]  RETURN
 *   freturn      →  FRETURN
 *   nreturn expr →  [fname = expr]  NRETURN (or bare NRETURN)
 *
 *   { stmt; stmt; ... }  →  inline stmt sequence
 *
 *   expr  (expression clause)  →  [expr stmt, no goto]
 *
 * Expression clauses are compiled via snocone_parse() + snocone_lower() exactly as
 * before.  Control-flow keywords are consumed by this pass, not snocone_parse.
 */

#include "snocone_cf.h"
#include "snocone_lex.h"
#include "snocone_parse.h"
#include "snocone_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

typedef struct {
    const SnoconeToken *toks;
    int            count;
    int            pos;        /* current read position */
    const char    *filename;
    int            nerrors;
    int            label_ctr;  /* for newlab() */
    Program       *prog;
    /* Current function name (for return value assignment) */
    char          *fname;
} CfState;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static char *newlab(CfState *st) {
    char buf[32];
    snprintf(buf, sizeof buf, "L.%d", ++st->label_ctr);
    return strdup(buf);
}

static const SnoconeToken *cur(CfState *st) {
    if (st->pos < st->count) return &st->toks[st->pos];
    /* Return a synthetic EOF token */
    static SnoconeToken eof_tok = { SNOCONE_EOF, NULL, 0 };
    return &eof_tok;
}

static void advance(CfState *st) {
    if (st->pos < st->count) st->pos++;
}

/* Skip whitespace/newline tokens */
static void skip_nl(CfState *st) {
    while (st->pos < st->count &&
           (st->toks[st->pos].kind == SNOCONE_NEWLINE ||
            st->toks[st->pos].kind == SNOCONE_SEMICOLON))
        st->pos++;
}

/* Append a STMT_t to the program */
static void prog_append(CfState *st, STMT_t *s) {
    if (!s) return;
    s->next = NULL;
    if (!st->prog->head) st->prog->head = st->prog->tail = s;
    else { st->prog->tail->next = s; st->prog->tail = s; }
    st->prog->nstmts++;
}

/* Emit a pure-label STMT_t (empty body, no expr) */
static void emit_label(CfState *st, const char *lab) {
    STMT_t *s = stmt_new();
    s->label = strdup(lab);
    prog_append(st, s);
}

/* Emit an unconditional goto STMT_t */
static void emit_goto(CfState *st, const char *target) {
    STMT_t *s = stmt_new();
    s->go = sgoto_new();
    s->go->uncond = strdup(target);
    prog_append(st, s);
}

/* Emit a synthetic END sentinel */
static void emit_end(CfState *st) {
    STMT_t *s = stmt_new();
    s->is_end = 1;
    s->label  = strdup("END");
    prog_append(st, s);
}

/* -------------------------------------------------------------------------
 * Expression clause compilation (snocone_parse + snocone_lower on a token segment)
 * Consumes tokens up to but not including the next NEWLINE/SEMICOLON/EOF
 * or the stop_kind token.  Returns a STMT_t* (may be NULL for blank).
 *
 * The produced STMT_t has no label and no go — caller fills those in.
 * ---------------------------------------------------------------------- */
static STMT_t *compile_expr_clause(CfState *st, SnoconeKind stop_kind) {
    /* Collect tokens for this expression */
    int start = st->pos;
    while (st->pos < st->count) {
        SnoconeKind k = st->toks[st->pos].kind;
        if (k == SNOCONE_NEWLINE || k == SNOCONE_SEMICOLON || k == SNOCONE_EOF) break;
        if (stop_kind != SNOCONE_EOF && k == stop_kind) break;
        st->pos++;
    }
    int seg_len = st->pos - start;
    if (seg_len == 0) return NULL;

    ScParseResult pr = snocone_parse(st->toks + start, seg_len);
    if (pr.count == 0) { sc_parse_free(&pr); return NULL; }

    /* Wrap in a SNOCONE_NEWLINE sentinel for snocone_lower */
    int cap = pr.count + 1;
    ScPToken *buf = malloc(cap * sizeof(ScPToken));
    memcpy(buf, pr.tokens, pr.count * sizeof(ScPToken));
    ScPToken nl; memset(&nl, 0, sizeof nl);
    nl.kind = SNOCONE_NEWLINE; nl.text = (char *)"\n";
    buf[pr.count] = nl;

    ScLowerResult lr = snocone_lower(buf, cap, st->filename);
    free(buf);
    sc_parse_free(&pr);

    if (lr.nerrors > 0) {
        fprintf(stderr, "%s: expression error\n", st->filename);
        st->nerrors += lr.nerrors;
        sc_lower_free(&lr);
        return NULL;
    }

    /* Extract the single STMT_t produced */
    STMT_t *s = lr.prog ? lr.prog->head : NULL;
    if (s) { lr.prog->head = lr.prog->tail = NULL; lr.prog->nstmts = 0; }
    sc_lower_free(&lr);
    return s;
}

/* Forward declaration */
static void do_stmt(CfState *st);
static void do_block(CfState *st);

/* -------------------------------------------------------------------------
 * peek_kind — look at the current non-nl token kind
 * ---------------------------------------------------------------------- */
static SnoconeKind peek_kind(CfState *st) {
    int save = st->pos;
    skip_nl(st);
    SnoconeKind k = cur(st)->kind;
    st->pos = save;
    return k;
}

/* -------------------------------------------------------------------------
 * consume_keyword — advance past a keyword if it matches
 * ---------------------------------------------------------------------- */
static int consume_kw(CfState *st, SnoconeKind k) {
    skip_nl(st);
    if (cur(st)->kind == k) { advance(st); return 1; }
    return 0;
}

/* -------------------------------------------------------------------------
 * Collect tokens inside balanced parens ( ... ) or braces { ... }
 * On entry pos points at the open delimiter.  Returns token array for
 * the interior (not including delimiters), advances pos past close.
 * ---------------------------------------------------------------------- */
static void collect_balanced(CfState *st, SnoconeKind open, SnoconeKind close,
                              const SnoconeToken **out_toks, int *out_count) {
    skip_nl(st);
    if (cur(st)->kind != open) {
        fprintf(stderr, "%s: expected '%c'\n", st->filename,
                open == SNOCONE_LPAREN ? '(' : '{');
        st->nerrors++;
        *out_toks = NULL; *out_count = 0;
        return;
    }
    advance(st); /* consume open */
    int start = st->pos;
    int depth = 1;
    while (st->pos < st->count && depth > 0) {
        SnoconeKind k = st->toks[st->pos].kind;
        if (k == open)  depth++;
        if (k == close) depth--;
        if (depth > 0) st->pos++;
        else st->pos++; /* consume close */
    }
    *out_toks  = st->toks + start;
    *out_count = st->pos - start - 1; /* exclude close */
}

/* -------------------------------------------------------------------------
 * compile_paren_expr — compile expression inside ( ... ) into a STMT_t
 * ---------------------------------------------------------------------- */
static STMT_t *compile_paren_expr(CfState *st) {
    const SnoconeToken *inner; int inner_len;
    collect_balanced(st, SNOCONE_LPAREN, SNOCONE_RPAREN, &inner, &inner_len);
    if (!inner || inner_len == 0) return NULL;

    ScParseResult pr = snocone_parse(inner, inner_len);
    if (pr.count == 0) { sc_parse_free(&pr); return NULL; }

    int cap = pr.count + 1;
    ScPToken *buf = malloc(cap * sizeof(ScPToken));
    memcpy(buf, pr.tokens, pr.count * sizeof(ScPToken));
    ScPToken nl; memset(&nl, 0, sizeof nl);
    nl.kind = SNOCONE_NEWLINE; nl.text = (char *)"\n";
    buf[pr.count] = nl;

    ScLowerResult lr = snocone_lower(buf, cap, st->filename);
    free(buf);
    sc_parse_free(&pr);

    if (lr.nerrors > 0) { st->nerrors += lr.nerrors; sc_lower_free(&lr); return NULL; }
    STMT_t *s = lr.prog ? lr.prog->head : NULL;
    if (s) { lr.prog->head = lr.prog->tail = NULL; lr.prog->nstmts = 0; }
    sc_lower_free(&lr);
    return s;
}

/* -------------------------------------------------------------------------
 * emit_cond_stmt — emit a condition statement with S/F gotos.
 * cond_stmt: the compiled expression STMT_t (subject already set).
 * Caller passes s_label (success target) and f_label (failure target).
 * If either is NULL, no goto is emitted for that port.
 * ---------------------------------------------------------------------- */
static void emit_cond(CfState *st, STMT_t *cond_s,
                      const char *s_lab, const char *f_lab) {
    if (!cond_s) { cond_s = stmt_new(); }
    cond_s->go = sgoto_new();
    if (s_lab) cond_s->go->onsuccess = strdup(s_lab);
    if (f_lab) cond_s->go->onfailure = strdup(f_lab);
    prog_append(st, cond_s);
}

/* -------------------------------------------------------------------------
 * do_body — emit one statement or a brace-delimited block
 * ---------------------------------------------------------------------- */
static void do_body(CfState *st) {
    skip_nl(st);
    if (cur(st)->kind == SNOCONE_LBRACE) {
        do_block(st);
    } else {
        do_stmt(st);
    }
}

/* -------------------------------------------------------------------------
 * do_block — emit a { ... } block
 * ---------------------------------------------------------------------- */
static void do_block(CfState *st) {
    skip_nl(st);
    if (cur(st)->kind != SNOCONE_LBRACE) { do_stmt(st); return; }
    advance(st); /* consume { */
    skip_nl(st);
    while (cur(st)->kind != SNOCONE_RBRACE && cur(st)->kind != SNOCONE_EOF) {
        do_stmt(st);
        skip_nl(st);
    }
    if (cur(st)->kind == SNOCONE_RBRACE) advance(st); /* consume } */
}

/* -------------------------------------------------------------------------
 * do_return_stmt — handle return/freturn/nreturn
 * ---------------------------------------------------------------------- */
static void do_return_stmt(CfState *st, SnoconeKind ret_kind) {
    /* Consume optional expression on same line (before newline/semicolon/})  */
    skip_nl(st);
    SnoconeKind next = cur(st)->kind;
    int has_expr = (next != SNOCONE_NEWLINE && next != SNOCONE_SEMICOLON &&
                    next != SNOCONE_RBRACE  && next != SNOCONE_EOF);

    if (has_expr && ret_kind != SNOCONE_KW_FRETURN) {
        STMT_t *val_s = compile_expr_clause(st, SNOCONE_EOF);
        if (val_s) {
            /* Assign return value: fname = expr */
            if (st->fname && val_s->subject) {
                /* Wrap as assignment: fname = subject expr */
                EXPR_t *lhs = expr_new(E_VART);
                lhs->sval = strdup(st->fname);
                STMT_t *asgn = stmt_new();
                asgn->subject     = lhs;
                asgn->replacement = val_s->subject;
                asgn->has_eq      = 1;
                val_s->subject    = NULL;
                free(val_s);
                prog_append(st, asgn);
            } else {
                prog_append(st, val_s);
            }
        }
    }

    /* Emit the RETURN/FRETURN/NRETURN goto */
    STMT_t *ret_s = stmt_new();
    ret_s->go = sgoto_new();
    const char *target = (ret_kind == SNOCONE_KW_FRETURN)  ? "FRETURN" :
                         (ret_kind == SNOCONE_KW_NRETURN)   ? "NRETURN" :
                                                          "RETURN";
    ret_s->go->uncond = strdup(target);
    prog_append(st, ret_s);
}

/* -------------------------------------------------------------------------
 * do_procedure — handle "procedure name(args) { body }"
 * ---------------------------------------------------------------------- */
static void do_procedure(CfState *st) {
    skip_nl(st);

    /* Read function name */
    if (cur(st)->kind != SNOCONE_IDENT) {
        fprintf(stderr, "%s: expected procedure name\n", st->filename);
        st->nerrors++; return;
    }
    char *fname = strdup(cur(st)->text);
    advance(st);

    /* Read argument list ( id, id, ... ) */
    char args_buf[256] = "";
    skip_nl(st);
    if (cur(st)->kind == SNOCONE_LPAREN) {
        advance(st); /* consume ( */
        int first = 1;
        skip_nl(st);
        while (cur(st)->kind != SNOCONE_RPAREN && cur(st)->kind != SNOCONE_EOF) {
            if (!first) {
                if (cur(st)->kind == SNOCONE_COMMA) advance(st);
                skip_nl(st);
            }
            if (cur(st)->kind == SNOCONE_IDENT) {
                if (!first) strncat(args_buf, ",", sizeof args_buf - strlen(args_buf) - 1);
                strncat(args_buf, cur(st)->text, sizeof args_buf - strlen(args_buf) - 1);
                advance(st);
                first = 0;
            } else break;
            skip_nl(st);
        }
        if (cur(st)->kind == SNOCONE_RPAREN) advance(st);
    }

    /* Local variable list: optional second paren group before { */
    char locals_buf[256] = "";
    skip_nl(st);
    if (cur(st)->kind == SNOCONE_LPAREN) {
        /* second paren = locals (rare in snocone.sc but supported) */
        advance(st);
        int first = 1;
        skip_nl(st);
        while (cur(st)->kind != SNOCONE_RPAREN && cur(st)->kind != SNOCONE_EOF) {
            if (!first) {
                if (cur(st)->kind == SNOCONE_COMMA) advance(st);
                skip_nl(st);
            }
            if (cur(st)->kind == SNOCONE_IDENT) {
                if (!first) strncat(locals_buf, ",", sizeof locals_buf - strlen(locals_buf) - 1);
                strncat(locals_buf, cur(st)->text, sizeof locals_buf - strlen(locals_buf) - 1);
                advance(st);
                first = 0;
            } else break;
            skip_nl(st);
        }
        if (cur(st)->kind == SNOCONE_RPAREN) advance(st);
    }

    /* Emit: goto fname.END (jump over function body) */
    char *end_lab = malloc(strlen(fname) + 8);
    sprintf(end_lab, "%s.END", fname);
    emit_goto(st, end_lab);

    /* Emit DEFINE call as a statement: DEFINE('fname(args)locals') */
    /* We model this as E_FNC("DEFINE", E_QLIT("fname(args)locals")) */
    {
        char def_buf[256 + 256 + 256 + 4]; /* fname + args_buf + locals_buf + "()" + NUL */
        snprintf(def_buf, sizeof def_buf, "%s(%s)%s", fname, args_buf, locals_buf);
        EXPR_t *arg = expr_new(E_QLIT);
        arg->sval = strdup(def_buf);
        EXPR_t *call = expr_new(E_FNC);
        call->sval   = strdup("DEFINE");
        expr_add_child(call, arg);
        STMT_t *def_s = stmt_new();
        def_s->subject = call;
        /* Mark deferred — emit at program start for now (append inline) */
        prog_append(st, def_s);
    }

    /* Emit function label */
    emit_label(st, fname);

    /* Save outer fname, set current fname for return statements */
    char *outer_fname = st->fname;
    st->fname = fname;

    /* Emit body */
    skip_nl(st);
    do_body(st);

    /* Restore fname */
    st->fname = outer_fname;

    /* Emit RETURN at end of function */
    {
        STMT_t *ret_s = stmt_new();
        ret_s->go = sgoto_new();
        ret_s->go->uncond = strdup("RETURN");
        prog_append(st, ret_s);
    }

    /* Emit fname.END label */
    emit_label(st, end_lab);
    free(end_lab);
    free(fname);
}

/* -------------------------------------------------------------------------
 * do_stmt — dispatch on current clause type
 * ---------------------------------------------------------------------- */
static void do_stmt(CfState *st) {
    skip_nl(st);
    SnoconeKind k = cur(st)->kind;

    /* ---- if ( cond ) body [ else body ] ---- */
    if (k == SNOCONE_KW_IF) {
        advance(st); /* consume 'if' */
        STMT_t *cond_s = compile_paren_expr(st);

        char *lab_true  = newlab(st);
        char *lab_false = newlab(st);
        char *lab_end   = newlab(st);

        emit_cond(st, cond_s, lab_true, lab_false);

        /* Consume optional 'then' keyword */
        skip_nl(st);
        if (cur(st)->kind == SNOCONE_KW_THEN) advance(st);

        /* True branch — if next token is '{' use block, otherwise
         * compile a single expression stopping at SNOCONE_KW_ELSE so that
         * single-line  if (c) then S1 else S2  works correctly. */
        emit_label(st, lab_true);
        skip_nl(st);
        if (cur(st)->kind == SNOCONE_LBRACE) {
            do_block(st);
        } else {
            STMT_t *body_s = compile_expr_clause(st, SNOCONE_KW_ELSE);
            if (body_s) prog_append(st, body_s);
        }

        /* Check for else */
        skip_nl(st);
        int has_else = (cur(st)->kind == SNOCONE_KW_ELSE);
        if (has_else) {
            emit_goto(st, lab_end);
            emit_label(st, lab_false);
            advance(st); /* consume 'else' */
            do_body(st);
            emit_label(st, lab_end);
        } else {
            emit_label(st, lab_false);
        }

        free(lab_true); free(lab_false); free(lab_end);
        return;
    }

    /* ---- while ( cond ) body ---- */
    if (k == SNOCONE_KW_WHILE) {
        advance(st);
        char *lab_start = newlab(st);
        char *lab_body  = newlab(st);
        char *lab_end   = newlab(st);

        emit_label(st, lab_start);
        STMT_t *cond_s = compile_paren_expr(st);
        emit_cond(st, cond_s, lab_body, lab_end);

        emit_label(st, lab_body);
        /* Consume optional 'do' keyword */
        skip_nl(st);
        if (cur(st)->kind == SNOCONE_KW_DO) advance(st);
        do_body(st);
        emit_goto(st, lab_start);
        emit_label(st, lab_end);

        free(lab_start); free(lab_body); free(lab_end);
        return;
    }

    /* ---- do body while ( cond ) ---- */
    if (k == SNOCONE_KW_DO) {
        advance(st);
        char *lab_start = newlab(st);
        char *lab_end   = newlab(st);

        emit_label(st, lab_start);
        do_body(st);

        /* expect 'while' */
        skip_nl(st);
        if (cur(st)->kind == SNOCONE_KW_WHILE) {
            advance(st);
            STMT_t *cond_s = compile_paren_expr(st);
            /* on success loop back, on failure exit */
            emit_cond(st, cond_s, lab_start, lab_end);
        }
        emit_label(st, lab_end);
        free(lab_start); free(lab_end);
        return;
    }

    /* ---- for ( init ; cond ; step ) body ---- */
    if (k == SNOCONE_KW_FOR) {
        advance(st);
        /* Collect three comma-separated expressions in parens */
        skip_nl(st);
        if (cur(st)->kind != SNOCONE_LPAREN) { st->nerrors++; return; }
        advance(st); /* consume ( */

        /* init */
        STMT_t *init_s = compile_expr_clause(st, SNOCONE_SEMICOLON);
        if (cur(st)->kind == SNOCONE_SEMICOLON) advance(st);
        /* cond */
        STMT_t *cond_s = compile_expr_clause(st, SNOCONE_SEMICOLON);
        if (cur(st)->kind == SNOCONE_SEMICOLON) advance(st);
        /* step */
        STMT_t *step_s = compile_expr_clause(st, SNOCONE_RPAREN);
        if (cur(st)->kind == SNOCONE_RPAREN) advance(st);

        char *lab_test = newlab(st);
        char *lab_body = newlab(st);
        char *lab_end  = newlab(st);

        if (init_s) prog_append(st, init_s);
        emit_label(st, lab_test);
        emit_cond(st, cond_s, lab_body, lab_end);
        emit_label(st, lab_body);
        /* Consume optional 'do' keyword */
        skip_nl(st);
        if (cur(st)->kind == SNOCONE_KW_DO) advance(st);
        do_body(st);
        if (step_s) prog_append(st, step_s);
        emit_goto(st, lab_test);
        emit_label(st, lab_end);

        free(lab_test); free(lab_body); free(lab_end);
        return;
    }

    /* ---- go to label ---- */
    if (k == SNOCONE_KW_GO) {
        advance(st);
        consume_kw(st, SNOCONE_KW_TO); /* optional 'to' */
        skip_nl(st);
        if (cur(st)->kind == SNOCONE_IDENT) {
            char *target = strdup(cur(st)->text);
            advance(st);
            emit_goto(st, target);
            free(target);
        } else { st->nerrors++; }
        return;
    }

    /* ---- procedure name ( args ) body ---- */
    if (k == SNOCONE_KW_PROCEDURE) {
        advance(st);
        do_procedure(st);
        return;
    }

    /* ---- return / freturn / nreturn ---- */
    if (k == SNOCONE_KW_RETURN || k == SNOCONE_KW_FRETURN || k == SNOCONE_KW_NRETURN) {
        SnoconeKind ret_kind = k;
        advance(st);
        do_return_stmt(st, ret_kind);
        return;
    }

    /* ---- { block } ---- */
    if (k == SNOCONE_LBRACE) {
        do_block(st);
        return;
    }

    /* ---- } end-of-block (should be consumed by do_block) ---- */
    if (k == SNOCONE_RBRACE) {
        return; /* caller (do_block) will consume it */
    }

    /* ---- expression / assignment statement ---- */
    if (k != SNOCONE_EOF && k != SNOCONE_NEWLINE && k != SNOCONE_SEMICOLON) {
        STMT_t *s = compile_expr_clause(st, SNOCONE_EOF);
        if (s) prog_append(st, s);
        /* consume trailing newline/semicolon */
        skip_nl(st);
        return;
    }
    /* blank line — skip */
    if (k == SNOCONE_NEWLINE || k == SNOCONE_SEMICOLON) advance(st);
}

/* -------------------------------------------------------------------------
 * snocone_cf_compile — public entry point
 * ---------------------------------------------------------------------- */
Program *snocone_cf_compile(const char *source, const char *filename) {
    if (!filename) filename = "<stdin>";

    ScTokenArray ta = snocone_lex(source);

    CfState st;
    memset(&st, 0, sizeof st);
    st.toks     = ta.tokens;
    st.count    = ta.count;
    st.pos      = 0;
    st.filename = filename;
    st.fname    = NULL;
    st.prog     = calloc(1, sizeof(Program));

    /* Walk all top-level statements */
    skip_nl(&st);
    while (cur(&st)->kind != SNOCONE_EOF) {
        do_stmt(&st);
        skip_nl(&st);
    }

    /* Append synthetic END sentinel */
    emit_end(&st);

    sc_tokens_free(&ta);

    if (st.nerrors > 0) {
        fprintf(stderr, "snocone_cf_compile: %d error(s) in %s\n",
                st.nerrors, filename);
        /* Return prog anyway — partial output is useful for debugging */
    }

    return st.prog;
}
