/*
 * emit_byrd_jvm.c — JVM Jasmin text emitter for sno2c
 *
 * Consumes the same Program* IR as emit_byrd_asm.c.
 * Emits Jasmin assembler text (.j files) assembled by jasmin.jar.
 *
 * Pipeline:
 *   sno2c -jvm prog.sno > prog.j
 *   java -jar jasmin.jar prog.j -d outdir/
 *   java -cp outdir/ <ClassName>
 *
 * Sprint map:
 *   J0  M-JVM-HELLO   — skeleton: null program → exit 0          ✓
 *   J1  M-JVM-LIT     — OUTPUT = 'hello' / arith / concat        ← NOW
 *   J2  M-JVM-ASSIGN  — variable assign + arithmetic
 *   J3  M-JVM-GOTO    — :S/:F branching
 *   J4  M-JVM-PATTERN — Byrd boxes: LIT/SEQ/ALT/ARBNO
 *   J5  M-JVM-CAPTURE — . and $ capture
 *   J-R1..J-R5        — corpus ladder 106/106
 *
 * Design:
 *   Each SNOBOL4 program becomes one public JVM class with a
 *   public static main([Ljava/lang/String;)V method.
 *   Class name is derived from the source filename (or "SnobolProg"
 *   if reading stdin).
 *
 *   Three-column Jasmin layout mirrors the NASM backend:
 *     label:       instruction    operands
 *
 *   Value representation (J1):
 *     All SNOBOL4 values on the JVM stack are java/lang/String.
 *     Integers and reals are stored as their decimal string representation
 *     and parsed back when arithmetic is needed.  This matches CSNOBOL4
 *     string-numeric duality.
 *
 *   null/failure is represented as Java null (aconst_null).
 *   Success leaves a non-null String on the stack.
 *   :S/:F goto uses ifnull / ifnonnull — mirrors JCON bc_conditional_transfer_to.
 *
 * Variable storage (J1):
 *   Each SNOBOL4 variable becomes a static String field on the class.
 *   Field name: sno_var_<VARNAME> (uppercase preserved).
 *   OUTPUT is special: assignment calls System.out.println.
 *
 * Arithmetic (J1):
 *   Push both sides as strings → parse to double → operate → format back.
 *   Helper: sno_arith(String a, String op, String b) → String
 *   Emitted inline as a call to a static helper method on the class.
 *
 * Reference:
 *   JCON gen_bc.icn  — Icon→JVM blueprint (α/β/γ/ω → four Labels per node)
 *   emit_byrd_asm.c  — direct structural oracle (same IR, same corpus)
 *   Jasmin docs      — http://jasmin.sourceforge.net/
 */

#include "sno2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -----------------------------------------------------------------------
 * Output helpers — three-column layout
 * ----------------------------------------------------------------------- */

static FILE *jvm_out;

/* J(fmt, ...) — emit raw text (no column management) */
static void J(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(jvm_out, fmt, ap);
    va_end(ap);
    /* update col tracker for simple cases */
    /* (not perfect for all fmt strings, but adequate) */
}

/* JIR(instr, ops) — instruction with no label, raw (no column wrap) */
static void JIR(const char *instr, const char *ops) {
    if (ops && ops[0])
        J("    %s %s\n", instr, ops);
    else
        J("    %s\n", instr);
}

/* JI — alias for JIR */
static void JI(const char *instr, const char *ops) { JIR(instr, ops); }

/* JL(label, instr, ops) — label + instruction, no column wrapping */
static void JL(const char *label, const char *instr, const char *ops) {
    if (label && label[0]) J("%s:\n", label);
    JIR(instr, ops);
}

/* JC(comment) — comment line */
static void JC(const char *comment) {
    J("; %s\n", comment);
}

/* JSep — visual separator between SNOBOL4 statements */
static void JSep(const char *tag) {
    J("; === %s ", tag ? tag : "");
    int used = 7 + (tag ? (int)strlen(tag) + 1 : 0);
    for (int i = used; i < 72; i++) fputc('=', jvm_out);
    J("\n");
}

/* -----------------------------------------------------------------------
 * Class name derivation from filename
 * ----------------------------------------------------------------------- */

static char jvm_classname[256];

static void jvm_set_classname(const char *filename) {
    if (!filename || strcmp(filename, "<stdin>") == 0) {
        strcpy(jvm_classname, "SnobolProg");
        return;
    }
    /* strip directory */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    /* strip extension */
    char buf[256];
    strncpy(buf, base, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    char *dot = strrchr(buf, '.');
    if (dot) *dot = '\0';
    /* sanitize: replace non-alnum with _ */
    char *p = buf;
    if (!isalpha((unsigned char)*p) && *p != '_') *p = '_';
    for (; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    /* capitalise first letter (Java class convention) */
    buf[0] = (char)toupper((unsigned char)buf[0]);
    strncpy(jvm_classname, buf, sizeof jvm_classname - 1);
}

/* -----------------------------------------------------------------------
 * Variable name registry — collect all SNOBOL4 variables for static fields
 * ----------------------------------------------------------------------- */

#define MAX_VARS 512
static char *jvm_vars[MAX_VARS];
static int   jvm_nvar = 0;

static void jvm_var_register(const char *name) {
    if (!name) return;
    if (strcasecmp(name, "OUTPUT") == 0) return;
    if (strcasecmp(name, "INPUT")  == 0) return;
    for (int i = 0; i < jvm_nvar; i++)
        if (strcasecmp(jvm_vars[i], name) == 0) return;
    if (jvm_nvar < MAX_VARS)
        jvm_vars[jvm_nvar++] = strdup(name);
}

static void jvm_collect_vars_expr(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_VART && e->sval)
        jvm_var_register(e->sval);
    jvm_collect_vars_expr(e->left);
    jvm_collect_vars_expr(e->right);
    if (e->args)
        for (int i = 0; e->args[i]; i++)
            jvm_collect_vars_expr(e->args[i]);
}

static void jvm_collect_vars(Program *prog) {
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->subject) {
            /* Only register plain variable names as fields */
            if (s->subject->kind == E_VART && s->subject->sval)
                jvm_var_register(s->subject->sval);
        }
        jvm_collect_vars_expr(s->pattern);
        jvm_collect_vars_expr(s->replacement);
    }
}

/* Safe field name: uppercase var name, prefix sno_var_ */
static void jvm_field_name(const char *varname, char *buf, int bufsz) {
    snprintf(buf, bufsz, "sno_var_%s", varname);
}

/* -----------------------------------------------------------------------
 * String escaping for Jasmin ldc operand
 * ----------------------------------------------------------------------- */

/* Escape a C string for use in a Jasmin ldc "..." literal.
 * Jasmin accepts standard Java string escapes inside double quotes. */
static void jvm_escape_string(const char *s, char *buf, int bufsz) {
    int out = 0;
    buf[out++] = '"';
    for (; *s && out < bufsz - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if      (c == '"')  { buf[out++] = '\\'; buf[out++] = '"'; }
        else if (c == '\\') { buf[out++] = '\\'; buf[out++] = '\\'; }
        else if (c == '\n') { buf[out++] = '\\'; buf[out++] = 'n'; }
        else if (c == '\r') { buf[out++] = '\\'; buf[out++] = 'r'; }
        else if (c == '\t') { buf[out++] = '\\'; buf[out++] = 't'; }
        else if (c < 0x20 || c > 0x7E) {
            out += snprintf(buf + out, bufsz - out - 2, "\\u%04X", c);
        } else {
            buf[out++] = (char)c;
        }
    }
    buf[out++] = '"';
    buf[out] = '\0';
}

/* -----------------------------------------------------------------------
 * Expression emitter — pushes a java/lang/String on the operand stack
 * Stack discipline: each call leaves exactly one String reference on top.
 * null represents SNOBOL4 failure / unset variable.
 * ----------------------------------------------------------------------- */

static void jvm_emit_expr(EXPR_t *e);

/* jvm_emit_to_double — safe numeric coercion helper.
 * Pops String, pushes double. Empty string/null → 0.0 (SNOBOL4 semantics). */
static int jvm_need_sno_parse_helper = 0;
static int jvm_need_input_helper = 0;

static void jvm_emit_to_double(void) {
    /* Stack: String → double.  Empty/null → 0.0 */
    jvm_need_sno_parse_helper = 1;
    char desc[512];
    snprintf(desc, sizeof desc, "%s/sno_to_double(Ljava/lang/String;)D", jvm_classname);
    JI("invokestatic", desc);
}

static void jvm_emit_parse_helper(void) {
    if (!jvm_need_sno_parse_helper) return;
    J(".method static sno_to_double(Ljava/lang/String;)D\n");
    J("    .limit stack 4\n");
    J("    .limit locals 1\n");
    J("    aload_0\n");
    J("    ifnull Lsno_pd_zero\n");
    J("    aload_0\n");
    J("    invokevirtual java/lang/String/trim()Ljava/lang/String;\n");
    J("    invokevirtual java/lang/String/isEmpty()Z\n");
    J("    ifne Lsno_pd_zero\n");
    J("    aload_0\n");
    J("    invokestatic java/lang/Double/parseDouble(Ljava/lang/String;)D\n");
    J("    dreturn\n");
    J("Lsno_pd_zero:\n");
    J("    dconst_0\n");
    J("    dreturn\n");
    J(".end method\n\n");
    jvm_need_sno_parse_helper = 0;
}

/* jvm_d2sno — convert double on stack to SNOBOL4 string */
static void jvm_d2sno(void) {
    /* Stack: ..., double → ..., String */
    JI("invokestatic", "java/lang/Double/toString(D)Ljava/lang/String;");
}

/* Push the SNOBOL4 numeric string for an integer on stack as long */
static void jvm_l2sno(void) {
    JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
}

static void jvm_emit_expr(EXPR_t *e) {
    if (!e) {
        /* null = unset */
        JI("ldc", "\"\"");
        return;
    }
    switch (e->kind) {
    case E_QLIT: {
        /* String literal */
        char buf[4096];
        jvm_escape_string(e->sval ? e->sval : "", buf, sizeof buf);
        JI("ldc", buf);
        break;
    }
    case E_ILIT: {
        /* Integer literal — push as String */
        char buf[64];
        snprintf(buf, sizeof buf, "%ld", e->ival);
        char esc[80];
        jvm_escape_string(buf, esc, sizeof esc);
        JI("ldc", esc);
        break;
    }
    case E_FLIT: {
        /* Real literal — CSNOBOL4 format: trailing dot for whole numbers */
        char buf[64];
        /* Match CSNOBOL4: if value is whole, emit "N." */
        if (e->dval == (long)e->dval)
            snprintf(buf, sizeof buf, "%ld.", (long)e->dval);
        else
            snprintf(buf, sizeof buf, "%g", e->dval);
        char esc[80];
        jvm_escape_string(buf, esc, sizeof esc);
        JI("ldc", esc);
        break;
    }
    case E_NULV:
        /* Null/empty string */
        JI("ldc", "\"\"");
        break;
    case E_KW: {
        /* &KEYWORD — read from sno_kw_get() helper */
        if (!e->sval) { JI("ldc", "\"\""); break; }
        char kwesc[128];
        jvm_escape_string(e->sval, kwesc, sizeof kwesc);
        JI("ldc", kwesc);
        char kgdesc[512];
        snprintf(kgdesc, sizeof kgdesc, "%s/sno_kw_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        JI("invokestatic", kgdesc);
        break;
    }
    case E_VART: {
        /* Variable reference — load from sno_vars HashMap (supports indirect write) */
        if (!e->sval) { JI("ldc", "\"\""); break; }
        if (strcasecmp(e->sval, "INPUT") == 0) {
            /* INPUT — reads one line from stdin; returns null on EOF (→ failure) */
            char irdesc[512];
            snprintf(irdesc, sizeof irdesc,
                "%s/sno_input_read()Ljava/lang/String;", jvm_classname);
            JI("invokestatic", irdesc);
            jvm_need_input_helper = 1;
            break;
        }
        /* Read via sno_indr_get(name) — HashMap is always authoritative */
        char nameesc[256];
        jvm_escape_string(e->sval, nameesc, sizeof nameesc);
        JI("ldc", nameesc);
        char igdesc[512];
        snprintf(igdesc, sizeof igdesc, "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        JI("invokestatic", igdesc);
        break;
    }
    case E_INDR: {
        /* $expr — indirect variable read: evaluate expr → string → lookup in sno_vars map */
        jvm_emit_expr(e->right ? e->right : e->left);
        char ivdesc[512];
        snprintf(ivdesc, sizeof ivdesc, "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        JI("invokestatic", ivdesc);
        break;
    }
    case E_ADD:
    case E_SUB:
    case E_MPY:
    case E_DIV:
    case E_EXPOP: {
        /* Arithmetic: parse both sides to double, operate, convert back */
        jvm_emit_expr(e->left);
        jvm_emit_to_double();
        jvm_emit_expr(e->right);
        jvm_emit_to_double();
        switch (e->kind) {
        case E_ADD:   JI("dadd", ""); break;
        case E_SUB:   JI("dsub", ""); break;
        case E_MPY:   JI("dmul", ""); break;
        case E_DIV:   JI("ddiv", ""); break;
        case E_EXPOP:
            /* Math.pow(a, b) */
            JI("invokestatic", "java/lang/Math/pow(DD)D");
            break;
        default: break;
        }
        /* Convert result back to SNOBOL4 string.
         * Strategy: store double in local var, check if whole, convert.
         * Locals 0-1 are double slot (2 wide), local 2 is long slot (2 wide).
         * Use a unique local offset per arith node to avoid conflicts.
         * For J1 simplicity: use fixed locals 0/1 (double) — sufficient for
         * non-nested arith which is all J1 corpus needs. Nested arith (J2+)
         * will use a local-stack allocator. */
        static int _arlbl = 0;
        int loc_d = 2; /* double stored at locals 2-3 */
        int loc_l = 4; /* long stored at locals 4-5 */
        char arfrac[32], ardone[32];
        snprintf(arfrac, sizeof arfrac, "Larf_%d", _arlbl);
        snprintf(ardone, sizeof ardone, "Lard_%d", _arlbl++);
        /* stack: double */
        J("    dstore %d\n", loc_d);              /* store result */
        J("    dload %d\n",  loc_d);              /* reload */
        JI("d2l",  "");                           /* convert to long (truncate) */
        J("    lstore %d\n", loc_l);              /* store long */
        J("    lload %d\n",  loc_l);              /* reload long */
        JI("l2d",  "");                           /* back to double */
        J("    dload %d\n",  loc_d);              /* original double */
        JI("dcmpl", "");                          /* compare: 0 if equal (whole) */
        JI("ifne", arfrac);                       /* != 0 → fractional */
        /* Whole: emit as long */
        J("    lload %d\n", loc_l);
        jvm_l2sno();
        JI("goto", ardone);
        J("%s:\n", arfrac);
        /* Fractional: emit as double string */
        J("    dload %d\n", loc_d);
        jvm_d2sno();
        J("%s:\n", ardone);
        break;
    }
    case E_CONC: {
        /* String concatenation: StringBuilder */
        JI("new", "java/lang/StringBuilder");
        JI("dup", "");
        JI("invokespecial", "java/lang/StringBuilder/<init>()V");
        jvm_emit_expr(e->left);
        JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
        jvm_emit_expr(e->right);
        JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
        JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
        break;
    }
    case E_MNS: {
        /* Unary minus: -expr */
        jvm_emit_expr(e->left);
        jvm_emit_to_double();
        JI("dneg", "");
        static int _mnslbl = 0;
        int loc_d = 2, loc_l = 4;
        char mfrac[32], mdone[32];
        snprintf(mfrac, sizeof mfrac, "Lmnsf_%d", _mnslbl);
        snprintf(mdone, sizeof mdone, "Lmnsd_%d", _mnslbl++);
        J("    dstore %d\n", loc_d);
        J("    dload %d\n",  loc_d);
        JI("d2l", ""); J("    lstore %d\n", loc_l);
        J("    lload %d\n", loc_l); JI("l2d", "");
        J("    dload %d\n", loc_d); JI("dcmpl", "");
        JI("ifne", mfrac);
        J("    lload %d\n", loc_l); jvm_l2sno(); JI("goto", mdone);
        J("%s:\n", mfrac); J("    dload %d\n", loc_d); jvm_d2sno();
        J("%s:\n", mdone);
        break;
    }
    case E_FNC: {
        /* Built-in function call — handle numeric/string builtins for J1 */
        const char *fname = e->sval ? e->sval : "";

        /* Arithmetic runtime functions: add/sub/mul/div/exp/mod
         * Called when parser can't resolve statically (e.g. '' + 1) */
        int is_arith = 0;
        int arith_op = 0; /* 0=add 1=sub 2=mul 3=div 4=exp */
        if      (strcasecmp(fname, "add") == 0) { is_arith=1; arith_op=0; }
        else if (strcasecmp(fname, "sub") == 0) { is_arith=1; arith_op=1; }
        else if (strcasecmp(fname, "mul") == 0) { is_arith=1; arith_op=2; }
        else if (strcasecmp(fname, "div") == 0) { is_arith=1; arith_op=3; }
        else if (strcasecmp(fname, "exp") == 0) { is_arith=1; arith_op=4; }
        if (is_arith && e->args && e->args[0] && e->args[1]) {
            jvm_emit_expr(e->args[0]);
            jvm_emit_to_double();
            jvm_emit_expr(e->args[1]);
            jvm_emit_to_double();
            switch (arith_op) {
            case 0: JI("dadd", ""); break;
            case 1: JI("dsub", ""); break;
            case 2: JI("dmul", ""); break;
            case 3: JI("ddiv", ""); break;
            case 4: JI("invokestatic", "java/lang/Math/pow(DD)D"); break;
            }
            static int _fnarlbl = 0;
            int loc_d = 2, loc_l = 4;
            char ffrac[32], ffdone[32];
            snprintf(ffrac,  sizeof ffrac,  "Lfnarf_%d", _fnarlbl);
            snprintf(ffdone, sizeof ffdone, "Lfnard_%d", _fnarlbl++);
            J("    dstore %d\n", loc_d);
            J("    dload %d\n",  loc_d);
            JI("d2l", ""); J("    lstore %d\n", loc_l);
            J("    lload %d\n", loc_l); JI("l2d", "");
            J("    dload %d\n", loc_d); JI("dcmpl", "");
            JI("ifne", ffrac);
            J("    lload %d\n", loc_l); jvm_l2sno(); JI("goto", ffdone);
            J("%s:\n", ffrac); J("    dload %d\n", loc_d); jvm_d2sno();
            J("%s:\n", ffdone);
            break;
        }

        /* neg(x) — unary minus */
        if (strcasecmp(fname, "neg") == 0 && e->args && e->args[0]) {
            jvm_emit_expr(e->args[0]);
            jvm_emit_to_double();
            JI("dneg", "");
            /* convert back: whole-number check using locals 2-5 */
            static int _neglbl = 0;
            int loc_d = 2, loc_l = 4;
            char nfrac[32], ndone[32];
            snprintf(nfrac, sizeof nfrac, "Lnegf_%d", _neglbl);
            snprintf(ndone, sizeof ndone, "Lnegd_%d", _neglbl++);
            J("    dstore %d\n", loc_d);
            J("    dload %d\n",  loc_d);
            JI("d2l",  "");
            J("    lstore %d\n", loc_l);
            J("    lload %d\n",  loc_l);
            JI("l2d",  "");
            J("    dload %d\n",  loc_d);
            JI("dcmpl", "");
            JI("ifne", nfrac);
            J("    lload %d\n", loc_l);
            jvm_l2sno();
            JI("goto", ndone);
            J("%s:\n", nfrac);
            J("    dload %d\n", loc_d);
            jvm_d2sno();
            J("%s:\n", ndone);
            break;
        }
        /* abs(x) — absolute value */
        if (strcasecmp(fname, "abs") == 0 && e->args && e->args[0]) {
            jvm_emit_expr(e->args[0]);
            jvm_emit_to_double();
            JI("invokestatic", "java/lang/Math/abs(D)D");
            static int _abslbl = 0;
            int loc_d = 2, loc_l = 4;
            char afrac[32], adone[32];
            snprintf(afrac, sizeof afrac, "Labsf_%d", _abslbl);
            snprintf(adone, sizeof adone, "Labsd_%d", _abslbl++);
            J("    dstore %d\n", loc_d);
            J("    dload %d\n",  loc_d);
            JI("d2l",  "");
            J("    lstore %d\n", loc_l);
            J("    lload %d\n",  loc_l);
            JI("l2d",  "");
            J("    dload %d\n",  loc_d);
            JI("dcmpl", "");
            JI("ifne", afrac);
            J("    lload %d\n", loc_l);
            jvm_l2sno();
            JI("goto", adone);
            J("%s:\n", afrac);
            J("    dload %d\n", loc_d);
            jvm_d2sno();
            J("%s:\n", adone);
            break;
        }
        /* SIZE(str) → length as decimal string */
        if (strcasecmp(fname, "SIZE") == 0 && e->args && e->args[0]) {
            jvm_emit_expr(e->args[0]);
            JI("invokevirtual", "java/lang/String/length()I");
            JI("i2l", "");
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
            break;
        }
        /* DUPL(str, n) → str repeated n times (Java 11+ String.repeat) */
        if (strcasecmp(fname, "DUPL") == 0 && e->args && e->args[0] && e->args[1]) {
            jvm_emit_expr(e->args[0]);
            jvm_emit_expr(e->args[1]);
            jvm_emit_to_double();
            JI("d2i", "");
            JI("invokevirtual", "java/lang/String/repeat(I)Ljava/lang/String;");
            break;
        }
        /* REMDR(a, b) → a mod b as decimal string */
        if (strcasecmp(fname, "REMDR") == 0 && e->args && e->args[0] && e->args[1]) {
            jvm_emit_expr(e->args[0]);
            jvm_emit_to_double(); JI("d2l", "");
            jvm_emit_expr(e->args[1]);
            jvm_emit_to_double(); JI("d2l", "");
            JI("lrem", "");
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
            break;
        }
        /* IDENT(a,b) → succeeds (returns a) if a equals b, fails (null) otherwise */
        if (strcasecmp(fname, "IDENT") == 0 && e->args && e->args[0]) {
            static int _identlbl = 0;
            char ifail[32], idone[32];
            snprintf(ifail, sizeof ifail, "Lident_f_%d", _identlbl);
            snprintf(idone, sizeof idone, "Lident_d_%d", _identlbl++);
            jvm_emit_expr(e->args[0]);
            if (e->args[1]) {
                jvm_emit_expr(e->args[1]);
            } else {
                JI("ldc", "\"\"");
            }
            JI("invokevirtual", "java/lang/String/equals(Ljava/lang/Object;)Z");
            JI("ifeq", ifail);
            /* success: push arg0 again */
            jvm_emit_expr(e->args[0]);
            JI("goto", idone);
            J("%s:\n", ifail);
            JI("aconst_null", "");
            J("%s:\n", idone);
            break;
        }
        /* DIFFER(a,b) → succeeds (returns a) if a differs from b, fails (null) if equal */
        if (strcasecmp(fname, "DIFFER") == 0 && e->args && e->args[0]) {
            static int _difflbl = 0;
            char dfail[32], ddone[32];
            snprintf(dfail, sizeof dfail, "Ldiff_f_%d", _difflbl);
            snprintf(ddone, sizeof ddone, "Ldiff_d_%d", _difflbl++);
            jvm_emit_expr(e->args[0]);
            if (e->args[1]) {
                jvm_emit_expr(e->args[1]);
            } else {
                JI("ldc", "\"\"");
            }
            JI("invokevirtual", "java/lang/String/equals(Ljava/lang/Object;)Z");
            JI("ifne", dfail);
            jvm_emit_expr(e->args[0]);
            JI("goto", ddone);
            J("%s:\n", dfail);
            JI("aconst_null", "");
            J("%s:\n", ddone);
            break;
        }
        /* Unrecognised function — stub as empty string */
        JI("ldc", "\"\"");
        break;
    }
    default:
        /* Unsupported expr kind — push empty string as stub */
        JI("ldc", "\"\"");
        break;
    }
}

/* -----------------------------------------------------------------------
 * Byrd box pattern node emitter — Sprint J4
 *
 * jvm_emit_pat_node(pat, gamma, omega, loc_subj, loc_cursor, loc_len,
 *                   p_cap_local, out, classname)
 *
 * Emits Jasmin bytecode for one pattern node.
 * On match success:  falls through to / jumps to  gamma
 * On match failure:  jumps to  omega
 *
 * All pattern state lives in JVM locals:
 *   loc_subj   (aload) — String   subject
 *   loc_cursor (iload) — int      current cursor position
 *   loc_len    (iload) — int      subject.length()
 *
 * Capture locals (*p_cap_local)++ allocates a new String slot per capture.
 *
 * Label naming: Jn<uid>_<role>  — globally unique via static counter.
 * ----------------------------------------------------------------------- */

static int jvm_pat_node_uid = 0;  /* global label counter for pattern nodes */
static char jvm_cur_pat_abort_label[128]; /* set per-statement: FAIL jumps here */

/* Forward declaration for recursive calls */
static void jvm_emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_cap_local,
                               FILE *out, const char *classname);

static void jvm_emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_cap_local,
                               FILE *out, const char *classname) {
    if (!pat) {
        /* empty pattern — always succeeds */
        fprintf(out, "    goto %s\n", gamma);
        return;
    }

    int uid = jvm_pat_node_uid++;

#define PN(fmt,...) fprintf(out, "    " fmt "\n", ##__VA_ARGS__)
#define PNL(lbl,fmt,...) fprintf(out, "%s:\n    " fmt "\n", lbl, ##__VA_ARGS__)
#define PNLABEL(lbl) fprintf(out, "%s:\n", lbl)

    switch (pat->kind) {

    /* ------------------------------------------------------------------ */
    case E_QLIT: {
        /* LIT node: match literal string at cursor
         * subject.regionMatches(cursor, lit, 0, lit.length())
         * On success: cursor += lit.length(); goto gamma
         * On failure: goto omega                                          */
        const char *s   = pat->sval ? pat->sval : "";
        int         slen = (int)strlen(s);

        char lbl_ok[64];
        snprintf(lbl_ok, sizeof lbl_ok, "Jn%d_lit_ok", uid);

        char litesc[4096];
        /* escape for Jasmin ldc */
        {
            int o = 0; litesc[o++] = '"';
            for (const char *p = s; *p && o < (int)sizeof litesc - 4; p++) {
                unsigned char c = (unsigned char)*p;
                if      (c == '"')  { litesc[o++]='\\'; litesc[o++]='"';  }
                else if (c == '\\') { litesc[o++]='\\'; litesc[o++]='\\'; }
                else if (c == '\n') { litesc[o++]='\\'; litesc[o++]='n';  }
                else if (c == '\r') { litesc[o++]='\\'; litesc[o++]='r';  }
                else if (c == '\t') { litesc[o++]='\\'; litesc[o++]='t';  }
                else                { litesc[o++]=(char)c; }
            }
            litesc[o++]='"'; litesc[o]='\0';
        }

        /* subject.regionMatches(cursor, lit, 0, litlen) */
        PN("aload %d", loc_subj);
        PN("iload %d", loc_cursor);
        PN("ldc %s", litesc);
        PN("iconst_0");
        PN("ldc %d", slen);
        PN("invokevirtual java/lang/String/regionMatches(ILjava/lang/String;II)Z");
        PN("ifeq %s", omega);    /* false → fail */
        /* success: advance cursor */
        PN("iinc %d %d", loc_cursor, slen);
        PN("goto %s", gamma);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_CONC: {
        /* SEQ node: left then right
         * Wiring (matches emit_asm_seq):
         *   alpha → left_alpha
         *   left_gamma  → right_alpha
         *   left_omega  → omega          (left failed, overall fail)
         *   right_gamma → gamma          (both succeeded)
         *   right_omega → left_beta      (right failed, backtrack left)
         *
         * JVM: no explicit beta ports needed — backtracking is handled
         * by the cursor being a local int that we can save/restore.
         * For J4 we implement non-backtracking SEQ (left must succeed,
         * then right must succeed; on right failure we fail overall).
         * Full backtracking SEQ requires a cursor save slot per level.    */
        char lmid[64];
        snprintf(lmid, sizeof lmid, "Jn%d_seq_mid", uid);

        jvm_emit_pat_node(pat->left,  lmid,  omega,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
        PNLABEL(lmid);
        jvm_emit_pat_node(pat->right, gamma, omega,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_OR: {
        /* ALT node: try left; on failure restore cursor and try right
         * Wiring:
         *   save cursor_save
         *   left: gamma → gamma, omega → restore+right_alpha
         *   right: gamma → gamma, omega → omega                          */
        int loc_save = (*p_cap_local)++;   /* allocate a local int for saved cursor */

        char lbl_try_right[64], lbl_restore[64];
        snprintf(lbl_try_right, sizeof lbl_try_right, "Jn%d_alt_right", uid);
        snprintf(lbl_restore,   sizeof lbl_restore,   "Jn%d_alt_rst",   uid);

        /* save cursor */
        PN("iload %d", loc_cursor);
        PN("istore %d", loc_save);

        jvm_emit_pat_node(pat->left, gamma, lbl_try_right,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

        PNLABEL(lbl_try_right);
        /* restore cursor */
        PN("iload %d", loc_save);
        PN("istore %d", loc_cursor);

        jvm_emit_pat_node(pat->right, gamma, omega,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_NAM: {
        /* Conditional assign:  pat . var
         * Match pat; on success capture matched substring into var.
         * cursor_before saved, on gamma: var = subject.substring(cursor_before, cursor) */
        int loc_before = (*p_cap_local)++;

        char lbl_inner_ok[64];
        snprintf(lbl_inner_ok, sizeof lbl_inner_ok, "Jn%d_nam_ok", uid);

        /* save cursor before child */
        PN("iload %d", loc_cursor);
        PN("istore %d", loc_before);

        jvm_emit_pat_node(pat->left, lbl_inner_ok, omega,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

        PNLABEL(lbl_inner_ok);
        /* var = subject.substring(cursor_before, cursor) */
        const char *varname = (pat->right && pat->right->sval) ? pat->right->sval : "";
        /* push name for sno_var_put */
        char nameesc[256];
        {
            int o=0; nameesc[o++]='"';
            for (const char *p=varname; *p && o<(int)sizeof nameesc-4; p++) {
                unsigned char c=(unsigned char)*p;
                if (c=='"') { nameesc[o++]='\\'; nameesc[o++]='"'; }
                else nameesc[o++]=(char)c;
            }
            nameesc[o++]='"'; nameesc[o]='\0';
        }
        PN("ldc %s", nameesc);
        PN("aload %d", loc_subj);
        PN("iload %d", loc_before);
        PN("iload %d", loc_cursor);
        PN("invokevirtual java/lang/String/substring(II)Ljava/lang/String;");
        /* call sno_var_put(name, substring) */
        char vpdesc[512];
        snprintf(vpdesc, sizeof vpdesc,
                 "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", classname);
        PN("invokestatic %s", vpdesc);
        PN("goto %s", gamma);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_DOL: {
        /* Immediate assign:  pat $ var  — same as E_NAM for J4 */
        int loc_before = (*p_cap_local)++;
        char lbl_inner_ok[64];
        snprintf(lbl_inner_ok, sizeof lbl_inner_ok, "Jn%d_dol_ok", uid);

        PN("iload %d", loc_cursor);
        PN("istore %d", loc_before);

        jvm_emit_pat_node(pat->left, lbl_inner_ok, omega,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

        PNLABEL(lbl_inner_ok);
        const char *varname = (pat->right && pat->right->sval) ? pat->right->sval : "";
        char nameesc[256];
        {
            int o=0; nameesc[o++]='"';
            for (const char *p=varname; *p && o<(int)sizeof nameesc-4; p++) {
                unsigned char c=(unsigned char)*p;
                if (c=='"') { nameesc[o++]='\\'; nameesc[o++]='"'; }
                else nameesc[o++]=(char)c;
            }
            nameesc[o++]='"'; nameesc[o]='\0';
        }
        PN("ldc %s", nameesc);
        PN("aload %d", loc_subj);
        PN("iload %d", loc_before);
        PN("iload %d", loc_cursor);
        PN("invokevirtual java/lang/String/substring(II)Ljava/lang/String;");
        char vpdesc[512];
        snprintf(vpdesc, sizeof vpdesc,
                 "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", classname);
        PN("invokestatic %s", vpdesc);
        PN("goto %s", gamma);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_FNC: {
        const char *fname = pat->sval ? pat->sval : "";

        /* ---- ARBNO(child) ---- */
        if (strcasecmp(fname, "ARBNO") == 0) {
            /* Greedy ARBNO: keep matching child as long as it succeeds
             * and cursor advances. This handles the common case correctly
             * without full backtracking support.
             *
             * α: save cursor; try child repeatedly until fail or no-advance
             * On loop exit: proceed to gamma with cursor at end of matched span */
            int loc_save = (*p_cap_local)++;   /* cursor before each attempt */

            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Jn%d_arb_loop", uid);
            snprintf(lbl_done, sizeof lbl_done, "Jn%d_arb_done", uid);

            /* inner child success → back to loop top; failure → done */
            char lbl_child_ok[64], lbl_child_fail[64];
            snprintf(lbl_child_ok,   sizeof lbl_child_ok,   "Jn%d_arb_cok",  uid);
            snprintf(lbl_child_fail, sizeof lbl_child_fail, "Jn%d_arb_cfail",uid);

            EXPR_t *child = (pat->args && pat->args[0]) ? pat->args[0] : NULL;

            /* loop: save cursor, try child */
            PNLABEL(lbl_loop);
            PN("iload %d", loc_cursor);
            PN("istore %d", loc_save);

            jvm_emit_pat_node(child, lbl_child_ok, lbl_child_fail,
                              loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

            PNLABEL(lbl_child_ok);
            /* zero-advance guard */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_save);
            PN("if_icmpeq %s", lbl_done);   /* no progress → stop */
            PN("goto %s", lbl_loop);         /* progress → try again */

            PNLABEL(lbl_child_fail);
            /* child failed — restore cursor to pre-attempt position */
            PN("iload %d", loc_save);
            PN("istore %d", loc_cursor);

            PNLABEL(lbl_done);
            /* ARBNO always succeeds (zero or more) */
            PN("goto %s", gamma);
            break;
        }

        /* ---- ANY(charset) ---- */
        if (strcasecmp(fname, "ANY") == 0) {
            /* cursor < len AND subject.charAt(cursor) in charset */
            EXPR_t *cs_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            char lbl_ok[64];
            snprintf(lbl_ok, sizeof lbl_ok, "Jn%d_any_ok", uid);

            /* bounds check: cursor < len */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", omega);

            /* ch = subject.charAt(cursor) */
            PN("aload %d", loc_subj);
            PN("iload %d", loc_cursor);
            PN("invokevirtual java/lang/String/charAt(I)C");
            /* convert to 1-char string */
            PN("invokestatic java/lang/String/valueOf(C)Ljava/lang/String;");
            /* charset.contains(ch_string) */
            /* push charset string */
            if (cs_arg) {
                /* inline the charset expr as a string, then use indexOf */
                /* We need the charset on the stack; push it, swap, then check */
                /* Stack after: ch_string on top */
                /* Use: charset.indexOf(ch_string) >= 0 */
                /* Temporarily store ch in a local */
                int loc_ch = (*p_cap_local)++;
                PN("astore %d", loc_ch);
                /* Evaluate charset expression */
                /* We have access to jvm_out globally; emit via jvm_emit_expr */
                /* But we need to use the module-level jvm_emit_expr.
                 * jvm_out is the module-level FILE* — save and restore. */
                FILE *saved_out = jvm_out;
                jvm_out = out;
                jvm_emit_expr(cs_arg);
                jvm_out = saved_out;
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifeq %s", omega);
            } else {
                /* no charset — always fail */
                PN("pop");
                PN("goto %s", omega);
                break;
            }
            /* success: advance cursor */
            PN("iinc %d 1", loc_cursor);
            PN("goto %s", gamma);
            break;
        }

        /* ---- NOTANY(charset) ---- */
        if (strcasecmp(fname, "NOTANY") == 0) {
            EXPR_t *cs_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            char lbl_ok[64];
            snprintf(lbl_ok, sizeof lbl_ok, "Jn%d_notany_ok", uid);

            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", omega);

            PN("aload %d", loc_subj);
            PN("iload %d", loc_cursor);
            PN("invokevirtual java/lang/String/charAt(I)C");
            PN("invokestatic java/lang/String/valueOf(C)Ljava/lang/String;");
            if (cs_arg) {
                int loc_ch = (*p_cap_local)++;
                PN("astore %d", loc_ch);
                FILE *saved_out = jvm_out; jvm_out = out;
                jvm_emit_expr(cs_arg);
                jvm_out = saved_out;
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifne %s", omega);   /* in charset → fail */
            } else {
                PN("pop");
            }
            PN("iinc %d 1", loc_cursor);
            PN("goto %s", gamma);
            break;
        }

        /* ---- SPAN(charset) ---- */
        if (strcasecmp(fname, "SPAN") == 0) {
            /* consume longest run of chars in charset (at least 1) */
            EXPR_t *cs_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_cs = (*p_cap_local)++;
            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Jn%d_span_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Jn%d_span_dn", uid);

            /* evaluate charset once */
            FILE *saved_out = jvm_out; jvm_out = out;
            if (cs_arg) jvm_emit_expr(cs_arg); else PN("ldc \"\"");
            jvm_out = saved_out;
            PN("astore %d", loc_cs);

            /* must match at least 1 */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", omega);
            PN("aload %d", loc_subj);
            PN("iload %d", loc_cursor);
            PN("invokevirtual java/lang/String/charAt(I)C");
            PN("invokestatic java/lang/String/valueOf(C)Ljava/lang/String;");
            {
                int loc_ch = (*p_cap_local)++;
                PN("astore %d", loc_ch);
                PN("aload %d", loc_cs);
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifeq %s", omega);
                PN("iinc %d 1", loc_cursor);

                /* loop for rest */
                PNLABEL(lbl_loop);
                PN("iload %d", loc_cursor);
                PN("iload %d", loc_len);
                PN("if_icmpge %s", lbl_done);
                PN("aload %d", loc_subj);
                PN("iload %d", loc_cursor);
                PN("invokevirtual java/lang/String/charAt(I)C");
                PN("invokestatic java/lang/String/valueOf(C)Ljava/lang/String;");
                PN("astore %d", loc_ch);
                PN("aload %d", loc_cs);
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifeq %s", lbl_done);
                PN("iinc %d 1", loc_cursor);
                PN("goto %s", lbl_loop);
            }
            PNLABEL(lbl_done);
            PN("goto %s", gamma);
            break;
        }

        /* ---- BREAK(charset) ---- */
        if (strcasecmp(fname, "BREAK") == 0) {
            /* consume chars NOT in charset until one IS in charset */
            EXPR_t *cs_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_cs = (*p_cap_local)++;
            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Jn%d_brk_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Jn%d_brk_dn", uid);

            FILE *saved_out = jvm_out; jvm_out = out;
            if (cs_arg) jvm_emit_expr(cs_arg); else PN("ldc \"\"");
            jvm_out = saved_out;
            PN("astore %d", loc_cs);

            PNLABEL(lbl_loop);
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", omega);
            {
                int loc_ch = (*p_cap_local)++;
                PN("aload %d", loc_subj);
                PN("iload %d", loc_cursor);
                PN("invokevirtual java/lang/String/charAt(I)C");
                PN("invokestatic java/lang/String/valueOf(C)Ljava/lang/String;");
                PN("astore %d", loc_ch);
                PN("aload %d", loc_cs);
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifne %s", lbl_done);   /* found break char → stop */
                PN("iinc %d 1", loc_cursor);
                PN("goto %s", lbl_loop);
            }
            PNLABEL(lbl_done);
            PN("goto %s", gamma);
            break;
        }

        /* ---- LEN(n) ---- */
        if (strcasecmp(fname, "LEN") == 0) {
            EXPR_t *n_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_n = (*p_cap_local)++;
            /* evaluate n, convert to int */
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) jvm_emit_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor + n <= len? */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("iadd");
            PN("iload %d", loc_len);
            PN("if_icmpgt %s", omega);
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("iadd");
            PN("istore %d", loc_cursor);
            PN("goto %s", gamma);
            break;
        }

        /* ---- POS(n) ---- */
        if (strcasecmp(fname, "POS") == 0) {
            EXPR_t *n_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) jvm_emit_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor == n? */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("if_icmpne %s", omega);
            PN("goto %s", gamma);
            break;
        }

        /* ---- RPOS(n) ---- */
        if (strcasecmp(fname, "RPOS") == 0) {
            EXPR_t *n_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) jvm_emit_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor == len - n? */
            PN("iload %d", loc_len);
            PN("iload %d", loc_n);
            PN("isub");
            PN("iload %d", loc_cursor);
            PN("if_icmpne %s", omega);
            PN("goto %s", gamma);
            break;
        }

        /* ---- TAB(n) ---- */
        if (strcasecmp(fname, "TAB") == 0) {
            EXPR_t *n_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) jvm_emit_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor <= n <= len? */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("if_icmpgt %s", omega);
            PN("iload %d", loc_n);
            PN("iload %d", loc_len);
            PN("if_icmpgt %s", omega);
            PN("iload %d", loc_n);
            PN("istore %d", loc_cursor);
            PN("goto %s", gamma);
            break;
        }

        /* ---- RTAB(n) ---- */
        if (strcasecmp(fname, "RTAB") == 0) {
            EXPR_t *n_arg = (pat->args && pat->args[0]) ? pat->args[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) jvm_emit_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* target = len - n; cursor <= target? */
            PN("iload %d", loc_len);
            PN("iload %d", loc_n);
            PN("isub");
            {
                int loc_tgt = (*p_cap_local)++;
                PN("istore %d", loc_tgt);
                PN("iload %d", loc_cursor);
                PN("iload %d", loc_tgt);
                PN("if_icmpgt %s", omega);
                PN("iload %d", loc_tgt);
                PN("istore %d", loc_cursor);
            }
            PN("goto %s", gamma);
            break;
        }

        /* ---- REM ---- */
        if (strcasecmp(fname, "REM") == 0) {
            /* always succeeds, cursor → len */
            PN("iload %d", loc_len);
            PN("istore %d", loc_cursor);
            PN("goto %s", gamma);
            break;
        }

        /* ---- ARB ---- */
        if (strcasecmp(fname, "ARB") == 0) {
            /* ARB: match minimum (0 chars) first; backtrack to extend.
             * For J4 non-backtracking: just succeed with 0 chars. */
            PN("goto %s", gamma);
            break;
        }

        /* ---- FAIL ---- */
        if (strcasecmp(fname, "FAIL") == 0) {
            /* FAIL forces the entire match to fail — jump past retry loop */
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : omega);
            break;
        }

        /* ---- SUCCEED ---- */
        if (strcasecmp(fname, "SUCCEED") == 0) {
            PN("goto %s", gamma);
            break;
        }

        /* ---- FENCE ---- */
        if (strcasecmp(fname, "FENCE") == 0) {
            PN("goto %s", gamma);
            break;
        }

        /* ---- ABORT ---- */
        if (strcasecmp(fname, "ABORT") == 0) {
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : omega);
            break;
        }

        /* ---- Unknown function — stub as success ---- */
        fprintf(out, "    ; STUB: unknown pattern function %s\n", fname);
        PN("goto %s", gamma);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_VART: {
        /* Variable reference in pattern context.
         * First check if it's a zero-arg builtin pattern name (REM, ARB, FAIL,
         * SUCCEED, FENCE, ABORT) — the parser emits these as E_VART when
         * they appear without parentheses. */
        const char *vname = pat->sval ? pat->sval : "";
        if (strcasecmp(vname, "REM") == 0) {
            PN("iload %d", loc_len);
            PN("istore %d", loc_cursor);
            PN("goto %s", gamma);
            break;
        }
        if (strcasecmp(vname, "FAIL") == 0) {
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : omega);
            break;
        }
        if (strcasecmp(vname, "SUCCEED") == 0) { PN("goto %s", gamma); break; }
        if (strcasecmp(vname, "FENCE") == 0)   { PN("goto %s", gamma); break; }
        if (strcasecmp(vname, "ABORT") == 0)   {
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : omega);
            break;
        }
        if (strcasecmp(vname, "ARB") == 0)     { PN("goto %s", gamma); break; }

        /* Otherwise: indirect pattern reference — look up value and match as literal */
        char lbl_ok[64];
        snprintf(lbl_ok, sizeof lbl_ok, "Jn%d_var_ok", uid);
        int loc_lit = (*p_cap_local)++;
        int loc_llen = (*p_cap_local)++;

        char nameesc[256];
        {
            int o=0; nameesc[o++]='"';
            for (const char *p=vname; *p && o<(int)sizeof nameesc-4; p++) {
                unsigned char c=(unsigned char)*p;
                if (c=='"') { nameesc[o++]='\\'; nameesc[o++]='"'; }
                else nameesc[o++]=(char)c;
            }
            nameesc[o++]='"'; nameesc[o]='\0';
        }
        PN("ldc %s", nameesc);
        char igdesc[512];
        snprintf(igdesc, sizeof igdesc,
                 "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", classname);
        PN("invokestatic %s", igdesc);
        PN("astore %d", loc_lit);
        PN("aload %d", loc_lit);
        PN("invokevirtual java/lang/String/length()I");
        PN("istore %d", loc_llen);
        PN("aload %d", loc_subj);
        PN("iload %d", loc_cursor);
        PN("aload %d", loc_lit);
        PN("iconst_0");
        PN("iload %d", loc_llen);
        PN("invokevirtual java/lang/String/regionMatches(ILjava/lang/String;II)Z");
        PN("ifeq %s", omega);
        PN("iload %d", loc_cursor);
        PN("iload %d", loc_llen);
        PN("iadd");
        PN("istore %d", loc_cursor);
        PN("goto %s", gamma);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_INDR: {
        /* *VAR — indirect pattern reference: evaluate inner expr to get
         * the variable NAME, look up its value, match as literal.
         * The inner expr for *PAT is E_VART("PAT") — we need its name
         * as a string, NOT its value.  So push the sval directly.        */
        int loc_lit  = (*p_cap_local)++;
        int loc_llen = (*p_cap_local)++;
        EXPR_t *inner = pat->right ? pat->right : pat->left;

        /* Push the variable name as a literal string (not its value) */
        const char *vname = (inner && inner->sval) ? inner->sval : "";
        char nameesc[256];
        {
            int o=0; nameesc[o++]='"';
            for (const char *p=vname; *p && o<(int)sizeof nameesc-4; p++) {
                unsigned char c=(unsigned char)*p;
                if (c=='"') { nameesc[o++]='\\'; nameesc[o++]='"'; }
                else nameesc[o++]=(char)c;
            }
            nameesc[o++]='"'; nameesc[o]='\0';
        }
        PN("ldc %s", nameesc);
        /* Look up value of that variable */
        char igdesc[512];
        snprintf(igdesc, sizeof igdesc,
                 "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", classname);
        PN("invokestatic %s", igdesc);
        PN("astore %d", loc_lit);
        PN("aload %d", loc_lit);
        PN("invokevirtual java/lang/String/length()I");
        PN("istore %d", loc_llen);
        /* regionMatches(cursor, lit, 0, llen) */
        PN("aload %d", loc_subj);
        PN("iload %d", loc_cursor);
        PN("aload %d", loc_lit);
        PN("iconst_0");
        PN("iload %d", loc_llen);
        PN("invokevirtual java/lang/String/regionMatches(ILjava/lang/String;II)Z");
        PN("ifeq %s", omega);
        PN("iload %d", loc_cursor);
        PN("iload %d", loc_llen);
        PN("iadd");
        PN("istore %d", loc_cursor);
        PN("goto %s", gamma);
        break;
    }

    /* ------------------------------------------------------------------ */
        fprintf(out, "    ; STUB: unhandled pattern node kind %d\n", (int)pat->kind);
        PN("goto %s", gamma);
        break;
    }

#undef PN
#undef PNL
#undef PNLABEL
}

/* -----------------------------------------------------------------------
 * Statement emitter
 * ----------------------------------------------------------------------- */

static void jvm_emit_stmt(STMT_t *s, int stmt_idx) {
    char next_lbl[64];
    snprintf(next_lbl, sizeof next_lbl, "Ln_%d", stmt_idx);

    /* Label */
    if (s->label) {
        JSep(s->label);
        char lbuf[128];
        snprintf(lbuf, sizeof lbuf, "L_%s", s->label);
        J("%s:\n", lbuf);
    }

    /* Label-only statement (no subject, no body) — just fall through */
    if (!s->subject && !s->has_eq && !s->pattern) {
        if (s->go && s->go->uncond && s->go->uncond[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->uncond);
            JI("goto", glbl);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        return;
    }

    /* Case 1: pure assignment — no pattern */
    if (s->has_eq && s->subject &&
        (s->subject->kind == E_VART || s->subject->kind == E_KW) &&
        !s->pattern) {

        const char *subj = s->subject->sval ? s->subject->sval : "";
        int is_output = strcasecmp(subj, "OUTPUT") == 0;
        int is_kw     = (s->subject->kind == E_KW);

        if (is_output) {
            /* OUTPUT = expr → System.out.println(expr) */
            char desc[512];
            snprintf(desc, sizeof desc, "%s/sno_stdout Ljava/io/PrintStream;", jvm_classname);
            JI("getstatic", desc);
            if (!s->replacement || s->replacement->kind == E_NULV) {
                JI("ldc", "\"\"");
            } else {
                jvm_emit_expr(s->replacement);
            }
            JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
        } else if (is_kw) {
            /* &KEYWORD = expr → sno_kw_set(name, val) */
            char kwesc[128];
            jvm_escape_string(subj, kwesc, sizeof kwesc);
            JI("ldc", kwesc);
            if (!s->replacement || s->replacement->kind == E_NULV) {
                JI("ldc", "\"\"");
            } else {
                jvm_emit_expr(s->replacement);
            }
            char ksdesc[512];
            snprintf(ksdesc, sizeof ksdesc, "%s/sno_kw_set(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
            JI("invokestatic", ksdesc);
        } else {
            /* VAR = expr → sno_var_put(name, val) into HashMap */
            char nameesc[256];
            jvm_escape_string(subj, nameesc, sizeof nameesc);

            /* Check if RHS is INPUT — may return null (EOF = failure) */
            int rhs_is_input = (s->replacement &&
                                s->replacement->kind == E_VART &&
                                s->replacement->sval &&
                                strcasecmp(s->replacement->sval, "INPUT") == 0);

            if (rhs_is_input && s->go && s->go->onfailure && s->go->onfailure[0]) {
                /* Emit INPUT call first, dup, ifnull → :F, then store */
                jvm_emit_expr(s->replacement);   /* → String | null */
                JI("dup", "");
                char flbl[128]; snprintf(flbl, sizeof flbl, "L_%s", s->go->onfailure);
                JI("ifnull", flbl);
                /* non-null: store via sno_var_put(name, val) */
                /* Stack: val — need to push name first: swap trick */
                JI("ldc", nameesc);
                JI("swap", "");
                char vpdesc2[512];
                snprintf(vpdesc2, sizeof vpdesc2, "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
                JI("invokestatic", vpdesc2);
            } else {
                JI("ldc", nameesc);
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    JI("ldc", "\"\"");
                } else {
                    jvm_emit_expr(s->replacement);
                }
                char vpdesc[512];
                snprintf(vpdesc, sizeof vpdesc, "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
                JI("invokestatic", vpdesc);
            }
        }

        /* :S goto (always unconditional for non-INPUT assigns) */
        if (s->go && s->go->uncond && s->go->uncond[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->uncond);
            JI("goto", glbl);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        /* :F fallthrough (no-op — already jumped or falling through) */
        return;
    }

    /* Case 3: indirect assignment — $expr = val */
    if (s->has_eq && s->subject && s->subject->kind == E_INDR && !s->pattern) {
        /* Evaluate the indirect target name */
        EXPR_t *indr_operand = s->subject->right ? s->subject->right : s->subject->left;
        jvm_emit_expr(indr_operand);   /* → String: the variable name */
        /* Evaluate RHS value */
        if (!s->replacement || s->replacement->kind == E_NULV) {
            JI("ldc", "\"\"");
        } else {
            jvm_emit_expr(s->replacement);
        }
        /* sno_indr_set(name_str, value_str) */
        char isdesc[512];
        snprintf(isdesc, sizeof isdesc, "%s/sno_indr_set(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        JI("invokestatic", isdesc);

        if (s->go && s->go->uncond && s->go->uncond[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->uncond);
            JI("goto", glbl);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        return;
    }

    /* Case 2: expression-only statement (no =, no pattern) — just evaluate */
    if (!s->has_eq && !s->pattern && s->subject) {
        jvm_emit_expr(s->subject);
        /* Result may be null (IDENT/DIFFER failure) */
        if (s->go && s->go->onfailure && s->go->onfailure[0]) {
            /* Test for null → :F (pop null before jump, clean stack) */
            static int _ef_lbl = 0;
            char enotnull[32];
            snprintf(enotnull, sizeof enotnull, "Lef_nn_%d", _ef_lbl++);
            char flbl[128]; snprintf(flbl, sizeof flbl, "L_%s", s->go->onfailure);
            JI("dup", "");
            JI("ifnonnull", enotnull);
            JI("pop", "");   /* discard null */
            JI("goto", flbl);
            J("%s:\n", enotnull);
            JI("pop", "");   /* discard non-null result */
        } else {
            JI("pop", "");  /* discard result (no :F needed) */
        }
        if (s->go && s->go->uncond && s->go->uncond[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->uncond);
            JI("goto", glbl);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * Case 4: Pattern match statement  subject  pattern  [= replacement]  :S/:F
     *
     * Strategy (Sprint J4 — Byrd boxes in JVM):
     *   1. Load subject string into a local var (cursor_local = local slot 6)
     *   2. cursor starts at 0 (local slot 7 = int)
     *   3. Walk the pattern tree, emitting one Jasmin label per Byrd port (α/β/γ/ω)
     *   4. On γ (overall match success): run replacement (if any), goto :S
     *   5. On ω (overall fail): if ANCHOR=0, advance cursor by 1 and retry from top;
     *      if exhausted, goto :F
     *
     * Local variable layout (per-statement):
     *   L6  : subject String
     *   L7  : cursor int
     *   L8  : subject length int
     *   L9+ : scratch (capture captures use higher slots via capture_local counter)
     *
     * Label naming: jvmN_xxx  where N = stmt_idx (unique per statement)
     * ----------------------------------------------------------------------- */

    if (s->pattern) {
        static int jvm_pat_uid_counter = 0;
        int uid = jvm_pat_uid_counter++;

        /* Local slots for subject/cursor/length */
        int loc_subj   = 6;
        int loc_cursor = 7;
        int loc_len    = 8;
        /* slot 9: dedicated retry cursor-save (scan loop saves here before each attempt) */
        int loc_retry_save = 9;
        /* capture locals start at 10, allocated per capture node */
        int loc_cap_base = 10;

        /* The outer retry loop label (scan mode: try each cursor position) */
        char lbl_retry[64], lbl_success[64], lbl_fail[64];
        snprintf(lbl_retry,   sizeof lbl_retry,   "Jpat%d_retry",   uid);
        snprintf(lbl_success, sizeof lbl_success, "Jpat%d_success", uid);
        snprintf(lbl_fail,    sizeof lbl_fail,    "Jpat%d_fail",    uid);

        J("; --- pattern match statement ---\n");

        /* Load subject into local 6 */
        jvm_emit_expr(s->subject);
        J("    astore %d\n", loc_subj);

        /* cursor = 0 */
        JI("iconst_0", "");
        J("    istore %d\n", loc_cursor);

        /* len = subject.length() */
        J("    aload %d\n", loc_subj);
        JI("invokevirtual", "java/lang/String/length()I");
        J("    istore %d\n", loc_len);

        /* --- RETRY LOOP: try match at current cursor position --- */
        J("%s:\n", lbl_retry);
        /* save cursor at start of each attempt (for scan advance) */
        J("    iload %d\n", loc_cursor);
        J("    istore %d\n", loc_retry_save);

        /* Emit the pattern tree.  On tree-level success → lbl_success.
         * On tree-level failure → advance cursor or goto lbl_fail. */
        char lbl_tree_ok[64], lbl_tree_fail[64];
        snprintf(lbl_tree_ok,   sizeof lbl_tree_ok,   "Jpat%d_tok",  uid);
        snprintf(lbl_tree_fail, sizeof lbl_tree_fail, "Jpat%d_tfail", uid);

        /* We need capture-local counter accessible across recursive calls. */
        static int jvm_cap_local_counter;
        jvm_cap_local_counter = loc_cap_base;

        /* Set module-level abort label so FAIL/ABORT can jump past retry loop */
        snprintf(jvm_cur_pat_abort_label, sizeof jvm_cur_pat_abort_label, "%s", lbl_fail);

        jvm_emit_pat_node(s->pattern,
                          lbl_tree_ok, lbl_tree_fail,
                          loc_subj, loc_cursor, loc_len,
                          &jvm_cap_local_counter,
                          jvm_out, jvm_classname);

        /* --- tree OK: match succeeded at this cursor position --- */
        J("%s:\n", lbl_tree_ok);
        /* Replacement (if any) — subject[cursor_start..cursor_end] = replacement.
         * For J4 we do not yet implement in-place replacement; we handle
         * OUTPUT = subject in a later sprint.  The captures (. and $) are
         * already stored.  Just goto :S. */
        if (s->has_eq && s->replacement) {
            /* Subject replacement: rebuild subject with matched region replaced.
             * Use String.substring(0,cursor_start) + replacement + String.substring(cursor,end)
             * We stored cursor_start in a BSS slot; for J4 we approximate:
             * full replacement not yet implemented — emit a TODO comment and skip. */
            JC("TODO J5: subject replacement (= rhs) — Sprint J5");
        }
        JI("goto", lbl_success);

        /* --- tree FAIL: try next cursor position (scan mode) --- */
        J("%s:\n", lbl_tree_fail);
        /* if &ANCHOR, no scan — immediate failure */
        {
            char lbl_anchor_skip[64];
            snprintf(lbl_anchor_skip, sizeof lbl_anchor_skip, "Jpat%d_askip", uid);
            char anchor_desc[512];
            snprintf(anchor_desc, sizeof anchor_desc, "%s/sno_kw_ANCHOR I", jvm_classname);
            JI("getstatic", anchor_desc);
            JI("ifeq", lbl_anchor_skip);
            JI("goto", lbl_fail);
            J("%s:\n", lbl_anchor_skip);
        }
        /* cursor++ */
        J("    iinc %d 1\n", loc_cursor);
        /* if cursor <= len, retry */
        J("    iload %d\n", loc_cursor);
        J("    iload %d\n", loc_len);
        JI("if_icmple", lbl_retry);
        JI("goto", lbl_fail);

        /* --- SUCCESS --- */
        J("%s:\n", lbl_success);
        if (s->go && s->go->uncond && s->go->uncond[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->uncond);
            JI("goto", glbl);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        /* fall through if no :S */

        /* --- FAIL --- */
        J("%s:\n", lbl_fail);
        if (s->go && s->go->onfailure && s->go->onfailure[0]) {
            char flbl[128]; snprintf(flbl, sizeof flbl, "L_%s", s->go->onfailure);
            JI("goto", flbl);
        }
        return;
    }

    /* Fallthrough: unhandled stmt form */
    JC("unhandled stmt form");
    JI("nop", "");
}

/* -----------------------------------------------------------------------
 * Runtime helper methods — emitted once per program
 * ----------------------------------------------------------------------- */

/* Track which helpers are needed */
static int jvm_need_kw_helpers   = 0;
static int jvm_need_indr_helpers = 0;
static int jvm_need_varmap       = 0;

static void jvm_emit_runtime_helpers(void) {
    /* sno_kw_get(String name) → String
     * Returns value of SNOBOL4 keyword &name.
     * For J2: ALPHABET=256-char string, TRIM/ANCHOR/FULLSCAN/STCOUNT/STLIMIT = integers.
     * Unrecognised keyword → "" (unset). */
    if (jvm_need_kw_helpers) {
        J(".method static sno_kw_get(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 1\n");
        /* &ALPHABET */
        J("    aload_0\n");
        J("    ldc \"ALPHABET\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_alphabet\n");
        /* Build the 256-char alphabet string via StringBuilder */
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    ldc 0\n"); /* using iconst */
        J("    istore_0\n");  /* reuse local 0 as counter */
        J("Lkwg_alpha_loop:\n");
        J("    iload_0\n");
        J("    sipush 256\n");
        J("    if_icmpge Lkwg_alpha_done\n");
        J("    iload_0\n");
        J("    i2c\n");
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    iinc 0 1\n");
        J("    goto Lkwg_alpha_loop\n");
        J("Lkwg_alpha_done:\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_alphabet:\n");
        /* &TRIM / &ANCHOR / &FULLSCAN: stored in sno_kw_* static int fields */
        J("    aload_0\n");
        J("    ldc \"TRIM\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_trim\n");
        char trdesc[512];
        snprintf(trdesc, sizeof trdesc, "%s/sno_kw_TRIM I", jvm_classname);
        J("    getstatic %s\n", trdesc);
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_trim:\n");
        J("    aload_0\n");
        J("    ldc \"ANCHOR\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_anchor\n");
        char andesc[512];
        snprintf(andesc, sizeof andesc, "%s/sno_kw_ANCHOR I", jvm_classname);
        J("    getstatic %s\n", andesc);
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_anchor:\n");
        /* Unknown keyword → "" */
        J("    ldc \"\"\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_kw_set(String name, String val) → void */
        J(".method static sno_kw_set(Ljava/lang/String;Ljava/lang/String;)V\n");
        J("    .limit stack 4\n");
        J("    .limit locals 2\n");
        J("    aload_0\n");
        J("    ldc \"TRIM\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkws_not_trim\n");
        J("    aload_1\n");
        J("    invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I\n");
        snprintf(trdesc, sizeof trdesc, "%s/sno_kw_TRIM I", jvm_classname);
        J("    putstatic %s\n", trdesc);
        J("    return\n");
        J("Lkws_not_trim:\n");
        J("    aload_0\n");
        J("    ldc \"ANCHOR\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkws_not_anchor\n");
        J("    aload_1\n");
        J("    invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I\n");
        snprintf(andesc, sizeof andesc, "%s/sno_kw_ANCHOR I", jvm_classname);
        J("    putstatic %s\n", andesc);
        J("    return\n");
        J("Lkws_not_anchor:\n");
        J("    return\n");
        J(".end method\n\n");

        jvm_need_kw_helpers = 0;
    }

    if (jvm_need_varmap || jvm_need_indr_helpers) {
        /* sno_var_put(String name, String val) → void
         * Stores val into sno_vars HashMap for indirect access. */
        J(".method static sno_var_put(Ljava/lang/String;Ljava/lang/String;)V\n");
        J("    .limit stack 4\n");
        J("    .limit locals 2\n");
        char vmdesc[512];
        snprintf(vmdesc, sizeof vmdesc, "%s/sno_vars Ljava/util/HashMap;", jvm_classname);
        J("    getstatic %s\n", vmdesc);
        J("    aload_0\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    return\n");
        J(".end method\n\n");

        /* sno_indr_get(String name) → String
         * Look up variable whose name is the string value of name. */
        J(".method static sno_indr_get(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 1\n");
        snprintf(vmdesc, sizeof vmdesc, "%s/sno_vars Ljava/util/HashMap;", jvm_classname);
        J("    getstatic %s\n", vmdesc);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/lang/String\n");
        J("    dup\n");
        J("    ifnonnull Lsig_done\n");
        J("    pop\n");
        J("    ldc \"\"\n");
        J("Lsig_done:\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_indr_set(String name, String val) → void
         * Store val into the variable named by the string value of name.
         * Updates sno_vars map. */
        J(".method static sno_indr_set(Ljava/lang/String;Ljava/lang/String;)V\n");
        J("    .limit stack 4\n");
        J("    .limit locals 2\n");
        snprintf(vmdesc, sizeof vmdesc, "%s/sno_vars Ljava/util/HashMap;", jvm_classname);
        J("    getstatic %s\n", vmdesc);
        J("    aload_0\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    return\n");
        J(".end method\n\n");

        jvm_need_varmap = 0;
        jvm_need_indr_helpers = 0;
    }

    /* sno_input_read() → String | null
     * Reads one line from stdin (stripping trailing newline).
     * Returns null on EOF — maps to SNOBOL4 INPUT failure. */
    if (jvm_need_input_helper) {
        J(".method static sno_input_read()Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 1\n");
        /* Lazy-init sno_input_br */
        J("    getstatic       %s/sno_input_br Ljava/io/BufferedReader;\n", jvm_classname);
        J("    ifnonnull       Lir_ready\n");
        J("    new             java/io/BufferedReader\n");
        J("    dup\n");
        J("    new             java/io/InputStreamReader\n");
        J("    dup\n");
        J("    getstatic       java/lang/System/in Ljava/io/InputStream;\n");
        J("    invokespecial   java/io/InputStreamReader/<init>(Ljava/io/InputStream;)V\n");
        J("    invokespecial   java/io/BufferedReader/<init>(Ljava/io/Reader;)V\n");
        J("    putstatic       %s/sno_input_br Ljava/io/BufferedReader;\n", jvm_classname);
        J("Lir_ready:\n");
        J("    getstatic       %s/sno_input_br Ljava/io/BufferedReader;\n", jvm_classname);
        J("    invokevirtual   java/io/BufferedReader/readLine()Ljava/lang/String;\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_input_helper = 0;
    }
}



static void jvm_emit_header(Program *prog) {
    J(".class public %s\n", jvm_classname);
    J(".super java/lang/Object\n");
    J("\n");

    /* static field: PrintStream for OUTPUT */
    J("; Runtime fields\n");
    J(".field static sno_stdout Ljava/io/PrintStream;\n");
    J(".field static sno_input_br Ljava/io/BufferedReader;\n");

    /* Keyword int fields (J2) */
    J(".field static sno_kw_TRIM I\n");
    J(".field static sno_kw_ANCHOR I\n");

    /* Dynamic variable map for indirect assignment (J2) */
    J(".field static sno_vars Ljava/util/HashMap;\n");

    /* static fields for SNOBOL4 variables */
    for (int i = 0; i < jvm_nvar; i++) {
        char fld[256];
        jvm_field_name(jvm_vars[i], fld, sizeof fld);
        J(".field static %s Ljava/lang/String;\n", fld);
    }
    J("\n");

    /* static initialiser: cache System.out, init vars to "", init map */
    J(".method static <clinit>()V\n");
    /* stack: need 4 for HashMap init + put operations */
    int clinit_stack = 4 + jvm_nvar * 3;
    if (clinit_stack < 4) clinit_stack = 4;
    J("    .limit stack %d\n", clinit_stack);
    J("    .limit locals 0\n");
    J("    getstatic       java/lang/System/out Ljava/io/PrintStream;\n");
    J("    putstatic       %s/sno_stdout Ljava/io/PrintStream;\n", jvm_classname);
    /* init keyword ints to 0 */
    J("    iconst_0\n");
    J("    putstatic       %s/sno_kw_TRIM I\n", jvm_classname);
    J("    iconst_0\n");
    J("    putstatic       %s/sno_kw_ANCHOR I\n", jvm_classname);
    /* init sno_vars HashMap */
    J("    new java/util/HashMap\n");
    J("    dup\n");
    J("    invokespecial java/util/HashMap/<init>()V\n");
    J("    putstatic       %s/sno_vars Ljava/util/HashMap;\n", jvm_classname);
    /* init variables to empty string and pre-populate map */
    for (int i = 0; i < jvm_nvar; i++) {
        char fld[256];
        jvm_field_name(jvm_vars[i], fld, sizeof fld);
        J("    ldc             \"\"\n");
        J("    putstatic       %s/%s Ljava/lang/String;\n", jvm_classname, fld);
        J("    getstatic       %s/sno_vars Ljava/util/HashMap;\n", jvm_classname);
        J("    ldc             \"%s\"\n", jvm_vars[i]);
        J("    ldc             \"\"\n");
        J("    invokevirtual   java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
    }
    J("    return\n");
    J(".end method\n");
    J("\n");
}

static void jvm_emit_main_open(void) {
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 16\n");
    J("    .limit locals 32\n");
    J("\n");
}

static void jvm_emit_main_close(void) {
    JC("program end");
    JI("return", "");
    J(".end method\n");
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

void jvm_emit(Program *prog, FILE *out, const char *filename) {
    jvm_out = out;
    jvm_nvar = 0;
    jvm_need_sno_parse_helper = 0;
    jvm_need_input_helper = 0;
    jvm_need_kw_helpers   = 1;  /* always emit for J2 — callers may use &KEYWORD */
    jvm_need_indr_helpers = 1;  /* always emit for J2 — indirect assign/get */
    jvm_need_varmap       = 1;
    jvm_set_classname(filename);

    if (prog && prog->head)
        jvm_collect_vars(prog);

    JC("Generated by sno2c -jvm");
    JC("Assemble: java -jar jasmin.jar <file>.j -d .");
    JC("Run:      java <classname>");
    J("\n");

    jvm_emit_header(prog);
    jvm_emit_main_open();

    /* Walk statements */
    int idx = 0;
    if (prog && prog->head) {
        for (STMT_t *s = prog->head; s; s = s->next, idx++) {
            if (s->is_end) {
                JSep("END");
                J("L_END:\n");
                JI("nop", "");
                break;
            }
            jvm_emit_stmt(s, idx);
        }
    }

    jvm_emit_main_close();
    jvm_emit_parse_helper();
    jvm_emit_runtime_helpers();

    /* Free collected var names */
    for (int i = 0; i < jvm_nvar; i++) { free(jvm_vars[i]); jvm_vars[i] = NULL; }
    jvm_nvar = 0;
}
