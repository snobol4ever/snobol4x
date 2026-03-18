/*
 * rebus_print.c  —  AST pretty-printer for Rebus
 *
 * Walks the RProgram AST and writes a human-readable reconstruction
 * to the given FILE*.  Used for smoke-testing the parser: parse a
 * Rebus program, print the AST, verify it matches the source structure.
 *
 * This is NOT a code generator.  It prints reconstructed Rebus source,
 * not SNOBOL4 output.
 */

#include "rebus.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Indentation helpers
 * ================================================================ */
static void indent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) fprintf(out, "  ");
}

/* ================================================================
 * Expression printer
 * ================================================================ */
static void print_expr(RExpr *e, FILE *out);

static const char *op_str(REKind k) {
    switch (k) {
        case RE_ADD:       return "+";
        case RE_SUB:       return "-";
        case RE_MUL:       return "*";
        case RE_DIV:       return "/";
        case RE_MOD:       return "%";
        case RE_POW:       return "^";
        case RE_STRCAT:    return "||";
        case RE_PATCAT:    return "&";
        case RE_ALT:       return "|";
        case RE_EQ:        return "=";
        case RE_NE:        return "~=";
        case RE_LT:        return "<";
        case RE_LE:        return "<=";
        case RE_GT:        return ">";
        case RE_GE:        return ">=";
        case RE_SEQ:       return "==";
        case RE_SNE:       return "~==";
        case RE_SLT:       return "<<";
        case RE_SLE:       return "<<=";
        case RE_SGT:       return ">>";
        case RE_SGE:       return ">>=";
        case RE_ASSIGN:    return ":=";
        case RE_EXCHANGE:  return ":=:";
        case RE_ADDASSIGN: return "+:=";
        case RE_SUBASSIGN: return "-:=";
        case RE_CATASSIGN: return "||:=";
        case RE_COND:      return ".";
        case RE_IMM:       return "$";
        default:           return "??";
    }
}

static void print_expr(RExpr *e, FILE *out) {
    if (!e) { fprintf(out, "<null>"); return; }
    switch (e->kind) {
        case RE_NULL:    fprintf(out, "\"\""); break;
        case RE_STR:     fprintf(out, "\"%s\"", e->sval ? e->sval : ""); break;
        case RE_INT:     fprintf(out, "%ld", e->ival); break;
        case RE_REAL:    fprintf(out, "%g", e->dval); break;
        case RE_VAR:     fprintf(out, "%s", e->sval ? e->sval : "?"); break;
        case RE_KEYWORD: fprintf(out, "&%s", e->sval ? e->sval : ""); break;

        case RE_NEG:
            fprintf(out, "-("); print_expr(e->right, out); fprintf(out, ")");
            break;
        case RE_POS:
            fprintf(out, "+("); print_expr(e->right, out); fprintf(out, ")");
            break;
        case RE_NOT:
            fprintf(out, "\\("); print_expr(e->right, out); fprintf(out, ")");
            break;
        case RE_VALUE:
            fprintf(out, "/("); print_expr(e->right, out); fprintf(out, ")");
            break;
        case RE_BANG:
            fprintf(out, "!("); print_expr(e->right, out); fprintf(out, ")");
            break;
        case RE_DEREF:
            fprintf(out, "$("); print_expr(e->right, out); fprintf(out, ")");
            break;
        case RE_CURSOR:
            fprintf(out, "@%s", e->sval ? e->sval : "?");
            break;
        case RE_PATOPT:
            fprintf(out, "~("); print_expr(e->right, out); fprintf(out, ")");
            break;

        /* Binary operators */
        case RE_ADD: case RE_SUB: case RE_MUL: case RE_DIV: case RE_MOD:
        case RE_POW: case RE_STRCAT: case RE_PATCAT: case RE_ALT:
        case RE_EQ:  case RE_NE:  case RE_LT: case RE_LE: case RE_GT: case RE_GE:
        case RE_SEQ: case RE_SNE: case RE_SLT: case RE_SLE: case RE_SGT: case RE_SGE:
        case RE_ASSIGN: case RE_EXCHANGE: case RE_ADDASSIGN:
        case RE_SUBASSIGN: case RE_CATASSIGN:
            fprintf(out, "(");
            print_expr(e->left, out);
            fprintf(out, " %s ", op_str(e->kind));
            print_expr(e->right, out);
            fprintf(out, ")");
            break;

        /* Pattern captures */
        case RE_COND:
            print_expr(e->left, out);
            fprintf(out, " . ");
            print_expr(e->right, out);
            break;
        case RE_IMM:
            print_expr(e->left, out);
            fprintf(out, " $ ");
            print_expr(e->right, out);
            break;

        case RE_RANGE:
            print_expr(e->left, out);
            fprintf(out, " +: ");
            print_expr(e->right, out);
            break;

        case RE_CALL:
            if (e->sval) fprintf(out, "%s", e->sval);
            else if (e->left) { fprintf(out, "("); print_expr(e->left, out); fprintf(out, ")"); }
            fprintf(out, "(");
            for (int i = 0; i < e->nargs; i++) {
                if (i) fprintf(out, ", ");
                if (e->args[i]) print_expr(e->args[i], out);
            }
            fprintf(out, ")");
            break;

        case RE_SUB_IDX:
            print_expr(e->left, out);
            fprintf(out, "[");
            for (int i = 0; i < e->nargs; i++) {
                if (i) fprintf(out, ", ");
                if (e->args[i]) print_expr(e->args[i], out);
            }
            fprintf(out, "]");
            break;

        default:
            fprintf(out, "<expr:%d>", e->kind);
            break;
    }
}

/* ================================================================
 * Statement printer
 * ================================================================ */
static void print_stmt(RStmt *s, FILE *out, int depth);

static void print_stmt_list(RStmt *head, FILE *out, int depth) {
    for (RStmt *s = head; s; s = s->next)
        print_stmt(s, out, depth);
}

static void print_stmt(RStmt *s, FILE *out, int depth) {
    if (!s) return;
    indent(out, depth);
    switch (s->kind) {

        case RS_EXPR:
            print_expr(s->expr, out);
            fprintf(out, "\n");
            break;

        case RS_MATCH:
            print_expr(s->expr, out);
            fprintf(out, " ? ");
            print_expr(s->pat, out);
            fprintf(out, "\n");
            break;

        case RS_REPLACE:
            print_expr(s->expr, out);
            fprintf(out, " ? ");
            print_expr(s->pat, out);
            fprintf(out, " <- ");
            print_expr(s->repl, out);
            fprintf(out, "\n");
            break;

        case RS_REPLN:
            print_expr(s->expr, out);
            fprintf(out, " ?- ");
            print_expr(s->pat, out);
            fprintf(out, "\n");
            break;

        case RS_IF:
            fprintf(out, "if ");
            /* s->body holds the condition-stmt, s->alt holds the then-body */
            print_stmt(s->body, out, 0);
            indent(out, depth);
            fprintf(out, "then\n");
            print_stmt(s->alt, out, depth + 1);
            if (s->repl) {
                indent(out, depth);
                fprintf(out, "else\n");
                print_stmt((RStmt*)s->repl, out, depth + 1);
            }
            break;

        case RS_UNLESS:
            fprintf(out, "unless ");
            print_stmt(s->body, out, 0);
            indent(out, depth);
            fprintf(out, "then\n");
            print_stmt(s->alt, out, depth + 1);
            break;

        case RS_WHILE:
            fprintf(out, "while ");
            print_stmt(s->body, out, 0);   /* condition */
            indent(out, depth);
            fprintf(out, "do\n");
            print_stmt(s->alt, out, depth + 1);
            break;

        case RS_UNTIL:
            fprintf(out, "until ");
            print_stmt(s->body, out, 0);
            indent(out, depth);
            fprintf(out, "do\n");
            print_stmt(s->alt, out, depth + 1);
            break;

        case RS_REPEAT:
            fprintf(out, "repeat\n");
            print_stmt(s->alt, out, depth + 1);
            break;

        case RS_FOR:
            fprintf(out, "for %s from ", s->for_var ? s->for_var : "?");
            print_expr(s->for_from, out);
            fprintf(out, " to ");
            print_expr(s->for_to, out);
            if (s->for_by) {
                fprintf(out, " by ");
                print_expr(s->for_by, out);
            }
            fprintf(out, " do\n");
            print_stmt(s->alt, out, depth + 1);
            break;

        case RS_CASE:
            fprintf(out, "case ");
            print_expr(s->case_expr, out);
            fprintf(out, " of {\n");
            for (RCase *c = s->cases; c; c = c->next) {
                indent(out, depth + 1);
                if (c->is_default) {
                    fprintf(out, "default");
                } else {
                    print_expr(c->guard, out);
                }
                fprintf(out, " :\n");
                print_stmt(c->body, out, depth + 2);
            }
            indent(out, depth);
            fprintf(out, "}\n");
            break;

        case RS_EXIT:   fprintf(out, "exit\n");   break;
        case RS_NEXT:   fprintf(out, "next\n");   break;
        case RS_FAIL:   fprintf(out, "fail\n");   break;
        case RS_STOP:   fprintf(out, "stop\n");   break;

        case RS_RETURN:
            fprintf(out, "return");
            if (s->retval) { fprintf(out, " "); print_expr(s->retval, out); }
            fprintf(out, "\n");
            break;

        case RS_COMPOUND:
            fprintf(out, "{\n");
            for (int i = 0; i < s->nstmts; i++)
                print_stmt(s->stmts[i], out, depth + 1);
            indent(out, depth);
            fprintf(out, "}\n");
            break;

        default:
            fprintf(out, "<stmt:%d>\n", s->kind);
            break;
    }
}

/* ================================================================
 * Declaration printer
 * ================================================================ */
static void print_decl(RDecl *d, FILE *out) {
    if (!d) return;
    switch (d->kind) {

        case RD_RECORD:
            fprintf(out, "record %s(", d->name ? d->name : "?");
            for (int i = 0; i < d->nfields; i++) {
                if (i) fprintf(out, ", ");
                fprintf(out, "%s", d->fields[i] ? d->fields[i] : "?");
            }
            fprintf(out, ")\n\n");
            break;

        case RD_FUNCTION:
            fprintf(out, "function %s(", d->name ? d->name : "?");
            for (int i = 0; i < d->nparams; i++) {
                if (i) fprintf(out, ", ");
                fprintf(out, "%s", d->params[i] ? d->params[i] : "?");
            }
            fprintf(out, ")");
            if (d->nlocals > 0) {
                fprintf(out, "\n  local ");
                for (int i = 0; i < d->nlocals; i++) {
                    if (i) fprintf(out, ", ");
                    fprintf(out, "%s", d->locals[i] ? d->locals[i] : "?");
                }
            }
            if (d->initial) {
                fprintf(out, "\n  initial ");
                print_stmt(d->initial, out, 0);
            }
            fprintf(out, "\n");
            print_stmt_list(d->body, out, 1);
            fprintf(out, "end\n\n");
            break;
    }
}

/* ================================================================
 * Top-level entry
 * ================================================================ */
void rebus_print(RProgram *prog, FILE *out) {
    if (!prog) { fprintf(out, "<null program>\n"); return; }
    for (RDecl *d = prog->decls; d; d = d->next)
        print_decl(d, out);
}
