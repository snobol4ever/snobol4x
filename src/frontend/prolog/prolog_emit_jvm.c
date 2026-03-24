/*
 * prolog_emit_jvm.c — Prolog IR → Jasmin .j emitter
 *
 * Consumes E_CHOICE/E_CLAUSE/E_UNIFY/E_CUT/E_TRAIL_MARK/E_TRAIL_UNWIND
 * nodes produced by prolog_lower() and emits a Jasmin assembly file.
 *
 * Entry point: prolog_emit_jvm(Program *prog, FILE *out, const char *filename)
 * Called from driver/main.c when -pl -jvm flags are set.
 *
 * Design: mirrors emit_byrd_jvm.c (SNOBOL4 JVM backend).
 * Four-port Byrd box: α (try) / β (retry) / γ (succeed) / ω (fail).
 * Terms encoded as Object[] on the JVM heap (see FRONTEND-PROLOG-JVM.md).
 *
 * Sprint: PJ-S1 (M-PJ-SCAFFOLD, M-PJ-HELLO)
 */

#include "sno2c.h"
#include "prolog_atom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Output state
 * ------------------------------------------------------------------------- */

static FILE *pj_out = NULL;
static char  pj_classname[256];

/* -------------------------------------------------------------------------
 * Output helpers — identical pattern to emit_byrd_jvm.c
 * ------------------------------------------------------------------------- */

static void J(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(pj_out, fmt, ap);
    va_end(ap);
}

static void JI(const char *instr, const char *ops) {
    if (ops && ops[0])
        J("    %s %s\n", instr, ops);
    else
        J("    %s\n", instr);
}

static void JL(const char *label, const char *instr, const char *ops) {
    if (label && label[0]) J("%s:\n", label);
    JI(instr, ops);
}

static void JC(const char *comment) {
    J("; %s\n", comment);
}

static void JSep(const char *tag) {
    J("; === %s ", tag ? tag : "");
    int used = 7 + (tag ? (int)strlen(tag) + 1 : 0);
    for (int i = used; i < 72; i++) fputc('=', pj_out);
    J("\n");
}

/* -------------------------------------------------------------------------
 * Class name derivation (mirrors jvm_set_classname)
 * ------------------------------------------------------------------------- */

static void pj_set_classname(const char *filename) {
    if (!filename || strcmp(filename, "<stdin>") == 0) {
        strcpy(pj_classname, "PrologProg");
        return;
    }
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    char buf[256];
    strncpy(buf, base, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    char *dot = strrchr(buf, '.');
    if (dot) *dot = '\0';
    char *p = buf;
    if (!isalpha((unsigned char)*p) && *p != '_') *p = '_';
    for (; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    buf[0] = (char)toupper((unsigned char)buf[0]);
    strncpy(pj_classname, buf, sizeof pj_classname - 1);
}

/* pj_safe_name — sanitize a Prolog functor/atom for use as a Jasmin label */
static void pj_safe_name(const char *src, char *dst, int dstlen) {
    int di = 0;
    for (int si = 0; src[si] && di < dstlen - 1; si++) {
        char c = src[si];
        if (isalnum((unsigned char)c) || c == '_')
            dst[di++] = (char)tolower((unsigned char)c);
        else
            dst[di++] = '_';
    }
    dst[di] = '\0';
}

/* -------------------------------------------------------------------------
 * Atom table — collect atoms during emission, emit as static String fields
 * ------------------------------------------------------------------------- */

#define PJ_MAX_ATOMS 512
static char *pj_atoms[PJ_MAX_ATOMS];
static int   pj_natoms = 0;

static int pj_intern_atom(const char *name) {
    for (int i = 0; i < pj_natoms; i++)
        if (strcmp(pj_atoms[i], name) == 0) return i;
    if (pj_natoms >= PJ_MAX_ATOMS) return 0;
    pj_atoms[pj_natoms] = strdup(name);
    return pj_natoms++;
}

/* -------------------------------------------------------------------------
 * Label counter
 * ------------------------------------------------------------------------- */

static int pj_label_counter = 0;
static int pj_fresh_label(void) { return pj_label_counter++; }

/* -------------------------------------------------------------------------
 * Runtime helper emission
 *
 * All emitted as static methods on the class — same pattern as sno_arith etc.
 * in emit_byrd_jvm.c.
 *
 * Term encoding:
 *   Object[] of length >= 2:
 *     [0] = String tag: "atom","int","float","var","compound","ref"
 *     [1] = value (String for atom/int/float, Object[] for compound/ref)
 *   null = unbound variable slot
 * ------------------------------------------------------------------------- */

static void pj_emit_runtime_helpers(void) {
    JSep("Runtime helpers");

    /* Trail is declared in class header and initialized in <clinit>.
     * Runtime helpers: trail_mark, trail_push, trail_unwind, deref, unify, term constructors, write. */

    /* trail_mark()I — return current trail size */
    J(".method static pj_trail_mark()I\n");
    J("    .limit stack 2\n");
    J("    .limit locals 0\n");
    J("    getstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("ireturn", "");
    J(".end method\n\n");

    /* trail_push(Object[] slot_holder, int slot_idx) — push binding to undo
     * We encode as Object[]{slot_holder, Integer(slot_idx)} on trail */
    /* Simpler approach: trail stores the variable cell Object[] directly.
     * On unwind, set cell[1] = null to unbind. */
    J(".method static pj_trail_push([Ljava/lang/Object;)V\n");
    J("    .limit stack 4\n");
    J("    .limit locals 1\n");
    J("    getstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    JI("aload_0", "");
    JI("invokevirtual", "java/util/ArrayList/add(Ljava/lang/Object;)Z");
    JI("pop", "");
    JI("return", "");
    J(".end method\n\n");

    /* trail_unwind(int mark) — undo all bindings down to mark */
    J(".method static pj_trail_unwind(I)V\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    /* local 0 = mark (I), local 1 = i (I) */
    /* while trail.size() > mark: pop last, set [1]=null */
    J("pj_unwind_loop:\n");
    J("    getstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("iload_0", "");
    JI("if_icmple", "pj_unwind_done");
    /* get last element */
    J("    getstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    J("    getstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("iconst_1", "");
    JI("isub", "");
    JI("invokevirtual", "java/util/ArrayList/remove(I)Ljava/lang/Object;");
    /* cast to Object[] and restore both slots: [0]="var", [1]=null */
    JI("checkcast", "[Ljava/lang/Object;");
    /* restore [0] = "var" */
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"var\"");
    JI("aastore", "");
    /* restore [1] = null */
    JI("iconst_1", "");
    JI("aconst_null", "");
    JI("aastore", "");
    JI("goto", "pj_unwind_loop");
    J("pj_unwind_done:\n");
    JI("return", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * deref(Object) — dereference chain
     * ------------------------------------------------------------------ */
    J(".method static pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n");
    J("    .limit stack 3\n");
    J("    .limit locals 2\n");
    JI("aload_0", "");
    JI("astore_1", "");
    J("pj_deref_loop:\n");
    JI("aload_1", "");
    JI("ifnull", "pj_deref_done");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"ref\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_deref_done");
    /* it's a ref: follow [1] */
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("astore_1", "");
    JI("goto", "pj_deref_loop");
    J("pj_deref_done:\n");
    JI("aload_1", "");
    JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * term_atom(String name) — allocate atom term Object[]{tag,name}
     * ------------------------------------------------------------------ */
    J(".method static pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n");
    J("    .limit stack 5\n");
    J("    .limit locals 1\n");
    JI("iconst_2", "");
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"atom\"");
    JI("aastore", "");
    JI("dup", "");
    JI("iconst_1", "");
    JI("aload_0", "");
    JI("aastore", "");
    JI("areturn", "");
    J(".end method\n\n");

    /* term_int(long) — allocate integer term (encode as String like SNOBOL4 JVM) */
    J(".method static pj_term_int(J)[Ljava/lang/Object;\n");
    J("    .limit stack 5\n");
    J("    .limit locals 2\n");
    JI("iconst_2", "");
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"int\"");
    JI("aastore", "");
    JI("dup", "");
    JI("iconst_1", "");
    JI("lload_0", "");
    JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
    JI("aastore", "");
    JI("areturn", "");
    J(".end method\n\n");

    /* term_var() — allocate unbound variable cell: Object[]{"var", null} */
    J(".method static pj_term_var()[Ljava/lang/Object;\n");
    J("    .limit stack 4\n");
    J("    .limit locals 0\n");
    JI("iconst_2", "");
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"var\"");
    JI("aastore", "");
    /* [1] stays null = unbound */
    JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * unify(Object a, Object b) — WAM-style; returns boolean
     * ------------------------------------------------------------------ */
    J(".method static pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 6\n");
    J("    .limit locals 4\n");
    /* deref both */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_1", "");
    /* if a==b (same reference), succeed */
    JI("aload_0", "");
    JI("aload_1", "");
    JI("if_acmpeq", "pj_unify_true");
    /* if a is unbound var (tag=="var", [1]==null): bind a→b */
    JI("aload_0", "");
    JI("ifnull", "pj_unify_check_b_var");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_check_b_var");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("ifnonnull", "pj_unify_check_b_var");
    /* a is unbound: bind */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    /* change tag to ref */
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"ref\"");
    JI("aastore", "");
    /* set [1] = b */
    JI("iconst_1", "");
    JI("aload_1", "");
    JI("aastore", "");
    /* trail the cell */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    J("    invokestatic %s/pj_trail_push([Ljava/lang/Object;)V\n", pj_classname);
    JI("goto", "pj_unify_true");
    /* check if b is unbound var */
    J("pj_unify_check_b_var:\n");
    JI("aload_1", "");
    JI("ifnull", "pj_unify_check_atoms");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_check_atoms");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("ifnonnull", "pj_unify_check_atoms");
    /* b is unbound: bind b→a */
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"ref\"");
    JI("aastore", "");
    JI("iconst_1", "");
    JI("aload_0", "");
    JI("aastore", "");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    J("    invokestatic %s/pj_trail_push([Ljava/lang/Object;)V\n", pj_classname);
    JI("goto", "pj_unify_true");
    /* check atom==atom */
    J("pj_unify_check_atoms:\n");
    JI("aload_0", "");
    JI("ifnull", "pj_unify_false");
    JI("aload_1", "");
    JI("ifnull", "pj_unify_false");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_check_int");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    /* both atoms: compare names */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    JI("goto", "pj_unify_true");
    /* check int==int */
    J("pj_unify_check_int:\n");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_check_compound");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_check_compound");
    JI("goto", "pj_unify_true");
    /* check compound==compound: same functor, same arity, recurse on args
     * Flat encoding: [0]="compound",[1]=functor,[2..]=args; length=2+arity */
    J("pj_unify_check_compound:\n");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"compound\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"compound\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    /* check same functor [1] */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    /* check same arity: array length must match */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("if_icmpne", "pj_unify_false");
    /* recurse on args: local 2=arity (arr.length-2), local 3=i */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("iconst_2", "");
    JI("isub", "");
    JI("istore_2", "");   /* local 2 = arity */
    JI("iconst_0", "");
    JI("istore_3", "");   /* local 3 = i = 0 */
    J("pj_unify_cmp_loop:\n");
    JI("iload_3", "");
    JI("iload_2", "");
    JI("if_icmpge", "pj_unify_true");   /* all args matched */
    /* unify a.args[i] with b.args[i] */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iload_3", "");
    JI("iconst_2", "");
    JI("iadd", "");
    JI("aaload", "");          /* a.args[i] */
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iload_3", "");
    JI("iconst_2", "");
    JI("iadd", "");
    JI("aaload", "");          /* b.args[i] */
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ifeq", "pj_unify_false");
    JI("iinc", "3 1");         /* i++ */
    JI("goto", "pj_unify_cmp_loop");
    J("pj_unify_true:\n");
    JI("iconst_1", "");
    JI("ireturn", "");
    J("pj_unify_false:\n");
    JI("iconst_0", "");
    JI("ireturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * write_term(Object) — write/1 builtin
     * ------------------------------------------------------------------ */
    J(".method static pj_write(Ljava/lang/Object;)V\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    /* deref first */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    /* null = unbound var, print "_" */
    JI("aload_0", "");
    JI("ifnonnull", "pj_write_notnull");
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("ldc", "\"_\"");
    JI("invokevirtual", "java/io/PrintStream/print(Ljava/lang/String;)V");
    JI("return", "");
    J("pj_write_notnull:\n");
    /* check tag */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("astore_1", "");
    /* atom or int → print [1] as string */
    JI("aload_1", "");
    JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pj_write_scalar");
    JI("aload_1", "");
    JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pj_write_scalar");
    JI("aload_1", "");
    JI("ldc", "\"float\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pj_write_scalar");
    /* var or ref: print tag[0]...[1] recursively — simplified: print atom name or "_" */
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("ldc", "\"_\"");
    JI("invokevirtual", "java/io/PrintStream/print(Ljava/lang/String;)V");
    JI("return", "");
    J("pj_write_scalar:\n");
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");  /* verifier: Object → String */
    JI("invokevirtual", "java/io/PrintStream/print(Ljava/lang/String;)V");
    JI("return", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Class-level scaffold: header + <clinit>
 * ------------------------------------------------------------------------- */

static void pj_emit_class_header(void) {
    J(".class public %s\n", pj_classname);
    J(".super java/lang/Object\n\n");
    JC("Trail field");
    J(".field static pj_trail Ljava/util/ArrayList;\n\n");

    /* <clinit> — init trail */
    J(".method static <clinit>()V\n");
    J("    .limit stack 3\n");
    J("    .limit locals 0\n");
    JI("new", "java/util/ArrayList");
    JI("dup", "");
    JI("invokespecial", "java/util/ArrayList/<init>()V");
    J("    putstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    JI("return", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Forward declarations for goal/term emitters
 * ------------------------------------------------------------------------- */

static void pj_emit_goal(EXPR_t *goal, const char *lbl_gamma, const char *lbl_omega,
                         int trail_local, int *var_locals, int n_vars);
static void pj_emit_term(EXPR_t *term, int *var_locals, int n_vars);
static void pj_emit_arith(EXPR_t *e, int *var_locals, int n_vars);

/* -------------------------------------------------------------------------
 * Term emitter — leaves Object[] on JVM stack
 * Locals layout (per-clause method):
 *   local 0..n_args-1  = head args (Object[])
 *   local n_args..n_vars-1 = body vars (Object[])
 *   local n_vars       = trail mark (I)
 * var_locals[slot] = JVM local index for that Prolog variable
 * ------------------------------------------------------------------------- */

static void pj_emit_term(EXPR_t *e, int *var_locals, int n_vars) {
    if (!e) { JI("aconst_null", ""); return; }
    switch (e->kind) {
    case E_QLIT: {
        /* atom — push atom term */
        pj_intern_atom(e->sval ? e->sval : "");
        J("    ldc \"%s\"\n", e->sval ? e->sval : "");
        J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n",
          pj_classname);
        break;
    }
    case E_ILIT: {
        J("    ldc2_w %ld\n", e->ival);
        J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
        break;
    }
    case E_VART: {
        int slot = e->ival;
        if (slot >= 0 && slot < n_vars && var_locals) {
            J("    aload %d\n", var_locals[slot]);
        } else {
            /* anonymous wildcard _ (slot=-1): allocate fresh unbound var cell.
             * aconst_null would break unification — every _ must be a distinct
             * fresh variable so it unifies with anything without binding anything
             * the caller cares about. */
            J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
        }
        break;
    }
    case E_FNC: {
        /* compound term f(arg0, arg1, ...) — flat Object[] encoding:
         * [0]="compound", [1]=functor_string, [2..2+arity-1]=args */
        int arity = e->nchildren;
        if (arity == 0) {
            /* arity-0 compound = atom */
            J("    ldc \"%s\"\n", e->sval ? e->sval : "");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n",
              pj_classname);
        } else {
            /* allocate array of size 2+arity */
            J("    bipush %d\n", 2 + arity);
            JI("anewarray", "java/lang/Object");
            /* [0] = "compound" */
            JI("dup", "");
            JI("iconst_0", "");
            JI("ldc", "\"compound\"");
            JI("aastore", "");
            /* [1] = functor string */
            JI("dup", "");
            JI("iconst_1", "");
            J("    ldc \"%s\"\n", e->sval ? e->sval : "");
            JI("aastore", "");
            /* [2..] = args */
            for (int ai = 0; ai < arity; ai++) {
                JI("dup", "");
                J("    bipush %d\n", 2 + ai);
                pj_emit_term(e->children[ai], var_locals, n_vars);
                JI("aastore", "");
            }
        }
        break;
    }
    default:
        JI("aconst_null", "");
        break;
    }
}

/* -------------------------------------------------------------------------
 * Arithmetic emitter for is/2 RHS — mirrors emit_byrd_jvm.c arith pattern
 * Leaves long on stack.
 * ------------------------------------------------------------------------- */

static void pj_emit_arith(EXPR_t *e, int *var_locals, int n_vars) {
    if (!e) { JI("lconst_0", ""); return; }
    switch (e->kind) {
    case E_ILIT:
        J("    ldc2_w %ld\n", e->ival);
        break;
    case E_VART: {
        int slot = e->ival;
        /* load var, deref, get [1] (string), parse to long */
        if (slot >= 0 && slot < n_vars && var_locals)
            J("    aload %d\n", var_locals[slot]);
        else
            JI("aconst_null", "");
        J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
        JI("checkcast", "[Ljava/lang/Object;");
        JI("iconst_1", "");
        JI("aaload", "");
        JI("checkcast", "java/lang/String");
        JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        break;
    }
    case E_ADD:
        pj_emit_arith(e->children[0], var_locals, n_vars);
        pj_emit_arith(e->children[1], var_locals, n_vars);
        JI("ladd", "");
        break;
    case E_SUB:
        pj_emit_arith(e->children[0], var_locals, n_vars);
        pj_emit_arith(e->children[1], var_locals, n_vars);
        JI("lsub", "");
        break;
    case E_MPY:
        pj_emit_arith(e->children[0], var_locals, n_vars);
        pj_emit_arith(e->children[1], var_locals, n_vars);
        JI("lmul", "");
        break;
    case E_DIV:
        pj_emit_arith(e->children[0], var_locals, n_vars);
        pj_emit_arith(e->children[1], var_locals, n_vars);
        JI("ldiv", "");
        break;
    default:
        JI("lconst_0", "");
        break;
    }
}

/* -------------------------------------------------------------------------
 * pj_is_user_call — true for user-defined predicates needing retry loop
 * ------------------------------------------------------------------------- */
static int pj_is_user_call(EXPR_t *goal) {
    if (!goal || goal->kind != E_FNC || !goal->sval) return 0;
    const char *fn = goal->sval;
    static const char *builtins[] = {
        "true","fail","halt","nl","write","writeln","print","tab","is",
        "<",">","=<",">=","=:=","=\\=","=","\\=","==","\\==",
        "@<","@>","@=<","@>=",
        "var","nonvar","atom","integer","float","compound","atomic","is_list",
        "functor","arg","=..","\\+","not",",",";","->",
        NULL
    };
    for (int i = 0; builtins[i]; i++)
        if (strcmp(fn, builtins[i]) == 0) return 0;
    return 1;
}

/* -------------------------------------------------------------------------
 * Goal emitter
 * lbl_gamma = label to goto on goal success
 * lbl_omega  = label to goto on goal failure
 * trail_local = JVM local index of trail mark (I)
 * var_locals  = JVM local indices for Prolog variables
 * ------------------------------------------------------------------------- */

/* forward decl for pj_emit_body */
static void pj_emit_body(EXPR_t **goals, int ngoals, const char *lbl_gamma,
                         const char *lbl_omega, const char *lbl_outer_omega,
                         int trail_local,
                         int *var_locals, int n_vars, int *next_local,
                         int init_cs_local, int sub_cs_out_local);

static void pj_emit_goal(EXPR_t *goal, const char *lbl_gamma, const char *lbl_omega,
                         int trail_local, int *var_locals, int n_vars) {
    if (!goal) { JI("goto", lbl_gamma); return; }

    if (goal->kind == E_CUT) {
        /* cut: succeed immediately, caller seals β */
        JI("goto", lbl_gamma);
        return;
    }

    if (goal->kind == E_UNIFY) {
        /* =/2: unify children[0] and children[1] */
        pj_emit_term(goal->children[0], var_locals, n_vars);
        pj_emit_term(goal->children[1], var_locals, n_vars);
        J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
        J("    ifeq %s\n", lbl_omega);
        JI("goto", lbl_gamma);
        return;
    }

    if (goal->kind == E_FNC) {
        const char *fn = goal->sval ? goal->sval : "";
        int nargs = goal->nchildren;

        /* write/1 */
        if (strcmp(fn, "write") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_write(Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_gamma);
            return;
        }
        /* nl/0 */
        if (strcmp(fn, "nl") == 0 && nargs == 0) {
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            JI("invokevirtual", "java/io/PrintStream/println()V");
            JI("goto", lbl_gamma);
            return;
        }
        /* writeln/1 */
        if (strcmp(fn, "writeln") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_write(Ljava/lang/Object;)V\n", pj_classname);
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            JI("invokevirtual", "java/io/PrintStream/println()V");
            JI("goto", lbl_gamma);
            return;
        }
        /* true/0 */
        if (strcmp(fn, "true") == 0 && nargs == 0) {
            JI("goto", lbl_gamma);
            return;
        }
        /* fail/0 */
        if (strcmp(fn, "fail") == 0 && nargs == 0) {
            JI("goto", lbl_omega);
            return;
        }
        /* halt/0, halt/1 */
        if (strcmp(fn, "halt") == 0) {
            if (nargs == 1) {
                /* evaluate arg as int */
                pj_emit_arith(goal->children[0], var_locals, n_vars);
                JI("l2i", "");
            } else {
                JI("iconst_0", "");
            }
            JI("invokestatic", "java/lang/System/exit(I)V");
            JI("goto", lbl_gamma);
            return;
        }
        /* is/2 — arithmetic */
        if (strcmp(fn, "is") == 0 && nargs == 2) {
            /* evaluate RHS, create int term, unify with LHS */
            pj_emit_arith(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            /* swap: stack = [int_term, lhs_term] — need (lhs, rhs) for unify */
            JI("swap", "");
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_omega);
            JI("goto", lbl_gamma);
            return;
        }
        /* comparison builtins =:= < > =< >= =\= */
        if ((strcmp(fn, "=:=") == 0 || strcmp(fn, "=\\=") == 0 ||
             strcmp(fn, "<")   == 0 || strcmp(fn, ">")    == 0 ||
             strcmp(fn, "=<")  == 0 || strcmp(fn, ">=")   == 0) && nargs == 2) {
            pj_emit_arith(goal->children[0], var_locals, n_vars);
            pj_emit_arith(goal->children[1], var_locals, n_vars);
            JI("lcmp", "");
            /* result: -1, 0, +1 */
            if (strcmp(fn, "=:=") == 0)      J("    ifne %s\n", lbl_omega);
            else if (strcmp(fn, "=\\=") == 0) J("    ifeq %s\n", lbl_omega);
            else if (strcmp(fn, "<") == 0)    J("    ifge %s\n", lbl_omega);
            else if (strcmp(fn, ">") == 0)    J("    ifle %s\n", lbl_omega);
            else if (strcmp(fn, "=<") == 0)   J("    ifgt %s\n", lbl_omega);
            else if (strcmp(fn, ">=") == 0)   J("    iflt %s\n", lbl_omega);
            JI("goto", lbl_gamma);
            return;
        }
        /* ,/2 (now n-ary after lowering) — conjunction.
         * All children are conjuncts; pass directly to pj_emit_body. */
        if (strcmp(fn, ",") == 0 && nargs >= 1) {
            int next_local_tmp = trail_local + 1 + n_vars + 8;
            int ics = next_local_tmp++; /* fresh init_cs = 0 */
            int sco = next_local_tmp++; /* fresh sub_cs_out */
            /* These locals don't exist in the JVM frame yet; we use 0 constants */
            /* emit iconst_0 into dummy locals to satisfy the interface */
            JI("iconst_0", ""); J("    istore %d\n", ics);
            JI("iconst_0", ""); J("    istore %d\n", sco);
            pj_emit_body(goal->children, nargs, lbl_gamma, lbl_omega, lbl_omega,
                         trail_local, var_locals, n_vars, &next_local_tmp,
                         ics, sco);
            return;
        }
        /* ;/2 (now n-ary after lowering) — disjunction: try each branch.
         * Special case: if first child is ->/2, it's an if-then-else:
         *   (Cond -> Then ; Else)  ==> branch on Cond; take Then or Else.
         * On success goto lbl_gamma; on failure of all branches goto lbl_omega. */
        if (strcmp(fn, ";") == 0 && nargs >= 2) {
            /* check for (Cond -> Then) as first branch = if-then-else */
            EXPR_t *first = goal->children[0];
            if (first && first->kind == E_FNC && first->sval &&
                strcmp(first->sval, "->") == 0 && first->nchildren >= 2) {
                /* if-then-else: Cond -> Then ; Else1 ; Else2 ...
                 * -> is flat n-ary: children[0]=Cond, children[1..]=Then goals */
                int uid = pj_fresh_label();
                char cond_ok[128], cond_fail[128], done_lbl[128];
                snprintf(cond_ok,   sizeof cond_ok,   "ite%d_ok",   uid);
                snprintf(cond_fail, sizeof cond_fail, "ite%d_else", uid);
                snprintf(done_lbl,  sizeof done_lbl,  "ite%d_done", uid);
                /* emit condition — success falls through to cond_ok, failure jumps to else */
                pj_emit_goal(first->children[0], cond_ok, cond_fail,
                             trail_local, var_locals, n_vars);
                J("%s:\n", cond_ok);
                /* emit Then goals: children[1..nchildren-1] as flat sequence */
                int nthen = first->nchildren - 1;
                for (int ti = 0; ti < nthen; ti++) {
                    char tstep_g[128], tstep_o[128];
                    snprintf(tstep_g, sizeof tstep_g, "ite%d_t%d_g", uid, ti);
                    snprintf(tstep_o, sizeof tstep_o, "ite%d_t%d_o", uid, ti);
                    const char *step_gamma = (ti == nthen - 1) ? lbl_gamma : tstep_g;
                    pj_emit_goal(first->children[1 + ti], step_gamma, lbl_omega,
                                 trail_local, var_locals, n_vars);
                    if (ti < nthen - 1) J("%s:\n", tstep_g);
                }
                J("    goto %s\n", done_lbl);
                J("%s:\n", cond_fail);
                /* emit remaining else branches */
                for (int bi = 1; bi < nargs; bi++) {
                    char next_lbl[128];
                    snprintf(next_lbl, sizeof next_lbl, "ite%d_alt%d", uid, bi + 1);
                    const char *fail_to = (bi < nargs - 1) ? next_lbl : lbl_omega;
                    pj_emit_goal(goal->children[bi], lbl_gamma, fail_to,
                                 trail_local, var_locals, n_vars);
                    if (bi < nargs - 1) {
                        J("    goto %s\n", done_lbl);
                        J("%s:\n", next_lbl);
                    }
                }
                J("%s:\n", done_lbl);
                return;
            }
            /* plain disjunction: try each branch in sequence */
            int uid = pj_fresh_label();
            char done_lbl[128];
            snprintf(done_lbl, sizeof done_lbl, "disj%d_done", uid);
            for (int bi = 0; bi < nargs; bi++) {
                char next_lbl[128];
                snprintf(next_lbl, sizeof next_lbl, "disj%d_alt%d", uid, bi + 1);
                const char *fail_to = (bi < nargs - 1) ? next_lbl : lbl_omega;
                pj_emit_goal(goal->children[bi], lbl_gamma, fail_to,
                             trail_local, var_locals, n_vars);
                if (bi < nargs - 1) {
                    J("    goto %s\n", done_lbl);
                    J("%s:\n", next_lbl);
                }
            }
            J("%s:\n", done_lbl);
            return;
        }
        /* -> (if-then, no else): Cond -> Then1, Then2, ...
         * Flat n-ary: children[0]=Cond, children[1..]=Then goals.
         * Condition success → emit each Then step in sequence.
         * Condition failure → lbl_omega. */
        if (strcmp(fn, "->") == 0 && nargs >= 2) {
            int uid = pj_fresh_label();
            char cond_ok[128], cond_fail[128];
            snprintf(cond_ok,   sizeof cond_ok,   "ifthen%d_ok",   uid);
            snprintf(cond_fail, sizeof cond_fail,  "ifthen%d_fail", uid);
            pj_emit_goal(goal->children[0], cond_ok, cond_fail, trail_local, var_locals, n_vars);
            J("%s:\n", cond_ok);
            int nthen = nargs - 1;
            for (int ti = 0; ti < nthen; ti++) {
                char tstep_g[128];
                snprintf(tstep_g, sizeof tstep_g, "ifthen%d_t%d_g", uid, ti);
                const char *step_gamma = (ti == nthen - 1) ? lbl_gamma : tstep_g;
                pj_emit_goal(goal->children[1 + ti], step_gamma, lbl_omega,
                             trail_local, var_locals, n_vars);
                if (ti < nthen - 1) J("%s:\n", tstep_g);
            }
            J("%s:\n", cond_fail);
            JI("goto", lbl_omega);
            return;
        }
        /* user-defined predicate call — deterministic single-solution call.
         * For backtracking calls in a clause body, pj_emit_body() wraps
         * this in a retry loop (Proebsting E2.fail→E1.resume). */
        {
            char safe[256]; pj_safe_name(fn, safe, sizeof safe);
            char desc[512];
            char argpart[256]; argpart[0] = '\0';
            for (int i = 0; i < nargs; i++) strcat(argpart, "[Ljava/lang/Object;");
            snprintf(desc, sizeof desc, "%s/p_%s_%d(%sI)[Ljava/lang/Object;",
                     pj_classname, safe, nargs, argpart);
            /* push args */
            for (int i = 0; i < nargs && i < goal->nchildren; i++)
                pj_emit_term(goal->children[i], var_locals, n_vars);
            /* push continuation index 0 (first try) */
            JI("iconst_0", "");
            J("    invokestatic %s\n", desc);
            JI("ifnull", lbl_omega);
            JI("goto", lbl_gamma);
            return;
        }
    }

    /* fallthrough */
    JI("goto", lbl_gamma);
}

/* -------------------------------------------------------------------------
 * pj_emit_body — emit N goals with Proebsting retry wiring.
 *
 * User calls: wrapped in a retry loop (E2.fail -> E1.resume).
 * Deterministic goals: linear recursive chain.
 * next_local: pointer to next available JVM local slot (bumped on alloc).
 * ------------------------------------------------------------------------- */
static void pj_emit_body(EXPR_t **goals, int ngoals, const char *lbl_gamma,
                         const char *lbl_omega, const char *lbl_outer_omega,
                         int trail_local,
                         int *var_locals, int n_vars, int *next_local,
                         int init_cs_local, int sub_cs_out_local) {
    if (ngoals == 0) { JI("goto", lbl_gamma); return; }

    EXPR_t *g = goals[0];

    if (!g || g->kind == E_TRAIL_MARK || g->kind == E_TRAIL_UNWIND) {
        pj_emit_body(goals + 1, ngoals - 1, lbl_gamma, lbl_omega, lbl_outer_omega,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local);
        return;
    }

    if (g->kind == E_CUT) {
        JI("goto", lbl_gamma);
        return;
    }

    if (pj_is_user_call(g)) {
        const char *fn = g->sval;
        int nargs = g->nchildren;
        char safe[256]; pj_safe_name(fn, safe, sizeof safe);
        char desc[512], argpart[256]; argpart[0] = '\0';
        for (int j = 0; j < nargs; j++) strcat(argpart, "[Ljava/lang/Object;");
        snprintf(desc, sizeof desc, "%s/p_%s_%d(%sI)[Ljava/lang/Object;",
                 pj_classname, safe, nargs, argpart);

        int uid      = pj_fresh_label();
        int local_cs = (*next_local)++;
        int local_rv = (*next_local)++;

        char call_try[128], call_omega[128];
        snprintf(call_try,   sizeof call_try,   "call%d_try",   uid);
        snprintf(call_omega, sizeof call_omega, "call%d_omega", uid);

        /* init: local_cs = init_cs_local */
        J("    iload %d\n", init_cs_local);
        J("    istore %d\n", local_cs);

        J("%s:\n", call_try);
        for (int j = 0; j < nargs && j < g->nchildren; j++)
            pj_emit_term(g->children[j], var_locals, n_vars);
        J("    iload %d\n", local_cs);
        J("    invokestatic %s\n", desc);
        JI("dup", "");
        J("    astore %d\n", local_rv);
        J("    ifnull %s\n", call_omega);
        /* extract returned cs */
        J("    aload %d\n", local_rv);
        JI("iconst_0", "");
        JI("aaload", "");
        JI("checkcast", "java/lang/Integer");
        JI("invokevirtual", "java/lang/Integer/intValue()I");
        JI("dup", "");
        J("    istore %d\n", sub_cs_out_local);
        JI("iconst_1", "");
        JI("iadd", "");
        J("    istore %d\n", local_cs);
        /* suffix: failure saves local_cs → init_cs_local, goto lbl_omega (retry_head) */
        {
            int fresh_init = (*next_local)++;
            JI("iconst_0", ""); J("    istore %d\n", fresh_init);
            int fresh_sub = (*next_local)++;
            JI("iconst_0", ""); J("    istore %d\n", fresh_sub);
            char suffix_fail[128];
            snprintf(suffix_fail, sizeof suffix_fail, "call%d_sfail", uid);
            pj_emit_body(goals + 1, ngoals - 1, lbl_gamma, suffix_fail, lbl_outer_omega,
                         trail_local, var_locals, n_vars, next_local,
                         fresh_init, fresh_sub);
            J("%s:\n", suffix_fail);
            /* save updated cs into init_cs_local for retry_head re-entry */
            J("    iload %d\n", local_cs);
            J("    istore %d\n", init_cs_local);
            JI("goto", lbl_omega);   /* → retry_head_lbl */
        }
        /* ucall exhausted → outer omega (next clause) */
        J("%s:\n", call_omega);
        JI("goto", lbl_outer_omega);
        return;
    }

    /* Deterministic goal */
    {
        int uid = pj_fresh_label();
        char g_gamma[128], g_omega[128];
        snprintf(g_gamma, sizeof g_gamma, "dg%d_g", uid);
        snprintf(g_omega, sizeof g_omega, "dg%d_o", uid);
        pj_emit_goal(g, g_gamma, g_omega, trail_local, var_locals, n_vars);
        J("%s:\n", g_gamma);
        pj_emit_body(goals + 1, ngoals - 1, lbl_gamma, lbl_omega, lbl_outer_omega,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local);
        J("%s:\n", g_omega);
        JI("goto", lbl_omega);
    }
}

/* -------------------------------------------------------------------------
 * Clause emitter
 * Emits one clause block inside the predicate method (switch case).
 * Layout: p_functor_arity(arg0..argN-1, cs) — cs = continuation state
 * Returns Object[] = args array on success, null on failure.
 * ------------------------------------------------------------------------- */

static void pj_emit_choice(EXPR_t *choice) {
    if (!choice || choice->kind != E_CHOICE) return;
    const char *functor = choice->sval ? choice->sval : "unknown";
    int nclauses = choice->nchildren;
    /* parse arity from sval "name/arity" */
    int arity = 0;
    const char *sl = functor ? strrchr(functor, '/') : NULL;
    if (sl) arity = atoi(sl + 1);
    char safe_fn[256];
    char name_only[256];
    if (sl) {
        int nlen = (int)(sl - functor);
        strncpy(name_only, functor, nlen); name_only[nlen] = '\0';
    } else {
        strncpy(name_only, functor, sizeof name_only - 1);
    }
    pj_safe_name(name_only, safe_fn, sizeof safe_fn);

    JSep(functor);
    JC("Predicate method: p_FUNCTOR_ARITY(args..., cs) -> Object[]|null");
    JC("cs = continuation state: 0=first try, N=retry from clause N");

    /* method signature: static [Ljava/lang/Object; p_fn_arity(arg0.., cs I) */
    J(".method static p_%s_%d(", safe_fn, arity);
    for (int i = 0; i < arity; i++) J("[Ljava/lang/Object;");
    J("I)[Ljava/lang/Object;\n");

    /* compute max locals needed:
     * 0..arity-1: args
     * arity: cs
     * arity+1: trail mark (per clause) */
    int max_vars = 0;
    for (int ci = 0; ci < nclauses; ci++) {
        EXPR_t *cl = choice->children[ci];
        if (cl && cl->kind == E_CLAUSE) {
            int nv = (int)cl->ival;  /* n_vars stored directly on E_CLAUSE */
            if (nv > max_vars) max_vars = nv;
        }
    }
    /* -----------------------------------------------------------------
     * base[] computation — mirrors emit_byrd_asm.c base[] logic.
     * Fact/builtin-only clause: 1 slot → base[ci+1] = base[ci] + 1.
     * Body-ucall clause: open range, same stride (cs encodes sub_cs offset
     * within the range, which is [base[ci], base[ci+1]) = open-ended for
     * the last clause with a ucall; the sub_cs from the inner call fills
     * the gap and the next entry is base[ci] + sub_cs + 1).
     * Dispatch: linear scan from last clause to first — jge base[ci] → clause.
     * ----------------------------------------------------------------- */
    if (nclauses > 64) nclauses = 64;
    int base[64];
    base[0] = 0;
    /* helper: does clause ci have any body user-calls? */
    for (int ci = 0; ci < nclauses - 1; ci++) {
        EXPR_t *ec = choice->children[ci];
        int nv_args = ec ? (int)ec->dval : 0;
        int nb = ec ? (int)ec->nchildren - nv_args : 0;
        if (nb < 0) nb = 0;
        int has_ucall = 0;
        for (int bi = 0; bi < nb && !has_ucall; bi++) {
            EXPR_t *g = ec->children[nv_args + bi];
            if (g && pj_is_user_call(g)) has_ucall = 1;
        }
        base[ci + 1] = base[ci] + 1;
        (void)has_ucall; /* stride is 1 regardless; sub_cs fills the gap */
    }
    /* ω_base: value >= which cs is exhausted (only matters if last clause is fact) */
    {
        EXPR_t *last = choice->children[nclauses - 1];
        int nv_args_last = last ? (int)last->dval : 0;
        int nb_last = last ? (int)last->nchildren - nv_args_last : 0;
        if (nb_last < 0) nb_last = 0;
        int last_has_ucall = 0;
        for (int bi = 0; bi < nb_last && !last_has_ucall; bi++) {
            EXPR_t *g = last->children[nv_args_last + bi];
            if (g && pj_is_user_call(g)) last_has_ucall = 1;
        }
        /* ω_base only needed for the dispatch guard on last fact clause */
        (void)last_has_ucall;
    }

    /* locals layout:
     *   0..arity-1          : args
     *   arity               : cs (parameter)
     *   arity+1             : trail_mark (I)
     *   arity+2             : init_cs (sub_cs extracted from incoming cs) (I)
     *   arity+3             : sub_cs_out (accumulated from body ucalls) (I)
     *   arity+4..arity+3+max_vars : var slots
     *   arity+4+max_vars..  : retry loop temporaries
     */
    int cs_local      = arity;
    int trail_local   = arity + 1;
    int init_cs_local = arity + 2;
    int sub_cs_out_local = arity + 3;
    int vars_base     = arity + 4;
    int locals_needed = vars_base + max_vars + 32;

    J("    .limit stack 16\n");
    J("    .limit locals %d\n", locals_needed);

    /* dispatch: linear scan from last clause down.
     * cs >= base[ci] → enter clause ci.
     * Mirrors ASM: cmp eax, base[ci]; jge pl_name_cN_α */
    J("    iload %d\n", cs_local);
    J("    istore %d\n", init_cs_local);   /* will be refined per clause below */
    /* Linear scan from last to first */
    for (int ci = nclauses - 1; ci >= 0; ci--)
        J("    iload %d ; cs >= %d? → clause%d\n    ldc %d\n    if_icmpge p_%s_%d_clause%d\n",
          cs_local, base[ci], ci, base[ci], safe_fn, arity, ci);
    J("    goto p_%s_%d_omega\n", safe_fn, arity);

    /* emit each clause block */
    for (int ci = 0; ci < nclauses; ci++) {
        EXPR_t *clause = choice->children[ci];
        if (!clause || clause->kind != E_CLAUSE) continue;

        int n_vars = (int)clause->ival;
        int n_args = (int)clause->dval;

        /* var_locals: map slot → JVM local */
        int *var_locals = calloc(n_vars + 1, sizeof(int));
        for (int vi = 0; vi < n_vars; vi++)
            var_locals[vi] = vars_base + vi;

        char alpha_lbl[128], omega_lbl[128];
        snprintf(alpha_lbl, sizeof alpha_lbl, "p_%s_%d_clause%d", safe_fn, arity, ci);
        if (ci + 1 < nclauses)
            snprintf(omega_lbl, sizeof omega_lbl, "p_%s_%d_clause%d", safe_fn, arity, ci + 1);
        else
            snprintf(omega_lbl, sizeof omega_lbl, "p_%s_%d_omega", safe_fn, arity);

        /* α port */
        J("%s:\n", alpha_lbl);
        /* Compute init_cs = cs - base[ci].  This is the sub-cs to resume
         * the first body user-call at; 0 means fresh entry. */
        J("    iload %d\n", cs_local);
        J("    ldc %d\n", base[ci]);
        JI("isub", "");
        J("    istore %d\n", init_cs_local);
        /* Reset sub_cs_out to 0 */
        JI("iconst_0", "");
        J("    istore %d\n", sub_cs_out_local);

        /* trail mark */
        J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
        J("    istore %d\n", trail_local);

        /* allocate variable cells */
        for (int vi = 0; vi < n_vars; vi++) {
            if (vi < n_args) {
                J("    aload %d\n", vi);
                J("    astore %d\n", var_locals[vi]);
            } else {
                J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", var_locals[vi]);
            }
        }

        /* head unification */
        for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
            EXPR_t *head_term = clause->children[ai];
            if (!head_term) continue;
            if (head_term->kind == E_VART) continue;
            J("    aload %d\n", ai);
            pj_emit_term(head_term, var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq p_%s_%d_head_fail_%d\n", safe_fn, arity, ci);
        }
        J("    goto p_%s_%d_body_%d\n", safe_fn, arity, ci);
        J("p_%s_%d_head_fail_%d:\n", safe_fn, arity, ci);
        J("    iload %d\n", trail_local);
        J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
        J("    goto %s\n", omega_lbl);

        /* retry_head label: on β-retry from body ucall, unwind clause
         * trail mark and redo var alloc + head unification with updated cs.
         * This correctly handles body-only vars that got bound during head unif. */
        J("p_%s_%d_retry_head_%d:\n", safe_fn, arity, ci);
        J("    iload %d\n", trail_local);
        J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
        /* re-allocate variable cells */
        for (int vi = 0; vi < n_vars; vi++) {
            if (vi < n_args) {
                J("    aload %d\n", vi);
                J("    astore %d\n", var_locals[vi]);
            } else {
                J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", var_locals[vi]);
            }
        }
        /* re-run head unification */
        for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
            EXPR_t *head_term = clause->children[ai];
            if (!head_term) continue;
            if (head_term->kind == E_VART) continue;
            J("    aload %d\n", ai);
            pj_emit_term(head_term, var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", omega_lbl);  /* head fail on retry → omega */
        }
        /* fall through to body */

        /* body goals */
        J("p_%s_%d_body_%d:\n", safe_fn, arity, ci);
        {
            int nbody = clause->nchildren - n_args;
            EXPR_t **body_goals = clause->children + n_args;
            char gamma_lbl[128], retry_head_lbl[128];
            snprintf(gamma_lbl, sizeof gamma_lbl, "p_%s_%d_gamma_%d", safe_fn, arity, ci);
            snprintf(retry_head_lbl, sizeof retry_head_lbl, "p_%s_%d_retry_head_%d", safe_fn, arity, ci);
            int next_local = vars_base + n_vars;
            pj_emit_body(body_goals, nbody, gamma_lbl, retry_head_lbl, omega_lbl,
                         trail_local, var_locals, n_vars, &next_local,
                         init_cs_local, sub_cs_out_local);
        }

        /* γ port: return base[ci] + sub_cs_out + 1
         * This encodes which clause AND where the inner ucall left off.
         * Caller's next cs = this value; on retry: clause = largest ci where
         * cs >= base[ci], inner_cs = cs - base[ci]. */
        J("p_%s_%d_gamma_%d:\n", safe_fn, arity, ci);
        JI("iconst_1", "");
        JI("anewarray", "java/lang/Object");
        JI("dup", "");
        JI("iconst_0", "");
        /* compute base[ci] + sub_cs_out + 1 */
        J("    ldc %d\n", base[ci]);
        J("    iload %d\n", sub_cs_out_local);
        JI("iadd", "");
        JI("iconst_1", "");
        JI("iadd", "");
        JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
        JI("aastore", "");
        JI("areturn", "");

        free(var_locals);
    }

    /* ω port */
    J("p_%s_%d_omega:\n", safe_fn, arity);
    JI("aconst_null", "");
    JI("areturn", "");

    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Main method emitter
 * Finds the initialization(main) directive and emits a JVM main() that calls it.
 * ------------------------------------------------------------------------- */

static void pj_emit_main(Program *prog) {
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 8\n");
    J("    .limit locals 2\n");

    /* call p_main_0() — the main/0 predicate */
    JI("iconst_0", ""); /* cs = 0 */
    J("    invokestatic %s/p_main_0(I)[Ljava/lang/Object;\n", pj_classname);
    JI("pop", "");
    JI("return", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */

void prolog_emit_jvm(Program *prog, FILE *out, const char *filename) {
    pj_out = out;
    pj_natoms = 0;
    pj_label_counter = 0;
    pj_set_classname(filename);

    JC("Generated by snobol4x Prolog JVM emitter (prolog_emit_jvm.c)");
    J("; Source: %s\n\n", filename ? filename : "<stdin>");

    pj_emit_class_header();
    pj_emit_runtime_helpers();

    /* emit each predicate */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE)
            pj_emit_choice(s->subject);
    }

    pj_emit_main(prog);
}
