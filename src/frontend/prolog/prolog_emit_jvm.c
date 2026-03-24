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
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("astore_2", "");   /* local 2 = sb */
    /* append functor */
    JI("aload_2", "");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
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

static void pj_emit_goal(EXPR_t *goal, const char *lbl_γ, const char *lbl_ω,
                         int trail_local, int *var_locals, int n_vars,
                         int cut_cs_seal, int cs_local_for_cut);
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
                         const char *lbl_pred_ω);

static void pj_emit_goal(EXPR_t *goal, const char *lbl_γ, const char *lbl_ω,
                         int trail_local, int *var_locals, int n_vars,
                         int cut_cs_seal, int cs_local_for_cut) {
    if (!goal) { JI("goto", lbl_γ); return; }

    if (goal->kind == E_CUT) {
        /* cut: seal β by storing base[nclauses] into cs_local,
         * so the next dispatch hits default:omega.
         * -1 means no enclosing choice (should not happen in valid Prolog). */
        if (cut_cs_seal >= 0 && cs_local_for_cut >= 0) {
            J("    ldc %d\n", cut_cs_seal);
            J("    istore %d\n", cs_local_for_cut);
        }
        JI("goto", lbl_γ);
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
                         ics, sco, cut_cs_seal, cs_local_for_cut, NULL);
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
                             cut_cs_seal, cs_local_for_cut);
                J("%s:\n", cond_ok);
                /* emit Then goals: children[1..nchildren-1] as flat sequence */
                int nthen = first->nchildren - 1;
                for (int ti = 0; ti < nthen; ti++) {
                    char tstep_γ[128], tstep_ω[128];
                    snprintf(tstep_γ, sizeof tstep_γ, "ite%d_t%d_gamma", uid, ti);
                    snprintf(tstep_ω, sizeof tstep_ω, "ite%d_t%d_omega", uid, ti);
                    const char *step_γ = (ti == nthen - 1) ? lbl_γ : tstep_γ;
                    pj_emit_goal(first->children[1 + ti], step_γ, lbl_ω,
                                 trail_local, var_locals, n_vars,
                                 cut_cs_seal, cs_local_for_cut);
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
                                 cut_cs_seal, cs_local_for_cut);
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
                                 ics_disj, sco_disj, cut_cs_seal, cs_local_for_cut, NULL);
                } else {
                    /* single goal branch */
                    pj_emit_body(&branch, 1,
                                 lbl_γ, fail_to, fail_to,
                                 trail_local, var_locals, n_vars, &next_local_disj,
                                 ics_disj, sco_disj, cut_cs_seal, cs_local_for_cut, NULL);
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
                         cut_cs_seal, cs_local_for_cut);
            J("%s:\n", cond_ok);
            int nthen = nargs - 1;
            for (int ti = 0; ti < nthen; ti++) {
                char tstep_γ[128];
                snprintf(tstep_γ, sizeof tstep_γ, "ifthen%d_t%d_gamma", uid, ti);
                const char *step_γ = (ti == nthen - 1) ? lbl_γ : tstep_γ;
                pj_emit_goal(goal->children[1 + ti], step_γ, lbl_ω,
                             trail_local, var_locals, n_vars,
                             cut_cs_seal, cs_local_for_cut);
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
        /* \+/1, not/1 — negation as failure */
        if ((strcmp(fn, "\\+") == 0 || strcmp(fn, "not") == 0) && nargs == 1) {
            int uid = pj_fresh_label();
            char inner_ok[128], inner_fail[128];
            snprintf(inner_ok,   sizeof inner_ok,   "naf%d_ok",   uid);
            snprintf(inner_fail, sizeof inner_fail, "naf%d_fail", uid);
            pj_emit_goal(goal->children[0], inner_ok, inner_fail,
                         trail_local, var_locals, n_vars, cut_cs_seal, cs_local_for_cut);
            J("%s:\n", inner_ok);   JI("goto", lbl_ω);
            J("%s:\n", inner_fail); JI("goto", lbl_γ);
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
                         const char *lbl_pred_ω) {
    if (ngoals == 0) { JI("goto", lbl_γ); return; }

    EXPR_t *g = goals[0];

    if (!g || g->kind == E_TRAIL_MARK || g->kind == E_TRAIL_UNWIND) {
        pj_emit_body(goals + 1, ngoals - 1, lbl_γ, lbl_ω, lbl_outer_ω,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local,
                     cut_cs_seal, cs_local_for_cut, lbl_pred_ω);
        return;
    }

    if (g->kind == E_CUT) {
        /* seal β: store base[nclauses] into cs_local so next dispatch → ω */
        if (cut_cs_seal >= 0 && cs_local_for_cut >= 0) {
            J("    ldc %d\n", cut_cs_seal);
            J("    istore %d\n", cs_local_for_cut);
        }
        /* Remaining goals after cut: failure goes to predicate omega (skip all clauses). */
        const char *cut_fail = lbl_pred_ω ? lbl_pred_ω : lbl_outer_ω;
        pj_emit_body(goals + 1, ngoals - 1, lbl_γ, cut_fail, cut_fail,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local,
                     cut_cs_seal, cs_local_for_cut, lbl_pred_ω);
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
                         cut_cs_seal, cs_local_for_cut, lbl_pred_ω);
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
        /* ucall exhausted → retry enclosing call (lbl_ω = enclosing beta) */
        J("%s:\n", call_ω);
        JI("goto", lbl_ω);
        return;
    }

    /* Deterministic goal */
    {
        int uid = pj_fresh_label();
        char g_γ[128], g_ω[128];
        snprintf(g_γ, sizeof g_γ, "dg%d_gamma", uid);
        snprintf(g_ω, sizeof g_ω, "dg%d_omega", uid);
        pj_emit_goal(g, g_γ, g_ω, trail_local, var_locals, n_vars,
                     cut_cs_seal, cs_local_for_cut);
        J("%s:\n", g_γ);
        pj_emit_body(goals + 1, ngoals - 1, lbl_γ, lbl_ω, lbl_outer_ω,
                     trail_local, var_locals, n_vars, next_local,
                     init_cs_local, sub_cs_out_local,
                     cut_cs_seal, cs_local_for_cut, lbl_pred_ω);
        J("%s:\n", g_ω);
        JI("goto", lbl_ω);
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
    /* base[nclauses]: one-past-end sentinel for the dispatch omega guard */
    base[nclauses] = base[nclauses - 1] + 1;
    /* last_has_ucall: does the last clause have any body user-calls?
     * If yes, the cs range for that clause is open-ended (sub_cs from inner
     * calls can push cs beyond base[nclauses]), so we CANNOT add an omega guard.
     * If no (pure fact/builtin), cs >= base[nclauses] means exhausted → ω safe. */
    int last_has_ucall = 0;
    {
        EXPR_t *last = choice->children[nclauses - 1];
        int nv_args_last = last ? (int)last->dval : 0;
        int nb_last = last ? (int)last->nchildren - nv_args_last : 0;
        if (nb_last < 0) nb_last = 0;
        for (int bi = 0; bi < nb_last && !last_has_ucall; bi++) {
            EXPR_t *g = last->children[nv_args_last + bi];
            if (g && pj_is_user_call(g)) last_has_ucall = 1;
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
    for (int ci2 = 0; ci2 < nclauses; ci2++) {
        EXPR_t *cl2 = choice->children[ci2];
        if (!cl2 || cl2->kind != E_CLAUSE) continue;
        int n_args_ci2 = (int)cl2->dval;
        int nb2 = cl2->nchildren - n_args_ci2;
        if (nb2 < 0) nb2 = 0;
        int uc = pj_count_ucalls(cl2->children + n_args_ci2, nb2);
        if (uc > max_ucalls) max_ucalls = uc;
    }
    int locals_needed = vars_base + max_vars + 5 * max_ucalls + 16;

    J("    .limit stack 16\n");
    J("    .limit locals %d\n", locals_needed);

    /* dispatch: linear scan from last clause down.
     * cs >= base[ci] → enter clause ci.
     * Mirrors ASM: cmp eax, base[ci]; jge pl_name_cN_α
     *
     * GUARD: Only emit omega guard when last clause is a pure fact (no ucalls).
     * If last clause has body user-calls, cs range is open-ended because sub_cs
     * from recursive calls can push cs beyond base[nclauses]; no guard needed
     * as the sub_cs path correctly re-enters clause N-1 for continuation. */
    if (!last_has_ucall) {
        J("    iload %d ; cs >= %d (all clauses exhausted)? → omega\n    ldc %d\n    if_icmpge p_%s_%d_omega\n",
          cs_local, base[nclauses], base[nclauses], safe_fn, arity);
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
            int next_local = vars_base + n_vars;
            pj_emit_body(body_goals, nbody, γ_lbl, α_retry_lbl, ω_lbl,
                         trail_local, var_locals, n_vars, &next_local,
                         init_cs_local, sub_cs_out_local,
                         base[nclauses], cs_local, pred_ω_lbl);
        }

        /* γ port: return base[ci] + init_cs + 1
         * init_cs is the cs we entered this clause with (minus base[ci]).
         * On next entry caller provides cs = base[ci] + init_cs + 1, so
         * clause computes init_cs_new = init_cs + 1 — incrementing the inner
         * cs by exactly 1 each retry, matching the ASM conjunction behaviour.
         * Using sub_cs_out here was wrong for recursive predicates: it could
         * skip inner cs values (e.g. member/2 skipping 'c'). */
        J("p_%s_%d_gamma_%d:\n", safe_fn, arity, ci);
        JI("iconst_1", "");
        JI("anewarray", "java/lang/Object");
        JI("dup", "");
        JI("iconst_0", "");
        /* compute base[ci] + init_cs_local + 1 */
        J("    ldc %d\n", base[ci]);
        J("    iload %d\n", init_cs_local);   /* was sub_cs_out_local — fixed PJ-7 */
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
