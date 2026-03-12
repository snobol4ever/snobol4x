/*
 * emit.c — SNOBOL4→C emitter for snoc
 *
 * One Expr type.  emit_expr() and emit_pat() both walk Expr nodes,
 * but emit_pat() routes E_CALL to sno_pat_* and E_CONCAT to sno_pat_cat().
 *
 * Generated C uses the snobol4.c runtime API:
 *   SnoVal, sno_var_get/set, sno_apply, sno_concat, sno_add, …
 *   SnoPattern*, sno_pat_lit, sno_pat_len, sno_pat_cat, sno_match, …
 */

#include "snoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* Forward declarations */
static int is_io_name(const char *name);
static void emit_assign_target(Expr *lhs, const char *rhs_str);
static void emit_assign_target_io(Expr *lhs, const char *rhs_str);

static int cur_stmt_next_uid = 0;  /* set by snoc_emit before each emit_stmt */

static FILE *out;
static int   uid_ctr = 0;
static int   uid(void) { return ++uid_ctr; }

static void E(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); vfprintf(out,fmt,ap); va_end(ap);
}

/* C-safe version of a SNOBOL4 name */
static char csafe_buf[512];
static const char *cs(const char *s) {
    int i=0, j=0;
    csafe_buf[j++]='_';
    for (; s[i]&&j<510; i++) {
        unsigned char c=(unsigned char)s[i];
        if (isalnum(c)||c=='_') csafe_buf[j++]=(char)c;
        else csafe_buf[j++]='_';
    }
    csafe_buf[j]='\0';
    return csafe_buf;
}

static void emit_cstr(const char *s) {
    fputc('"',out);
    for (; *s; s++) {
        if (*s=='"'||*s=='\\') fputc('\\',out);
        else if (*s=='\n') { fputs("\\n",out); continue; }
        else if (*s=='\t') { fputs("\\t",out); continue; }
        fputc(*s,out);
    }
    fputc('"',out);
}

/* ============================================================
 * Value expression emission → SnoVal
 * ============================================================ */
static void emit_expr(Expr *e);

static void emit_expr(Expr *e) {
    if (!e) { E("SNO_NULL_VAL"); return; }
    switch (e->kind) {
    case E_NULL:    E("SNO_NULL_VAL"); break;
    case E_STR:     E("sno_str("); emit_cstr(e->sval); E(")"); break;
    case E_INT:     E("sno_int(%ld)", e->ival); break;
    case E_REAL:    E("sno_real(%g)", e->dval); break;
    case E_VAR:
        if (is_io_name(e->sval)) E("sno_var_get(\"%s\")", e->sval);
        else E("sno_get(%s)", cs(e->sval));
        break;
    case E_KEYWORD: E("sno_kw(\"%s\")", e->sval); break;

    case E_DEREF:
        if (!e->left) {
            /* $expr — indirect lookup */
            E("sno_deref("); emit_expr(e->right); E(")");
        } else if (e->left->kind == E_VAR) {
            /* *varname — deref in value context */
            E("sno_get(%s)", cs(e->left->sval));
        } else {
            /* *(expr) — deref of compound expression */
            E("sno_deref("); emit_expr(e->left); E(")");
        }
        break;

    case E_NEG: E("sno_neg("); emit_expr(e->right); E(")"); break;

    case E_CONCAT: E("sno_concat("); emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_ADD:    E("sno_add(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_SUB:    E("sno_sub(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_MUL:    E("sno_mul(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_DIV:    E("sno_div(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_POW:    E("sno_pow(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_ALT:    E("sno_alt(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;

    /* capture nodes — in value context, evaluate the child */
    case E_COND: emit_expr(e->left); break;
    case E_IMM:  emit_expr(e->left); break;

    case E_CALL:
        if (e->nargs == 0) {
            E("sno_apply(\"%s\",NULL,0)", e->sval);
        } else {
            E("sno_apply(\"%s\",(SnoVal[]){", e->sval);
            for (int i=0; i<e->nargs; i++) {
                if (i) E(","); emit_expr(e->args[i]);
            }
            E("},%d)", e->nargs);
        }
        break;

    case E_ARRAY:
        E("sno_aref(%s,(SnoVal[]){", cs(e->sval));
        for (int i=0; i<e->nargs; i++) {
            if (i) E(","); emit_expr(e->args[i]);
        }
        E("},%d)", e->nargs);
        break;

    case E_INDEX:
        /* postfix subscript: expr[i] — e.g. c(x)[i] */
        E("sno_index("); emit_expr(e->left); E(",(SnoVal[]){");
        for (int i=0; i<e->nargs; i++) {
            if (i) E(","); emit_expr(e->args[i]);
        }
        E("},%d)", e->nargs);
        break;

    case E_AT:
        /* @var — cursor position capture: evaluates to cursor int */
        E("sno_cursor_get(\"%s\")", e->sval);
        break;

    case E_ASSIGN:
        /* var = expr inside expression context */
        E("sno_assign_expr(%s,", cs(e->left->sval)); emit_expr(e->right); E(")");
        break;
    }
}

/* ============================================================
 * Pattern expression emission → SnoPattern*
 *
 * Same Expr nodes, different routing:
 *   E_CALL  → sno_pat_builtin or sno_pat_call
 *   E_CONCAT → sno_pat_cat
 *   E_ALT   → sno_pat_alt
 *   E_COND  → sno_pat_cond(child_pat, varname)
 *   E_IMM   → sno_pat_imm(child_pat, varname)
 *   E_DEREF → sno_pat_ref(varname)   (deferred pattern reference *X)
 *   E_STR   → sno_pat_lit(str)
 *   E_VAR   → sno_pat_var(varname)   (pattern variable)
 * ============================================================ */
static void emit_pat(Expr *e);

static void emit_pat(Expr *e) {
    if (!e) { E("sno_pat_epsilon()"); return; }
    switch (e->kind) {
    case E_STR:
        E("sno_pat_lit("); emit_cstr(e->sval); E(")"); break;

    case E_VAR:
        E("sno_pat_var(\"%s\")", e->sval); break;

    case E_DEREF:
        /* *X — deferred pattern reference */
        if (e->left && e->left->kind == E_VAR)
            E("sno_pat_ref(\"%s\")", e->left->sval);
        else {
            E("sno_pat_deref("); emit_expr(e->right ? e->right : e->left); E(")");
        }
        break;

    case E_CONCAT:
        E("sno_pat_cat("); emit_pat(e->left); E(","); emit_pat(e->right); E(")"); break;

    case E_ALT:
        E("sno_pat_alt("); emit_pat(e->left); E(","); emit_pat(e->right); E(")"); break;

    case E_COND: {
        /* pat . var */
        const char *varname = (e->right && e->right->kind==E_VAR)
                              ? e->right->sval : "?";
        E("sno_pat_cond("); emit_pat(e->left); E(",\"%s\")", varname); break;
    }
    case E_IMM: {
        /* pat $ var */
        const char *varname = (e->right && e->right->kind==E_VAR)
                              ? e->right->sval : "?";
        E("sno_pat_imm("); emit_pat(e->left); E(",\"%s\")", varname); break;
    }

    case E_CALL: {
        /* Route known builtins to sno_pat_* */
        const char *n = e->sval;
        #define B0(nm,fn) if(strcasecmp(n,nm)==0){E(fn"()");break;}
        #define B1(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(");emit_expr(e->args[0]);E(")");break;}
        B0("ARB","sno_pat_arb")  B0("REM","sno_pat_rem")
        B0("FAIL","sno_pat_fail") B0("ABORT","sno_pat_abort")
        B0("FENCE","sno_pat_fence") B0("SUCCEED","sno_pat_succeed")
        B0("BAL","sno_pat_bal")
        B1("LEN","sno_pat_len")   B1("POS","sno_pat_pos")
        B1("RPOS","sno_pat_rpos") B1("TAB","sno_pat_tab")
        B1("RTAB","sno_pat_rtab") B1("SPAN","sno_pat_span")
        B1("BREAK","sno_pat_break") B1("NOTANY","sno_pat_notany")
        B1("ANY","sno_pat_any")   B1("ARBNO","sno_pat_arbno")
        #undef B0
        #undef B1
        /* user-defined pattern function */
        E("sno_pat_call(\"%s\"", n);
        for (int i=0; i<e->nargs; i++) { E(","); emit_expr(e->args[i]); }
        E(")");
        break;
    }

    /* value nodes that shouldn't appear in pattern context — treat as var */
    case E_INDEX:
    case E_AT:
    case E_ASSIGN:
    default:
        E("sno_pat_val("); emit_expr(e); E(")"); break;
    }
}

/* ============================================================
 * Emit lvalue assignment target
 * ============================================================ */
static void emit_assign_target(Expr *lhs, const char *rhs_str) {
    if (!lhs) return;
    if (lhs->kind == E_VAR) {
        E("sno_set(%s, %s);\n", cs(lhs->sval), rhs_str);
    } else if (lhs->kind == E_ARRAY) {
        E("sno_aset(%s,(SnoVal[]){", cs(lhs->sval));
        for (int i=0; i<lhs->nargs; i++) {
            if (i) E(","); emit_expr(lhs->args[i]);
        }
        E("},%d,%s);\n", lhs->nargs, rhs_str);
    } else if (lhs->kind == E_KEYWORD) {
        E("sno_kw_set(\"%s\",%s);\n", lhs->sval, rhs_str);
    } else if (lhs->kind == E_DEREF) {
        E("sno_iset("); emit_expr(lhs->right); E(",%s);\n", rhs_str);
    } else {
        /* complex lvalue: evaluate and assign indirectly */
        E("sno_iset("); emit_expr(lhs); E(",%s);\n", rhs_str);
    }
}

/* ============================================================
 * Emit goto field
 * ============================================================ */
static void emit_goto(SnoGoto *g, const char *fn, int result_ok) {
    if (!g) { E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid); return; }
    if (g->uncond) {
        if      (strcasecmp(g->uncond,"RETURN") ==0) E("    goto _SNO_RETURN_%s;\n",fn);
        else if (strcasecmp(g->uncond,"FRETURN")==0) E("    goto _SNO_FRETURN_%s;\n",fn);
        else if (strcasecmp(g->uncond,"END")    ==0) E("    goto _SNO_END;\n");
        else E("    goto _L%s;\n", cs(g->uncond));
    } else {
        if (result_ok) {
            /* we have a runtime result variable _ok */
            if (g->onsuccess) E("    if(_ok) goto _L%s;\n", cs(g->onsuccess));
            if (g->onfailure) E("    if(!_ok) goto _L%s;\n", cs(g->onfailure));
        } else {
            /* no runtime result — success-only goto */
            if (g->onsuccess) E("    goto _L%s;\n", cs(g->onsuccess));
            if (g->onfailure) { /* can't reach failure — skip */ }
        }
        E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
    }
}

/* ============================================================
 * Emit one statement
 * ============================================================ */
static void emit_stmt(Stmt *s, const char *fn) {
    E("/* line %d */\n", s->lineno);
    if (s->label) E("_L%s:;\n", cs(s->label));

    /* label-only statement */
    if (!s->subject) {
        emit_goto(s->go, fn, 0);
        return;
    }

    /* ---- pure assignment: subject = replacement, no pattern ---- */
    if (!s->pattern && s->replacement) {
        char rhs[32]; int u=uid(); snprintf(rhs,sizeof rhs,"_v%d",u);
        E("{ SnoVal %s = ", rhs); emit_expr(s->replacement); E(";\n");
        emit_assign_target_io(s->subject, rhs);
        E("}\n");
        emit_goto(s->go, fn, 0);
        return;
    }

    /* ---- pattern match: subject pattern [= replacement] ---- */
    if (s->pattern) {
        int u = uid();
        E("/* match u%d */\n", u);
        E("SnoVal   _s%d = ", u); emit_expr(s->subject); E(";\n");
        E("SnoVal   _p%d = ", u); emit_pat(s->pattern); E(";\n");
        E("SnoMatch _m%d = sno_match(&_s%d, _p%d);\n", u,u,u);
        E("int      _ok%d = !_m%d.failed;\n", u, u);
        if (s->replacement) {
            E("if(_ok%d) {\n", u);
            E("    SnoVal _r%d = ", u); emit_expr(s->replacement); E(";\n");
            E("    sno_replace(&_s%d, &_m%d, _r%d);\n", u,u,u);
            /* write back to subject variable */
            if (s->subject->kind == E_VAR) {
                if (is_io_name(s->subject->sval))
                    E("    sno_var_set(\"%s\", _s%d);\n", s->subject->sval, u);
                else
                    E("    sno_set(%s, _s%d);\n", cs(s->subject->sval), u);
            }
            E("}\n");
        }
        /* emit goto using _ok%d */
        if (!s->go) { E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid); }
        else if (s->go->uncond) { emit_goto(s->go, fn, 0); }
        else {
            if (s->go->onsuccess) E("    if(_ok%d) goto _L%s;\n", u, cs(s->go->onsuccess));
            if (s->go->onfailure) E("    if(!_ok%d) goto _L%s;\n", u, cs(s->go->onfailure));
            E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
        return;
    }

    /* ---- expression evaluation only ---- */
    {
        int u=uid();
        E("SnoVal _v%d = ", u); emit_expr(s->subject); E(";\n");
        E("int _ok%d = !SNO_IS_FAIL(_v%d);\n", u, u);
        /* emit goto using _ok%d */
        if (!s->go) { E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid); }
        else if (s->go->uncond) { emit_goto(s->go, fn, 0); }
        else {
            if (s->go->onsuccess) E("    if(_ok%d) goto _L%s;\n", u, cs(s->go->onsuccess));
            if (s->go->onfailure) E("    if(!_ok%d) goto _L%s;\n", u, cs(s->go->onfailure));
            E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
    }
}

/* ============================================================
 * Symbol collection — walk entire AST, collect variable names
 * ============================================================ */

/* Simple hash set of variable names */
#define SYM_MAX 4096
static char *sym_table[SYM_MAX];
static int   sym_count = 0;

/* Names that should NOT become static locals — routed through sno_var_set/get */
static const char *io_names[] = {
    "OUTPUT","INPUT","PUNCH","TERMINAL","TRACE",NULL
};

static int is_io_name(const char *name) {
    for (int i=0; io_names[i]; i++)
        if (strcasecmp(name, io_names[i])==0) return 1;
    return 0;
}

static void sym_add(const char *name) {
    if (!name || !*name) return;
    if (is_io_name(name)) return;
    /* deduplicate */
    for (int i=0; i<sym_count; i++)
        if (strcmp(sym_table[i], name)==0) return;
    if (sym_count < SYM_MAX)
        sym_table[sym_count++] = strdup(name);
}

static void collect_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case E_VAR:   sym_add(e->sval); break;
    case E_ARRAY: sym_add(e->sval); break;  /* array variable itself */
    default: break;
    }
    collect_expr(e->left);
    collect_expr(e->right);
    for (int i=0; i<e->nargs; i++) collect_expr(e->args[i]);
}

static void collect_stmt(Stmt *s) {
    if (!s) return;
    collect_expr(s->subject);
    collect_expr(s->pattern);
    collect_expr(s->replacement);
}

static void collect_symbols(Program *prog) {
    sym_count = 0;
    for (Stmt *s = prog->head; s; s = s->next)
        collect_stmt(s);
}

/* ============================================================
 * OUTPUT/INPUT: route through sno_var_set/sno_var_get
 *
 * snoc emits: sno_set(_OUTPUT, val)
 * We want:    sno_var_set("OUTPUT", val)
 *
 * We override emit_assign_target for IO names, and override emit_expr
 * for E_VAR on IO names.
 * ============================================================ */

/* Wrapper: emit an lvalue assignment, routing IO names through var table */
static void emit_assign_target_io(Expr *lhs, const char *rhs_str) {
    if (lhs && lhs->kind == E_VAR && is_io_name(lhs->sval)) {
        E("sno_var_set(\"%s\", %s);\n", lhs->sval, rhs_str);
        return;
    }
    emit_assign_target(lhs, rhs_str);
}

/* Emit expr, substituting IO variable names with sno_var_get() calls */
/* We patch emit_expr's E_VAR case via a post-process: instead of patching
 * the existing emit_expr (which is static), we provide a wrapper that checks
 * for IO names before calling emit_expr for E_VAR nodes.
 * Since emit_expr is already defined as static void, we add a new emit_expr_top
 * that handles the IO routing at the top level. */

/* ============================================================
 * Program header and body
 * ============================================================ */

static void emit_header(void) {
    E("/* generated by snoc */\n");
    E("#include \"snoc_runtime.h\"\n\n");
}

static void emit_var_decls(void) {
    E("    /* --- SNOBOL4 variable declarations --- */\n");
    for (int i=0; i<sym_count; i++) {
        E("    static SnoVal %s = {0};\n", cs(sym_table[i]));
    }
    E("\n");
}

void snoc_emit(Program *prog, FILE *f) {
    out = f;

    /* Phase 1: collect all variable names */
    collect_symbols(prog);

    emit_header();

    E("int main(void) {\n");
    E("    sno_init();\n\n");

    /* Phase 2: emit variable declarations */
    emit_var_decls();

    /* Phase 3: emit statements */
    for (Stmt *s = prog->head; s; s = s->next) {
        cur_stmt_next_uid = uid();
        emit_stmt(s, "main");
        E("_SNO_NEXT_%d:;\n", cur_stmt_next_uid);
    }

    E("\n_SNO_END:;\n");
    E("    sno_finish();\n");
    E("    return 0;\n");
    E("}\n");
}
