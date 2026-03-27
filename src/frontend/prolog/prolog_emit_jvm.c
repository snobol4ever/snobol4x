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
#include "prolog_parse.h"
#include "prolog_lower.h"
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

/* Emit:  ldc "escaped_string"
 * Escapes backslash and double-quote so Jasmin accepts any atom name. */
static void pj_ldc_str(const char *s) {
    J("    ldc \"");
    for (; s && *s; s++) {
        if      (*s == '\\') J("\\\\");
        else if (*s == '"')  J("\\\"");
        else                 fputc(*s, pj_out);
    }
    J("\"\n");
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
    /* ABI §5: lowercase classnames — do NOT capitalize */
    strncpy(pj_classname, buf, sizeof pj_classname - 1);
}

/* pj_safe_name — sanitize a Prolog functor/atom for use as a Jasmin label */
static void pj_safe_name(const char *src, char *dst, int dstlen) {
    int di = 0;
    for (int si = 0; src[si] && di < dstlen - 1; si++) {
        char c = src[si];
        if (isalnum((unsigned char)c) || c == '_')
            dst[di++] = c;
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

    /* term_float(D) — allocate float term: Object[]{"float", Double.toString(d)} */
    J(".method static pj_term_float(D)[Ljava/lang/Object;\n");
    J("    .limit stack 5\n");
    J("    .limit locals 2\n");
    JI("iconst_2", "");
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"float\"");
    JI("aastore", "");
    JI("dup", "");
    JI("iconst_1", "");
    JI("dload_0", "");
    JI("invokestatic", "java/lang/Double/toString(D)Ljava/lang/String;");
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
    JI("ifeq", "pj_unify_check_float");
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
    /* check float==float: both tagged "float", compare string values */
    J("pj_unify_check_float:\n");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"float\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_check_compound");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"float\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pj_unify_false");
    /* compare float values numerically (handles -0.0 == 0.0) */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("invokestatic", "java/lang/Double/parseDouble(Ljava/lang/String;)D");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("invokestatic", "java/lang/Double/parseDouble(Ljava/lang/String;)D");
    JI("dcmpl", "");
    JI("ifne", "pj_unify_false");
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
    /* Check for '$VAR'(N) — numbervars marker: print as A,B,...Z,A1,B1,... */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", "");
    JI("iconst_3", "");  /* arity 1 → length 3 */
    JI("if_icmpne", "pts_not_var_term");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("ldc", "\"$VAR\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pts_not_var_term");
    /* it's '$VAR'(N): compute name = chr('A'+N%26) + (N/26==0 ? "" : str(N/26)) */
    /* get N as int */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("l2i", "");       /* int n */
    JI("istore_1", ""); /* local 1 = n */
    /* letter = (char)('A' + n%26) */
    JI("iload_1", "");
    JI("bipush", "26"); JI("irem", "");
    JI("bipush", "65"); JI("iadd", "");  /* 'A'=65 */
    JI("i2c", "");
    JI("invokestatic", "java/lang/Character/toString(C)Ljava/lang/String;");
    JI("astore_2", "");  /* local 2 = letter string */
    /* suffix = n/26 */
    JI("iload_1", "");
    JI("bipush", "26"); JI("idiv", "");
    JI("istore_3", "");  /* local 3 = suffix int */
    JI("iload_3", "");
    JI("ifne", "pts_var_suffix");
    /* no suffix: return letter */
    JI("aload_2", "");
    JI("areturn", "");
    J("pts_var_suffix:\n");
    /* return letter + Integer.toString(suffix) */
    JI("new", "java/lang/StringBuilder");
    JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("aload_2", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("iload_3", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(I)Ljava/lang/StringBuilder;");
    JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    J("pts_not_var_term:\n");
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
     * pj_needs_quote(String name) → Z
     * True if atom name requires single-quote wrapping in writeq/write_canonical.
     * Rules: empty string, starts with uppercase or '_', contains non-alnum/underscore.
     * locals: 0=name 1=len 2=i 3=ch
     * ------------------------------------------------------------------ */
    J(".method static pj_needs_quote(Ljava/lang/String;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 4\n");
    JI("aload_0", "");
    JI("invokevirtual", "java/lang/String/length()I");
    JI("istore_1", "");
    /* empty string → needs quote */
    JI("iload_1", ""); JI("ifne", "pjnq_check_first");
    JI("iconst_1", ""); JI("ireturn", "");
    J("pjnq_check_first:\n");
    /* Special atoms that never need quotes: [] {} , | ! ; */
    JI("aload_0", ""); JI("ldc", "\"[]\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "pjnq_no");
    JI("aload_0", ""); JI("ldc", "\"{}\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "pjnq_no");
    JI("aload_0", ""); JI("ldc", "\",\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "pjnq_no");
    JI("aload_0", ""); JI("ldc", "\"|\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "pjnq_no");
    JI("aload_0", ""); JI("ldc", "\"!\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "pjnq_no");
    JI("aload_0", ""); JI("ldc", "\";\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "pjnq_no");
    /* Scan all chars: classify as alpha-id (starts lower, rest alnum/_)
       or symbolic (all in #&*+-./:<=>?@\\^~!)  or neither (needs quote) */
    /* Check first char */
    JI("aload_0", ""); JI("iconst_0", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C"); JI("istore_2", "");
    /* starts lower → alpha-id path */
    JI("iload_2", "");
    JI("invokestatic", "java/lang/Character/isLowerCase(C)Z"); JI("ifne", "pjnq_alphaid");
    /* starts symbolic char → symbolic path */
    JI("iload_2", "");
    J("    invokestatic %s/pj_is_sym_char(I)Z\n", pj_classname);
    JI("ifne", "pjnq_symbolic");
    /* anything else (upper, digit, _, space, etc.) → quote */
    JI("iconst_1", ""); JI("ireturn", "");

    J("pjnq_alphaid:\n");
    /* all chars must be alnum or _ */
    JI("iconst_1", ""); JI("istore_2", "");  /* i=1 */
    J("pjnq_ai_loop:\n");
    JI("iload_2", ""); JI("iload_1", ""); JI("if_icmpge", "pjnq_no");
    JI("aload_0", ""); JI("iload_2", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C"); JI("istore_3", "");
    JI("iload_3", ""); JI("invokestatic", "java/lang/Character/isLetterOrDigit(C)Z"); JI("ifne", "pjnq_ai_next");
    JI("iload_3", ""); JI("bipush", "95"); JI("if_icmpeq", "pjnq_ai_next");
    JI("goto", "pjnq_yes");
    J("pjnq_ai_next:\n"); JI("iinc", "2 1"); JI("goto", "pjnq_ai_loop");

    J("pjnq_symbolic:\n");
    /* all chars must be symbolic */
    JI("iconst_1", ""); JI("istore_2", "");
    J("pjnq_sym_loop:\n");
    JI("iload_2", ""); JI("iload_1", ""); JI("if_icmpge", "pjnq_no");
    JI("aload_0", ""); JI("iload_2", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C"); JI("istore_3", "");
    JI("iload_3", "");
    J("    invokestatic %s/pj_is_sym_char(I)Z\n", pj_classname);
    JI("ifeq", "pjnq_yes");
    JI("iinc", "2 1"); JI("goto", "pjnq_sym_loop");

    J("pjnq_yes:\n"); JI("iconst_1", ""); JI("ireturn", "");
    J("pjnq_no:\n");  JI("iconst_0", ""); JI("ireturn", "");
    J(".end method\n\n");

    /* pj_is_sym_char(int ch) → Z: true if ch is a Prolog symbolic char */
    J(".method static pj_is_sym_char(I)Z\n");
    J("    .limit stack 2\n");
    J("    .limit locals 1\n");
    /* symbolic chars: # & * + - . / : < = > ? @ \\ ^ ~ */
    JI("iload_0", ""); JI("bipush", "35"); JI("if_icmpeq", "pjsc_yes"); /* # */
    JI("iload_0", ""); JI("bipush", "38"); JI("if_icmpeq", "pjsc_yes"); /* & */
    JI("iload_0", ""); JI("bipush", "42"); JI("if_icmpeq", "pjsc_yes"); /* * */
    JI("iload_0", ""); JI("bipush", "43"); JI("if_icmpeq", "pjsc_yes"); /* + */
    JI("iload_0", ""); JI("bipush", "45"); JI("if_icmpeq", "pjsc_yes"); /* - */
    JI("iload_0", ""); JI("bipush", "46"); JI("if_icmpeq", "pjsc_yes"); /* . */
    JI("iload_0", ""); JI("bipush", "47"); JI("if_icmpeq", "pjsc_yes"); /* / */
    JI("iload_0", ""); JI("bipush", "58"); JI("if_icmpeq", "pjsc_yes"); /* : */
    JI("iload_0", ""); JI("bipush", "60"); JI("if_icmpeq", "pjsc_yes"); /* < */
    JI("iload_0", ""); JI("bipush", "61"); JI("if_icmpeq", "pjsc_yes"); /* = */
    JI("iload_0", ""); JI("bipush", "62"); JI("if_icmpeq", "pjsc_yes"); /* > */
    JI("iload_0", ""); JI("bipush", "63"); JI("if_icmpeq", "pjsc_yes"); /* ? */
    JI("iload_0", ""); JI("bipush", "64"); JI("if_icmpeq", "pjsc_yes"); /* @ */
    JI("iload_0", ""); JI("bipush", "92"); JI("if_icmpeq", "pjsc_yes"); /* \ */
    JI("iload_0", ""); JI("bipush", "94"); JI("if_icmpeq", "pjsc_yes"); /* ^ */
    JI("iload_0", ""); JI("bipush", "126"); JI("if_icmpeq", "pjsc_yes"); /* ~ */
    JI("iconst_0", ""); JI("ireturn", "");
    J("pjsc_yes:\n"); JI("iconst_1", ""); JI("ireturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_quoted_atom_str(String name) → String
     * Wraps name in single quotes, escaping any embedded single quotes.
     * locals: 0=name 1=sb 2=len 3=i 4=ch
     * ------------------------------------------------------------------ */
    J(".method static pj_quoted_atom_str(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 4\n");
    J("    .limit locals 5\n");
    JI("new", "java/lang/StringBuilder");
    JI("dup", ""); JI("invokespecial", "java/lang/StringBuilder/<init>()V");
    JI("astore_1", "");
    JI("aload_1", ""); JI("ldc", "\"'\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("aload_0", ""); JI("invokevirtual", "java/lang/String/length()I"); JI("istore_2", "");
    JI("iconst_0", ""); JI("istore_3", "");
    J("pjqa_loop:\n");
    JI("iload_3", ""); JI("iload_2", ""); JI("if_icmpge", "pjqa_close");
    JI("aload_0", ""); JI("iload_3", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C"); JI("istore", "4");
    JI("iload", "4"); JI("bipush", "39"); /* '\'' */
    JI("if_icmpne", "pjqa_append");
    /* escape: append '' */
    JI("aload_1", ""); JI("ldc", "\"''\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", ""); JI("iinc", "3 1"); JI("goto", "pjqa_loop");
    J("pjqa_append:\n");
    JI("aload_1", ""); JI("iload", "4"); JI("i2c", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;");
    JI("pop", ""); JI("iinc", "3 1"); JI("goto", "pjqa_loop");
    J("pjqa_close:\n");
    JI("aload_1", ""); JI("ldc", "\"'\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
    JI("pop", "");
    JI("aload_1", ""); JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
    JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_term_str_q(Object) → String   (writeq: quoted atoms, operator notation)
     * locals: 0=term 1=tag 2=sb 3=cur 4=scratch
     * ------------------------------------------------------------------ */
    J(".method static pj_term_str_q(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_0", ""); JI("ifnonnull", "ptsq_notnull");
    JI("ldc", "\"_\""); JI("areturn", "");
    J("ptsq_notnull:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", ""); JI("astore_1", "");
    /* atom/int/float: return quoted name if atom */
    JI("aload_1", ""); JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "ptsq_scalar");
    JI("aload_1", ""); JI("ldc", "\"float\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "ptsq_scalar");
    JI("aload_1", ""); JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifeq", "ptsq_compound_or_var");
    /* atom: check if needs quoting */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String");
    JI("astore_2", "");
    JI("aload_2", "");
    J("    invokestatic %s/pj_needs_quote(Ljava/lang/String;)Z\n", pj_classname);
    JI("ifeq", "ptsq_atom_plain");
    JI("aload_2", "");
    J("    invokestatic %s/pj_quoted_atom_str(Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
    JI("areturn", "");
    J("ptsq_atom_plain:\n"); JI("aload_2", ""); JI("areturn", "");
    J("ptsq_scalar:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String"); JI("areturn", "");
    J("ptsq_compound_or_var:\n");
    JI("aload_1", ""); JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "ptsq_var");
    /* compound: build functor(args) with quoted functors and recursive _q args */
    /* arity = arraylength - 2 */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", ""); JI("iconst_2", ""); JI("isub", ""); JI("istore_3", "");
    JI("new", "java/lang/StringBuilder"); JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V"); JI("astore_2", "");
    /* functor name */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String"); JI("astore", "4");
    /* for list functor '.' use plain pj_term_str for the whole thing */
    JI("aload", "4"); JI("ldc", "\".\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "ptsq_not_list");
    JI("iload_3", ""); JI("bipush", "2"); JI("if_icmpne", "ptsq_not_list");
    /* it's a list: delegate to pj_term_str */
    JI("aload_0", "");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("areturn", "");
    J("ptsq_not_list:\n");
    /* quote functor if needed */
    JI("aload", "4");
    J("    invokestatic %s/pj_needs_quote(Ljava/lang/String;)Z\n", pj_classname);
    JI("ifeq", "ptsq_fn_plain");
    JI("aload", "4");
    J("    invokestatic %s/pj_quoted_atom_str(Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
    JI("goto", "ptsq_fn_app");
    J("ptsq_fn_plain:\n"); JI("aload", "4");
    J("ptsq_fn_app:\n");
    JI("aload_2", ""); JI("swap", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("iload_3", ""); JI("ifne", "ptsq_open_p");
    JI("aload_2", ""); JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;"); JI("areturn", "");
    J("ptsq_open_p:\n");
    JI("aload_2", ""); JI("ldc", "\"(\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("iconst_0", ""); JI("istore", "4");
    J("ptsq_arg_loop:\n");
    JI("iload", "4"); JI("iload_3", ""); JI("if_icmpge", "ptsq_close_p");
    JI("iload", "4"); JI("ifle", "ptsq_no_c");
    JI("aload_2", ""); JI("ldc", "\",\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    J("ptsq_no_c:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iload", "4"); JI("iconst_2", ""); JI("iadd", ""); JI("aaload", "");
    J("    invokestatic %s/pj_term_str_q(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("aload_2", ""); JI("swap", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("iinc", "4 1"); JI("goto", "ptsq_arg_loop");
    J("ptsq_close_p:\n");
    JI("aload_2", ""); JI("ldc", "\")\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("aload_2", ""); JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;"); JI("areturn", "");
    J("ptsq_var:\n"); JI("ldc", "\"_\""); JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_term_str_canonical(Object) → String
     * write_canonical: no operator notation, always functor(args), quoted atoms.
     * locals: 0=term 1=tag 2=sb 3=arity 4=i
     * ------------------------------------------------------------------ */
    J(".method static pj_term_str_canonical(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_0", ""); JI("ifnonnull", "ptsc_notnull");
    JI("ldc", "\"_\""); JI("areturn", "");
    J("ptsc_notnull:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", ""); JI("astore_1", "");
    /* int/float → scalar */
    JI("aload_1", ""); JI("ldc", "\"int\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "ptsc_scalar");
    JI("aload_1", ""); JI("ldc", "\"float\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "ptsc_scalar");
    JI("aload_1", ""); JI("ldc", "\"atom\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifeq", "ptsc_compound_or_var");
    /* atom: quote if needed */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String");
    JI("astore_2", "");
    JI("aload_2", "");
    J("    invokestatic %s/pj_needs_quote(Ljava/lang/String;)Z\n", pj_classname);
    JI("ifeq", "ptsc_atom_plain");
    JI("aload_2", "");
    J("    invokestatic %s/pj_quoted_atom_str(Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
    JI("areturn", "");
    J("ptsc_atom_plain:\n"); JI("aload_2", ""); JI("areturn", "");
    J("ptsc_scalar:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String"); JI("areturn", "");
    J("ptsc_compound_or_var:\n");
    JI("aload_1", ""); JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z"); JI("ifne", "ptsc_var");
    /* compound: check for list ('.'/2) → print as [a,b,...] */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("ldc", "\".\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "ptsc_plain_compound");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", ""); JI("iconst_4", ""); JI("if_icmpne", "ptsc_plain_compound");
    /* list: delegate to pj_term_str for the list notation */
    JI("aload_0", "");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("areturn", "");
    J("ptsc_plain_compound:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", ""); JI("iconst_2", ""); JI("isub", ""); JI("istore_3", "");
    /* sb = new StringBuilder */
    JI("new", "java/lang/StringBuilder"); JI("dup", "");
    JI("invokespecial", "java/lang/StringBuilder/<init>()V"); JI("astore_2", "");
    /* append functor (quoted if needed) */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", ""); JI("checkcast", "java/lang/String"); JI("astore", "4");
    JI("aload", "4");
    J("    invokestatic %s/pj_needs_quote(Ljava/lang/String;)Z\n", pj_classname);
    JI("ifeq", "ptsc_fn_plain");
    JI("aload", "4");
    J("    invokestatic %s/pj_quoted_atom_str(Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
    JI("goto", "ptsc_fn_append");
    J("ptsc_fn_plain:\n"); JI("aload", "4");
    J("ptsc_fn_append:\n");
    JI("aload_2", ""); JI("swap", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    /* if arity == 0 → just functor */
    JI("iload_3", ""); JI("ifne", "ptsc_open_paren");
    JI("aload_2", ""); JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;"); JI("areturn", "");
    J("ptsc_open_paren:\n");
    JI("aload_2", ""); JI("ldc", "\"(\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("iconst_0", ""); JI("istore", "4");  /* i = 0 */
    J("ptsc_arg_loop:\n");
    JI("iload", "4"); JI("iload_3", ""); JI("if_icmpge", "ptsc_close_paren");
    JI("iload", "4"); JI("ifle", "ptsc_no_comma");
    JI("aload_2", ""); JI("ldc", "\",\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    J("ptsc_no_comma:\n");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iload", "4"); JI("iconst_2", ""); JI("iadd", ""); JI("aaload", "");
    J("    invokestatic %s/pj_term_str_canonical(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("aload_2", ""); JI("swap", "");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("iinc", "4 1"); JI("goto", "ptsc_arg_loop");
    J("ptsc_close_paren:\n");
    JI("aload_2", ""); JI("ldc", "\")\"");
    JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;"); JI("pop", "");
    JI("aload_2", ""); JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;"); JI("areturn", "");
    J("ptsc_var:\n"); JI("ldc", "\"_\""); JI("areturn", "");
    J(".end method\n\n");

    /* pj_writeq(Object) → V */
    J(".method static pj_writeq(Ljava/lang/Object;)V\n");
    J("    .limit stack 3\n");
    J("    .limit locals 1\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_term_str_q(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("swap", "");
    JI("invokevirtual", "java/io/PrintStream/print(Ljava/lang/String;)V");
    JI("return", "");
    J(".end method\n\n");

    /* pj_write_canonical(Object) → V */
    J(".method static pj_write_canonical(Ljava/lang/Object;)V\n");
    J("    .limit stack 3\n");
    J("    .limit locals 1\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_term_str_canonical(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
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
     * pj_list_to_term(Object list) -> Object[]
     * Compose direction of =../2: [F|Args] -> F(Args...)
     * list is a proper Prolog list: cons cells or nil.
     * Returns null on bad input.
     * local 0 = list (current cell), local 1 = arity, local 2 = functor string
     * local 3 = result term array, local 4 = tmp, local 5 = i
     * ------------------------------------------------------------------ */
    /* ------------------------------------------------------------------
     * pj_list_to_term(Object list) -> Object[]
     * Compose direction of =../2: [F|Args] -> F(Args...)
     * Uses a 32-slot scratch array; returns null on bad input.
     * local 0 = list cursor, local 1 = scratch Object[34], local 2 = count,
     * local 3 = tmp cell, local 4 = head
     * ------------------------------------------------------------------ */
    J(".method static pj_list_to_term(Ljava/lang/Object;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 6\n");
    /* deref first cell — must be cons */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_0", ""); JI("ifnull", "ltl_fail");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"compound\""); JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "ltl_fail");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("ldc", "\".\""); JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "ltl_fail");
    /* extract functor term (head of first cons) */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore", "4");  /* local 4 = functor term */
    /* advance to args list */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("bipush", "3"); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    /* allocate scratch array of 34 elements */
    JI("bipush", "34"); JI("anewarray", "java/lang/Object"); JI("astore_1", "");
    JI("iconst_0", ""); JI("istore_2", "");  /* count = 0 */
    J("ltl_loop:\n");
    JI("aload_0", ""); JI("ifnull", "ltl_build");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"atom\""); JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "ltl_build");  /* nil atom -> done */
    /* check not overflowing scratch */
    JI("iload_2", ""); JI("bipush", "32"); JI("if_icmpge", "ltl_fail");
    /* store head into scratch[count] */
    JI("aload_1", ""); JI("iload_2", "");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    JI("aastore", "");
    JI("iinc", "2 1");
    /* advance */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("bipush", "3"); JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("goto", "ltl_loop");
    J("ltl_build:\n");
    /* arity 0: return functor term as-is (it's already an atom cell) */
    JI("iload_2", ""); JI("ifne", "ltl_compound");
    JI("aload", "4"); JI("checkcast", "[Ljava/lang/Object;"); JI("areturn", "");
    J("ltl_compound:\n");
    /* allocate result: Object[2 + count] */
    JI("iload_2", ""); JI("iconst_2", ""); JI("iadd", "");
    JI("anewarray", "java/lang/Object");
    JI("astore_3", "");
    JI("aload_3", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("aload_3", ""); JI("iconst_1", "");
    JI("aload", "4");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("aastore", "");
    /* copy args */
    JI("iconst_0", ""); JI("istore", "5");
    J("ltl_copy:\n");
    JI("iload", "5"); JI("iload_2", ""); JI("if_icmpge", "ltl_ret");
    JI("aload_3", ""); JI("iload", "5"); JI("iconst_2", ""); JI("iadd", "");
    JI("aload_1", ""); JI("iload", "5"); JI("aaload", "");
    JI("aastore", "");
    JI("iinc", "5 1"); JI("goto", "ltl_copy");
    J("ltl_ret:\n");
    JI("aload_3", ""); JI("areturn", "");
    J("ltl_fail:\n");
    JI("aconst_null", ""); JI("areturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_univ(Object term, Object list) -> boolean (Z)
     * Bidirectional =../2 at runtime.
     * If term is bound: decompose to list via pj_term_to_list, unify.
     * If term is unbound: compose from list via pj_list_to_term, unify.
     * local 0 = term, local 1 = list, local 2 = deref'd term, local 3 = result
     * ------------------------------------------------------------------ */
    J(".method static pj_univ(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 4\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");  /* local 2 = deref term */
    /* check if term is var (unbound): tag == null or tag == "var" */
    JI("aload_2", ""); JI("ifnull", "univ_compose");
    JI("aload_2", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", "");
    JI("ldc", "\"var\""); JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "univ_compose");
    /* term is bound: decompose */
    JI("aload_2", "");
    J("    invokestatic %s/pj_term_to_list(Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("univ_compose:\n");
    /* term is unbound: build term from list */
    JI("aload_1", "");
    J("    invokestatic %s/pj_list_to_term(Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
    JI("astore_3", "");
    JI("aload_3", ""); JI("ifnull", "univ_fail");
    JI("aload_0", ""); JI("aload_3", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("univ_fail:\n");
    JI("iconst_0", ""); JI("ireturn", "");
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

    /* pj_db_retract(String key, int idx) -> Object | null
     * Removes and returns the entry at index idx from the DB list for key.
     * Returns null if key not in DB or idx out of bounds. */
    J("; pj_db_retract(String key, int idx) -> Object | null\n");
    J(".method static pj_db_retract(Ljava/lang/String;I)Ljava/lang/Object;\n");
    J("    .limit stack 4\n");
    J("    .limit locals 3\n");
    J("    getstatic %s/pj_db Ljava/util/HashMap;\n", pj_classname);
    JI("aload_0", "");
    JI("invokevirtual", "java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;");
    JI("dup", "");
    J("    ifnonnull pj_db_retract_have\n");
    JI("areturn", "");  /* null -> no entries */
    J("pj_db_retract_have:\n");
    JI("checkcast", "java/util/ArrayList");
    JI("astore_2", "");
    /* bounds check: idx < list.size() */
    JI("aload_2", "");
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("iload_1", "");
    J("    if_icmpgt pj_db_retract_ok\n");
    JI("aconst_null", "");
    JI("areturn", "");
    J("pj_db_retract_ok:\n");
    JI("aload_2", "");
    JI("iload_1", "");
    JI("invokevirtual", "java/util/ArrayList/remove(I)Ljava/lang/Object;");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_db_abolish(String key) -> void
     * Removes the entire ArrayList for key from pj_db (no-op if absent). */
    J("; pj_db_abolish(String key) -> void\n");
    J(".method static pj_db_abolish(Ljava/lang/String;)V\n");
    J("    .limit stack 2\n");
    J("    .limit locals 1\n");
    J("    getstatic %s/pj_db Ljava/util/HashMap;\n", pj_classname);
    JI("aload_0", "");
    JI("invokevirtual", "java/util/HashMap/remove(Ljava/lang/Object;)Ljava/lang/Object;");
    JI("pop", "");
    JI("return", "");
    J(".end method\n\n");

    /* pj_db_abolish_key(Object slashTerm) -> String
     * slashTerm is a compound /(Name, Arity) — arr[0]="compound", arr[1]="/",
     * arr[2]=Name (pj atom term), arr[3]=Arity (pj int term).
     * Returns "Name/Arity" string matching pj_db_assert_key format. */
    J("; pj_db_abolish_key(Object) -> String\n");
    J(".method static pj_db_abolish_key(Ljava/lang/Object;)Ljava/lang/String;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 4\n");
    /* arr[2] = Name pj-term → extract via pj_atom_name */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", "");
    JI("aaload", "");   /* pj atom term for Name */
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("astore_1", "");   /* local1 = Name string */
    /* arr[3] = Arity pj-term → extract via pj_int_val -> long -> int */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_3", "");
    JI("aaload", "");   /* pj int term for Arity */
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("l2i", "");
    JI("istore_2", "");   /* local2 = arity int */
    /* build "Name/Arity" */
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

    /* pj_list_to_arraylist(Object list) -> ArrayList<Object>
     * Walks a pj cons list ([H|T] = compound(".",H,T), [] = atom("[]"))
     * and collects elements into a new ArrayList. */
    J("; pj_list_to_arraylist(Object) -> ArrayList\n");
    J(".method static pj_list_to_arraylist(Ljava/lang/Object;)Ljava/util/ArrayList;\n");
    J("    .limit stack 4\n");
    J("    .limit locals 3\n");
    JI("new", "java/util/ArrayList");
    JI("dup", "");
    JI("invokespecial", "java/util/ArrayList/<init>()V");
    JI("astore_1", "");   /* local1 = result ArrayList */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");   /* local2 = current cell */
    J("pj_lta_loop:\n");
    /* check if [] atom */
    JI("aload_2", "");
    J("    instanceof [Ljava/lang/Object;\n");
    J("    ifeq pj_lta_done\n");
    JI("aload_2", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");   /* arr[1] = functor */
    JI("checkcast", "java/lang/String");
    JI("astore_0", "");
    JI("aload_0", "");
    JI("ldc", "\"[]\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    J("    ifne pj_lta_done\n");
    /* add head (arr[2]) to list */
    JI("aload_1", "");
    JI("aload_2", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", "");
    JI("aaload", "");
    JI("invokevirtual", "java/util/ArrayList/add(Ljava/lang/Object;)Z");
    JI("pop", "");
    /* advance to tail (arr[3]) */
    JI("aload_2", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_3", "");
    JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    J("    goto pj_lta_loop\n");
    J("pj_lta_done:\n");
    JI("aload_1", "");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_length_2(Object list, Object n) -> Z
     * length(+List, ?N): count cons cells, unify N with result.
     * locals: 0=list, 1=n, 2=cur(deref'd cell), 3=count(int) */
    J("; pj_length_2(Object list, Object n) -> Z\n");
    J(".method static pj_length_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 6\n");
    J("    .limit locals 4\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");   /* cur = deref(list) */
    JI("iconst_0", "");
    JI("istore_3", "");   /* count = 0 */
    J("pj_len_loop:\n");
    /* if cur is not Object[] → it's [] atom, done */
    JI("aload_2", "");
    J("    instanceof [Ljava/lang/Object;\n");
    J("    ifeq pj_len_done\n");
    /* check functor == "[]" → done */
    JI("aload_2", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");   /* arr[1] = functor */
    JI("ldc", "\"[]\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    J("    ifne pj_len_done\n");
    /* count++ */
    JI("iinc", "3 1");
    /* advance to tail arr[3] */
    JI("aload_2", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_3", "");
    JI("aaload", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    J("    goto pj_len_loop\n");
    J("pj_len_done:\n");
    /* build int term from count and unify with n */
    JI("iload_3", "");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* pj_arraylist_to_list(ArrayList) -> Object
     * Rebuilds a pj cons list from an ArrayList (last element first). */
    J("; pj_arraylist_to_list(ArrayList) -> Object\n");
    J(".method static pj_arraylist_to_list(Ljava/util/ArrayList;)Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");
    /* start with [] */
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("astore_1", "");   /* local1 = accumulated list (tail-first) */
    JI("aload_0", "");
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("iconst_1", "");
    JI("isub", "");
    JI("istore_2", "");   /* local2 = i = size-1 */
    J("pj_atl_loop:\n");
    JI("iload_2", "");
    J("    iflt pj_atl_done\n");
    /* build cons cell: [arr[i] | acc] */
    JI("bipush", "4");
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    JI("ldc", "\"compound\"");
    JI("aastore", "");
    JI("dup", "");
    JI("iconst_1", "");
    JI("ldc", "\".\"");
    JI("aastore", "");
    JI("dup", "");
    JI("iconst_2", "");
    JI("aload_0", "");
    JI("iload_2", "");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    JI("aastore", "");
    JI("dup", "");
    JI("iconst_3", "");
    JI("aload_1", "");
    JI("aastore", "");
    JI("astore_1", "");
    JI("iinc", "2 -1");
    J("    goto pj_atl_loop\n");
    J("pj_atl_done:\n");
    JI("aload_1", "");
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_sort_list(Object list, int dedup) -> Object
     * Sorts a pj list by pj_term_str order (insertion sort).
     * dedup=1 → remove duplicates (sort/2); dedup=0 → keep (msort/2). */
    J("; pj_sort_list(Object list, int dedup) -> Object\n");
    J(".method static pj_sort_list(Ljava/lang/Object;I)Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 8\n");
    /* collect to ArrayList */
    JI("aload_0", "");
    J("    invokestatic %s/pj_list_to_arraylist(Ljava/lang/Object;)Ljava/util/ArrayList;\n", pj_classname);
    JI("astore_2", "");   /* local2 = al */
    JI("aload_2", "");
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("istore_3", "");   /* local3 = n */
    /* insertion sort: for i=1..n-1: key=al[i]; j=i-1; while j>=0 && al[j]>key: al[j+1]=al[j]; j--; al[j+1]=key */
    JI("iconst_1", "");
    JI("istore", "4");   /* i */
    J("pj_sl_outer:\n");
    JI("iload", "4");
    JI("iload_3", "");
    J("    if_icmpge pj_sl_sorted\n");
    JI("aload_2", "");
    JI("iload", "4");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    JI("astore", "5");   /* key */
    JI("iload", "4");
    JI("iconst_1", "");
    JI("isub", "");
    JI("istore", "6");   /* j */
    J("pj_sl_inner:\n");
    JI("iload", "6");
    J("    iflt pj_sl_insert\n");
    /* compare al[j] vs key via pj_term_str */
    JI("aload_2", "");
    JI("iload", "6");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("aload", "5");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/String/compareTo(Ljava/lang/String;)I");
    J("    ifle pj_sl_insert\n");   /* al[j] <= key → stop */
    /* al[j+1] = al[j] */
    JI("aload_2", "");
    JI("iload", "6");
    JI("iconst_1", "");
    JI("iadd", "");
    JI("aload_2", "");
    JI("iload", "6");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    JI("invokevirtual", "java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;");
    JI("pop", "");
    JI("iinc", "6 -1");
    J("    goto pj_sl_inner\n");
    J("pj_sl_insert:\n");
    JI("aload_2", "");
    JI("iload", "6");
    JI("iconst_1", "");
    JI("iadd", "");
    JI("aload", "5");
    JI("invokevirtual", "java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;");
    JI("pop", "");
    JI("iinc", "4 1");
    J("    goto pj_sl_outer\n");
    J("pj_sl_sorted:\n");
    /* dedup if requested */
    JI("iload_1", "");
    J("    ifeq pj_sl_build\n");
    /* dedup: walk forward, remove consecutive equal elements */
    JI("iconst_0", "");
    JI("istore", "4");   /* i */
    J("pj_sl_dedup:\n");
    JI("iload", "4");
    JI("aload_2", "");
    JI("invokevirtual", "java/util/ArrayList/size()I");
    JI("iconst_1", "");
    JI("isub", "");
    J("    if_icmpge pj_sl_build\n");
    /* compare al[i] vs al[i+1] */
    JI("aload_2", "");
    JI("iload", "4");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("aload_2", "");
    JI("iload", "4");
    JI("iconst_1", "");
    JI("iadd", "");
    JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("invokevirtual", "java/lang/String/compareTo(Ljava/lang/String;)I");
    J("    ifne pj_sl_dedup_next\n");
    /* equal: remove al[i+1] */
    JI("aload_2", "");
    JI("iload", "4");
    JI("iconst_1", "");
    JI("iadd", "");
    JI("invokevirtual", "java/util/ArrayList/remove(I)Ljava/lang/Object;");
    JI("pop", "");
    J("    goto pj_sl_dedup\n");
    J("pj_sl_dedup_next:\n");
    JI("iinc", "4 1");
    J("    goto pj_sl_dedup\n");
    J("pj_sl_build:\n");
    JI("aload_2", "");
    J("    invokestatic %s/pj_arraylist_to_list(Ljava/util/ArrayList;)Ljava/lang/Object;\n", pj_classname);
    JI("areturn", "");
    J(".end method\n\n");

    /* pj_is_var(Object) -> Z
     * Returns 1 if term is an unbound variable (null or var tag with null binding). */
    J("; pj_is_var(Object) -> Z\n");
    J(".method static pj_is_var(Ljava/lang/Object;)Z\n");
    J("    .limit stack 3\n");
    J("    .limit locals 1\n");
    JI("aload_0", "");
    J("    ifnull pj_isvar_true\n");
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("aaload", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    J("    ifeq pj_isvar_false\n");
    /* tag=="var": check [1]==null */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    J("    ifnonnull pj_isvar_false\n");
    J("pj_isvar_true:\n");
    JI("iconst_1", "");
    JI("ireturn", "");
    J("pj_isvar_false:\n");
    JI("iconst_0", "");
    JI("ireturn", "");
    J(".end method\n\n");

    /* pj_succ_2(Object x, Object y) -> Z
     * succ(?X, ?Y): Y = X+1 or X = Y-1.
     * Returns 1 on success (after unification), 0 on failure. */
    J("; pj_succ_2(Object x, Object y) -> Z\n");
    J(".method static pj_succ_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 6\n");
    J("    .limit locals 6\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_1", "");
    JI("aload_0", "");
    J("    invokestatic %s/pj_is_var(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifne pj_succ2_x_unbound\n");
    /* X is bound: Y = X+1 */
    JI("aload_0", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("lconst_1", "");
    JI("ladd", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("pj_succ2_x_unbound:\n");
    /* X unbound: X = Y-1; fail if Y-1 < 0 */
    JI("aload_1", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("lconst_1", "");
    JI("lsub", "");
    JI("dup2", "");
    JI("lstore_2", "");   /* save result */
    JI("lconst_0", "");
    JI("lcmp", "");
    J("    iflt pj_succ2_neg\n");
    JI("lload_2", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_0", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("pj_succ2_neg:\n");
    /* Y < 0: throw domain_error(not_less_than_zero, Y) */
    /* Build error term: error(domain_error(not_less_than_zero, Y), context) */
    JI("lload_2", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname); /* Y-1 as term */
    /* domain_error(not_less_than_zero, Y-1+1) — actually Y is aload_1 */
    JI("pop", ""); /* discard Y-1 term */
    JI("aload_1", ""); /* Y term */
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    /* build domain_error(not_less_than_zero, Y) compound */
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\"domain_error\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", "");
    JI("ldc", "\"not_less_than_zero\"");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aastore", "");
    JI("dup", ""); JI("bipush", "3");
    JI("aload_1", ""); J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("aastore", "");
    JI("astore", "4"); /* save domain_error term in slot 4 (slots 2-3 = long) */
    /* build error(domain_error(...), succ/2) compound */
    JI("bipush", "4"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\"error\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", ""); JI("aload", "4"); JI("aastore", "");
    JI("dup", ""); JI("bipush", "3");
    JI("ldc", "\"succ/2\"");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aastore", "");
    /* throw it */
    J("    putstatic %s/pj_throw_term Ljava/lang/Object;\n", pj_classname);
    J("    new java/lang/RuntimeException\n");
    JI("dup", ""); JI("ldc", "\"PROLOG_THROW\"");
    J("    invokespecial java/lang/RuntimeException/<init>(Ljava/lang/String;)V\n");
    J("    athrow\n");
    J(".end method\n\n");

    /* pj_plus_3(Object x, Object y, Object z) -> Z
     * plus(?X, ?Y, ?Z): Z = X+Y (any two bound determines third).
     * Returns 1 on success, 0 on failure. */
    J("; pj_plus_3(Object x, Object y, Object z) -> Z\n");
    J(".method static pj_plus_3(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 8\n");
    J("    .limit locals 8\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_1", "");
    JI("aload_2", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_2", "");
    /* case: X var → Z = Y+? → need Y and Z bound: X = Z-Y */
    JI("aload_0", "");
    J("    invokestatic %s/pj_is_var(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifne pj_plus3_x_var\n");
    /* X bound */
    JI("aload_1", "");
    J("    invokestatic %s/pj_is_var(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifne pj_plus3_y_var\n");
    /* X and Y bound: Z = X+Y */
    JI("aload_0", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("ladd", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_2", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("pj_plus3_y_var:\n");
    /* X bound, Y var, Z must be bound: Y = Z-X */
    JI("aload_2", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("aload_0", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("lsub", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("pj_plus3_x_var:\n");
    /* X var, Y and Z must be bound: X = Z-Y */
    JI("aload_2", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("lsub", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_0", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_nb_setval_2(Object name, Object value) -> V
     * nb_setval(+Name, +Value): store Value in global nb table under Name.
     * Destructive (no trail). Always succeeds.
     * ------------------------------------------------------------------ */
    J("; pj_nb_setval_2(Object name, Object value) -> V\n");
    J(".method static pj_nb_setval_2(Ljava/lang/Object;Ljava/lang/Object;)V\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    /* deref name → String key */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    /* deref value */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    /* pj_nb.put(key, value) */
    J("    getstatic %s/pj_nb Ljava/util/HashMap;\n", pj_classname);
    /* stack: key value map — need map key value */
    JI("dup_x2", "");
    JI("pop", "");
    /* stack: map key value */
    JI("invokevirtual", "java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    JI("pop", "");
    JI("return", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_nb_getval_2(Object name, Object var) -> Z
     * nb_getval(+Name, -Value): unify Value with stored term.
     * Fails if Name not set.
     * ------------------------------------------------------------------ */
    J("; pj_nb_getval_2(Object name, Object var) -> Z\n");
    J(".method static pj_nb_getval_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 3\n");
    /* key = atom_name(deref(name)) */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    astore_2\n");
    /* val = pj_nb.get(key) */
    J("    getstatic %s/pj_nb Ljava/util/HashMap;\n", pj_classname);
    J("    aload_2\n");
    JI("invokevirtual", "java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;");
    JI("dup", "");
    J("    ifnonnull pj_nb_getval_found\n");
    JI("pop", "");
    JI("iconst_0", "");
    JI("ireturn", "");
    J("pj_nb_getval_found:\n");
    /* unify val with arg1 */
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_succ_or_zero_2(Object x, Object y) -> Z
     * succ_or_zero(+X, ?Y): Y = max(0, X-1).
     * X must be bound non-negative integer. Y unified with result.
     * ------------------------------------------------------------------ */
    J("; pj_succ_or_zero_2(Object x, Object y) -> Z\n");
    J(".method static pj_succ_or_zero_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 6\n");
    J("    .limit locals 4\n");
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("lstore_2", "");       /* local 2-3 = x (long) */
    JI("lload_2", "");
    JI("lconst_0", "");
    JI("lcmp", "");
    J("    ifle pj_soz_zero\n");
    /* x > 0: result = x - 1 */
    JI("lload_2", "");
    JI("lconst_1", "");
    JI("lsub", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J("pj_soz_zero:\n");
    /* x <= 0: result = 0 */
    JI("lconst_0", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_format_2(Object fmt, Object arglist) -> V
     * Implements format/1 (pass nil as arglist) and format/2.
     * Directives: ~w ~a ~d (write term/int), ~n (newline), ~i (ignore).
     * locals: 0=fmt 1=arglist 2=fmtstr(String) 3=len(int)
     *         4=i(int) 5=ch(int) 6=head(Object)
     * ------------------------------------------------------------------ */
    J("; pj_format_2(Object fmt, Object arglist) -> V\n");
    J(".method static pj_format_2(Ljava/lang/Object;Ljava/lang/Object;)V\n");
    J("    .limit stack 6\n");
    J("    .limit locals 7\n");
    /* deref fmt, extract name string */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_0", "");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("astore_2", "");      /* fmtstr */
    JI("aload_2", "");
    JI("invokevirtual", "java/lang/String/length()I");
    JI("istore_3", "");      /* len */
    JI("iconst_0", "");
    JI("istore", "4");       /* i = 0 */
    J("pjfmt_loop:\n");
    JI("iload", "4");
    JI("iload_3", "");
    JI("if_icmpge", "pjfmt_done");
    JI("aload_2", "");
    JI("iload", "4");
    JI("invokevirtual", "java/lang/String/charAt(I)C");
    JI("istore", "5");       /* ch */
    /* if ch != '~' goto plain */
    JI("iload", "5");
    JI("bipush", "126");
    JI("if_icmpne", "pjfmt_plain");
    /* tilde: consume it, read next */
    JI("iinc", "4 1");
    JI("iload", "4");
    JI("iload_3", "");
    JI("if_icmpge", "pjfmt_done");
    JI("aload_2", "");
    JI("iload", "4");
    JI("invokevirtual", "java/lang/String/charAt(I)C");
    JI("istore", "5");
    /* dispatch on directive */
    JI("iload", "5"); JI("bipush", "119"); JI("if_icmpeq", "pjfmt_write"); /* w */
    JI("iload", "5"); JI("bipush", "97");  JI("if_icmpeq", "pjfmt_write"); /* a */
    JI("iload", "5"); JI("bipush", "100"); JI("if_icmpeq", "pjfmt_write"); /* d */
    JI("iload", "5"); JI("bipush", "105"); JI("if_icmpeq", "pjfmt_ignore");/* i */
    JI("iload", "5"); JI("bipush", "110"); JI("if_icmpeq", "pjfmt_nl");    /* n */
    /* unknown: skip */
    JI("iinc", "4 1");
    JI("goto", "pjfmt_loop");

    J("pjfmt_write:\n");
    /* head = car(arglist) */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");   /* args[2] = head */
    JI("astore", "6");
    /* arglist = cdr(arglist) */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("bipush", "3"); JI("aaload", "");    /* args[3] = tail */
    JI("astore_1", "");
    JI("aload", "6");
    J("    invokestatic %s/pj_write(Ljava/lang/Object;)V\n", pj_classname);
    JI("iinc", "4 1");
    JI("goto", "pjfmt_loop");

    J("pjfmt_ignore:\n");
    /* advance arglist, discard head */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("bipush", "3"); JI("aaload", "");
    JI("astore_1", "");
    JI("iinc", "4 1");
    JI("goto", "pjfmt_loop");

    J("pjfmt_nl:\n");
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("invokevirtual", "java/io/PrintStream/println()V");
    JI("iinc", "4 1");
    JI("goto", "pjfmt_loop");

    J("pjfmt_plain:\n");
    JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
    JI("iload", "5");
    JI("i2c", "");
    JI("invokestatic", "java/lang/Character/toString(C)Ljava/lang/String;");
    JI("invokevirtual", "java/io/PrintStream/print(Ljava/lang/String;)V");
    JI("iinc", "4 1");
    JI("goto", "pjfmt_loop");

    J("pjfmt_done:\n");
    JI("return", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_numbervars_walk(Object term, int[] counter) -> V
     * Recursive tree walk for numbervars.
     * Binds each unbound var to '$VAR'(N) and increments counter[0].
     * locals: 0=term(Object) 1=counter(int[]) 2=tag(Object) 3=arity(int) 4=i(int)
     * ------------------------------------------------------------------ */
    J("; pj_numbervars_walk(Object term, int[] counter) -> V\n");
    J(".method static pj_numbervars_walk(Ljava/lang/Object;[I)V\n");
    J("    .limit stack 8\n");
    J("    .limit locals 6\n");
    /* deref term */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    /* get tag */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", "");
    JI("astore_2", "");
    /* if tag == "var" → bind to '$VAR'(counter[0]) */
    JI("aload_2", "");
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjnv_not_var");
    /* check [1] is null (unbound) */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("ifnonnull", "pjnv_done");  /* already bound, skip */
    /* build '$VAR'(N) compound: ["compound","$VAR", int_term(counter[0])] */
    JI("aload_1", "");  /* counter */
    JI("iconst_0", ""); JI("iaload", "");  /* counter[0] */
    JI("istore_3", "");  /* local3 = n */
    /* make int term for n */
    JI("iload_3", "");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("astore", "4");  /* local4 = int_term */
    /* build ["compound","$VAR",int_term] */
    JI("bipush", "3"); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", ""); JI("ldc", "\"compound\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_1", ""); JI("ldc", "\"$VAR\""); JI("aastore", "");
    JI("dup", ""); JI("iconst_2", ""); JI("aload", "4"); JI("aastore", "");
    JI("astore", "5");  /* local5 = $VAR compound */
    /* bind: turn var into a ref pointing at $VAR compound */
    /* var_cell[0] = "ref" */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", "");
    JI("ldc", "\"ref\"");
    JI("aastore", "");
    /* var_cell[1] = $VAR compound */
    JI("aload_0", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aload", "5");
    JI("aastore", "");
    /* counter[0]++ */
    JI("aload_1", "");
    JI("iconst_0", "");
    JI("aload_1", ""); JI("iconst_0", ""); JI("iaload", "");
    JI("iconst_1", ""); JI("iadd", "");
    JI("iastore", "");
    JI("goto", "pjnv_done");

    J("pjnv_not_var:\n");
    /* if tag == "compound" → recurse into args */
    JI("aload_2", "");
    JI("ldc", "\"compound\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjnv_done");
    /* arity = arraylength - 2 */
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("arraylength", ""); JI("iconst_2", ""); JI("isub", "");
    JI("istore_3", "");  /* local3 = arity */
    JI("iconst_0", ""); JI("istore", "4");  /* i = 0 */
    J("pjnv_arg_loop:\n");
    JI("iload", "4"); JI("iload_3", ""); JI("if_icmpge", "pjnv_done");
    JI("aload_0", ""); JI("checkcast", "[Ljava/lang/Object;");
    JI("iload", "4"); JI("iconst_2", ""); JI("iadd", ""); JI("aaload", "");
    JI("aload_1", "");
    J("    invokestatic %s/pj_numbervars_walk(Ljava/lang/Object;[I)V\n", pj_classname);
    JI("iinc", "4 1");
    JI("goto", "pjnv_arg_loop");

    J("pjnv_done:\n");
    JI("return", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_numbervars_3(Object term, Object start, Object end) -> Z
     * numbervars(+Term, +Start, -End)
     * locals: 0=term 1=start 2=end 3=counter(int[1]) 4=final_n
     * ------------------------------------------------------------------ */
    J("; pj_numbervars_3(Object term, Object start, Object end) -> Z\n");
    J(".method static pj_numbervars_3(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 6\n");
    J("    .limit locals 5\n");
    /* counter = new int[1]; counter[0] = pj_int_val(deref(start)) */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    JI("l2i", "");
    JI("iconst_1", ""); JI("newarray", "int");
    JI("dup_x1", "");
    JI("swap", "");
    JI("iconst_0", ""); JI("swap", ""); JI("iastore", "");
    JI("astore_3", "");  /* counter */
    /* walk */
    JI("aload_0", ""); JI("aload_3", "");
    J("    invokestatic %s/pj_numbervars_walk(Ljava/lang/Object;[I)V\n", pj_classname);
    /* build int term for counter[0] */
    JI("aload_3", ""); JI("iconst_0", ""); JI("iaload", "");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    /* unify with end */
    JI("aload_2", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* ------------------------------------------------------------------
     * pj_char_type_2(Object ch, Object type) -> Z
     * char_type(+Char, +Type): tests/queries character properties.
     * Supported types: alpha, alnum, digit, digit(V), space, white,
     *   end_of_line, upper, upper(L), lower, lower(U),
     *   to_upper(U), to_lower(L), ascii(Code).
     * locals:
     *   0 = ch (Object)   1 = type (Object)
     *   2 = ch_str (String) 3 = ch_char (char/int)
     *   4 = type_name (String) 5 = scratch (Object)
     * ------------------------------------------------------------------ */
    J("; pj_char_type_2(Object ch, Object type) -> Z\n");
    J(".method static pj_char_type_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 6\n");
    J("    .limit locals 6\n");
    /* deref ch → get char string */
    JI("aload_0", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_0", "");
    JI("aload_0", "");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("astore_2", "");  /* ch_str */
    /* ch_char = ch_str.charAt(0) */
    JI("aload_2", "");
    JI("iconst_0", "");
    JI("invokevirtual", "java/lang/String/charAt(I)C");
    JI("istore_3", "");  /* ch_char (int) */
    /* deref type */
    JI("aload_1", "");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("astore_1", "");
    /* get type tag */
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", "");
    /* if tag == "compound" → has argument (digit(V), upper(L), etc.) */
    JI("ldc", "\"compound\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifne", "pjct_compound_type");
    /* atom type: get name */
    JI("aload_1", "");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    JI("astore", "4");  /* type_name */

    /* alpha */
    JI("aload", "4"); JI("ldc", "\"alpha\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_alnum");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isLetter(C)Z");
    JI("ireturn", "");

    J("pjct_try_alnum:\n");
    JI("aload", "4"); JI("ldc", "\"alnum\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_digit");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isLetterOrDigit(C)Z");
    JI("ireturn", "");

    J("pjct_try_digit:\n");
    JI("aload", "4"); JI("ldc", "\"digit\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_space");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isDigit(C)Z");
    JI("ireturn", "");

    J("pjct_try_space:\n");
    JI("aload", "4"); JI("ldc", "\"space\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_white");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isWhitespace(C)Z");
    JI("ireturn", "");

    J("pjct_try_white:\n");
    JI("aload", "4"); JI("ldc", "\"white\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_eol");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isWhitespace(C)Z");
    JI("ireturn", "");

    J("pjct_try_eol:\n");
    JI("aload", "4"); JI("ldc", "\"end_of_line\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_upper");
    /* end_of_line: ch == '\n' (10) or '\r' (13) */
    JI("iload_3", ""); JI("bipush", "10"); JI("if_icmpeq", "pjct_true");
    JI("iload_3", ""); JI("bipush", "13"); JI("if_icmpeq", "pjct_true");
    JI("iconst_0", ""); JI("ireturn", "");

    J("pjct_try_upper:\n");
    JI("aload", "4"); JI("ldc", "\"upper\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_lower");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isUpperCase(C)Z");
    JI("ireturn", "");

    J("pjct_try_lower:\n");
    JI("aload", "4"); JI("ldc", "\"lower\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_fail");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isLowerCase(C)Z");
    JI("ireturn", "");

    J("pjct_true:\n");
    JI("iconst_1", ""); JI("ireturn", "");
    J("pjct_fail:\n");
    JI("iconst_0", ""); JI("ireturn", "");

    /* compound type: functor + 1 arg */
    J("pjct_compound_type:\n");
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("astore", "4");  /* type functor name */

    /* digit(V): unify V with numeric value */
    JI("aload", "4"); JI("ldc", "\"digit\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_upper_l");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isDigit(C)Z");
    JI("ifeq", "pjct_fail");
    /* value = ch - '0' */
    JI("iload_3", ""); JI("bipush", "48"); JI("isub", "");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");  /* arg of digit(V) */
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");

    /* upper(L): ch is upper, L is lower version */
    J("pjct_try_upper_l:\n");
    JI("aload", "4"); JI("ldc", "\"upper\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_lower_u");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isUpperCase(C)Z");
    JI("ifeq", "pjct_fail");
    /* build atom for toLowerCase(ch) */
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/toLowerCase(C)C");
    JI("invokestatic", "java/lang/Character/toString(C)Ljava/lang/String;");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");

    /* lower(U): ch is lower, U is upper version */
    J("pjct_try_lower_u:\n");
    JI("aload", "4"); JI("ldc", "\"lower\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_to_upper");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/isLowerCase(C)Z");
    JI("ifeq", "pjct_fail");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/toUpperCase(C)C");
    JI("invokestatic", "java/lang/Character/toString(C)Ljava/lang/String;");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");

    /* to_upper(U): unify U with uppercase of ch (always succeeds) */
    J("pjct_try_to_upper:\n");
    JI("aload", "4"); JI("ldc", "\"to_upper\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_to_lower");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/toUpperCase(C)C");
    JI("invokestatic", "java/lang/Character/toString(C)Ljava/lang/String;");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");

    /* to_lower(L): unify L with lowercase of ch (always succeeds) */
    J("pjct_try_to_lower:\n");
    JI("aload", "4"); JI("ldc", "\"to_lower\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_try_ascii");
    JI("iload_3", "");
    JI("invokestatic", "java/lang/Character/toLowerCase(C)C");
    JI("invokestatic", "java/lang/Character/toString(C)Ljava/lang/String;");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");

    /* ascii(Code): unify Code with char code */
    J("pjct_try_ascii:\n");
    JI("aload", "4"); JI("ldc", "\"ascii\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    JI("ifeq", "pjct_fail");
    JI("iload_3", "");
    JI("i2l", "");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    JI("aload_1", "");
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_2", ""); JI("aaload", "");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    JI("ireturn", "");
    J(".end method\n\n");

    /* pj_atom_string_2(Object a, Object s) -> Z
     * atom_string/2: bidirectional atom<->string (strings treated as atoms).
     * Forward (atom bound): extract name, box as atom, unify with s.
     * Reverse (atom unbound): deref s, extract name, box as atom, unify with a. */
    J("; pj_atom_string_2\n");
    J(".method static pj_atom_string_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 4\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    checkcast [Ljava/lang/Object;\n");
    J("    astore_2\n");
    J("    aload_2\n");
    J("    iconst_0\n");
    J("    aaload\n");
    J("    ldc \"var\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne as2_rev\n");
    J("    aload_2\n");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ireturn\n");
    J("as2_rev:\n");
    J("    aload_1\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    checkcast [Ljava/lang/Object;\n");
    J("    astore_3\n");
    J("    aload_3\n");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_0\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ireturn\n");
    J(".end method\n\n");

    /* pj_number_string_2(Object n, Object s) -> Z
     * number_string/2: bidirectional.
     * Forward (n bound): convert to string atom, unify with s.
     * Reverse (n unbound): parse string, build int term, unify with n. */
    J("; pj_number_string_2\n");
    J(".method static pj_number_string_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 5\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    checkcast [Ljava/lang/Object;\n");
    J("    astore_2\n");
    J("    aload_2\n");
    J("    iconst_0\n");
    J("    aaload\n");
    J("    ldc \"var\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne ns2_rev\n");
    J("    aload_2\n");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ireturn\n");
    J("ns2_rev:\n");
    J("    aload_1\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_3\n");
    J("    aload_3\n");
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    astore 4\n");
    J("    aload 4\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_0\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ireturn\n");
    J(".end method\n\n");

    /* pj_term_to_atom_2(Object term, Object atom_or_var) -> Z
     * term_to_atom/2 and term_string/2 — forward only (term bound).
     * Converts term to its write-representation string, boxes as atom, unifies. */
    J("; pj_term_to_atom_2\n");
    J(".method static pj_term_to_atom_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ireturn\n");
    J(".end method\n\n");

    /* pj_alc_sep(Object list, String sep) -> String
     * Walk a Prolog list of atoms/numbers, concat with separator string.
     * locals: 0=list(Object) 1=sep(String) 2=sb(StringBuilder) 3=cur(Object) 4=first(I) */
    J("; pj_alc_sep(Object list, String sep) -> String\n");
    J(".method static pj_alc_sep(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 5\n");
    J("    new java/lang/StringBuilder\n");
    J("    dup\n");
    J("    invokespecial java/lang/StringBuilder/<init>()V\n");
    J("    astore_2\n");
    J("    iconst_1\n");  /* first = 1 */
    J("    istore 4\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_3\n");
    J("alcs_loop:\n");
    J("    aload_3\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_1\n");
    J("    aaload\n");  /* tag/name field */
    J("    ldc \"[]\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne alcs_done\n");
    /* not first: append separator */
    J("    iload 4\n");
    J("    ifne alcs_skip_sep\n");
    J("    aload_2\n");
    J("    aload_1\n");
    J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
    J("    pop\n");
    J("alcs_skip_sep:\n");
    J("    iconst_0\n");
    J("    istore 4\n");
    /* head = field[2] */
    J("    aload_3\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_2\n");
    J("    aaload\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    aload_2\n");
    J("    swap\n");
    J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
    J("    pop\n");
    /* tail = field[3] */
    J("    aload_3\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    bipush 3\n");
    J("    aaload\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_3\n");
    J("    goto alcs_loop\n");
    J("alcs_done:\n");
    J("    aload_2\n");
    J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
    J("    areturn\n");
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
    JC("M-PJ-EXCEPTIONS — throw/catch term carrier");
    J(".field static pj_throw_term Ljava/lang/Object;\n\n");
    JC("Dynamic DB field: HashMap<String, ArrayList<Object[]>>");
    J(".field static pj_db Ljava/util/HashMap;\n\n");
    JC("Global nb_setval/nb_getval store: HashMap<String, Object[]>");
    J(".field static pj_nb Ljava/util/HashMap;\n\n");

    /* <clinit> — init trail, dynamic DB, and nb store */
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
    JI("new", "java/util/HashMap");
    JI("dup", "");
    JI("invokespecial", "java/util/HashMap/<init>()V");
    J("    putstatic %s/pj_nb Ljava/util/HashMap;\n", pj_classname);
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
static void pj_emit_arith_as_term(EXPR_t *e, int *var_locals, int n_vars);
static void pj_emit_arith_as_double(EXPR_t *e, int *var_locals, int n_vars);
static void pj_emit_arith_as_term(EXPR_t *e, int *var_locals, int n_vars);

/* pj_arith_is_float — returns 1 if arithmetic expression produces a float result */
/* pj_emit_dbl_const — emit a double constant onto the JVM stack as D.
 * Jasmin truncates ldc2_w decimal literals to float32, so we use the raw
 * bit pattern: ldc2_w <longbits> then longBitsToDouble. */
static void pj_emit_dbl_const(double v) {
    /* reinterpret double bits as long */
    long long bits;
    memcpy(&bits, &v, sizeof bits);
    J("    ldc2_w %lld\n", (long long)bits);
    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
}

static int pj_arith_is_float(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FLIT) return 1;
    if (e->kind == E_FNC && e->sval) {
        const char *fn = e->sval;
        /* these always produce float regardless of input */
        if (strcmp(fn,"sqrt")==0 || strcmp(fn,"sin")==0 || strcmp(fn,"cos")==0 ||
            strcmp(fn,"tan")==0  || strcmp(fn,"exp")==0 || strcmp(fn,"log")==0 ||
            strcmp(fn,"atan")==0 || strcmp(fn,"atan2")==0 || strcmp(fn,"float")==0 ||
            strcmp(fn,"float_integer_part")==0 ||
            strcmp(fn,"asin")==0 || strcmp(fn,"acos")==0 ||
            strcmp(fn,"sinh")==0 || strcmp(fn,"cosh")==0 || strcmp(fn,"tanh")==0 ||
            strcmp(fn,"asinh")==0 || strcmp(fn,"acosh")==0 || strcmp(fn,"atanh")==0 ||
            strcmp(fn,"pi")==0   || strcmp(fn,"e")==0 ||
            strcmp(fn,"nan")==0  || strcmp(fn,"inf")==0 || strcmp(fn,"infinity")==0)
            return 1;
        /* float_fractional_part: float if arg is float, integer (0) if arg is integer */
        if (strcmp(fn,"float_fractional_part")==0)
            return (e->nchildren==1) ? pj_arith_is_float(e->children[0]) : 0;
        /* copysign: result type follows magnitude (first arg) */
        if (strcmp(fn,"copysign")==0 && e->nchildren==2)
            return pj_arith_is_float(e->children[0]);
        /* sign: result type follows arg type */
        if (strcmp(fn,"sign")==0 && e->nchildren==1)
            return pj_arith_is_float(e->children[0]);
        /* these always produce integer regardless of input */
        if (strcmp(fn,"truncate")==0 || strcmp(fn,"integer")==0 ||
            strcmp(fn,"round")==0    || strcmp(fn,"ceiling")==0 || strcmp(fn,"ceil")==0 ||
            strcmp(fn,"floor")==0    || strcmp(fn,"gcd")==0)
            return 0;
    }
    /* propagate: if any child is float, result is float */
    for (int i = 0; i < (int)e->nchildren; i++)
        if (pj_arith_is_float(e->children[i])) return 1;
    return 0;
}

/* pj_arith_is_mixed_minmax — returns 1 if e is min/max with one int and one float arg.
 * These need the pj_min_mixed/pj_max_mixed runtime helper to preserve result type. */
static int pj_arith_is_mixed_minmax(EXPR_t *e) {
    if (!e || !e->sval || e->nchildren != 2) return 0;
    if (strcmp(e->sval,"min")!=0 && strcmp(e->sval,"max")!=0) return 0;
    int lf = pj_arith_is_float(e->children[0]);
    int rf = pj_arith_is_float(e->children[1]);
    return (lf != rf);  /* exactly one is float */
}


/* pj_arith_has_var — returns 1 if expression contains a runtime variable.
 * When true, =:= etc. must use pj_num_cmp for correct type handling. */
static int pj_arith_has_var(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_VART) return 1;
    for (int i = 0; i < (int)e->nchildren; i++)
        if (pj_arith_has_var(e->children[i])) return 1;
    return 0;
}


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
        pj_ldc_str(e->sval ? e->sval : "");
        J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n",
          pj_classname);
        break;
    }
    case E_ILIT: {
        J("    ldc2_w %ld\n", e->ival);
        J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
        break;
    }
    case E_FLIT: {
        pj_emit_dbl_const(e->dval);   /* push double constant */
        J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
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
            pj_ldc_str(e->sval ? e->sval : "");
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
            pj_ldc_str(e->sval ? e->sval : "");
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
    case E_FLIT:
        /* float literal: push as double, convert to long bits */
        pj_emit_dbl_const(e->dval);
        JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
        break;
    case E_VART: {
        int slot = e->ival;
        /* load var, deref; if float tag → parseDouble→bits, else parseLong */
        if (slot >= 0 && slot < n_vars && var_locals)
            J("    aload %d\n", var_locals[slot]);
        else
            JI("aconst_null", "");
        J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
        JI("checkcast", "[Ljava/lang/Object;");
        JI("dup", "");
        JI("iconst_0", "");
        JI("aaload", "");          /* tag string */
        JI("ldc", "\"float\"");
        JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
        char lbl_flt[64], lbl_done[64];
        static int pj_vart_cnt = 0; pj_vart_cnt++;
        snprintf(lbl_flt,  sizeof lbl_flt,  "pj_vart_flt_%d",  pj_vart_cnt);
        snprintf(lbl_done, sizeof lbl_done, "pj_vart_done_%d", pj_vart_cnt);
        J("    ifne %s\n", lbl_flt);
        /* int path: return parseLong as-is (raw long) */
        JI("iconst_1", "");
        JI("aaload", "");
        JI("checkcast", "java/lang/String");
        JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        J("    goto %s\n", lbl_done);
        /* float path */
        J("%s:\n", lbl_flt);
        JI("iconst_1", "");
        JI("aaload", "");
        JI("checkcast", "java/lang/String");
        JI("invokestatic", "java/lang/Double/parseDouble(Ljava/lang/String;)D");
        JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
        J("%s:\n", lbl_done);
        break;
    }
    case E_ADD: {
        int lf = pj_arith_is_float(e->children[0]);
        int rf = pj_arith_is_float(e->children[1]);
        int lv = pj_arith_has_var(e->children[0]);
        int rv = pj_arith_has_var(e->children[1]);
        if (lf || rf) {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            if (!lf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            pj_emit_arith(e->children[1], var_locals, n_vars);
            if (!rf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            JI("dadd", "");
            JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
        } else if (pj_arith_has_var(e->children[0]) || pj_arith_has_var(e->children[1])) {
            /* var operand: runtime dispatch via pj_varnum_* */
            pj_emit_arith_as_term(e->children[0], var_locals, n_vars);
            pj_emit_arith_as_term(e->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_varnum_add([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_obj_to_bits([Ljava/lang/Object;)J\n", pj_classname);
        } else {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            pj_emit_arith(e->children[1], var_locals, n_vars);
            JI("ladd", "");
        }
        break;
    }
    case E_SUB: {
        int lf = pj_arith_is_float(e->children[0]);
        int rf = pj_arith_is_float(e->children[1]);
        int lv = pj_arith_has_var(e->children[0]);
        int rv = pj_arith_has_var(e->children[1]);
        if (lf || rf) {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            if (!lf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            pj_emit_arith(e->children[1], var_locals, n_vars);
            if (!rf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            JI("dsub", "");
            JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
        } else if (pj_arith_has_var(e->children[0]) || pj_arith_has_var(e->children[1])) {
            /* var operand: runtime dispatch via pj_varnum_* */
            pj_emit_arith_as_term(e->children[0], var_locals, n_vars);
            pj_emit_arith_as_term(e->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_varnum_sub([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_obj_to_bits([Ljava/lang/Object;)J\n", pj_classname);
        } else {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            pj_emit_arith(e->children[1], var_locals, n_vars);
            JI("lsub", "");
        }
        break;
    }
    case E_MPY: {
        int lf = pj_arith_is_float(e->children[0]);
        int rf = pj_arith_is_float(e->children[1]);
        int lv = pj_arith_has_var(e->children[0]);
        int rv = pj_arith_has_var(e->children[1]);
        if (lf || rf) {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            if (!lf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            pj_emit_arith(e->children[1], var_locals, n_vars);
            if (!rf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            JI("dmul", "");
            JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
        } else if (pj_arith_has_var(e->children[0]) || pj_arith_has_var(e->children[1])) {
            /* var operand: runtime dispatch via pj_varnum_* */
            pj_emit_arith_as_term(e->children[0], var_locals, n_vars);
            pj_emit_arith_as_term(e->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_varnum_mul([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_obj_to_bits([Ljava/lang/Object;)J\n", pj_classname);
        } else {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            pj_emit_arith(e->children[1], var_locals, n_vars);
            JI("lmul", "");
        }
        break;
    }
    case E_DIV: {
        int lf = pj_arith_is_float(e->children[0]);
        int rf = pj_arith_is_float(e->children[1]);
        int lv = pj_arith_has_var(e->children[0]);
        int rv = pj_arith_has_var(e->children[1]);
        if (lf || rf) {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            if (!lf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            pj_emit_arith(e->children[1], var_locals, n_vars);
            if (!rf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            JI("ddiv", "");
            JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
        } else if (pj_arith_has_var(e->children[0]) || pj_arith_has_var(e->children[1])) {
            /* var operand: runtime dispatch via pj_varnum_* */
            pj_emit_arith_as_term(e->children[0], var_locals, n_vars);
            pj_emit_arith_as_term(e->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_varnum_div([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_obj_to_bits([Ljava/lang/Object;)J\n", pj_classname);
        } else {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            pj_emit_arith(e->children[1], var_locals, n_vars);
            JI("ldiv", "");
        }
        break;
    }
    case E_FNC:
        /* mod/2, rem/2, //2 (integer div) — not given dedicated opcodes by lowerer */
        if (e->sval && e->nchildren >= 2) {
            if (strcmp(e->sval, "rem") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("lrem", "");  /* truncating remainder = SWI rem/2 */
                break;
            }
            if (strcmp(e->sval, "mod") == 0) {
                /* floor remainder: r = a rem b; if (r != 0 && (r^b) < 0) r += b */
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                J("    invokestatic %s/pj_mod(JJ)J\n", pj_classname);
                break;
            }
            if (strcmp(e->sval, "//") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("ldiv", "");
                break;
            }
            if (strcmp(e->sval, "/\\") == 0 && e->nchildren == 2) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("land", ""); break;
            }
            if (strcmp(e->sval, "\\/") == 0 && e->nchildren == 2) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("lor", ""); break;
            }
            if (strcmp(e->sval, "xor") == 0 && e->nchildren == 2) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("lxor", ""); break;
            }
            if (strcmp(e->sval, ">>") == 0 && e->nchildren == 2) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                J("    invokestatic %s/pj_shr(JJ)J\n", pj_classname);
                break;
            }
            if (strcmp(e->sval, "<<") == 0 && e->nchildren == 2) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                J("    invokestatic %s/pj_shl(JJ)J\n", pj_classname);
                break;
            }
            if (strcmp(e->sval, "\\") == 0 && e->nchildren == 1) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("lconst_1", ""); JI("lneg", ""); JI("lxor", ""); /* ~N = N xor -1 */
                break;
            }
            if (strcmp(e->sval, "max") == 0 && e->nchildren == 2) {
                int lf = pj_arith_is_float(e->children[0]);
                int rf = pj_arith_is_float(e->children[1]);
                if (lf || rf) {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    if (!lf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    if (!rf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    J("    invokestatic java/lang/Math/max(DD)D\n");
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    J("    invokestatic java/lang/Math/max(JJ)J\n");
                }
                break;
            }
            if (strcmp(e->sval, "min") == 0 && e->nchildren == 2) {
                int lf = pj_arith_is_float(e->children[0]);
                int rf = pj_arith_is_float(e->children[1]);
                if (lf || rf) {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    if (!lf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    if (!rf) JI("l2d", ""); else JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    J("    invokestatic java/lang/Math/min(DD)D\n");
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    J("    invokestatic java/lang/Math/min(JJ)J\n");
                }
                break;
            }
            if ((strcmp(e->sval, "**") == 0 || strcmp(e->sval, "^") == 0) && e->nchildren == 2) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("l2d", "");
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("l2d", "");
                J("    invokestatic java/lang/Math/pow(DD)D\n");
                JI("d2l", "");
                break;
            }
        }
        /* unary arithmetic functions */
        if (e->sval && e->nchildren >= 1) {
            if (strcmp(e->sval, "abs") == 0 && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0])) {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    J("    invokestatic java/lang/Math/abs(D)D\n");
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    J("    invokestatic java/lang/Math/abs(J)J\n");
                }
                break;
            }
            if (strcmp(e->sval, "sign") == 0 && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0])) {
                    /* float sign: Math.signum, then normalize -0.0→0.0 via +0.0 */
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    J("    invokestatic java/lang/Math/signum(D)D\n");
                    JI("dconst_0", "");
                    JI("dadd", "");  /* signum(x)+0.0: normalizes -0.0 to 0.0 */
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                } else {
                    /* integer sign: Long.signum returns int, convert to long */
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    J("    invokestatic java/lang/Long/signum(J)I\n");
                    JI("i2l", "");
                }
                break;
            }
            if (strcmp(e->sval, "copysign") == 0 && e->nchildren == 2) {
                /* copysign(Mag, Sign): result type follows magnitude type */
                int mag_float = pj_arith_is_float(e->children[0]);
                int sgn_float = pj_arith_is_float(e->children[1]);
                if (mag_float) {
                    /* float magnitude: result is float */
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    if (!sgn_float) { JI("l2d", ""); } else { JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D"); }
                    J("    invokestatic java/lang/Math/copySign(DD)D\n");
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                } else if (sgn_float) {
                    /* integer magnitude, float sign: result is integer
                     * copySign(1.0, sign_arg) gives +1.0 or -1.0, then d2l, times abs(mag) */
                    JI("dconst_1", "");
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    J("    invokestatic java/lang/Math/copySign(DD)D\n");
                    JI("d2l", "");
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    J("    invokestatic java/lang/Math/abs(J)J\n");
                    JI("lmul", "");
                } else {
                    /* both integer: abs(Mag) * signum(Sign) */
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    J("    invokestatic java/lang/Math/abs(J)J\n");
                    pj_emit_arith(e->children[1], var_locals, n_vars);
                    J("    invokestatic java/lang/Long/signum(J)I\n");
                    JI("i2l", "");
                    JI("lmul", "");
                }
                break;
            }
            if (strcmp(e->sval, "truncate") == 0 && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0])) {
                    /* float→int: truncate toward zero */
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    JI("d2l", "");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                }
                break;
            }
            if (strcmp(e->sval, "integer") == 0 && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0]) || pj_arith_has_var(e->children[0])) {
                    pj_emit_arith_as_double(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Math/round(D)J");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                }
                break;
            }
            if (strcmp(e->sval, "round") == 0 && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0]) || pj_arith_has_var(e->children[0])) {
                    pj_emit_arith_as_double(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Math/round(D)J");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                }
                break;
            }
            if ((strcmp(e->sval, "ceiling") == 0 || strcmp(e->sval, "ceil") == 0) && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0])) {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    JI("invokestatic", "java/lang/Math/ceil(D)D");
                    JI("d2l", "");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                }
                break;
            }
            if (strcmp(e->sval, "floor") == 0 && e->nchildren == 1) {
                if (pj_arith_is_float(e->children[0])) {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    JI("invokestatic", "java/lang/Math/floor(D)D");
                    JI("d2l", "");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                }
                break;
            }
            if (strcmp(e->sval, "msb") == 0 && e->nchildren == 1) {
                /* msb(N) = 63 - Long.numberOfLeadingZeros(N) */
                JI("ldc2_w", "63");
                pj_emit_arith(e->children[0], var_locals, n_vars);
                J("    invokestatic java/lang/Long/numberOfLeadingZeros(J)I\n");
                JI("i2l", "");
                JI("lsub", "");
                break;
            }
        }  /* end unary */
        /* ---- float math functions ---- */
        if (e->sval && e->nchildren == 1) {
            if (strcmp(e->sval, "sqrt") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/sqrt(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "sin") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/sin(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "cos") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/cos(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "tan") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/tan(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "exp") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/exp(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "log") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/log(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "atan") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/atan(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "asin") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/asin(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "acos") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/acos(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "sinh") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/sinh(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "cosh") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/cosh(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "tanh") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                JI("invokestatic", "java/lang/Math/tanh(D)D");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "asinh") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                J("    invokestatic %s/pj_asinh(D)D\n", pj_classname);
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "acosh") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                J("    invokestatic %s/pj_acosh(D)D\n", pj_classname);
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "atanh") == 0) {
                /* atanh(x) implemented via pj_atanh helper: 0.5*log((1+x)/(1-x)) */
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                J("    invokestatic %s/pj_atanh(D)D\n", pj_classname);
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "float") == 0) {
                /* float(X): if arg is already float, bits pass through unchanged;
                 * if int, convert via l2d */
                pj_emit_arith(e->children[0], var_locals, n_vars);
                if (!pj_arith_is_float(e->children[0])) {
                    JI("l2d", "");
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                }
                /* else: already float bits on stack, leave as-is */
                break;
            }
            if (strcmp(e->sval, "float_integer_part") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                /* promote integer to float if needed */
                if (!pj_arith_is_float(e->children[0])) {
                    JI("l2d", ""); JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                }
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                /* truncate toward zero: (double)(long)x */
                JI("d2l", "");
                JI("l2d", "");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "float_fractional_part") == 0) {
                if (!pj_arith_is_float(e->children[0])) {
                    /* integer arg: fractional part is always 0 (integer) */
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("pop2", "");  /* discard the integer value */
                    JI("lconst_0", "");
                } else {
                    pj_emit_arith(e->children[0], var_locals, n_vars);
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    JI("dup2", "");  /* x x */
                    JI("d2l", ""); JI("l2d", "");  /* x trunc(x) */
                    JI("dsub", "");
                    JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                }
                break;
            }
        }
        if (e->sval && e->nchildren == 2) {
            if (strcmp(e->sval, "atan2") == 0) {
                pj_emit_arith(e->children[0], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                pj_emit_arith(e->children[1], var_locals, n_vars);
                JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                J("    invokestatic java/lang/Math/atan2(DD)D\n");
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "gcd") == 0) {
                /* gcd via Euclidean: emit helper call */
                pj_emit_arith(e->children[0], var_locals, n_vars);
                pj_emit_arith(e->children[1], var_locals, n_vars);
                J("    invokestatic %s/pj_gcd(JJ)J\n", pj_classname);
                break;
            }
        }
        /* pi and e constants */
        if (e->sval && e->nchildren == 0) {
            if (strcmp(e->sval, "pi") == 0) {
                pj_emit_dbl_const(3.141592653589793);
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "e") == 0) {
                pj_emit_dbl_const(2.718281828459045);
                JI("invokestatic", "java/lang/Double/doubleToRawLongBits(D)J");
                break;
            }
            if (strcmp(e->sval, "nan") == 0) {
                J("    ldc2_w 9221120237041090560\n"); /* Double.NaN bits */
                break;
            }
            if (strcmp(e->sval, "inf") == 0 || strcmp(e->sval, "infinity") == 0) {
                J("    ldc2_w 9218868437227405312\n"); /* Double.POSITIVE_INFINITY bits */
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

/* pj_emit_arith_as_term — emit arithmetic expression, leave Object[] term on stack.
 * Used by =:= etc. when operands may contain runtime-typed variables.
 * For variables: deref and return the term directly (already typed).
 * For constants/exprs: compute via pj_emit_arith, then box. */
static void pj_emit_arith_as_term(EXPR_t *e, int *var_locals, int n_vars) {
    if (!e) { JI("aconst_null", ""); return; }
    if (e->kind == E_VART) {
        /* variable: deref and return the term as-is */
        int slot = e->ival;
        if (slot >= 0 && slot < n_vars && var_locals)
            J("    aload %d\n", var_locals[slot]);
        else
            JI("aconst_null", "");
        J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
        JI("checkcast", "[Ljava/lang/Object;");
    } else if (pj_arith_is_mixed_minmax(e)) {
        /* mixed min/max already returns Object[] */
        int is_min = (strcmp(e->sval,"min")==0);
        int lf = pj_arith_is_float(e->children[0]);
        if (lf) {
            pj_emit_arith(e->children[1], var_locals, n_vars);
            pj_emit_arith(e->children[0], var_locals, n_vars);
        } else {
            pj_emit_arith(e->children[0], var_locals, n_vars);
            pj_emit_arith(e->children[1], var_locals, n_vars);
        }
        if (is_min)
            J("    invokestatic %s/pj_min_mixed(JJ)[Ljava/lang/Object;\n", pj_classname);
        else
            J("    invokestatic %s/pj_max_mixed(JJ)[Ljava/lang/Object;\n", pj_classname);
    } else if (pj_arith_has_var(e) && !pj_arith_is_float(e) &&
               (e->kind == E_ADD || e->kind == E_SUB ||
                e->kind == E_MPY || e->kind == E_DIV)) {
        /* var-containing binary op: use runtime pj_varnum_* helper */
        const char *vn_fn = (e->kind==E_ADD) ? "pj_varnum_add" :
                            (e->kind==E_SUB) ? "pj_varnum_sub" :
                            (e->kind==E_MPY) ? "pj_varnum_mul" : "pj_varnum_div";
        pj_emit_arith_as_term(e->children[0], var_locals, n_vars);
        pj_emit_arith_as_term(e->children[1], var_locals, n_vars);
        J("    invokestatic %s/%s([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n",
          pj_classname, vn_fn);
    } else {
        pj_emit_arith(e, var_locals, n_vars);
        if (pj_arith_is_float(e)) {
            JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
            J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
        } else {
            J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
        }
    }
}

/* pj_emit_arith_as_double — emit arithmetic expr leaving a D (double) on JVM stack.
 * For E_VART: uses pj_num_as_double to handle int/float tags at runtime.
 * For float exprs: pj_emit_arith then longBitsToDouble.
 * For int exprs: pj_emit_arith then l2d. */
static void pj_emit_arith_as_double(EXPR_t *e, int *var_locals, int n_vars) {
    if (!e) { JI("dconst_0", ""); return; }
    if (e->kind == E_VART || pj_arith_has_var(e)) {
        /* runtime dispatch: emit as term, then pj_num_as_double */
        pj_emit_arith_as_term(e, var_locals, n_vars);
        J("    invokestatic %s/pj_num_as_double([Ljava/lang/Object;)D\n", pj_classname);
    } else if (pj_arith_is_float(e)) {
        pj_emit_arith(e, var_locals, n_vars);
        JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
    } else {
        pj_emit_arith(e, var_locals, n_vars);
        JI("l2d", "");
    }
}


/* pj_emit_stdlib_shim — always-emitted pure-Prolog stdlib predicates.
 * member/2 and memberchk/2 are general builtins, skipped if user defines them. */
static void pj_emit_choice(EXPR_t *choice); /* forward decl */

static int pj_prog_defines(Program *prog, const char *name, int arity) {
    char key[128]; snprintf(key, sizeof key, "%s/%d", name, arity);
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_CHOICE) continue;
        if (s->subject->sval && strcmp(s->subject->sval, key) == 0)
            return 1;
    }
    return 0;
}

static void pj_emit_stdlib_pred(const char *src) {
    PlProgram *pl = prolog_parse(src, "stdlib");
    if (!pl) return;
    PlProgram *lowered = prolog_lower(pl);
    if (!lowered) return;
    for (STMT_t *s = lowered->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE)
            pj_emit_choice(s->subject);
    }
}

static void pj_emit_stdlib_shim(Program *prog) {
    if (!pj_prog_defines(prog, "member", 2))
        pj_emit_stdlib_pred("member(X, [X|_]).\nmember(X, [_|T]) :- member(X, T).\n");
    if (!pj_prog_defines(prog, "memberchk", 2))
        pj_emit_stdlib_pred("memberchk(X, [X|_]) :- !.\nmemberchk(X, [_|T]) :- memberchk(X, T).\n");
    if (!pj_prog_defines(prog, "forall", 2)) {
        /* forall/2 is now a synthetic JVM method — no pure-Prolog shim needed */
    }
}

/* pj_gcd(J,J)J emitter — emitted once into the class */
static void pj_emit_gcd_helper(void) {
    J(".method static pj_gcd(JJ)J\n");
    J("    .limit stack 4\n");
    J("    .limit locals 4\n");
    /* Euclidean gcd: while b != 0 { t=b; b=a%b; a=t; } return abs(a) */
    J("    lload_0\n");
    J("    invokestatic java/lang/Math/abs(J)J\n");
    J("    lstore_0\n");
    J("    lload_2\n");
    J("    invokestatic java/lang/Math/abs(J)J\n");
    J("    lstore_2\n");
    J("pj_gcd_loop:\n");
    J("    lload_2\n");
    JI("lconst_0", "");
    JI("lcmp", "");
    J("    ifeq pj_gcd_done\n");
    J("    lload_0\n");
    J("    lload_2\n");
    JI("lrem", "");
    J("    lload_2\n");
    J("    lstore_0\n");
    J("    lstore_2\n");
    J("    goto pj_gcd_loop\n");
    J("pj_gcd_done:\n");
    J("    lload_0\n");
    JI("lreturn", "");
    J(".end method\n\n");
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
        "assertz","asserta","abolish","retract",
        "sort","msort",
        "succ","plus",
        "format",
        "numbervars",
        "char_type",
        "writeq","write_canonical",
        "atom_string","number_string",
        "string_chars","string_codes",
        "length",
        "string_concat","string_length","string_lower","string_upper",
        "term_to_atom","term_string",
        "copy_term",
        "string_to_atom","concat_atom",
        "atomic_list_concat",
        "nb_setval","nb_getval",
        "aggregate_all",
        "succ_or_zero",
        "throw","catch",
        "garbage_collect","trim_stacks",
        "?=",
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
            /* evaluate RHS, create int or float term, unify with LHS */
            EXPR_t *rhs = goal->children[1];
            if (pj_arith_is_mixed_minmax(rhs)) {
                /* mixed min/max: helper returns Object[] term directly */
                int is_min = (strcmp(rhs->sval,"min")==0);
                int lf = pj_arith_is_float(rhs->children[0]);
                /* always pass (intArg, floatArg) to helper */
                if (lf) {
                    /* left is float, right is int */
                    pj_emit_arith(rhs->children[1], var_locals, n_vars); /* int */
                    pj_emit_arith(rhs->children[0], var_locals, n_vars); /* float bits */
                } else {
                    /* left is int, right is float */
                    pj_emit_arith(rhs->children[0], var_locals, n_vars); /* int */
                    pj_emit_arith(rhs->children[1], var_locals, n_vars); /* float bits */
                }
                if (is_min)
                    J("    invokestatic %s/pj_min_mixed(JJ)[Ljava/lang/Object;\n", pj_classname);
                else
                    J("    invokestatic %s/pj_max_mixed(JJ)[Ljava/lang/Object;\n", pj_classname);
                /* result is already an Object[] term; unify with LHS */
                pj_emit_term(goal->children[0], var_locals, n_vars);
                JI("swap", "");
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", lbl_ω);
                JI("goto", lbl_γ);
                return;
            }
            /* When rhs has vars with no static float type, use pj_emit_arith_as_term
             * which returns Object[] with correct int/float tag via pj_varnum_* helpers. */
            int rhs_has_var = pj_arith_has_var(rhs);
            int rhs_is_float = pj_arith_is_float(rhs);
            if (rhs_has_var && !rhs_is_float) {
                /* runtime dispatch: pj_emit_arith_as_term handles var+float correctly */
                pj_emit_arith_as_term(rhs, var_locals, n_vars);
            } else {
                pj_emit_arith(rhs, var_locals, n_vars);
                if (rhs_is_float) {
                    JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
                } else {
                    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
                }
            }
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
            int has_var = pj_arith_has_var(goal->children[0]) ||
                          pj_arith_has_var(goal->children[1]);
            int flt = pj_arith_is_float(goal->children[0]) ||
                      pj_arith_is_float(goal->children[1]);
            if (has_var) {
                /* runtime type dispatch via pj_num_cmp */
                pj_emit_arith_as_term(goal->children[0], var_locals, n_vars);
                pj_emit_arith_as_term(goal->children[1], var_locals, n_vars);
                J("    invokestatic %s/pj_num_cmp([Ljava/lang/Object;[Ljava/lang/Object;)I\n", pj_classname);
            } else {
                pj_emit_arith(goal->children[0], var_locals, n_vars);
                if (flt) JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                pj_emit_arith(goal->children[1], var_locals, n_vars);
                if (flt) JI("invokestatic", "java/lang/Double/longBitsToDouble(J)D");
                if (flt) JI("dcmpl", ""); else JI("lcmp", "");
            }
            /* result: negative/zero/positive */
            if (strcmp(fn, "=:=") == 0)      J("    ifne %s\n", lbl_ω);
            else if (strcmp(fn, "=\\=") == 0) J("    ifeq %s\n", lbl_ω);
            else if (strcmp(fn, "<") == 0)    J("    ifge %s\n", lbl_ω);
            else if (strcmp(fn, ">") == 0)    J("    ifle %s\n", lbl_ω);
            else if (strcmp(fn, "=<") == 0)   J("    ifgt %s\n", lbl_ω);
            else if (strcmp(fn, ">=") == 0)   J("    iflt %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* term-order comparisons @< @> @=< @>= */
        if ((strcmp(fn, "@<")  == 0 || strcmp(fn, "@>")  == 0 ||
             strcmp(fn, "@=<") == 0 || strcmp(fn, "@>=") == 0) && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokevirtual java/lang/String/compareTo(Ljava/lang/String;)I\n");
            if      (strcmp(fn, "@<")  == 0) J("    ifge %s\n", lbl_ω);
            else if (strcmp(fn, "@>")  == 0) J("    ifle %s\n", lbl_ω);
            else if (strcmp(fn, "@=<") == 0) J("    ifgt %s\n", lbl_ω);
            else if (strcmp(fn, "@>=") == 0) J("    iflt %s\n", lbl_ω);
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

        /* retract(+Head) — remove first matching fact from dynamic DB, backtrackable.
         * Strategy: peek with pj_db_query first; unify; only remove on confirmed match.
         * On unify failure: undo trail, increment idx, loop. Never removes until match. */
        if (strcmp(fn, "retract") == 0 && nargs == 1) {
            int uid = pj_fresh_label();
            int db_idx_local  = (*next_local)++;  /* I — current probe index */
            int db_term_local = (*next_local)++;  /* Object — peeked term */
            int trail_lbl     = (*next_local)++;  /* I — trail mark for unify probe */
            char loop[64], ok[64], unify_fail[64], miss[64];
            snprintf(loop,       sizeof loop,       "pj_ret%d_loop",  uid);
            snprintf(ok,         sizeof ok,         "pj_ret%d_ok",    uid);
            snprintf(unify_fail, sizeof unify_fail, "pj_ret%d_ufail", uid);
            snprintf(miss,       sizeof miss,       "pj_ret%d_miss",  uid);

            /* initialise locals */
            JI("iconst_0", ""); J("    istore %d\n", db_idx_local);
            JI("aconst_null", ""); J("    astore %d\n", db_term_local);
            JI("iconst_0", ""); J("    istore %d\n", trail_lbl);

            /* --- loop: peek at db_idx_local without removing --- */
            J("%s:\n", loop);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    iload %d\n", db_idx_local);
            J("    invokestatic %s/pj_db_query(Ljava/lang/String;I)Ljava/lang/Object;\n", pj_classname);
            JI("dup", "");
            J("    ifnonnull %s\n", ok);
            JI("pop", "");
            J("    goto %s\n", miss);

            J("%s:\n", ok);
            J("    astore %d\n", db_term_local);
            J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
            J("    istore %d\n", trail_lbl);

            /* probe unify */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    aload %d\n", db_term_local);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", unify_fail);

            /* unify succeeded: remove from DB at db_idx_local */
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    iload %d\n", db_idx_local);
            J("    invokestatic %s/pj_db_retract(Ljava/lang/String;I)Ljava/lang/Object;\n", pj_classname);
            JI("pop", "");
            JI("goto", lbl_γ);

            /* unify failed: unwind trail, increment idx, try next */
            J("%s:\n", unify_fail);
            J("    iload %d\n", trail_lbl);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            J("    iinc %d 1\n", db_idx_local);
            J("    goto %s\n", loop);

            J("%s:\n", miss);
            J("    goto %s\n", lbl_ω);
            return;
        }
        /* abolish(+Functor/Arity) — remove entire predicate from dynamic DB.
         * Always succeeds (even if predicate absent). */
        if (strcmp(fn, "abolish") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_db_abolish_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokestatic %s/pj_db_abolish(Ljava/lang/String;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* length(+List, ?N) — count list elements, unify N */
        if (strcmp(fn, "length") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_length_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* sort(+List, -Sorted) — sort with deduplication */
        /* msort(+List, -Sorted) — sort without deduplication */
        if ((strcmp(fn, "sort") == 0 || strcmp(fn, "msort") == 0) && nargs == 2) {
            int dedup = (strcmp(fn, "sort") == 0) ? 1 : 0;
            int uid = pj_fresh_label();
            char ok[64]; snprintf(ok, sizeof ok, "sort%d_ok", uid);
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    %s\n", dedup ? "iconst_1" : "iconst_0");
            J("    invokestatic %s/pj_sort_list(Ljava/lang/Object;I)Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* succ(?X, ?Y) — Y = X+1 or X = Y-1 */
        if (strcmp(fn, "succ") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_succ_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* plus(?X, ?Y, ?Z) — Z = X+Y */
        if (strcmp(fn, "plus") == 0 && nargs == 3) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_plus_3(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* format/1: format(Fmt) — no args */
        if (strcmp(fn, "format") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            /* push nil as arglist */
            JI("ldc", "\"[]\"");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_format_2(Ljava/lang/Object;Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* format/2: format(Fmt, Args) */
        if (strcmp(fn, "format") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_format_2(Ljava/lang/Object;Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* numbervars/3: numbervars(+Term, +Start, -End) */
        if (strcmp(fn, "numbervars") == 0 && nargs == 3) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_numbervars_3(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* char_type/2: char_type(+Char, +Type) */
        if (strcmp(fn, "char_type") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_char_type_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* writeq/1 */
        if (strcmp(fn, "writeq") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_writeq(Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* write_canonical/1 */
        if (strcmp(fn, "write_canonical") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_write_canonical(Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* print/1 — same as write/1 */
        if (strcmp(fn, "print") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_write(Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
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

        /* string_chars(+Str, ?Chars) — identical semantics to atom_chars on JVM */
        if (strcmp(fn, "string_chars") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_atom_chars_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* string_codes(+Str, ?Codes) — identical semantics to atom_codes on JVM */
        if (strcmp(fn, "string_codes") == 0 && nargs == 2) {
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

        /* atom_string(?Atom, ?Str) — bidirectional via helper */
        if (strcmp(fn, "atom_string") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_atom_string_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* number_string(?Num, ?Str) — bidirectional via helper */
        if (strcmp(fn, "number_string") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_number_string_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* string_concat(+A, +B, ?C) — identical to atom_concat */
        if (strcmp(fn, "string_concat") == 0 && nargs == 3) {
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

        /* string_length(+Str, ?Len) — identical to atom_length */
        if (strcmp(fn, "string_length") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokevirtual java/lang/String/length()I\n");
            J("    i2l\n");
            J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* string_upper(+Str, ?Upper) — identical to upcase_atom */
        if (strcmp(fn, "string_upper") == 0 && nargs == 2) {
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

        /* string_lower(+Str, ?Lower) — identical to downcase_atom */
        if (strcmp(fn, "string_lower") == 0 && nargs == 2) {
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

        /* term_to_atom(+Term, ?Atom) — forward: serialize term → atom string */
        if (strcmp(fn, "term_to_atom") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_term_to_atom_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* term_string(+Term, ?Str) — identical to term_to_atom */
        if (strcmp(fn, "term_string") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_term_to_atom_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* garbage_collect/0 — no-op on JVM (GC is automatic) */
        if (strcmp(fn, "garbage_collect") == 0 && nargs == 0) {
            JI("goto", lbl_γ);
            return;
        }

        /* trim_stacks/0 — no-op on JVM */
        if (strcmp(fn, "trim_stacks") == 0 && nargs == 0) {
            JI("goto", lbl_γ);
            return;
        }

        /* ?=(X, Y) — can-compare: succeed if X and Y are ground (contain no vars)
         * or are identical. Fails if either contains unbound variables. */
        if (strcmp(fn, "?=") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_can_compare(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* copy_term(+Original, ?Copy) — deep copy with fresh variables */
        if (strcmp(fn, "copy_term") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_copy_term(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* string_to_atom(?Str, ?Atom) — bidirectional alias for atom_string */
        if (strcmp(fn, "string_to_atom") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_atom_string_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* atomic_list_concat(+List, ?Atom) — concat list with no separator */
        if (strcmp(fn, "atomic_list_concat") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    ldc \"\"\n");
            J("    invokestatic %s/pj_alc_sep(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* atomic_list_concat(+List, +Sep, ?Atom) — concat with separator */
        if (strcmp(fn, "atomic_list_concat") == 0 && nargs == 3) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    invokestatic %s/pj_atom_name(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    invokestatic %s/pj_alc_sep(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[2], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* concat_atom(+List, ?Atom) — SWI alias for atomic_list_concat/2 */
        if (strcmp(fn, "concat_atom") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    ldc \"\"\n");
            J("    invokestatic %s/pj_alc_sep(Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/String;\n", pj_classname);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* nb_setval(+Name, +Value) — destructive global store */
        if (strcmp(fn, "nb_setval") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_nb_setval_2(Ljava/lang/Object;Ljava/lang/Object;)V\n", pj_classname);
            JI("goto", lbl_γ);
            return;
        }
        /* nb_getval(+Name, -Value) — read global store */
        if (strcmp(fn, "nb_getval") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_nb_getval_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* succ_or_zero(+X, ?Y) — Y = max(0, X-1) */
        if (strcmp(fn, "succ_or_zero") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_succ_or_zero_2(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* aggregate_all(+Template, +Goal, -Result)
         * Supported templates: count, sum(E), max(E), min(E), bag(T), set(T).
         * Dispatched as a synthetic predicate p_aggregate_all_3 (like findall). */
        if (strcmp(fn, "aggregate_all") == 0 && nargs == 3) {
            int agg_rv = (*next_local)++;
            char desc[512];
            snprintf(desc, sizeof desc,
                     "%s/p_aggregate_all_3([Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;",
                     pj_classname);
            for (int i = 0; i < 3; i++)
                pj_emit_term(goal->children[i], var_locals, n_vars);
            JI("iconst_0", "");
            J("    invokestatic %s\n", desc);
            J("    astore %d\n", agg_rv);
            J("    aload %d\n", agg_rv);
            J("    ifnull %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }

        /* M-PJ-EXCEPTIONS -------------------------------------------------- */

        /* throw/1 — store term in pj_throw_term, throw RuntimeException("PROLOG_THROW") */
        if (strcmp(fn, "throw") == 0 && nargs == 1) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
            J("    putstatic %s/pj_throw_term Ljava/lang/Object;\n", pj_classname);
            J("    new java/lang/RuntimeException\n");
            J("    dup\n");
            J("    ldc \"PROLOG_THROW\"\n");
            J("    invokespecial java/lang/RuntimeException/<init>(Ljava/lang/String;)V\n");
            J("    athrow\n");
            /* unreachable, but lbl_ω is still valid target for verifier */
            return;
        }

        /* catch/3 — catch(Goal, Catcher, Recovery)
         * Wrap Goal in a .catch block.  On RuntimeException("PROLOG_THROW"),
         * attempt to unify pj_throw_term with Catcher.  If unification
         * succeeds run Recovery (→ lbl_γ), otherwise re-throw.
         * If no exception, Goal success → lbl_γ, Goal failure → lbl_ω. */
        if (strcmp(fn, "catch") == 0 && nargs == 3) {
            static int catch_ctr = 0;
            int cid = catch_ctr++;
            char lbl_try_start[64], lbl_try_end[64], lbl_catch_h[64];
            char lbl_match_ok[64], lbl_rethrow[64], lbl_goal_ok[64];
            snprintf(lbl_try_start, sizeof lbl_try_start, "pj_catch%d_try_start", cid);
            snprintf(lbl_try_end,   sizeof lbl_try_end,   "pj_catch%d_try_end",   cid);
            snprintf(lbl_catch_h,   sizeof lbl_catch_h,   "pj_catch%d_handler",   cid);
            snprintf(lbl_match_ok,  sizeof lbl_match_ok,  "pj_catch%d_match_ok",  cid);
            snprintf(lbl_rethrow,   sizeof lbl_rethrow,   "pj_catch%d_rethrow",   cid);
            snprintf(lbl_goal_ok,   sizeof lbl_goal_ok,   "pj_catch%d_goal_ok",   cid);

            int exc_local = (*next_local)++;   /* stores the caught exception */

            /* Emit .catch directive before the try block */
            J("    .catch java/lang/RuntimeException from %s to %s using %s\n",
              lbl_try_start, lbl_try_end, lbl_catch_h);

            /* --- try block: emit Goal --- */
            J("%s:\n", lbl_try_start);
            /* Inline the goal; on success fall through to lbl_goal_ok */
            pj_emit_goal(goal->children[0], lbl_goal_ok, lbl_ω,
                         trail_local, var_locals, n_vars,
                         cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
            J("%s:\n", lbl_try_end);
            J("    goto %s\n", lbl_ω);   /* goal failed without exception → fail */

            /* goal_ok: goal succeeded without exception */
            J("%s:\n", lbl_goal_ok);
            JI("goto", lbl_γ);

            /* --- exception handler --- */
            J("%s:\n", lbl_catch_h);
            J("    astore %d\n", exc_local);  /* save exception */
            /* Is it a Prolog throw? Check getMessage() == "PROLOG_THROW" */
            J("    aload %d\n", exc_local);
            J("    invokevirtual java/lang/Throwable/getMessage()Ljava/lang/String;\n");
            J("    ldc \"PROLOG_THROW\"\n");
            J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
            J("    ifeq %s\n", lbl_rethrow);  /* not a Prolog throw → re-throw */

            /* It is a Prolog throw — try to unify pj_throw_term with Catcher */
            J("    getstatic %s/pj_throw_term Ljava/lang/Object;\n", pj_classname);
            pj_emit_term(goal->children[1], var_locals, n_vars);  /* Catcher */
            J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_rethrow);   /* unification failed → re-throw */

            /* Catcher matched — run Recovery */
            J("    goto %s\n", lbl_match_ok);

            J("%s:\n", lbl_rethrow);
            J("    aload %d\n", exc_local);
            J("    athrow\n");

            J("%s:\n", lbl_match_ok);
            /* Emit Recovery goal */
            pj_emit_goal(goal->children[2], lbl_γ, lbl_ω,
                         trail_local, var_locals, n_vars,
                         cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
            return;
        }

        /* END M-PJ-EXCEPTIONS ---------------------------------------------- */

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
                /* stack: [cast-ref, bool] - swap+pop to get bool alone, stack clean */
                JI("swap", ""); JI("pop", "");
                J("    ifeq %s\n", vfail);   /* not var tag → fail; stack empty */
                /* tag=="var": re-deref and check [1]==null */
                pj_emit_term(goal->children[0], var_locals, n_vars);
                J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
                JI("checkcast", "[Ljava/lang/Object;");
                JI("iconst_1", ""); JI("aaload", "");
                J("    ifnonnull %s\n", vfail); /* bound → fail; stack empty */
                JI("goto", lbl_γ);
                /* vok: reached via ifnull (which popped dup'd copy); original ref still on stack → pop it */
                J("%s:\n", vok);   JI("pop", ""); JI("goto", lbl_γ);
                /* vfail: reached via ifeq/ifnonnull (which already popped); stack clean */
                J("%s:\n", vfail); JI("goto", lbl_ω);
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
                /* stack: [cast-ref, bool] - swap+pop to get bool alone */
                JI("swap", ""); JI("pop", "");
                J("    ifeq %s\n", nok);   /* not var tag → nonvar → succeed; stack empty */
                /* tag=="var": re-deref, check if [1]!=null (bound ref = nonvar) */
                pj_emit_term(goal->children[0], var_locals, n_vars);
                J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
                JI("checkcast", "[Ljava/lang/Object;");
                JI("iconst_1", ""); JI("aaload", "");
                J("    ifnonnull %s\n", nok); /* bound ref → succeed; stack empty */
                /* unbound var → fail */
                JI("goto", lbl_ω);
                J("%s:\n", nok);   JI("goto", lbl_γ);
                J("%s:\n", nfail); JI("goto", lbl_ω);
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
        /* =../2: Term =.. List (univ) — bidirectional via pj_univ helper.
         * Decompose: Term bound   -> list = pj_term_to_list(Term); unify list.
         * Compose:   Term unbound -> term = pj_list_to_term(List); unify term.
         * Both directions handled at runtime by pj_univ(term, list) -> bool. */
        if (strcmp(fn, "=..") == 0 && nargs == 2) {
            pj_emit_term(goal->children[0], var_locals, n_vars);
            pj_emit_term(goal->children[1], var_locals, n_vars);
            J("    invokestatic %s/pj_univ(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
            J("    ifeq %s\n", lbl_ω);
            JI("goto", lbl_γ);
            return;
        }
        /* phrase/2 — phrase(NT, List) -> NT(List, [])\n         * phrase/3 — phrase(NT, List, Rest) -> NT(List, Rest)\n         * Rewrite into a direct NT/+2 call so the normal ucall retry loop\n         * handles backtracking correctly. */
        if (strcmp(fn, "phrase") == 0 && (nargs == 2 || nargs == 3)) {
            EXPR_t *nt_expr  = goal->children[0];
            EXPR_t *list_arg = goal->children[1];
            EXPR_t *rest_arg = (nargs == 3) ? goal->children[2] : NULL;

            /* Variable NT: phrase(Var, List[, Rest]) — NT is unbound/runtime-determined.
             * Build a phrase/N term and dispatch via pj_call_goal at runtime. */
            if (nt_expr->kind == E_VART) {
                int n_phrase = nargs; /* 2 or 3 */
                /* build phrase(NT, List[, Rest]) as a compound term on stack */
                J("    ldc %d\n", n_phrase + 2);
                J("    anewarray java/lang/Object\n");
                J("    dup\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
                J("    dup\n"); J("    iconst_1\n"); J("    ldc \"phrase\"\n");   J("    aastore\n");
                J("    dup\n"); J("    iconst_2\n");
                pj_emit_term(nt_expr, var_locals, n_vars);
                J("    aastore\n");
                J("    dup\n"); J("    iconst_3\n");
                pj_emit_term(list_arg, var_locals, n_vars);
                J("    aastore\n");
                if (rest_arg) {
                    J("    dup\n"); J("    bipush 4\n");
                    pj_emit_term(rest_arg, var_locals, n_vars);
                    J("    aastore\n");
                }
                J("    iconst_0\n");
                J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
                J("    ldc -1\n");
                J("    if_icmpeq %s\n", lbl_ω);
                J("    goto %s\n", lbl_γ);
                return;
            }

            /* Special case: NT = [] (empty list body).
             * phrase([], List)       -> List = []
             * phrase([], List, Rest) -> List = Rest */
            int nt_is_nil = (nt_expr->sval && strcmp(nt_expr->sval, "[]") == 0 &&
                             nt_expr->nchildren == 0);
            if (nt_is_nil) {
                EXPR_t *rhs;
                if (rest_arg) {
                    rhs = rest_arg;
                } else {
                    rhs = expr_new(E_QLIT);
                    rhs->sval = strdup("[]");
                }
                /* emit: pj_unify(list_arg, rhs); ifeq omega; goto gamma */
                pj_emit_term(list_arg, var_locals, n_vars);
                pj_emit_term(rhs, var_locals, n_vars);
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", lbl_ω);
                J("    goto %s\n", lbl_γ);
                return;
            }

            /* Special case: NT = '.'(H, T) — list terminal body.
             * phrase([H|T], L0)       -> L0 = [H|L1], phrase(T, L1)
             * phrase([H|T], L0, L)    -> L0 = [H|L1], phrase(T, L1, L)
             * Allocate a fresh local for L1, extend var_locals so pj_emit_term
             * can address it via an E_VART at index n_vars, then recurse. */
            int nt_is_cons = (nt_expr->sval && strcmp(nt_expr->sval, ".") == 0 &&
                              nt_expr->nchildren == 2);
            if (nt_is_cons) {
                EXPR_t *head_elem = nt_expr->children[0];
                EXPR_t *tail_nt   = nt_expr->children[1];
                int l1_local = *next_local; *next_local += 1;
                J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", l1_local);
                /* emit: L0 = [H | L1] */
                pj_emit_term(list_arg, var_locals, n_vars);
                J("    ldc 4\n"); J("    anewarray java/lang/Object\n");
                J("    dup\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
                J("    dup\n"); J("    iconst_1\n"); J("    ldc \".\"\n");       J("    aastore\n");
                J("    dup\n"); J("    iconst_2\n");
                pj_emit_term(head_elem, var_locals, n_vars);
                J("    aastore\n");
                J("    dup\n"); J("    iconst_3\n"); J("    aload %d\n", l1_local); J("    aastore\n");
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", lbl_ω);
                /* extend var_locals: index n_vars → l1_local */
                int *vl2 = malloc((n_vars + 1) * sizeof(int));
                for (int i = 0; i < n_vars; i++) vl2[i] = var_locals ? var_locals[i] : 0;
                vl2[n_vars] = l1_local;
                /* build E_VART for L1 at index n_vars */
                EXPR_t *l1_vart = expr_new(E_VART); l1_vart->ival = n_vars;
                /* build phrase(tail_nt, L1 [, rest_arg]) goal */
                EXPR_t *rec = expr_new(E_FNC);
                rec->sval = strdup("phrase");
                rec->nchildren = rest_arg ? 3 : 2;
                rec->children = malloc(rec->nchildren * sizeof(EXPR_t *));
                rec->children[0] = tail_nt;
                rec->children[1] = l1_vart;
                if (rest_arg) rec->children[2] = rest_arg;
                pj_emit_goal(rec, lbl_γ, lbl_ω,
                             trail_local, vl2, n_vars + 1,
                             cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
                free(vl2);
                return;
            }

            /* General NT (atom or compound, non-list): phrase(NT, List[, Rest]) -> NT/+2 call */
            {
                EXPR_t *nil_node = NULL;
                if (!rest_arg) {
                    nil_node = expr_new(E_QLIT);
                    nil_node->sval = strdup("[]");
                }
                EXPR_t *call = expr_new(E_FNC);
                int base = (nt_expr->kind == E_FNC) ? nt_expr->nchildren : 0;
                call->sval = strdup(nt_expr->sval ? nt_expr->sval : "unknown");
                call->nchildren = base + 2;
                call->children  = malloc(call->nchildren * sizeof(EXPR_t *));
                for (int i = 0; i < base; i++) call->children[i] = nt_expr->children[i];
                call->children[base]   = list_arg;
                call->children[base+1] = rest_arg ? rest_arg : nil_node;
                pj_emit_goal(call, lbl_γ, lbl_ω,
                             trail_local, var_locals, n_vars,
                             cut_cs_seal, cs_local_for_cut, next_local, lbl_cutγ);
                return;
            }
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

    /* E_VART: variable used as goal — dispatch via pj_call_goal at runtime.
     * This handles catch(Goal,...) and call(Var) where Goal/Var is a variable. */
    if (goal->kind == E_VART) {
        int slot = goal->ival;
        if (slot >= 0 && slot < n_vars) {
            J("    aload %d\n", var_locals[slot]);
        } else {
            /* shouldn't happen — emit atom 'fail' as safe fallback */
            J("    ldc \"fail\"\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
        }
        J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
        J("    iconst_0\n");
        J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
        /* pj_call_goal returns -1 on failure, >=0 (next cs) on success */
        J("    ldc -1\n");
        J("    if_icmpeq %s\n", lbl_ω);
        JI("goto", lbl_γ);
        return;
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
        /* PJ-72: phrase/N rewrite in ucall path.
         * phrase/2,3 is recognised as a user call by pj_is_user_call, so it
         * reaches here before pj_emit_goal's phrase handler fires.  Rewrite
         * phrase(NT, List) -> NT(List, []) and phrase(NT, List, Rest) ->
         * NT(List, Rest) so the retry loop below operates on the real NT. */
        if (g->sval && strcmp(g->sval, "phrase") == 0 &&
            (g->nchildren == 2 || g->nchildren == 3)) {
            EXPR_t *nt_expr  = g->children[0];
            EXPR_t *list_arg = g->children[1];
            EXPR_t *rest_arg = (g->nchildren == 3) ? g->children[2] : NULL;

            /* Variable NT: dispatch at runtime via pj_call_goal */
            if (nt_expr->kind == E_VART) {
                int np = g->nchildren;
                J("    ldc %d\n", np + 2);
                J("    anewarray java/lang/Object\n");
                J("    dup\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
                J("    dup\n"); J("    iconst_1\n"); J("    ldc \"phrase\"\n");   J("    aastore\n");
                J("    dup\n"); J("    iconst_2\n");
                pj_emit_term(nt_expr, var_locals, n_vars);
                J("    aastore\n");
                J("    dup\n"); J("    iconst_3\n");
                pj_emit_term(list_arg, var_locals, n_vars);
                J("    aastore\n");
                if (rest_arg) {
                    J("    dup\n"); J("    bipush 4\n");
                    pj_emit_term(rest_arg, var_locals, n_vars);
                    J("    aastore\n");
                }
                J("    iconst_0\n");
                J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
                J("    ldc -1\n"); J("    if_icmpeq %s\n", lbl_ω);
                pj_emit_body(goals + 1, ngoals - 1, lbl_γ, lbl_ω, lbl_outer_ω,
                             trail_local, var_locals, n_vars, next_local,
                             init_cs_local, sub_cs_out_local,
                             cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
                return;
            }

            /* Special case: NT = [] — phrase([], List) -> List = []
             *                        phrase([], List, Rest) -> List = Rest */
            int nt_is_nil = (nt_expr->sval && strcmp(nt_expr->sval, "[]") == 0 &&
                             nt_expr->nchildren == 0);
            if (nt_is_nil) {
                EXPR_t *rhs;
                if (rest_arg) {
                    rhs = rest_arg;
                } else {
                    rhs = expr_new(E_QLIT);
                    rhs->sval = strdup("[]");
                }
                pj_emit_term(list_arg, var_locals, n_vars);
                pj_emit_term(rhs, var_locals, n_vars);
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", lbl_ω);
                pj_emit_body(goals + 1, ngoals - 1, lbl_γ, lbl_ω, lbl_outer_ω,
                             trail_local, var_locals, n_vars, next_local,
                             init_cs_local, sub_cs_out_local,
                             cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
                return;
            }

            EXPR_t *nil_node = NULL;
            if (!rest_arg) {
                nil_node = expr_new(E_QLIT);
                nil_node->sval = strdup("[]");
            }

            /* Special case: NT = '.'(H, T) — list terminal.
             * Allocate L1 local, extend var_locals, recurse via pj_emit_body. */
            int nt_is_cons2 = (nt_expr->sval && strcmp(nt_expr->sval, ".") == 0 &&
                               nt_expr->nchildren == 2);
            if (nt_is_cons2) {
                EXPR_t *hd2 = nt_expr->children[0];
                EXPR_t *tl2 = nt_expr->children[1];
                int l1_loc = *next_local; *next_local += 1;
                J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", l1_loc);
                pj_emit_term(list_arg, var_locals, n_vars);
                J("    ldc 4\n"); J("    anewarray java/lang/Object\n");
                J("    dup\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
                J("    dup\n"); J("    iconst_1\n"); J("    ldc \".\"\n");       J("    aastore\n");
                J("    dup\n"); J("    iconst_2\n"); pj_emit_term(hd2, var_locals, n_vars); J("    aastore\n");
                J("    dup\n"); J("    iconst_3\n"); J("    aload %d\n", l1_loc); J("    aastore\n");
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", lbl_ω);
                /* extend var_locals: new index n_vars → l1_loc */
                int *vl2b = malloc((n_vars + 1) * sizeof(int));
                for (int i = 0; i < n_vars; i++) vl2b[i] = var_locals ? var_locals[i] : 0;
                vl2b[n_vars] = l1_loc;
                EXPR_t *l1v2 = expr_new(E_VART); l1v2->ival = n_vars;
                EXPR_t *rec2 = expr_new(E_FNC);
                rec2->sval = strdup("phrase");
                rec2->nchildren = rest_arg ? 3 : 2;
                rec2->children = malloc(rec2->nchildren * sizeof(EXPR_t *));
                rec2->children[0] = tl2; rec2->children[1] = l1v2;
                if (rest_arg) rec2->children[2] = rest_arg;
                /* Prepend rec2 to remaining goals */
                int ngoals2 = ngoals; /* total remaining including current phrase */
                EXPR_t **goals2 = malloc((ngoals2) * sizeof(EXPR_t *));
                goals2[0] = rec2;
                for (int gi = 1; gi < ngoals2; gi++) goals2[gi] = goals[gi]; /* goals[0]=current phrase, rest follow */
                pj_emit_body(goals2, ngoals2, lbl_γ, lbl_ω, lbl_outer_ω,
                             trail_local, vl2b, n_vars + 1, next_local,
                             init_cs_local, sub_cs_out_local,
                             cut_cs_seal, cs_local_for_cut, lbl_pred_ω, lbl_cutγ);
                free(goals2); free(vl2b);
                return;
            }

            EXPR_t *call = expr_new(E_FNC);
            int base = (nt_expr->kind == E_FNC) ? nt_expr->nchildren : 0;
            call->sval      = strdup(nt_expr->sval ? nt_expr->sval : "unknown");
            call->nchildren = base + 2;
            call->children  = malloc(call->nchildren * sizeof(EXPR_t *));
            for (int i = 0; i < base; i++) call->children[i] = nt_expr->children[i];
            call->children[base]   = list_arg;
            call->children[base+1] = rest_arg ? rest_arg : nil_node;
            g = call;
        }
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
            /* M-PJ-CUT-SCOPE: A cut inside a *called* predicate is scoped to
             * that predicate only and must NOT propagate into the caller.
             * Clamp MAX_VALUE return to 1 (deterministic success) so the
             * caller's gamma body continues normally. */
            char clamp_done[128];
            snprintf(clamp_done, sizeof clamp_done, "clamp_done_%d", uid);
            J("    aload %d\n", local_rv);
            JI("iconst_0", "");
            JI("aaload", "");
            JI("checkcast", "java/lang/Integer");
            JI("invokevirtual", "java/lang/Integer/intValue()I");
            J("    ldc 2147483647\n");
            J("    if_icmpne %s\n", clamp_done);
            J("    aload %d\n", local_rv);
            J("    iconst_0\n");
            J("    iconst_1\n");
            J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
            J("    aastore\n");
            J("%s:\n", clamp_done);
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
 * pj_emit_reverse_builtin — emit synthetic p_reverse_2 method.
 * Signature: p_reverse_2([List], [Result], cs) → Object[] | null
 * Deterministic: reverses the Prolog list in List and unifies with Result.
 * cs > 0 → always fail (no retry).
 * ------------------------------------------------------------------------- */
static void pj_emit_reverse_builtin(void) {
    J("; === reverse/2 synthetic predicate ========================================\n");
    J(".method static p_reverse_2([Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 8\n");
    /* cs > 0 → already used, fail */
    J("    iload_2\n");
    J("    ifne p_reverse_2_fail\n");
    /* Walk list into ArrayList */
    J("    new java/util/ArrayList\n");
    J("    dup\n");
    J("    invokespecial java/util/ArrayList/<init>()V\n");
    J("    astore_3\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore 4\n");
    J("p_rev_walk:\n");
    J("    aload 4\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_1\n");
    J("    aaload\n");
    J("    ldc \"[]\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne p_rev_build\n");
    /* head = list[2] */
    J("    aload_3\n");
    J("    aload 4\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_2\n");
    J("    aaload\n");
    J("    invokevirtual java/util/ArrayList/add(Ljava/lang/Object;)Z\n");
    J("    pop\n");
    /* tail = list[3], deref, loop */
    J("    aload 4\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_3\n");
    J("    aaload\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore 4\n");
    J("    goto p_rev_walk\n");
    /* Build reversed Prolog list: prepend each element front-to-back.
     * Walking i=0..size-1 and prepending gives [arr[N-1],...,arr[0]] = reversed. */
    J("p_rev_build:\n");
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 5\n");     /* accumulator, starts as [] */
    J("    iconst_0\n");
    J("    istore 6\n");     /* i = 0, iterate forward */
    J("p_rev_loop:\n");
    J("    iload 6\n");
    J("    aload_3\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    if_icmpge p_rev_unify\n");
    J("    bipush 4\n");
    J("    anewarray java/lang/Object\n");
    J("    dup\n");
    J("    iconst_0\n");
    J("    ldc \"compound\"\n");
    J("    aastore\n");
    J("    dup\n");
    J("    iconst_1\n");
    J("    ldc \".\"\n");
    J("    aastore\n");
    J("    dup\n");
    J("    iconst_2\n");
    J("    aload_3\n");
    J("    iload 6\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    aastore\n");
    J("    dup\n");
    J("    iconst_3\n");
    J("    aload 5\n");
    J("    aastore\n");
    J("    astore 5\n");
    J("    iinc 6 1\n");
    J("    goto p_rev_loop\n");
    /* Unify result */
    J("p_rev_unify:\n");
    J("    aload 5\n");
    J("    aload_1\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq p_reverse_2_fail\n");
    J("    iconst_1\n");
    J("    anewarray java/lang/Object\n");
    J("    dup\n");
    J("    iconst_0\n");
    J("    iconst_1\n");
    J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
    J("    aastore\n");
    J("    areturn\n");
    J("p_reverse_2_fail:\n");
    J("    aconst_null\n");
    J("    areturn\n");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * pj_emit_forall_builtin — emit synthetic p_forall_2 method.
 * forall(Cond, Action): for each solution of Cond call Action; succeed iff
 * all Actions succeed (vacuously true if Cond has no solutions).
 * Signature: p_forall_2([Cond], [Action], cs) → Object[] | null
 * cs is ignored (forall always deterministic); returns singleton {0} on success.
 * Uses pj_call_goal to drive Cond with backtracking and call Action for each.
 * ------------------------------------------------------------------------- */
static void pj_emit_forall_builtin(void) {
    J("; === forall/2 synthetic predicate =========================================\n");
    J(".method static p_forall_2([Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 6\n");
    /* locals: 0=Cond[], 1=Action[], 2=cs(ignored), 3=cs_cond(int), 4=r(int) */
    /* deref Cond */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    iconst_0\n");
    J("    istore 3\n");             /* cs_cond = 0 */
    J("p_forall_2_loop:\n");
    /* call Cond with cs_cond */
    J("    dup\n");                  /* Cond still on stack */
    J("    iload 3\n");
    J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
    J("    istore 4\n");
    J("    iload 4\n");
    J("    ldc -1\n");
    J("    if_icmpeq p_forall_2_done\n");  /* Cond exhausted → success */
    J("    iload 4\n");
    J("    istore 3\n");             /* cs_cond = next cs */
    /* call Action */
    J("    aload_1\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    iconst_0\n");
    J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
    J("    ldc -1\n");
    J("    if_icmpeq p_forall_2_fail\n");  /* Action failed → fail */
    J("    goto p_forall_2_loop\n");
    J("p_forall_2_done:\n");
    J("    pop\n");                  /* pop Cond ref */
    J("    iconst_1\n");
    J("    anewarray java/lang/Object\n");
    J("    dup\n");
    J("    iconst_0\n");
    J("    iconst_0\n");
    J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
    J("    aastore\n");
    J("    areturn\n");
    J("p_forall_2_fail:\n");
    J("    pop\n");                  /* pop Cond ref */
    J("    aconst_null\n");
    J("    areturn\n");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * pj_emit_between_builtin — emit synthetic p_between_3 method.
 * Signature: p_between_3([Low], [High], [Var], cs) -> Object[] | null
 * cs = offset from Low: try value Low+cs each invocation.
 * Returns {cs+1} on success so caller retries with the next value.
 * ------------------------------------------------------------------------- */
static void pj_emit_between_builtin(void) {
    J("; === between/3 synthetic predicate ========================================\n");
    J(".method static p_between_3([Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 10\n");
    /* locals: 0=Low,1=High,2=Var,3=cs, 4-5=low(long),6-7=high(long),8-9=cur(long) */

    /* Fast path: if cs==0 AND Var is already bound (tag!="var"), do range check */
    J("    iload_3\n"); J("    ifne p_between_3_generate\n");  /* cs!=0 → generate mode */
    J("    aload_2\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_0", ""); JI("aaload", "");     /* tag */
    JI("ldc", "\"var\"");
    JI("invokevirtual", "java/lang/Object/equals(Ljava/lang/Object;)Z");
    J("    ifne p_between_3_generate\n");     /* var is unbound → generate mode */

    /* Bound var fast path: check Low =< Var =< High */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;"); JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String"); JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    J("    lstore 4\n");   /* low */
    J("    aload_1\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;"); JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String"); JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    J("    lstore 6\n");   /* high */
    J("    aload_2\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;"); JI("iconst_1", ""); JI("aaload", "");
    JI("checkcast", "java/lang/String"); JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    J("    lstore 8\n");   /* var_val */
    /* check low <= var_val */
    J("    lload 4\n"); J("    lload 8\n"); JI("lcmp", ""); J("    ifgt p_between_3_fail\n");
    /* check var_val <= high */
    J("    lload 8\n"); J("    lload 6\n"); JI("lcmp", ""); J("    ifgt p_between_3_fail\n");
    /* succeed deterministically: return {MAX_INT} to signal no retry needed */
    JI("iconst_1", ""); JI("anewarray", "java/lang/Object");
    JI("dup", ""); JI("iconst_0", "");
    JI("ldc", "2147483647");
    JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
    JI("aastore", "");
    JI("areturn", "");

    /* Generate mode: iterate cur = Low + cs */
    J("p_between_3_generate:\n");
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
    J("    lstore 8\n");                /* cur = Low + cs */
    J("    aload_1\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    JI("checkcast", "[Ljava/lang/Object;");
    JI("iconst_1", "");
    JI("aaload", "");
    JI("checkcast", "java/lang/String");
    JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
    J("    lstore 6\n");                /* high */
    J("    lload 8\n");
    J("    lload 6\n");
    JI("lcmp", "");
    J("    ifgt p_between_3_fail\n");   /* cur > high → fail */
    J("    lload 8\n");
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
 * pj_emit_aggregate_all_builtin — emit synthetic p_aggregate_all_3 method.
 *
 * aggregate_all(+Template, +Goal, -Result)
 * Supported templates:
 *   count       → Result = N (integer count of solutions)
 *   sum(Expr)   → Result = sum of Expr over solutions
 *   max(Expr)   → Result = max of Expr over solutions (fail if 0 solutions)
 *   min(Expr)   → Result = min of Expr over solutions (fail if 0 solutions)
 *   bag(T)      → Result = list of T  (alias for findall)
 *   set(T)      → Result = sorted deduped list of T
 *
 * Strategy: run the same pj_call_goal loop as findall, collecting solutions
 * into an ArrayList.  After loop, reduce according to Template tag.
 *
 * Locals: 0=tmpl 1=goal 2=result_var 3=cs(unused)
 *         4=acc(ArrayList) 5=trail_mark 6=goal_cs
 *         7=tag(String) 8=count(I) 9=tmp 10=i 11=sum(long lo) 12=sum hi
 *         13=best(long lo) 14=best hi 15=has_val(I)
 * ------------------------------------------------------------------------- */
static void pj_emit_aggregate_all_builtin(void) {
    J("; === aggregate_all/3 synthetic predicate ============================\n");
    J(".method static p_aggregate_all_3([Ljava/lang/Object;[Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n");
    J("    .limit stack 20\n");
    J("    .limit locals 20\n");

    /* deref template to get tag string */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_0\n");

    /* acc = new ArrayList */
    J("    new java/util/ArrayList\n");
    J("    dup\n");
    J("    invokespecial java/util/ArrayList/<init>()V\n");
    J("    astore 4\n");

    /* trail mark */
    J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
    J("    istore 5\n");

    /* goal_cs = 0 */
    J("    iconst_0\n"); J("    istore 6\n");

    /* --- solution loop (same as findall) --- */
    J("pj_agg_loop:\n");
    J("    aload_1\n");
    J("    iload 6\n");
    J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
    J("    istore 6\n");
    J("    iload 6\n");
    J("    ldc -1\n");
    J("    if_icmpeq pj_agg_done\n");
    /* copy template and accumulate */
    J("    aload_0\n");
    J("    invokestatic %s/pj_copy_term(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore 9\n");
    J("    aload 4\n");
    J("    aload 9\n");
    J("    invokevirtual java/util/ArrayList/add(Ljava/lang/Object;)Z\n");
    J("    pop\n");
    /* unwind trail */
    J("    iload 5\n");
    J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
    J("    goto pj_agg_loop\n");

    J("pj_agg_done:\n");
    J("    iload 5\n");
    J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);

    /* --- determine template functor name ---
     * atom term:     tmpl = ["atom", name]        → functor = tmpl[1]
     * compound term: tmpl = ["compound", name, …] → functor = tmpl[1]
     * Either way, tmpl[1] gives us the functor name. */
    J("    aload_0\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_1\n");
    J("    aaload\n");
    J("    invokevirtual java/lang/Object/toString()Ljava/lang/String;\n");
    J("    astore 7\n");  /* functor name */

    /* --- dispatch on tag --- */

    /* count */
    J("    aload 7\n");
    J("    ldc \"count\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_agg_not_count\n");
    /* Result = size of acc */
    J("    aload 4\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    i2l\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_2\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_agg_fail\n");
    J("    goto pj_agg_succeed\n");

    J("pj_agg_not_count:\n");

    /* bag(T) — build list of T values */
    J("    aload 7\n");
    J("    ldc \"bag\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_agg_build_list\n");
    /* set(T) — sorted deduped list */
    J("    aload 7\n");
    J("    ldc \"set\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_agg_build_sorted_list\n");
    /* not bag or set — fall through to sum/max/min */
    J("    goto pj_agg_not_bag\n");

    J("pj_agg_build_list:\n");
    /* Build Prolog list from acc — each element is bag(T), extract T at index [2] */
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 10\n");  /* tail = [] */
    J("    aload 4\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    iconst_1\n"); J("    isub\n"); J("    istore 11\n"); /* i */
    J("pj_agg_bag_loop:\n");
    J("    iload 11\n"); J("    iflt pj_agg_bag_done\n");
    /* elem = acc[i][2] (the T in bag(T)) */
    J("    aload 4\n"); J("    iload 11\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_2\n"); J("    aaload\n");  /* T value */
    J("    astore 12\n");
    /* cons = [compound, ".", T, tail] */
    J("    bipush 4\n"); J("    anewarray java/lang/Object\n"); J("    astore 13\n");
    J("    aload 13\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
    J("    aload 13\n"); J("    iconst_1\n"); J("    ldc \".\"\n"); J("    aastore\n");
    J("    aload 13\n"); J("    iconst_2\n"); J("    aload 12\n"); J("    aastore\n");
    J("    aload 13\n"); J("    iconst_3\n"); J("    aload 10\n"); J("    aastore\n");
    J("    aload 13\n"); J("    astore 10\n");
    J("    iinc 11 -1\n");
    J("    goto pj_agg_bag_loop\n");
    J("pj_agg_bag_done:\n");
    J("    aload_2\n"); J("    aload 10\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_agg_fail\n");
    J("    goto pj_agg_succeed\n");

    J("pj_agg_build_sorted_list:\n");
    /* Build list first, sort it (dedup=1), unify with result_var */
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 10\n");
    J("    aload 4\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    iconst_1\n"); J("    isub\n"); J("    istore 11\n");
    J("pj_agg_set_bld_loop:\n");
    J("    iload 11\n"); J("    iflt pj_agg_set_bld_done\n");
    J("    bipush 4\n"); J("    anewarray java/lang/Object\n"); J("    astore 12\n");
    J("    aload 12\n"); J("    iconst_0\n"); J("    ldc \"compound\"\n"); J("    aastore\n");
    J("    aload 12\n"); J("    iconst_1\n"); J("    ldc \".\"\n"); J("    aastore\n");
    J("    aload 12\n"); J("    iconst_2\n");
    J("    aload 4\n"); J("    iload 11\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    aastore\n");
    J("    aload 12\n"); J("    iconst_3\n"); J("    aload 10\n"); J("    aastore\n");
    J("    aload 12\n"); J("    astore 10\n");
    J("    iinc 11 -1\n");
    J("    goto pj_agg_set_bld_loop\n");
    J("pj_agg_set_bld_done:\n");
    /* sort with dedup=1 via pj_sort_list */
    J("    aload 10\n");
    J("    iconst_1\n");  /* dedup=1 */
    J("    invokestatic %s/pj_sort_list(Ljava/lang/Object;I)Ljava/lang/Object;\n", pj_classname);
    J("    astore 10\n");
    J("    aload_2\n"); J("    aload 10\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_agg_fail\n");
    J("    goto pj_agg_succeed\n");

    J("pj_agg_not_bag:\n");

    /* sum(Expr), max(Expr), min(Expr) — local 7 already holds functor name */
    J("    lconst_0\n"); J("    lstore 11\n");  /* accumulator = 0 */
    J("    lconst_0\n"); J("    lstore 13\n");  /* best = 0 */
    J("    iconst_0\n"); J("    istore 15\n");  /* has_val = false */

    J("    aload 7\n");
    J("    ldc \"sum\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_agg_sum\n");
    J("    aload 7\n");
    J("    ldc \"max\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_agg_max\n");
    J("    aload 7\n");
    J("    ldc \"min\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_agg_min\n");
    J("    goto pj_agg_unknown\n");

    /* --- sum loop --- */
    J("pj_agg_sum:\n");
    J("    iconst_0\n"); J("    istore 10\n");  /* i=0 */
    J("pj_agg_sum_loop:\n");
    J("    iload 10\n");
    J("    aload 4\n"); J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    if_icmpge pj_agg_sum_done\n");
    J("    aload 4\n"); J("    iload 10\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_2\n"); J("    aaload\n");  /* bag(Expr) — arg0 of compound */
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    J("    lload 11\n"); J("    ladd\n"); J("    lstore 11\n");
    J("    iinc 10 1\n");
    J("    goto pj_agg_sum_loop\n");
    J("pj_agg_sum_done:\n");
    J("    lload 11\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_2\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_agg_fail\n");
    J("    goto pj_agg_succeed\n");

    /* --- max loop --- */
    J("pj_agg_max:\n");
    J("    iconst_0\n"); J("    istore 10\n");
    J("pj_agg_max_loop:\n");
    J("    iload 10\n");
    J("    aload 4\n"); J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    if_icmpge pj_agg_max_done\n");
    J("    aload 4\n"); J("    iload 10\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_2\n"); J("    aaload\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    J("    lstore 13\n");  /* val */
    J("    iload 15\n"); J("    ifne pj_agg_max_cmp\n");
    /* first element */
    J("    lload 13\n"); J("    lstore 11\n");
    J("    iconst_1\n"); J("    istore 15\n");
    J("    goto pj_agg_max_next\n");
    J("pj_agg_max_cmp:\n");
    J("    lload 13\n"); J("    lload 11\n"); J("    lcmp\n");
    J("    ifle pj_agg_max_next\n");
    J("    lload 13\n"); J("    lstore 11\n");
    J("pj_agg_max_next:\n");
    J("    iinc 10 1\n");
    J("    goto pj_agg_max_loop\n");
    J("pj_agg_max_done:\n");
    J("    iload 15\n"); J("    ifeq pj_agg_fail\n");  /* no solutions → fail */
    J("    lload 11\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_2\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_agg_fail\n");
    J("    goto pj_agg_succeed\n");

    /* --- min loop --- */
    J("pj_agg_min:\n");
    J("    iconst_0\n"); J("    istore 10\n");
    J("pj_agg_min_loop:\n");
    J("    iload 10\n");
    J("    aload 4\n"); J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    if_icmpge pj_agg_min_done\n");
    J("    aload 4\n"); J("    iload 10\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    checkcast [Ljava/lang/Object;\n");
    J("    iconst_2\n"); J("    aaload\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    invokestatic %s/pj_int_val(Ljava/lang/Object;)J\n", pj_classname);
    J("    lstore 13\n");
    J("    iload 15\n"); J("    ifne pj_agg_min_cmp\n");
    J("    lload 13\n"); J("    lstore 11\n");
    J("    iconst_1\n"); J("    istore 15\n");
    J("    goto pj_agg_min_next\n");
    J("pj_agg_min_cmp:\n");
    J("    lload 13\n"); J("    lload 11\n"); J("    lcmp\n");
    J("    ifge pj_agg_min_next\n");
    J("    lload 13\n"); J("    lstore 11\n");
    J("pj_agg_min_next:\n");
    J("    iinc 10 1\n");
    J("    goto pj_agg_min_loop\n");
    J("pj_agg_min_done:\n");
    J("    iload 15\n"); J("    ifeq pj_agg_fail\n");
    J("    lload 11\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    aload_2\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_agg_fail\n");
    J("    goto pj_agg_succeed\n");

    J("pj_agg_unknown:\n");
    J("    aconst_null\n"); J("    areturn\n");

    J("pj_agg_succeed:\n");
    J("    iconst_1\n");
    J("    anewarray java/lang/Object\n");
    J("    dup\n");
    J("    iconst_0\n");
    J("    iconst_1\n");
    J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
    J("    aastore\n");
    J("    areturn\n");

    J("pj_agg_fail:\n");
    J("    aconst_null\n"); J("    areturn\n");
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

    /* ---- pj_is_ground: check if a term contains no unbound variables ---- */
    J("; === pj_is_ground (recursive ground check) ===========================\n");
    J(".method static pj_is_ground(Ljava/lang/Object;)Z\n");
    J("    .limit stack 8\n");
    J("    .limit locals 5\n");
    /* deref */
    J("    aload_0\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore_0\n");
    /* null = unbound var → not ground */
    J("    aload_0\n");
    J("    ifnull pj_isg_false\n");
    J("    aload_0\n");
    J("    instanceof [Ljava/lang/Object;\n");
    J("    ifeq pj_isg_string\n");
    J("    aload_0\n"); J("    checkcast [Ljava/lang/Object;\n"); J("    astore 1\n");
    J("    aload 1\n"); J("    iconst_0\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n"); J("    astore 2\n");
    /* var tag → not ground */
    J("    aload 2\n"); J("    ldc \"var\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_isg_false\n");
    /* ref tag → recurse on [1] */
    J("    aload 2\n"); J("    ldc \"ref\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_isg_not_ref\n");
    J("    aload 1\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    invokestatic %s/pj_is_ground(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ireturn\n");
    J("pj_isg_not_ref:\n");
    /* atom or int → ground */
    J("    aload 2\n"); J("    ldc \"atom\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_isg_true\n");
    J("    aload 2\n"); J("    ldc \"int\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_isg_true\n");
    J("    aload 2\n"); J("    ldc \"float\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifne pj_isg_true\n");
    /* compound: check all args */
    J("    aload 1\n"); J("    arraylength\n"); J("    iconst_2\n"); J("    isub\n"); J("    istore 3\n"); /* arity */
    J("    iconst_0\n"); J("    istore 4\n"); /* i=0 */
    J("pj_isg_compound_loop:\n");
    J("    iload 4\n"); J("    iload 3\n"); J("    if_icmpge pj_isg_true\n");
    J("    aload 1\n"); J("    iload 4\n"); J("    iconst_2\n"); J("    iadd\n"); J("    aaload\n");
    J("    invokestatic %s/pj_is_ground(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_isg_false\n");
    J("    iinc 4 1\n"); J("    goto pj_isg_compound_loop\n");
    J("pj_isg_string:\n");
    J("pj_isg_true:\n"); J("    iconst_1\n"); J("    ireturn\n");
    J("pj_isg_false:\n"); J("    iconst_0\n"); J("    ireturn\n");
    J(".end method\n\n");

    /* ---- pj_can_compare(?X, ?Y) → Z: succeed if both ground or identical -- */
    J("; === pj_can_compare (?= operator) ====================================\n");
    J(".method static pj_can_compare(Ljava/lang/Object;Ljava/lang/Object;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_is_ground(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_cc_false\n");
    J("    aload_1\n");
    J("    invokestatic %s/pj_is_ground(Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_cc_false\n");
    J("    iconst_1\n"); J("    ireturn\n");
    J("pj_cc_false:\n"); J("    iconst_0\n"); J("    ireturn\n");
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
    J("    .limit locals 16\n");
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
    /* phrase/2,3 — rewrite to NT/+2 then reflect-call NT.
     * phrase(NT, List)       -> p_NT_2(List, [])
     * phrase(NT, List, Rest) -> p_NT_3(List, Rest)   (arity = NT_base+2)
     * NT may itself be a compound: item(X) -> p_item_3(X, List, Rest).
     * We detect phrase by functor name and arity 2 or 3, then build a new
     * args array [nt_arg0..., list, rest] and reflect-call the NT functor. */
    J("    aload 4\n"); J("    ldc \"phrase\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_not_phrase\n");
    /* compute arity = t.length - 2 inline (local 5 not yet set at this point) */
    J("    aload 2\n"); J("    arraylength\n"); J("    iconst_2\n"); J("    isub\n"); J("    istore 5\n");
    J("    iload 5\n"); J("    iconst_2\n"); J("    if_icmplt pj_cg_not_phrase\n");
    J("    iload 5\n"); J("    iconst_3\n"); J("    if_icmpgt pj_cg_not_phrase\n");
    /* NT term = t[2] (first arg of phrase), deref */
    J("    aload 2\n"); J("    iconst_2\n"); J("    aaload\n");
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    astore 6\n");  /* nt_term */
    /* Determine NT functor name and base arity */
    J("    aload 6\n");
    J("    instanceof [Ljava/lang/Object;\n");
    J("    ifeq pj_cg_phrase_atom_nt\n");
    /* compound NT: functor = nt[1], base_arity = nt.length-2 */
    J("    aload 6\n"); J("    checkcast [Ljava/lang/Object;\n"); J("    astore 7\n");
    J("    aload 7\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n"); J("    astore 8\n"); /* nt_fn */
    J("    aload 7\n"); J("    arraylength\n"); J("    iconst_2\n"); J("    isub\n"); J("    istore 9\n"); /* nt_base */
    J("    goto pj_cg_phrase_have_nt\n");
    J("pj_cg_phrase_atom_nt:\n");
    /* atom NT: functor = nt itself, base_arity = 0 */
    J("    aconst_null\n"); J("    astore 7\n"); /* local 7 unused but must be typed */
    J("    aload 6\n"); J("    checkcast java/lang/String\n"); J("    astore 8\n"); /* nt_fn */
    J("    iconst_0\n"); J("    istore 9\n"); /* nt_base = 0 */
    J("pj_cg_phrase_have_nt:\n");
    /* nil-NT shortcut: phrase([], List) -> List=[] ; phrase([], List, Rest) -> List=Rest */
    J("    aload 8\n"); J("    ldc \"[]\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_phrase_not_nil_nt\n");
    /* nt_fn == "[]": List unifies with Rest (or []) */
    J("    aload 2\n"); J("    iconst_3\n"); J("    aaload\n"); /* list arg */
    J("    iload 5\n"); J("    iconst_3\n"); J("    if_icmpeq pj_cg_phrase_nil_has_rest\n");
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    goto pj_cg_phrase_nil_do_unify\n");
    J("pj_cg_phrase_nil_has_rest:\n");
    J("    aload 2\n"); J("    bipush 4\n"); J("    aaload\n"); /* rest arg */
    J("pj_cg_phrase_nil_do_unify:\n");
    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
    J("    ifeq pj_cg_fail\n");
    J("    iconst_0\n"); J("    ireturn\n");
    J("pj_cg_phrase_not_nil_nt:\n");
    /* total call arity = nt_base + 2 */
    J("    iload 9\n"); J("    iconst_2\n"); J("    iadd\n"); J("    istore 10\n"); /* call_arity */
    /* build args array: [nt_args..., list, rest_or_nil] */
    J("    iload 10\n");
    J("    anewarray java/lang/Object\n"); J("    astore 11\n");
    /* copy nt_args (indices 2..nt_base+1 from nt_term if compound) */
    J("    iconst_0\n"); J("    istore 12\n"); /* j=0 */
    J("pj_cg_phrase_nt_args_loop:\n");
    J("    iload 12\n"); J("    iload 9\n"); J("    if_icmpge pj_cg_phrase_nt_args_done\n");
    J("    aload 11\n"); J("    iload 12\n");
    J("    aload 7\n"); J("    iload 12\n"); J("    iconst_2\n"); J("    iadd\n"); J("    aaload\n"); /* nt[j+2] */
    J("    aastore\n");
    J("    iinc 12 1\n"); J("    goto pj_cg_phrase_nt_args_loop\n");
    J("pj_cg_phrase_nt_args_done:\n");
    /* args[nt_base] = list arg = t[3] */
    J("    aload 11\n"); J("    iload 9\n");
    J("    aload 2\n"); J("    iconst_3\n"); J("    aaload\n"); /* t[3] = list */
    J("    aastore\n");
    /* args[nt_base+1] = rest: t[4] if phrase/3, else [] */
    J("    aload 11\n"); J("    iload 9\n"); J("    iconst_1\n"); J("    iadd\n");
    J("    iload 5\n"); J("    iconst_3\n"); J("    if_icmpeq pj_cg_phrase_has_rest\n");
    /* phrase/2: rest = [] (nil atom term, not bare string) */
    J("    ldc \"[]\"\n");
    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
    J("    goto pj_cg_phrase_store_rest\n");
    J("pj_cg_phrase_has_rest:\n");
    J("    aload 2\n"); J("    bipush 4\n"); J("    aaload\n"); /* t[4] = rest */
    J("pj_cg_phrase_store_rest:\n");
    J("    aastore\n");
    /* reflect-call NT */
    J("    aload 8\n");   /* nt functor */
    J("    iload 10\n");  /* call arity */
    J("    aload 11\n");  /* args */
    J("    iload_1\n");   /* cs */
    J("    invokestatic %s/pj_reflect_call(Ljava/lang/String;I[Ljava/lang/Object;I)[Ljava/lang/Object;\n", pj_classname);
    J("    astore 13\n");
    J("    aload 13\n"); J("    ifnull pj_cg_fail\n");
    J("    aload 13\n"); J("    iconst_0\n"); J("    aaload\n");
    J("    checkcast java/lang/Integer\n");
    J("    invokevirtual java/lang/Integer/intValue()I\n");
    J("    ireturn\n");
    J("pj_cg_not_phrase:\n");
    /* assertz/1 — dynamic fact assertion */
    J("    aload 4\n");
    J("    ldc \"assertz\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_cg_not_assertz\n");
    J("    aload 2\n"); J("    iconst_2\n"); J("    aaload\n");   /* assertz arg */
    J("    invokestatic %s/pj_deref(Ljava/lang/Object;)Ljava/lang/Object;\n", pj_classname);
    J("    dup\n");
    J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
    J("    swap\n"); J("    iconst_0\n");
    J("    invokestatic %s/pj_db_assert(Ljava/lang/String;Ljava/lang/Object;I)V\n", pj_classname);
    J("    iconst_0\n"); J("    ireturn\n");
    J("pj_cg_not_assertz:\n");
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
    J("    .limit locals 15\n");
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
    J("    astore 10\n");   /* save exception */
    /* Unwrap InvocationTargetException so PROLOG_THROW survives reflection */
    J("    aload 10\n");
    J("    instanceof java/lang/reflect/InvocationTargetException\n");
    J("    ifeq pj_rc_not_ite\n");
    J("    aload 10\n");
    J("    checkcast java/lang/reflect/InvocationTargetException\n");
    J("    invokevirtual java/lang/reflect/InvocationTargetException/getCause()Ljava/lang/Throwable;\n");
    J("    dup\n");
    J("    ifnull pj_rc_ite_null\n");  /* no cause → swallow */
    J("    astore 10\n");   /* replace with unwrapped cause */
    J("    goto pj_rc_not_ite\n");
    J("pj_rc_ite_null:\n");
    J("    pop\n");   /* discard null from stack */
    J("    goto pj_rc_swallow\n");
    J("pj_rc_not_ite:\n");
    J("    aload 10\n");
    J("    invokevirtual java/lang/Throwable/getMessage()Ljava/lang/String;\n");
    J("    ifnull pj_rc_swallow\n");
    J("    aload 10\n");
    J("    invokevirtual java/lang/Throwable/getMessage()Ljava/lang/String;\n");
    J("    ldc \"PROLOG_THROW\"\n");
    J("    invokevirtual java/lang/String/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_rc_swallow\n");
    J("    aload 10\n");
    J("    athrow\n");       /* re-throw Prolog exceptions */
    J("pj_rc_swallow:\n");
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
    /* Reserve last two slots for dynamic DB walker: db_idx (int) and db_term (ref).
     * Using locals_needed-2/locals_needed-1 ensures no overlap with clause locals.
     * Declared here so we can emit iconst_0/istore at method entry before any branch,
     * ensuring the JVM verifier types the slot as int (avoids ClassCastException on iinc). */
    int db_idx_local_slot  = locals_needed - 2;

    J("    .limit stack %d\n", max_stack < 512 ? 512 : max_stack);
    J("    .limit locals %d\n", locals_needed);
    /* Pre-initialise db_idx slot as int so iinc is always valid */
    J("    iconst_0\n");
    J("    istore %d\n", db_idx_local_slot);

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
    /* PJ-81: method splitting.
     * When nclauses > PJ_SPLIT_THRESHOLD the combined clause bodies can exceed
     * the JVM 16-bit branch-offset limit (~32 KB of bytecode per method).
     * Fix: emit each clause as its own static sub-method
     *   p_fn_arity__cN(args..., I init_cs) → Object[]
     * The main dispatcher calls invokestatic for the matched clause and returns
     * the result directly.  Branch distances in the dispatcher stay tiny.
     *
     * Sub-method signature: same args as parent + I (init_cs), no outer cs.
     * Returns: null = clause failed (→ try next), Object[] = gamma/cutgamma result.
     */
#define PJ_SPLIT_THRESHOLD 16
    int do_split = (nclauses > PJ_SPLIT_THRESHOLD);

    if (!do_split) {
    /* Linear scan from last to first */
    for (int ci = nclauses - 1; ci >= 0; ci--)
        J("    iload %d ; cs >= %d? → clause%d\n    ldc %d\n    if_icmpge p_%s_%d_clause%d\n",
          cs_local, base[ci], ci, base[ci], safe_fn, arity, ci);
    J("    goto p_%s_%d_omega\n", safe_fn, arity);
    }

    /* PJ-81 split threshold */
    /* When nclauses > PJ_SPLIT_THRESHOLD the combined clause bodies can exceed
     * the JVM 16-bit branch-offset limit (~32 KB of bytecode per method).
     * Fix: emit each clause as its own static sub-method
     *   p_fn_arity__cN(args..., I init_cs) → Object[]
     * The main dispatcher calls invokestatic for the matched clause and returns
     * the result directly.  Branch distances in the dispatcher stay tiny.
     *
     * Sub-method signature: same args as parent + I (init_cs), no outer cs.
     * Returns: null = clause failed (→ try next), Object[] = gamma/cutgamma result.
     */
    if (do_split) {
        /* ---- SPLIT PATH: dispatcher calls per-clause sub-methods ---- */

        /* Dispatcher: linear scan, invokestatic to sub-method, areturn result.
         * On null return try next clause. */
        for (int ci = nclauses - 1; ci >= 0; ci--)
            J("    iload %d ; cs >= %d? → clause%d\n    ldc %d\n    if_icmpge p_%s_%d_clause%d\n",
              cs_local, base[ci], ci, base[ci], safe_fn, arity, ci);
        J("    goto p_%s_%d_omega\n", safe_fn, arity);

        /* Emit per-clause dispatch stubs in the main method */
        for (int ci = 0; ci < nclauses; ci++) {
            EXPR_t *clause = choice->children[ci];
            if (!clause || clause->kind != E_CLAUSE) continue;

            /* label */
            J("p_%s_%d_clause%d:\n", safe_fn, arity, ci);

            /* compute init_cs = max(0, cs - base[ci]) */
            J("    iload %d\n", cs_local);
            J("    ldc %d\n", base[ci]);
            J("    isub\n");
            J("    dup\n");
            {
                char lbl_clamp[128];
                snprintf(lbl_clamp, sizeof lbl_clamp, "p_%s_%d_dc_clamp_%d", safe_fn, arity, ci);
                J("    ifge %s\n", lbl_clamp);
                J("    pop\n");
                J("    iconst_0\n");
                J("%s:\n", lbl_clamp);
            }
            /* init_cs is on stack from clamp above — save it, then load args, then reload */
            J("    istore %d\n", init_cs_local);
            for (int ai = 0; ai < arity; ai++)
                J("    aload %d\n", ai);
            J("    iload %d\n", init_cs_local);
            /* invokestatic p_fn_arity__cN */
            J("    invokestatic %s/p_%s_%d__c%d(", pj_classname, safe_fn, arity, ci);
            for (int ai = 0; ai < arity; ai++) J("[Ljava/lang/Object;");
            J("I)[Ljava/lang/Object;\n");
            /* null → next clause; non-null → return to caller */
            {
                char lbl_null[128];
                snprintf(lbl_null, sizeof lbl_null, "p_%s_%d_split_null_%d", safe_fn, arity, ci);
                char next_lbl[128];
                if (ci + 1 < nclauses)
                    snprintf(next_lbl, sizeof next_lbl, "p_%s_%d_clause%d", safe_fn, arity, ci + 1);
                else
                    snprintf(next_lbl, sizeof next_lbl, "p_%s_%d_omega", safe_fn, arity);
                J("    dup\n");
                J("    ifnull %s\n", lbl_null);
                J("    areturn\n");
                J("%s:\n", lbl_null);
                J("    pop\n");
                J("    goto %s\n", next_lbl);
            }
        }

        /* Close the main dispatcher method before sub-methods */
        J("p_%s_%d_omega:\n", safe_fn, arity);
        /* (dynamic DB walker follows below — we fall through to it) */

    } else {
    /* ---- INLINE PATH (original): emit all clause bodies in main method ---- */

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
    } /* end inline else */

    /* ω port — first try dynamic DB, then truly fail */
    if (!do_split)
        J("p_%s_%d_omega:\n", safe_fn, arity);

    /* Dynamic DB walker: key = "name/arity", cs encodes db index as base[nclauses]+idx */
    {
        /* cs range for dynamic clauses: base[nclauses], base[nclauses]+1, ...
         * db_idx = cs - base[nclauses]  (0 = first dynamic clause)
         * On static-clause exhaustion cs < base[nclauses]+N, so we check if
         * cs >= base[nclauses] which is already assured by reaching omega.
         * We use a local for db_idx. */
        int db_idx_local = db_idx_local_slot; /* fixed: use locals_needed-2, not hardcoded offset */
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
            int db_term_local = db_idx_local_slot + 1; /* locals_needed-1 */
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

        /* success — return proper continuation array: Object[1+arity]
         * slot [0] = Integer(base[nclauses] + db_idx + 1)  (next cs for retry)
         * slots [1..arity] = args (already unified in-place above) */
        J("    ldc %d\n", arity + 1);
        JI("anewarray", "java/lang/Object");
        JI("dup", "");
        JI("iconst_0", "");
        /* next cs = base[nclauses] + db_idx + 1 */
        J("    ldc %d\n", base[nclauses]);
        J("    iload %d\n", db_idx_local);
        JI("iadd", "");
        JI("iconst_1", "");
        JI("iadd", "");
        JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
        JI("aastore", "");
        for (int ai = 0; ai < arity; ai++) {
            JI("dup", "");
            J("    ldc %d\n", ai + 1);
            J("    aload %d\n", ai);
            JI("aastore", "");
        }
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

    /* PJ-81: emit per-clause sub-methods for split predicates */
    if (do_split) {
        for (int ci = 0; ci < nclauses; ci++) {
            EXPR_t *clause = choice->children[ci];
            if (!clause || clause->kind != E_CLAUSE) continue;

            int n_vars = (int)clause->ival;
            int n_args = (int)clause->dval;

            /* Sub-method signature: p_fn_arity__cN(args..., I init_cs) → Object[] */
            J("; --- split clause %d of %s/%d ---\n", ci, name_only, arity);
            J(".method static p_%s_%d__c%d(", safe_fn, arity, ci);
            for (int ai = 0; ai < arity; ai++) J("[Ljava/lang/Object;");
            J("I)[Ljava/lang/Object;\n");

            /* locals layout in sub-method:
             *   0..arity-1  : args (same as parent)
             *   arity       : init_cs  (was arity+2 in parent; here it's the last param)
             *   arity+1     : trail_mark
             *   arity+2     : sub_cs_out
             *   arity+3..   : vars + ucall temps
             */
            int sm_init_cs   = arity;       /* parameter */
            int sm_trail     = arity + 1;
            int sm_subcs_out = arity + 2;
            int sm_vars_base = arity + 3;

            /* recompute max_ucalls/stack for this clause only */
            int sm_max_vars  = n_vars;
            int sm_max_uc    = 0, sm_max_neq = 0, sm_max_disj = 0, sm_max_stack = 16;
            {
                int n_args_ci = (int)clause->dval;
                int nb = clause->nchildren - n_args_ci; if (nb < 0) nb = 0;
                sm_max_uc   = pj_count_ucalls(clause->children + n_args_ci, nb);
                sm_max_neq  = pj_count_neq(clause->children + n_args_ci, nb);
                sm_max_disj = pj_count_disj_locals(clause->children + n_args_ci, nb);
                sm_max_stack = pj_clause_stack_needed(clause->children + n_args_ci, nb);
                for (int hi = 0; hi < n_args_ci; hi++) {
                    int d = pj_term_stack_depth(clause->children[hi]) + 4;
                    if (d > sm_max_stack) sm_max_stack = d;
                }
                if (sm_max_stack < 512) sm_max_stack = 512;
            }
            int sm_locals = sm_vars_base + sm_max_vars + 5*sm_max_uc + 2*sm_max_neq + sm_max_disj + 16 + 4;
            J("    .limit stack %d\n", sm_max_stack);
            J("    .limit locals %d\n", sm_locals);

            /* var_locals for this sub-method */
            int *var_locals = calloc(n_vars + 1, sizeof(int));
            for (int vi = 0; vi < n_vars; vi++)
                var_locals[vi] = sm_vars_base + vi;

            /* omega label = return null */
            char sm_omega[128];
            snprintf(sm_omega, sizeof sm_omega, "p_%s_%d__c%d_omega", safe_fn, arity, ci);

            /* trail mark */
            J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
            J("    istore %d\n", sm_trail);

            /* init sub_cs_out = 0 */
            J("    iconst_0\n");
            J("    istore %d\n", sm_subcs_out);

            /* allocate var cells */
            {
                int *jaf = calloc(n_vars + 1, sizeof(int));
                for (int vi = 0; vi < n_vars; vi++) jaf[vi] = -1;
                for (int ai2 = 0; ai2 < n_args && ai2 < clause->nchildren; ai2++) {
                    EXPR_t *ht = clause->children[ai2];
                    if (ht && ht->kind == E_VART && ht->ival >= 0 && ht->ival < n_vars)
                        if (jaf[ht->ival] < 0) jaf[ht->ival] = ai2;
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

            /* head unification */
            {
                char sm_alphafail[128];
                snprintf(sm_alphafail, sizeof sm_alphafail, "p_%s_%d__c%d_af", safe_fn, arity, ci);
                int *seen_at = calloc(n_vars + 1, sizeof(int));
                for (int vi = 0; vi < n_vars; vi++) seen_at[vi] = -1;
                for (int ai2 = 0; ai2 < n_args && ai2 < clause->nchildren; ai2++) {
                    EXPR_t *ht = clause->children[ai2];
                    if (ht && ht->kind == E_VART && ht->ival >= 0 && ht->ival < n_vars)
                        if (seen_at[ht->ival] < 0) seen_at[ht->ival] = ai2;
                }
                for (int ai2 = 0; ai2 < n_args && ai2 < clause->nchildren; ai2++) {
                    EXPR_t *ht = clause->children[ai2];
                    if (!ht) continue;
                    if (ht->kind == E_VART) {
                        int slot = ht->ival;
                        if (slot < 0 || slot >= n_vars) continue;
                        if (seen_at[slot] == ai2) continue;
                        J("    aload %d\n", ai2);
                        J("    aload %d\n", var_locals[slot]);
                        J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                        J("    ifeq %s\n", sm_alphafail);
                        continue;
                    }
                    J("    aload %d\n", ai2);
                    pj_emit_term(ht, var_locals, n_vars);
                    J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                    J("    ifeq %s\n", sm_alphafail);
                }
                free(seen_at);

                J("    goto p_%s_%d__c%d_beta\n", safe_fn, arity, ci);
                J("%s:\n", sm_alphafail);
                J("    iload %d\n", sm_trail);
                J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
                J("    aconst_null\n");
                J("    areturn\n");
            }

            /* retry_head (alpha) label */
            J("p_%s_%d__c%d_alpha:\n", safe_fn, arity, ci);
            J("    iload %d\n", sm_trail);
            J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
            {
                int *jaf = calloc(n_vars + 1, sizeof(int));
                for (int vi = 0; vi < n_vars; vi++) jaf[vi] = -1;
                for (int ai2 = 0; ai2 < n_args && ai2 < clause->nchildren; ai2++) {
                    EXPR_t *ht = clause->children[ai2];
                    if (ht && ht->kind == E_VART && ht->ival >= 0 && ht->ival < n_vars)
                        jaf[ht->ival] = ai2;
                }
                for (int vi = 0; vi < n_vars; vi++) {
                    if (jaf[vi] >= 0) { J("    aload %d\n", jaf[vi]); J("    astore %d\n", var_locals[vi]); }
                    else { J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname); J("    astore %d\n", var_locals[vi]); }
                }
                free(jaf);
            }
            for (int ai2 = 0; ai2 < n_args && ai2 < clause->nchildren; ai2++) {
                EXPR_t *ht = clause->children[ai2];
                if (!ht || ht->kind == E_VART) continue;
                J("    aload %d\n", ai2);
                pj_emit_term(ht, var_locals, n_vars);
                J("    invokestatic %s/pj_unify(Ljava/lang/Object;Ljava/lang/Object;)Z\n", pj_classname);
                J("    ifeq %s\n", sm_omega);
            }

            /* beta label + body */
            J("p_%s_%d__c%d_beta:\n", safe_fn, arity, ci);
            {
                int nbody = clause->nchildren - n_args;
                EXPR_t **body_goals = clause->children + n_args;
                char γ_lbl[128], cutγ_lbl[128], bodyfail_lbl[128];
                snprintf(γ_lbl,        sizeof γ_lbl,        "p_%s_%d__c%d_gamma",    safe_fn, arity, ci);
                snprintf(cutγ_lbl,     sizeof cutγ_lbl,     "p_%s_%d__c%d_cutgamma", safe_fn, arity, ci);
                snprintf(bodyfail_lbl, sizeof bodyfail_lbl,  "p_%s_%d__c%d_bodyfail", safe_fn, arity, ci);
                int next_local_sm = sm_vars_base + n_vars;
                /* lbl_outer_ω = sm_omega (null return = clause failed) */
                pj_emit_body(body_goals, nbody, γ_lbl, bodyfail_lbl, sm_omega,
                             sm_trail, var_locals, n_vars, &next_local_sm,
                             sm_init_cs, sm_subcs_out,
                             base[nclauses], sm_init_cs, sm_omega, cutγ_lbl);
                J("%s:\n", bodyfail_lbl);
                J("    iload %d\n", sm_trail);
                J("    invokestatic %s/pj_trail_unwind(I)V\n", pj_classname);
                J("    aconst_null\n");
                J("    areturn\n");
            }

            /* gamma port */
            J("p_%s_%d__c%d_gamma:\n", safe_fn, arity, ci);
            J("    iconst_1\n");
            J("    anewarray java/lang/Object\n");
            J("    dup\n");
            J("    iconst_0\n");
            if (nclauses == 1 && last_has_ucall) {
                J("    iload %d\n", sm_subcs_out);
            } else {
                J("    ldc %d\n", base[ci]);
                J("    iload %d\n", sm_init_cs);
                J("    iadd\n");
                J("    iconst_1\n");
                J("    iadd\n");
            }
            J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
            J("    aastore\n");
            J("    areturn\n");

            /* cutgamma port */
            J("p_%s_%d__c%d_cutgamma:\n", safe_fn, arity, ci);
            J("    iconst_1\n");
            J("    anewarray java/lang/Object\n");
            J("    dup\n");
            J("    iconst_0\n");
            J("    ldc 2147483647\n");
            J("    invokestatic java/lang/Integer/valueOf(I)Ljava/lang/Integer;\n");
            J("    aastore\n");
            J("    areturn\n");

            /* omega: null return */
            J("%s:\n", sm_omega);
            J("    aconst_null\n");
            J("    areturn\n");

            J(".end method\n\n");
            free(var_locals);
        }
    }
}

/* =========================================================================
 * M-PJ-LINKER: plunit linker
 *
 * When a program contains :- use_module(library(plunit)), the linker:
 *   1. Compiles the embedded plunit.pl shim (parse→lower→emit choices)
 *   2. Emits empty dynamic stubs for pj_suite/1 and pj_test/4
 *   3. In pj_emit_main, assertz pj_suite/pj_test facts statically derived
 *      from the test/1 and test/2 clause heads between begin_tests/end_tests
 *   4. Emits bridge predicates suite_name/0 :- test(name)
 *
 * This is the Wizard of Oz approach: SWI files load unchanged.
 * ========================================================================= */

/* plunit.pl shim source embedded as a C string */
static const char pj_plunit_shim_src[] =
    "begin_tests(_).\n"
    "end_tests(_).\n"
    "forall(Cond, Action) :- \\+ forall_fails(Cond, Action).\n"
    "forall_fails(Cond, Action) :- Cond, \\+ Action.\n"
    "acyclic_term(_).\n"
    "cyclic_term(_) :- fail.\n"
    "maplist(_, []).\n"
    "maplist(Goal, [H|T]) :- call_1(Goal, H), maplist(Goal, T).\n"
    "maplist(_, [], []).\n"
    "maplist(Goal, [H1|T1], [H2|T2]) :- call_2(Goal, H1, H2), maplist(Goal, T1, T2).\n"
    "call_1(Goal, Arg) :- Goal =.. L, append(L, [Arg], L2), G2 =.. L2, G2.\n"
    "call_2(Goal, A1, A2) :- Goal =.. L, append(L, [A1,A2], L2), G2 =.. L2, G2.\n"
    "append([], L, L).\n"
    "append([H|T], L, [H|R]) :- append(T, L, R).\n"
    "pj_init :- nb_setval(pj_p,0), nb_setval(pj_f,0), nb_setval(pj_s,0).\n"
    "pj_inc_pass :- nb_getval(pj_p,N), N1 is N+1, nb_setval(pj_p,N1).\n"
    "pj_inc_fail :- nb_getval(pj_f,N), N1 is N+1, nb_setval(pj_f,N1).\n"
    "pj_inc_skip :- nb_getval(pj_s,N), N1 is N+1, nb_setval(pj_s,N1).\n"
    "pj_summary :-\n"
    "    nb_getval(pj_p,P), nb_getval(pj_f,F), nb_getval(pj_s,S),\n"
    "    format('~n% ~w passed, ~w failed, ~w skipped~n',[P,F,S]).\n"
    "run_tests :- pj_init, run_all_suites, pj_summary.\n"
    "run_tests(Suite) :- is_list(Suite), !, pj_init, run_suites_list(Suite), pj_summary.\n"
    "run_tests(Suite) :- pj_init, run_suite(Suite), pj_summary.\n"
    "run_suites_list([]).\n"
    "run_suites_list([H|T]) :- run_suite(H), run_suites_list(T).\n"
    "run_all_suites :- pj_suite(S), run_suite(S), fail.\n"
    "run_all_suites.\n"
    "run_suite(Suite) :-\n"
    "    format('~n% PL-Unit: ~w~n',[Suite]), !,\n"
    "    run_suite_tests(Suite).\n"
    "run_suite_tests(Suite) :-\n"
    "    pj_test(Suite, Name, Opts, Goal),\n"
    "    run_one(Suite, Name, Opts, Goal),\n"
    "    fail.\n"
    "run_suite_tests(_).\n"
    "run_one(Suite, Name, Opts, Goal) :-\n"
    "    ( Opts = pj_inline ->\n"
    "        ( catch(Goal, _E, fail) -> true ; true )\n"
    "    ; pj_has_sto(Opts) ->\n"
    "        pj_inc_skip, format('  skip: ~w:~w  [sto]~n',[Suite,Name])\n"
    "    ; pj_skip_condition(Opts) ->\n"
    "        pj_inc_skip, format('  skip: ~w:~w  [condition]~n',[Suite,Name])\n"
    "    ; pj_has_error(Opts, ExpErr) ->\n"
    "        run_error(Suite, Name, Goal, ExpErr)\n"
    "    ; pj_has_throws(Opts, ExpThrow) ->\n"
    "        run_throw(Suite, Name, Goal, ExpThrow)\n"
    "    ; pj_wants_fail(Opts) ->\n"
    "        run_fail(Suite, Name, Goal)\n"
    "    ; pj_has_true(Opts, Expr) ->\n"
    "        run_true(Suite, Name, Goal, Expr)\n"
    "    ; pj_has_all(Opts, AllExpr) ->\n"
    "        run_all(Suite, Name, Goal, AllExpr)\n"
    "    ;\n"
    "        run_succeed(Suite, Name, Goal, Opts)\n"
    "    ).\n"
    "pj_has_sto([H|_]) :- H = sto(_), !.\n"
    "pj_has_sto([_|T]) :- pj_has_sto(T).\n"
    "pj_skip_condition(Opts) :- member(condition(C), Opts), \\+ C.\n"
    "pj_has_error([error(E)|_], E) :- !.\n"
    "pj_has_error([_|T], E) :- pj_has_error(T, E).\n"
    "pj_has_error(error(E), E).\n"
    "pj_has_throws([throws(T)|_], T) :- !.\n"
    "pj_has_throws([_|T2], T) :- pj_has_throws(T2, T).\n"
    "pj_has_throws(throws(T), T).\n"
    "pj_wants_fail([fail|_]) :- !.\n"
    "pj_wants_fail([false|_]) :- !.\n"
    "pj_wants_fail([_|T]) :- pj_wants_fail(T).\n"
    "pj_wants_fail(fail).\n"
    "pj_wants_fail(false).\n"
    "pj_has_true([true(E)|_], E) :- !.\n"
    "pj_has_true([_|T], E) :- pj_has_true(T, E).\n"
    "pj_has_all([all(E)|_], E) :- !.\n"
    "pj_has_all([_|T], E) :- pj_has_all(T, E).\n"
    "run_succeed(Suite, Name, Goal, Opts) :-\n"
    "    ( is_list(Opts), \\+ Opts = [] -> true ; Opts = [] ; Opts = true ),\n"
    "    ( catch(Goal, _E, fail) ->\n"
    "        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "    ;   pj_inc_fail, format('  FAIL: ~w:~w  (goal failed)~n',[Suite,Name])\n"
    "    ).\n"
    "run_succeed(Suite, Name, Goal, Opts) :-\n"
    "    Opts \\= [], \\+ is_list(Opts), Opts \\= true, Opts \\= fail, Opts \\= false,\n"
    "    Opts \\= pj_inline,\n"
    "    run_true(Suite, Name, Goal, Opts).\n"
    "run_fail(Suite, Name, Goal) :-\n"
    "    ( catch(Goal, _E, true) ->\n"
    "        pj_inc_fail, format('  FAIL: ~w:~w  (expected fail, succeeded)~n',[Suite,Name])\n"
    "    ;   pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "    ).\n"
    "run_error(Suite, Name, Goal, ExpErr) :-\n"
    "    ( catch(Goal, error(ActErr,_), pj_match_err(Suite,Name,ExpErr,ActErr)) ->\n"
    "        true\n"
    "    ;   pj_inc_fail, format('  FAIL: ~w:~w  (no exception)~n',[Suite,Name])\n"
    "    ).\n"
    "pj_match_err(Suite, Name, Exp, Act) :-\n"
    "    copy_term(Exp, ExpC),\n"
    "    ( ExpC = Act ->\n"
    "        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "    ; functor(ExpC,F,_), functor(Act,F,_) ->\n"
    "        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "    ;   pj_inc_fail, format('  FAIL: ~w:~w  (error mismatch ~w vs ~w)~n',[Suite,Name,Exp,Act])\n"
    "    ).\n"
    "run_throw(Suite, Name, Goal, ExpThrow) :-\n"
    "    ( catch(Goal, Actual,\n"
    "        ( Actual = ExpThrow ->\n"
    "            pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "        ;   pj_inc_fail, format('  FAIL: ~w:~w  (throw mismatch)~n',[Suite,Name])\n"
    "        )) ->\n"
    "        true\n"
    "    ;   pj_inc_fail, format('  FAIL: ~w:~w  (no throw)~n',[Suite,Name])\n"
    "    ).\n"
    "run_true(Suite, Name, Goal, Expr) :-\n"
    "    ( catch(Goal, _E, fail) ->\n"
    "        ( catch(Expr, _E2, fail) ->\n"
    "            pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "        ;   pj_inc_fail, format('  FAIL: ~w:~w  (check failed: ~w)~n',[Suite,Name,Expr])\n"
    "        )\n"
    "    ;   pj_inc_fail, format('  FAIL: ~w:~w  (goal failed)~n',[Suite,Name])\n"
    "    ).\n"
    "run_all(Suite, Name, Goal, (Var == Expected)) :-\n"
    "    findall(Var, Goal, Actual),\n"
    "    ( Actual == Expected ->\n"
    "        pj_inc_pass, format('  pass: ~w:~w~n',[Suite,Name])\n"
    "    ;   pj_inc_fail, format('  FAIL: ~w:~w  (all mismatch)~n',[Suite,Name])\n"
    "    ).\n";

/* -------------------------------------------------------------------------
 * pj_linker_has_plunit — returns 1 if program uses use_module(library(plunit))
 * ------------------------------------------------------------------------- */
static int pj_linker_has_plunit(Program *prog) {
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        EXPR_t *g = s->subject;
        if (g->kind == E_CHOICE) continue;
        if (g->kind != E_FNC || !g->sval) continue;
        if (strcmp(g->sval, "use_module") != 0 || g->nchildren != 1) continue;
        /* use_module(library(plunit)) — child is library(plunit) */
        EXPR_t *arg = g->children[0];
        if (!arg || arg->kind != E_FNC || !arg->sval) continue;
        if (strcmp(arg->sval, "library") != 0 || arg->nchildren != 1) continue;
        EXPR_t *lib = arg->children[0];
        if (!lib || !lib->sval) continue;
        if (strcmp(lib->sval, "plunit") == 0) return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * pj_linker_emit_plunit_shim — parse+lower+emit the embedded plunit.pl
 * Called from prolog_emit_jvm() before the user predicates are emitted.
 * ------------------------------------------------------------------------- */
static void pj_linker_emit_plunit_shim(void) {
    JC("=== plunit shim (M-PJ-LINKER) ===");
    PlProgram *pl = prolog_parse(pj_plunit_shim_src, "plunit.pl");
    if (!pl) { fprintf(stderr, "plunit linker: parse failed\n"); return; }
    Program *shim = prolog_lower(pl);
    if (!shim) { fprintf(stderr, "plunit linker: lower failed\n"); return; }
    /* Emit each E_CHOICE from the shim, but skip predicates already defined
     * in the user program to avoid duplicate method errors. */
    for (STMT_t *ss = shim->head; ss; ss = ss->next) {
        if (!ss->subject || ss->subject->kind != E_CHOICE) continue;
        const char *shim_key = ss->subject->sval; /* "name/arity" */
        if (!shim_key) continue;
        /* Check if user already defines this predicate */
        int user_defines = 0;
        for (STMT_t *us = pj_prog->head; us; us = us->next) {
            if (!us->subject || us->subject->kind != E_CHOICE) continue;
            if (us->subject->sval && strcmp(us->subject->sval, shim_key) == 0) {
                user_defines = 1; break;
            }
        }
        if (!user_defines) {
            /* Also skip predicates emitted as JVM synthetics to avoid duplicates */
            if (strcmp(shim_key, "forall/2") == 0 ||
                strcmp(shim_key, "forall_fails/2") == 0)
                continue;
            pj_emit_choice(ss->subject);
        }
    }
    /* Note: shim/pl memory intentionally not freed (static lifetime) */
}

/* -------------------------------------------------------------------------
 * pj_linker_emit_db_stub — emit a proper dynamic-DB stub for name/arity.
 * Mirrors the Bug 1 pure-dynamic stub pattern exactly.
 * ------------------------------------------------------------------------- */
static void pj_linker_emit_db_stub(const char *name, int arity) {
    char safe[256];
    pj_safe_name(name, safe, sizeof safe);
    JC("dynamic DB stub (M-PJ-LINKER)");
    J("; stub for linker-dynamic predicate %s/%d\n", name, arity);
    J(".method static p_%s_%d(", safe, arity);
    for (int i = 0; i < arity; i++) J("[Ljava/lang/Object;");
    J("I)[Ljava/lang/Object;\n");
    int stub_locals = arity + 50;
    J("    .limit stack 16\n");
    J("    .limit locals %d\n", stub_locals);
    int cs_loc  = arity;
    int tr_loc  = arity + 1;
    int idx_loc = arity + 2;
    int trm_loc = arity + 3;
    int lbl = pj_fresh_label();
    char sb_store[64], sb_loop[64], sb_hit[64], sb_miss[64], sb_ok[64], sb_fail[64];
    snprintf(sb_store, sizeof sb_store, "lnk%d_store", lbl);
    snprintf(sb_loop,  sizeof sb_loop,  "lnk%d_loop",  lbl);
    snprintf(sb_hit,   sizeof sb_hit,   "lnk%d_hit",   lbl);
    snprintf(sb_miss,  sizeof sb_miss,  "lnk%d_miss",  lbl);
    snprintf(sb_ok,    sizeof sb_ok,    "lnk%d_ok",    lbl);
    snprintf(sb_fail,  sizeof sb_fail,  "lnk%d_fail",  lbl);
    J("    iload %d\n", cs_loc);
    JI("dup", "");
    J("    ifge %s\n", sb_store);
    JI("pop", "");
    JI("iconst_0", "");
    J("%s:\n", sb_store);
    J("    istore %d\n", idx_loc);
    J("%s:\n", sb_loop);
    J("    ldc \"%s/%d\"\n", name, arity);
    J("    iload %d\n", idx_loc);
    J("    invokestatic %s/pj_db_query(Ljava/lang/String;I)Ljava/lang/Object;\n", pj_classname);
    JI("dup", "");
    J("    ifnonnull %s\n", sb_hit);
    JI("pop", "");
    J("    goto %s\n", sb_miss);
    J("%s:\n", sb_hit);
    JI("checkcast", "[Ljava/lang/Object;");
    J("    astore %d\n", trm_loc);
    J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
    J("    istore %d\n", tr_loc);
    for (int ai = 0; ai < arity; ai++) {
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
    J("    ldc %d\n", arity + 1);
    JI("anewarray", "java/lang/Object");
    JI("dup", "");
    JI("iconst_0", "");
    J("    iload %d\n", idx_loc);
    JI("iconst_1", "");
    JI("iadd", "");
    JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
    JI("aastore", "");
    for (int ai = 0; ai < arity; ai++) {
        JI("dup", "");
        J("    ldc %d\n", ai + 1);
        J("    aload %d\n", ai);
        JI("aastore", "");
    }
    JI("areturn", "");
    J("%s:\n", sb_miss);
    JI("aconst_null", "");
    JI("areturn", "");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * pj_linker_emit_dynamic_stubs — emit proper DB stubs for pj_suite/1 and pj_test/4
 * ------------------------------------------------------------------------- */
static void pj_linker_emit_dynamic_stubs(void) {
    pj_linker_emit_db_stub("pj_suite", 1);
    pj_linker_emit_db_stub("pj_test",  4);
}

/* -------------------------------------------------------------------------
 * pj_linker_prescan — called from main.c BEFORE prolog_program_free(),
 * while PlProgram source order is still intact.
 *
 * Walks the flat PlClause list in source order, tracking begin_tests /
 * end_tests directives, and records which suite each test/N clause belongs
 * to by (functor_key, occurrence_index).
 *
 * Result is stored in pj_prescan_map[], consumed by pj_linker_scan pass-2.
 * ------------------------------------------------------------------------- */
#include "prolog_atom.h"
#include "term.h"

#define PJ_PRESCAN_MAX 512
typedef struct {
    char key[64];   /* "test/1" or "test/2" */
    int  idx;       /* 0-based occurrence within that key */
    char suite[64]; /* suite name from enclosing begin_tests */
} PjPrescanEntry;
static PjPrescanEntry pj_prescan_map[PJ_PRESCAN_MAX];
static int            pj_prescan_n = 0;

void pj_linker_prescan(PlProgram *pl_prog) {
    pj_prescan_n = 0;
    char current_suite[64] = "";
    /* per-key occurrence counters */
    int cnt1 = 0, cnt2 = 0;  /* test/1, test/2 */

    for (PlClause *cl = pl_prog->head; cl; cl = cl->next) {
        if (!cl->head) {
            /* directive — check for begin_tests/end_tests */
            if (cl->nbody > 0) {
                Term *d = cl->body[0];
                if (d && d->tag == TT_COMPOUND && d->compound.arity >= 1) {
                    const char *fn = prolog_atom_name(d->compound.functor);
                    Term *arg0 = d->compound.args[0];
                    if (fn && strcmp(fn, "begin_tests") == 0 && arg0) {
                        const char *sname = NULL;
                        if (arg0->tag == TT_ATOM)
                            sname = prolog_atom_name(arg0->atom_id);
                        else if (arg0->tag == TT_COMPOUND)
                            sname = prolog_atom_name(arg0->compound.functor);
                        if (sname) strncpy(current_suite, sname, 63);
                    } else if (fn && strcmp(fn, "end_tests") == 0) {
                        current_suite[0] = '\0';
                    }
                }
            }
            continue;
        }
        /* clause — record if it's test/1 or test/2 */
        if (!cl->head) continue;
        if (cl->head->tag != TT_COMPOUND && cl->head->tag != TT_ATOM) continue;
        const char *fn = NULL;
        int arity = 0;
        if (cl->head->tag == TT_COMPOUND) {
            fn    = prolog_atom_name(cl->head->compound.functor);
            arity = cl->head->compound.arity;
        } else {
            fn    = prolog_atom_name(cl->head->atom_id);
            arity = 0;
        }
        if (!fn) continue;
        int is1 = (strcmp(fn,"test") == 0 && arity == 1);
        int is2 = (strcmp(fn,"test") == 0 && arity == 2);
        if (!is1 && !is2) continue;
        if (pj_prescan_n >= PJ_PRESCAN_MAX) continue;
        PjPrescanEntry *e = &pj_prescan_map[pj_prescan_n++];
        snprintf(e->key, sizeof e->key, "test/%d", arity);
        e->idx = is1 ? cnt1++ : cnt2++;
        strncpy(e->suite, current_suite[0] ? current_suite : "unknown", 63);
    }
}

/* Forward declarations — pj_linker_emit_bridge needs the test array
 * but the canonical definitions follow below. */
#ifndef PJ_LINKER_MAX_TESTS
#define PJ_LINKER_MAX_TESTS 512
typedef struct {
    char suite[64]; char name[64]; char bridge[128];
    int  arity; EXPR_t *opts_expr;
    EXPR_t *clause_expr;  /* full E_CLAUSE node — needed for true(Expr) var-sharing fix */
} PjTestInfo;
static PjTestInfo pj_linker_tests[PJ_LINKER_MAX_TESTS];
static int        pj_linker_ntest = 0;
static char       pj_linker_suites[32][64];
static int        pj_linker_nsuite = 0;
#endif

/* -------------------------------------------------------------------------
 * pj_linker_emit_bridge — emit a bridge predicate:
 *   suite_name/0 :- test(name).          [arity==1]
 *   suite_name/0 :- test(name, opts).    [arity==2]
 * bridge_fn is "suite_name" (safe-name), test_name is the raw atom.
 * For test/2 the opts term is emitted inline so p_test_2 gets both args.
 * ------------------------------------------------------------------------- */
static void pj_linker_emit_bridge(const char *bridge_fn, const char *test_name, const char *suite_name) {
    /* arity and opts_expr are picked up from the PjTestInfo array by the caller;
     * we need them here — find the matching entry */
    int arity = 1;
    EXPR_t *opts_expr   = NULL;
    EXPR_t *clause_expr = NULL;
    for (int i = 0; i < pj_linker_ntest; i++) {
        if (strcmp(pj_linker_tests[i].bridge, bridge_fn) == 0) {
            arity       = pj_linker_tests[i].arity;
            opts_expr   = pj_linker_tests[i].opts_expr;
            clause_expr = pj_linker_tests[i].clause_expr;
            break;
        }
    }

    /* Detect true(Expr) opts — may be bare true(E), a bare expression, or inside a list.
     * Walk the list to find the first true(Expr) or bare-expression element. */
    int has_true_expr = 0;
    EXPR_t *true_expr_arg = NULL;
    if (arity == 2 && opts_expr) {
        /* bare true(Expr) */
        if (opts_expr->sval && strcmp(opts_expr->sval, "true") == 0
            && opts_expr->nchildren == 1) {
            has_true_expr = 1;
            true_expr_arg = opts_expr->children[0];
        } else if (opts_expr->sval &&
                   strcmp(opts_expr->sval, ".") != 0 &&
                   strcmp(opts_expr->sval, "[]") != 0 &&
                   strcmp(opts_expr->sval, "fail") != 0 &&
                   strcmp(opts_expr->sval, "error") != 0 &&
                   strcmp(opts_expr->sval, "throws") != 0 &&
                   strcmp(opts_expr->sval, "all") != 0 &&
                   strcmp(opts_expr->sval, "forall") != 0 &&
                   strcmp(opts_expr->sval, "sto") != 0 &&
                   strcmp(opts_expr->sval, "blocked") != 0 &&
                   strcmp(opts_expr->sval, "setup") != 0 &&
                   strcmp(opts_expr->sval, "cleanup") != 0 &&
                   opts_expr->nchildren > 0) {
            /* bare expression like X==y or Bs==[b]: treat as true(Expr) */
            has_true_expr = 1;
            true_expr_arg = opts_expr;
        } else {
            /* walk list .(Head, Tail): look for true(E) or bare expr as head */
            EXPR_t *node = opts_expr;
            while (node && node->sval && strcmp(node->sval, ".") == 0
                   && node->nchildren >= 2) {
                EXPR_t *head = node->children[0];
                if (head && head->sval && strcmp(head->sval, "true") == 0
                    && head->nchildren == 1) {
                    has_true_expr = 1;
                    true_expr_arg = head->children[0];
                    break;
                } else if (head && head->sval &&
                           strcmp(head->sval, "fail") != 0 &&
                           strcmp(head->sval, "error") != 0 &&
                           strcmp(head->sval, "throws") != 0 &&
                           strcmp(head->sval, "all") != 0 &&
                           strcmp(head->sval, "forall") != 0 &&
                           strcmp(head->sval, "sto") != 0 &&
                           strcmp(head->sval, "blocked") != 0 &&
                           strcmp(head->sval, "setup") != 0 &&
                           strcmp(head->sval, "cleanup") != 0 &&
                           strcmp(head->sval, ".") != 0 &&
                           strcmp(head->sval, "[]") != 0 &&
                           head->nchildren > 0) {
                    has_true_expr = 1;
                    true_expr_arg = head;
                    break;
                }
                node = node->children[1];
            }
        }
    }

    JC("bridge predicate (M-PJ-LINKER)");
    J(".method static p_%s_0(I)[Ljava/lang/Object;\n", bridge_fn);
    J("    .limit stack 512\n");
    /* For true(Expr) we need locals: 0=cs(I), 1=trail(I), 2..2+n_vars-1=vars, then scratch */
    if (has_true_expr && clause_expr) {
        int n_vars = (int)clause_expr->ival;
        J("    .limit locals %d\n", 4 + n_vars + 32);  /* generous: vars + scratch for body/check */
    } else {
        J("    .limit locals 4\n");
    }
    /* cs dispatch — single clause, cs==0 only */
    J("    iload 0\n");
    J("    ifne p_%s_0_omega_empty\n", bridge_fn);

    if (has_true_expr && clause_expr) {
        /* true(Expr) opts: inline body + check in same frame so vars are shared.
         * Locals layout: 0=cs(I), 1=trail(I), 2..2+n_vars-1=Prolog vars */
        int n_vars = (int)clause_expr->ival;
        int n_args = (int)clause_expr->dval;
        int trail_local = 1;
        int vars_base   = 2;
        int *var_locals = (int*)calloc(n_vars + 1, sizeof(int));
        for (int vi = 0; vi < n_vars; vi++) var_locals[vi] = vars_base + vi;
        int next_local_val = vars_base + n_vars;
        int *next_local = &next_local_val;

        /* trail mark */
        J("    invokestatic %s/pj_trail_mark()I\n", pj_classname);
        J("    istore %d\n", trail_local);

        /* allocate fresh var cells for all Prolog variables */
        for (int vi = 0; vi < n_vars; vi++) {
            J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
            J("    astore %d\n", var_locals[vi]);
        }

        /* allocate scratch locals for cs bookkeeping */
        int init_cs_local    = (*next_local)++;
        int sub_cs_out_local = (*next_local)++;
        /* initialise them to 0 */
        J("    iconst_0\n"); J("    istore %d\n", init_cs_local);
        J("    iconst_0\n"); J("    istore %d\n", sub_cs_out_local);

        /* emit body goals (children[n_args..nchildren-1]) */
        int nbody = (int)clause_expr->nchildren - n_args;
        if (nbody > 0) {
            EXPR_t **body_goals = clause_expr->children + n_args;
            char lbl_body_ok[128], lbl_body_fail[128];
            snprintf(lbl_body_ok,   sizeof lbl_body_ok,   "p_%s_0_body_ok",   bridge_fn);
            snprintf(lbl_body_fail, sizeof lbl_body_fail, "p_%s_0_omega_fail", bridge_fn);
            pj_emit_body(body_goals, nbody,
                         lbl_body_ok, lbl_body_fail, lbl_body_fail,
                         trail_local, var_locals, n_vars, next_local,
                         init_cs_local, sub_cs_out_local,
                         /*cut_cs_seal=*/0, /*cs_local_for_cut=*/0,
                         lbl_body_fail, NULL);
            J("%s:\n", lbl_body_ok);
        }

        /* emit the true(Expr) check as a goal */
        {
            char lbl_check_ok[128], lbl_check_fail[128];
            snprintf(lbl_check_ok,   sizeof lbl_check_ok,   "p_%s_0_check_ok",   bridge_fn);
            snprintf(lbl_check_fail, sizeof lbl_check_fail, "p_%s_0_omega_fail",  bridge_fn);
            pj_emit_goal(true_expr_arg, lbl_check_ok, lbl_check_fail,
                         trail_local, var_locals, n_vars,
                         /*cut_cs_seal=*/0, /*cs_local_for_cut=*/0,
                         next_local, NULL);
            J("%s:\n", lbl_check_ok);
        }

        free(var_locals);

        /* check_ok: body+expr both succeeded — report pass, return null */
        J("    iconst_0\n");
        J("    invokestatic %s/p_pj_inc_pass_0(I)[Ljava/lang/Object;\n", pj_classname);
        J("    pop\n");
        J("    getstatic java/lang/System/out Ljava/io/PrintStream;\n");
        J("    ldc \"  pass: %s:%s\"\n", suite_name, test_name);
        J("    invokevirtual java/io/PrintStream/println(Ljava/lang/String;)V\n");
        J("    aconst_null\n");
        J("    areturn\n");
        J("p_%s_0_omega_fail:\n", bridge_fn);
        J("    iconst_0\n");
        J("    invokestatic %s/p_pj_inc_fail_0(I)[Ljava/lang/Object;\n", pj_classname);
        J("    pop\n");
        J("    getstatic java/lang/System/out Ljava/io/PrintStream;\n");
        J("    ldc \"  FAIL: %s:%s  (true-check failed)\"\n", suite_name, test_name);
        J("    invokevirtual java/io/PrintStream/println(Ljava/lang/String;)V\n");
        J("    aconst_null\n");
        J("    areturn\n");
        J("p_%s_0_omega_empty:\n", bridge_fn);
        J("    aconst_null\n");
        J("    areturn\n");
        J(".end method\n\n");
        return;

    } else if (arity == 2) {
        /* call test(name, opts) — emit as p_test_2(atom(name), opts_term, 0) */
        J("    ldc \"%s\"\n", test_name);
        J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
        /* opts: emit term if available, else [] */
        if (opts_expr) {
            int *dummy_locals = NULL;
            pj_emit_term(opts_expr, dummy_locals, 0);
        } else {
            J("    ldc \"[]\"\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
        }
        J("    iconst_0\n");
        J("    invokestatic %s/p_test_2([Ljava/lang/Object;[Ljava/lang/Object;I)[Ljava/lang/Object;\n", pj_classname);
    } else {
        /* call test(name) — emit as p_test_1(atom(name), 0) */
        J("    ldc \"%s\"\n", test_name);
        J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
        J("    iconst_0\n");
        J("    invokestatic %s/p_test_1([Ljava/lang/Object;I)[Ljava/lang/Object;\n", pj_classname);
    }

    /* if null → fail (result on stack) */
    J("    dup\n");
    J("    ifnull p_%s_0_omega_pop\n", bridge_fn);
    /* success: return the result array as-is (non-null means success) */
    J("    areturn\n");
    /* omega_pop: result (null) on stack — pop then return null */
    J("p_%s_0_omega_pop:\n", bridge_fn);
    J("    pop\n");
    J("    aconst_null\n");
    J("    areturn\n");
    /* omega_empty: nothing on stack */
    J("p_%s_0_omega_empty:\n", bridge_fn);
    J("    aconst_null\n");
    J("    areturn\n");
    J(".end method\n\n");
}

/* -------------------------------------------------------------------------
 * Linker test registration info (declared earlier as forward decl)
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * pj_linker_scan — walk program collecting suite/test info.
 * Must be called before pj_emit_main and before bridge emission.
 *
 * NOTE: prolog_lower() batches all E_CHOICE nodes separately from directives.
 * They are NOT interleaved in source order.  So we cannot track
 * begin_tests/end_tests windows while walking E_CHOICE nodes.
 *
 * Strategy:
 *   Pass 1 — collect suite names from begin_tests directives.
 *   Pass 2 — collect test/1 and test/2 E_CHOICE nodes; assign all to
 *             suite[0].  Correct for the universal case of one suite per file.
 * ------------------------------------------------------------------------- */
static void pj_linker_scan(Program *prog) {
    pj_linker_ntest  = 0;
    pj_linker_nsuite = 0;

    /* Pass 1: collect suite names from begin_tests directives */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        EXPR_t *g = s->subject;
        if (g->kind == E_CHOICE) continue;
        if (g->kind != E_FNC || !g->sval) continue;
        if (strcmp(g->sval, "begin_tests") != 0 || g->nchildren < 1) continue;
        EXPR_t *suite_arg = g->children[0];
        if (!suite_arg || !suite_arg->sval) continue;
        const char *sname = suite_arg->sval;
        int found = 0;
        for (int i = 0; i < pj_linker_nsuite; i++)
            if (strcmp(pj_linker_suites[i], sname) == 0) { found = 1; break; }
        if (!found && pj_linker_nsuite < 32)
            strncpy(pj_linker_suites[pj_linker_nsuite++], sname, 63);
    }

    if (pj_linker_nsuite == 0) return;

    /* Pass 2: collect test/1 and test/2 clause heads.
     * Suite assignment: use pj_prescan_map[] (populated by pj_linker_prescan
     * before PlProgram was freed) to recover source-order suite context.
     * Falls back to suite[0] if prescan map is empty (single-suite files). */
    int cnt1 = 0, cnt2 = 0;  /* per-key occurrence counters, match prescan order */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_CHOICE) continue;
        EXPR_t *g = s->subject;
        const char *key = g->sval;
        if (!key) continue;
        int is_test1 = (strcmp(key, "test/1") == 0);
        int is_test2 = (strcmp(key, "test/2") == 0);
        if (!is_test1 && !is_test2) continue;

        for (int ci = 0; ci < g->nchildren && pj_linker_ntest < PJ_LINKER_MAX_TESTS; ci++) {
            EXPR_t *cl = g->children[ci];
            if (!cl || cl->kind != E_CLAUSE) continue;
            int n_args = (int)cl->dval;
            if (n_args < 1) continue;
            EXPR_t *name_arg = cl->children[0];
            if (!name_arg || !name_arg->sval) continue;

            /* Determine suite: consult prescan map by key+occurrence index */
            int occ = is_test1 ? cnt1++ : cnt2++;
            const char *suite = pj_linker_nsuite > 0 ? pj_linker_suites[0] : "unknown";
            if (pj_prescan_n > 0) {
                for (int pi = 0; pi < pj_prescan_n; pi++) {
                    if (strcmp(pj_prescan_map[pi].key, key) == 0 &&
                        pj_prescan_map[pi].idx == occ) {
                        suite = pj_prescan_map[pi].suite;
                        break;
                    }
                }
            }

            PjTestInfo *ti = &pj_linker_tests[pj_linker_ntest++];
            strncpy(ti->suite, suite, 63);
            strncpy(ti->name,  name_arg->sval, 63);
            ti->arity       = is_test2 ? 2 : 1;
            ti->opts_expr   = (is_test2 && n_args >= 2) ? cl->children[1] : NULL;
            ti->clause_expr = cl;
            /* Count prior entries with same suite+name to form a unique bridge name */
            int dup = 0;
            for (int di = 0; di < pj_linker_ntest - 1; di++)
                if (strcmp(pj_linker_tests[di].suite, suite) == 0 &&
                    strcmp(pj_linker_tests[di].name,  name_arg->sval) == 0)
                    dup++;
            char raw[128];
            if (dup == 0)
                snprintf(raw, sizeof raw, "pjt__%s_%s", suite, name_arg->sval);
            else
                snprintf(raw, sizeof raw, "pjt__%s_%s_%d", suite, name_arg->sval, dup);
            pj_safe_name(raw, ti->bridge, sizeof ti->bridge);
        }
    }
}

/* -------------------------------------------------------------------------
 * pj_linker_emit_main_assertz — emit assertz calls for pj_suite/pj_test facts.
 * Called from inside pj_emit_main after the existing assertz loop.
 *
 * Each pj_suite(Suite) assertz:
 *   ldc "Suite" → pj_term_atom → pj_db_assert_key → pj_term_atom → iconst_0
 *   → pj_db_assert
 *
 * Each pj_test(Suite, Name, Opts, Goal) assertz:
 *   build compound ["compound","pj_test", atom(Suite), atom(Name), Opts, atom(bridge)]
 *   → pj_db_assert_key → same term → iconst_0 → pj_db_assert
 * ------------------------------------------------------------------------- */
static void pj_linker_emit_main_assertz(void) {
    if (pj_linker_ntest == 0 && pj_linker_nsuite == 0) return;

    JC("M-PJ-LINKER: assert pj_suite facts");
    for (int i = 0; i < pj_linker_nsuite; i++) {
        const char *sname = pj_linker_suites[i];
        /* build pj_suite(Suite) term — compound arity 1 */
        /* Emit twice: once for key derivation, once for the actual assert */
        for (int pass = 0; pass < 2; pass++) {
            J("    bipush 3\n");
            J("    anewarray java/lang/Object\n");
            J("    dup\n");
            J("    iconst_0\n");
            J("    ldc \"compound\"\n");
            J("    aastore\n");
            J("    dup\n");
            J("    iconst_1\n");
            J("    ldc \"pj_suite\"\n");
            J("    aastore\n");
            J("    dup\n");
            J("    bipush 2\n");
            J("    ldc \"%s\"\n", sname);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            J("    aastore\n");
            if (pass == 0) {
                J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            }
        }
        J("    iconst_0\n");  /* asserta=1 / assertz=0 */
        J("    invokestatic %s/pj_db_assert(Ljava/lang/String;Ljava/lang/Object;I)V\n", pj_classname);
    }

    JC("M-PJ-LINKER: assert pj_test facts");
    int *dummy_locals = NULL;
    for (int i = 0; i < pj_linker_ntest; i++) {
        PjTestInfo *ti = &pj_linker_tests[i];
        /* build pj_test(Suite, Name, Opts, BridgeAtom) — compound arity 4 */
        for (int pass = 0; pass < 2; pass++) {
            J("    bipush 6\n");  /* 2 + 4 args */
            J("    anewarray java/lang/Object\n");
            J("    dup\n");
            J("    iconst_0\n");
            J("    ldc \"compound\"\n");
            J("    aastore\n");
            J("    dup\n");
            J("    iconst_1\n");
            J("    ldc \"pj_test\"\n");
            J("    aastore\n");
            /* arg0: Suite atom */
            J("    dup\n");
            J("    bipush 2\n");
            J("    ldc \"%s\"\n", ti->suite);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            J("    aastore\n");
            /* arg1: Name atom */
            J("    dup\n");
            J("    bipush 3\n");
            J("    ldc \"%s\"\n", ti->name);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            J("    aastore\n");
            /* arg2: Opts — emit as term if available, else []
             * If opts contain true(Expr) (var-sharing case), emit pj_inline atom
             * so run_one delegates entirely to the self-reporting bridge. */
            J("    dup\n");
            J("    bipush 4\n");
            if (ti->opts_expr) {
                EXPR_t *oe = ti->opts_expr;
                /* Check for true(Expr) — bare or inside list */
                int is_true_expr = 0;
                if (oe->sval && strcmp(oe->sval,"true")==0 && oe->nchildren==1) {
                    is_true_expr = 1;
                } else if (oe->sval && strcmp(oe->sval,".") != 0 &&
                           strcmp(oe->sval,"[]") != 0 &&
                           strcmp(oe->sval,"fail") != 0 && strcmp(oe->sval,"false") != 0 &&
                           strcmp(oe->sval,"error") != 0 && strcmp(oe->sval,"throws") != 0 &&
                           strcmp(oe->sval,"all") != 0 && strcmp(oe->sval,"sto") != 0 &&
                           strcmp(oe->sval,"blocked") != 0 && strcmp(oe->sval,"setup") != 0 &&
                           strcmp(oe->sval,"cleanup") != 0 && strcmp(oe->sval,"forall") != 0 &&
                           oe->nchildren > 0) {
                    is_true_expr = 1; /* bare expression like X==y */
                } else if (oe->sval && strcmp(oe->sval,".")==0 && oe->nchildren>=2) {
                    EXPR_t *nd = oe;
                    while (nd && nd->sval && strcmp(nd->sval,".")==0 && nd->nchildren>=2) {
                        EXPR_t *hd = nd->children[0];
                        if (hd && hd->sval && strcmp(hd->sval,"true")==0 && hd->nchildren==1)
                            { is_true_expr=1; break; }
                        /* bare expression inside list */
                        if (hd && hd->sval &&
                            strcmp(hd->sval,"fail")!=0 && strcmp(hd->sval,"false")!=0 &&
                            strcmp(hd->sval,"error")!=0 && strcmp(hd->sval,"throws")!=0 &&
                            strcmp(hd->sval,"all")!=0 && strcmp(hd->sval,"sto")!=0 &&
                            strcmp(hd->sval,"blocked")!=0 && strcmp(hd->sval,"setup")!=0 &&
                            strcmp(hd->sval,"cleanup")!=0 && strcmp(hd->sval,"forall")!=0 &&
                            strcmp(hd->sval,".")!=0 && strcmp(hd->sval,"[]")!=0 &&
                            hd->nchildren > 0)
                            { is_true_expr=1; break; }
                        nd = nd->children[1];
                    }
                }
                if (is_true_expr) {
                    /* emit pj_inline atom — bridge is self-reporting.
                     * For bare goals (X==y) this is essential: the check
                     * shares variables with the body only inside the bridge
                     * JVM frame, so we cannot re-evaluate the expression
                     * from a disconnected assertz'd term. */
                    J("    ldc \"pj_inline\"\n");
                    J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
                } else {
                    pj_emit_term(oe, dummy_locals, 0);
                }
            } else {
                /* [] */
                J("    ldc \"[]\"\n");
                J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            }
            J("    aastore\n");
            /* arg3: Goal — atom naming the bridge predicate */
            J("    dup\n");
            J("    bipush 5\n");
            J("    ldc \"%s\"\n", ti->bridge);
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            J("    aastore\n");
            if (pass == 0) {
                J("    invokestatic %s/pj_db_assert_key(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            }
        }
        J("    iconst_0\n");
        J("    invokestatic %s/pj_db_assert(Ljava/lang/String;Ljava/lang/Object;I)V\n", pj_classname);
    }
}

/* -------------------------------------------------------------------------
 * Main method emitter
 * Finds the initialization(main) directive and emits a JVM main() that calls it.
 * ------------------------------------------------------------------------- */

static void pj_emit_main(Program *prog) {
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 512\n");
    J("    .limit locals 4\n");

    /* M-PJ-LINKER: assert pj_suite/pj_test facts FIRST — before any directives
     * (run_tests/:- halt may appear as directives and must find the DB populated) */
    pj_linker_emit_main_assertz();

    /* Bug 2 fix: execute :- assertz/asserta directives before calling main/0.
     * Directives are STMT_t nodes whose subject is NOT E_CHOICE. */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE) continue;
        EXPR_t *g = s->subject;
        if (g->kind != E_FNC || !g->sval) continue;
        /* Skip meta-directives that have no runtime effect */
        if (strcmp(g->sval, "dynamic") == 0 ||
            strcmp(g->sval, "discontiguous") == 0 ||
            strcmp(g->sval, "module") == 0 ||
            strcmp(g->sval, "use_module") == 0 ||
            strcmp(g->sval, "begin_tests") == 0 ||
            strcmp(g->sval, "end_tests") == 0 ||
            strcmp(g->sval, "ensure_loaded") == 0 ||
            strcmp(g->sval, "style_check") == 0 ||
            strcmp(g->sval, "set_prolog_flag") == 0 ||
            strcmp(g->sval, "module_transparent") == 0 ||
            strcmp(g->sval, "meta_predicate") == 0 ||
            strcmp(g->sval, "multifile") == 0 ||
            strcmp(g->sval, "reexport") == 0 ||
            strcmp(g->sval, "load_files") == 0 ||
            strcmp(g->sval, "if") == 0 ||
            strcmp(g->sval, "else") == 0 ||
            strcmp(g->sval, "endif") == 0 ||
            strcmp(g->sval, "initialization") == 0) {
            /* silently skip */
        } else if ((strcmp(g->sval, "assertz") == 0 || strcmp(g->sval, "asserta") == 0)
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
        } else {
            /* General directive: call via pj_call_goal at runtime */
            int *dummy_locals = NULL;
            pj_emit_term(s->subject, dummy_locals, 0);
            J("    iconst_0\n");
            J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
            J("    pop\n");
        }
    }

    /* Call p_main_0(0) once — only if main/0 is actually defined.
     * Plunit files use :- run_tests / :- halt directives and have no main/0. */
    int has_main0 = 0;
    for (STMT_t *s2 = prog->head; s2; s2 = s2->next) {
        if (!s2->subject || s2->subject->kind != E_CHOICE) continue;
        if (s2->subject->sval && strcmp(s2->subject->sval, "main/0") == 0) {
            has_main0 = 1; break;
        }
    }
    if (has_main0) {
        JI("iconst_0", ""); /* cs = 0 */
        J("    invokestatic %s/p_main_0(I)[Ljava/lang/Object;\n", pj_classname);
        JI("pop", "");
    }

    /* Auto run_tests: if this is a plunit file with no main/0 and no explicit
     * :- run_tests directive, emit run_tests + halt automatically so plunit-only
     * files (e.g. SWI test_*.pl) execute without needing a wrapper. */
    if (pj_linker_ntest > 0 && !has_main0) {
        /* Check whether :- run_tests was already emitted as a directive */
        int has_run_tests_dir = 0;
        for (STMT_t *s2 = prog->head; s2; s2 = s2->next) {
            if (!s2->subject || s2->subject->kind == E_CHOICE) continue;
            EXPR_t *g = s2->subject;
            if (g->kind == E_FNC && g->sval &&
                (strcmp(g->sval, "run_tests") == 0)) {
                has_run_tests_dir = 1; break;
            }
        }
        if (!has_run_tests_dir) {
            JC("auto run_tests (M-PJ-LINKER: no main/0, no :- run_tests)");
            J("    ldc \"run_tests\"\n");
            J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
            J("    iconst_0\n");
            J("    invokestatic %s/pj_call_goal(Ljava/lang/Object;I)I\n", pj_classname);
            J("    pop\n");
        }
    }
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

    /* Always emit forall/2 synthetic (unless user defines it) */
    {
        int defines_forall = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            if (s->subject->kind == E_CHOICE && s->subject->sval &&
                strcmp(s->subject->sval, "forall") == 0)
                defines_forall = 1;
        }
        if (!defines_forall)
            pj_emit_forall_builtin();
    }

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

    /* Check if program uses aggregate_all/3 but doesn't define it */
    {
        int defines_agg = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            if (s->subject->kind == E_CHOICE && s->subject->sval &&
                strcmp(s->subject->sval, "aggregate_all") == 0)
                defines_agg = 1;
        }
        if (!defines_agg)
            pj_emit_aggregate_all_builtin();
    }

    /* Check if program uses reverse/2 but doesn't define it — emit synthetic method */
    {
        int defines_reverse = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject) continue;
            if (s->subject->kind == E_CHOICE && s->subject->sval &&
                strcmp(s->subject->sval, "reverse") == 0)
                defines_reverse = 1;
        }
        if (!defines_reverse)
            pj_emit_reverse_builtin();
    }

    /* M-PJ-LINKER: plunit linker — scan + emit shim + stubs + bridges */
    int use_plunit = pj_linker_has_plunit(prog);
    if (use_plunit) {
        pj_linker_scan(prog);
        pj_linker_emit_plunit_shim();
        pj_linker_emit_dynamic_stubs();
    }

    /* Always emit pj_gcd helper (used by gcd/2 in is/2) */
    pj_emit_gcd_helper();

    /* pj_shr(J val, J count) -> J: arithmetic right shift with saturation (count>=63 -> 0 or -1) */
    J(".method static pj_shr(JJ)J\n");
    J("    .limit stack 4\n"); J("    .limit locals 4\n");
    J("    lload_2\n"); J("    ldc2_w 63\n"); J("    lcmp\n"); J("    ifle pj_shr_ok\n");
    /* count > 63: result is sign extension of val */
    J("    lload_0\n"); J("    ldc2_w 63\n"); J("    l2i\n"); J("    lshr\n"); J("    lreturn\n");
    J("pj_shr_ok:\n");
    J("    lload_0\n"); J("    lload_2\n"); J("    l2i\n"); J("    lshr\n"); J("    lreturn\n");
    J(".end method\n\n");

    /* pj_shl(J val, J count) -> J: left shift with saturation (count>=64 -> 0) */
    J(".method static pj_shl(JJ)J\n");
    J("    .limit stack 4\n"); J("    .limit locals 4\n");
    J("    lload_2\n"); J("    ldc2_w 63\n"); J("    lcmp\n"); J("    ifle pj_shl_ok\n");
    J("    lconst_0\n"); J("    lreturn\n");
    J("pj_shl_ok:\n");
    J("    lload_0\n"); J("    lload_2\n"); J("    l2i\n"); J("    lshl\n"); J("    lreturn\n");
    J(".end method\n\n");

    /* pj_mod(J a, J b) -> J: floor remainder (SWI mod/2 semantics)
     * r = a rem b; if (r != 0 && signs differ) r += b */
    J(".method static pj_mod(JJ)J\n");
    J("    .limit stack 6\n"); J("    .limit locals 6\n");
    J("    lload_0\n"); J("    lload_2\n"); J("    lrem\n"); J("    lstore 4\n"); /* r = a%b, local4 */
    /* if r == 0, return 0 */
    J("    lload 4\n"); J("    lconst_0\n"); J("    lcmp\n"); J("    ifeq pj_mod_done\n");
    /* if (r XOR b) >= 0 (same sign), return r */
    J("    lload 4\n"); J("    lload_2\n"); J("    lxor\n");
    J("    lconst_0\n"); J("    lcmp\n"); J("    ifge pj_mod_done\n");
    /* different signs: r += b */
    J("    lload 4\n"); J("    lload_2\n"); J("    ladd\n"); J("    lstore 4\n");
    J("pj_mod_done:\n"); J("    lload 4\n"); J("    lreturn\n");
    J(".end method\n\n");

    /* Hyperbolic inverse function helpers (not in java.lang.Math pre-Java 21) */
    J(".method static pj_asinh(D)D\n");
    J("    .limit stack 6\n"); J("    .limit locals 2\n");
    J("    dload_0\n");                                     /* x */
    J("    dload_0\n"); J("    dload_0\n"); J("    dmul\n");/* x x² */
    J("    dconst_1\n"); J("    dadd\n");                   /* x (x²+1) */
    J("    invokestatic java/lang/Math/sqrt(D)D\n");        /* x sqrt(x²+1) */
    J("    dadd\n");                                        /* x+sqrt(x²+1) */
    J("    invokestatic java/lang/Math/log(D)D\n");
    J("    dreturn\n"); J(".end method\n\n");

    J(".method static pj_acosh(D)D\n");
    J("    .limit stack 6\n"); J("    .limit locals 2\n");
    J("    dload_0\n");
    J("    dload_0\n"); J("    dload_0\n"); J("    dmul\n");
    J("    dconst_1\n"); J("    dsub\n");
    J("    invokestatic java/lang/Math/sqrt(D)D\n");
    J("    dadd\n");
    J("    invokestatic java/lang/Math/log(D)D\n");
    J("    dreturn\n"); J(".end method\n\n");

    J(".method static pj_atanh(D)D\n");
    J("    .limit stack 6\n"); J("    .limit locals 2\n");
    /* 0.5 * log((1+x)/(1-x)) */
    J("    dconst_1\n"); J("    dload_0\n"); J("    dadd\n"); /* (1+x) */
    J("    dconst_1\n"); J("    dload_0\n"); J("    dsub\n"); /* (1-x) */
    J("    ddiv\n");                                           /* (1+x)/(1-x) */
    J("    invokestatic java/lang/Math/log(D)D\n");
    J("    ldc2_w 4602678819172646912\n");                    /* 0.5 bits */
    J("    invokestatic java/lang/Double/longBitsToDouble(J)D\n");
    J("    dmul\n");
    J("    dreturn\n"); J(".end method\n\n");

    /* pj_num_as_double(Object[] term) → D: extract numeric value as double.
     * Handles both "int" and "float" tagged terms. */
    J(".method static pj_num_as_double([Ljava/lang/Object;)D\n");
    J("    .limit stack 4\n");
    J("    .limit locals 1\n");
    J("    aload_0\n");
    J("    iconst_0\n"); J("    aaload\n");
    J("    ldc \"float\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_num_as_double_int\n");
    /* float: parse as double */
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Double/parseDouble(Ljava/lang/String;)D\n");
    J("    dreturn\n");
    J("pj_num_as_double_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    l2d\n");
    J("    dreturn\n");
    J(".end method\n\n");

    /* pj_num_cmp(Object[] a, Object[] b) → I: numeric comparison.
     * Returns negative/zero/positive like Double.compare.
     * Used by =:= =\= < > =< >= when operands may be runtime-typed vars. */
    J(".method static pj_num_cmp([Ljava/lang/Object;[Ljava/lang/Object;)I\n");
    J("    .limit stack 6\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_num_as_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_num_as_double([Ljava/lang/Object;)D\n", pj_classname);
    /* Use dcmpl for IEEE semantics: -0.0 == 0.0, NaN comparisons return -1 */
    J("    dcmpl\n");
    J("    ireturn\n");
    J(".end method\n\n");

    /* Always emit pj_min_mixed / pj_max_mixed helpers:
     * pj_min_mixed(J intVal, J floatBits) -> Object[] term (int or float)
     * pj_max_mixed(J intVal, J floatBits) -> Object[] term (int or float)
     * Result type follows the winning value (int if int wins, float if float wins) */
    J(".method static pj_min_mixed(JJ)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");
    /* convert float bits to double, compare with int */
    J("    lload_0\n"); J("    l2d\n");      /* (double)intVal */
    J("    lload_2\n");
    J("    invokestatic java/lang/Double/longBitsToDouble(J)D\n");
    J("    dcmpl\n");                          /* intAsDouble cmp floatVal */
    J("    ifgt pj_min_mixed_float\n");       /* int > float → float wins */
    /* int wins (int <= float): return int term */
    J("    lload_0\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_min_mixed_float:\n");
    /* float wins: return float term */
    J("    lload_2\n");
    J("    invokestatic java/lang/Double/longBitsToDouble(J)D\n");
    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J(".end method\n\n");

    J(".method static pj_max_mixed(JJ)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 4\n");
    J("    lload_0\n"); J("    l2d\n");
    J("    lload_2\n");
    J("    invokestatic java/lang/Double/longBitsToDouble(J)D\n");
    J("    dcmpl\n");
    J("    iflt pj_max_mixed_float\n");       /* int < float → float wins */
    /* int wins (int >= float): return int term */
    J("    lload_0\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_max_mixed_float:\n");
    J("    lload_2\n");
    J("    invokestatic java/lang/Double/longBitsToDouble(J)D\n");
    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J(".end method\n\n");

    /* Always emit stdlib shim: member/2, memberchk/2 (skipped if user-defined) */
    /* pj_varnum helpers: runtime arithmetic when one operand may be a var holding float.
     * Each takes two Object[] terms (already deref'd), returns long bits
     * (float raw bits if result is float, else raw long). */
    J(".method static pj_obj_to_double([Ljava/lang/Object;)D\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    iconst_0\n"); J("    aaload\n");
    J("    ldc \"float\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_o2d_int\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Double/parseDouble(Ljava/lang/String;)D\n");
    J("    dreturn\n");
    J("pj_o2d_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    l2d\n");
    J("    dreturn\n");
    J(".end method\n\n");

    J(".method static pj_obj_is_float([Ljava/lang/Object;)Z\n");
    J("    .limit stack 3\n");
    J("    .limit locals 1\n");
    J("    aload_0\n"); J("    iconst_0\n"); J("    aaload\n");
    J("    ldc \"float\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ireturn\n");
    J(".end method\n\n");

    /* pj_varnum_mul(Object[] a, Object[] b) -> J */
    J(".method static pj_varnum_mul([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    ior\n");
    J("    ifeq pj_vn_mul_int\n");
    /* float path */
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    dmul\n");
    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_vn_mul_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    lmul\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J(".end method\n\n");

    /* pj_varnum_add(Object[] a, Object[] b) -> J */
    J(".method static pj_varnum_add([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    ior\n");
    J("    ifeq pj_vn_add_int\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    dadd\n");
    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_vn_add_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    ladd\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J(".end method\n\n");

    /* pj_varnum_sub(Object[] a, Object[] b) -> J */
    J(".method static pj_varnum_sub([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    ior\n");
    J("    ifeq pj_vn_sub_int\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    dsub\n");
    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_vn_sub_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    lsub\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J(".end method\n\n");

    /* pj_varnum_div(Object[] a, Object[] b) -> J */
    J(".method static pj_varnum_div([Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;\n");
    J("    .limit stack 8\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_is_float([Ljava/lang/Object;)Z\n", pj_classname);
    J("    ior\n");
    J("    ifeq pj_vn_div_int\n");
    J("    aload_0\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    aload_1\n");
    J("    invokestatic %s/pj_obj_to_double([Ljava/lang/Object;)D\n", pj_classname);
    J("    ddiv\n");
    J("    invokestatic %s/pj_term_float(D)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J("pj_vn_div_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    aload_1\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    ldiv\n");
    J("    invokestatic %s/pj_term_int(J)[Ljava/lang/Object;\n", pj_classname);
    J("    areturn\n");
    J(".end method\n\n");

    /* pj_obj_to_bits: extract J from Object[] term (raw long or float bits) */
    J(".method static pj_obj_to_bits([Ljava/lang/Object;)J\n");
    J("    .limit stack 4\n");
    J("    .limit locals 1\n");
    J("    aload_0\n");
    J("    iconst_0\n"); J("    aaload\n");
    J("    ldc \"float\"\n");
    J("    invokevirtual java/lang/Object/equals(Ljava/lang/Object;)Z\n");
    J("    ifeq pj_otb_int\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Double/parseDouble(Ljava/lang/String;)D\n");
    J("    invokestatic java/lang/Double/doubleToRawLongBits(D)J\n");
    J("    lreturn\n");
    J("pj_otb_int:\n");
    J("    aload_0\n"); J("    iconst_1\n"); J("    aaload\n");
    J("    checkcast java/lang/String\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("    lreturn\n");
    J(".end method\n\n");

    pj_emit_stdlib_shim(prog);

    /* emit each predicate */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->subject->kind == E_CHOICE)
            pj_emit_choice(s->subject);
    }

    /* M-PJ-LINKER: emit bridge predicates suite_name/0 :- test(name). */
    if (use_plunit) {
        for (int i = 0; i < pj_linker_ntest; i++)
            pj_linker_emit_bridge(pj_linker_tests[i].bridge, pj_linker_tests[i].name, pj_linker_tests[i].suite);
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
            /* return proper continuation array: Object[1+dyn_arity]
             * [0] = Integer(idx_loc + 1),  [1..N] = args (unified in-place) */
            J("    ldc %d\n", dyn_arity + 1);
            JI("anewarray", "java/lang/Object");
            JI("dup", "");
            JI("iconst_0", "");
            J("    iload %d\n", idx_loc);
            JI("iconst_1", "");
            JI("iadd", "");
            JI("invokestatic", "java/lang/Integer/valueOf(I)Ljava/lang/Integer;");
            JI("aastore", "");
            for (int ai = 0; ai < dyn_arity; ai++) {
                JI("dup", "");
                J("    ldc %d\n", ai + 1);
                J("    aload %d\n", ai);
                JI("aastore", "");
            }
            JI("areturn", "");
            J("%s:\n", sb_miss);
            JI("aconst_null", "");
            JI("areturn", "");
            J(".end method\n\n");
        }
    }

    /* -----------------------------------------------------------------------
     * M-LINK-JVM: EXPORT wrappers for Prolog predicates.
     * ----------------------------------------------------------------------- */
    if (prog && prog->exports) {
        typedef struct { char name[64]; int arity; } PjExportArity;
        #define PJ_EXPORT_MAX 64
        PjExportArity pj_ea[PJ_EXPORT_MAX];
        int pj_nea = 0;
        for (STMT_t *s = prog->head; s; s = s->next) {
            if (!s->subject || s->subject->kind != E_CHOICE) continue;
            if (!s->subject->sval) continue;
            const char *sv = s->subject->sval;
            const char *slash = strchr(sv, '/');
            if (!slash) continue;
            int ar = atoi(slash + 1);
            int namelen = (int)(slash - sv);
            if (namelen <= 0 || namelen >= 64) continue;
            char fn[64]; strncpy(fn, sv, namelen); fn[namelen] = '\0';
            int found = 0;
            for (int k = 0; k < pj_nea; k++)
                if (strcmp(pj_ea[k].name, fn) == 0) { found = 1; break; }
            if (!found && pj_nea < PJ_EXPORT_MAX) {
                strncpy(pj_ea[pj_nea].name, fn, 63);
                pj_ea[pj_nea].arity = ar;
                pj_nea++;
            }
        }

        for (ExportEntry *e = prog->exports; e; e = e->next) {
            int arity = -1;
            char lname[64]; strncpy(lname, e->name, 63); lname[63] = '\0';
            for (int k = 0; lname[k]; k++) lname[k] = (char)tolower((unsigned char)lname[k]);
            for (int k = 0; k < pj_nea; k++) {
                char kl[64]; strncpy(kl, pj_ea[k].name, 63); kl[63] = '\0';
                for (int j = 0; kl[j]; j++) kl[j] = (char)tolower((unsigned char)kl[j]);
                if (strcmp(kl, lname) == 0) { arity = pj_ea[k].arity; break; }
            }
            if (arity < 0) {
                J("; EXPORT %s — predicate not found, skipping\n", e->name);
                continue;
            }

            J("\n; EXPORT wrapper for %s/%d (M-LINK-JVM)\n", e->name, arity);
            J(".method public static %s([Ljava/lang/Object;Ljava/lang/Runnable;Ljava/lang/Runnable;)V\n", e->name);
            J("    .limit stack 16\n");
            J("    .limit locals %d\n", 4 + arity);

            /* Each arg: if args non-null and args[a] non-null → atom; else fresh var */
            for (int a = 0; a < arity; a++) {
                char ok[64], var[64], done[64];
                snprintf(ok,   sizeof ok,   "pj_exp_%s_a%d_ok",   e->name, a);
                snprintf(var,  sizeof var,  "pj_exp_%s_a%d_var",  e->name, a);
                snprintf(done, sizeof done, "pj_exp_%s_a%d_done", e->name, a);
                J("    aload_0\n"); J("    ifnull %s\n", var);
                J("    aload_0\n"); J("    ldc %d\n", a); J("    aaload\n");
                J("    ifnull %s\n", var);
                J("    aload_0\n"); J("    ldc %d\n", a); J("    aaload\n");
                J("    checkcast java/lang/String\n");
                J("    invokestatic %s/pj_term_atom(Ljava/lang/String;)[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", 4 + a);
                J("    goto %s\n", done);
                J("%s:\n", var);
                J("    invokestatic %s/pj_term_var()[Ljava/lang/Object;\n", pj_classname);
                J("    astore %d\n", 4 + a);
                J("%s:\n", done);
            }

            /* Call p_name_arity(arg0, ..., 0) */
            char internal[128];
            snprintf(internal, sizeof internal, "p_%s_%d", lname, arity);
            char desc[512]; int dp = 0;
            for (int a = 0; a < arity; a++) {
                const char *s2 = "[Ljava/lang/Object;";
                for (const char *p = s2; *p; p++) desc[dp++] = *p;
            }
            desc[dp++] = 'I'; desc[dp++] = ')';
            const char *rs = "[Ljava/lang/Object;";
            for (const char *p = rs; *p; p++) desc[dp++] = *p;
            desc[dp] = '\0';
            for (int a = 0; a < arity; a++) J("    aload %d\n", 4 + a);
            J("    ldc 0\n");
            J("    invokestatic %s/%s(%s\n", pj_classname, internal, desc);
            J("    astore_3\n");

            J("    aload_3\n"); J("    ifnull pj_exp_omega_%s\n", e->name);
            J("    aload_3\n");
            J("    invokestatic %s/pj_term_str(Ljava/lang/Object;)Ljava/lang/String;\n", pj_classname);
            J("    getstatic ByrdBoxLinkage/RESULT Ljava/util/concurrent/atomic/AtomicReference;\n");
            J("    swap\n");
            J("    invokevirtual java/util/concurrent/atomic/AtomicReference/set(Ljava/lang/Object;)V\n");
            J("    aload_1\n"); J("    ifnull pj_exp_done_%s\n", e->name);
            J("    aload_1\n"); J("    invokeinterface java/lang/Runnable/run()V 1\n");
            J("    goto pj_exp_done_%s\n", e->name);
            J("pj_exp_omega_%s:\n", e->name);
            J("    aload_2\n"); J("    ifnull pj_exp_done_%s\n", e->name);
            J("    aload_2\n"); J("    invokeinterface java/lang/Runnable/run()V 1\n");
            J("pj_exp_done_%s:\n", e->name);
            J("    return\n");
            J(".end method\n");
        }
    }

    pj_emit_main(prog);
}
