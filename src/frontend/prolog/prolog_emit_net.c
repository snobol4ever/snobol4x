/*
 * prolog_emit_net.c — Prolog IR → .NET CIL text emitter
 *
 * Consumes the Program* IR produced by prolog_lower() and emits Mono CIL
 * assembler text (.il files) assembled by ilasm.
 *
 * Subset handled (M-LINK-NET-5):
 *   E_CLAUSE head args:  E_QLIT (atom), E_VART (variable)
 *   E_CLAUSE body goals: E_FNC (predicate call), E_UNIFY (=/2)
 *   Multiple clauses per predicate — deterministic left-to-right, no β port.
 *   EXPORT directive → .method public static (Byrd-ABI wrapper).
 *   Non-exported predicates → .method private static.
 *
 * Full backtracking (β port, trail, choice-point stack) — future sprint.
 *
 * Byrd-box ABI (ARCH-scrip-abi.md §4.1):
 *   public static void PRED(object[] args, Action gamma, Action omega)
 *
 * Variable representation: object[] env — slot index = var slot from lower.
 * Atom representation: boxed string stored in env slot or on stack.
 *
 * References:
 *   prolog_emit_jvm.c  — JVM twin (structural oracle)
 *   emit_byrd_net.c    — SNOBOL4 NET emitter (CIL patterns)
 *   ARCH-scrip-abi.md  — ABI contract
 */

#include "sno2c.h"
#include "prolog_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------
 * Output state
 * ----------------------------------------------------------------------- */

static FILE   *pn_out     = NULL;
static char    pn_class[256];
static Program *pn_prog   = NULL;
static int     pn_uid     = 0;   /* unique label counter */
static char   *pn_decl_buf = NULL;  /* class-level decls buffered during method emit */
static size_t  pn_decl_len = 0;
static size_t  pn_decl_cap = 0;

static void N(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(pn_out, fmt, ap);
    va_end(ap);
}

/* D() — buffer a class-level declaration (field/helper method).
 * Flushed to pn_out before each emitted method via pn_flush_decls(). */
static void D(const char *fmt, ...) {
    char tmp[1024];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (pn_decl_len + (size_t)n + 1 > pn_decl_cap) {
        pn_decl_cap = (pn_decl_len + (size_t)n + 1) * 2 + 4096;
        pn_decl_buf = realloc(pn_decl_buf, pn_decl_cap);
    }
    memcpy(pn_decl_buf + pn_decl_len, tmp, (size_t)n);
    pn_decl_len += (size_t)n;
    pn_decl_buf[pn_decl_len] = '\0';
}

static void pn_flush_decls(void) {
    if (pn_decl_len > 0) {
        fwrite(pn_decl_buf, 1, pn_decl_len, pn_out);
        pn_decl_len = 0;
    }
}

/* -----------------------------------------------------------------------
 * Class name: bare basename (no LANG prefix — ARCH-scrip-abi.md §5)
 * ----------------------------------------------------------------------- */

static void pn_set_classname(const char *filename) {
    if (!filename) { strcpy(pn_class, "PrologProg"); return; }
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    char buf[256]; strncpy(buf, base, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    char *dot = strrchr(buf, '.'); if (dot) *dot = '\0';
    strncpy(pn_class, buf, sizeof pn_class - 1);
    pn_class[sizeof pn_class - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * Export predicate
 * ----------------------------------------------------------------------- */

static int pn_is_exported(const char *name) {
    for (ExportEntry *e = pn_prog->exports; e; e = e->next)
        if (strcasecmp(e->name, name) == 0) return 1;
    return 0;
}


/* -----------------------------------------------------------------------
 * Helpers: escape a string for CIL ldstr
 * ----------------------------------------------------------------------- */

static void pn_ldstr(const char *s) {
    N("    ldstr      \"");
    for (; s && *s; s++) {
        if      (*s == '\\') N("\\\\");
        else if (*s == '"')  N("\\\"");
        else if (*s == '\n') N("\\n");
        else                 fputc(*s, pn_out);
    }
    N("\"\n");
}

/* -----------------------------------------------------------------------
 * Emit unification of a head argument against an env slot.
 *
 * For atom heads (E_QLIT): compare env[slot] string to literal.
 *   If mismatch → branch to omega_lbl.
 * For variable heads (E_VART): bind env[var_slot] = env[arg_slot].
 *   (Deterministic — no trail needed for M-LINK-NET-5.)
 * ----------------------------------------------------------------------- */

static void pn_emit_head_unify(EXPR_t *arg, int arg_slot,
                                const char *omega_lbl, int n_vars) {
    if (!arg) return;
    int uid = pn_uid++;
    /* Prolog lower encodes atoms as E_FNC with zero children (zero-arity functor).
     * SNOBOL4 string literals use E_QLIT; handle both. */
    int is_atom = (arg->kind == E_QLIT) ||
                  (arg->kind == E_FNC && arg->nchildren == 0);
    if (is_atom) {
        /* env[arg_slot] must equal atom string */
        N("    ldloc.0\n");                        /* env array */
        N("    ldc.i4  %d\n", arg_slot);
        N("    ldelem.ref\n");
        N("    castclass [mscorlib]System.String\n");
        pn_ldstr(arg->sval ? arg->sval : "");
        N("    call       bool [mscorlib]System.String::op_Equality"
          "(string, string)\n");
        N("    brfalse    %s\n", omega_lbl);
    } else if (arg->kind == E_VART) {
        int vslot = (int)arg->ival;
        if (vslot < 0) return;  /* wildcard _ */
        /* bind: vars[vslot] = env[arg_slot] */
        N("    ldloc.1\n");                        /* vars array */
        N("    ldc.i4  %d\n", vslot);
        N("    ldloc.0\n");                        /* env array */
        N("    ldc.i4  %d\n", arg_slot);
        N("    ldelem.ref\n");
        N("    stelem.ref\n");
    }
    (void)uid; (void)n_vars;
}

/* -----------------------------------------------------------------------
 * Emit a body goal.
 * Returns 1 if it might fail (needs omega wiring), 0 if always succeeds.
 * ----------------------------------------------------------------------- */

static void pn_emit_goal(EXPR_t *goal, const char *omega_lbl, int n_vars);

static void pn_emit_goal(EXPR_t *goal, const char *omega_lbl, int n_vars) {
    if (!goal) return;

    if (goal->kind == E_UNIFY) {
        /* =/2: unify two terms — for M-LINK-NET-5 handle var=atom only */
        EXPR_t *lhs = expr_arg(goal, 0);
        EXPR_t *rhs = expr_arg(goal, 1);
        int uid = pn_uid++;
        int rhs_atom = rhs && (rhs->kind == E_QLIT || (rhs->kind == E_FNC && rhs->nchildren == 0));
        int lhs_atom = lhs && (lhs->kind == E_QLIT || (lhs->kind == E_FNC && lhs->nchildren == 0));
        if (lhs && lhs->kind == E_VART && rhs_atom) {
            /* X = atom: vars[slot] = atom */
            int slot = (int)lhs->ival;
            if (slot >= 0) {
                N("    ldloc.1\n");
                N("    ldc.i4  %d\n", slot);
                pn_ldstr(rhs->sval ? rhs->sval : "");
                N("    stelem.ref\n");
            }
        } else if (lhs_atom && rhs && rhs->kind == E_VART) {
            int slot = (int)rhs->ival;
            if (slot >= 0) {
                N("    ldloc.1\n");
                N("    ldc.i4  %d\n", slot);
                pn_ldstr(lhs->sval ? lhs->sval : "");
                N("    stelem.ref\n");
            }
        } else if (lhs && lhs->kind == E_VART && rhs && rhs->kind == E_VART) {
            /* X = Y: copy */
            int sl = (int)lhs->ival, sr = (int)rhs->ival;
            if (sl >= 0 && sr >= 0) {
                N("    ldloc.1\n"); N("    ldc.i4  %d\n", sl);
                N("    ldloc.1\n"); N("    ldc.i4  %d\n", sr);
                N("    ldelem.ref\n");
                N("    stelem.ref\n");
            }
        } else if (lhs_atom && rhs_atom) {
            /* atom = atom: fail if different */
            pn_ldstr(lhs->sval ? lhs->sval : "");
            pn_ldstr(rhs->sval ? rhs->sval : "");
            N("    call       bool [mscorlib]System.String::op_Equality"
              "(string, string)\n");
            N("    brfalse    %s\n", omega_lbl);
        }
        (void)uid;
        return;
    }

    if (goal->kind == E_FNC) {
        /* predicate call: functor/arity */
        const char *fn  = goal->sval ? goal->sval : "unknown";
        int         na  = expr_nargs(goal);
        int         uid = pn_uid++;
        char lbl_ok[64], lbl_done[64];
        snprintf(lbl_ok,   sizeof lbl_ok,   "Pg_ok_%d",   uid);
        snprintf(lbl_done, sizeof lbl_done,  "Pg_done_%d", uid);

        /* Build args array */
        N("    ldc.i4  %d\n", na);
        N("    newarr     [mscorlib]System.Object\n");
        for (int i = 0; i < na; i++) {
            EXPR_t *a = expr_arg(goal, i);
            N("    dup\n");
            N("    ldc.i4  %d\n", i);
            if (a && (a->kind == E_QLIT ||
                     (a->kind == E_FNC && a->nchildren == 0))) {
                pn_ldstr(a->sval ? a->sval : "");
            } else if (a && a->kind == E_VART) {
                int slot = (int)a->ival;
                if (slot >= 0) {
                    N("    ldloc.1\n");
                    N("    ldc.i4  %d\n", slot);
                    N("    ldelem.ref\n");
                } else {
                    N("    ldnull\n");
                }
            } else {
                N("    ldnull\n");
            }
            N("    stelem.ref\n");
        }

        /* gamma delegate: set flag=1 */
        char gname[64], oname[64], fname[64];
        snprintf(gname, sizeof gname, "pn_gamma_%d", uid);
        snprintf(oname, sizeof oname, "pn_omega_%d", uid);
        snprintf(fname, sizeof fname, "pn_flag_%d",  uid);
        /* Emit helpers inline (ilasm allows forward-referenced labels) */
        D("  .field private static int32 %s\n", fname);
        D("  .method private static void %s() cil managed\n", gname);
        D("  {\n    .maxstack 1\n    ldc.i4.1\n    stsfld int32 %s::%s\n    ret\n  }\n", pn_class, fname);
        D("  .method private static void %s() cil managed\n", oname);
        D("  {\n    .maxstack 1\n    ldc.i4.0\n    stsfld int32 %s::%s\n    ret\n  }\n", pn_class, fname);

        /* gamma Action */
        N("    ldnull\n");
        N("    ldftn      void %s::%s()\n", pn_class, gname);
        N("    newobj     instance void [mscorlib]System.Action::.ctor"
          "(object, native int)\n");
        /* omega Action */
        N("    ldnull\n");
        N("    ldftn      void %s::%s()\n", pn_class, oname);
        N("    newobj     instance void [mscorlib]System.Action::.ctor"
          "(object, native int)\n");

        /* call predicate — check if local or imported */
        /* For now: assume local predicate on same class */
        N("    call       void %s::%s("
          "object[], class [mscorlib]System.Action,"
          " class [mscorlib]System.Action)\n",
          pn_class, fn);

        /* check flag */
        N("    ldsfld     int32 %s::%s\n", pn_class, fname);
        N("    brfalse    %s\n", omega_lbl);
        return;
    }

    /* unknown goal kind — skip */
    (void)n_vars;
}

/* -----------------------------------------------------------------------
 * Emit one predicate (E_CHOICE node → one .method per clause, plus
 * a dispatcher method that tries clauses left-to-right).
 * ----------------------------------------------------------------------- */

static void pn_emit_predicate(STMT_t *stmt) {
    EXPR_t *choice = stmt->subject;
    if (!choice || choice->kind != E_CHOICE) return;

    /* functor/arity string e.g. "ancestor/2" */
    const char *fa    = choice->sval ? choice->sval : "unknown/0";
    int         ncl   = expr_nargs(choice);

    /* Parse functor and arity from "functor/arity" */
    char functor[128]; int arity = 0;
    {
        strncpy(functor, fa, sizeof functor - 1); functor[sizeof functor - 1] = '\0';
        char *slash = strchr(functor, '/');
        if (slash) { arity = atoi(slash + 1); *slash = '\0'; }
    }
    int n_args_pred = arity;  /* authoritative arity from functor/arity string */

    /* Upcase functor for method name */
    char method[128]; strncpy(method, functor, sizeof method - 1);
    method[sizeof method - 1] = '\0';
    for (char *p = method; *p; p++) *p = (char)toupper((unsigned char)*p);

    /* public if :- export declared, private otherwise */
    const char *vis = (pn_is_exported(functor) || pn_is_exported(method))
                      ? "public" : "private";
    const char *ACT = "class [mscorlib]System.Action";

    /* Emit one clause method per clause */
    for (int ci = 0; ci < ncl; ci++) {
        EXPR_t *clause = expr_arg(choice, ci);
        if (!clause || clause->kind != E_CLAUSE) continue;
        int n_vars = (int)clause->ival;
        int n_args = n_args_pred;

        char cl_name[128];
        snprintf(cl_name, sizeof cl_name, "pn_cl_%s_%d", functor, ci);
        char omega_lbl[64]; snprintf(omega_lbl, sizeof omega_lbl, "CL%d_omega", ci);

        N("  .method private static void %s("
          "object[] args, object[] vars, %s gamma, %s omega)"
          " cil managed\n", cl_name, ACT, ACT);
        N("  {\n");
        N("    .maxstack 8\n");
        N("    .locals init (object[] V_0, object[] V_1)\n");
        /* V_0 = args, V_1 = vars (fresh per clause) */
        N("    ldarg.0\n    stloc.0\n");
        /* allocate fresh var env */
        N("    ldc.i4  %d\n", n_vars > 0 ? n_vars : 1);
        N("    newarr     [mscorlib]System.Object\n");
        N("    stloc.1\n");

        /* Unify head args */
        for (int ai = 0; ai < n_args; ai++) {
            EXPR_t *ha = expr_arg(clause, ai);
            pn_emit_head_unify(ha, ai, omega_lbl, n_vars);
        }

        /* Emit body goals */
        int nbody = expr_nargs(clause) - n_args;
        for (int bi = 0; bi < nbody; bi++) {
            EXPR_t *goal = expr_arg(clause, n_args + bi);
            pn_emit_goal(goal, omega_lbl, n_vars);
        }

        /* Success: store result (last arg) in ByrdBoxLinkage.Result, then call gamma */
        if (n_args > 0) {
            /* Store vars[n_args-1] (the output variable) as Result */
            N("    ldloc.1\n");   /* vars array — bound variable values */
            N("    ldc.i4  %d\n", n_args - 1);
            N("    ldelem.ref\n");
            N("    castclass [mscorlib]System.String\n");
            N("    newobj     instance void [snobol4lib]DESCR::.ctor(string)\n");
            N("    stsfld     class [snobol4lib]DESCR [snobol4lib]ByrdBoxLinkage::Result\n");
        }
        N("    ldarg.2\n");
        N("    callvirt   instance void [mscorlib]System.Action::Invoke()\n");
        N("    ret\n");
        N("  %s:\n", omega_lbl);
        N("    ldarg.3\n");
        N("    callvirt   instance void [mscorlib]System.Action::Invoke()\n");
        N("    ret\n");
        N("  }\n\n");
    }

    /* Dispatcher: try clauses left-to-right */
    N("  .method %s static void %s(object[] args, %s gamma, %s omega)"
      " cil managed\n", vis, method, ACT, ACT);
    N("  {\n");
    N("    .maxstack 8\n");

    if (ncl == 0) {
        /* no clauses → always fail */
        N("    ldarg.2\n");
        N("    callvirt   instance void [mscorlib]System.Action::Invoke()\n");
        N("    ret\n");
    } else if (ncl == 1) {
        /* single clause: call directly with original gamma/omega */
        EXPR_t *cl = expr_arg(choice, 0);
        if (cl) {
            char cl_name[128];
            snprintf(cl_name, sizeof cl_name, "pn_cl_%s_0", functor);
            N("    ldarg.0\n    ldnull\n");
            N("    ldarg.1\n    ldarg.2\n");
            N("    call       void %s::%s("
              "object[], object[], %s, %s)\n",
              pn_class, cl_name, ACT, ACT);
        }
        N("    ret\n");
    } else {
        /* Multiple clauses: try left-to-right, jump to exit on first success */
        int exit_uid = pn_uid++;
        char exit_lbl[64]; snprintf(exit_lbl, sizeof exit_lbl, "Pd_exit_%d", exit_uid);
        for (int ci = 0; ci < ncl; ci++) {
            int uid = pn_uid++;
            char cl_name[128];
            snprintf(cl_name, sizeof cl_name, "pn_cl_%s_%d", functor, ci);
            char on[64], fn2[64];
            snprintf(on,  sizeof on,  "pd_omega_%d", uid);
            snprintf(fn2, sizeof fn2, "pd_flag_%d",  uid);

            D("  .field private static int32 %s\n", fn2);
            D("  .method private static void %s() cil managed\n", on);
            D("  {\n    .maxstack 1\n    ldc.i4.0\n    stsfld int32 %s::%s\n    ret\n  }\n", pn_class, fn2);

            /* try this clause: gamma = original gamma, omega = flag-setter */
            N("    ldarg.0\n    ldnull\n");
            N("    ldarg.1\n");   /* original gamma */
            N("    ldnull\n");
            N("    ldftn      void %s::%s()\n", pn_class, on);
            N("    newobj     instance void [mscorlib]System.Action::.ctor"
              "(object, native int)\n");
            N("    call       void %s::%s(object[], object[], %s, %s)\n",
              pn_class, cl_name, ACT, ACT);
            N("    ldsfld     int32 %s::%s\n", pn_class, fn2);
            /* flag=1: gamma was called (success) — gamma already invoked, just return */
            N("    brtrue     %s\n", exit_lbl);
            /* flag=0: omega was called (fail) — try next clause */
        }
        /* all clauses failed → call original omega */
        N("    ldarg.2\n");
        N("    callvirt   instance void [mscorlib]System.Action::Invoke()\n");
        N("  %s:\n", exit_lbl);
        N("    ret\n");
    }
    N("  }\n\n");

}

/* -----------------------------------------------------------------------
 * Header / footer
 * ----------------------------------------------------------------------- */

static void pn_emit_header(void) {
    N(".assembly extern mscorlib {}\n");
    N(".assembly extern snobol4lib {}\n");
    for (ImportEntry *ie = pn_prog->imports; ie; ie = ie->next)
        N(".assembly extern %s {}\n", ie->name);
    N(".assembly %s {}\n", pn_class);
    N(".module %s.dll\n\n", pn_class);
    N(".class public auto ansi beforefieldinit %s\n", pn_class);
    N("       extends [mscorlib]System.Object\n{\n\n");
}

static void pn_emit_footer(void) {
    N("} // end class %s\n", pn_class);
}

/* -----------------------------------------------------------------------
 * Entry point
 * ----------------------------------------------------------------------- */

void prolog_emit_net(Program *prog, FILE *out, const char *filename) {
    pn_out      = out;
    pn_prog     = prog;
    pn_uid      = 0;
    pn_decl_len = 0;
    pn_decl_cap = 0;
    pn_decl_buf = NULL;
    pn_set_classname(filename);

    pn_emit_header();

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE)
            pn_emit_predicate(s);
    }

    pn_flush_decls();
    pn_emit_footer();
    free(pn_decl_buf);
    pn_decl_buf = NULL;
}
