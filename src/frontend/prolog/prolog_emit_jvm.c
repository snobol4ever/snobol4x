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

static FILE    *pj_out = NULL;
static FILE    *pj_helper_buf = NULL;
static char     pj_classname[256];
static Program *pj_prog = NULL;  /* M-PJ-CUT-UCALL: program root for callee lookup */

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
     * pj_term_str(Object) — convert any term to its Prolog string representation
     * Returns String on stack.  Handles atom, int, float, compound, var, list.
     * Flat compound encoding: [0]="compound",[1]=functor,[2..2+arity-1]=args
     * Lists: functor=".", arity=2 → printed as [H|T] or [a,b,c]
     * ------------------------------------------------------------------ */
    J(".method static pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");
    /* deref */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    /* null → "_" */
    JI("aload_0", "");
    JI("ifnonnull", "pts_notnull");
    JI("ldc", "\"_\"");
    JI("areturn", "");
    J("pts_notnull:\n");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("astore_1", "");  /* local 1 = tag */
    /* atom/int/float → return [1] */
    JI("aload_1", "");
    JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_scalar");
    JI("aload_1", "");
    JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_scalar");
    JI("aload_1", "");
    JI("ldc", "\"float\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_scalar");
    /* compound */
    JI("aload_1", "");
    JI("ldc", "\"compound\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pts_var");
    /* compound: check if it's a list ('.'/2) */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("ldc", "\".\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pts_plain_compound");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("iconst_4", "");  /* 2+arity=4 for arity=2 */
    JI("if_icmpne", "pts_plain_compound");
    /* it's a list: build "[a,b,c]" using StringBuilder */
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("ldc", "\"[\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("astore_2", "");   /* local 2 = sb */
    JI("aload_0", "");    /* local 3 = current list cell */
    JI("astore_3", "");
    J("pts_list_loop:\n");
    /* print head */
    JI("aload_2", "");
    JI("aload_3", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", "");
    JI("aaload", "");   /* head */
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* deref tail */
    JI("aload_3", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_3", "");
    JI("aaload", "");   /* tail */
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_3", "");
    /* if tail is '[]' atom → done */
    JI("aload_3", "");
    JI("ifnull", "pts_list_close");
    JI("aload_3", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pts_list_tail_check");
    /* atom tail: check if it's [] */
    JI("aload_3", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("ldc", "\"[]\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_list_close");
    /* non-[] atom tail: print as |atom] */
    J("pts_list_tail_check:\n");
    /* check if tail is another '.' compound */
    JI("aload_3", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"compound\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pts_list_improper");
    JI("aload_3", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("ldc", "\".\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pts_list_improper");
    /* proper cons cell: append comma and continue */
    JI("aload_2", "");
    JI("ldc", "\",\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("goto", "pts_list_loop");
    J("pts_list_improper:\n");
    /* improper list tail: print |tail */
    JI("aload_2", "");
    JI("ldc", "\"|\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("aload_2", "");
    JI("aload_3", "");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    J("pts_list_close:\n");
    JI("aload_2", "");
    JI("ldc", "\"]\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("aload_2", "");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    /* plain compound: functor(arg1,...) */
    J("pts_plain_compound:\n");
    /* Check for infix operators: -/2, +/2, */2, //2, mod/2 → print infix */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");  /* functor */
    JI("checkcast", "java/lang/String");
    JI("astore_1", "");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("ldc", "4");  /* arity 2 → arraylength 4 */
    JI("if_icmpne", "pts_plain_noninfix");
    /* check functor is one of: - + * / mod */
    JI("aload_1", "");
    JI("ldc", "\"-\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_infix");
    JI("aload_1", "");
    JI("ldc", "\"+\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_infix");
    JI("aload_1", "");
    JI("ldc", "\"*\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_infix");
    JI("aload_1", "");
    JI("ldc", "\"/\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pts_infix");
    JI("goto", "pts_plain_noninfix");
    J("pts_infix:\n");
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("astore_2", "");
    /* left arg */
    JI("aload_2", "");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;"); JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* functor */
    JI("aload_2", ""); JI("aload_1", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* right arg */
    JI("aload_2", "");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;"); JI("iconst_3", ""); JI("aaload", "");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("aload_2", "");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    J("pts_plain_noninfix:\n");
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("astore_2", "");   /* local 2 = sb */
    /* append functor (already in local 1) */
    JI("aload_2", "");
    JI("aload_1", "");  /* functor string already loaded into local 1 */
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* arity = arraylength - 2 */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("iconst_2", "");
    JI("isub", "");
    JI("istore_3", "");   /* local 3 = arity */
    JI("iload_3", "");
    JI("ifeq", "pts_compound_done");
    /* arity > 0: append "(" then args */
    JI("aload_2", "");
    JI("ldc", "\"(\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* use local 1 (was tag, now free) as loop index i=0 */
    JI("iconst_0", "");
    JI("istore_1", "");
    J("pts_compound_loop:\n");
    JI("iload_1", "");
    JI("iload_3", "");
    JI("if_icmpge", "pts_compound_close");
    /* if i>0 append "," */
    JI("iload_1", "");
    JI("ifeq", "pts_compound_nocomma");
    JI("aload_2", "");
    JI("ldc", "\",\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    J("pts_compound_nocomma:\n");
    JI("aload_2", "");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iload_1", "");
    JI("iconst_2", "");
    JI("iadd", "");
    JI("aaload", "");   /* args[i] */
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("iinc", "1 1");
    JI("goto", "pts_compound_loop");
    J("pts_compound_close:\n");
    JI("aload_2", "");
    JI("ldc", "\")\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    J("pts_compound_done:\n");
    JI("aload_2", "");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    /* var/ref → "_" */
    J("pts_var:\n");
    JI("ldc", "\"_\"");
    JI("areturn", "");
    J("pts_scalar:\n");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * write_term(Object) — write/1 builtin
     * ------------------------------------------------------------------ */
    J(".method static pj_write(Ljava/lang/Object;)V\n");
    J("    .limit stack 3\n");
    J("    .limit locals 1\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("swap", "");
    JI("invokevirtual", "java/io/PrintStream/print(Ljava/lang/String;)V");
    JI("return", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_term_to_list(Object term) -> Object[] list
     * Implements =../2 (univ): atom -> [atom], compound -> [F|args]
     * Returns a proper Prolog list as nested cons cells.
     * ------------------------------------------------------------------ */
    J(".method static pj_term_to_list(Ljava/lang/Object;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    /* local 0 = term (deref'd Object[])
     * local 1 = arity (I)
     * local 2 = i (I) — loop counter (going backward)
     * local 3 = list (current tail, Object[])
     * local 4 = new cons cell */

    /* deref term */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("astore_0", "");

    /* check tag */
    JI("aload_0", "");
    JI("iconst_0", ""); JI("aaload", "");  /* tag */
    JI("astore_1", "");  /* reuse local 1 for tag temporarily */

    /* start with nil */
    JI("ldc", "\"[]\"");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("astore_3", "");  /* list = nil */

    /* is it an atom? */
    JI("aload_1", "");
    JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "ttl_compound");

    /* atom: build [name_atom] = cons(name, nil) */
    JI("aload_0", "");
    JI("iconst_1", ""); JI("aaload", "");  /* name string */
    JI("checkcast", "java/lang/String");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    /* cons = {"compound", ".", name_atom, nil} */
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\".\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", "");
    JI("aload_0", ""); JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aastore", "");
    JI("dup", ""); JI("bipush", "3"); JI("aload_3", ""); JI("aastore", "");
    JI("areturn", "");

    J("ttl_compound:\n");
    /* compound: arity = arraylength-2 */
    JI("aload_0", ""); JI("arraylength", ""); JI("iconst_2", ""); JI("isub", "");
    JI("istore_1", "");  /* local 1 = arity */
    /* loop i from arity-1 down to 0: prepend args[i] to list */
    JI("iload_1", ""); JI("iconst_1", ""); JI("isub", ""); JI("istore_2", "");
    J("ttl_arg_loop:\n");
    JI("iload_2", ""); JI("iflt", "ttl_prepend_functor");
    /* cons = {"compound", ".", args[i], list} */
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\".\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", "");
    JI("aload_0", ""); JI("iload_2", ""); JI("iconst_2", ""); JI("iadd", ""); JI("aaload", ""); /* args[i] */
    JI("aastore", "");
    JI("dup", ""); JI("bipush", "3"); JI("aload_3", ""); JI("aastore", "");
    JI("astore_3", "");  /* list = new cons */
    JI("iinc", "2 -1");
    JI("goto", "ttl_arg_loop");
    J("ttl_prepend_functor:\n");
    /* prepend functor atom */
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\".\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", "");
    JI("aload_0", ""); JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aastore", "");
    JI("dup", ""); JI("bipush", "3"); JI("aload_3", ""); JI("aastore", "");
    JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * M-PJ-ATOM-BUILTINS — PJ-48 runtime helpers
     * ------------------------------------------------------------------ */

    /* pj_atom_name(Object term) → String
     * Extracts the name string from an ATOM or INT term.
     * For INT: converts to decimal string. For ATOM: returns field[1].
     * local 0 = term */
    J(".method static pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("astore_0", "");
    /* check tag */
    JI("aload_0", ""); JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pan_atom");
    /* INT: field[1] is a String (Long.toString) — return it directly */
    JI("aload_0", ""); JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("areturn", "");
    J("pan_atom:\n");
    /* ATOM: field[1] is the name String */
    JI("aload_0", ""); JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_int_val(Object term) → J
     * Extracts the long value from an INT term. */
    J(".method static pj_int_val(Ljava/lang/Object;)J\n");
    J("    .limit stack 3\n");
    J("    .limit locals 2\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    JI("lreturn", "");
    J(".end method\n\n");

    /* pj_string_to_char_list(String s) → Object[] list
     * Builds a Prolog list of single-char atoms from a String.
     * local 0=s, 1=len(I), 2=i(I), 3=list(Object[]), 4=char_atom(Object[]) */
    J(".method static pj_string_to_char_list(Ljava/lang/String;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    JI("aload_0", "");
    JI("invokevirtual", "java/lang/String/length()I");
    JI("istore_1", "");
    /* list = nil */
    JI("ldc", "\"[]\"");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("astore_3", "");
    /* loop i from len-1 down to 0 */
    JI("iload_1", ""); JI("iconst_1", ""); JI("isub", ""); JI("istore_2", "");
    J("scl_loop:\n");
    JI("iload_2", ""); JI("iflt", "scl_done");
    /* char_atom = pj_term_atom(String.valueOf(s.charAt(i))) */
    JI("aload_0", ""); JI("iload_2", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C");
    JI("invokestatic", "java/lang/String/valueOf(C)Ljava/lang/String;");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("astore", "4");
    /* cons = {"compound", ".", char_atom, list} */
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\".\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", ""); JI("aload", "4"); JI("aastore", "");
    JI("dup", ""); JI("bipush", "3"); JI("aload_3", ""); JI("aastore", "");
    JI("astore_3", "");
    JI("iinc", "2 -1");
    JI("goto", "scl_loop");
    J("scl_done:\n");
    JI("aload_3", ""); JI("areturn", "");
    J(".end method\n\n");

    /* pj_string_to_code_list(String s) → Object[] list of INT code terms */
    J(".method static pj_string_to_code_list(Ljava/lang/String;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    JI("aload_0", "");
    JI("invokevirtual", "java/lang/String/length()I");
    JI("istore_1", "");
    JI("ldc", "\"[]\"");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("astore_3", "");
    JI("iload_1", ""); JI("iconst_1", ""); JI("isub", ""); JI("istore_2", "");
    J("scol_loop:\n");
    JI("iload_2", ""); JI("iflt", "scol_done");
    JI("aload_0", ""); JI("iload_2", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("astore", "4");
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\".\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", ""); JI("aload", "4"); JI("aastore", "");
    JI("dup", ""); JI("bipush", "3"); JI("aload_3", ""); JI("aastore", "");
    JI("astore_3", "");
    JI("iinc", "2 -1");
    JI("goto", "scol_loop");
    J("scol_done:\n");
    JI("aload_3", ""); JI("areturn", "");
    J(".end method\n\n");

    /* pj_atom_chars_2(Object atom_or_var, Object list_or_var) → Z
     * Both-direction: if atom bound → build char list and unify.
     *                 if atom unbound → read list → build string → unify atom.
     * local 0=atom, 1=list, 2=da(Object[]), 3=dl(Object[]) */
    J(".method static pj_atom_chars_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 8\n");
    J("    .limit locals 6\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("astore_2", "");
    /* check if atom is bound (tag != "var") */
    JI("aload_2", ""); JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "ac2_reverse");
    /* forward: atom→chars — reload deref'd term and call pj_atom_name */
    JI("aload_2", "");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_string_to_char_list(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("ac2_reverse:\n");
    /* reverse: list of char atoms → concat → unify with atom arg */
    JI("aload_1", "");
    J("    invokestatic %s/pj_char_list_to_string(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_0", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* pj_atom_codes_2(Object atom_or_var, Object list_or_var) → Z */
    J(".method static pj_atom_codes_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("astore_2", "");
    JI("aload_2", ""); JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "acd2_reverse");
    /* forward: atom→codes */
    JI("aload_2", "");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_string_to_code_list(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("acd2_reverse:\n");
    JI("aload_1", "");
    J("    invokestatic %s/pj_code_list_to_string(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_0", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* pj_char_code_2(Object char_or_var, Object code_or_var) → Z */
    J(".method static pj_char_code_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("astore_2", "");
    JI("aload_2", ""); JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "cc2_reverse");
    /* forward: char atom → code int */
    JI("aload_2", "");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("iconst_0", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("cc2_reverse:\n");
    /* reverse: code → char atom */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("l2i", "");
    JI("invokestatic", "java/lang/String/valueOf(C)Ljava/lang/String;");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_0", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* pj_char_list_to_string(Object list) → String
     * Walk a Prolog list of char atoms, concat chars into StringBuilder. */
    J(".method static pj_char_list_to_string(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 3\n");
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("astore_1", "");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    J("clts_loop:\n");
    /* check if nil */
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");  /* functor or name */
    JI("ldc", "\"[]\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "clts_done");
    /* head = args[0] = field[2] */
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("aload_1", ""); JI("swap", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* tail = args[1] = field[3] */
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("bipush", "3"); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    JI("goto", "clts_loop");
    J("clts_done:\n");
    JI("aload_1", "");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_code_list_to_string(Object list) → String
     * Walk a Prolog list of INT code terms, build string from char codes. */
    J(".method static pj_code_list_to_string(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 3\n");
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("astore_1", "");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    J("colts_loop:\n");
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("ldc", "\"[]\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "colts_done");
    /* head code */
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("l2i", ""); JI("i2c", "");
    JI("aload_1", ""); JI("swap", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;");
    JI("pop", "");
    /* tail */
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("bipush", "3"); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    JI("goto", "colts_loop");
    J("colts_done:\n");
    JI("aload_1", "");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Class-level scaffold: header + <clinit>
 * ------------------------------------------------------------------------- */

static void pj_emit_assertz_helpers(void) {
    JSep("Dynamic DB helpers: pj_db_assert_key, pj_db_assert, pj_db_query");

    /* pj_db_assert_key(Object term) -> String
     * Returns "functor/arity" key for the term.
     * Term encoding: arr[0]=tag, arr[1]=name, arr[2..]=args
     * atom  → arr[0]="atom",     arr[1]=name  → key = "name/0"
     * compound → arr[0]="compound", arr[1]=functor, arr[2..n+1]=args → key = "functor/(n)"
     */
    J("; pj_db_assert_key(Object) -> String\n");
    J(".method static pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 3\n");
    /* is it an Object[]? */
    JI("aload_0", "");
    JI("dup", "");
    J("    instanceof [Ljava/lang/Object;\n");
    J("    ifeq pj_db_key_atom\n");
    /* compound: arr[0]=tag, arr[1]=functor, arr[2..]=args */
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");  /* arr[1] = functor string */
    JI("checkcast", "java/lang/String");
    JI("astore_1", "");
    /* arity = arr.length - 2 */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("iconst_2", "");
    JI("isub", "");
    JI("istore_2", "");
    /* build "functor/arity" */
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("aload_1", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("ldc", "\"/\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("iload_2", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(I)Ljava/lang/StringBuilder;");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    /* atom: key = "atom/0" */
    J("pj_db_key_atom:\n");
    JI("aload_0", "");
    JI("checkcast", "java/lang/String");
    JI("astore_1", "");
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("aload_1", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("ldc", "\"/0\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_db_assert(String key, Object term, int prepend) -> void
     * Appends (prepend=0) or prepends (prepend=1) a deep-copy of term
     * to the list keyed by key in pj_db. */
    J("; pj_db_assert(String key, Object term, int prepend) -> void\n");
    J(".method static pj_db_assert(Ljava/lang/String;Ljava/lang/Object;I)V\n");
    J("    .limit stack 6\n");
    J("    .limit locals 4\n");
    /* local 0=key, 1=term, 2=prepend, 3=list */
    /* get or create list — store to local 3 on BOTH paths, join with empty stack */
    J("    getstatic %s/pj_db Ljava/util/HashMap;\n", pj_classname);
    JI("aload_0", "");
    JI("invokevirtual", "java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;");
    JI("dup", "");
    J("    ifnonnull pj_db_assert_have_list\n");
    /* null path: create new ArrayList, store to local 3, put in map */
    JI("pop", "");
    JI("new", "java/util/ArrayList");
    JI("dup", "");
    JI("invokespecial", "java/util/ArrayList/<init>()V");
    J("    astore_3\n");          /* store new list; stack now empty */
    J("    getstatic %s/pj_db Ljava/util/HashMap;\n", pj_classname);
    JI("aload_0", "");
    JI("aload_3", "");
    JI("invokevirtual", "java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    JI("pop", "");                /* discard old value (null); stack empty */
    J("    goto pj_db_assert_join\n");
    /* non-null path: cast and store to local 3 */
    J("pj_db_assert_have_list:\n");
    JI("checkcast", "java/util/ArrayList");
    J("    astore_3\n");          /* stack now empty */
    J("pj_db_assert_join:\n");   /* stack empty, local 3 = list */
    /* deep-copy the term for storage */
    JI("aload_1", "");
    J("    invokestatic %s/pj_copy_term_ground(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_1", "");
    /* prepend==1 → list.add(0, term); else list.add(term) */
    JI("iload_2", "");
    J("    ifeq pj_db_assert_append\n");
    JI("aload_3", "");
    JI("iconst_0", "");
    JI("aload_1", "");
    JI("invokevirtual", "java/util/ArrayList/add(ILjava/lang/Object;)V");
    JI("return", "");
    J("pj_db_assert_append:\n");
    JI("aload_3", "");
    JI("aload_1", "");
    JI("invokevirtual", "java/util/ArrayList/add(Ljava/lang/Object;)Z");
    JI("pop", "");
    JI("return", "");
    J(".end method\n\n");

    /* pj_db_query(String key, int idx) -> Object | null
     * Returns term at index idx in the DB list for key, or null if OOB. */
    J("; pj_db_query(String key, int idx) -> Object | null\n");
    J(".method static pj_db_query(Ljava/lang/String;I)Ljava/lang/Object;\n");
    J("    .limit stack 4\n");
    J("    .limit locals 3\n");
    J("    getstatic %s/pj_db Ljava/util/HashMap;\n", pj_classname);
    JI("aload_0", "");
    JI("invokevirtual", "java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;");
    JI("dup", "");
    J("    ifnonnull pj_db_query_have\n");
    JI("areturn", "");  /* null → no entries */
    J("pj_db_query_have:\n");
    JI("checkcast", "java/util/ArrayList");
    JI("astore_2", "");
    /* bounds check: idx < list.size() */
    JI("aload_2", "");
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("iload_1", "");
    J("    if_icmpgt pj_db_query_ok\n");
    JI("aconst_null", "");
    JI("areturn", "");
    J("pj_db_query_ok:\n");
    JI("aload_2", "");
    JI("iload_1", "");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_copy_term_ground(Object) -> Object
     * Deep-copy a ground term (atoms, ints, compounds) for DB storage.
     * Variables are stored as-is (for future retract support). */
    J("; pj_copy_term_ground(Object) -> Object\n");
    J(".method static pj_copy_term_ground(Ljava/lang/Object;)Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    JI("aload_0", "");
    /* atom (String) or int (Long)? return as-is */
    JI("dup", "");
    J("    instanceof [Ljava/lang/Object;\n");
    J("    ifne pj_ctg_compound\n");
    JI("areturn", "");
    J("pj_ctg_compound:\n");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("astore_1", "");
    /* new array of same length */
    JI("aload_1", "");
    JI("arraylength", "");
    JI("istore_2", "");
    JI("iload_2", "");
    JI("anewarray", "java/lang/Object");
    JI("astore_3", "");
    /* copy each slot recursively */
    /* slot 0 = functor (String) — copy as-is */
    JI("aload_3", "");
    JI("iconst_0", "");
    JI("aload_1", "");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("aastore", "");
    /* loop i=1..length-1 */
    JI("iconst_1", "");
    JI("istore", "4");
    J("pj_ctg_loop:\n");
    JI("iload", "4");
    JI("iload_2", "");
    J("    if_icmpge pj_ctg_done\n");
    JI("aload_3", "");
    JI("iload", "4");
    JI("aload_1", "");
    JI("iload", "4");
    JI("aaload", "");
    J("    invokestatic %s/pj_copy_term_ground(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("aastore", "");
    JI("iinc", "4 1");
    J("    goto pj_ctg_loop\n");
    J("pj_ctg_done:\n");
    JI("aload_3", "");
    JI("areturn", "");
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
    JC("Dynamic DB field: HashMap<String, ArrayList<Object[]>>");
    J(".field static pj_db Ljava/util/HashMap;\n\n");

    /* <clinit> — init trail and dynamic DB */
    J(".method static <clinit>()V\n");
    J("    .limit stack 3\n");
    J("    .limit locals 0\n");
    JI("new", "java/util/ArrayList");
    JI("dup", "");
    JI("invokespecial", "java/util/ArrayList/<init>()V");
    J("    putstatic %s/pj_trail Ljava/util/ArrayList;\n", pj_classname);
    JI("new", "java/util/HashMap");
    JI("dup", "");
    JI("invokespecial", "java/util/HashMap/<init>()V");
    J("    putstatic %s/pj_db Ljava/util/HashMap;\n", pj_classname);
    JI("return", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Forward declarations for goal/term emitters
 * ------------------------------------------------------------------------- */

static void pj_emit_goal(EXPR_t *goal, const char *lbl_γ, const char *lbl_ω,
                         int trail_local, int *var_locals, int n_vars,
                         int cut_cs_seal, int cs_local_for_cut,
                         int *next_local, const char *lbl_cutγ);
static void pj_emit_term(EXPR_t *term, int *var_locals, int n_vars);
static void pj_emit_arith(EXPR_t *e, int *var_locals, int n_vars);
/* M-PJ-CUT-UCALL: forward decls for callee cut-sentinel helpers */
static int pj_predicate_base_nclauses(const char *fn, int arity);
static int pj_callee_has_cut_no_last_ucall(const char *fn, int arity);

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
    case E_ADD: case E_SUB: case E_MPY: case E_DIV: {
        /* Arithmetic op node in TERM position (e.g. write(X-Y), write(A+B)).
         * Lowerer maps -/+/* to E_SUB/E_ADD/E_MPY unconditionally.
         * In term position emit as compound so write/1 and unification work. */
        const char *afn = (e->kind==E_ADD)?"+": (e->kind==E_SUB)?"-":
                          (e->kind==E_MPY)?"*": "/";
        int arity = e->nchildren;
        if (arity == 0) {
            J("    ldc \"%s\"\n", afn);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n",
              pj_classname);
        } else {
            J("    bipush %d\n", 2 + arity);
            JI("anewarray", "java/lang/Object");
            JI("dup",""); JI("iconst_0",""); JI("ldc","\"compound\""); JI("aastore","");
            JI("dup",""); JI("iconst_1","");
            J("    ldc \"%s\"\n", afn);
            JI("aastore","");
            for (int ai = 0; ai < arity; ai++) {
                JI("dup","");
                J("    bipush %d\n", 2 + ai);
                pj_emit_term(e->children[ai], var_locals, n_vars);
                JI("aastore","");
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
    case E_FNC:
        /* mod/2, rem/2, //2 (integer div) — not given dedicated opcodes by lowerer */
        if (e->sval && e->nchildren >= 2) {
            if (strcmp(e->sval, "mod") == 0 || strcmp(e->sval, "rem") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("lrem", "");
                break;
            }
            if (strcmp(e->sval, "//") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("ldiv", "");
                break;
            }
            if (strcmp(e->sval, "abs") == 0 && e->nchildren == 1) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                /* abs: dup, negate, take max */
                J("    dup2\n    lneg\n");
                J("    invokestatic java/lang/Math/max(JJ)J\n");
                break;
            }
        }
        JI("lconst_0", "");
        break;
    default:
        JI("lconst_0", "");
        break;
    }
}

/* -------------------------------------------------------------------------
 * pj_is_always_fail — true for goals that unconditionally fail (no retry).
 * These must route their omega to lbl_pred_ω (next clause), not to the
 * clause's own alpha/retry label, to avoid infinite retry loops.
 * ------------------------------------------------------------------------- */
static int pj_is_always_fail(EXPR_t *goal) {
    if (!goal || goal->kind != E_FNC || !goal->sval) return 0;
    return (strcmp(goal->sval, "fail") == 0 || strcmp(goal->sval, "false") == 0)
           && goal->nchildren == 0;
}

/* -------------------------------------------------------------------------
 * pj_body_has_cut — recursive scan for E_CUT in a goal tree (walks E_FNC
 * children for "," and ";" conjunction/disjunction nodes).
 * ------------------------------------------------------------------------- */
static int pj_body_has_cut(EXPR_t *g) {
    if (!g) return 0;
    if (g->kind == E_CUT) return 1;
    if (g->kind == E_FNC) {
        for (int i = 0; i < (int)g->nchildren; i++)
            if (pj_body_has_cut(g->children[i])) return 1;
    }
    return 0;
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
        "atom_length","atom_concat","atom_chars","atom_codes","char_code",
        "number_chars","number_codes","upcase_atom","downcase_atom",
        "between","findall",
        "assertz","asserta","abolish",
        NULL
    };
    for (int i = 0; builtins[i]; i++)
        if (strcmp(fn, builtins[i]) == 0) return 0;
    return 1;
}

/* -------------------------------------------------------------------------
 * pj_count_ucalls — count total user calls in a body (each allocates 5 locals)
 * ------------------------------------------------------------------------- */
static int pj_count_ucalls(EXPR_t **goals, int ngoals) {
    int count = 0;
    for (int i = 0; i < ngoals; i++) {
        EXPR_t *g = goals[i];
        if (!g) continue;
        if (pj_is_user_call(g)) count++;
    }
    return count;
}

/* pj_count_neq — count \=/2 goals in a body (each allocates 2 scratch locals) */
static int pj_count_neq(EXPR_t **goals, int ngoals) {
    int count = 0;
    for (int i = 0; i < ngoals; i++) {
        EXPR_t *g = goals[i];
        if (!g) continue;
        if (g->kind == E_FNC && g->sval && strcmp(g->sval, "\\=") == 0 && g->nchildren == 2)
            count++;
    }
    return count;
}

/* pj_count_disj_locals — extra JVM locals for plain disjunctions in a body.
 * Each plain ; allocates: 2 (local_cs+local_tmark) + 2*narms (ics+sco per arm)
 * + 2 (fresh_init + fresh_sub). Also recurse into arm bodies. */
static int pj_count_disj_locals(EXPR_t **goals, int ngoals) {
    int total = 0;
    for (int i = 0; i < ngoals; i++) {
        EXPR_t *g = goals[i];
        if (!g || g->kind != E_FNC || !g->sval) continue;
        if (strcmp(g->sval, ";") == 0 && g->nchildren >= 2) {
            if (g->children[0] && g->children[0]->kind == E_FNC &&
                g->children[0]->sval && strcmp(g->children[0]->sval, "->") == 0)
                continue; /* ITE handled separately */
            /* base cost: 2 + 2 + 2*narms locals at this disjunction level */
            total += 4 + 2 * g->nchildren;
            /* recurse into each arm — each arm may have user calls (5 locals each) */
            for (int ai = 0; ai < g->nchildren; ai++) {
                EXPR_t *arm = g->children[ai];
                if (!arm) continue;
                if (arm->kind == E_FNC && arm->sval && strcmp(arm->sval, ",") == 0) {
                    /* count user calls in conjunction arm */
                    total += 5 * pj_count_ucalls(arm->children, arm->nchildren);
                    total += pj_count_disj_locals(arm->children, arm->nchildren);
                } else {
                    /* single goal arm */
                    if (pj_is_user_call(arm)) total += 5;
                }
            }
        }
    }
    return total;
}

/* pj_term_stack_depth — compute max JVM stack slots needed to emit a term.
 * Compound: dup+idx+child leaves [arr,arr,idx,child]=4 per level before aastore.
 * Worst case per nesting level = 4 slots. */
static int pj_term_stack_depth(EXPR_t *e) {
    if (!e) return 1;
    switch (e->kind) {
    case E_FNC:
        if (e->nchildren == 0) return 2; /* ldc + invokestatic */
        {
            int max_child = 0;
            for (int i = 0; i < e->nchildren; i++) {
                int d = pj_term_stack_depth(e->children[i]);
                if (d > max_child) max_child = d;
            }
            return 4 + max_child; /* array ref + dup + index + child */
        }
    default:
        return 2;
    }
}

/* pj_clause_stack_needed — max stack depth across all goals in a clause body */
static int pj_clause_stack_needed(EXPR_t **goals, int ngoals) {
    int max_s = 4; /* baseline for simple goals */
    for (int i = 0; i < ngoals; i++) {
        EXPR_t *g = goals[i];
        if (!g) continue;
        /* for goals with term arguments, stack depth = goal overhead + max arg depth */
        if (g->kind == E_FNC || g->kind == E_UNIFY) {
            int nc = g->nchildren;
            for (int j = 0; j < nc; j++) {
                int d = pj_term_stack_depth(g->children[j]);
                if (d > max_s) max_s = d;
            }
        }
    }
    return max_s + 4; /* +4 for invokestatic overhead */
}

/* -------------------------------------------------------------------------
 * Goal emitter
 * lbl_γ = label to goto on goal success
 * lbl_ω  = label to goto on goal failure
 * trail_local = JVM local index of trail mark (I)
 * var_locals  = JVM local indices for Prolog variables
 * ------------------------------------------------------------------------- */

/* forward decl for pj_emit_body */
static void pj_emit_body(EXPR_t **goals, int ngoals, const char *lbl_γ,
                         const char *lbl_ω, const char *lbl_outer_ω,
                         int trail_local,
                         int *var_locals, int n_vars, int *next_local,
                         int init_cs_local, int sub_cs_out_local,
                         int cut_cs_seal, int cs_local_for_cut,
                         const char *lbl_pred_ω,
                         const char *lbl_cutγ);

static void pj_emit_goal(EXPR_t *goal, const char *lbl_γ, const char *lbl_ω,
                         int trail_local, int *var_locals, int n_vars,
                         int cut_cs_seal, int cs_local_for_cut,
                         int *next_local, const char *lbl_cutγ) {
    if (!goal) { JI("goto", lbl_γ); return; }

    if (goal->kind == E_CUT) {
        /* cut: seal β by storing base[nclauses] into cs_local,
         * so the next dispatch hits default:omega.
         * -1 means no enclosing choice (should not happen in valid Prolog). */
        if (cut_cs_seal >= 0 && cs_local_for_cut >= 0) {
            J("    ldc %d\n", cut_cs_seal);
            J("    istore %d\n", cs_local_for_cut);
        }
        /* Jump to cutgamma label which returns base[nclauses] sentinel,
         * preventing caller from retrying this predicate. */
        JI("goto", lbl_cutγ ? lbl_cutγ : lbl_γ);
        return;
    }

    if (goal->kind == E_UNIFY) {
        /* =/2: unify children[0] and children[1] */
        pj_emit_term(goal->children[0], var_locals, n_vars);
        pj_emit_term(goal->children[1], var_locals, n_vars);
        J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
        J("    ifeq %s\n", lbl_ω);
        JI("goto", lbl_γ);
        return;
    }

    if (goal->kind == E_FNC) {
        const char *fn = goal->sval ? goal->sval : "";
        int nargs = goal->nchildren;

        /* write/1 */
        if (strcmp(fn, "write") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_write(Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* nl/0 */
        if (strcmp(fn, "nl") == 0 && nargs == 0) {
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            JI("invokevirtual", "java/io/PrintStream/println()V");
            JI("goto", lbl_γ);
            return;
        }
        /* writeln/1 */
        if (strcmp(fn, "writeln") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_write(Ljava/lang/Object;)V\n", pj_classname);
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            JI("invokevirtual", "java/io/PrintStream/println()V");
            JI("goto", lbl_γ);
            return;
        }
        /* true/0 */
        if (strcmp(fn, "true") == 0 && nargs == 0) {
            JI("goto", lbl_γ);
            return;
        }
        /* fail/0 */
        if (strcmp(fn, "fail") == 0 && nargs == 0) {
            JI("goto", lbl_ω);
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
            JI("goto", lbl_γ);
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
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
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
            if (strcmp(fn, "=:=") == 0)      J("    ifne %s\n", lbl_ω);
            else if (strcmp(fn, "=\\=") == 0) J("    ifeq %s\n", lbl_ω);
            else if (strcmp(fn, "<") == 0)    J("    ifge %s\n", lbl_ω);
            else if (strcmp(fn, ">") == 0)    J("    ifle %s\n", lbl_ω);
            else if (strcmp(fn, "=<") == 0)   J("    ifgt %s\n", lbl_ω);
            else if (strcmp(fn, ">=") == 0)   J("    iflt %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* ------------------------------------------------------------------ *
         * M-PJ-ATOM-BUILTINS — PJ-48
         * All atom/string builtins are JVM String operations.
         * Pattern: deref the bound arg → extract String → operate → box result
         * as a PlTerm atom → unify with the output variable.
         * pj_term_atom(String) → Object[]   (tag=ATOM, name=String)
         * pj_term_int(long)    → Object[]   (tag=INT, val=long)
         * pj_unify(a,b)        → Z
         * pj_deref(Object)     → Object   (follow REF chain)
         * pj_atom_name(Object) → String   (extract name field from ATOM term)
         * pj_int_val(Object)   → J        (extract val field from INT term)
         * pj_term_list_chars(String) → Object[]  (build char-atom list)
         * pj_term_list_codes(String) → Object[]  (build int-code list)
         * pj_list_to_string(Object)  → String    (build String from char/code list)
         * ------------------------------------------------------------------ */

        /* assertz(+Term) — append fact to dynamic DB */
        if ((strcmp(fn, "assertz") == 0 || strcmp(fn, "asserta") == 0) && nargs == 1) {
            int is_asserta = (strcmp(fn, "asserta") == 0);
            /* We emit a call to the static helper pj_db_assert(key, term, append).
             * key = functor/arity string of the asserted fact.
             * We push: term (Object[]), key (String), is_asserta (I) */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    %s\n", is_asserta ? "iconst_1" : "iconst_0");
            J("    invokestatic %s/pj_db_assert(Ljava/lang/String;Ljava/lang/Object;I)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }

        /* atom_length(+Atom, ?Length) */
        if (strcmp(fn, "atom_length") == 0 && nargs == 2) {
            int uid = pj_fresh_label();
            char ok[64]; snprintf(ok, sizeof ok, "atlen%d_ok", uid);
            /* deref arg0, extract atom name string, call .length() */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokevirtual java/lang/String/length()I\n");
            J("    i2l\n");
            J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
            /* unify result with arg1 */
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* atom_concat(+A, +B, ?C) */
        if (strcmp(fn, "atom_concat") == 0 && nargs == 3) {
            /* extract A name, extract B name, concatenate, box as atom, unify with C */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* atom_chars(+Atom, ?Chars) or atom_chars(?Atom, +Chars)
         * Direction detected at runtime via pj_atom_chars_2 helper. */
        if (strcmp(fn, "atom_chars") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_atom_chars_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* atom_codes(+Atom, ?Codes) or atom_codes(?Atom, +Codes) */
        if (strcmp(fn, "atom_codes") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_atom_codes_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* number_chars(+Number, ?Chars) — one-directional (+N → chars) */
        if (strcmp(fn, "number_chars") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
            J("    invokestatic java/lang/Long/toString(J)Ljava/lang/String;\n");
            J("    invokestatic %s/pj_string_to_char_list(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* number_codes(+Number, ?Codes) — one-directional */
        if (strcmp(fn, "number_codes") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
            J("    invokestatic java/lang/Long/toString(J)Ljava/lang/String;\n");
            J("    invokestatic %s/pj_string_to_code_list(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* char_code(+Char, ?Code) or char_code(?Char, +Code) */
        if (strcmp(fn, "char_code") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_char_code_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* upcase_atom(+Atom, ?Upper) */
        if (strcmp(fn, "upcase_atom") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokevirtual java/lang/String/toUpperCase()Ljava/lang/String;\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* downcase_atom(+Atom, ?Lower) */
        if (strcmp(fn, "downcase_atom") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokevirtual java/lang/String/toLowerCase()Ljava/lang/String;\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
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
            pj_emit_body(goal->children, nargs, lbl_γ, lbl_ω, lbl_ω,
                         trail_local, var_locals, n_vars, &next_local_tmp,
                         ics, sco, cut_cs_seal, cs_local_for_cut, NULL, lbl_cutγ);
            return;
        }
        /* ;/2 (now n-ary after lowering) — disjunction: try each branch.
         * Special case: if first child is ->/2, it's an if-then-else:
         *   (Cond -> Then ; Else)  ==> branch on Cond; take Then or Else.
         * On success goto lbl_γ; on failure of all branches goto lbl_ω. */
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
                             trail_local, var_locals, n_vars,
                             cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
                J("%s:\n", cond_ok);
                /* ITE cut: once condition succeeds, commit — seal the enclosing
                 * clause β so backtrack cannot retry the else branches.
                 * This matches swipl semantics: (Cond -> Then ; Else) is
                 * deterministic once Cond succeeds. */
                if (cut_cs_seal >= 0 && cs_local_for_cut >= 0) {
                    J("    ldc %d\n", cut_cs_seal);
                    J("    istore %d\n", cs_local_for_cut);
                }
                /* emit Then goals: children[1..nchildren-1] as flat sequence */
                int nthen = first->nchildren - 1;
                for (int ti = 0; ti < nthen; ti++) {
                    char tstep_γ[128], tstep_ω[128];
                    snprintf(tstep_γ, sizeof tstep_γ, "ite%d_t%d_gamma", uid, ti);
                    snprintf(tstep_ω, sizeof tstep_ω, "ite%d_t%d_omega", uid, ti);
                    const char *step_γ = (ti == nthen - 1) ? lbl_γ : tstep_γ;
                    pj_emit_goal(first->children[1 + ti], step_γ, lbl_ω,
                                 trail_local, var_locals, n_vars,
                                 cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
                    if (ti < nthen - 1) J("%s:\n", tstep_γ);
                }
                J("    goto %s\n", done_lbl);
                J("%s:\n", cond_fail);
                /* emit remaining else branches */
                for (int bi = 1; bi < nargs; bi++) {
                    char next_lbl[128];
                    snprintf(next_lbl, sizeof next_lbl, "ite%d_alt%d", uid, bi + 1);
                    const char *fail_to = (bi < nargs - 1) ? next_lbl : lbl_ω;
                    pj_emit_goal(goal->children[bi], lbl_γ, fail_to,
                                 trail_local, var_locals, n_vars,
                                 cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
                    if (bi < nargs - 1) {
                        J("    goto %s\n", done_lbl);
                        J("%s:\n", next_lbl);
                    }
                }
                J("%s:\n", done_lbl);
                return;
            }
            /* plain disjunction: try each branch in sequence.
             * Use pj_emit_body for each branch so that user calls within
             * a branch get proper retry loops (e.g. puzzle ; true). */
            int uid = pj_fresh_label();
            char done_lbl[128];
            snprintf(done_lbl, sizeof done_lbl, "disj%d_done", uid);
            for (int bi = 0; bi < nargs; bi++) {
                char next_lbl[128];
                snprintf(next_lbl, sizeof next_lbl, "disj%d_alt%d", uid, bi + 1);
                const char *fail_to = (bi < nargs - 1) ? next_lbl : lbl_ω;
                /* Fresh locals for this branch — branches are mutually exclusive */
                int next_local_disj = trail_local + 1 + n_vars + 8;
                int ics_disj = next_local_disj++;
                int sco_disj = next_local_disj++;
                JI("iconst_0", ""); J("    istore %d\n", ics_disj);
                JI("iconst_0", ""); J("    istore %d\n", sco_disj);
                EXPR_t *branch = goal->children[bi];
                if (branch && branch->kind == E_FNC && branch->sval &&
                    strcmp(branch->sval, ",") == 0) {
                    /* conjunction branch: pass children directly to pj_emit_body */
                    pj_emit_body(branch->children, branch->nchildren,
                                 lbl_γ, fail_to, fail_to,
                                 trail_local, var_locals, n_vars, &next_local_disj,
                                 ics_disj, sco_disj, cut_cs_seal, cs_local_for_cut, NULL, lbl_cutγ);
                } else {
                    /* single goal branch */
                    pj_emit_body(&branch, 1,
                                 lbl_γ, fail_to, fail_to,
                                 trail_local, var_locals, n_vars, &next_local_disj,
                                 ics_disj, sco_disj, cut_cs_seal, cs_local_for_cut, NULL, lbl_cutγ);
                }
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
         * Condition failure → lbl_ω. */
        if (strcmp(fn, "->") == 0 && nargs >= 2) {
            int uid = pj_fresh_label();
            char cond_ok[128], cond_fail[128];
            snprintf(cond_ok,   sizeof cond_ok,   "ifthen%d_ok",   uid);
            snprintf(cond_fail, sizeof cond_fail,  "ifthen%d_fail", uid);
            pj_emit_goal(goal->children[0], cond_ok, cond_fail, trail_local, var_locals, n_vars,
                         cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
            J("%s:\n", cond_ok);
            int nthen = nargs - 1;
            for (int ti = 0; ti < nthen; ti++) {
                char tstep_γ[128];
                snprintf(tstep_γ, sizeof tstep_γ, "ifthen%d_t%d_gamma", uid, ti);
                const char *step_γ = (ti == nthen - 1) ? lbl_γ : tstep_γ;
                pj_emit_goal(goal->children[1 + ti], step_γ, lbl_ω,
                             trail_local, var_locals, n_vars,
                             cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
                if (ti < nthen - 1) J("%s:\n", tstep_γ);
            }
            J("%s:\n", cond_fail);
            JI("goto", lbl_ω);
            return;
        }
        /* structural equality ==/2, \==/2 */
        if ((strcmp(fn, "==") == 0 || strcmp(fn, "\\==") == 0) && nargs == 2) {
            int uid = pj_fresh_label();
            char ok[128]; snprintf(ok, sizeof ok, "seq%d_ok", uid);
            /* deref both terms and compare via pj_term_str (structural print) */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
            if (strcmp(fn, "==") == 0) J("    ifeq %s\n", lbl_ω);
            else                        J("    ifne %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* \=/2 — non-unifiability: succeed iff X and Y cannot be unified.
         * Strategy: save trail mark, probe pj_unify, unwind regardless, branch inverted.
         * Uses a fresh local for the trial trail mark so we don't clobber trail_local
         * (which belongs to the enclosing clause's β-retry machinery). */
        if (strcmp(fn, "\\=") == 0 && nargs == 2) {
            int scratch_tmark  = (*next_local)++;
            int scratch_result = (*next_local)++;
            /* initialise scratch locals to 0 */
            JI("iconst_0", ""); J("    istore %d\n", scratch_tmark);
            JI("iconst_0", ""); J("    istore %d\n", scratch_result);
            /* save trail mark */
            J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
            J("    istore %d\n", scratch_tmark);
            /* attempt unification; save boolean result */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    istore %d\n", scratch_result);
            /* unwind trail — undo any bindings made by the probe */
            J("    iload %d\n", scratch_tmark);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            /* branch inverted: unify succeeded (1) → \= fails; unify failed (0) → \= succeeds */
            J("    iload %d\n", scratch_result);
            J("    ifne %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* \+/1, not/1 — negation as failure.
         * Emit the inner goal into a separate synthetic static helper method
         * naf_helper_N(Object v0 ... vN-1) returning Z (boolean).
         * This gives the inner goal its own clean JVM local frame, preventing
         * aliasing between inner ucall cs-locals and outer clause locals.
         * The helper is emitted inline at the current output position;
         * Jasmin accepts methods in any order in the file. */
        if ((strcmp(fn, "\\+") == 0 || strcmp(fn, "not") == 0) && nargs == 1) {
            int uid = pj_fresh_label();
            /* --- emit helper method into pj_helper_buf (flushed after .end method) --- */
            char parmdesc[1024]; parmdesc[0] = '\0';
            for (int _p = 0; _p < n_vars; _p++) strcat(parmdesc, "[Ljava/lang/Object;");
            char hname[128];
            snprintf(hname, sizeof hname, "naf_helper_%d", uid);
            int inner_var_locals[256];
            int nvl = n_vars < 256 ? n_vars : 256;
            for (int _v = 0; _v < nvl; _v++) inner_var_locals[_v] = _v;
            int h_trail = n_vars;
            int h_next  = n_vars + 1;
            /* Redirect to helper buffer */
            FILE *pj_saved_out = pj_out;
            if (!pj_helper_buf) pj_helper_buf = tmpfile();
            pj_out = pj_helper_buf;
            J("\n.method static %s(%s)Z\n", hname, parmdesc);
            J("    .limit stack 32\n");
            J("    .limit locals %d\n", n_vars + 64);
            J("    iconst_0\n"); J("    istore %d\n", h_trail);
            J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
            J("    istore %d\n", h_trail);
            /* Emit inner goal */
            char h_ok[128], h_fail[128];
            snprintf(h_ok,   sizeof h_ok,   "nafh%d_ok",   uid);
            snprintf(h_fail, sizeof h_fail, "nafh%d_fail", uid);
            EXPR_t *naf_inner = goal->children[0];
            if (naf_inner && naf_inner->kind == E_FNC && naf_inner->sval
                    && strcmp(naf_inner->sval, ",") == 0 && naf_inner->nchildren >= 1) {
                int h_ics = h_next++; int h_sco = h_next++;
                J("    iconst_0\n"); J("    istore %d\n", h_ics);
                J("    iconst_0\n"); J("    istore %d\n", h_sco);
                pj_emit_body(naf_inner->children, naf_inner->nchildren,
                             h_ok, h_fail, h_fail,
                             h_trail, inner_var_locals, nvl, &h_next,
                             h_ics, h_sco, 0, 0, NULL, NULL);
            } else {
                int h_ics = h_next++; int h_sco = h_next++;
                J("    iconst_0\n"); J("    istore %d\n", h_ics);
                J("    iconst_0\n"); J("    istore %d\n", h_sco);
                pj_emit_body(&naf_inner, 1,
                             h_ok, h_fail, h_fail,
                             h_trail, inner_var_locals, nvl, &h_next,
                             h_ics, h_sco, 0, 0, NULL, NULL);
            }
            J("%s:\n", h_ok);
            J("    iload %d\n", h_trail);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            J("    iconst_1\n    ireturn\n");
            J("%s:\n", h_fail);
            J("    iload %d\n", h_trail);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            J("    iconst_0\n    ireturn\n");
            J(".end method\n\n");
            pj_out = pj_saved_out;
            /* --- call helper from outer body --- */
            for (int _p = 0; _p < n_vars && _p < nvl; _p++)
                J("    aload %d\n", var_locals[_p]);
            J("    invokestatic %s/%s(%s)Z\n", pj_classname, hname, parmdesc);
            /* result on stack: 1=inner succeeded (NAF fails), 0=inner failed (NAF succeeds) */
            J("    ifeq %s\n", lbl_γ);   /* == 0 → inner failed → NAF succeeds → lbl_γ */
            JI("goto", lbl_ω);           /* != 0 → inner succeeded → NAF fails → lbl_ω */
            return;
        }
        /* type tests: atom/1, integer/1, float/1, compound/1, var/1, nonvar/1, atomic/1, is_list/1 */
        if (nargs == 1 &&
            (strcmp(fn,"atom")==0 || strcmp(fn,"integer")==0 || strcmp(fn,"float")==0 ||
             strcmp(fn,"compound")==0 || strcmp(fn,"var")==0 || strcmp(fn,"nonvar")==0 ||
             strcmp(fn,"atomic")==0 || strcmp(fn,"is_list")==0)) {
            int uid = pj_fresh_label();
            char deref_lbl[128], tag_lbl[128];
            snprintf(deref_lbl, sizeof deref_lbl, "tt%d_deref", uid);
            snprintf(tag_lbl,   sizeof tag_lbl,   "tt%d_tag",   uid);
            /* deref term, store in a temp — we'll check its tag string */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            /* stack: deref'd term (Object) */
            if (strcmp(fn, "var") == 0) {
                /* var: term is null OR (tag=="var" && [1]==null) */
                int uid2 = pj_fresh_label();
                char vok[128], vfail[128];
                snprintf(vok,   sizeof vok,   "var%d_ok",   uid2);
                snprintf(vfail, sizeof vfail, "var%d_fail", uid2);
                JI("dup", "");
                J("    ifnull %s\n", vok);   /* null = unbound → succeed */
                /* not null: check tag=="var" */
                JI("checkcast", "[Ljava/lang/Object;");
                JI("dup", ""); JI("iconst_0", ""); JI("aaload", "");
                JI("ldc", "\"var\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", vfail);   /* not var tag → fail */
                /* tag=="var": check [1]==null (unbound) */
                JI("iconst_1", ""); JI("aaload", "");
                J("    ifnonnull %s\n", vfail); /* bound → fail */
                JI("goto", lbl_γ);
                J("%s:\n", vok);   JI("pop", ""); JI("goto", lbl_γ);
                J("%s:\n", vfail); JI("pop", ""); JI("goto", lbl_ω);
                return;
            }
            if (strcmp(fn, "nonvar") == 0) {
                /* nonvar: not null AND not unbound var */
                int uid2 = pj_fresh_label();
                char nfail[128], nok[128];
                snprintf(nfail, sizeof nfail, "nv%d_fail", uid2);
                snprintf(nok,   sizeof nok,   "nv%d_ok",   uid2);
                JI("dup", "");
                J("    ifnull %s\n", nfail);  /* null = unbound → fail */
                JI("checkcast", "[Ljava/lang/Object;");
                JI("dup", ""); JI("iconst_0", ""); JI("aaload", "");
                JI("ldc", "\"var\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", nok);   /* not var tag → nonvar → succeed */
                /* tag=="var": check if [1]!=null (bound ref = nonvar) */
                JI("iconst_1", ""); JI("aaload", "");
                J("    ifnonnull %s\n", nok); /* bound ref → succeed */
                /* unbound var → fail */
                JI("goto", lbl_ω);
                J("%s:\n", nok);   JI("pop", ""); JI("goto", lbl_γ);
                J("%s:\n", nfail); JI("pop", ""); JI("goto", lbl_ω);
                return;
            }
            /* For remaining type tests: get tag string */
            JI("ifnull", lbl_ω);  /* null = unbound var → fail for all remaining */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            JI("iconst_0", ""); JI("aaload", "");  /* tag string on stack */
            if (strcmp(fn, "atom") == 0) {
                JI("ldc", "\"atom\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", lbl_ω);
            } else if (strcmp(fn, "integer") == 0) {
                JI("ldc", "\"int\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", lbl_ω);
            } else if (strcmp(fn, "float") == 0) {
                JI("ldc", "\"float\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", lbl_ω);
            } else if (strcmp(fn, "compound") == 0) {
                JI("ldc", "\"compound\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", lbl_ω);
            } else if (strcmp(fn, "atomic") == 0) {
                /* atomic: atom or integer or float */
                int uid2 = pj_fresh_label();
                char a_ok[128]; snprintf(a_ok, sizeof a_ok, "atomic%d_ok", uid2);
                JI("dup", ""); JI("ldc", "\"atom\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifne %s\n", a_ok);
                JI("dup", ""); JI("ldc", "\"int\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifne %s\n", a_ok);
                JI("ldc", "\"float\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifeq %s\n", lbl_ω);
                J("%s:\n", a_ok);
            } else if (strcmp(fn, "is_list") == 0) {
                /* is_list: atom "[]" or compound with functor "." and arity 2 */
                /* We already have the tag on stack; just check it's [] atom or '.' compound */
                int uid2 = pj_fresh_label();
                char il_ok[128]; snprintf(il_ok, sizeof il_ok, "islist%d_ok", uid2);
                JI("pop", "");  /* discard tag — re-emit term */
                pj_emit_term(goal->children[0], var_locals, n_vars);
                J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
                J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
                JI("ldc", "\"[]\"");
                JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
                J("    ifne %s\n", lbl_γ);
                /* not [] — check if starts with '[' (is a proper list string) */
                pj_emit_term(goal->children[0], var_locals, n_vars);
                J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
                J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
                JI("iconst_0", "");
                JI("invokevirtual", "java/lang/String/charAt(I)C");
                JI("bipush", "91");  /* '[' */
                J("    if_icmpne %s\n", lbl_ω);
                /* also confirm no '|' (proper list only) */
                pj_emit_term(goal->children[0], var_locals, n_vars);
                J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
                J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
                JI("ldc", "\"|\"");
                JI("invokevirtual", "java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                J("    ifne %s\n", lbl_ω);
            }
            JI("goto", lbl_γ);
            return;
        }
        /* functor/3: functor(Term, Name, Arity)
         * Deref Term; if atom → Name=functor, Arity=0; if compound → Name=functor, Arity=arity.
         * Unify Name and Arity with children[1] and children[2]. */
        if (strcmp(fn, "functor") == 0 && nargs == 3) {
            int uid = pj_fresh_label();
            char is_atom[128], is_compound[128], done[128], is_null[128];
            snprintf(is_atom,     sizeof is_atom,     "funct%d_atom",     uid);
            snprintf(is_compound, sizeof is_compound, "funct%d_compound", uid);
            snprintf(done,        sizeof done,        "funct%d_done",     uid);
            snprintf(is_null,     sizeof is_null,     "funct%d_null",     uid);
            /* deref once, check tag — all paths start with empty stack */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("ifnull", lbl_ω);  /* consumes the ref; stack empty */
            /* re-emit to check tag */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            JI("iconst_0", ""); JI("aaload", "");  /* tag on stack */
            JI("ldc", "\"atom\"");
            JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
            J("    ifne %s\n", is_atom);   /* consumes boolean; stack empty */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            JI("iconst_0", ""); JI("aaload", "");
            JI("ldc", "\"compound\"");
            JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
            J("    ifne %s\n", is_compound);
            JI("goto", lbl_ω);

            /* atom path: stack empty at label */
            J("%s:\n", is_atom);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            JI("iconst_1", ""); JI("aaload", "");
            JI("checkcast", "java/lang/String");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("lconst_0", "");
            J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            J("    goto %s\n", done);

            /* compound path: stack empty at label */
            J("%s:\n", is_compound);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            JI("iconst_1", ""); JI("aaload", "");
            JI("checkcast", "java/lang/String");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            JI("arraylength", ""); JI("iconst_2", ""); JI("isub", ""); JI("i2l", "");
            J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            J("%s:\n", done);
            JI("goto", lbl_γ);
            return;
        }
        /* arg/3: arg(N, Term, Arg) — 1-based argument access */
        if (strcmp(fn, "arg") == 0 && nargs == 3) {
            /* check Term is non-null compound first */
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("ifnull", lbl_ω);
            /* load compound array, index into args: array[N+1] */
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            JI("checkcast", "[Ljava/lang/Object;");
            pj_emit_arith(goal->children[0], var_locals, n_vars);
            JI("l2i", ""); JI("iconst_1", ""); JI("iadd", "");
            JI("aaload", "");  /* args[N-1+2] = array[N+1] */
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* =../2: Term =.. List (univ)
         * foo(a,b) =.. [foo,a,b];  atom =.. [atom]
         * Delegate entirely to pj_term_to_list helper. */
        if (strcmp(fn, "=..") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    ifnull %s\n", lbl_ω);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_term_to_list(Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
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
            JI("ifnull", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
    }

    /* fallthrough */
    JI("goto", lbl_γ);
}

/* -------------------------------------------------------------------------
 * pj_emit_body — emit N goals with Proebsting retry wiring.
 *
 * User calls: wrapped in a retry loop (E2.fail -> E1.resume).
 * Deterministic goals: linear recursive chain.
 * next_local: pointer to next available JVM local slot (bumped on alloc).
 * ------------------------------------------------------------------------- */
static void pj_emit_body(EXPR_t **goals, int ngoals, const char *lbl_γ,
                         const char *lbl_ω, const char *lbl_outer_ω,
                         int trail_local,
                         int *var_locals, int n_vars, int *next_local,
                         int init_cs_local, int sub_cs_out_local,
                         int cut_cs_seal, int cs_local_for_cut,
                         const char *lbl_pred_ω,
                         const char *lbl_cutγ) {
    if (ngoals == 0) { JI("goto", lbl_γ); return; }

    EXPR_t *g = goals[0];

    if (!g || g->kind == E_TRAIL_MARK || g->kind == E_TRAIL_UNWIND) {
        pj_emit_body(goals + 1, ngoals - 1, lbl_γ, lbl_ω, lbl_outer_ω,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local,
                     cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
        return;
    }

    if (g->kind == E_CUT) {
        /* seal β: store base[nclauses] into cs_local so next dispatch → ω */
        if (cut_cs_seal >= 0 && cs_local_for_cut >= 0) {
            J("    ldc %d\n", cut_cs_seal);
            J("    istore %d\n", cs_local_for_cut);
        }
        /* Remaining goals after cut: failure goes to predicate omega.
         * Success goes to cutgamma which returns base[nclauses] so caller
         * cannot retry — fixes M-PJ-CUT-UCALL double-output. */
        const char *cut_fail = lbl_pred_ω ? lbl_pred_ω : lbl_outer_ω;
        const char *cut_succ = lbl_cutγ ? lbl_cutγ : lbl_γ;
        pj_emit_body(goals + 1, ngoals - 1, cut_succ, cut_fail, cut_fail,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local,
                     cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
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

        int uid          = pj_fresh_label();
        int local_cs     = (*next_local)++;
        int local_rv     = (*next_local)++;
        int local_tmark  = (*next_local)++;  /* per-call trail mark for β-unwind */

        char call_α[128], call_ω[128];
        snprintf(call_α,   sizeof call_α,   "call%d_alpha",   uid);
        snprintf(call_ω, sizeof call_ω, "call%d_omega", uid);

        /* init: local_cs = init_cs_local.
         * init_cs_local is set by the clause dispatcher to (incoming_cs - base[ci]),
         * which is 0 on first entry and non-zero only when resuming a suspended body
         * ucall (i.e., when the CALLER retries this clause for a next solution and
         * the clause itself has an inner ucall that was mid-stream).
         * local_cs is self-contained after init — sfail updates it directly. */
        J("    iload %d\n", init_cs_local);
        J("    istore %d\n", local_cs);

        J("%s:\n", call_α);
        /* Save trail mark before call — unwind here on β-retry to reset
         * any bindings made by the previous solution (e.g. X bound by member). */
        J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
        J("    istore %d\n", local_tmark);
        for (int j = 0; j < nargs && j < g->nchildren; j++)
            pj_emit_term(g->children[j], var_locals, n_vars);
        J("    iload %d\n", local_cs);
        J("    invokestatic %s\n", desc);
        JI("dup", "");
        J("    astore %d\n", local_rv);
        J("    ifnull %s\n", call_ω);
        /* M-PJ-CUT-UCALL: cutgamma propagation guard.
         * If callee contains cut and its last clause has no ucall, the
         * sentinel base[nclauses] is reliable.  Non-null rv with
         * rv[0].intValue() >= callee_sentinel means cut fired inside callee —
         * propagate upward by jumping to lbl_cutγ (caller's cutgamma port).
         * Without this guard, cutgamma is treated as a normal success and the
         * body continues, causing puzzle_18-style double-output. */
        /* M-PJ-CUT-UCALL: if callee has cut and reliable sentinel, propagate cutgamma.
         * MAX_VALUE (2147483647) is the unambiguous sentinel — check rv[0] >= it. */
        if (pj_callee_has_cut_no_last_ucall(fn, nargs)) {
            const char *cut_dest = lbl_cutγ ? lbl_cutγ : call_ω;
            J("    aload %d\n", local_rv);
            JI("iconst_0", "");
            JI("aaload", "");
            JI("checkcast", "java/lang/Integer");
            JI("invokevirtual", "java/lang/Integer/intValue()I");
            J("    ldc 2147483647\n");
            J("    if_icmpeq %s\n", cut_dest);
        }
        /* extract returned cs — store into sub_cs_out_local for γ encoding,
         * and advance local_cs for the next β-retry of THIS call. */
        J("    aload %d\n", local_rv);
        JI("iconst_0", "");
        JI("aaload", "");
        JI("checkcast", "java/lang/Integer");
        JI("invokevirtual", "java/lang/Integer/intValue()I");
        JI("dup", "");
        J("    istore %d\n", sub_cs_out_local);
        /* γ already encodes base[ci]+sub_cs+1 — pass directly as next cs.
         * Do NOT add another +1 here; the predicate's dispatch (cs >= base[ci])
         * handles advancement. Double-incrementing causes clause skipping. */
        J("    istore %d\n", local_cs);
        /* suffix: failure → unwind bindings from this call, then retry */
        {
            int fresh_init = (*next_local)++;
            JI("iconst_0", ""); J("    istore %d\n", fresh_init);
            int fresh_sub = (*next_local)++;
            JI("iconst_0", ""); J("    istore %d\n", fresh_sub);
            char call_β[128];
            snprintf(call_β, sizeof call_β, "call%d_beta", uid);
            pj_emit_body(goals + 1, ngoals - 1, lbl_γ, call_β, lbl_outer_ω,
                         trail_local, var_locals, n_vars, next_local,
                         fresh_init, fresh_sub,
                         cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
            J("%s:\n", call_β);
            /* β port: body conjunction after ucall succeeded has failed.
             * Unwind trail (undo bindings from last solution) then retry the
             * ucall with the next cs.  This is the standard retry driver —
             * call_α increments local_cs from the γ value stored in istore 8
             * above, giving the ucall's next continuation state.
             * NOTE: do NOT update init_cs_local here; that corrupts the outer
             * clause's sub-cs tracking for recursive predicates. */
            J("    iload %d\n", local_tmark);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            JI("goto", call_α);   /* β port: retry ucall for next solution */
        }
        /* ucall exhausted → retry enclosing call (lbl_ω = enclosing beta).
         * Reset local_cs to 0 so the next invocation of this predicate
         * starts fresh.  Without this reset, a retry of the enclosing call
         * (e.g. item(Y) binding a new Y) would re-enter differ with cs
         * already past its last clause, causing an infinite loop. */
        J("%s:\n", call_ω);
        JI("iconst_0", "");
        J("    istore %d\n", local_cs);
        JI("goto", lbl_ω);
        return;
    }

    /* Plain disjunction in body — needs retry loop like a user call.
     * Each arm is a choice point: try arm[local_cs], on success continue
     * body; if body later fails, unwind trail and try arm[local_cs+1].
     * This handles (A ; B), (A ; B ; C), etc. without ITE.
     * ITE (Cond -> Then ; Else) is handled above in pj_emit_goal. */
    if (g->kind == E_FNC && g->sval && strcmp(g->sval, ";") == 0 &&
        g->nchildren >= 2 &&
        !(g->children[0] && g->children[0]->kind == E_FNC &&
          g->children[0]->sval && strcmp(g->children[0]->sval, "->") == 0)) {
        int narms = g->nchildren;
        int uid = pj_fresh_label();
        int local_cs    = (*next_local)++;
        int local_tmark = (*next_local)++;
        char dj_α[128], dj_β[128], dj_ω[128];
        snprintf(dj_α, sizeof dj_α, "dj%d_alpha", uid);
        snprintf(dj_β, sizeof dj_β, "dj%d_beta",  uid);
        snprintf(dj_ω, sizeof dj_ω, "dj%d_omega", uid);

        /* init local_cs = 0 on first entry (disjunction always starts fresh) */
        J("    iconst_0\n");
        J("    istore %d\n", local_cs);

        J("%s:\n", dj_α);
        /* save trail mark so β can undo bindings from previous arm */
        J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
        J("    istore %d\n", local_tmark);

        /* dispatch: if local_cs >= narms → dj_ω */
        J("    iload %d\n", local_cs);
        J("    ldc %d\n", narms);
        J("    if_icmpge %s\n", dj_ω);

        /* tableswitch on local_cs → arm labels */
        char arm_done[128];
        snprintf(arm_done, sizeof arm_done, "dj%d_arm_done", uid);
        char **arm_ok  = (char**)malloc(narms * sizeof(char*));
        char **arm_fail = (char**)malloc(narms * sizeof(char*));
        for (int ai = 0; ai < narms; ai++) {
            arm_ok[ai]   = (char*)malloc(128);
            arm_fail[ai] = (char*)malloc(128);
            snprintf(arm_ok[ai],   128, "dj%d_arm%d_ok",   uid, ai);
            snprintf(arm_fail[ai], 128, "dj%d_arm%d_fail", uid, ai);
        }
        J("    iload %d\n", local_cs);
        J("    tableswitch 0\n");
        for (int ai = 0; ai < narms; ai++)
            J("        %s\n", arm_ok[ai]);
        J("        default: %s\n", dj_ω);

        /* emit each arm: success falls to arm_done, failure to dj_β */
        for (int ai = 0; ai < narms; ai++) {
            J("%s:\n", arm_ok[ai]);
            EXPR_t *arm = g->children[ai];
            int next_local_arm = *next_local;
            int ics_arm = next_local_arm++;
            int sco_arm = next_local_arm++;
            *next_local = next_local_arm;
            J("    iconst_0\n"); J("    istore %d\n", ics_arm);
            J("    iconst_0\n"); J("    istore %d\n", sco_arm);
            if (arm && arm->kind == E_FNC && arm->sval && strcmp(arm->sval, ",") == 0) {
                pj_emit_body(arm->children, arm->nchildren,
                             arm_done, arm_fail[ai], arm_fail[ai],
                             trail_local, var_locals, n_vars, next_local,
                             ics_arm, sco_arm, cut_cs_seal, cs_local_for_cut, NULL, lbl_cutγ);
            } else {
                pj_emit_body(&arm, 1,
                             arm_done, arm_fail[ai], arm_fail[ai],
                             trail_local, var_locals, n_vars, next_local,
                             ics_arm, sco_arm, cut_cs_seal, cs_local_for_cut, NULL, lbl_cutγ);
            }
            J("%s:\n", arm_fail[ai]);
            /* arm failed: fall through to dj_β to try next arm */
            J("    goto %s\n", dj_β);
        }

        /* arm succeeded — now emit the rest of the body */
        J("%s:\n", arm_done);
        {
            int fresh_init = (*next_local)++;
            J("    iconst_0\n"); J("    istore %d\n", fresh_init);
            int fresh_sub = (*next_local)++;
            J("    iconst_0\n"); J("    istore %d\n", fresh_sub);
            pj_emit_body(goals + 1, ngoals - 1, lbl_γ, dj_β, lbl_outer_ω,
                         trail_local, var_locals, n_vars, next_local,
                         fresh_init, fresh_sub,
                         cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
        }

        /* β: unwind trail (undo arm bindings), advance to next arm */
        J("%s:\n", dj_β);
        J("    iload %d\n", local_tmark);
        J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
        J("    iload %d\n", local_cs);
        J("    iconst_1\n");
        J("    iadd\n");
        J("    istore %d\n", local_cs);
        J("    goto %s\n", dj_α);

        /* ω: all arms exhausted → outer fail */
        J("%s:\n", dj_ω);
        J("    iconst_0\n");
        J("    istore %d\n", local_cs);
        J("    goto %s\n", lbl_ω);

        for (int ai = 0; ai < narms; ai++) { free(arm_ok[ai]); free(arm_fail[ai]); }
        free(arm_ok); free(arm_fail);
        return;
    }

    /* Deterministic goal */
    {
        int uid = pj_fresh_label();
        char g_γ[128], g_ω[128];
        snprintf(g_γ, sizeof g_γ, "dg%d_gamma", uid);
        snprintf(g_ω, sizeof g_ω, "dg%d_omega", uid);
        pj_emit_goal(g, g_γ, g_ω, trail_local, var_locals, n_vars,
                     cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
        J("%s:\n", g_γ);
        pj_emit_body(goals + 1, ngoals - 1, lbl_γ, lbl_ω, lbl_outer_ω,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local,
                     cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
        J("%s:\n", g_ω);
        JI("goto", lbl_ω);
    }
}

/* -------------------------------------------------------------------------
 * pj_emit_between_builtin — emit synthetic p_between_3 method.
 * Signature: p_between_3([Low], [High], [Var], cs) → Object[] | null
 * cs = offset from Low: try value Low+cs each invocation.
 * Returns {cs+1} on success so caller retries with the next value.
 * ------------------------------------------------------------------------- */
static void pj_emit_between_builtin(void) {
    J("; === between/3 synthetic predicate ========================================\n");
    J(".method static p_between_3([Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 8\n");
    /* locals: 0=Low, 1=High, 2=Var, 3=cs, 4-5=cur(long), 6-7=high(long) */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    J("    iload_3\n");
    JI("i2l", "");
    JI("ladd", "");
    J("    lstore 4\n");                /* cur = Low + cs */
    J("    aload_1\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    J("    lstore 6\n");                /* high */
    J("    lload 4\n");
    J("    lload 6\n");
    JI("lcmp", "");
    J("    ifgt p_between_3_fail\n");   /* cur > high → fail */
    J("    lload 4\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_2\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq p_between_3_fail\n");   /* unify failed → fail */
    JI("iconst_1", "");
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    J("    iload_3\n");
    JI("iconst_1", "");
    JI("iadd", "");
    JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
    JI("aastore", "");
    JI("areturn", "");
    J("p_between_3_fail:\n");
    JI("aconst_null", "");
    JI("areturn", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * pj_emit_findall_builtin — emit synthetic p_findall_3 method.
 *
 * findall(Template, Goal, List) — collect all solutions to Goal,
 * binding Template on each, copy each bound template, unify List with
 * the resulting Prolog list.  Never fails (returns [] on no solutions).
 *
 * Signature: p_findall_3([Template], [Goal], [List], cs) → Object[] | null
 * cs is always 0 on entry (findall is deterministic / never retried).
 *
 * Runtime strategy:
 *   1. Mark the trail.
 *   2. Use reflection to call p_functor_arity(args..., cs) in a loop,
 *      incrementing cs until null (ω).
 *   3. On each success: deep-copy the deref'd template, add to ArrayList.
 *   4. Unwind trail to mark after each attempt to reset bindings.
 *   5. Build Prolog list from ArrayList, unify with List arg.
 *   6. Return {1} (success).
 *
 * Goal dispatch: handles atom "fail/0", "true/0", compound "," (conj),
 * compound "is" (arithmetic), and user predicates via reflection.
 * Conjunction is handled by pj_call_goal_conj helper.
 * ------------------------------------------------------------------------- */
static void pj_emit_findall_builtin(void) {
    /* ---- pj_copy_term: deep-copy a deref'd term with fresh vars ---------- */
    J("; === pj_copy_term (deep copy for findall) ===========================\n");
    J(".method static pj_copy_term(Ljava/lang/Object;)Ljava/lang/Object;\n");
    J("    .limit stack 10\n");
    J("    .limit locals 6\n");
    /* deref arg */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_0\n");
    /* if null → return null */
    J("    aload_0\n");
    J("    ifnonnull pj_ct_notnull\n");
    J("    aconst_null\n");
    J("    areturn\n");
    J("pj_ct_notnull:\n");
    J("    aload_0\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    astore_1\n");  /* t = (Object[]) term */
    /* tag = t[0] */
    J("    aload_1\n");
    J("    iconst_0\n");
    J("    aaload\n");
    J("    astore_2\n");  /* tag string */
    /* if tag == "var" → return fresh var */
    J("    aload_2\n");
    J("    ldc \"var\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_ct_not_var\n");
    J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_ct_not_var:\n");
    /* if tag == "atom" → return same (atoms are immutable) */
    J("    aload_2\n");
    J("    ldc \"atom\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_ct_not_atom\n");
    J("    aload_1\n");  /* return same atom */
    J("    areturn\n");
    J("pj_ct_not_atom:\n");
    /* if tag == "int" → return same (ints are immutable) */
    J("    aload_2\n");
    J("    ldc \"int\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_ct_not_int\n");
    J("    aload_1\n");
    J("    areturn\n");
    J("pj_ct_not_int:\n");
    /* compound: copy all args */
    J("    aload_1\n");
    J("    arraylength\n");
    J("    istore_3\n");  /* len */
    J("    iload_3\n");
    J("    anewarray java/lang/Object\n");
    J("    astore 4\n");  /* newArr */
    /* copy [0] tag and [1] functor unchanged */
    J("    aload 4\n"); J("    iconst_0\n"); J("    aload_1\n"); J("    iconst_0\n"); J("    aaload\n"); J("    aastore\n");
    J("    aload 4\n"); J("    iconst_1\n"); J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n"); J("    aastore\n");
    /* copy args [2..len-1] recursively */
    J("    iconst_2\n");
    J("    istore 5\n");  /* i = 2 */
    J("pj_ct_arg_loop:\n");
    J("    iload 5\n");
    J("    iload_3\n");
    J("    if_icmpge pj_ct_done\n");
    J("    aload 4\n");
    J("    iload 5\n");
    J("    aload_1\n");
    J("    iload 5\n");
    J("    aaload\n");  /* t[i] */
    J("    invokestatic %s/pj_copy_term(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    aastore\n");
    J("    iinc 5 1\n");
    J("    goto pj_ct_arg_loop\n");
    J("pj_ct_done:\n");
    J("    aload 4\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* ---- pj_call_goal: interpret a goal term.
     * Returns new_cs (>=0) on success so caller can resume, -1 on failure.
     * For user predicates: extracts result[0].intValue() as the new cs.
     * For builtins (true, is, conj): returns 0 on success (deterministic).
     * Caller (p_findall_3) stores the returned value as goal_cs for next iter.
     * --------------------------------------------------------------------- */
    J("; === pj_call_goal (goal interpreter for findall) ====================\n");
    J(".method static pj_call_goal(Ljava/lang/Object;I)I\n");
    J("    .limit stack 20\n");
    J("    .limit locals 12\n");
    /* local 0 = goal, local 1 = cs, local 2..11 = scratch */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_0\n");
    J("    aload_0\n");
    J("    ifnull pj_cg_fail\n");
    J("    aload_0\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    astore 2\n");  /* t */
    /* get tag */
    J("    aload 2\n"); J("    iconst_0\n"); J("    aaload\n"); J("    checkcast java/lang/String\n"); J("    astore 3\n"); /* tag */
    /* atom? */
    J("    aload 3\n");
    J("    ldc \"atom\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_not_atom\n");
    /* atom: get name */
    J("    aload 2\n"); J("    iconst_1\n"); J("    aaload\n"); J("    checkcast java/lang/String\n"); J("    astore 4\n");
    /* fail? */
    J("    aload 4\n"); J("    ldc \"fail\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_not_fail_atom\n");
    J("    ldc -1\n"); J("    ireturn\n");
    J("pj_cg_not_fail_atom:\n");
    /* true? */
    J("    aload 4\n"); J("    ldc \"true\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_atom_user\n");
    J("    iconst_0\n"); J("    ireturn\n");
    J("pj_cg_atom_user:\n");
    /* atom user predicate (arity 0) — call via reflection, return new cs */
    J("    aload 4\n");      /* functor name */
    J("    iconst_0\n");     /* arity 0 */
    J("    aconst_null\n");  /* args[] = null for arity 0 */
    J("    iload_1\n");      /* cs */
    J("    invokestatic %s/pj_reflect_call(Ljava/lang/String;I[Ljava/lang/Object;I)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 5\n");
    J("    aload 5\n"); J("    ifnull pj_cg_fail\n");
    /* extract new cs from result[0].intValue() */
    J("    aload 5\n"); J("    iconst_0\n"); J("    aaload\n");
    J("    checkcast java/lang/Integer\n");
    J("    invokevirtual java/lang/Integer/intValue()I\n");
    J("    ireturn\n");
    J("pj_cg_not_atom:\n");
    /* compound */
    J("    aload 3\n"); J("    ldc \"compound\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_fail\n");
    /* functor */
    J("    aload 2\n"); J("    iconst_1\n"); J("    aaload\n"); J("    checkcast java/lang/String\n"); J("    astore 4\n");
    /* arity = arraylength - 2 */
    J("    aload 2\n"); J("    arraylength\n"); J("    iconst_2\n"); J("    isub\n"); J("    istore 5\n");
    /* conjunction? */
    J("    aload 4\n"); J("    ldc \",\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_not_conj\n");
    /* conjunction: pass cs to left (backtracking), right always cs=0 (det).
     * Return left_new_cs so p_findall_3 can advance left on next iteration. (PJ-46) */
    J("    aload 2\n"); J("    iconst_2\n"); J("    aaload\n");  /* left */
    J("    iload_1\n");  /* pass incoming cs to left — was iconst_0 */
    J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
    J("    istore 9\n");  /* left_new_cs */
    J("    iload 9\n"); J("    ldc -1\n"); J("    if_icmpeq pj_cg_fail\n");
    J("    aload 2\n"); J("    iconst_3\n"); J("    aaload\n");  /* right */
    J("    iconst_0\n");
    J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
    J("    ldc -1\n"); J("    if_icmpeq pj_cg_fail\n");
    J("    iload 9\n"); J("    ireturn\n");  /* return left_new_cs → findall advances left */
    J("pj_cg_not_conj:\n");
    /* is/2? */
    J("    aload 4\n"); J("    ldc \"is\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_not_is\n");
    /* is(Var, Expr): evaluate expr, unify with var — return 0 on success, -1 on fail */
    J("    aload 2\n"); J("    iconst_2\n"); J("    aaload\n"); J("    astore 6\n"); /* lhs */
    J("    aload 2\n"); J("    iconst_3\n"); J("    aaload\n"); J("    astore 7\n"); /* rhs expr */
    J("    aload 7\n");
    J("    invokestatic %s/pj_eval_arith(Ljava/lang/Object;)J\n", pj_classname);
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 8\n");
    J("    aload 6\n"); J("    aload 8\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_cg_fail\n");
    J("    iconst_0\n"); J("    ireturn\n");
    J("pj_cg_not_is:\n");
    /* user predicate: build args array, call via reflection, return new cs */
    J("    aload 2\n"); J("    arraylength\n"); J("    iconst_2\n"); J("    isub\n"); J("    istore 5\n"); /* arity */
    J("    iload 5\n");
    J("    anewarray java/lang/Object\n");
    J("    astore 6\n");  /* args array */
    J("    iconst_0\n"); J("    istore 7\n"); /* i=0 */
    J("pj_cg_args_loop:\n");
    J("    iload 7\n"); J("    iload 5\n"); J("    if_icmpge pj_cg_args_done\n");
    J("    aload 6\n"); J("    iload 7\n");
    J("    aload 2\n"); J("    iload 7\n"); J("    iconst_2\n"); J("    iadd\n"); J("    aaload\n"); /* t[i+2] */
    J("    aastore\n");
    J("    iinc 7 1\n");
    J("    goto pj_cg_args_loop\n");
    J("pj_cg_args_done:\n");
    J("    aload 4\n"); /* functor */
    J("    iload 5\n"); /* arity */
    J("    aload 6\n"); /* args */
    J("    iload_1\n"); /* cs */
    J("    invokestatic %s/pj_reflect_call(Ljava/lang/String;I[Ljava/lang/Object;I)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 8\n");
    J("    aload 8\n"); J("    ifnull pj_cg_fail\n");
    /* extract new cs from result[0].intValue() */
    J("    aload 8\n"); J("    iconst_0\n"); J("    aaload\n");
    J("    checkcast java/lang/Integer\n");
    J("    invokevirtual java/lang/Integer/intValue()I\n");
    J("    ireturn\n");
    J("pj_cg_fail:\n");
    J("    ldc -1\n"); J("    ireturn\n");
    J(".end method\n\n");

    /* ---- pj_eval_arith: evaluate arithmetic term, return long ------------ */
    J("; === pj_eval_arith (arithmetic evaluator for findall/is) ============\n");
    J(".method static pj_eval_arith(Ljava/lang/Object;)J\n");
    J("    .limit stack 10\n");
    J("    .limit locals 8\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_0\n");
    J("    aload_0\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    astore_1\n");
    J("    aload_1\n"); J("    iconst_0\n"); J("    aaload\n"); J("    checkcast java/lang/String\n"); J("    astore_2\n"); /* tag */
    /* int? */
    J("    aload_2\n"); J("    ldc \"int\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_ea_not_int\n");
    J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n"); J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    lreturn\n");
    J("pj_ea_not_int:\n");
    /* compound: get functor */
    J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n"); J("    checkcast java/lang/String\n"); J("    astore_3\n");
    /* eval left (arg[0] = t[2]) and right (arg[1] = t[3]) */
    J("    aload_1\n"); J("    iconst_2\n"); J("    aaload\n");
    J("    invokestatic %s/pj_eval_arith(Ljava/lang/Object;)J\n", pj_classname);
    J("    lstore 4\n");  /* L */
    /* check arity before loading right */
    J("    aload_1\n"); J("    arraylength\n"); J("    iconst_3\n"); J("    if_icmplt pj_ea_unary\n");
    J("    aload_1\n"); J("    iconst_3\n"); J("    aaload\n");
    J("    invokestatic %s/pj_eval_arith(Ljava/lang/Object;)J\n", pj_classname);
    J("    lstore 6\n");  /* R */
    /* dispatch on functor */
    J("    aload_3\n"); J("    ldc \"+\"\n"); J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n"); J("    ifeq pj_ea_not_add\n");
    J("    lload 4\n"); J("    lload 6\n"); J("    ladd\n"); J("    lreturn\n");
    J("pj_ea_not_add:\n");
    J("    aload_3\n"); J("    ldc \"-\"\n"); J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n"); J("    ifeq pj_ea_not_sub\n");
    J("    lload 4\n"); J("    lload 6\n"); J("    lsub\n"); J("    lreturn\n");
    J("pj_ea_not_sub:\n");
    J("    aload_3\n"); J("    ldc \"*\"\n"); J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n"); J("    ifeq pj_ea_not_mul\n");
    J("    lload 4\n"); J("    lload 6\n"); J("    lmul\n"); J("    lreturn\n");
    J("pj_ea_not_mul:\n");
    J("    aload_3\n"); J("    ldc \"/\"\n"); J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n"); J("    ifeq pj_ea_not_div\n");
    J("    lload 4\n"); J("    lload 6\n"); J("    ldiv\n"); J("    lreturn\n");
    J("pj_ea_not_div:\n");
    J("    aload_3\n"); J("    ldc \"mod\"\n"); J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n"); J("    ifeq pj_ea_not_mod\n");
    J("    lload 4\n"); J("    lload 6\n"); J("    lrem\n"); J("    lreturn\n");
    J("pj_ea_not_mod:\n");
    J("    lload 4\n"); J("    lreturn\n"); /* fallback */
    J("pj_ea_unary:\n");
    /* unary minus */
    J("    aload_3\n"); J("    ldc \"-\"\n"); J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n"); J("    ifeq pj_ea_unary_other\n");
    J("    lload 4\n"); J("    lneg\n"); J("    lreturn\n");
    J("pj_ea_unary_other:\n");
    J("    lload 4\n"); J("    lreturn\n");
    J(".end method\n\n");

    /* ---- pj_reflect_call: call p_name_arity via reflection --------------- */
    /* Signature: pj_reflect_call(String functor, int arity, Object[] args, int cs) → Object[] | null */
    J("; === pj_reflect_call (reflection dispatch for findall) ==============\n");
    J(".method static pj_reflect_call(Ljava/lang/String;I[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 20\n");
    J("    .limit locals 14\n");
    /* wrap entire body in try/catch Exception → return null */
    J("    .catch java/lang/Exception from pj_rc_try_start to pj_rc_try_end using pj_rc_catch\n");
    J("pj_rc_try_start:\n");
    /* build method name: "p_" + functor + "_" + arity */
    J("    ldc \"p_\"\n");
    J("    aload_0\n");
    J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    ldc \"_\"\n");
    J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    iload_1\n");
    J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
    J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    astore 4\n");  /* methodName = "p_functor_arity" */
    /* build param type array: [Object[]]*arity + [int] */
    J("    iload_1\n"); J("    iconst_1\n"); J("    iadd\n");
    J("    anewarray java/lang/Class\n");
    J("    astore 5\n");  /* paramTypes */
    J("    iconst_0\n"); J("    istore 6\n"); /* i=0 */
    J("pj_rc_param_loop:\n");
    J("    iload 6\n"); J("    iload_1\n"); J("    if_icmpge pj_rc_param_done\n");
    J("    aload 5\n"); J("    iload 6\n");
    /* Get Object[].class by creating a zero-length Object[] and calling getClass() */
    J("    iconst_0\n");
    J("    anewarray java/lang/Object\n");
    J("    invokevirtual java/lang/Object/getClass()Ljava/lang/Class;\n");
    J("    aastore\n");
    J("    iinc 6 1\n"); J("    goto pj_rc_param_loop\n");
    J("pj_rc_param_done:\n");
    /* last param: int.class */
    J("    aload 5\n"); J("    iload_1\n");
    J("    getstatic java/lang/Integer/TYPE Ljava/lang/Class;\n");
    J("    aastore\n");
    /* get class */
    J("    ldc \"%s\"\n", pj_classname);
    J("    invokestatic java/lang/Class/forName(Ljava/lang/String;)Ljava/lang/Class;\n");
    J("    astore 7\n");
    /* getDeclaredMethod(name, paramTypes) — finds non-public methods too */
    J("    aload 7\n");
    J("    aload 4\n");
    J("    aload 5\n");
    J("    invokevirtual java/lang/Class/getDeclaredMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;\n");
    J("    astore 8\n");  /* method */
    /* setAccessible(true) so we can call package-private static methods */
    J("    aload 8\n");
    J("    iconst_1\n");
    J("    invokevirtual java/lang/reflect/Method/setAccessible(Z)V\n");
    /* build invoke args: [args[0], args[1], ..., Integer(cs)] */
    J("    iload_1\n"); J("    iconst_1\n"); J("    iadd\n");
    J("    anewarray java/lang/Object\n");
    J("    astore 9\n");
    J("    iconst_0\n"); J("    istore 6\n");
    J("pj_rc_invoke_loop:\n");
    J("    iload 6\n"); J("    iload_1\n"); J("    if_icmpge pj_rc_invoke_done\n");
    J("    aload 9\n"); J("    iload 6\n");
    J("    aload_2\n"); J("    iload 6\n"); J("    aaload\n");
    J("    aastore\n");
    J("    iinc 6 1\n"); J("    goto pj_rc_invoke_loop\n");
    J("pj_rc_invoke_done:\n");
    J("    aload 9\n"); J("    iload_1\n");
    J("    iload_3\n");
    J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
    J("    aastore\n");
    /* invoke: null target for static */
    J("    aload 8\n");
    J("    aconst_null\n");
    J("    aload 9\n");
    J("    invokevirtual java/lang/reflect/Method/invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;\n");
    J("pj_rc_try_end:\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    areturn\n");
    J("pj_rc_catch:\n");
    J("    pop\n");  /* discard exception */
    J("    aconst_null\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* ---- p_findall_3: the main findall predicate -------------------------- */
    J("; === findall/3 synthetic predicate ===================================\n");
    J(".method static p_findall_3([Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 20\n");
    J("    .limit locals 16\n");
    /* locals: 0=template, 1=goal, 2=list_var, 3=cs(unused), 4=results(ArrayList),
               5=trail_mark, 6=goal_cs, 7=call_result, 8=copied_template,
               9=prolog_list(built), 10=nil, 11=cons */
    /* results = new ArrayList */
    J("    new java/util/ArrayList\n");
    J("    dup\n");
    J("    invokespecial java/util/ArrayList/<init>()V\n");
    J("    astore 4\n");
    /* save trail mark */
    J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
    J("    istore 5\n");
    /* goal_cs = 0 */
    J("    iconst_0\n"); J("    istore 6\n");
    /* loop: call goal with goal_cs, collect solutions */
    J("pj_fa_loop:\n");
    /* call pj_call_goal(goal, goal_cs) → new_cs or -1 */
    J("    aload_1\n");
    J("    iload 6\n");
    J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
    J("    istore 6\n");        /* goal_cs = returned new_cs (or -1) */
    J("    iload 6\n");
    J("    ldc -1\n");
    J("    if_icmpeq pj_fa_done\n");  /* -1 = failure → done */
    /* success: copy template and add to results */
    J("    aload_0\n");
    J("    invokestatic %s/pj_copy_term(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore 8\n");
    J("    aload 4\n");
    J("    aload 8\n");
    J("    invokevirtual java/util/ArrayList/add(Ljava/lang/Object;)Z\n");
    J("    pop\n");
    /* unwind trail to reset bindings */
    J("    iload 5\n");
    J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
    /* goal_cs already updated above — loop back */
    J("    goto pj_fa_loop\n");
    J("pj_fa_done:\n");
    /* unwind any residual trail */
    J("    iload 5\n");
    J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
    /* build Prolog list from results ArrayList (reverse iterate) */
    /* nil = atom("[]") */
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 9\n");
    /* i = results.size()-1, down to 0 */
    J("    aload 4\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    iconst_1\n"); J("    isub\n"); J("    istore 10\n");
    J("pj_fa_list_loop:\n");
    J("    iload 10\n"); J("    iflt pj_fa_list_done\n");
    /* cons = ["compound", ".", element, tail] */
    J("    bipush 4\n"); J("    anewarray java/lang/Object\n"); J("    astore 11\n");
    J("    aload 11\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
    J("    aload 11\n"); J("    iconst_1\n"); J("    ldc \".\"\n"); J("    aastore\n");
    J("    aload 11\n"); J("    iconst_2\n");
    J("    aload 4\n"); J("    iload 10\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    aastore\n");
    J("    aload 11\n"); J("    iconst_3\n"); J("    aload 9\n"); J("    aastore\n");
    J("    aload 11\n"); J("    astore 9\n");  /* tail = cons */
    J("    iinc 10 -1\n");
    J("    goto pj_fa_list_loop\n");
    J("pj_fa_list_done:\n");
    /* unify list var with built list */
    J("    aload_2\n");  /* list_var */
    J("    aload 9\n");  /* prolog_list */
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_fa_fail\n");
    /* return success {1} */
    J("    iconst_1\n");
    J("    anewarray java/lang/Object\n");
    J("    dup\n");
    J("    iconst_0\n");
    J("    iconst_1\n");
    J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
    J("    aastore\n");
    J("    areturn\n");
    J("pj_fa_fail:\n");
    J("    aconst_null\n"); J("    areturn\n");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Clause emitter
 * Emits one clause block inside the predicate method (switch case).
 * Layout: p_functor_arity(arg0..argN-1, cs) — cs = continuation state
 * Returns Object[] = args array on success, null on failure.
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * pj_predicate_base_nclauses — M-PJ-CUT-UCALL
 * Returns base[nclauses] for the named predicate (fn/arity key).
 * Since stride=1 and base[0]=0, base[nclauses] == nclauses exactly.
 * Returns -1 if predicate not found or too large.
 * ------------------------------------------------------------------------- */
static int pj_predicate_base_nclauses(const char *fn, int arity) {
    if (!pj_prog || !fn) return -1;
    char key[256];
    snprintf(key, sizeof key, "%s/%d", fn, arity);
    for (STMT_t *s = pj_prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_CHOICE) continue;
        if (!s->subject->sval || strcmp(s->subject->sval, key) != 0) continue;
        int nc = s->subject->nchildren;
        if (nc <= 0 || nc > 64) return -1;
        return nc;  /* base[nclauses] = nclauses (stride=1, base[0]=0) */
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * pj_callee_has_cut_no_last_ucall — M-PJ-CUT-UCALL
 * Returns 1 if the named predicate has any cut AND its last clause has no
 * user call (i.e., base[nclauses] is a reliable sentinel — not open-ended).
 * ------------------------------------------------------------------------- */
static int pj_callee_has_cut_no_last_ucall(const char *fn, int arity) {
    if (!pj_prog || !fn) return 0;
    char key[256];
    snprintf(key, sizeof key, "%s/%d", fn, arity);
    for (STMT_t *s = pj_prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_CHOICE) continue;
        if (!s->subject->sval || strcmp(s->subject->sval, key) != 0) continue;
        EXPR_t *choice = s->subject;
        int nc = choice->nchildren;
        if (nc <= 0 || nc > 64) return 0;
        /* scan all clauses for cut */
        int any_cut = 0;
        for (int ci = 0; ci < nc && !any_cut; ci++) {
            EXPR_t *cl = choice->children[ci];
            if (!cl) continue;
            int nv = (int)cl->dval;
            int nb = (int)cl->nchildren - nv;
            if (nb < 0) nb = 0;
            for (int bi = 0; bi < nb && !any_cut; bi++)
                if (pj_body_has_cut(cl->children[nv + bi])) any_cut = 1;
        }
        if (!any_cut) return 0;
        /* check last clause has no user call */
        EXPR_t *last = choice->children[nc - 1];
        if (!last) return 1;
        int nv_last = (int)last->dval;
        int nb_last = (int)last->nchildren - nv_last;
        if (nb_last < 0) nb_last = 0;
        for (int bi = 0; bi < nb_last; bi++) {
            EXPR_t *g = last->children[nv_last + bi];
            if (g && pj_is_user_call(g)) return 0;  /* last has ucall — sentinel unreliable */
        }
        return 1;
    }
    return 0;
}

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
    /* base[nclauses]: one-past-end sentinel for the dispatch omega guard */
    base[nclauses] = base[nclauses - 1] + 1;
    /* last_has_ucall: does the last clause have any body user-calls?
     * If yes, the cs range for that clause is open-ended (sub_cs from inner
     * calls can push cs beyond base[nclauses]), so we CANNOT add an omega guard.
     * If no (pure fact/builtin), cs >= base[nclauses] means exhausted → ω safe. */
    int last_has_ucall = 0;
    int any_has_cut = 0;
    {
        EXPR_t *last = choice->children[nclauses - 1];
        int nv_args_last = last ? (int)last->dval : 0;
        int nb_last = last ? (int)last->nchildren - nv_args_last : 0;
        if (nb_last < 0) nb_last = 0;
        for (int bi = 0; bi < nb_last && !last_has_ucall; bi++) {
            EXPR_t *g = last->children[nv_args_last + bi];
            if (g && pj_is_user_call(g)) last_has_ucall = 1;
        }
        /* scan all clauses for cut presence — recursive so nested "," and ";" are checked */
        for (int ci2 = 0; ci2 < nclauses && !any_has_cut; ci2++) {
            EXPR_t *cl = choice->children[ci2];
            if (!cl) continue;
            int nv2 = (int)cl->dval;
            int nb2 = (int)cl->nchildren - nv2;
            if (nb2 < 0) nb2 = 0;
            for (int bi2 = 0; bi2 < nb2 && !any_has_cut; bi2++) {
                if (pj_body_has_cut(cl->children[nv2 + bi2])) any_has_cut = 1;
            }
        }
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
    /* count max user calls in any single clause body — each allocates 5 locals */
    int max_ucalls = 0;
    int max_neq = 0;
    int max_disj = 0;
    int max_stack = 16;
    for (int ci2 = 0; ci2 < nclauses; ci2++) {
        EXPR_t *cl2 = choice->children[ci2];
        if (!cl2 || cl2->kind != E_CLAUSE) continue;
        int n_args_ci2 = (int)cl2->dval;
        int nb2 = cl2->nchildren - n_args_ci2;
        if (nb2 < 0) nb2 = 0;
        int uc = pj_count_ucalls(cl2->children + n_args_ci2, nb2);
        if (uc > max_ucalls) max_ucalls = uc;
        int nq = pj_count_neq(cl2->children + n_args_ci2, nb2);
        if (nq > max_neq) max_neq = nq;
        int dj = pj_count_disj_locals(cl2->children + n_args_ci2, nb2);
        if (dj > max_disj) max_disj = dj;
        /* also check head args for deep terms */
        int s = pj_clause_stack_needed(cl2->children + n_args_ci2, nb2);
        for (int hi = 0; hi < n_args_ci2; hi++) {
            int d = pj_term_stack_depth(cl2->children[hi]) + 4;
            if (d > s) s = d;
        }
        if (s > max_stack) max_stack = s;
    }
    int locals_needed = vars_base + max_vars + 5 * max_ucalls + 2 * max_neq + max_disj + 16 + 42;

    J("    .limit stack %d\n", max_stack < 16 ? 16 : max_stack);
    J("    .limit locals %d\n", locals_needed);

    /* dispatch: linear scan from last clause down.
     * cs >= base[ci] → enter clause ci.
     * Mirrors ASM: cmp eax, base[ci]; jge pl_name_cN_α
     *
     * CUT SENTINEL GUARD: if last clause has ucalls AND a cut, add exact-equality
     * guard cs == base[nclauses] → omega.  cutgamma returns base[nclauses] as the
     * sentinel; this guard catches it before clause dispatch (the >= range guard
     * is suppressed for last_has_ucall predicates to allow open-ended sub-cs). */
    if (!last_has_ucall) {
        J("    iload %d ; cs >= %d (all clauses exhausted)? → omega\n    ldc %d\n    if_icmpge p_%s_%d_omega\n",
          cs_local, base[nclauses], base[nclauses], safe_fn, arity);
    } else if (any_has_cut) {
        /* M-PJ-CUT-UCALL: use MAX_VALUE as unambiguous cutgamma sentinel.
         * base[nclauses] collides with last-clause γ return (base[nc-1]+0+1==nclauses).
         * Integer.MAX_VALUE (0x7fffffff) is never a legitimate cs value. */
        J("    iload %d ; cs == 0x7fffffff (cutgamma sentinel)? -> omega\n    ldc 2147483647\n    if_icmpeq p_%s_%d_omega\n",
          cs_local, safe_fn, arity);
    }
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

        char α_lbl[128], ω_lbl[128];
        snprintf(α_lbl, sizeof α_lbl, "p_%s_%d_clause%d", safe_fn, arity, ci);
        if (ci + 1 < nclauses)
            snprintf(ω_lbl, sizeof ω_lbl, "p_%s_%d_clause%d", safe_fn, arity, ci + 1);
        else
            snprintf(ω_lbl, sizeof ω_lbl, "p_%s_%d_omega", safe_fn, arity);

        /* α port */
        J("%s:\n", α_lbl);
        /* Compute init_cs = max(0, cs - base[ci]).
         * 0 means fresh entry; positive means resuming a suspended body ucall.
         * CLAMP to 0: when alphafail routes from an earlier clause to this one,
         * cs < base[ci] (e.g. cs=0, base[ci]=1 → would give -1).
         * A negative init_cs propagates into body ucall cs arguments, causing
         * the callee's dispatch to hit omega immediately (cs < 0 < base[0]).
         * Clamping ensures fresh-entry semantics on any alphafail path. */
        J("    iload %d\n", cs_local);
        J("    ldc %d\n", base[ci]);
        JI("isub", "");
        JI("dup", "");
        {
            char lbl_clamp[128];
            snprintf(lbl_clamp, sizeof lbl_clamp, "p_%s_%d_initcs_clamp_%d",
                     safe_fn, arity, ci);
            J("    ifge %s\n", lbl_clamp);
            JI("pop", "");
            JI("iconst_0", "");
            J("%s:\n", lbl_clamp);
        }
        J("    istore %d\n", init_cs_local);
        /* Reset sub_cs_out to 0 */
        JI("iconst_0", "");
        J("    istore %d\n", sub_cs_out_local);

        /* trail mark */
        J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
        J("    istore %d\n", trail_local);

        /* allocate variable cells.
         * Build jvm_arg_for_slot[]: for each var slot, which JVM arg local
         * provides its initial binding (-1 = none, allocate fresh var cell).
         * A var slot is a "direct arg" when head position ai is E_VART with
         * ival==slot.  slot and ai can differ (e.g. append([H|T],L,[H|R]):
         * H=slot0, T=slot1, L=slot2, R=slot3; L is at head arg position 1
         * with slot 2 — old code failed because it tested slot==ai). */
        int *jvm_arg_for_slot = calloc(n_vars + 1, sizeof(int));
        for (int vi = 0; vi < n_vars; vi++) jvm_arg_for_slot[vi] = -1;
        for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
            EXPR_t *ht = clause->children[ai];
            if (ht && ht->kind == E_VART && ht->ival >= 0 && ht->ival < n_vars)
                if (jvm_arg_for_slot[ht->ival] < 0)   /* first occurrence wins */
                    jvm_arg_for_slot[ht->ival] = ai;
        }
        for (int vi = 0; vi < n_vars; vi++) {
            if (jvm_arg_for_slot[vi] >= 0) {
                J("    aload %d\n", jvm_arg_for_slot[vi]);
                J("    astore %d\n", var_locals[vi]);
            } else {
                J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", var_locals[vi]);
            }
        }
        free(jvm_arg_for_slot);

        /* head unification.
         * For compound/atom/int head terms: unify arg[ai] with the emitted term.
         * For E_VART head terms: the var was initialized from the FIRST occurrence's
         * JVM arg (via jvm_arg_for_slot above).  Subsequent occurrences of the same
         * slot need an explicit unify to enforce non-linear equality.
         * E.g. append([],L,L): L=slot0, first at ai=1 → slot loaded from arg1.
         * At ai=2: also E_VART slot0, but var_locals[0] already holds arg1 value.
         * We must unify arg2 with var_locals[0] to enforce arg1==arg2. */
        {
            /* seen_at_arg[slot] = first arg index that claimed this slot, or -1 */
            int *seen_at = calloc(n_vars + 1, sizeof(int));
            for (int vi = 0; vi < n_vars; vi++) seen_at[vi] = -1;
            /* first pass: record first-claim arg for each var slot */
            for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
                EXPR_t *ht = clause->children[ai];
                if (ht && ht->kind == E_VART && ht->ival >= 0 && ht->ival < n_vars) {
                    if (seen_at[ht->ival] < 0) seen_at[ht->ival] = ai;
                }
            }
            for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
                EXPR_t *head_term = clause->children[ai];
                if (!head_term) continue;
                if (head_term->kind == E_VART) {
                    int slot = head_term->ival;
                    if (slot < 0 || slot >= n_vars) continue;
                    if (seen_at[slot] == ai) continue;  /* first occurrence: no-op */
                    /* second+ occurrence: unify arg[ai] with the var cell */
                    J("    aload %d\n", ai);
                    J("    aload %d\n", var_locals[slot]);
                    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                    J("    ifeq p_%s_%d_alphafail_%d\n", safe_fn, arity, ci);
                    continue;
                }
                /* compound/atom/int: standard unify */
                J("    aload %d\n", ai);
                pj_emit_term(head_term, var_locals, n_vars);
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq p_%s_%d_alphafail_%d\n", safe_fn, arity, ci);
            }
            free(seen_at);
        }
        J("    goto p_%s_%d_beta_%d\n", safe_fn, arity, ci);
        J("p_%s_%d_alphafail_%d:\n", safe_fn, arity, ci);
        J("    iload %d\n", trail_local);
        J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
        J("    goto %s\n", ω_lbl);

        /* retry_head label: on β-retry from body ucall, unwind clause
         * trail mark and redo var alloc + head unification with updated cs.
         * This correctly handles body-only vars that got bound during head unif. */
        J("p_%s_%d_alpha_%d:\n", safe_fn, arity, ci);
        J("    iload %d\n", trail_local);
        J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
        /* re-allocate variable cells (same fix as initial alloc above) */
        {
            int *jaf = calloc(n_vars + 1, sizeof(int));
            for (int vi = 0; vi < n_vars; vi++) jaf[vi] = -1;
            for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
                EXPR_t *ht = clause->children[ai];
                if (ht && ht->kind == E_VART && ht->ival >= 0 && ht->ival < n_vars)
                    jaf[ht->ival] = ai;
            }
            for (int vi = 0; vi < n_vars; vi++) {
                if (jaf[vi] >= 0) {
                    J("    aload %d\n", jaf[vi]);
                    J("    astore %d\n", var_locals[vi]);
                } else {
                    J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                    J("    astore %d\n", var_locals[vi]);
                }
            }
            free(jaf);
        }
        /* re-run head unification */
        for (int ai = 0; ai < n_args && ai < clause->nchildren; ai++) {
            EXPR_t *head_term = clause->children[ai];
            if (!head_term) continue;
            if (head_term->kind == E_VART) continue;
            J("    aload %d\n", ai);
            pj_emit_term(head_term, var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", ω_lbl);  /* head fail on retry → ω */
        }
        /* fall through to body */

        /* body goals */
        J("p_%s_%d_beta_%d:\n", safe_fn, arity, ci);
        {
            int nbody = clause->nchildren - n_args;
            EXPR_t **body_goals = clause->children + n_args;
            char γ_lbl[128], α_retry_lbl[128];
            snprintf(γ_lbl, sizeof γ_lbl, "p_%s_%d_gamma_%d", safe_fn, arity, ci);
            snprintf(α_retry_lbl, sizeof α_retry_lbl, "p_%s_%d_alpha_%d", safe_fn, arity, ci);
            char pred_ω_lbl[128];
            snprintf(pred_ω_lbl, sizeof pred_ω_lbl, "p_%s_%d_omega", safe_fn, arity);
            char cutγ_lbl[128];
            snprintf(cutγ_lbl, sizeof cutγ_lbl, "p_%s_%d_cutgamma_%d", safe_fn, arity, ci);
            int next_local = vars_base + n_vars;
            /* PJ-24 fix: body-fail trail unwind.
             * When a body goal fails it jumps to lbl_ω (next clause label).
             * But head unification may have bound caller vars that must be
             * undone before the next clause tries its own head unification.
             * Previously the body-fail path skipped trail unwind entirely.
             * Fix: emit a per-clause body_fail trampoline that unwinds the
             * clause's own trail mark, then jumps to the real ω_lbl.
             * Pass this trampoline as lbl_ω to pj_emit_body so ALL body-goal
             * failure paths route through it.
             * PJ-16 fix preserved: outermost body ucall exhaustion still routes
             * to ω_lbl (next clause) rather than α_retry_lbl. */
            char body_fail_lbl[128];
            snprintf(body_fail_lbl, sizeof body_fail_lbl,
                     "p_%s_%d_bodyfail_%d", safe_fn, arity, ci);
            /* lbl_ω=body_fail_lbl: deterministic goal failure unwinds clause trail.
             * lbl_outer_ω=ω_lbl: ucall exhaustion goes directly to next clause;
             * ucall already manages its own trail via local_tmark so the clause-level
             * unwind must NOT fire again (would double-unwind and corrupt bindings). */
            pj_emit_body(body_goals, nbody, γ_lbl, body_fail_lbl, ω_lbl,
                         trail_local, var_locals, n_vars, &next_local,
                         init_cs_local, sub_cs_out_local,
                         base[nclauses], cs_local, pred_ω_lbl, cutγ_lbl);
            /* body_fail trampoline: unwind this clause's trail, jump to next
             * clause (or predicate omega for the last clause). */
            J("p_%s_%d_bodyfail_%d:\n", safe_fn, arity, ci);
            J("    iload %d\n", trail_local);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            J("    goto %s\n", ω_lbl);
        }

        /* γ port: return new_cs for caller to retry this predicate.
         * General case: base[ci] + init_cs + 1  (advances outer clause cs by 1).
         * Special case: nclauses==1 && last_has_ucall — single-clause predicate
         *   whose body contains a ucall (e.g. even/1 :- num(X), ...).
         *   The outer cs (init_cs) is always 0 here; what the caller needs to
         *   advance is the sub_cs from the inner ucall (sub_cs_out_local = local4).
         *   Returning init_cs+1=1 always would restart the inner call from 0
         *   on every retry, producing duplicates (PJ-46 fix). */
        J("p_%s_%d_gamma_%d:\n", safe_fn, arity, ci);
        JI("iconst_1", "");
        JI("anewarray", "java/lang/Object");
        JI("dup", "");
        JI("iconst_0", "");
        if (nclauses == 1 && last_has_ucall) {
            /* Return sub_cs_out so caller can drive the inner ucall forward. */
            J("    iload %d\n", sub_cs_out_local);  /* sub_cs_out (PJ-46) */
        } else {
            /* Multi-clause or no-ucall: advance outer clause cs by 1. */
            J("    ldc %d\n", base[ci]);
            J("    iload %d\n", init_cs_local);
            JI("iadd", "");
            JI("iconst_1", "");
            JI("iadd", "");
        }
        JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
        JI("aastore", "");
        JI("areturn", "");

        /* cutgamma port: cut fired — return {base[nclauses]} sentinel (M-PJ-CUT-UCALL fix).
         * base[nclauses] is the one-past-end value. The dispatch at the top of this
         * predicate now has an unconditional exact-equality guard (cs == base[nclauses]
         * → omega) regardless of last_has_ucall, so re-entry with this sentinel
         * immediately routes to omega without executing any clause body. */
        J("p_%s_%d_cutgamma_%d:\n", safe_fn, arity, ci);
        JI("iconst_1", "");
        JI("anewarray", "java/lang/Object");
        JI("dup", "");
        JI("iconst_0", "");
        /* M-PJ-CUT-UCALL: return MAX_VALUE (2147483647) as unambiguous sentinel.
         * base[nclauses] is ambiguous — last-clause gamma returns the same value. */
        J("    ldc 2147483647\n");
        JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
        JI("aastore", "");
        JI("areturn", "");

        free(var_locals);
    }

    /* ω port — first try dynamic DB, then truly fail */
    J("p_%s_%d_omega:\n", safe_fn, arity);

    /* Dynamic DB walker: key = "name/arity", cs encodes db index as base[nclauses]+idx */
    {
        /* cs range for dynamic clauses: base[nclauses], base[nclauses]+1, ...
         * db_idx = cs - base[nclauses]  (0 = first dynamic clause)
         * On static-clause exhaustion cs < base[nclauses]+N, so we check if
         * cs >= base[nclauses] which is already assured by reaching omega.
         * We use a local for db_idx. */
        int db_idx_local = arity + 4 + 32 + 4; /* safe scratch local beyond all clause locals */
        int db_lbl = pj_fresh_label();
        char db_key[128], db_loop[128], db_hit[128], db_miss[128];
        snprintf(db_key,  sizeof db_key,  "pj_db%d_key",  db_lbl);
        snprintf(db_loop, sizeof db_loop, "pj_db%d_loop", db_lbl);
        snprintf(db_hit,  sizeof db_hit,  "pj_db%d_hit",  db_lbl);
        snprintf(db_miss, sizeof db_miss, "pj_db%d_miss", db_lbl);

        /* compute db_idx = max(0, cs - base[nclauses]) */
        J("    iload %d\n", cs_local);
        J("    ldc %d\n", base[nclauses]);
        JI("isub", "");
        JI("dup", "");
        J("    ifge pj_db%d_store\n", db_lbl);
        JI("pop", "");
        JI("iconst_0", "");
        J("pj_db%d_store:\n", db_lbl);
        J("    istore %d\n", db_idx_local);

        /* retry entry point — db_idx_local already set */
        J("%s:\n", db_loop);

        /* query DB for this index */
        J("    ldc \"%s/%d\"\n", name_only, arity);
        J("    iload %d\n", db_idx_local);
        J("    invokestatic %s/pj_db_query(Ljava/lang/String;I)Ljava/lang/Object;\n", pj_classname);
        JI("dup", "");
        J("    ifnonnull %s\n", db_hit);
        JI("pop", "");
        J("    goto %s\n", db_miss);

        J("%s:\n", db_hit);
        /* stack: term (Object[]) — try to unify with each arg */
        /* For arity 0 (atom fact): just succeed */
        if (arity == 0) {
            JI("pop", ""); /* discard term */
        } else {
            /* term is Object[]: slot 0=functor, slots 1..arity = args */
            JI("checkcast", "[Ljava/lang/Object;");
            /* unify each arg */
            int db_term_local = db_idx_local + 1;
            J("    astore %d\n", db_term_local);
            /* save trail mark for backtrack-on-fail */
            J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
            J("    istore %d\n", trail_local);

            int unify_ok_lbl = pj_fresh_label();
            char unify_ok[128], unify_fail[128];
            snprintf(unify_ok,   sizeof unify_ok,   "pj_dbu%d_ok",   unify_ok_lbl);
            snprintf(unify_fail, sizeof unify_fail, "pj_dbu%d_fail", unify_ok_lbl);

            for (int ai = 0; ai < arity; ai++) {
                J("    aload %d\n", ai);  /* incoming arg */
                J("    aload %d\n", db_term_local);
                J("    ldc %d\n", ai + 2);
                JI("aaload", ""); /* term->args[ai] */
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", unify_fail);
            }
            J("    goto %s\n", unify_ok);

            J("%s:\n", unify_fail);
            /* undo bindings, try next */
            J("    iload %d\n", trail_local);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            J("    iinc %d 1\n", db_idx_local);
            J("    goto %s\n", db_loop);

            J("%s:\n", unify_ok);
        }

        /* success — unification happened in-place via pj_unify above.
         * Return any non-null Object[] as success token. */
        J("    ldc \"true\"\n");
        J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
        JI("areturn", "");

        J("%s:\n", db_miss);
    }

    /* truly exhausted */
    JI("aconst_null", "");
    JI("areturn", "");

    J(".end method\n\n");
    /* Flush any NAF helper methods buffered during this predicate */
    if (pj_helper_buf) {
        fflush(pj_helper_buf);
        rewind(pj_helper_buf);
        int _hc;
        while ((_hc = fgetc(pj_helper_buf)) != EOF) fputc(_hc, pj_out);
        fclose(pj_helper_buf);
        pj_helper_buf = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Main method emitter
 * Finds the initialization(main) directive and emits a JVM main() that calls it.
 * ------------------------------------------------------------------------- */

static void pj_emit_main(Program *prog) {
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");

    /* Bug 2 fix: execute :- assertz/asserta directives before calling main/0.
     * Directives are STMT_t nodes whose subject is NOT E_CHOICE. */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE) continue;
        EXPR_t *g = s->subject;
        if (g->kind != E_FNC || !g->sval) continue;
        if ((strcmp(g->sval, "assertz") == 0 || strcmp(g->sval, "asserta") == 0)
             && g->nchildren == 1) {
            /* emit assertz/asserta inline — same logic as pj_emit_goal assertz case */
            int is_asserta = (strcmp(g->sval, "asserta") == 0);
            int *dummy_locals = NULL;
            pj_emit_term(g->children[0], dummy_locals, 0);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            pj_emit_term(g->children[0], dummy_locals, 0);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    %s\n", is_asserta ? "iconst_1" : "iconst_0");
            J("    invokestatic %s/pj_db_assert(Ljava/lang/String;Ljava/lang/Object;I)V\n", pj_classname);
        }
        /* ignore :- dynamic and other directives silently */
    }

    /* Call p_main_0(0) once. The internal fail-loop inside main/0 drives
     * all backtracking via call_sfail→call_α. No outer retry needed. */
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
    pj_prog = prog;  /* M-PJ-CUT-UCALL: store for callee base[nclauses] lookup */
    pj_natoms = 0;
    pj_label_counter = 0;
    pj_set_classname(filename);

    JC("Generated by snobol4x Prolog JVM emitter (prolog_emit_jvm.c)");
    J("; Source: %s\n\n", filename ? filename : "<stdin>");

    pj_emit_class_header();
    pj_emit_runtime_helpers();
    pj_emit_assertz_helpers();

    /* Check if program uses between/3 but doesn't define it — emit synthetic method */
    {
        int defines_between = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            if (s->subject->kind == E_CHOICE && s->subject->sval &&
                strcmp(s->subject->sval, "between") == 0) {
                defines_between = 1;
            }
        }
        if (!defines_between)
            pj_emit_between_builtin();
    }

    /* Check if program uses findall/3 but doesn't define it — emit synthetic method */
    {
        int defines_findall = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            if (s->subject->kind == E_CHOICE && s->subject->sval &&
                strcmp(s->subject->sval, "findall") == 0)
                defines_findall = 1;
        }
        if (!defines_findall)
            pj_emit_findall_builtin();
    }

    /* emit each predicate */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE)
            pj_emit_choice(s->subject);
    }

    /* Bug 1 fix: emit stub methods for pure-dynamic predicates.
     * A predicate is "pure-dynamic" if it appears as assertz/asserta arg
     * in a directive but has no E_CHOICE in the program. */
    {
        /* Collect all statically-defined functor/arity keys */
        /* Simple approach: for each directive assertz(foo(...)), check if
         * foo/arity has an E_CHOICE; if not, emit a stub. */
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject || s->subject->kind == E_CHOICE) continue;
            EXPR_t *g = s->subject;
            if (!g || g->kind != E_FNC || !g->sval) continue;
            if ((strcmp(g->sval, "assertz") != 0 && strcmp(g->sval, "asserta") != 0)
                || g->nchildren != 1) continue;
            EXPR_t *term = g->children[0];
            if (!term || term->kind != E_FNC || !term->sval) continue;
            /* get functor and arity */
            const char *raw = term->sval;
            int dyn_arity = term->nchildren;
            /* check if static choice exists */
            int has_static = 0;
            for (STMT_t *s2 = prog->head; s2; s2 = s2->next) {
                if (!s2->subject || s2->subject->kind != E_CHOICE) continue;
                EXPR_t *ch = s2->subject;
                if (!ch->sval) continue;
                /* ch->sval is "name/arity" */
                const char *sl2 = strrchr(ch->sval, '/');
                int ch_arity = sl2 ? atoi(sl2+1) : 0;
                int nlen = sl2 ? (int)(sl2 - ch->sval) : (int)strlen(ch->sval);
                if (ch_arity == dyn_arity && strncmp(ch->sval, raw, nlen) == 0
                    && (int)strlen(raw) == nlen) {
                    has_static = 1; break;
                }
            }
            if (has_static) continue;
            /* check we haven't already emitted a stub for this functor/arity
             * (multiple assertz calls for same predicate) — use a dedup scan
             * of earlier directives */
            int already = 0;
            for (STMT_t *s3 = prog->head; s3 != s; s3 = s3->next) {
                if (!s3->subject || s3->subject->kind == E_CHOICE) continue;
                EXPR_t *g3 = s3->subject;
                if (!g3 || g3->kind != E_FNC || !g3->sval) continue;
                if ((strcmp(g3->sval,"assertz")!=0 && strcmp(g3->sval,"asserta")!=0)
                    || g3->nchildren!=1) continue;
                EXPR_t *t3 = g3->children[0];
                if (t3 && t3->kind == E_FNC && t3->sval &&
                    strcmp(t3->sval, raw) == 0 && t3->nchildren == dyn_arity) {
                    already = 1; break;
                }
            }
            if (already) continue;

            /* emit stub: only the dynamic DB walker */
            char safe_stub[256];
            pj_safe_name(raw, safe_stub, sizeof safe_stub);
            JSep(raw);
            J("; stub for pure-dynamic predicate %s/%d\n", raw, dyn_arity);
            J(".method static p_%s_%d(", safe_stub, dyn_arity);
            for (int i = 0; i < dyn_arity; i++) J("[Ljava/lang/Object;");
            J("I)[Ljava/lang/Object;\n");
            int stub_locals = dyn_arity + 50;
            J("    .limit stack 16\n");
            J("    .limit locals %d\n", stub_locals);
            int cs_loc  = dyn_arity;
            int tr_loc  = dyn_arity + 1;
            int idx_loc = dyn_arity + 2;
            int trm_loc = dyn_arity + 3;
            int stub_lbl = pj_fresh_label();
            char sb_store[64], sb_loop[64], sb_hit[64], sb_miss[64], sb_ok[64], sb_fail[64];
            snprintf(sb_store, sizeof sb_store, "stub%d_store", stub_lbl);
            snprintf(sb_loop,  sizeof sb_loop,  "stub%d_loop",  stub_lbl);
            snprintf(sb_hit,   sizeof sb_hit,   "stub%d_hit",   stub_lbl);
            snprintf(sb_miss,  sizeof sb_miss,  "stub%d_miss",  stub_lbl);
            snprintf(sb_ok,    sizeof sb_ok,    "stub%d_ok",    stub_lbl);
            snprintf(sb_fail,  sizeof sb_fail,  "stub%d_fail",  stub_lbl);
            /* db_idx = max(0, cs) */
            J("    iload %d\n", cs_loc);
            JI("dup", "");
            J("    ifge %s\n", sb_store);
            JI("pop", "");
            JI("iconst_0", "");
            J("%s:\n", sb_store);
            J("    istore %d\n", idx_loc);
            J("%s:\n", sb_loop);
            J("    ldc \"%s/%d\"\n", raw, dyn_arity);
            J("    iload %d\n", idx_loc);
            J("    invokestatic %s/pj_db_query(Ljava/lang/String;I)Ljava/lang/Object;\n", pj_classname);
            JI("dup", "");
            J("    ifnonnull %s\n", sb_hit);
            JI("pop", "");
            J("    goto %s\n", sb_miss);
            J("%s:\n", sb_hit);
            if (dyn_arity == 0) {
                JI("pop", "");
            } else {
                JI("checkcast", "[Ljava/lang/Object;");
                J("    astore %d\n", trm_loc);
                J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
                J("    istore %d\n", tr_loc);
                for (int ai = 0; ai < dyn_arity; ai++) {
                    J("    aload %d\n", ai);
                    J("    aload %d\n", trm_loc);
                    J("    ldc %d\n", ai + 2);
                    JI("aaload", "");
                    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                    J("    ifeq %s\n", sb_fail);
                }
                J("    goto %s\n", sb_ok);
                J("%s:\n", sb_fail);
                J("    iload %d\n", tr_loc);
                J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
                J("    iinc %d 1\n", idx_loc);
                J("    goto %s\n", sb_loop);
                J("%s:\n", sb_ok);
            }
            J("    ldc \"true\"\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            JI("areturn", "");
            J("%s:\n", sb_miss);
            JI("aconst_null", "");
            JI("areturn", "");
            J(".end method\n\n");
        }
    }

    pj_emit_main(prog);
}
