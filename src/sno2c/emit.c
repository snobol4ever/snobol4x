/*
 * emit.c — SNOBOL4→C emitter for sno2c
 *
 * One Expr type.  emit_expr() and emit_pat() both walk Expr nodes,
 * but emit_pat() routes E_CALL to pat_* and E_CONCAT to pat_cat().
 *
 * Generated C uses the snobol4.c runtime API:
 *   SnoVal, var_get/set, aply, ccat, add, …
 *   SnoPattern*, pat_lit, pat_len, pat_cat, mtch, …
 */

#include "sno2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* Forward declarations */
static int is_io_name(const char *name);
static int is_defined_function(const char *name);
static void emit_assign_target(Expr *lhs, const char *rhs_str);
static void emit_assign_target_io(Expr *lhs, const char *rhs_str);

static int cur_stmt_next_uid = 0;  /* set by snoc_emit before each emit_stmt */

/* ---- Trampoline mode (sprint stmt-fn) ----
 * When trampoline_mode=1, emit_goto_target emits "return block_X" instead of
 * "goto _L_X", and each stmt is wrapped in its own static void* stmt_N(void).
 * Controlled by -trampoline flag on the sno2c command line.
 */
int trampoline_mode = 0;         /* set by main.c when -trampoline passed */
static int tramp_stmt_id = 0;    /* sequential stmt ID within current scope */

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
static void emit_pat(Expr *e);
static int  expr_contains_pattern(Expr *e);

static void emit_expr(Expr *e) {
    if (!e) { E("NULL_VAL"); return; }
    switch (e->kind) {
    case E_NULL:    E("NULL_VAL"); break;
    case E_STR:     E("strv("); emit_cstr(e->sval); E(")"); break;
    case E_INT:     E("vint(%ld)", e->ival); break;
    case E_REAL:    E("real(%g)", e->dval); break;
    case E_VAR:
        if (is_io_name(e->sval)) E("var_get(\"%s\")", e->sval);
        else E("get(%s)", cs(e->sval));
        break;
    case E_KEYWORD: E("kw(\"%s\")", e->sval); break;

    case E_DEREF:
        if (!e->left) {
            /* $expr — indirect lookup */
            E("deref("); emit_expr(e->right); E(")");
        } else if (e->left->kind == E_VAR) {
            /* *varname — deferred pattern reference (resolved at mtch time) */
            E("var_as_pattern(pat_ref(\"%s\"))", e->left->sval);
        } else if (e->left->kind == E_CALL && e->left->nargs >= 1
                   && !is_defined_function(e->left->sval)) {

            /* *varname(arg...) — parser misparse: *varname concatenated with (arg).
             * SNOBOL4 continuation lines cause the parser to greedily consume the
             * next '(' as a function-call argument to varname.  The correct
             * semantics are: deferred-ref(*varname) cat arg. */
            E("concat_sv(var_as_pattern(pat_ref(\"%s\")),", e->left->sval);
            emit_expr(e->left->args[0]);
            E(")");
        } else {
            /* *(expr) — deref of compound expression */
            E("deref("); emit_expr(e->left); E(")");
        }
        break;

    case E_NEG: E("neg("); emit_expr(e->right); E(")"); break;

    case E_CONCAT:
        E("concat_sv("); emit_expr(e->left); E(","); emit_expr(e->right); E(")");
        break;

    case E_REDUCE: E("aply(\"reduce\",(SnoVal[]){"); emit_expr(e->left); E(","); emit_expr(e->right); E("},2)"); break;
    case E_ADD:    E("add(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_SUB:    E("sub(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_MUL:    E("mul(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_DIV:    E("divyde(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_POW:    E("powr(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_ALT:
        /* Same: if either side is pattern-valued, use pat_alt */
        if (expr_contains_pattern(e->left) || expr_contains_pattern(e->right)) {
            E("pat_alt("); emit_pat(e->left); E(","); emit_pat(e->right); E(")");
        } else {
            E("alt("); emit_expr(e->left); E(","); emit_expr(e->right); E(")");
        }
        break;

    /* capture nodes — in value context, evaluate the child */
    case E_COND: emit_expr(e->left); break;
    case E_IMM:  emit_expr(e->left); break;

    case E_CALL:
        if (e->nargs == 0) {
            E("aply(\"%s\",NULL,0)", e->sval);
        } else {
            E("aply(\"%s\",(SnoVal[]){", e->sval);
            for (int i=0; i<e->nargs; i++) {
                if (i) E(","); emit_expr(e->args[i]);
            }
            E("},%d)", e->nargs);
        }
        break;

    case E_ARRAY:
        E("aref(%s,(SnoVal[]){", cs(e->sval));
        for (int i=0; i<e->nargs; i++) {
            if (i) E(","); emit_expr(e->args[i]);
        }
        E("},%d)", e->nargs);
        break;

    case E_INDEX:
        /* postfix subscript: expr[i] — e.g. c(x)[i] */
        E("indx("); emit_expr(e->left); E(",(SnoVal[]){");
        for (int i=0; i<e->nargs; i++) {
            if (i) E(","); emit_expr(e->args[i]);
        }
        E("},%d)", e->nargs);
        break;

    case E_AT:
        /* @var — cursor position capture: evaluates to cursor int */
        E("cursor_get(\"%s\")", e->sval);
        break;

    case E_ASSIGN:
        /* var = expr inside expression context */
        E("assign_expr(%s,", cs(e->left->sval)); emit_expr(e->right); E(")");
        break;
    }
}

/* ============================================================
 * Pattern expression emission → SnoPattern*
 *
 * Same Expr nodes, different routing:
 *   E_CALL  → pat_builtin or pat_call
 *   E_CONCAT → pat_cat
 *   E_ALT   → pat_alt
 *   E_COND  → pat_cond(child_pat, varname)
 *   E_IMM   → pat_imm(child_pat, varname)
 *   E_DEREF → pat_ref(varname)   (deferred pattern reference *X)
 *   E_STR   → pat_lit(strv)
 *   E_VAR   → pat_var(varname)   (pattern variable)
 * ============================================================ */
static void emit_pat(Expr *e);

static void emit_pat(Expr *e) {
    if (!e) { E("pat_epsilon()"); return; }
    switch (e->kind) {
    case E_STR:
        E("pat_lit("); emit_cstr(e->sval); E(")"); break;

    case E_VAR:
        E("pat_var(\"%s\")", e->sval); break;

    case E_DEREF:
        /* *X — deferred pattern reference */
        if (e->left && e->left->kind == E_VAR)
            E("pat_ref(\"%s\")", e->left->sval);
        else if (e->left && e->left->kind == E_CALL && e->left->nargs >= 1
                 && !is_defined_function(e->left->sval)) {
            /* *varname(arg...) — continuation-line misparse: deref-ref cat arg
             * Only applies when varname is NOT a known function (it's a pat var). */
            E("pat_cat(pat_ref(\"%s\"),", e->left->sval);
            emit_pat(e->left->args[0]);
            E(")");
        } else {
            E("pat_deref("); emit_expr(e->right ? e->right : e->left); E(")");
        }
        break;

    case E_CONCAT:
        E("pat_cat("); emit_pat(e->left); E(","); emit_pat(e->right); E(")"); break;

    case E_MUL:
        /* pat * x — parsed as arithmetic multiply, but in pattern context
         * this is: left_pattern ccat *right (deferred ref to right) */
        E("pat_cat("); emit_pat(e->left); E(",");
        if (e->right && e->right->kind == E_VAR)
            E("pat_ref(\"%s\")", e->right->sval);
        else { E("pat_deref("); emit_expr(e->right); E(")"); }
        E(")"); break;

    case E_REDUCE:
        /* & in pattern context: reduce(left, right) — must fire at MATCH TIME.
         * reduce() calls EVAL("epsilon . *Reduce(t, n)") where n may contain
         * nTop() — which must be evaluated at mtch time, not build time.
         * Use pat_user_call to defer the call until the engine executes
         * this node during pattern matching. */
        E("pat_user_call(\"reduce\",(SnoVal[]){"); emit_expr(e->left); E(","); emit_expr(e->right); E("},2)"); break;

    case E_ALT:
        E("pat_alt("); emit_pat(e->left); E(","); emit_pat(e->right); E(")"); break;

    case E_COND: {
        /* pat . var */
        const char *varname = (e->right && e->right->kind==E_VAR)
                              ? e->right->sval : "?";
        E("pat_cond("); emit_pat(e->left); E(",\"%s\")", varname); break;
    }
    case E_IMM: {
        /* pat $ var */
        const char *varname = (e->right && e->right->kind==E_VAR)
                              ? e->right->sval : "?";
        E("pat_imm("); emit_pat(e->left); E(",\"%s\")", varname); break;
    }

    case E_CALL: {
        /* Route known builtins to pat_* */
        const char *n = e->sval;
        /* B0: zero-arg pattern; B1i: one int64_t arg; B1s: one string arg; B1v: one SnoVal arg */
        #define B0(nm,fn)  if(strcasecmp(n,nm)==0){E(fn"()");break;}
        #define B1i(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(to_int(");emit_expr(e->args[0]);E("))");break;}
        #define B1s(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(to_str(");emit_expr(e->args[0]);E("))");break;}
        #define B1v(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(");emit_expr(e->args[0]);E(")");break;}
        B0("ARB","pat_arb")  B0("REM","pat_rem")
        B0("FAIL","pat_fail") B0("ABORT","pat_abort")
        /* FENCE() = bare fence; FENCE(p) = fence with sub-pattern */
        if(strcasecmp(n,"FENCE")==0){
            if(e->nargs>=1){E("pat_fence_p(");emit_pat(e->args[0]);E(")");}
            else{E("pat_fence()");}
            break;
        }
        B0("SUCCEED","pat_succeed")
        B0("BAL","pat_bal")
        B1i("LEN","pat_len")   B1i("POS","pat_pos")
        B1i("RPOS","pat_rpos") B1i("TAB","pat_tab")
        B1i("RTAB","pat_rtab")
        B1s("SPAN","pat_span") B1s("BREAK","pat_break")
        B1s("NOTANY","pat_notany") B1s("ANY","pat_any")
        B1v("ARBNO","pat_arbno")
        #undef B0
        #undef B1i
        #undef B1s
        #undef B1v
        /* user-defined pattern function — use pat_user_call so the function
         * fires at MATCH TIME (SPAT_USER_CALL materialisation), not at build time.
         * This correctly handles nPush()/nPop() side effects per mtch attempt,
         * and reduce()/shift() which return pattern objects at materialisation time.
         *
         * EXCEPTION: if name is NOT a known/defined function AND has args, the
         * parser has misinterpreted  var(pat)  as a function call.  In SNOBOL4,
         * a variable followed by a parenthesised expression is CONCATENATION, not
         * a call.  Emit: pat_cat(pat_var(n), emit_pat(args[0])) instead. */
        if (e->nargs > 0 && !is_defined_function(n)) {
            /* variable(args) → CONCAT(var, grouped_pat) */
            E("pat_cat(pat_var(\"%s\"),", n);
            if (e->nargs == 1) {
                emit_pat(e->args[0]);
            } else {
                /* Multiple args: emit as successive concatenations */
                for (int i = 0; i < e->nargs; i++) {
                    if (i < e->nargs - 1) E("pat_cat(");
                }
                emit_pat(e->args[0]);
                for (int i = 1; i < e->nargs; i++) { E(","); emit_pat(e->args[i]); E(")"); }
            }
            E(")");
            break;
        }
        if (e->nargs == 0) {
            E("pat_user_call(\"%s\",NULL,0)", n);
        } else {
            /* Pattern-constructor functions are called eagerly at BUILD TIME.
             * They return a SnoVal of type PATTERN — wrap with var_as_pattern.
             * reduce(t,n), shift(p,t), EVAL(s) → build-time call → aply.
             * Side-effect functions (nPush, nPop, nInc, Reduce, Shift, TZ, etc.)
             * must fire at MATCH TIME → stay as pat_user_call. */
            static const char *_build_time_fns[] = {
                "reduce", "shift", "EVAL", NULL
            };
            int _is_build = 0;
            for (int _ci = 0; _build_time_fns[_ci]; _ci++) {
                if (strcasecmp(n, _build_time_fns[_ci]) == 0) { _is_build = 1; break; }
            }
            if (_is_build) {
                E("var_as_pattern(aply(\"%s\",(SnoVal[]){", n);
                for (int i=0; i<e->nargs; i++) { if(i) E(","); emit_expr(e->args[i]); }
                E("},%d))", e->nargs);
            } else {
                E("pat_user_call(\"%s\",(SnoVal[]){", n);
                for (int i=0; i<e->nargs; i++) { if(i) E(","); emit_expr(e->args[i]); }
                E("},%d)", e->nargs);
            }
        }
        break;
    }

    /* value nodes that shouldn't appear in pattern context — treat as var */
    case E_INDEX:
    case E_AT:
    case E_ASSIGN:
    default:
        E("pat_val("); emit_expr(e); E(")"); break;
    }
}

/* ============================================================
 * Emit lvalue assignment target
 * ============================================================ */

/* cur_fn_def and is_fn_local() are defined after FnDef (below) */
static int is_fn_local(const char *varname);

static void emit_assign_target(Expr *lhs, const char *rhs_str) {
    if (!lhs) return;
    if (lhs->kind == E_VAR) {
        E("set(%s, %s);\n", cs(lhs->sval), rhs_str);
        E("var_set(\"%s\", %s);\n", lhs->sval, cs(lhs->sval)); /* all vars are natural/hashed */
    } else if (lhs->kind == E_ARRAY) {
        E("aset(%s,(SnoVal[]){", cs(lhs->sval));
        for (int i=0; i<lhs->nargs; i++) {
            if (i) E(","); emit_expr(lhs->args[i]);
        }
        E("},%d,%s);\n", lhs->nargs, rhs_str);
    } else if (lhs->kind == E_KEYWORD) {
        E("kw_set(\"%s\",%s);\n", lhs->sval, rhs_str);
    } else if (lhs->kind == E_DEREF) {
        E("iset("); emit_expr(lhs->right); E(",%s);\n", rhs_str);
    } else if (lhs->kind == E_CALL && lhs->nargs == 1) {
        /* field accessor lvalue: val(n) = x  →  field_set(n, "val", x) */
        E("field_set("); emit_expr(lhs->args[0]); E(", \"%s\", %s);\n", lhs->sval, rhs_str);
    } else {
        /* complex lvalue: evaluate and assign indirectly */
        E("iset("); emit_expr(lhs); E(",%s);\n", rhs_str);
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

    /* ---- Trampoline mode: every exit is a return, not a goto ---- */
    if (trampoline_mode) {
        if      (strcasecmp(label,"RETURN") ==0 ||
                 strcasecmp(label,"NRETURN")==0) {
            if (in_main) { E("return NULL"); return; }
            E("return _tramp_return_%s", fn); return;
        }
        else if (strcasecmp(label,"FRETURN")==0 ||
                 strcasecmp(label,"error")  ==0) {
            if (in_main) { E("return NULL"); return; }
            E("return _tramp_freturn_%s", fn); return;
        }
        else if (strcasecmp(label,"END")==0) {
            E("return NULL"); return;
        }
        else if (strcasecmp(label,"$COMPUTED")==0 ||
                 strcasecmp(label,"_COMPUTED")==0) {
            E("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        /* Cross-scope: fall through */
        if (label_is_in_fn_body(label, NULL) && !label_is_in_fn_body(label, fn)) {
            E("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        if (!in_main && !label_is_in_fn_body(label, fn)) {
            E("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        E("return (void*)block_%s", cs_label(label));
        return;
    }

    /* ---- Classic goto mode (unchanged) ---- */
    if      (strcasecmp(label,"RETURN") ==0) {
        if (in_main) { E("goto _SNO_END"); return; }
        E("goto _SNO_RETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"FRETURN")==0) {
        if (in_main) { E("goto _SNO_END"); return; }
        E("goto _SNO_FRETURN_%s", fn); return;
    }
    else if (strcasecmp(label,"NRETURN")==0) {
        if (in_main) { E("goto _SNO_END"); return; }
        E("goto _SNO_RETURN_%s", fn); return;
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
    if (label_is_in_fn_body(label, NULL) && !label_is_in_fn_body(label, fn)) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    if (!in_main && !label_is_in_fn_body(label, fn)) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid); return;
    }
    E("goto _L%s", cs_label(label));
}

static void emit_goto(SnoGoto *g, const char *fn, int result_ok) {
    if (trampoline_mode) {
        if (!g) { E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid); return; }
        if (g->uncond) {
            E("    "); emit_goto_target(g->uncond, fn); E(";\n");
        } else {
            if (result_ok) {
                if (g->onsuccess) { E("    if(_ok) { "); emit_goto_target(g->onsuccess, fn); E("; }\n"); }
                if (g->onfailure) { E("    if(!_ok) { "); emit_goto_target(g->onfailure, fn); E("; }\n"); }
            } else {
                if (g->onsuccess) { E("    "); emit_goto_target(g->onsuccess, fn); E(";\n"); }
            }
            E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
        }
        return;
    }
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
 * Post-parse pattern-statement repair
 *
 * The grammar is LALR(1) and cannot always distinguish:
 *   subject pattern = replacement    (pattern mtch)
 *   subject_expr = replacement       (pure assignment)
 * when pattern primitives (LEN, POS, etc.) appear inside the subject_expr.
 * The lexer returns PAT_BUILTIN only at bstack_top==0, but PAT_BUILTIN IS
 * also in the `primary` grammar rule for value exprs, causing the parser to
 * absorb the pattern into the subject.
 *
 * This function detects the case and repairs the Stmt in place.
 * It looks for: s->pattern==NULL, s->replacement==E_NULL, and the subject
 * tree contains a PAT_BUILTIN call in a position that looks like a pattern start.
 * ============================================================ */

static int is_pat_builtin_call(Expr *e) {
    if (!e || e->kind != E_CALL) return 0;
    static const char *pb[] = {
        "LEN","POS","RPOS","TAB","RTAB","SPAN","BREAK",
        "NOTANY","ANY","ARB","REM","FAIL","ABORT",
        "FENCE","SUCCEED","BAL","ARBNO", NULL
    };
    for (int i = 0; pb[i]; i++)
        if (strcasecmp(e->sval, pb[i]) == 0) return 1;
    return 0;
}

/* Returns 1 if expr e is a pattern node (E_CALL to pat_builtin, E_COND capture,
 * E_ALT, or E_CONCAT whose left child is a pattern). */
static int is_pat_node(Expr *e) {
    if (!e) return 0;
    if (is_pat_builtin_call(e)) return 1;
    if (e->kind == E_COND)   return 1;  /* .var capture */
    if (e->kind == E_ALT)    return 1;  /* | alternation */
    if (e->kind == E_REDUCE) return 1;  /* & reduce() call — always pattern context */
    /* E_MUL(pat_node, x) — parsed from "pat *x" where * is multiplication token
     * but semantically is pattern-ccat with deferred ref *x */
    if (e->kind == E_MUL && is_pat_node(e->left)) return 1;
    return 0;
}

/* Recursively checks if any node in e's subtree indicates pattern context.
 * Used to decide whether a pure assignment RHS should use emit_pat.
 * Indicators: E_DEREF (*var — always a pattern ref), E_REDUCE (& — reduce()),
 * E_COND (. capture), E_ALT (| alternation in pattern context), E_CALL to
 * any pattern builtin including ARBNO/FENCE/etc. */
/* Returns 1 if the expression subtree rooted at e contains ANY pattern-valued
 * node.  Used by emit_expr to decide whether E_CONCAT / E_ALT should be routed
 * through emit_pat (pat_cat / pat_alt) instead of the string path
 * (concat_sv / alt).
 *
 * Key cases that are pattern-valued but NOT caught by is_pat_node:
 *   - E_DEREF whose left child is E_VAR — "*varname" deferred pattern ref
 *   - E_CONCAT or E_ALT whose subtree contains any of the above
 */
static int expr_contains_pattern(Expr *e) {
    if (!e) return 0;
    if (is_pat_node(e)) return 1;
    /* *varname — deferred pattern ref */
    if (e->kind == E_DEREF && e->left && e->left->kind == E_VAR) return 1;
    /* *varname(arg) — parser misparse deref+ccat */
    if (e->kind == E_DEREF && e->left && e->left->kind == E_CALL) return 1;
    /* recurse into children */
    if (e->kind == E_CONCAT || e->kind == E_ALT || e->kind == E_MUL)
        return expr_contains_pattern(e->left) || expr_contains_pattern(e->right);
    if (e->kind == E_CALL) {
        /* ARBNO, FENCE, etc. already caught by is_pat_builtin_call above.
         * Also treat reduce/evl calls as pattern-valued when inside ccat. */
        if (e->sval && (strcasecmp(e->sval,"reduce")==0 || strcasecmp(e->sval,"evl")==0))
            return 1;
        for (int i = 0; i < e->nargs; i++)
            if (expr_contains_pattern(e->args[i])) return 1;
    }
    return 0;
}

/* Walk the E_CONCAT left spine. When we find a right child that is_pat_node,
 * detach it and everything after it into the pattern.
 * Returns the extracted pattern root, or NULL if nothing found.
 * *subj_out is set to the remaining subject (may be the original expr if
 * no split needed).
 *
 * The tree is LEFT-ASSOCIATIVE ccat:
 *   (((strv, POS(0)), ANY('abc')), E_COND(letter))
 * We walk the left spine, looking for the first right child that is a pat node.
 * When found at depth D, the pattern is: right_at_D ccat right_at_D-1 ccat ... ccat right_at_0
 * assembled left-to-right.
 */
typedef struct { Expr *subj; Expr *pat; } SplitResult;

static Expr *make_concat(Expr *left, Expr *right) {
    if (!left)  return right;
    if (!right) return left;
    Expr *e = expr_new(E_CONCAT);
    e->left = left; e->right = right;
    return e;
}

static SplitResult split_spine(Expr *e) {
    /* Null or non-ccat node that's a pure value: subject only */
    if (!e) { SplitResult r = {NULL, NULL}; return r; }

    if (e->kind != E_CONCAT) {
        if (is_pat_node(e)) {
            SplitResult r = {NULL, e}; return r;   /* entire node is pattern */
        } else {
            SplitResult r = {e, NULL}; return r;   /* entire node is subject */
        }
    }

    /* e = E_CONCAT(left, right) */
    /* First check if RIGHT is a pattern node (first pat on the right spine) */
    if (is_pat_node(e->right)) {
        /* Split here: left is subject, right and above become pattern */
        SplitResult inner = split_spine(e->left);
        /* inner.pat (if any) should be prepended, but since left was already
         * recursed and left's right IS the current pat... */
        /* Actually: the split is between left and right.
         * Subject = inner.subj (what was pure subject in e->left)
         * Pattern = inner.pat (any pattern found in e->left's right chain) ccat e->right */
        SplitResult r;
        r.subj = inner.subj;
        r.pat  = make_concat(inner.pat, e->right);
        return r;
    }

    /* Right is not a pattern node. Recurse left. */
    SplitResult inner = split_spine(e->left);
    if (!inner.pat) {
        /* No split found in left, and right is not a pattern. No split. */
        SplitResult r = {e, NULL}; return r;
    }
    /* Split found in left spine: reassemble */
    /* inner.subj is the new left, e->right gets appended to the pattern */
    SplitResult r;
    r.subj = inner.subj;
    r.pat  = make_concat(inner.pat, e->right);
    return r;
}

static Expr *split_subject_pattern(Expr *e, Expr **subj_out) {
    if (!e) { *subj_out = NULL; return NULL; }
    SplitResult r = split_spine(e);
    *subj_out = r.subj;
    return r.pat;
}

/* Repair a misparsed pattern-mtch stmt.
 * Called when s->pattern==NULL and s->replacement is E_NULL (bare '=').
 * Also repairs pattern-mtch stmts with no replacement (s->replacement==NULL)
 * where the subject absorbed the pattern (no '=' present).
 * Returns 1 if the stmt was repaired. */
static int maybe_fix_pattern_stmt(Stmt *s) {
    if (!s->subject) return 0;  /* no subject */
    /* Heuristic: if replacement is non-null non-E_NULL, this is a plain assignment,
     * not a pattern mtch. Skip. */
    if (s->replacement && s->replacement->kind != E_NULL) return 0;

    Expr *new_subj = NULL;
    Expr *new_pat  = split_subject_pattern(s->subject, &new_subj);

    if (!new_pat && !s->pattern) return 0;  /* nothing to fix */

    if (new_pat) {
        s->subject = new_subj;
        if (s->pattern) {
            /* Pattern already parsed (e.g. RPOS(0) at end). Prepend extracted
             * pattern from subject in front of the existing s->pattern. */
            s->pattern = make_concat(new_pat, s->pattern);
        } else {
            s->pattern = new_pat;
        }
        return 1;
    }
    return 0;
}

/* ============================================================
 * pat_is_anchored — returns 1 if the leftmost node of pattern e
 * is POS(), meaning the pattern anchors at a specific position.
 * Used to decide whether to wrap in ARB for substring scan.
 * ============================================================ */
static int pat_is_anchored(Expr *e) {
    if (!e) return 0;
    if (e->kind == E_CALL && e->sval && strcasecmp(e->sval, "POS") == 0) return 1;
    if (e->kind == E_CONCAT) return pat_is_anchored(e->left);
    return 0;
}

/* ============================================================
 * Emit one statement
 * ============================================================ */
static void emit_stmt(Stmt *s, const char *fn) {
    /* Repair misparsed pattern-mtch stmts (grammar absorbs pattern into subject) */
    maybe_fix_pattern_stmt(s);

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
        /* If the RHS contains deferred refs (*var), reduce() calls (&), or
         * pattern builtins (ARBNO/FENCE/etc.), emit in pattern context so
         * E_CONCAT becomes pat_cat and *var becomes pat_ref.
         * This handles: snoParse = nPush() ARBNO(*snoCommand) ... nPop() */
        if (expr_contains_pattern(s->replacement)) {
            E("SnoVal _v%d = ", u); emit_pat(s->replacement); E(";\n");
        } else {
            E("SnoVal _v%d = ", u); emit_expr(s->replacement); E(";\n");
        }
        E("int _ok%d = !IS_FAIL(_v%d);\n", u, u);
        E("if(_ok%d) {\n", u);
        char rhs[32]; snprintf(rhs,sizeof rhs,"_v%d",u);
        emit_assign_target_io(s->subject, rhs);
        E("}\n");
        /* emit goto using _ok%d for conditional :S/:F branches */
        if (!s->go) {
            if (trampoline_mode) E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
            else                 E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
        else if (s->go->uncond) { emit_goto(s->go, fn, 0); }
        else {
            if (s->go->onsuccess) { E("    if(_ok%d) ", u); emit_goto_target(s->go->onsuccess, fn); E(";\n"); }
            if (s->go->onfailure) { E("    if(!_ok%d) ", u); emit_goto_target(s->go->onfailure, fn); E(";\n"); }
            if (trampoline_mode) E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
            else                 E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
        return;
    }

    /* ---- pattern mtch: subject pattern [= replacement] ---- */
    /* Compiled Byrd box path — replaces pat_* / engine.c stopgap. */
    if (s->pattern) {
        int u = uid();
        E("/* byrd mtch u%d */\n", u);
        E("SnoVal _s%d = ", u); emit_expr(s->subject); E(";\n");
        E("const char *_subj%d = to_str(_s%d);\n", u, u);
        E("int64_t _slen%d = _subj%d ? (int64_t)strlen(_subj%d) : 0;\n", u, u, u);
        E("int64_t _cur%d  = 0;\n", u);
        E("int64_t _mstart%d = 0;\n", u);  /* cursor before mtch — for replacement */

        char root_lbl[64], ok_lbl[64], fail_lbl[64], done_lbl[64];
        snprintf(root_lbl, sizeof root_lbl, "_byrd_%d",      u);
        snprintf(ok_lbl,   sizeof ok_lbl,   "_byrd_%d_ok",   u);
        snprintf(fail_lbl, sizeof fail_lbl, "_byrd_%d_fail", u);
        snprintf(done_lbl, sizeof done_lbl, "_byrd_%d_done", u);

        char sv[32], sl[32], cv[32];
        snprintf(sv, sizeof sv, "_subj%d", u);
        snprintf(sl, sizeof sl, "_slen%d", u);
        snprintf(cv, sizeof cv, "_cur%d",  u);

        /* Declare _ok before the Byrd block (C: no jumps over declarations) */
        E("int _ok%d = 0;\n", u);
        E("_mstart%d = _cur%d;\n", u, u);

        /* SNOBOL4 pattern matching is a substring scan: wrap pattern in ARB
         * unless the leftmost node is POS() which anchors to a position. */
        Expr *scan_pat = s->pattern;
        if (!pat_is_anchored(s->pattern)) {
            Expr *arb = expr_new(E_CALL);
            arb->sval = strdup("ARB");
            arb->nargs = 0;
            Expr *seq = expr_new(E_CONCAT);
            seq->left = arb;
            seq->right = s->pattern;
            scan_pat = seq;
        }
        byrd_emit_pattern(scan_pat, out, root_lbl, sv, sl, cv, ok_lbl, fail_lbl);

        /* gamma: mtch succeeded */
        E("%s:;\n", ok_lbl);
        E("_ok%d = 1;\n", u);
        if (s->replacement) {
            /* Replace matched region [_mstart%d .. _cur%d) with replacement */
            E("{\n");
            E("    SnoVal _r%d = ", u); emit_expr(s->replacement); E(";\n");
            E("    const char *_rs%d = to_str(_r%d);\n", u, u);
            E("    int64_t _rlen%d = _rs%d ? (int64_t)strlen(_rs%d) : 0;\n", u, u, u);
            E("    int64_t _tail%d = _slen%d - _cur%d;\n", u, u, u);
            E("    int64_t _newlen%d = _mstart%d + _rlen%d + _tail%d;\n", u, u, u, u);
            E("    char *_nb%d = (char*)GC_malloc(_newlen%d + 1);\n", u, u);
            E("    if (_mstart%d > 0) memcpy(_nb%d, _subj%d, (size_t)_mstart%d);\n", u, u, u, u);
            E("    if (_rlen%d  > 0) memcpy(_nb%d + _mstart%d, _rs%d, (size_t)_rlen%d);\n", u, u, u, u, u);
            E("    if (_tail%d  > 0) memcpy(_nb%d + _mstart%d + _rlen%d, _subj%d + _cur%d, (size_t)_tail%d);\n", u, u, u, u, u, u, u);
            E("    _nb%d[_newlen%d] = '\\0';\n", u, u);
            E("    _s%d = STR_VAL(_nb%d);\n", u, u);
            /* write back to subject variable */
            if (s->subject && s->subject->kind == E_VAR) {
                if (is_io_name(s->subject->sval))
                    E("    var_set(\"%s\", _s%d);\n", s->subject->sval, u);
                else {
                    E("    set(%s, _s%d);\n", cs(s->subject->sval), u);
                    E("    var_set(\"%s\", %s);\n", s->subject->sval, cs(s->subject->sval));
                }
            }
            E("}\n");
        }
        E("goto %s;\n", done_lbl);

        /* omega: mtch failed */
        E("%s:;\n", fail_lbl);
        E("_ok%d = 0;\n", u);

        E("%s:;\n", done_lbl);

        /* emit goto using _ok%d */
        if (!s->go) {
            if (trampoline_mode) E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
            else                 E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
        else if (s->go->uncond) { emit_goto(s->go, fn, 0); }
        else {
            if (s->go->onsuccess) { E("    if(_ok%d) ", u); emit_goto_target(s->go->onsuccess, fn); E(";\n"); }
            if (s->go->onfailure) { E("    if(!_ok%d) ", u); emit_goto_target(s->go->onfailure, fn); E(";\n"); }
            if (trampoline_mode) E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
            else                 E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
        return;
    }

    /* ---- expression evaluation only ---- */
    {
        int u=uid();
        E("SnoVal _v%d = ", u); emit_expr(s->subject); E(";\n");
        E("int _ok%d = !IS_FAIL(_v%d);\n", u, u);
        /* emit goto using _ok%d */
        if (!s->go) {
            if (trampoline_mode) E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
            else                 E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
        }
        else if (s->go->uncond) { emit_goto(s->go, fn, 0); }
        else {
            if (s->go->onsuccess) { E("    if(_ok%d) ", u); emit_goto_target(s->go->onsuccess, fn); E(";\n"); }
            if (s->go->onfailure) { E("    if(!_ok%d) ", u); emit_goto_target(s->go->onfailure, fn); E(";\n"); }
            if (trampoline_mode) E("    return (void*)_tramp_next_%d;\n", cur_stmt_next_uid);
            else                 E("    goto _SNO_NEXT_%d;\n", cur_stmt_next_uid);
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
        E("var_set(\"%s\", %s);\n", lhs->sval, rhs_str);
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
    char  *entry_label;        /* explicit entry label from DEFINE('proto','label') */
} FnDef;

static FnDef fn_table[FN_MAX];
static int   fn_count = 0;

/* Returns 1 if 'name' is a user-defined function (present in fn_table) or a
 * known SNOBOL4 standard library function.  Used to distinguish CALL from
 * variable-concatenation-with-grouping: in SNOBOL4, nl('+') where nl is a
 * variable (not a function) means CONCAT(nl, '+'), not a function call. */
static int is_defined_function(const char *name) {
    if (name && (strcasecmp(name,"snoXList")==0 || strcasecmp(name,"snoX3")==0)) fprintf(stderr, "DEBUG is_defined_function(%s)\n", name);
    static const char *std[] = {
        "APPLY","ARG","ARRAY","ATAN","BACKSPACE","BREAK","BREAKX",
        "CHAR","CHOP","CLEAR","CODE","COLLECT","CONVERT","COPY","COS",
        "DATA","DATATYPE","DATE","DEFINE","DETACH","DIFFER","DUMP","DUPL",
        "EJECT","ENDFILE","EQ","EVAL","EXIT","EXP","FENCE","FIELD",
        "GE","GT","HOST","IDENT","INPUT","INTEGER","ITEM",
        "LE","LEN","LEQ","LGE","LGT","LLE","LLT","LN","LNE","LOAD",
        "LOCAL","LPAD","LT","NE","NOTANY","OPSYN","OUTPUT",
        "POS","PROTOTYPE","REMDR","REPLACE","REVERSE","REWIND","RPAD",
        "RPOS","RSORT","RTAB","SET","SETEXIT","SIN","SIZE","SORT","SPAN",
        "SQRT","STOPTR","SUBSTR","TAB","TRACE","TRIM","UNLOAD","UCASE","LCASE",
        "ANY","ARB","ARBNO","BAL","FAIL","ABORT","REM","SUCCEED",
        "ICASE","UCASE","LCASE","REVERSE","REPLACE","DUPL","LPAD","RPAD",
        NULL
    };
    for (int i = 0; std[i]; i++)
        if (strcasecmp(name, std[i]) == 0) return 1;
    for (int i = 0; i < fn_count; i++)
        if (strcasecmp(fn_table[i].name, name) == 0) return 1;
    return 0;
}

/* cur_fn_def: set to the current FnDef* during emit_fn, NULL during emit_main */
static FnDef *cur_fn_def = NULL;

/* is_fn_local: return 1 if varname is a declared param, local, or return-value
 * of the current function.  Returns 0 when in global (main) scope. */
static int is_fn_local(const char *varname) {
    if (!cur_fn_def || !varname) return 0;
    if (strcasecmp(cur_fn_def->name, varname) == 0) return 1;
    for (int i = 0; i < cur_fn_def->nargs; i++)
        if (strcasecmp(cur_fn_def->args[i], varname) == 0) return 1;
    for (int i = 0; i < cur_fn_def->nlocals; i++)
        if (strcasecmp(cur_fn_def->locals[i], varname) == 0) return 1;
    return 0;
}

/* Return fn indx if label matches a known function entry, else -1 */
static int fn_by_label(const char *label) {
    if (!label) return -1;
    for (int i=0; i<fn_count; i++) {
        if (strcasecmp(fn_table[i].name, label)==0) return i;
        if (fn_table[i].entry_label && strcasecmp(fn_table[i].entry_label, label)==0) return i;
    }
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

/* Return fn indx if stmt is the DEFINE(...) call for it, else -1 */
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
        /* Extract entry label from 2nd DEFINE arg: DEFINE('proto','entry_label') */
        fn->entry_label = NULL;
        if (s->subject && s->subject->kind == E_CALL &&
            s->subject->nargs >= 2) {
            const char *el = flatten_str_expr(s->subject->args[1]);
            if (el && el[0]) fn->entry_label = strdup(el);
        }
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
        /* Deduplicate: if this name already exists, overwrite it.
         * SNOBOL4 function names are case-sensitive — use strcmp, not strcasecmp.
         * e.g. "Pop(var)" (stack.sno) and "pop()" (semantic.sno) are DIFFERENT. */
        int found = -1;
        for (int i=0; i<fn_count; i++)
            if (strcmp(fn_table[i].name, fn->name)==0) { found=i; break; }
        if (found >= 0) {
            /* Free old name/args/locals, replc with new definition */
            fn_table[found] = *fn;
        } else {
            fn_count++;
        }
    }
    /* Second pass: find ALL body_starts for each function */
    for (int i=0; i<fn_count; i++) {
        fn_table[i].nbody_starts = 0;
        /* The entry label is: entry_label if set, else function name */
        const char *entry = fn_table[i].entry_label ? fn_table[i].entry_label : fn_table[i].name;
        for (Stmt *s = prog->head; s; s = s->next) {
            if (s->label && strcasecmp(s->label, entry)==0) {
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
        /* The "entry" for this function is entry_label if set, else name */
        const char *entry = fn_table[i].entry_label ? fn_table[i].entry_label : fn_table[i].name;
        if (strcasecmp(entry, label) == 0 &&
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
    E("/* generated by sno2c */\n");
    E("#include \"runtime_shim.h\"\n\n");
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
    /* Phantoms exist only as boundary markers — no C function to emit */
    if (!fn->define_stmt) return;
    (void)prog;
    lreg_reset();
    byrd_fn_scope_reset();   /* clear cross-pattern static-decl dedup for this fn */
    cur_fn_name = fn->name;
    cur_fn_def  = fn;
    E("static SnoVal _sno_fn_%s(SnoVal *_args, int _nargs) {\n", fn->name);

    /* CSNOBOL4 DEFF8/DEFF10/DEFF6 semantics: save caller's hash values on entry,
     * restore them on ALL exit paths (RETURN, FRETURN, abort/setjmp).
     * ALL SNOBOL4 variables are natural (hashed) — NEVER skip save/restore. */

    /* --- Save declarations (must come before setjmp to be in scope at restore labels) --- */
    for (int i = 0; i < fn->nargs; i++)
        E("    SnoVal _saved_%s = var_get(\"%s\"); /* save caller's hash value */\n",
          cs(fn->args[i]), fn->args[i]);
    for (int i = 0; i < fn->nlocals; i++)
        E("    SnoVal _saved_%s = var_get(\"%s\"); /* save caller's hash value */\n",
          cs(fn->locals[i]), fn->locals[i]);
    E("\n");

    E("    jmp_buf _fn_abort_jmp;\n");
    E("    if (setjmp(_fn_abort_jmp) != 0) goto _SNO_ABORT_%s;\n", fn->name);
    E("    push_abort_handler(&_fn_abort_jmp);\n\n");

    /* Return-value variable — skip if an arg has the same name */
    {
        int clash = 0;
        for (int i = 0; i < fn->nargs; i++)
            if (strcasecmp(fn->args[i], fn->name) == 0) { clash = 1; break; }
        if (!clash)
            E("    SnoVal %s = {0}; /* return value */\n", cs(fn->name));
    }
    /* Declare C stack locals and install args into hash (DEFF8: save+assign) */
    for (int i = 0; i < fn->nargs; i++) {
        E("    SnoVal %s = (_nargs>%d)?_args[%d]:NULL_VAL;\n",
          cs(fn->args[i]), i, i);
        E("    var_set(\"%s\", %s); /* install arg in hash */\n",
          fn->args[i], cs(fn->args[i]));
    }
    /* Declare C stack locals and install as NULL into hash (DEFF10: save+null) */
    for (int i = 0; i < fn->nlocals; i++) {
        E("    SnoVal %s = {0};\n", cs(fn->locals[i]));
        E("    var_set(\"%s\", NULL_VAL); /* install local as null in hash */\n",
          fn->locals[i]);
    }
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

    /* --- RETURN path: restore caller's hash values (DEFF6: restore in reverse) --- */
    E("\n_SNO_RETURN_%s:\n", fn->name);
    E("    pop_abort_handler();\n");
    for (int i = fn->nlocals - 1; i >= 0; i--)
        E("    var_set(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        E("    var_set(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    E("    return get(%s);\n", cs(fn->name));

    /* --- FRETURN path: restore caller's hash values --- */
    E("_SNO_FRETURN_%s:\n", fn->name);
    E("    pop_abort_handler();\n");
    for (int i = fn->nlocals - 1; i >= 0; i--)
        E("    var_set(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        E("    var_set(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    E("    return FAIL_VAL;\n");

    /* --- ABORT path (setjmp fired): restore then return FAIL --- */
    E("_SNO_ABORT_%s:\n", fn->name);
    for (int i = fn->nlocals - 1; i >= 0; i--)
        E("    var_set(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        E("    var_set(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    E("    return FAIL_VAL;\n");

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

/* Return 1 if stmt s lies within any real (non-phantom) function body */
static int stmt_is_in_any_fn_body(Stmt *s) {
    return stmt_in_fn_body(s, NULL);
}

/* Return 1 if stmt s lies within a phantom function's body region.
 * Phantoms have body_starts populated after injection — reuse stmt_in_fn_body. */
static int stmt_in_phantom_body(Stmt *s) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_table[i].define_stmt) continue;  /* not a phantom */
        if (stmt_in_fn_body(s, fn_table[i].name)) return 1;
    }
    return 0;
}

static void emit_main(Program *prog) {
    lreg_reset();
    byrd_fn_scope_reset();
    cur_fn_name = "main";
    cur_fn_def  = NULL;   /* NULL = global scope; is_fn_local returns 0 for all vars */
    E("int main(void) {\n");
    E("    ini();\n");
    /* Register all global C statics so var_set() can sync them back.
     * This bridges the two-store gap: vars set via pattern conditional
     * assignment (. varname) or pre-ini write to the hash table only;
     * registration lets those writes also update the C statics. */
    for (int i = 0; i < sym_count; i++)
        E("    var_register(\"%s\", &%s);\n", sym_table[i], cs(sym_table[i]));
    E("    var_sync_registered(); /* pull pre-inited vars (nl,tab,etc) into C statics */\n");
    E("\n");

    /* Register all DEFINE'd functions (skip phantoms — runtime-owned) */
    for (int i=0; i<fn_count; i++) {
        if (!fn_table[i].define_stmt) continue;  /* phantom — skip */
        /* Reconstruct the proto spec string: "name(a,b)loc1,loc2" */
        E("    define(\"");
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
            E("    finish();\n");
            E("    return 0;\n");
            E("}\n");
            return;
        }
        /* Skip statements that live inside a function body */
        if (stmt_is_in_any_fn_body(s)) continue;
        /* Skip statements inside phantom (runtime-owned) function bodies */
        if (stmt_in_phantom_body(s)) continue;
        /* Skip the DEFINE(...) call statements themselves */
        if (stmt_define_proto(s)) continue;

        cur_stmt_next_uid = uid();
        emit_stmt(s, "main");
        E("_SNO_NEXT_%d:;\n", cur_stmt_next_uid);
    }

    /* Fallback if no END stmt found */
}

/* ============================================================
 * Trampoline emission (sprint stmt-fn)
 *
 * emit_trampoline_program() is called instead of the classic
 * emit_fn/emit_main path when trampoline_mode=1.
 *
 * Output structure:
 *   #include "trampoline.h"
 *   #include runtime headers
 *   static SnoVal globals...
 *   forward decls: block_X for every labeled stmt
 *   static void* stmt_N(void) { ...emit_stmt body... }  per stmt
 *   static void* block_L(void) { stmt sequence }        per labeled group
 *   int main(void) { runtime_init(); trampoline_run(block_START); }
 * ============================================================ */

/* Collect all labels that appear on statements (block entry points) */
#define TRAMP_LABEL_MAX 4096
static char *tramp_labels[TRAMP_LABEL_MAX];
static int   tramp_nlabels = 0;

static void tramp_collect_labels(Program *prog) {
    tramp_nlabels = 0;
    for (Stmt *s = prog->head; s; s = s->next) {
        if (s->label && tramp_nlabels < TRAMP_LABEL_MAX)
            tramp_labels[tramp_nlabels++] = s->label;
    }
}

static int tramp_has_label(const char *lbl) {
    for (int i = 0; i < tramp_nlabels; i++)
        if (strcasecmp(tramp_labels[i], lbl) == 0) return 1;
    return 0;
}

static void emit_trampoline_program(Program *prog) {
    /* --- Header --- */
    E("/* generated by sno2c -trampoline */\n");
    E("#include \"trampoline.h\"\n");
    E("#include \"runtime_shim.h\"\n\n");

    /* --- Global variable declarations --- */
    E("/* --- global SNOBOL4 variables --- */\n");
    for (int i = 0; i < sym_count; i++)
        E("static SnoVal %s = {0};\n", cs(sym_table[i]));
    E("\n");

    /* --- Collect labels for forward declarations --- */
    tramp_collect_labels(prog);

    /* --- Forward declarations for all block functions --- */
    E("/* --- block forward declarations --- */\n");
    E("static void *block_START(void);\n");
    E("static void *block_END(void);\n");
    for (int i = 0; i < tramp_nlabels; i++) {
        const char *lbl = tramp_labels[i];
        if (strcasecmp(lbl,"END")==0) continue;
        E("static void *block_%s(void);\n", cs_label(lbl));
    }
    E("\n");

    /* --- Emit DEFINE'd functions using the existing emit_fn path ---
     * Function bodies use classic goto emission (trampoline_mode=0 inside).
     * Only main-level code uses the trampoline model. */
    {
        int saved = trampoline_mode;
        trampoline_mode = 0;
        emit_fn_forwards();
        for (int i = 0; i < fn_count; i++)
            emit_fn(&fn_table[i], prog);
        trampoline_mode = saved;
    }
    E("\n");

    /* --- Sentinel block pointers used by stmt_N as "continue" signals ---
     * _tramp_next_N is just a unique non-NULL address the block fn
     * recognises as "this stmt fell through; run next stmt". We use the
     * address of a unique static char as the sentinel. */
    /* (Generated inline per-stmt as needed — no pre-declaration required
     *  because block_L compares by value, not by name.) */

    /* --- Pass 0: emit compiled named pattern functions ---
     * Sub-pass 0a: pre-register ALL names so forward/mutual refs resolve.
     * Sub-pass 0b: emit function bodies (all names already in registry).
     */
    E("/* --- compiled named pattern functions --- */\n");
    byrd_named_pat_reset();

    /* 0a: register all names first */
    for (Stmt *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_is_in_any_fn_body(s)) continue;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (!s->pattern && s->replacement &&
            s->subject && s->subject->kind == E_VAR &&
            expr_contains_pattern(s->replacement)) {
            byrd_preregister_named_pattern(s->subject->sval);
        }
    }

    /* 0a.5: emit ALL struct typedef fwd-decls, then function fwd-decls */
    byrd_emit_named_typedecls(out);
    byrd_emit_named_fwdecls(out);

    /* 0b: emit function bodies */
    for (Stmt *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_is_in_any_fn_body(s)) continue;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (!s->pattern && s->replacement &&
            s->subject && s->subject->kind == E_VAR &&
            expr_contains_pattern(s->replacement)) {
            byrd_emit_named_pattern(s->subject->sval, s->replacement, out);
        }
    }
    E("\n");

    /* --- Emit each main-level stmt as its own function --- */
    lreg_reset();
    byrd_fn_scope_reset();
    cur_fn_name = "main";
    cur_fn_def  = NULL;

    /* sid_uid[sid] = the cur_stmt_next_uid assigned to stmt sid (1-based) */
#define TRAMP_STMT_MAX 8192
    static int sid_uid[TRAMP_STMT_MAX];
    static Stmt *sid_stmt[TRAMP_STMT_MAX];
    int stmt_count = 0;

    /* Pass 1: emit stmt_N() functions, record sid→uid mapping */
    for (Stmt *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_is_in_any_fn_body(s)) continue;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (stmt_count >= TRAMP_STMT_MAX) break;

        int sid = ++stmt_count;
        cur_stmt_next_uid = uid();
        sid_uid[sid]  = cur_stmt_next_uid;
        sid_stmt[sid] = s;

        /* sentinel: unique static char address == "this stmt fell through" */
        E("static char _tramp_sentinel_%d;\n", cur_stmt_next_uid);
        E("#define _tramp_next_%d ((void*)&_tramp_sentinel_%d)\n",
          cur_stmt_next_uid, cur_stmt_next_uid);

        E("static void *stmt_%d(void) { /* line %d%s%s */\n",
          sid, s->lineno,
          s->label ? " label:" : "",
          s->label ? s->label  : "");
        emit_stmt(s, "main");
        E("}\n\n");
    }

    /* Pass 2: emit block grouping functions.
     * Rule: a labeled stmt starts a new block.
     * Unlabeled stmts after it belong to the same block.
     * Each block calls its member stmts; if stmt returns _tramp_next_N
     * (fall-through sentinel), continue; otherwise return immediately.
     */
    E("/* --- block functions --- */\n");

    int block_open = 0;
    int first_block = 1;  /* first block is always block_START */

    for (int sid = 1; sid <= stmt_count; sid++) {
        Stmt *s = sid_stmt[sid];

        /* A labeled stmt closes the current block (falls through to new label) */
        if (s->label && block_open) {
            E("    return block_%s; /* fall into next block */\n}\n\n",
              cs_label(s->label));
            block_open = 0;
        }

        /* Open a new block if none is open */
        if (!block_open) {
            if (first_block) {
                E("static void *block_START(void) {\n");
                first_block = 0;
            } else if (s->label) {
                E("static void *block_%s(void) {\n", cs_label(s->label));
            } else {
                /* Unlabeled stmt after a fall-through gap — shouldn't normally
                 * happen in well-formed SNOBOL4, but handle gracefully */
                E("static void *block_START(void) {\n");
            }
            block_open = 1;
        }

        /* Call stmt, propagate any non-fallthrough return */
        E("    { void *_r = stmt_%d();\n", sid);
        E("      if (_r != _tramp_next_%d) return _r; }\n", sid_uid[sid]);
    }

    /* Close the last open block */
    if (block_open) {
        E("    return block_END;\n}\n\n");
    } else if (stmt_count == 0) {
        /* Empty program */
        E("static void *block_START(void) { return block_END; }\n\n");
    }

    E("static void *block_END(void) { return NULL; }\n\n");

    /* --- main --- */
    E("int main(void) {\n");
    E("    ini();\n");
    for (int i = 0; i < sym_count; i++)
        E("    var_register(\"%s\", &%s);\n", sym_table[i], cs(sym_table[i]));
    E("    var_sync_registered();\n\n");
    for (int i = 0; i < fn_count; i++) {
        if (!fn_table[i].define_stmt) continue;
        E("    define(\"%s(", fn_table[i].name);
        for (int j = 0; j < fn_table[i].nargs; j++) {
            if (j) E(",");
            E("%s", fn_table[i].args[j]);
        }
        E(")");
        for (int j = 0; j < fn_table[i].nlocals; j++) {
            if (j) E(","); else E("");
            E("%s", fn_table[i].locals[j]);
        }
        E("\", _sno_fn_%s);\n", fn_table[i].name);
    }
    E("\n    trampoline_run(block_START);\n");
    E("    return 0;\n}\n");
}

/* ============================================================
 * Public entry point
 * ============================================================ */
void snoc_emit(Program *prog, FILE *f) {
    out = f;

    /* Phase 1: collect variable names and function definitions */
    collect_symbols(prog);
    collect_functions(prog);

    /* DEBUG: dump fn_table after collect */
    if (getenv("SNOC_FN_DEBUG")) {
        for (int _di = 0; _di < fn_count; _di++)
            fprintf(stderr, "FN[%d] name=%-20s entry=%-12s end=%-14s nbody=%d\n",
                _di, fn_table[_di].name,
                fn_table[_di].entry_label ? fn_table[_di].entry_label : "(null)",
                fn_table[_di].end_label   ? fn_table[_di].end_label   : "(null)",
                fn_table[_di].nbody_starts);
    }

    /* Phase 1b: inject phantom FnDef entries for every runtime-owned function
     * whose source body appears in the expanded -INCLUDE stream but whose DEFINE
     * is handled by snobol4_inc.c at runtime (so collect_functions never sees it).
     *
     * Phantoms have name + end_label only. define_stmt = NULL, nbody_starts = 0.
     * They exist SOLELY so is_body_boundary() recognises their entry/end labels
     * as boundaries and stops body-absorption into the wrong C function.
     * emit_fn() skips phantoms (define_stmt == NULL → no C function emitted).
     * emit_main() skips phantoms (define_stmt == NULL → no define() call).
     *
     * Source: snobol4_inc.c inc_init() + inc_init_extra() registrations
     * whose bodies appear in: ShiftReduce.sno, stack.sno, counter.sno, semantic.sno
     */
    static const struct { const char *name; const char *end_label; } phantoms[] = {
        /* ShiftReduce.sno */
        { "Shift",        "ShiftEnd"    },
        { "Reduce",       "ReduceEnd"   },
        /* stack.sno */
        { "InitStack",    "StackEnd"    },
        { "Push",         "StackEnd"    },
        { "Pop",          "StackEnd"    },
        { "Top",          "StackEnd"    },
        /* counter.sno */
        { "InitCounter",  "CounterEnd"  },
        { "PushCounter",  "CounterEnd"  },
        { "IncCounter",   "CounterEnd"  },
        { "DecCounter",   "CounterEnd"  },
        { "PopCounter",   "CounterEnd"  },
        { "TopCounter",   "CounterEnd"  },
        { "InitBegTag",   "BegTagEnd"   },
        { "PushBegTag",   "BegTagEnd"   },
        { "PopBegTag",    "BegTagEnd"   },
        { "TopBegTag",    "BegTagEnd"   },
        { "DumpBegTag",   "BegTagEnd"   },
        { "InitEndTag",   "EndTagEnd"   },
        { "PushEndTag",   "EndTagEnd"   },
        { "PopEndTag",    "EndTagEnd"   },
        { "TopEndTag",    "EndTagEnd"   },
        { "DumpEndTag",   "EndTagEnd"   },
        /* semantic.sno — entry labels are name_, end is semanticEnd */
        { "shift_",       "semanticEnd" },
        { "reduce_",      "semanticEnd" },
        { "pop_",         "semanticEnd" },
        { "nPush_",       "semanticEnd" },
        { "nInc_",        "semanticEnd" },
        { "nDec_",        "semanticEnd" },
        { "nTop_",        "semanticEnd" },
        { "nPop_",        "semanticEnd" },
        { NULL, NULL }
    };
    for (int pi = 0; phantoms[pi].name && fn_count < FN_MAX; pi++) {
        /* Skip if already in fn_table (defined by SNOBOL4 DEFINE in-stream).
         * Two-arg DEFINE('nInc()', 'nInc_') stores name="nInc", entry_label="nInc_".
         * Phantom name is "nInc_" — must check entry_label to avoid double-registering. */
        int already = 0;
        for (int fi = 0; fi < fn_count; fi++)
            if (strcmp(fn_table[fi].name, phantoms[pi].name) == 0 ||
                (fn_table[fi].entry_label &&
                 strcmp(fn_table[fi].entry_label, phantoms[pi].name) == 0))
            { already=1; break; }
        if (already) continue;
        FnDef *ph = &fn_table[fn_count++];
        memset(ph, 0, sizeof *ph);
        ph->name        = strdup(phantoms[pi].name);
        ph->end_label   = strdup(phantoms[pi].end_label);
        ph->define_stmt = NULL;   /* phantom: no SNOBOL4 DEFINE, no C emission */
        ph->entry_label = NULL;
        ph->nbody_starts = 0;
    }

    /* Populate body_starts for phantoms by scanning the program for their entry label.
     * This lets stmt_in_fn_body() work for phantoms exactly like real functions. */
    for (int i = 0; i < fn_count; i++) {
        if (fn_table[i].define_stmt) continue;  /* not a phantom */
        const char *entry = fn_table[i].entry_label
                          ? fn_table[i].entry_label
                          : fn_table[i].name;
        fn_table[i].nbody_starts = 0;
        for (Stmt *s = prog->head; s; s = s->next) {
            if (s->label && strcmp(s->label, entry) == 0) {
                if (fn_table[i].nbody_starts < BODY_MAX)
                    fn_table[i].body_starts[fn_table[i].nbody_starts++] = s;
            }
        }
    }

    /* Phase 2: emit */
    if (trampoline_mode) {
        emit_trampoline_program(prog);
        return;
    }
    emit_header();
    emit_global_var_decls();
    emit_fn_forwards();

    for (int i=0; i<fn_count; i++)
        emit_fn(&fn_table[i], prog);

    emit_main(prog);
}
