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
    case E_VART: {
        /* Variable reference — load static field */
        if (!e->sval) { JI("ldc", "\"\""); break; }
        if (strcasecmp(e->sval, "INPUT") == 0) {
            /* INPUT: read a line from stdin — simplified for J1 */
            JI("ldc", "\"\"");
            break;
        }
        char fld[256];
        jvm_field_name(e->sval, fld, sizeof fld);
        char desc[512];
        snprintf(desc, sizeof desc, "%s/%s Ljava/lang/String;", jvm_classname, fld);
        JI("getstatic", desc);
        /* Unset variable is null → treat as empty string */
        JI("dup", "");
        char nonull[64];
        /* if non-null skip, else pop+push "" */
        static int _nulllbl = 0;
        char skip[32], done[32];
        snprintf(skip, sizeof skip, "Lnv_%d", _nulllbl);
        snprintf(done, sizeof done, "Lnvd_%d", _nulllbl++);
        JI("ifnonnull", skip);
        JI("pop", "");
        JI("ldc", "\"\"");
        J("%s:\n", skip);
        (void)nonull; (void)done;
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
        if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
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
        } else {
            /* VAR = expr → putstatic */
            if (!s->replacement || s->replacement->kind == E_NULV) {
                JI("ldc", "\"\"");
            } else {
                jvm_emit_expr(s->replacement);
            }
            char fld[256], desc[512];
            jvm_field_name(subj, fld, sizeof fld);
            snprintf(desc, sizeof desc, "%s/%s Ljava/lang/String;", jvm_classname, fld);
            JI("putstatic", desc);
        }

        /* :S/:F / unconditional goto */
        if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        /* else fall through to next statement */
        return;
    }

    /* Case 2: expression-only statement (no =, no pattern) — just evaluate */
    if (!s->has_eq && !s->pattern && s->subject) {
        /* Evaluate subject expression for side effects — discard result */
        jvm_emit_expr(s->subject);
        JI("pop", "");
        if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            char glbl[128]; snprintf(glbl, sizeof glbl, "L_%s", s->go->onsuccess);
            JI("goto", glbl);
        }
        return;
    }

    /* Stub for unhandled statement forms (pattern match etc.) — J3+ */
    JC("stub — unhandled stmt form (J3+)");
    JI("nop", "");
}

/* -----------------------------------------------------------------------
 * Header — class declaration, static fields, clinit
 * ----------------------------------------------------------------------- */

static void jvm_emit_header(Program *prog) {
    J(".class public %s\n", jvm_classname);
    J(".super java/lang/Object\n");
    J("\n");

    /* static field: PrintStream for OUTPUT */
    J("; Runtime fields\n");
    J(".field static sno_stdout Ljava/io/PrintStream;\n");

    /* static fields for SNOBOL4 variables */
    for (int i = 0; i < jvm_nvar; i++) {
        char fld[256];
        jvm_field_name(jvm_vars[i], fld, sizeof fld);
        J(".field static %s Ljava/lang/String;\n", fld);
    }
    J("\n");

    /* static initialiser: cache System.out, init vars to "" */
    J(".method static <clinit>()V\n");
    /* stack needs room for getstatic + putstatic per var */
    int clinit_stack = 1 + jvm_nvar;
    if (clinit_stack < 2) clinit_stack = 2;
    J("    .limit stack %d\n", clinit_stack);
    J("    .limit locals 0\n");
    J("    getstatic       java/lang/System/out Ljava/io/PrintStream;\n");
    J("    putstatic       %s/sno_stdout Ljava/io/PrintStream;\n", jvm_classname);
    /* init variables to empty string */
    for (int i = 0; i < jvm_nvar; i++) {
        char fld[256];
        jvm_field_name(jvm_vars[i], fld, sizeof fld);
        J("    ldc             \"\"\n");
        J("    putstatic       %s/%s Ljava/lang/String;\n", jvm_classname, fld);
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

    /* Free collected var names */
    for (int i = 0; i < jvm_nvar; i++) { free(jvm_vars[i]); jvm_vars[i] = NULL; }
    jvm_nvar = 0;
}
