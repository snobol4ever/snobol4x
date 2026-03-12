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

/* C-safe version of a SNOBOL4 name — always unique per call (heap-allocated) */
static char *cs_alloc(const char *s) {
    char base[512]; int j=0;
    base[j++]='_';
    for (int i=0; s[i]&&j<510; i++) {
        unsigned char c=(unsigned char)s[i];
        base[j++]=(isalnum(c)||c=='_')?(char)c:'_';
    }
    base[j]='\0';
    return strdup(base);
}

/* Label registry — per-function, reset at the start of each function body.
 * Detects collisions and appends disambiguation suffix.
 * Maps original SNOBOL4 label → unique C label within the current function. */
#define LREG_MAX 8192
typedef struct { char *orig; char *csafe; } LReg;
static LReg  lreg[LREG_MAX];
static int   lreg_count = 0;

static void lreg_reset(void) {
    for (int i=0; i<lreg_count; i++) { free(lreg[i].orig); free(lreg[i].csafe); }
    lreg_count = 0;
}

/* cs_label: return unique C label for SNOBOL4 label s within current function */
static const char *cs_label(const char *s) {
    /* Return previously registered entry */
    for (int i=0; i<lreg_count; i++)
        if (strcmp(lreg[i].orig, s)==0) return lreg[i].csafe;
    /* Compute base C name */
    char *base = cs_alloc(s);
    /* Find unique candidate (no collision with existing csafe entries) */
    char candidate[520];
    strcpy(candidate, base);
    int suffix=2, collision=1;
    while (collision) {
        collision=0;
        for (int i=0; i<lreg_count; i++)
            if (strcmp(lreg[i].csafe, candidate)==0) { collision=1; break; }
        if (collision) snprintf(candidate, sizeof candidate, "%s_%d", base, suffix++);
    }
    free(base);
    if (lreg_count < LREG_MAX) {
        lreg[lreg_count].orig  = strdup(s);
        lreg[lreg_count].csafe = strdup(candidate);
        lreg_count++;
    }
    return lreg[lreg_count-1].csafe;
}

/* cs: C-safe name for variables (not labels) — simple, no registry */
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

/* Current function being emitted (NULL = main) */
static const char *cur_fn_name = NULL;

/* Return 1 if label is defined within the body region of fn_name.
 * This is used to detect cross-function goto references. */
static int label_is_in_fn_body(const char *label, const char *fn_name);

/* Forward: current stmt next uid for fallthrough */
/* (cur_stmt_next_uid already declared above) */

/* ============================================================
 * Emit goto field
 * ============================================================ */

/* Emit a single branch target — handles RETURN/FRETURN/NRETURN/END specially.
 * Also handles:
 *   "error"     -> FRETURN (beauty.sno idiom: :F(error) means fail the function)
 *   "_COMPUTED" -> computed-goto stub (just fall through)
 *   Cross-fn labels -> fallthrough (label is in a different function's body)
 * All SNOBOL4 label -> C label mappings go through cs_label() for uniqueness.
 */
static void emit_goto_target(const char *label, const char *fn) {
    int in_main = !fn || strcasecmp(fn, "main") == 0;
    if      (strcasecmp(label,"RETURN") ==0) {
        if (in_main) { E("goto _SNO_END"); return; }
        E("goto _SNO_RETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"FRETURN")==0 || strcasecmp(label,"NRETURN")==0) {
        if (in_main) { E("goto _SNO_END"); return; }
        E("goto _SNO_FRETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"END")    ==0) {
        if (!in_main) { E("goto _SNO_FRETURN_%s", fn); return; }
        E("goto _SNO_END"); return;
    }
    else if (strcasecmp(label,"error")  ==0) {
        if (in_main) { E("goto _SNO_END"); return; }
        E("goto _SNO_FRETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"$COMPUTED")==0 || strcasecmp(label,"_COMPUTED")==0) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    /* Cross-scope goto detection:
     * 1. Label is in a different function's body → fallthrough.
     * 2. Label is in main scope (not in any fn body) but we're inside a fn → fallthrough.
     *    (main-scope labels are C labels in main(); unreachable from a C function.) */
    if (label_is_in_fn_body(label, NULL) && !label_is_in_fn_body(label, fn)) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    if (!in_main && !label_is_in_fn_body(label, fn)) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    E("goto _L%s", cs_label(label));
}

static void emit_goto(SnoGoto *g, const char *fn, int result_ok) {
    if (!g) { E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid); return; }
    if (g->uncond) {
        E("    "); emit_goto_target(g->uncond, fn); E(";\n");
    } else {
        if (result_ok) {
            if (g->onsuccess) { E("    if(_ok) "); emit_goto_target(g->onsuccess, fn); E(";\n"); }
            if (g->onfailure) { E("    if(!_ok) "); emit_goto_target(g->onfailure, fn); E(";\n"); }
        } else {
            if (g->onsuccess) { E("    "); emit_goto_target(g->onsuccess, fn); E(";\n"); }
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
    if (s->label) E("_L%s:;\n", cs_label(s->label));

    /* label-only statement */
    if (!s->subject) {
        emit_goto(s->go, fn, 0);
        return;
    }

    /* ---- pure assignment: subject = replacement, no pattern ---- */
    if (!s->pattern && s->replacement) {
        int u=uid();
        E("SnoVal _v%d = ", u); emit_expr(s->replacement); E(";\n");
        E("int _ok%d = !SNO_IS_FAIL(_v%d);\n", u, u);
        E("if(_ok%d) {\n", u);
        char rhs[32]; snprintf(rhs,sizeof rhs,"_v%d",u);
        emit_assign_target_io(s->subject, rhs);
        E("}\n");
        /* emit goto using _ok%d for conditional :S/:F branches */
        if (!s->go) { E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid); }
        else if (s->go->uncond) { emit_goto(s->go, fn, 0); }
        else {
            if (s->go->onsuccess) { E("    if(_ok%d) ", u); emit_goto_target(s->go->onsuccess, fn); E(";\n"); }
            if (s->go->onfailure) { E("    if(!_ok%d) ", u); emit_goto_target(s->go->onfailure, fn); E(";\n"); }
            E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
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
            if (s->go->onsuccess) { E("    if(_ok%d) ", u); emit_goto_target(s->go->onsuccess, fn); E(";\n"); }
            if (s->go->onfailure) { E("    if(!_ok%d) ", u); emit_goto_target(s->go->onfailure, fn); E(";\n"); }
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
            if (s->go->onsuccess) { E("    if(_ok%d) ", u); emit_goto_target(s->go->onsuccess, fn); E(";\n"); }
            if (s->go->onfailure) { E("    if(!_ok%d) ", u); emit_goto_target(s->go->onfailure, fn); E(";\n"); }
            E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
    }
}

/* ============================================================
 * Symbol collection — walk entire AST, collect variable names
 * ============================================================ */

#define SYM_MAX 4096
static char *sym_table[SYM_MAX];
static int   sym_count = 0;

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
    for (int i=0; i<sym_count; i++)
        if (strcmp(sym_table[i], name)==0) return;
    if (sym_count < SYM_MAX)
        sym_table[sym_count++] = strdup(name);
}

static void collect_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case E_VAR:   sym_add(e->sval); break;
    case E_ARRAY: sym_add(e->sval); break;
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
 * IO assignment routing
 * ============================================================ */

static void emit_assign_target_io(Expr *lhs, const char *rhs_str) {
    if (lhs && lhs->kind == E_VAR && is_io_name(lhs->sval)) {
        E("sno_var_set(\"%s\", %s);\n", lhs->sval, rhs_str);
        return;
    }
    emit_assign_target(lhs, rhs_str);
}

/* ============================================================
 * DEFINE function table
 *
 * Pre-pass: scan for DEFINE('fn(a,b)loc1,loc2') calls.
 * Parse the proto string → name, args[], locals[].
 * The SNOBOL4 label matching fn_name marks the start of the body.
 * ============================================================ */

#define FN_MAX  256
#define ARG_MAX  32

#define BODY_MAX 8

typedef struct {
    char  *name;               /* function name, e.g. "pp" */
    char  *args[ARG_MAX];      /* parameter names */
    int    nargs;
    char  *locals[ARG_MAX];    /* local variable names */
    int    nlocals;
    Stmt  *body_starts[BODY_MAX]; /* ALL entry points for this function */
    int    nbody_starts;
    Stmt  *define_stmt;        /* the last DEFINE(...) statement */
    char  *end_label;          /* label that ends the body (from DEFINE's goto) */
} FnDef;

static FnDef fn_table[FN_MAX];
static int   fn_count = 0;

/* Return fn index if label matches a known function entry, else -1 */
static int fn_by_label(const char *label) {
    if (!label) return -1;
    for (int i=0; i<fn_count; i++)
        if (strcasecmp(fn_table[i].name, label)==0) return i;
    return -1;
}

/* Global boundary-label set: every function entry label AND every function
 * end label.  A function body stops when it hits ANY boundary label that is
 * not its own entry label. Built once after collect_functions() completes. */
#define BOUNDARY_MAX (FN_MAX * 2 + 8)
static char *boundary_labels[BOUNDARY_MAX];
static int   boundary_count = 0;

static void boundary_add(const char *lbl) {
    if (!lbl || !*lbl) return;
    for (int i = 0; i < boundary_count; i++)
        if (strcasecmp(boundary_labels[i], lbl) == 0) return;
    if (boundary_count < BOUNDARY_MAX)
        boundary_labels[boundary_count++] = strdup(lbl);
}

static int is_boundary_label(const char *lbl) {
    if (!lbl) return 0;
    for (int i = 0; i < boundary_count; i++)
        if (strcasecmp(boundary_labels[i], lbl) == 0) return 1;
    return 0;
}

static void build_boundary_labels(void) {
    boundary_count = 0;
    for (int i = 0; i < fn_count; i++) {
        boundary_add(fn_table[i].name);       /* entry label */
        boundary_add(fn_table[i].end_label);  /* end label (may be NULL) */
    }
}

/* Return fn index if stmt is the DEFINE(...) call for it, else -1 */
static int fn_by_define_stmt(Stmt *s) {
    for (int i=0; i<fn_count; i++)
        if (fn_table[i].define_stmt == s) return i;
    return -1;
}

/* Parse "fn(a,b)loc1,loc2" into a FnDef */
static void parse_proto(const char *proto, FnDef *fn) {
    /* fn name: up to '(' or end */
    int i=0;
    char buf[256];
    int j=0;
    while (proto[i] && proto[i]!='(' && proto[i]!=')') buf[j++]=proto[i++];
    buf[j]='\0';
    /* trim trailing spaces */
    while (j>0 && buf[j-1]==' ') buf[--j]='\0';
    fn->name = strdup(buf);
    fn->nargs = 0;
    fn->nlocals = 0;

    if (proto[i]=='(') {
        i++; /* skip ( */
        while (proto[i] && proto[i]!=')') {
            j=0;
            while (proto[i] && proto[i]!=',' && proto[i]!=')') buf[j++]=proto[i++];
            buf[j]='\0';
            /* trim */
            while (j>0&&(buf[j-1]==' '||buf[j-1]=='\t')) buf[--j]='\0';
            int k=0; while(buf[k]==' '||buf[k]=='\t') k++;
            if (buf[k] && fn->nargs < ARG_MAX)
                fn->args[fn->nargs++] = strdup(buf+k);
            if (proto[i]==',') i++;
        }
        if (proto[i]==')') i++;
    }

    /* locals after ')': comma-separated */
    while (proto[i]) {
        j=0;
        while (proto[i] && proto[i]!=',') buf[j++]=proto[i++];
        buf[j]='\0';
        int k=0; while(buf[k]==' '||buf[k]=='\t') k++;
        /* trim trailing */
        while (j>0&&(buf[j-1]==' '||buf[j-1]=='\t')) buf[--j]='\0';
        if (buf[k] && fn->nlocals < ARG_MAX)
            fn->locals[fn->nlocals++] = strdup(buf+k);
        if (proto[i]==',') i++;
    }
}

/* Flatten a string-literal expression (E_STR or E_CONCAT chain of E_STR)
 * into a single static buffer.  Returns NULL if any node is not a string. */
static char _define_proto_buf[4096];
static const char *flatten_str_expr(Expr *e) {
    if (!e) return NULL;
    if (e->kind == E_STR) return e->sval;
    if (e->kind == E_CONCAT) {
        const char *l = flatten_str_expr(e->left);
        const char *r = flatten_str_expr(e->right);
        if (!l || !r) return NULL;
        snprintf(_define_proto_buf, sizeof _define_proto_buf, "%s%s", l, r);
        return _define_proto_buf;
    }
    return NULL;
}

/* Check if a statement is DEFINE('proto') or DEFINE('proto' 'locals' ...)
 * The first argument may be a chain of concatenated string literals. */
static const char *stmt_define_proto(Stmt *s) {
    if (!s->subject) return NULL;
    Expr *e = s->subject;
    if (e->kind != E_CALL) return NULL;
    if (strcasecmp(e->sval,"DEFINE")!=0) return NULL;
    if (e->nargs < 1) return NULL;
    return flatten_str_expr(e->args[0]);
}

static void collect_functions(Program *prog) {
    fn_count = 0;
    for (Stmt *s = prog->head; s; s = s->next) {
        const char *proto = stmt_define_proto(s);
        if (!proto) continue;
        if (fn_count >= FN_MAX) break;
        FnDef *fn = &fn_table[fn_count];
        memset(fn, 0, sizeof *fn);
        parse_proto(proto, fn);
        fn->define_stmt = s;
        /* Extract end-of-body label from DEFINE's goto.
         * Two forms:
         *   1. DEFINE('fn()')  :(FnEnd)   -- goto on same statement
         *   2. DEFINE('fn()')             -- goto on the NEXT standalone statement
         *      :(FnEnd)
         */
        fn->end_label = NULL;
        if (s->go) {
            if (s->go->uncond)   fn->end_label = strdup(s->go->uncond);
            else if (s->go->onsuccess) fn->end_label = strdup(s->go->onsuccess);
        }
        if (!fn->end_label && s->next) {
            Stmt *nxt = s->next;
            /* A standalone goto: no label being defined here, no subject, just a goto */
            if (!nxt->subject && !nxt->pattern && !nxt->replacement
                    && nxt->go && nxt->go->uncond) {
                fn->end_label = strdup(nxt->go->uncond);
            }
        }
        /* Deduplicate: if this name already exists, overwrite it */
        int found = -1;
        for (int i=0; i<fn_count; i++)
            if (strcasecmp(fn_table[i].name, fn->name)==0) { found=i; break; }
        if (found >= 0) {
            /* Free old name/args/locals, replace with new definition */
            fn_table[found] = *fn;
        } else {
            fn_count++;
        }
    }
    /* Second pass: find ALL body_starts for each function */
    for (int i=0; i<fn_count; i++) {
        fn_table[i].nbody_starts = 0;
        for (Stmt *s = prog->head; s; s = s->next) {
            if (s->label && strcasecmp(s->label, fn_table[i].name)==0) {
                if (fn_table[i].nbody_starts < BODY_MAX)
                    fn_table[i].body_starts[fn_table[i].nbody_starts++] = s;
            }
        }
    }
}

/* ============================================================
 * label_is_in_fn_body: return 1 if 'label' (a SNOBOL4 label) appears
 * within any body region of function fn_name.
 * If fn_name is NULL, return 1 if label appears in ANY function's body.
 * ============================================================ */
/* ============================================================
 * Body boundary rule:
 *   A function body stops when it hits a statement whose label is:
 *   (a) another DEFINE'd function's entry label, OR
 *   (b) a known end_label of any function (the closing marker).
 *   Plain internal labels (io1, assign2, etc.) do NOT stop the body.
 * ============================================================ */

static int is_body_boundary(const char *label, const char *cur_fn) {
    if (!label) return 0;
    for (int i = 0; i < fn_count; i++) {
        if (strcasecmp(fn_table[i].name, label) == 0 &&
            strcasecmp(fn_table[i].name, cur_fn) != 0) return 1;
        if (fn_table[i].end_label &&
            strcasecmp(fn_table[i].end_label, label) == 0) return 1;
    }
    return 0;
}

/* Return 1 if stmt s is inside the body of fn_name (NULL = any fn) */
static int stmt_in_fn_body(Stmt *s, const char *fn_name) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_name && strcasecmp(fn_table[i].name, fn_name) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            Stmt *bs = fn_table[i].body_starts[b];
            for (Stmt *t = bs; t; t = t->next) {
                if (t->is_end) break;
                if (t != bs && is_body_boundary(t->label, fn_table[i].name)) break;
                if (t == s) return 1;
            }
        }
    }
    return 0;
}

/* label_is_in_fn_body: used by emit_goto_target to detect cross-fn gotos */
static int label_is_in_fn_body(const char *label, const char *fn_name) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_name && strcasecmp(fn_table[i].name, fn_name) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            Stmt *bs = fn_table[i].body_starts[b];
            for (Stmt *t = bs; t; t = t->next) {
                if (t->is_end) break;
                if (t != bs && is_body_boundary(t->label, fn_table[i].name)) break;
                if (t->label && strcasecmp(t->label, label) == 0) return 1;
            }
        }
    }
    return 0;
}

/* ============================================================
 * Emit header
 * ============================================================ */
static void emit_header(void) {
    E("/* generated by snoc */\n");
    E("#include \"snoc_runtime.h\"\n\n");
}

/* ============================================================
 * Emit global variable declarations
 * ============================================================ */
static void emit_global_var_decls(void) {
    E("/* --- global SNOBOL4 variables --- */\n");
    for (int i = 0; i < sym_count; i++)
        E("static SnoVal %s = {0};\n", cs(sym_table[i]));
    E("\n");
}

/* ============================================================
 * Emit one DEFINE'd function
 * ============================================================ */
static void emit_fn(FnDef *fn, Program *prog) {
    (void)prog;
    lreg_reset();
    cur_fn_name = fn->name;
    E("static SnoVal _sno_fn_%s(SnoVal *_args, int _nargs) {\n", fn->name);

    E("    jmp_buf _fn_abort_jmp;\n");
    E("    if (setjmp(_fn_abort_jmp) != 0) return SNO_FAIL_VAL;\n");
    E("    sno_push_abort_handler(&_fn_abort_jmp);\n\n");

    /* Return-value variable — skip if an arg has the same name */
    {
        int clash = 0;
        for (int i = 0; i < fn->nargs; i++)
            if (strcasecmp(fn->args[i], fn->name) == 0) { clash = 1; break; }
        if (!clash)
            E("    SnoVal %s = {0}; /* return value */\n", cs(fn->name));
    }
    for (int i = 0; i < fn->nargs; i++)
        E("    SnoVal %s = (_nargs>%d)?_args[%d]:SNO_NULL_VAL;\n",
          cs(fn->args[i]), i, i);
    for (int i = 0; i < fn->nlocals; i++)
        E("    SnoVal %s = {0};\n", cs(fn->locals[i]));
    E("\n");

    if (fn->nbody_starts == 0) {
        E("    goto _SNO_RETURN_%s;\n", fn->name);
    } else {
        Stmt *bs = fn->body_starts[fn->nbody_starts - 1]; /* last DEFINE wins */
        for (Stmt *s = bs; s; s = s->next) {
            if (s->is_end) break;
            if (s != bs && is_body_boundary(s->label, fn->name)) break;
            cur_stmt_next_uid = uid();
            emit_stmt(s, fn->name);
            E("_SNO_NEXT_%d:;\n", cur_stmt_next_uid);
        }
        E("    goto _SNO_RETURN_%s;\n", fn->name);
    }

    E("\n_SNO_RETURN_%s:\n", fn->name);
    E("    sno_pop_abort_handler();\n");
    E("    return sno_get(%s);\n", cs(fn->name));
    E("_SNO_FRETURN_%s:\n", fn->name);
    E("    sno_pop_abort_handler();\n");
    E("    return SNO_FAIL_VAL;\n");
    E("}\n\n");
}

/* ============================================================
 * Emit forward declarations for all functions
 * ============================================================ */
static void emit_fn_forwards(void) {
    for (int i = 0; i < fn_count; i++)
        E("static SnoVal _sno_fn_%s(SnoVal *_args, int _nargs);\n",
          fn_table[i].name);
    E("\n");
}

/* ============================================================
 * Main entry point emitter
 * ============================================================ */
static int stmt_is_in_any_fn_body(Stmt *s) {
    return stmt_in_fn_body(s, NULL);
}

static void emit_main(Program *prog) {
    lreg_reset();
    cur_fn_name = "main";
    E("int main(void) {\n");
    E("    sno_init();\n\n");

    /* Register all DEFINE'd functions */
    for (int i=0; i<fn_count; i++) {
        /* Reconstruct the proto spec string: "name(a,b)loc1,loc2" */
        E("    sno_define(\"");
        E("%s(", fn_table[i].name);
        for (int j=0; j<fn_table[i].nargs; j++) {
            if (j) E(",");
            E("%s", fn_table[i].args[j]);
        }
        E(")");
        for (int j=0; j<fn_table[i].nlocals; j++) {
            if (j) E(","); else E("");
            E("%s", fn_table[i].locals[j]);
        }
        E("\", _sno_fn_%s);\n", fn_table[i].name);
    }
    E("\n");

    /* Emit main-level statements only */
    for (Stmt *s = prog->head; s; s = s->next) {
        /* END stmt — emit the end label and stop */
        if (s->is_end) {
            E("\n_SNO_END:;\n");
            E("    sno_finish();\n");
            E("    return 0;\n");
            E("}\n");
            return;
        }
        /* Skip statements that live inside a function body */
        if (stmt_is_in_any_fn_body(s)) continue;
        /* Skip the DEFINE(...) call statements themselves */
        if (stmt_define_proto(s)) continue;

        cur_stmt_next_uid = uid();
        emit_stmt(s, "main");
        E("_SNO_NEXT_%d:;\n", cur_stmt_next_uid);
    }

    /* Fallback if no END stmt found */
}

/* ============================================================
 * Public entry point
 * ============================================================ */
void snoc_emit(Program *prog, FILE *f) {
    out = f;

    /* Phase 1: collect variable names and function definitions */
    collect_symbols(prog);
    collect_functions(prog);

    /* Phase 2: emit */    emit_header();
    emit_global_var_decls();
    emit_fn_forwards();

    for (int i=0; i<fn_count; i++)
        emit_fn(&fn_table[i], prog);

    emit_main(prog);
}
