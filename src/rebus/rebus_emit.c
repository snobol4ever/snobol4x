/*
 * rebus_emit.c  —  SNOBOL4 emitter for Rebus (Griswold TR 84-9)
 *
 * Walks the RProgram AST produced by rebus_parse() and writes valid
 * SNOBOL4 source to a FILE*.
 *
 * Translation rules (TINY.md §Rebus / TR 84-9 §2):
 *
 *   record R(f1,f2)            → DATA('R(f1,f2)')
 *   function F(p1) local l1    → DEFINE('F(p1,l1)') :(F_end)
 *                                F  [initial guard]  [body]  F_end
 *   if E then S [else S2]      → [E] :F(rb_else_N)  [S] :(rb_end_N)
 *                                rb_else_N [S2]  rb_end_N
 *   unless E then S            → [E] :S(rb_end_N)  [S]  rb_end_N
 *   while  E do S              → rb_top_N  [E] :F(rb_end_N)  [S] :(rb_top_N)  rb_end_N
 *   until  E do S              → rb_top_N  [E] :S(rb_end_N)  [S] :(rb_top_N)  rb_end_N
 *   repeat S                   → rb_top_N  [S] :(rb_top_N)  rb_end_N
 *   for I from E1 to E2 do S  → I = E1
 *                                rb_top_N  GT(I,E2) :S(rb_end_N)
 *                                [S]  I = I + 1  :(rb_top_N)  rb_end_N
 *   exit                       → :(rb_end_N)   nearest enclosing loop
 *   next                       → :(rb_top_N)   nearest enclosing loop
 *   E1 := E2                   → E1 = E2
 *   E1 +:= E2                  → E1 = E1 + E2
 *   E1 -:= E2                  → E1 = E1 - E2
 *   E1 ||:= E2                 → E1 = E1 E2
 *   E1 || E2  /  E1 & E2       → E1 E2        (blank concat)
 *   E1 | E2                    → (E1 | E2)
 *   E1 ? E2                    → E1 ? E2
 *   E1 ? E2 <- E3              → E1 ? E2 = E3
 *   return E                   → FUNCNAME = E / :(RETURN)
 *   fail                       → :(FRETURN)
 *
 * Initial-clause guard:
 *   IDENT(F_RB_INIT_,'1') :S(F_RB_BODY_)
 *   F_RB_INIT_ = '1'
 *   [initial stmts]
 *   F_RB_BODY_
 *
 * Author: Claude Sonnet 4.6  (third developer, SNOBOL4-tiny)
 */

#include "rebus.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================
 * Label counter and loop stack
 * ================================================================ */
static int rb_label       = 0;
static int rb_loop_top[64];
static int rb_loop_end[64];
static int rb_loop_depth  = 0;
static const char *rb_current_func = "";
static int rb_stmt_fail_label = 0;  /* when >0, simple stmts append :F(rb_end_N) */

static int next_label(void) { return ++rb_label; }

static void push_loop(int top_n, int end_n) {
    if (rb_loop_depth < 64) {
        rb_loop_top[rb_loop_depth] = top_n;
        rb_loop_end[rb_loop_depth] = end_n;
        rb_loop_depth++;
    }
}
static void pop_loop(void) {
    if (rb_loop_depth > 0) rb_loop_depth--;
}

/* ================================================================
 * Forward declarations
 * ================================================================ */
static void emit_expr(RExpr *e, FILE *out);
static void emit_expr_atom(RExpr *e, FILE *out);
static void emit_stmt(RStmt *s, FILE *out);
static void emit_stmt_list(RStmt *head, FILE *out);

/* ================================================================
 * Expression emitter
 * ================================================================ */

static void emit_expr(RExpr *e, FILE *out) {
    if (!e) return;
    switch (e->kind) {

        case RE_NULL:   fprintf(out, "''"); break;
        case RE_INT:    fprintf(out, "%ld", e->ival); break;
        case RE_REAL:   fprintf(out, "%g",  e->dval); break;
        case RE_VAR:    fprintf(out, "%s",  e->sval ? e->sval : "?"); break;
        case RE_KEYWORD: fprintf(out, "&%s", e->sval ? e->sval : ""); break;

        case RE_STR:
            fprintf(out, "'");
            for (const char *p = e->sval ? e->sval : ""; *p; p++) {
                if (*p == '\'') fprintf(out, "''");
                else            fputc(*p, out);
            }
            fprintf(out, "'");
            break;

        /* Unary */
        case RE_NEG:   fprintf(out, "-(");        emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_POS:   fprintf(out, "+(");        emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_NOT:   fprintf(out, "DIFFER(");   emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_VALUE: fprintf(out, "IDENT(");    emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_BANG:  fprintf(out, "*");         emit_expr_atom(e->right,out); break;
        case RE_DEREF: fprintf(out, "*");         emit_expr_atom(e->right,out); break;
        case RE_CURSOR: fprintf(out, "@%s", e->sval ? e->sval : "?"); break;
        case RE_PATOPT:
            fprintf(out, "("); emit_expr(e->right,out); fprintf(out, " | '')"); break;

        /* Arithmetic */
        case RE_ADD: emit_expr_atom(e->left,out); fprintf(out," + "); emit_expr_atom(e->right,out); break;
        case RE_SUB: emit_expr_atom(e->left,out); fprintf(out," - "); emit_expr_atom(e->right,out); break;
        case RE_MUL: emit_expr_atom(e->left,out); fprintf(out," * "); emit_expr_atom(e->right,out); break;
        case RE_DIV: emit_expr_atom(e->left,out); fprintf(out," / "); emit_expr_atom(e->right,out); break;
        case RE_POW: emit_expr_atom(e->left,out); fprintf(out," ^ "); emit_expr_atom(e->right,out); break;
        case RE_MOD:
            fprintf(out,"REMDR("); emit_expr(e->left,out); fprintf(out,",");
            emit_expr(e->right,out); fprintf(out,")"); break;

        /* Concatenation: both || and & → blank concat in SNOBOL4 */
        case RE_STRCAT:
        case RE_PATCAT:
            emit_expr_atom(e->left,out); fprintf(out," "); emit_expr_atom(e->right,out); break;

        /* Alternation */
        case RE_ALT:
            fprintf(out,"("); emit_expr(e->left,out);
            fprintf(out," | "); emit_expr(e->right,out); fprintf(out,")"); break;

        /* Numeric comparisons */
        case RE_EQ: fprintf(out,"EQ(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_NE: fprintf(out,"NE(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_LT: fprintf(out,"LT(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_LE: fprintf(out,"LE(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_GT: fprintf(out,"GT(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_GE: fprintf(out,"GE(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;

        /* Lexical comparisons */
        case RE_SEQ: fprintf(out,"IDENT(");  emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_SNE: fprintf(out,"DIFFER("); emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_SLT: fprintf(out,"LLT(");    emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_SLE: fprintf(out,"LLE(");    emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_SGT: fprintf(out,"LGT(");    emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;
        case RE_SGE: fprintf(out,"LGE(");    emit_expr(e->left,out); fprintf(out,","); emit_expr(e->right,out); fprintf(out,")"); break;

        /* Assignment forms */
        case RE_ASSIGN:
            emit_expr(e->left,out); fprintf(out," = "); emit_expr(e->right,out); break;
        case RE_EXCHANGE:
            emit_expr(e->left,out); fprintf(out," :=: "); emit_expr(e->right,out); break;
        case RE_ADDASSIGN:
            emit_expr(e->left,out); fprintf(out," = ");
            emit_expr(e->left,out); fprintf(out," + "); emit_expr_atom(e->right,out); break;
        case RE_SUBASSIGN:
            emit_expr(e->left,out); fprintf(out," = ");
            emit_expr(e->left,out); fprintf(out," - "); emit_expr_atom(e->right,out); break;
        case RE_CATASSIGN:
            emit_expr(e->left,out); fprintf(out," = ");
            emit_expr(e->left,out); fprintf(out," ");   emit_expr_atom(e->right,out); break;

        /* Pattern captures */
        case RE_COND:
            emit_expr(e->left,out); fprintf(out," . "); emit_expr(e->right,out); break;
        case RE_IMM:
            emit_expr(e->left,out); fprintf(out," $ "); emit_expr(e->right,out); break;

        /* Substring range (inside RE_SUB_IDX args) */
        case RE_RANGE:
            emit_expr(e->left,out); fprintf(out," + "); emit_expr_atom(e->right,out); break;

        /* Function call */
        case RE_CALL:
            if (e->sval)       fprintf(out, "%s", e->sval);
            else if (e->left) { fprintf(out,"("); emit_expr(e->left,out); fprintf(out,")"); }
            fprintf(out,"(");
            for (int i = 0; i < e->nargs; i++) {
                if (i) fprintf(out,",");
                if (e->args[i]) emit_expr(e->args[i],out);
            }
            fprintf(out,")");
            break;

        /* Subscript: a[i]  or  a[start+:len] */
        case RE_SUB_IDX:
            emit_expr(e->left, out);
            if (e->nargs == 1 && e->args[0] && e->args[0]->kind == RE_RANGE) {
                /* a[start+:len] → a[start,len]  (MACRO SPITBOL substring) */
                fprintf(out,"[");
                emit_expr(e->args[0]->left,  out);
                fprintf(out,",");
                emit_expr(e->args[0]->right, out);
                fprintf(out,"]");
            } else {
                fprintf(out,"[");
                for (int i = 0; i < e->nargs; i++) {
                    if (i) fprintf(out,",");
                    if (e->args[i]) emit_expr(e->args[i],out);
                }
                fprintf(out,"]");
            }
            break;

        default:
            fprintf(out, "<expr:%d>", e->kind);
            break;
    }
}

static void emit_expr_atom(RExpr *e, FILE *out) {
    if (!e) return;
    switch (e->kind) {
        /* Self-delimiting — no extra parens needed */
        case RE_STR: case RE_INT: case RE_REAL: case RE_NULL:
        case RE_VAR: case RE_KEYWORD: case RE_CALL: case RE_SUB_IDX:
        case RE_CURSOR: case RE_DEREF: case RE_BANG:
            emit_expr(e, out);
            break;
        default:
            fprintf(out,"("); emit_expr(e,out); fprintf(out,")");
            break;
    }
}

/* ================================================================
 * Condition-to-string helper (for placing cond + goto on one line)
 * ================================================================ */
#define COND_BUF 4096
static char _cb[COND_BUF];

static const char *cond_str(RStmt *cond) {
    FILE *f = fmemopen(_cb, COND_BUF - 1, "w");
    if (!f) { _cb[0] = 0; return _cb; }
    if (cond) switch (cond->kind) {
        case RS_EXPR:
            emit_expr(cond->expr, f); break;
        case RS_MATCH:
            emit_expr(cond->expr,f); fprintf(f," ? "); emit_expr(cond->pat,f); break;
        case RS_REPLN:
            emit_expr(cond->expr,f); fprintf(f," ? "); emit_expr(cond->pat,f);
            fprintf(f," = ''"); break;
        case RS_REPLACE:
            emit_expr(cond->expr,f); fprintf(f," ? "); emit_expr(cond->pat,f);
            fprintf(f," = "); emit_expr(cond->repl,f); break;
        default:
            fprintf(f,"* <bad-cond-%d>", cond->kind); break;
    }
    fclose(f);
    return _cb;
}

/* ================================================================
 * Statement emitter
 * ================================================================ */

static void emit_stmt(RStmt *s, FILE *out) {
    if (!s) return;
    switch (s->kind) {

        case RS_EXPR:
            fprintf(out,"        "); emit_expr(s->expr,out);
            if (rb_stmt_fail_label) fprintf(out," :F(rb_end_%d)", rb_stmt_fail_label);
            fprintf(out,"\n"); break;

        case RS_MATCH:
            fprintf(out,"        ");
            emit_expr(s->expr,out); fprintf(out," ? "); emit_expr(s->pat,out);
            if (rb_stmt_fail_label) fprintf(out," :F(rb_end_%d)", rb_stmt_fail_label);
            fprintf(out,"\n"); break;

        case RS_REPLACE:
            fprintf(out,"        ");
            emit_expr(s->expr,out); fprintf(out," ? "); emit_expr(s->pat,out);
            fprintf(out," = "); emit_expr(s->repl,out);
            if (rb_stmt_fail_label) fprintf(out," :F(rb_end_%d)", rb_stmt_fail_label);
            fprintf(out,"\n"); break;

        case RS_REPLN:
            fprintf(out,"        ");
            emit_expr(s->expr,out); fprintf(out," ? "); emit_expr(s->pat,out);
            if (rb_stmt_fail_label) fprintf(out," :F(rb_end_%d)", rb_stmt_fail_label);
            fprintf(out," = ''\n"); break;

        /* if E then S1 [else S2] */
        case RS_IF: {
            int n = next_label();
            if (s->repl) {
                int n_end = next_label();
                fprintf(out,"        %s :F(rb_else_%d)\n", cond_str(s->body), n);
                emit_stmt(s->alt, out);
                fprintf(out,"        :(rb_end_%d)\n", n_end);
                fprintf(out,"rb_else_%d\n", n);
                emit_stmt((RStmt*)s->repl, out);
                fprintf(out,"rb_end_%d\n", n_end);
            } else {
                fprintf(out,"        %s :F(rb_end_%d)\n", cond_str(s->body), n);
                emit_stmt(s->alt, out);
                fprintf(out,"rb_end_%d\n", n);
            }
            break;
        }

        /* unless E then S */
        case RS_UNLESS: {
            int n = next_label();
            fprintf(out,"        %s :S(rb_end_%d)\n", cond_str(s->body), n);
            emit_stmt(s->alt, out);
            fprintf(out,"rb_end_%d\n", n);
            break;
        }

        /* while E do S */
        case RS_WHILE: {
            int n = next_label(), n_end = next_label();
            push_loop(n, n_end);
            fprintf(out,"rb_top_%d\n", n);
            fprintf(out,"        %s :F(rb_end_%d)\n", cond_str(s->body), n_end);
            emit_stmt(s->alt, out);
            fprintf(out,"        :(rb_top_%d)\n", n);
            fprintf(out,"rb_end_%d\n", n_end);
            pop_loop(); break;
        }

        /* until E do S */
        case RS_UNTIL: {
            int n = next_label(), n_end = next_label();
            push_loop(n, n_end);
            fprintf(out,"rb_top_%d\n", n);
            fprintf(out,"        %s :S(rb_end_%d)\n", cond_str(s->body), n_end);
            emit_stmt(s->alt, out);
            fprintf(out,"        :(rb_top_%d)\n", n);
            fprintf(out,"rb_end_%d\n", n_end);
            pop_loop(); break;
        }

        /* repeat S  — body stored in s->alt by parser */
        case RS_REPEAT: {
            int n = next_label(), n_end = next_label();
            push_loop(n, n_end);
            fprintf(out,"rb_top_%d\n", n);
            rb_stmt_fail_label = n_end;
            emit_stmt(s->alt, out);
            rb_stmt_fail_label = 0;
            fprintf(out,"        :(rb_top_%d)\n", n);
            fprintf(out,"rb_end_%d\n", n_end);
            pop_loop(); break;
        }

        /* for I from E1 to E2 [by E3] do S */
        case RS_FOR: {
            int n = next_label(), n_end = next_label();
            push_loop(n, n_end);
            const char *iv = s->for_var ? s->for_var : "RB_I_";
            fprintf(out,"        %s = ", iv); emit_expr(s->for_from,out); fprintf(out,"\n");
            fprintf(out,"rb_top_%d\n", n);
            fprintf(out,"        GT(%s,", iv); emit_expr(s->for_to,out);
            fprintf(out,") :S(rb_end_%d)\n", n_end);
            emit_stmt(s->alt, out);
            fprintf(out,"        %s = %s + ", iv, iv);
            if (s->for_by) emit_expr(s->for_by,out); else fprintf(out,"1");
            fprintf(out,"\n        :(rb_top_%d)\n", n);
            fprintf(out,"rb_end_%d\n", n_end);
            pop_loop(); break;
        }

        /* case E of { Vi:Si ; default:S0 } */
        case RS_CASE: {
            int n = next_label(), n_end = next_label();
            fprintf(out,"        RB_VAL_%d = ", n);
            emit_expr(s->case_expr, out); fprintf(out,"\n");
            int ci = 0, has_default = 0;
            for (RCase *c = s->cases; c; c = c->next) {
                if (c->is_default) { has_default = 1; continue; }
                fprintf(out,"        IDENT(RB_VAL_%d,", n);
                emit_expr(c->guard, out);
                fprintf(out,") :S(rb_c%d_%d)\n", ci++, n);
            }
            fprintf(out, has_default ? "        :(rb_def_%d)\n" : "        :(rb_end_%d)\n",
                    has_default ? n : n_end);
            ci = 0;
            for (RCase *c = s->cases; c; c = c->next) {
                if (c->is_default) continue;
                fprintf(out,"rb_c%d_%d\n", ci++, n);
                emit_stmt(c->body, out);
                fprintf(out,"        :(rb_end_%d)\n", n_end);
            }
            if (has_default) {
                fprintf(out,"rb_def_%d\n", n);
                for (RCase *c = s->cases; c; c = c->next)
                    if (c->is_default) { emit_stmt(c->body,out); break; }
            }
            fprintf(out,"rb_end_%d\n", n_end);
            break;
        }

        case RS_EXIT:
            fprintf(out, rb_loop_depth > 0
                ? "        :(rb_end_%d)\n" : "        :(END)\n",
                rb_loop_depth > 0 ? rb_loop_end[rb_loop_depth-1] : 0);
            break;

        case RS_NEXT:
            fprintf(out, rb_loop_depth > 0
                ? "        :(rb_top_%d)\n" : "        :(END)\n",
                rb_loop_depth > 0 ? rb_loop_top[rb_loop_depth-1] : 0);
            break;

        case RS_FAIL:   fprintf(out,"        :(FRETURN)\n"); break;
        case RS_STOP:   fprintf(out,"        END\n"); break;

        case RS_RETURN:
            if (s->retval) {
                fprintf(out,"        %s = ", rb_current_func); emit_expr(s->retval,out); fprintf(out,"\n");
                fprintf(out,"        :(RETURN)\n");
            } else {
                fprintf(out,"        :(RETURN)\n");
            }
            break;

        case RS_COMPOUND:
            for (int i = 0; i < s->nstmts; i++) emit_stmt(s->stmts[i],out);
            break;

        default:
            fprintf(out,"        * <stmt:%d>\n", s->kind); break;
    }
}

static void emit_stmt_list(RStmt *head, FILE *out) {
    for (RStmt *s = head; s; s = s->next) emit_stmt(s, out);
}

/* ================================================================
 * Declaration emitter
 * ================================================================ */

static void emit_record(RDecl *d, FILE *out) {
    fprintf(out,"        DATA('%s(", d->name ? d->name : "?");
    for (int i = 0; i < d->nfields; i++) {
        if (i) fprintf(out,",");
        fprintf(out,"%s", d->fields[i] ? d->fields[i] : "?");
    }
    fprintf(out,")')\n");
}

static void emit_function(RDecl *d, FILE *out) {
    const char *name = d->name ? d->name : "?";
    fprintf(out,"        DEFINE('%s(", name);
    int first = 1;
    for (int i = 0; i < d->nparams; i++) {
        if (!first) fprintf(out,",");
        fprintf(out,"%s", d->params[i] ? d->params[i] : "?");
        first = 0;
    }
    for (int i = 0; i < d->nlocals; i++) {
        if (!first) fprintf(out,",");
        fprintf(out,"%s", d->locals[i] ? d->locals[i] : "?");
        first = 0;
    }
    fprintf(out,")') :(%s_end)\n", name);
    fprintf(out,"%s\n", name);
    if (d->initial) {
        fprintf(out,"        IDENT(%s_RB_INIT_,'1') :S(%s_RB_BODY_)\n", name, name);
        fprintf(out,"        %s_RB_INIT_ = '1'\n", name);
        emit_stmt(d->initial, out);
        fprintf(out,"%s_RB_BODY_\n", name);
    }
    rb_current_func = name;
    emit_stmt_list(d->body, out);
    /* only emit fall-off return if body doesn't already end with return/fail */
    {
        RStmt *last = d->body;
        while (last && last->next) last = last->next;
        if (!last || (last->kind != RS_RETURN && last->kind != RS_FAIL))
            fprintf(out,"        :(RETURN)\n");
    }
    fprintf(out,"%s_end\n\n", name);
}

/* ================================================================
 * Top-level entry point
 * ================================================================ */
void rebus_emit(RProgram *prog, FILE *out) {
    if (!prog) return;
    rb_label = 0; rb_loop_depth = 0;
    fprintf(out,"*  Generated by rebus_emit.c — Rebus → SNOBOL4 (TR 84-9)\n*\n");
    for (RDecl *d = prog->decls; d; d = d->next) {
        if (d->kind == RD_RECORD) emit_record(d, out);
        else                       emit_function(d, out);
    }
    fprintf(out,"        MAIN()\n");
    fprintf(out,"END\n");
}
