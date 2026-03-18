/*
 * emit.c — SNOBOL4→C emitter for sno2c
 *
 * One EXPR_t type.  emit_expr() and emit_pat() both walk EXPR_t nodes,
 * but emit_pat() routes E_FNC to pat_* and E_CONC to pat_cat().
 *
 * Generated C uses the snobol4.c runtime API:
 *   DESCR_t, NV_GET_fn/set, APPLY_fn, CONCAT_fn, add, …
 *   PATND_t*, pat_lit, pat_len, pat_cat, MATCH_fn, …
 */

#include "sno2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "emit_cnode.h"

/* Forward declarations */
static int is_io_name(const char *name);
int is_defined_function(const char *name);
static void emit_assign_target(EXPR_t *lhs, const char *rhs_str);
static void emit_assign_target_io(EXPR_t *lhs, const char *rhs_str);

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

/* Three-column pretty printer — shared with emit_byrd.c */

/* -----------------------------------------------------------------------
 * Sprint 1 validation: assert build_expr+cn_flat_print == emit_expr
 * Build with: make CFLAGS="-DCNODE_VALIDATE ..."
 * ----------------------------------------------------------------------- */
#ifdef CNODE_VALIDATE
static void emit_expr(EXPR_t *e);
static void emit_expr_validated(EXPR_t *e) {
    char *old_buf = NULL; size_t old_sz = 0;
    FILE *old_fp = open_memstream(&old_buf, &old_sz);
    FILE *saved = out; out = old_fp;
    emit_expr(e);
    out = saved; fclose(old_fp);

    char *new_buf = NULL; size_t new_sz = 0;
    FILE *new_fp = open_memstream(&new_buf, &new_sz);
    CArena *ar = cn_arena_new(65536);
    cn_flat_print(build_expr(ar, e), new_fp);
    fclose(new_fp); cn_arena_free(ar);

    if (old_sz != new_sz || memcmp(old_buf, new_buf, old_sz) != 0)
        fprintf(stderr, "\nCNODE MISMATCH:\n  OLD: %.200s\n  NEW: %.200s\n",
                old_buf, new_buf);

    fwrite(old_buf, 1, old_sz, out);
    free(old_buf); free(new_buf);
}
#define emit_expr(e) emit_expr_validated(e)
#endif /* CNODE_VALIDATE */

/* -----------------------------------------------------------------------
 * Sprint 4 wiring: PP_EXPR / PP_PAT
 *
 * Replace  E("DESCR_t _v%d = ", u); emit_expr(e); E(";\n");
 * with     E("DESCR_t _v%d = ", u); PP_EXPR(e, 16); E(";\n");
 *
 * PP_EXPR(e, col): build CNODE_t tree, pp_cnode at column col, free arena.
 * PP_PAT(e, col):  same but via build_pat.
 * col = number of chars already on the line before the expression starts.
 * ----------------------------------------------------------------------- */
#define PP_EXPR(e, col) do { \
    CArena *_a = cn_arena_new(65536); \
    pp_cnode(build_expr(_a, (e)), out, (col), 4, 120); \
    cn_arena_free(_a); \
} while(0)

#define PP_PAT(e, col) do { \
    CArena *_a = cn_arena_new(65536); \
    pp_cnode(build_pat(_a, (e)), out, (col), 4, 120); \
    cn_arena_free(_a); \
} while(0)
#define PRETTY_OUT out
#include "emit_pretty.h"

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
 * Value expression emission → DESCR_t
 * ============================================================ */
static void emit_expr(EXPR_t *e);
static void emit_pat(EXPR_t *e);
int expr_contains_pattern(EXPR_t *e);

/* -----------------------------------------------------------------------
 * emit_chain_pretty — multi-line indented binary chain emitter
 *
 * Walks a left-associative binary chain (E_CONC/E_OR) collecting all
 * leaves, then emits as indented multi-line nested calls.
 *
 * fn_name:  "CONCAT_fn", "pat_cat", "pat_alt"
 * emit_leaf: emit_expr or emit_pat — called for each leaf node
 * min_depth: minimum chain depth before pretty-printing kicks in (3)
 *
 * Output form (depth 4, fn="CONCAT_fn"):
 *   CONCAT_fn(
 *       CONCAT_fn(
 *           CONCAT_fn(leaf0, leaf1),
 *           leaf2),
 *       leaf3)
 * ----------------------------------------------------------------------- */
#define CHAIN_MAX 64
static void emit_chain_pretty(EXPR_t *e, int kind,
                               const char *fn_name,
                               void (*emit_leaf)(EXPR_t *),
                               int min_depth) {
    /* Count depth */
    int depth = 0;
    for (EXPR_t *n = e; n->kind == kind; n = n->left) depth++;

    if (depth < min_depth) {
        /* Short — keep inline */
        E("%s(", fn_name); emit_leaf(e->left); E(","); emit_leaf(e->right); E(")");
        return;
    }

    /* Collect leaves (left spine → right-to-left, then reverse) */
    EXPR_t *leaves[CHAIN_MAX];
    int n = 0;
    EXPR_t *cur = e;
    while (cur->kind == kind && n < CHAIN_MAX - 1) {
        leaves[n++] = cur->right;
        cur = cur->left;
    }
    leaves[n++] = cur;
    /* Reverse to get left-to-right order */
    for (int i = 0, j = n-1; i < j; i++, j--) {
        EXPR_t *tmp = leaves[i]; leaves[i] = leaves[j]; leaves[j] = tmp;
    }

    /* Emit: nested left-associative with 4-space indent per nesting level.
     *
     * For n leaves:
     *   fn(                  <- open (n-2) times
     *     fn(
     *       fn(leaf0, leaf1),
     *       leaf2),
     *     leaf3)
     */
    int indent = 4;
    /* Opening preamble: (n-2) lines each starting a new concat level */
    for (int i = 0; i < n - 2; i++) {
        E("%s(\n", fn_name);
        for (int s = 0; s < indent; s++) E(" ");
    }
    /* Innermost pair */
    E("%s(", fn_name);
    emit_leaf(leaves[0]);
    E(",\n");
    for (int s = 0; s < indent; s++) E(" ");
    emit_leaf(leaves[1]);
    E(")");
    /* Close each outer level, appending next right-arg */
    for (int i = 2; i < n; i++) {
        E(",\n");
        for (int s = 0; s < indent; s++) E(" ");
        emit_leaf(leaves[i]);
        E(")");
    }
}
#undef CHAIN_MAX

static void emit_expr(EXPR_t *e) {
    if (!e) { E("NULVCL"); return; }
    switch (e->kind) {
    case E_NULV:    E("NULVCL"); break;
    case E_QLIT:     E("STRVAL_fn("); emit_cstr(e->sval); E(")"); break;
    case E_ILIT:     E("INTVAL_fn(%ld)", e->ival); break;
    case E_FLIT:    E("real(%g)", e->dval); break;
    case E_VART:
        if (is_io_name(e->sval)) E("NV_GET_fn(\"%s\")", e->sval);
        else E("get(%s)", cs(e->sval));
        break;
    case E_KW: E("kw(\"%s\")", e->sval); break;

    case E_INDR:
        if (!e->left) {
            /* $expr — indirect lookup */
            E("deref("); emit_expr(e->right); E(")");
        } else if (e->left->kind == E_VART) {
            /* *varname — deferred pattern reference (resolved at MATCH_fn time) */
            E("var_as_pattern(pat_ref(\"%s\"))", e->left->sval);
        } else if (e->left->kind == E_FNC && e->left->nargs >= 1
                   && !is_defined_function(e->left->sval)) {

            /* *varname(arg...) — parser misparse: *varname concatenated with (arg).
             * SNOBOL4 continuation lines cause the parser to greedily consume the
             * next '(' as a function-call argument to varname.  The correct
             * semantics are: deferred-ref(*varname) cat arg. */
            E("CONCAT_fn(var_as_pattern(pat_ref(\"%s\")),", e->left->sval);
            emit_expr(e->left->args[0]);
            E(")");
        } else {
            /* *(expr) — deref of compound expression */
            E("deref("); emit_expr(e->left); E(")");
        }
        break;

    case E_MNS: E("neg("); emit_expr(e->left); E(")"); break;

    case E_CONC:
        emit_chain_pretty(e, E_CONC, "CONCAT_fn", emit_expr, 2);
        break;

    case E_OPSYN: E("APPLY_fn(\"reduce\",(DESCR_t[]){"); emit_expr(e->left); E(","); emit_expr(e->right); E("},2)"); break;
    case E_ADD:    E("add(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_SUB:    E("sub(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_MPY:    E("mul(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_DIV:    E("DIVIDE_fn(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_EXPOP:    E("POWER_fn(");    emit_expr(e->left); E(","); emit_expr(e->right); E(")"); break;
    case E_OR:
        /* Same: if either side is pattern-valued, use pat_alt */
        if (expr_contains_pattern(e->left) || expr_contains_pattern(e->right)) {
            E("pat_alt("); emit_pat(e->left); E(","); emit_pat(e->right); E(")");
        } else {
            E("alt("); emit_expr(e->left); E(","); emit_expr(e->right); E(")");
        }
        break;

    /* capture nodes — in value context, evaluate the child */
    case E_NAM: emit_expr(e->left); break;
    case E_DOL:  emit_expr(e->left); break;

    case E_FNC:
        if (e->nargs == 0) {
            E("APPLY_fn(\"%s\",NULL,0)", e->sval);
        } else {
            E("APPLY_fn(\"%s\",(DESCR_t[]){", e->sval);
            for (int i=0; i<e->nargs; i++) {
                if (i) E(","); emit_expr(e->args[i]);
            }
            E("},%d)", e->nargs);
        }
        break;

    case E_ARY:
        E("aref(%s,(DESCR_t[]){", cs(e->sval));
        for (int i=0; i<e->nargs; i++) {
            if (i) E(","); emit_expr(e->args[i]);
        }
        E("},%d)", e->nargs);
        break;

    case E_IDX:
        /* postfix subscript: expr[i] — e.g. c(x)[i] */
        E("INDEX_fn("); emit_expr(e->left); E(",(DESCR_t[]){");
        for (int i=0; i<e->nargs; i++) {
            if (i) E(","); emit_expr(e->args[i]);
        }
        E("},%d)", e->nargs);
        break;

    case E_ATP:
        /* @var — cursor position capture: evaluates to cursor int */
        E("cursor_get(\"%s\")", e->sval);
        break;

    case E_ASGN:
        /* var = expr inside expression context */
        E("assign_expr(%s,", cs(e->left->sval)); emit_expr(e->right); E(")");
        break;
    }
}

/* ============================================================
 * Pattern expression emission → PATND_t*
 *
 * Same EXPR_t nodes, different routing:
 *   E_FNC  → pat_builtin or pat_call
 *   E_CONC → pat_cat
 *   E_OR   → pat_alt
 *   E_NAM  → pat_cond(child_pat, varname)
 *   E_DOL   → pat_imm(child_pat, varname)
 *   E_INDR → pat_ref(varname)   (deferred pattern reference *X)
 *   E_QLIT   → pat_lit(STRVAL_fn)
 *   E_VART   → pat_var(varname)   (pattern variable)
 * ============================================================ */
static void emit_pat(EXPR_t *e);

static void emit_pat(EXPR_t *e) {
    if (!e) { E("pat_epsilon()"); return; }
    switch (e->kind) {
    case E_QLIT:
        E("pat_lit("); emit_cstr(e->sval); E(")"); break;

    case E_VART:
        E("pat_var(\"%s\")", e->sval); break;

    case E_INDR:
        /* *X — deferred pattern reference */
        if (e->left && e->left->kind == E_VART)
            E("pat_ref(\"%s\")", e->left->sval);
        else if (e->left && e->left->kind == E_FNC && e->left->nargs >= 1
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

    case E_CONC:
        emit_chain_pretty(e, E_CONC, "pat_cat", emit_pat, 2);
        break;

    case E_MPY:
        /* pat * x — parsed as arithmetic multiply, but in pattern context
         * this is: left_pattern CONCAT_fn *right (deferred ref to right) */
        E("pat_cat("); emit_pat(e->left); E(",");
        if (e->right && e->right->kind == E_VART)
            E("pat_ref(\"%s\")", e->right->sval);
        else { E("pat_deref("); emit_expr(e->right); E(")"); }
        E(")"); break;

    case E_OPSYN:
        /* & in pattern context: reduce(left, right) — must fire at MATCH TIME.
         * reduce() calls EVAL("epsilon . *Reduce(t, n)") where n may contain
         * nTop() — which must be evaluated at MATCH_fn time, not build time.
         * Use pat_user_call to defer the call until the engine executes
         * this node during pattern matching. */
        E("pat_user_call(\"reduce\",(DESCR_t[]){"); emit_expr(e->left); E(","); emit_expr(e->right); E("},2)"); break;

    case E_OR:
        emit_chain_pretty(e, E_OR, "pat_alt", emit_pat, 2);
        break;

    case E_NAM: {
        /* pat . var */
        const char *varname = (e->right && e->right->kind==E_VART)
                              ? e->right->sval : "?";
        E("pat_cond("); emit_pat(e->left); E(",\"%s\")", varname); break;
    }
    case E_DOL: {
        /* pat $ var */
        const char *varname = (e->right && e->right->kind==E_VART)
                              ? e->right->sval : "?";
        E("pat_imm("); emit_pat(e->left); E(",\"%s\")", varname); break;
    }

    case E_FNC: {
        /* Route known builtins to pat_* */
        const char *n = e->sval;
        /* B0: zero-arg pattern; B1i: one int64_t arg; B1s: one string arg; B1v: one DESCR_t arg */
        #define B0(nm,fn)  if(strcasecmp(n,nm)==0){E(fn"()");break;}
        #define B1i(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(to_int(");emit_expr(e->args[0]);E("))");break;}
        #define B1s(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(VARVAL_fn(");emit_expr(e->args[0]);E("))");break;}
        #define B1v(nm,fn) if(strcasecmp(n,nm)==0&&e->nargs>=1){E(fn"(");emit_expr(e->args[0]);E(")");break;}
        B0("ARB","pat_arb")  B0("REM","pat_rem")
        B0("DT_FAIL","pat_fail") B0("ABORT","pat_abort")
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
         * fires at MATCH TIME (XATP materialisation), not at build time.
         * This correctly handles nPush()/nPop() side effects per MATCH_fn attempt,
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
             * They return a DESCR_t of type PATTERN — wrap with var_as_pattern.
             * reduce(t,n), shift(p,t), EVAL(s) → build-time call → APPLY_fn.
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
                E("var_as_pattern(APPLY_fn(\"%s\",(DESCR_t[]){", n);
                for (int i=0; i<e->nargs; i++) { if(i) E(","); emit_expr(e->args[i]); }
                E("},%d))", e->nargs);
            } else {
                E("pat_user_call(\"%s\",(DESCR_t[]){", n);
                for (int i=0; i<e->nargs; i++) { if(i) E(","); emit_expr(e->args[i]); }
                E("},%d)", e->nargs);
            }
        }
        break;
    }

    /* value nodes that shouldn't appear in pattern context — treat as var */
    case E_IDX:
    case E_ATP:
    case E_ASGN:
    default:
        E("pat_val("); emit_expr(e); E(")"); break;
    }
}

/* ============================================================
 * Emit lvalue assignment target
 * ============================================================ */

/* cur_fn_def and is_fn_local() are defined after FnDef (below) */
static int is_fn_local(const char *varname);

static void emit_assign_target(EXPR_t *lhs, const char *rhs_str) {
    if (!lhs) return;
    if (lhs->kind == E_VART) {
        E("set(%s, %s);\n", cs(lhs->sval), rhs_str);
        E("NV_SET_fn(\"%s\", %s);\n", lhs->sval, cs(lhs->sval)); /* all vars are natural/hashed */
    } else if (lhs->kind == E_ARY) {
        E("aset(%s,(DESCR_t[]){", cs(lhs->sval));
        for (int i=0; i<lhs->nargs; i++) {
            if (i) E(","); emit_expr(lhs->args[i]);
        }
        E("},%d,%s);\n", lhs->nargs, rhs_str);
    } else if (lhs->kind == E_KW) {
        E("kw_set(\"%s\",%s);\n", lhs->sval, rhs_str);
    } else if (lhs->kind == E_IDX) {
        /* v<i> = x  or  v[i] = x  →  aset(get(_v), {i}, nargs, x) */
        E("aset(");
        emit_expr(lhs->left);
        E(",(DESCR_t[]){");
        for (int i=0; i<lhs->nargs; i++) {
            if (i) E(","); emit_expr(lhs->args[i]);
        }
        E("},%d,%s);\n", lhs->nargs, rhs_str);
    } else if (lhs->kind == E_INDR) {
        E("iset("); emit_expr(lhs->right); E(",%s);\n", rhs_str);
    } else if (lhs->kind == E_FNC && lhs->nargs == 1) {
        /* field accessor lvalue: val(n) = x  →  FIELD_SET_fn(n, "val", x) */
        E("FIELD_SET_fn("); emit_expr(lhs->args[0]); E(", \"%s\", %s);\n", lhs->sval, rhs_str);
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
static void emit_computed_goto_inline(const char *label, const char *fn);
static int is_body_boundary(const char *label, const char *cur_fn);

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

/* Capture emit_goto_target output as a string (for use with PS/PL macros).
 * PG/PS macros expect just the goto *target* (without "goto " prefix),
 * because pretty_line col3 adds "goto " itself.
 * For "return ..." targets (trampoline mode), returns the full statement
 * with a leading "!" marker so callers can detect and use E() instead.
 * Returns heap-allocated string; caller must free(). */
static void emit_goto_target(const char *label, const char *fn);
static char *goto_target_str(const char *label, const char *fn) {
    char *buf = NULL; size_t sz = 0;
    FILE *tmp = open_memstream(&buf, &sz);
    FILE *saved = out; out = tmp;
    emit_goto_target(label, fn);
    out = saved; fclose(tmp);
    /* Strip leading "goto " — pretty_line adds it back in col3 */
    if (buf && strncmp(buf, "goto ", 5) == 0) {
        char *stripped = strdup(buf + 5);
        free(buf);
        return stripped;
    }
    /* Return statements or other non-goto fragments: return as-is.
     * Callers must use E("%s;\n", tgt) not PG(tgt) for these. */
    return buf; /* caller frees */
}

/* Emit a conditional or unconditional goto line in 3-column format.
 * cond: NULL/"" for unconditional; "if(_ok)" etc for conditional.
 * tgt: raw output of goto_target_str — either a label (use PG/PS)
 *      or a full "return ..." statement (use E). */
static void emit_pretty_goto(const char *tgt, const char *cond) {
    if (!tgt || !tgt[0]) return;
    int is_return   = (strncmp(tgt, "return", 6) == 0);
    int is_computed = (tgt[0] == '{');   /* computed-goto inline block */
    if (is_return || is_computed) {
        /* Can't put return/block in col3 — use E() raw */
        if (cond && cond[0]) E("    %s { %s; }\n", cond, tgt);
        else                  E("    %s;\n", tgt);
    } else {
        if (cond && cond[0]) PS(tgt, "%s", cond);
        else                  PG(tgt);
    }
}

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
        else if (strncasecmp(label,"$COMPUTED",9)==0) {
            /* Computed goto: $COMPUTED:expr_text
             * Re-parse the stored expression, emit runtime label lookup. */
            const char *expr_src = (label[9]==':') ? label+10 : NULL;
            if (expr_src && *expr_src) {
                EXPR_t *ce = parse_expr_from_str(expr_src);
                if (ce) {
                    E("{ const char *_cgoto_lbl = VARVAL_fn(");
                    emit_expr(ce);
                    E("); return sno_computed_goto(_cgoto_lbl); }");
                } else {
                    E("return (void*)_tramp_next_%d", cur_stmt_next_uid);
                }
            } else {
                E("return (void*)_tramp_next_%d", cur_stmt_next_uid);
            }
            return;
        }
        /* Cross-scope: fall through */
        if (label_is_in_fn_body(label, NULL) && !label_is_in_fn_body(label, fn)) {
            E("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        if (!in_main && !label_is_in_fn_body(label, fn)) {
            E("return (void*)_tramp_next_%d", cur_stmt_next_uid); return;
        }
        E("return (void*)block%s", cs_label(label));
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
    else if (strncasecmp(label,"$COMPUTED",9)==0 || strcasecmp(label,"_COMPUTED")==0) {
        /* Computed goto: delegate to helper defined after fn_table. */
        emit_computed_goto_inline(label, fn);
        return;
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
    if (!g) { char _nl[64]; snprintf(_nl,sizeof _nl,"_SNO_NEXT_%d",cur_stmt_next_uid); PG(_nl); return; }
    if (g->uncond) {
        char *tgt = goto_target_str(g->uncond, fn);
        emit_pretty_goto(tgt, NULL); free(tgt);
    } else {
        if (result_ok) {
            if (g->onsuccess) { char *tgt=goto_target_str(g->onsuccess,fn); emit_pretty_goto(tgt,"if(_ok)"); free(tgt); }
            if (g->onfailure) { char *tgt=goto_target_str(g->onfailure,fn); emit_pretty_goto(tgt,"if(!_ok)"); free(tgt); }
        } else {
            if (g->onsuccess) { char *tgt=goto_target_str(g->onsuccess,fn); emit_pretty_goto(tgt,NULL); free(tgt); }
            if (g->onfailure) { /* can't reach failure — skip */ }
        }
        char _nl[64]; snprintf(_nl,sizeof _nl,"_SNO_NEXT_%d",cur_stmt_next_uid); PG(_nl);
    }
}

/* ============================================================
 * Post-parse pattern-statement repair
 *
 * The grammar is LALR(1) and cannot always distinguish:
 *   subject pattern = replacement    (pattern MATCH_fn)
 *   subject_expr = replacement       (pure assignment)
 * when pattern primitives (LEN, POS, etc.) appear inside the subject_expr.
 * The lexer returns PAT_BUILTIN only at bstack_top==0, but PAT_BUILTIN IS
 * also in the `primary` grammar rule for value exprs, causing the parser to
 * absorb the pattern into the subject.
 *
 * This function detects the case and repairs the STMT_t in place.
 * It looks for: s->pattern==NULL, s->replacement==E_NULV, and the subject
 * tree contains a PAT_BUILTIN call in a position that looks like a pattern start.
 * ============================================================ */

static int is_pat_builtin_call(EXPR_t *e) {
    if (!e || e->kind != E_FNC) return 0;
    static const char *pb[] = {
        "LEN","POS","RPOS","TAB","RTAB","SPAN","BREAK",
        "NOTANY","ANY","ARB","REM","DT_FAIL","ABORT",
        "FENCE","SUCCEED","BAL","ARBNO", NULL
    };
    for (int i = 0; pb[i]; i++)
        if (strcasecmp(e->sval, pb[i]) == 0) return 1;
    return 0;
}

/* Returns 1 if expr e is a pattern node (E_FNC to pat_builtin, E_NAM capture,
 * E_OR, or E_CONC whose left child is a pattern). */
static int is_pat_node(EXPR_t *e) {
    if (!e) return 0;
    if (is_pat_builtin_call(e)) return 1;
    if (e->kind == E_NAM)   return 1;  /* .var capture */
    if (e->kind == E_OR)    return 1;  /* | alternation */
    if (e->kind == E_OPSYN) return 1;  /* & reduce() call — always pattern context */
    /* E_MPY(pat_node, x) — parsed from "pat *x" where * is multiplication token
     * but semantically is pattern-CONCAT_fn with deferred ref *x */
    if (e->kind == E_MPY && is_pat_node(e->left)) return 1;
    return 0;
}

/* Recursively checks if any node in e's subtree indicates pattern context.
 * Used to decide whether a pure assignment RHS should use emit_pat.
 * Indicators: E_INDR (*var — always a pattern ref), E_OPSYN (& — reduce()),
 * E_NAM (. capture), E_OR (| alternation in pattern context), E_FNC to
 * any pattern builtin including ARBNO/FENCE/etc. */
/* Returns 1 if the expression subtree rooted at e contains ANY pattern-valued
 * node.  Used by emit_expr to decide whether E_CONC / E_OR should be routed
 * through emit_pat (pat_cat / pat_alt) instead of the string path
 * (CONCAT_fn / alt).
 *
 * Key cases that are pattern-valued but NOT caught by is_pat_node:
 *   - E_INDR whose left child is E_VART — "*varname" deferred pattern ref
 *   - E_CONC or E_OR whose subtree contains any of the above
 */
int expr_contains_pattern(EXPR_t *e) {
    if (!e) return 0;
    if (is_pat_node(e)) return 1;
    /* *varname — deferred pattern ref (grammar: left=NULL, right=E_VART) */
    if (e->kind == E_INDR && e->right && e->right->kind == E_VART) return 1;
    if (e->kind == E_INDR && e->left  && e->left->kind  == E_VART) return 1;
    /* *varname(arg) — parser misparse deref+CONCAT_fn */
    if (e->kind == E_INDR && e->left && e->left->kind == E_FNC) return 1;
    /* recurse into children */
    if (e->kind == E_CONC || e->kind == E_OR || e->kind == E_MPY)
        return expr_contains_pattern(e->left) || expr_contains_pattern(e->right);
    /* $ and . operators — pattern may be on the left side */
    if (e->kind == E_DOL || e->kind == E_NAM)
        return expr_contains_pattern(e->left);
    if (e->kind == E_FNC) {
        /* ARBNO, FENCE, etc. already caught by is_pat_builtin_call above.
         * Also treat reduce/EVAL_fn calls as pattern-valued when inside CONCAT_fn. */
        if (e->sval && (strcasecmp(e->sval,"reduce")==0 || strcasecmp(e->sval,"EVAL_fn")==0))
            return 1;
        for (int i = 0; i < e->nargs; i++)
            if (expr_contains_pattern(e->args[i])) return 1;
    }
    return 0;
}

/* Walk the E_CONC left spine. When we find a right child that is_pat_node,
 * detach it and everything after it into the pattern.
 * Returns the extracted pattern root, or NULL if nothing found.
 * *subj_out is set to the remaining subject (may be the original expr if
 * no split needed).
 *
 * The tree is LEFT-ASSOCIATIVE CONCAT_fn:
 *   (((STRVAL_fn, POS(0)), ANY('abc')), E_NAM(letter))
 * We walk the left spine, looking for the first right child that is a pat node.
 * When found at depth D, the pattern is: right_at_D CONCAT_fn right_at_D-1 CONCAT_fn ... CONCAT_fn right_at_0
 * assembled left-to-right.
 */
typedef struct { EXPR_t *subj; EXPR_t *pat; } SplitResult;

static EXPR_t *make_concat(EXPR_t *left, EXPR_t *right) {
    if (!left)  return right;
    if (!right) return left;
    EXPR_t *e = expr_new(E_CONC);
    e->left = left; e->right = right;
    return e;
}

static SplitResult split_spine(EXPR_t *e) {
    /* Null or non-CONCAT_fn node that's a pure value: subject only */
    if (!e) { SplitResult r = {NULL, NULL}; return r; }

    if (e->kind != E_CONC) {
        if (is_pat_node(e)) {
            SplitResult r = {NULL, e}; return r;   /* entire node is pattern */
        } else {
            SplitResult r = {e, NULL}; return r;   /* entire node is subject */
        }
    }

    /* e = E_CONC(left, right) */
    /* First check if RIGHT is a pattern node (first pat on the right spine) */
    if (is_pat_node(e->right)) {
        /* Split here: left is subject, right and above become pattern */
        SplitResult inner = split_spine(e->left);
        /* inner.pat (if any) should be prepended, but since left was already
         * recursed and left's right IS the current pat... */
        /* Actually: the split is between left and right.
         * Subject = inner.subj (what was pure subject in e->left)
         * Pattern = inner.pat (any pattern found in e->left's right chain) CONCAT_fn e->right */
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

static EXPR_t *split_subject_pattern(EXPR_t *e, EXPR_t **subj_out) {
    if (!e) { *subj_out = NULL; return NULL; }
    SplitResult r = split_spine(e);
    *subj_out = r.subj;
    return r.pat;
}

/* Repair a misparsed pattern-MATCH_fn stmt.
 * Called when s->pattern==NULL and s->replacement is E_NULV (bare '=').
 * Also repairs pattern-MATCH_fn stmts with no replacement (s->replacement==NULL)
 * where the subject absorbed the pattern (no '=' present).
 * Returns 1 if the stmt was repaired. */
static int maybe_fix_pattern_stmt(STMT_t *s) {
    if (!s->subject) return 0;  /* no subject */
    /* Heuristic: if replacement is non-null non-E_NULV, this is a plain assignment,
     * not a pattern MATCH_fn. Skip. */
    if (s->replacement && s->replacement->kind != E_NULV) return 0;

    EXPR_t *new_subj = NULL;
    EXPR_t *new_pat  = split_subject_pattern(s->subject, &new_subj);

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
static int pat_is_anchored(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC && e->sval && strcasecmp(e->sval, "POS") == 0) {
        /* Only POS(0) literal anchors at start — no ARB needed.
         * Dynamic POS(N) needs ARB scan to advance cursor to position N. */
        if (e->nargs >= 1 && e->args[0]->kind == E_ILIT && e->args[0]->ival == 0)
            return 1;
        return 0;
    }
    if (e->kind == E_CONC) return pat_is_anchored(e->left);
    return 0;
}

/* ============================================================
 * emit_ok_goto — emit 3-column conditional :S/:F goto lines
 *
 * Replaces the repeated pattern:
 *   if (!s->go) { fallthrough; }
 *   else if (uncond) { emit_goto(g,fn,0); }
 *   else {
 *       if (onsuccess) PS(tgt, "if(_ok%d)", u);
 *       if (onfailure) PS(tgt, "if(!_ok%d)", u);
 *       fallthrough;
 *   }
 * Used by all three emit_stmt paths (pure-assign, pattern-match, expr-only).
 * ============================================================ */
static void emit_ok_goto(SnoGoto *g, const char *fn, int u) {
    char next_lbl[64];
    if (trampoline_mode)
        snprintf(next_lbl, sizeof next_lbl, "return (void*)_tramp_next_%d", cur_stmt_next_uid);
    else
        snprintf(next_lbl, sizeof next_lbl, "_SNO_NEXT_%d", cur_stmt_next_uid);

    if (!g) {
        emit_pretty_goto(next_lbl, NULL);
        return;
    }
    if (g->uncond) { emit_goto(g, fn, 0); return; }

    if (g->onsuccess) {
        char *tgt = goto_target_str(g->onsuccess, fn);
        char cond[32]; snprintf(cond, sizeof cond, "if(_ok%d)", u);
        emit_pretty_goto(tgt, cond);
        free(tgt);
    }
    if (g->onfailure) {
        char *tgt = goto_target_str(g->onfailure, fn);
        char cond[32]; snprintf(cond, sizeof cond, "if(!_ok%d)", u);
        emit_pretty_goto(tgt, cond);
        free(tgt);
    }
    emit_pretty_goto(next_lbl, NULL);
}

/* ============================================================
 * Emit one statement
 * ============================================================ */
static void emit_stmt(STMT_t *s, const char *fn) {
    /* Repair misparsed pattern-MATCH_fn stmts (grammar absorbs pattern into subject) */
    maybe_fix_pattern_stmt(s);

    E("/* line %d */\n", s->lineno);
    if (s->label) { char _sl[128]; snprintf(_sl, sizeof _sl, "_L%s", cs_label(s->label)); PLG(_sl, ""); }
    PS("", "trampoline_stno(%d);", s->lineno);

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
         * E_CONC becomes pat_cat and *var becomes pat_ref.
         * This handles: snoParse = nPush() ARBNO(*snoCommand) ... nPop() */
        if (expr_contains_pattern(s->replacement)) {
            int _col = fprintf(out, "DESCR_t _v%d = ", u);
            PP_PAT(s->replacement, _col); E(";\n");
        } else {
            int _col = fprintf(out, "DESCR_t _v%d = ", u);
            PP_EXPR(s->replacement, _col); E(";\n");
        }
        E("int _ok%d = !IS_FAIL_fn(_v%d);\n", u, u);
        E("if(_ok%d) {\n", u);
        char rhs[32]; snprintf(rhs,sizeof rhs,"_v%d",u);
        emit_assign_target_io(s->subject, rhs);
        E("}\n");
        /* emit goto using _ok%d for conditional :S/:F branches */
        emit_ok_goto(s->go, fn, u);
        return;
    }

    /* ---- null assign: subject = (empty RHS) — clears variable to null ---- */
    if (!s->pattern && !s->replacement && s->has_eq && s->subject) {
        int u=uid();
        E("{ /* null assign */\n");
        emit_assign_target_io(s->subject, "NULVCL");
        E("}\n");
        emit_ok_goto(s->go, fn, -1);
        return;
    }

    /* ---- pattern MATCH_fn: subject pattern [= replacement] ---- */
    /* Compiled Byrd box path — replaces pat_* / engine.c stopgap. */
    if (s->pattern) {
        int u = uid();
        E("/* byrd MATCH_fn u%d */\n", u);
        { int _col = fprintf(out, "DESCR_t _s%d = ", u); PP_EXPR(s->subject, _col); E(";\n"); }
        E("const char *_subj%d = VARVAL_fn(_s%d);\n", u, u);
        E("int64_t _slen%d = _subj%d ? (int64_t)strlen(_subj%d) : 0;\n", u, u, u);
        E("int64_t _cur%d  = 0;\n", u);
        E("int64_t _mstart%d = 0;\n", u);  /* cursor before MATCH_fn — for replacement */

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
        /* NOTE: _mstart is NOT set here — it is set by SNO_MSTART node after ARB scans */
        /* Checkpoint $'@S' (tree stack) before the pattern match.
         * On match failure, restore to undo any Shift/Reduce side-effects
         * (e.g. a zero-match ARBNO leaves a stray Parse tree on @S). */
        E("DESCR_t _stk_save_%d = NV_GET_fn(\"@S\");\n", u);

        /* SNOBOL4 pattern matching is a substring scan: wrap pattern in ARB
         * unless the leftmost node is POS() which anchors to a position. */
        EXPR_t *scan_pat = s->pattern;
        if (!pat_is_anchored(s->pattern)) {
            EXPR_t *arb = expr_new(E_FNC);
            arb->sval = strdup("ARB");
            arb->nargs = 0;

            /* SNO_MSTART: zero-width node that captures cursor into _mstart after ARB scans */
            EXPR_t *mstart_node = expr_new(E_FNC);
            mstart_node->sval = strdup("SNO_MSTART");
            mstart_node->nargs = 0;
            mstart_node->ival = u;  /* thread the statement uid so emit_byrd can name the var */

            EXPR_t *seq1 = expr_new(E_CONC);
            seq1->left  = arb;
            seq1->right = mstart_node;

            EXPR_t *seq = expr_new(E_CONC);
            seq->left  = seq1;
            seq->right = s->pattern;
            scan_pat = seq;
        }
        byrd_cond_reset();
        byrd_emit_pattern(scan_pat, out, root_lbl, sv, sl, cv, ok_lbl, fail_lbl);

        /* gamma: MATCH_fn succeeded */
        PLG(ok_lbl, "");
        PS("", "_ok%d = 1;", u);
        byrd_cond_emit_assigns(out, u);   /* flush pending . captures */
        if (s->replacement || s->has_eq) {
            /* Replace matched region [_mstart%d .. _cur%d) with replacement.
             * If has_eq but replacement==NULL, that is a null replacement — delete match. */
            E("{\n");
            if (s->replacement) {
                E("    DESCR_t _r%d = ", u); PP_EXPR(s->replacement, 4 + 10 + 3); E(";\n");
            } else {
                E("    DESCR_t _r%d = STRVAL_fn(\"\");\n", u);  /* null replacement = empty */
            }
            E("    const char *_rs%d = VARVAL_fn(_r%d);\n", u, u);
            E("    int64_t _rlen%d = _rs%d ? (int64_t)strlen(_rs%d) : 0;\n", u, u, u);
            E("    int64_t _tail%d = _slen%d - _cur%d;\n", u, u, u);
            E("    int64_t _newlen%d = _mstart%d + _rlen%d + _tail%d;\n", u, u, u, u);
            E("    char *_nb%d = (char*)GC_malloc(_newlen%d + 1);\n", u, u);
            E("    if (_mstart%d > 0) memcpy(_nb%d, _subj%d, (size_t)_mstart%d);\n", u, u, u, u);
            E("    if (_rlen%d  > 0) memcpy(_nb%d + _mstart%d, _rs%d, (size_t)_rlen%d);\n", u, u, u, u, u);
            E("    if (_tail%d  > 0) memcpy(_nb%d + _mstart%d + _rlen%d, _subj%d + _cur%d, (size_t)_tail%d);\n", u, u, u, u, u, u, u);
            E("    _nb%d[_newlen%d] = '\\0';\n", u, u);
            E("    _s%d = STRVAL(_nb%d);\n", u, u);
            /* write back to subject variable */
            if (s->subject && s->subject->kind == E_VART) {
                if (is_io_name(s->subject->sval))
                    E("    NV_SET_fn(\"%s\", _s%d);\n", s->subject->sval, u);
                else {
                    E("    set(%s, _s%d);\n", cs(s->subject->sval), u);
                    E("    NV_SET_fn(\"%s\", %s);\n", s->subject->sval, cs(s->subject->sval));
                }
            }
            E("}\n");
        }
        PG(done_lbl);

        /* omega: MATCH_fn failed — restore @S to pre-match state */
        PLG(fail_lbl, "");
        PS("", "NV_SET_fn(\"@S\", _stk_save_%d);", u);
        PS("", "_ok%d = 0;", u);

        PLG(done_lbl, "");

        /* emit goto using _ok%d */
        emit_ok_goto(s->go, fn, u);
        return;
    }

    /* ---- expression evaluation only ---- */
    {
        int u=uid();
        { int _col = fprintf(out, "DESCR_t _v%d = ", u); PP_EXPR(s->subject, _col); E(";\n"); }
        E("int _ok%d = !IS_FAIL_fn(_v%d);\n", u, u);
        /* emit goto using _ok%d */
        emit_ok_goto(s->go, fn, u);
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

static void collect_expr(EXPR_t *e) {
    if (!e) return;
    switch (e->kind) {
    case E_VART:   sym_add(e->sval); break;
    case E_ARY: sym_add(e->sval); break;
    default: break;
    }
    collect_expr(e->left);
    collect_expr(e->right);
    for (int i=0; i<e->nargs; i++) collect_expr(e->args[i]);
}

static void collect_stmt(STMT_t *s) {
    if (!s) return;
    collect_expr(s->subject);
    collect_expr(s->pattern);
    collect_expr(s->replacement);
}

static void collect_symbols(Program *prog) {
    sym_count = 0;
    for (STMT_t *s = prog->head; s; s = s->next)
        collect_stmt(s);
}

/* ============================================================
 * IO assignment routing
 * ============================================================ */

static void emit_assign_target_io(EXPR_t *lhs, const char *rhs_str) {
    if (lhs && lhs->kind == E_VART && is_io_name(lhs->sval)) {
        E("NV_SET_fn(\"%s\", %s);\n", lhs->sval, rhs_str);
        return;
    }
    emit_assign_target(lhs, rhs_str);
}

/* ============================================================
 * DEFINE_fn function table
 *
 * Pre-pass: scan for DEFINE_fn('fn(a,b)loc1,loc2') calls.
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
    STMT_t  *body_starts[BODY_MAX]; /* ALL entry points for this function */
    int    nbody_starts;
    STMT_t  *define_stmt;        /* the last DEFINE_fn(...) statement */
    char  *end_label;          /* label that ends the body (from DEFINE_fn's goto) */
    char  *entry_label;        /* explicit entry label from DEFINE_fn('proto','label') */
} FnDef;

static FnDef fn_table[FN_MAX];
static int   fn_count = 0;

/* emit_computed_goto_inline: emit a strcmp chain for $COMPUTED:expr in fn-body mode.
 * Called from emit_goto_target when trampoline_mode==0 and label starts with $COMPUTED. */
static void emit_computed_goto_inline(const char *label, const char *fn) {
    const char *expr_src = (strncasecmp(label,"$COMPUTED",9)==0 && label[9]==':')
                           ? label+10 : NULL;
    if (!expr_src || !*expr_src || !fn) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid);
        return;
    }
    /* Strip trailing ) and whitespace left by the off-by-one in parse.c capture */
    char expr_buf[4096];
    strncpy(expr_buf, expr_src, sizeof(expr_buf)-1);
    expr_buf[sizeof(expr_buf)-1] = '\0';
    int elen = (int)strlen(expr_buf);
    while (elen > 0 && (expr_buf[elen-1] == ')' || expr_buf[elen-1] == ' ' || expr_buf[elen-1] == '\t'))
        expr_buf[--elen] = '\0';
    EXPR_t *ce = parse_expr_from_str(expr_buf);
    if (!ce) {
        E("goto _SNO_NEXT_%d", cur_stmt_next_uid);
        return;
    }
    E("{ const char *_cg_raw = VARVAL_fn(");
    emit_expr(ce);
    E("); char _cg_buf[512]; size_t _cg_j=0;");
    E(" if(_cg_raw) { for(size_t _cg_i=0;_cg_raw[_cg_i]&&_cg_j<sizeof(_cg_buf)-1;_cg_i++)");
    E(" { if(_cg_raw[_cg_i]=='\\'' || _cg_raw[_cg_i]=='\"') continue; _cg_buf[_cg_j++]=_cg_raw[_cg_i]; } }");
    E(" _cg_buf[_cg_j]='\\0'; const char *_cg=_cg_buf;");
    E(" if(0){}");
    E(" if(0){}");
    for (int i = 0; i < fn_count; i++) {
        if (strcasecmp(fn_table[i].name, fn) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            STMT_t *bs = fn_table[i].body_starts[b];
            for (STMT_t *t = bs; t; t = t->next) {
                if (t->is_end) break;
                if (t != bs && is_body_boundary(t->label, fn_table[i].name)) break;
                if (t->label)
                    E(" else if(strcasecmp(_cg,\"%s\")==0) goto _L%s;",
                      t->label, cs_label(t->label));
            }
        }
        break;
    }
    E(" (void)_cg; }");
}

/* Returns 1 if 'name' is a user-defined function (present in fn_table) or a
 * known SNOBOL4 standard library function.  Used to distinguish CALL from
 * variable-concatenation-with-grouping: in SNOBOL4, nl('+') where nl is a
 * variable (not a function) means CONCAT(nl, '+'), not a function call. */
int is_defined_function(const char *name) {
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
        "ANY","ARB","ARBNO","BAL","DT_FAIL","ABORT","REM","SUCCEED",
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

/* Return fn INDEX_fn if label matches a known function entry, else -1 */
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

/* Return fn INDEX_fn if stmt is the DEFINE_fn(...) call for it, else -1 */
static int fn_by_define_stmt(STMT_t *s) {
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

/* Flatten a string-literal expression (E_QLIT or E_CONC chain of E_QLIT)
 * into a single static buffer.  Returns NULL if any node is not a string. */
static char _define_proto_buf[4096];
static const char *flatten_str_expr(EXPR_t *e) {
    if (!e) return NULL;
    if (e->kind == E_QLIT) return e->sval;
    if (e->kind == E_CONC) {
        const char *l = flatten_str_expr(e->left);
        const char *r = flatten_str_expr(e->right);
        if (!l || !r) return NULL;
        snprintf(_define_proto_buf, sizeof _define_proto_buf, "%s%s", l, r);
        return _define_proto_buf;
    }
    return NULL;
}

/* Check if a statement is DEFINE_fn('proto') or DEFINE_fn('proto' 'locals' ...)
 * The first argument may be a chain of concatenated string literals. */
static const char *stmt_define_proto(STMT_t *s) {
    if (!s->subject) return NULL;
    EXPR_t *e = s->subject;
    if (e->kind != E_FNC) return NULL;
    if (strcasecmp(e->sval,"DEFINE")!=0) return NULL;
    if (e->nargs < 1) return NULL;
    return flatten_str_expr(e->args[0]);
}

static void collect_functions(Program *prog) {
    fn_count = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        const char *proto = stmt_define_proto(s);
        if (!proto) continue;
        if (fn_count >= FN_MAX) break;
        FnDef *fn = &fn_table[fn_count];
        memset(fn, 0, sizeof *fn);
        parse_proto(proto, fn);
        fn->define_stmt = s;
        /* Extract entry label from 2nd DEFINE_fn arg: DEFINE_fn('proto','entry_label') */
        fn->entry_label = NULL;
        if (s->subject && s->subject->kind == E_FNC &&
            s->subject->nargs >= 2) {
            const char *el = flatten_str_expr(s->subject->args[1]);
            if (el && el[0]) fn->entry_label = strdup(el);
        }
        /* Extract end-of-body label from DEFINE_fn's goto.
         * Two forms:
         *   1. DEFINE_fn('fn()')  :(FnEnd)   -- goto on same statement
         *   2. DEFINE_fn('fn()')             -- goto on the NEXT standalone statement
         *      :(FnEnd)
         */
        fn->end_label = NULL;
        if (s->go) {
            if (s->go->uncond)   fn->end_label = strdup(s->go->uncond);
            else if (s->go->onsuccess) fn->end_label = strdup(s->go->onsuccess);
        }
        if (!fn->end_label && s->next) {
            STMT_t *nxt = s->next;
            /* A standalone goto: no label being defined here, no subject, just a goto */
            if (!nxt->subject && !nxt->pattern && !nxt->replacement
                    && nxt->go && nxt->go->uncond) {
                fn->end_label = strdup(nxt->go->uncond);
            }
        }
        /* Deduplicate: if this name already exists, overwrite it.
         * SNOBOL4 function names are case-sensitive — use strcmp, not strcasecmp.
         * e.g. "Pop(var)" (stack.sno) and "POP_fn()" (semantic.sno) are DIFFERENT. */
        int found = -1;
        for (int i=0; i<fn_count; i++)
            if (strcmp(fn_table[i].name, fn->name)==0) { found=i; break; }
        if (found >= 0) {
            /* Free old name/args/locals, REPLACE_fn with new definition */
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
        for (STMT_t *s = prog->head; s; s = s->next) {
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
 *   (a) another DEFINE_fn'd function's entry label, OR
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
static int stmt_in_fn_body(STMT_t *s, const char *fn_name) {
    for (int i = 0; i < fn_count; i++) {
        if (fn_name && strcasecmp(fn_table[i].name, fn_name) != 0) continue;
        for (int b = 0; b < fn_table[i].nbody_starts; b++) {
            STMT_t *bs = fn_table[i].body_starts[b];
            for (STMT_t *t = bs; t; t = t->next) {
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
            STMT_t *bs = fn_table[i].body_starts[b];
            for (STMT_t *t = bs; t; t = t->next) {
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
        E("static DESCR_t %s = {0};\n", cs(sym_table[i]));
    E("\n");
}

/* ============================================================
 * Emit one DEFINE_fn'd function
 * ============================================================ */
static void emit_fn(FnDef *fn, Program *prog) {
    /* Phantoms exist only as boundary markers — no C function to emit */
    if (!fn->define_stmt) return;
    (void)prog;
    lreg_reset();
    byrd_fn_scope_reset();   /* clear cross-pattern static-decl dedup for this fn */
    cur_fn_name = fn->name;
    cur_fn_def  = fn;
    E("static DESCR_t _sno_fn_%s(DESCR_t *_args, int _nargs) {\n", fn->name);

    /* CSNOBOL4 DEFF8/DEFF10/DEFF6 semantics: save caller's hash values on entry,
     * restore them on ALL exit paths (RETURN, FRETURN, abort/setjmp).
     * ALL SNOBOL4 variables are natural (hashed) — NEVER skip save/restore. */

    /* --- Save declarations (must come before setjmp to be in scope at restore labels) --- */
    for (int i = 0; i < fn->nargs; i++)
        E("    DESCR_t _saved_%s = NV_GET_fn(\"%s\"); /* save caller's hash value */\n",
          cs(fn->args[i]), fn->args[i]);
    for (int i = 0; i < fn->nlocals; i++)
        E("    DESCR_t _saved_%s = NV_GET_fn(\"%s\"); /* save caller's hash value */\n",
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
            E("    DESCR_t %s = {0}; /* return value */\n", cs(fn->name));
    }
    /* Declare C stack locals and install args into hash (DEFF8: save+assign) */
    for (int i = 0; i < fn->nargs; i++) {
        E("    DESCR_t %s = (_nargs>%d)?_args[%d]:NULVCL;\n",
          cs(fn->args[i]), i, i);
        E("    NV_SET_fn(\"%s\", %s); /* install arg in hash */\n",
          fn->args[i], cs(fn->args[i]));
    }
    /* Declare C stack locals and install as NULL into hash (DEFF10: save+null) */
    for (int i = 0; i < fn->nlocals; i++) {
        E("    DESCR_t %s = {0};\n", cs(fn->locals[i]));
        E("    NV_SET_fn(\"%s\", NULVCL); /* install local as null in hash */\n",
          fn->locals[i]);
    }
    E("\n");

    if (fn->nbody_starts == 0) {
        char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_RETURN_%s", fn->name);
        PG(_lbl);
    } else {
        STMT_t *bs = fn->body_starts[fn->nbody_starts - 1]; /* last DEFINE_fn wins */
        for (STMT_t *s = bs; s; s = s->next) {
            if (s->is_end) break;
            if (s != bs && is_body_boundary(s->label, fn->name)) break;
            cur_stmt_next_uid = uid();
            emit_stmt(s, fn->name);
            char _nl[64]; snprintf(_nl, sizeof _nl, "_SNO_NEXT_%d", cur_stmt_next_uid);
            PLG(_nl, "");
        }
        char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_RETURN_%s", fn->name);
        PG(_lbl);
    }

    /* --- RETURN path: restore caller's hash values (DEFF6: restore in reverse) --- */
    E("\n");
    { char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_RETURN_%s", fn->name); PLG(_lbl, ""); }
    E("    pop_abort_handler();\n");
    for (int i = fn->nlocals - 1; i >= 0; i--)
        E("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        E("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    E("    return get(%s);\n", cs(fn->name));

    /* --- FRETURN path: restore caller's hash values --- */
    { char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_FRETURN_%s", fn->name); PLG(_lbl, ""); }
    E("    pop_abort_handler();\n");
    for (int i = fn->nlocals - 1; i >= 0; i--)
        E("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        E("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    E("    return FAILDESCR;\n");

    /* --- ABORT path (setjmp fired): restore then return DT_FAIL --- */
    { char _lbl[128]; snprintf(_lbl, sizeof _lbl, "_SNO_ABORT_%s", fn->name); PLG(_lbl, ""); }
    for (int i = fn->nlocals - 1; i >= 0; i--)
        E("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->locals[i], cs(fn->locals[i]));
    for (int i = fn->nargs - 1; i >= 0; i--)
        E("    NV_SET_fn(\"%s\", _saved_%s); /* restore caller's value */\n",
          fn->args[i], cs(fn->args[i]));
    E("    return FAILDESCR;\n");

    E("}\n\n");
}

/* ============================================================
 * Emit forward declarations for all functions
 * ============================================================ */
static void emit_fn_forwards(void) {
    for (int i = 0; i < fn_count; i++)
        E("static DESCR_t _sno_fn_%s(DESCR_t *_args, int _nargs);\n",
          fn_table[i].name);
    E("\n");
}

/* Return 1 if stmt s lies within any real (non-phantom) function body */
static int stmt_is_in_any_fn_body(STMT_t *s) {
    return stmt_in_fn_body(s, NULL);
}

/* Return 1 if stmt s lies within a phantom function's body region.
 * Phantoms have body_starts populated after injection — reuse stmt_in_fn_body. */
static int stmt_in_phantom_body(STMT_t *s) {
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
    E("    INIT_fn();\n");
    /* Register all global C statics so NV_SET_fn() can sync them back.
     * This bridges the two-store gap: vars set via pattern conditional
     * assignment (. varname) or pre-INIT_fn write to the hash table only;
     * registration lets those writes also update the C statics. */
    for (int i = 0; i < sym_count; i++)
        E("    NV_REG_fn(\"%s\", &%s);\n", sym_table[i], cs(sym_table[i]));
    E("    NV_SYNC_fn(); /* pull pre-inited vars (nl,tab,etc) into C statics */\n");
    E("\n");

    /* Register all DEFINE_fn'd functions (skip phantoms — runtime-owned) */
    for (int i=0; i<fn_count; i++) {
        if (!fn_table[i].define_stmt) continue;  /* phantom — skip */
        /* Reconstruct the proto spec string: "name(a,b)loc1,loc2" */
        E("    DEFINE_fn(\"");
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
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* END stmt — emit the end label and stop */
        if (s->is_end) {
            E("\n");
            PLG("_SNO_END", "");
            E("    finish();\n");
            E("    return 0;\n");
            E("}\n");
            return;
        }
        /* Skip statements that live inside a function body */
        if (stmt_is_in_any_fn_body(s)) continue;
        /* Skip statements inside phantom (runtime-owned) function bodies */
        if (stmt_in_phantom_body(s)) continue;
        /* Skip the DEFINE_fn(...) call statements themselves */
        if (stmt_define_proto(s)) continue;

        cur_stmt_next_uid = uid();
        emit_stmt(s, "main");
        { char _nl[64]; snprintf(_nl, sizeof _nl, "_SNO_NEXT_%d", cur_stmt_next_uid); PLG(_nl, ""); }
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
 *   static DESCR_t globals...
 *   forward decls: block_X for every labeled stmt
 *   static void* stmt_N(void) { ...emit_stmt body... }  per stmt
 *   static void* block_L(void) { stmt sequence }        per labeled group
 *   int main(void) { SNO_INIT_fn(); trampoline_run(block_START); }
 * ============================================================ */

/* Collect all labels that appear on statements (block entry points) */
#define TRAMP_LABEL_MAX 4096
static char *tramp_labels[TRAMP_LABEL_MAX];
static int   tramp_nlabels = 0;

/* Collect all goto targets (may include undefined labels from library code) */
static char *tramp_goto_targets[TRAMP_LABEL_MAX];
static int   tramp_ngoto_targets = 0;

static int tramp_goto_target_known(const char *lbl) {
    for (int i = 0; i < tramp_ngoto_targets; i++)
        if (strcasecmp(tramp_goto_targets[i], lbl) == 0) return 1;
    return 0;
}

static void tramp_collect_labels(Program *prog) {
    tramp_nlabels = 0;
    tramp_ngoto_targets = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* Only collect top-level labels — skip labels inside DEFINE'd fn bodies.
         * Those are emitted as goto labels inside _sno_fn_X(), not as block fns. */
        if (s->label && tramp_nlabels < TRAMP_LABEL_MAX
                && !label_is_in_fn_body(s->label, NULL))
            tramp_labels[tramp_nlabels++] = s->label;
        /* Collect goto targets from S/F/uncond branches */
        if (s->go) {
            const char *tgts[3] = {s->go->onsuccess, s->go->onfailure, s->go->uncond};
            for (int t = 0; t < 3; t++) {
                const char *tgt = tgts[t];
                if (!tgt) continue;
                if (tgt[0] == '$') continue;  /* computed — skip */
                if (strcasecmp(tgt,"RETURN")==0 || strcasecmp(tgt,"FRETURN")==0) continue;
                if (strcasecmp(tgt,"END")==0 || strcasecmp(tgt,"START")==0) continue;
                if (tramp_goto_target_known(tgt)) continue;
                if (tramp_ngoto_targets < TRAMP_LABEL_MAX)
                    tramp_goto_targets[tramp_ngoto_targets++] = (char*)tgt;
            }
        }
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
        E("static DESCR_t %s = {0};\n", cs(sym_table[i]));
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
        if (strcasecmp(lbl,"START")==0) continue;  /* hardcoded above */
        E("static void *block%s(void);\n", cs_label(lbl));
    }
    /* Forward decls for undefined-label stubs */
    for (int i = 0; i < tramp_ngoto_targets; i++) {
        const char *tgt = tramp_goto_targets[i];
        if (tramp_has_label(tgt)) continue;
        E("static void *block%s(void);\n", cs_label(tgt));
    }
    E("\n");

    /* --- Pass 0a: pre-register ALL named pattern names FIRST ---
     * Must run before emit_fn so that *PatName inside DEFINE_fn bodies
     * (e.g. *SpecialNm in ss()) resolve to compiled functions, not
     * interpreter fallback.  Registration is just name→fnname mapping;
     * no C is emitted yet. */
    byrd_named_pat_reset();
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (!s->pattern && s->replacement &&
            s->subject && s->subject->kind == E_VART &&
            expr_contains_pattern(s->replacement)) {
            byrd_preregister_named_pattern(s->subject->sval);
        }
    }
    /* Emit struct typedecls and function fwdecls now — before emit_fn —
     * so DEFINE_fn function bodies can use pat_X_t types and call pat_X(). */
    byrd_emit_named_typedecls(out);
    byrd_emit_named_fwdecls(out);

    /* --- Emit DEFINE_fn'd functions using the existing emit_fn path ---
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

    /* --- Pass 0b/c/d: emit compiled named pattern functions ---
     * Names already pre-registered and typedecls/fwdecls already emitted
     * in pass 0a above (before emit_fn).
     * DO NOT call byrd_named_pat_reset() or re-emit typedecls here.
     */
    E("/* --- compiled named pattern function bodies --- */\n");

    /* 0d: emit function bodies (emitted flag prevents duplicates) */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) break;
        if (stmt_in_phantom_body(s)) continue;
        if (stmt_define_proto(s)) continue;
        if (!s->pattern && s->replacement &&
            s->subject && s->subject->kind == E_VART &&
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
    static STMT_t *sid_stmt[TRAMP_STMT_MAX];
    int stmt_count = 0;

    /* Pass 1: emit stmt_N() functions, record sid→uid mapping */
    for (STMT_t *s = prog->head; s; s = s->next) {
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
    const char *first_block_label = NULL; /* label on first stmt, needs alias */

    for (int sid = 1; sid <= stmt_count; sid++) {
        STMT_t *s = sid_stmt[sid];

        /* A labeled stmt closes the current block (falls through to new label) */
        if (s->label && block_open) {
            E("    return block%s; /* fall into next block */\n}\n\n",
              cs_label(s->label));
            block_open = 0;
        }

        /* Open a new block if none is open */
        if (!block_open) {
            if (first_block) {
                E("static void *block_START(void) {\n");
                first_block = 0;
                /* If first stmt carries a label, record it for alias emission */
                if (s->label) first_block_label = s->label;
                /* (alias emitted at block close — see first_block_label below) */
            } else if (s->label) {
                E("static void *block%s(void) {\n", cs_label(s->label));
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

    /* If first stmt had a label other than START, block_START absorbed it
     * but block_<label> was forward-declared. Emit a forwarding alias. */
    if (first_block_label && strcasecmp(first_block_label, "START") != 0) {
        E("static void *block%s(void) { return block_START(); }\n\n",
          cs_label(first_block_label));
    }

    E("static void *block_END(void) { return NULL; }\n\n");

    /* --- Stubs for undefined labels (e.g. 'err' from library code) --- */
    E("/* --- undefined label stubs --- */\n");
    for (int i = 0; i < tramp_ngoto_targets; i++) {
        const char *tgt = tramp_goto_targets[i];
        if (tramp_has_label(tgt)) continue;
        E("static void *block%s(void) { return NULL; }\n", cs_label(tgt));
    }
    E("\n");

    /* --- _block_label_table: label string -> block fn pointer --- */
    E("/* --- computed-goto label table --- */\n");
    E("_BlockEntry_t _block_label_table[] = {\n");
    E("    {\"START\", block_START},\n");
    E("    {\"END\",   block_END},\n");
    for (int i = 0; i < tramp_nlabels; i++) {
        const char *lbl = tramp_labels[i];
        if (strcasecmp(lbl,"END")==0) continue;
        if (strcasecmp(lbl,"START")==0) continue;  /* hardcoded above */
        E("    {\"%s\", block%s},\n", lbl, cs_label(lbl));
    }
    /* Add undefined-label stubs to the table too */
    for (int i = 0; i < tramp_ngoto_targets; i++) {
        const char *tgt = tramp_goto_targets[i];
        if (tramp_has_label(tgt)) continue;
        E("    {\"%s\", block%s},\n", tgt, cs_label(tgt));
    }
    E("};\n");
    E("int _block_label_count = (int)(sizeof(_block_label_table)/sizeof(_block_label_table[0]));\n\n");

    /* --- main --- */
    E("int main(void) {\n");
    E("    INIT_fn();\n");
    for (int i = 0; i < sym_count; i++)
        E("    NV_REG_fn(\"%s\", &%s);\n", sym_table[i], cs(sym_table[i]));
    E("    NV_SYNC_fn();\n\n");
    for (int i = 0; i < fn_count; i++) {
        if (!fn_table[i].define_stmt) continue;
        E("    DEFINE_fn(\"%s(", fn_table[i].name);
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
    /* Initialize well-known globals that SNOBOL4 programs assume are set
     * by the runtime but are not defined in beauty.sno's inc files.
     * nl = CHAR(10), tab = CHAR(9) */
    E("\n    /* runtime globals */\n");
    E("    NV_SET_fn(\"nl\",  APPLY_fn(\"CHAR\",(DESCR_t[]){INTVAL(10)},1));\n");
    E("    NV_SET_fn(\"tab\", APPLY_fn(\"CHAR\",(DESCR_t[]){INTVAL(9)},1));\n");
    /* Fix: DATA('tree(...)') and DATA('link(...)') land in dead code inside
     * _sno_fn_Top — tree.sno init block swallowed by StackEnd boundary.
     * Register explicitly here so tree()/link() are live before trampoline. */
    E("    /* DATA types from tree.sno/stack.sno (fn-body-walk bug) */\n");
    E("    APPLY_fn(\"DATA\",(DESCR_t[]){STRVAL(\"tree(t,v,n,c)\")},1);\n");
    E("    APPLY_fn(\"DATA\",(DESCR_t[]){STRVAL(\"link(next,value)\")},1);\n");
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
     * whose source body appears in the expanded -INCLUDE stream but whose DEFINE_fn
     * is handled by mock_includes.c at runtime (so collect_functions never sees it).
     *
     * Phantoms have name + end_label only. define_stmt = NULL, nbody_starts = 0.
     * They exist SOLELY so is_body_boundary() recognises their entry/end labels
     * as boundaries and stops body-absorption into the wrong C function.
     * emit_fn() skips phantoms (define_stmt == NULL → no C function emitted).
     * emit_main() skips phantoms (define_stmt == NULL → no DEFINE_fn() call).
     *
     * Source: mock_includes.c inc_init() + inc_init_extra() registrations
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
        /* Skip if already in fn_table (defined by SNOBOL4 DEFINE_fn in-stream).
         * Two-arg DEFINE_fn('nInc()', 'nInc_') stores name="nInc", entry_label="nInc_".
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
        ph->define_stmt = NULL;   /* phantom: no SNOBOL4 DEFINE_fn, no C emission */
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
        for (STMT_t *s = prog->head; s; s = s->next) {
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
