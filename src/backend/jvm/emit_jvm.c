/*
 * emit_byrd_jvm.c — JVM Jasmin text emitter for scrip-cc
 *
 * Consumes the same Program* IR as emit_byrd_asm.c.
 * Emits Jasmin assembler text (.j files) assembled by jasmin.jar.
 *
 * Pipeline:
 *   scrip-cc -jvm prog.sno > prog.j
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

#include "scrip_cc.h"
#include "ir/ir_emit_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Safe child accessor — returns NULL if index out of bounds.
 * Prevents OOB reads on unary nodes (nchildren==1) where children[1]
 * would read past the realloc'd array. (M-G-INV-EMIT-FIX) */
#define ECHILD(e, idx) ((e) && (e)->nchildren > (idx) ? (e)->children[(idx)] : NULL)

/* -----------------------------------------------------------------------
 * Output helpers — three-column layout
 * ----------------------------------------------------------------------- */

static FILE *jvm_out;

static int jvm_is_exported(Program *prog, const char *name) {
    if (!prog) return 0;
    for (ExportEntry *e = prog->exports; e; e = e->next)
        if (strcasecmp(e->name, name) == 0) return 1;
    return 0;
}

static ImportEntry *jvm_find_import(Program *prog, const char *name) {
    if (!prog) return NULL;
    for (ImportEntry *ie = prog->imports; ie; ie = ie->next)
        if (strcasecmp(ie->method, name) == 0) return ie;
    return NULL;
}

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

/* jvm_expand_label — sanitize a SNOBOL4 label for use as a Jasmin identifier.
 * Jasmin labels must not contain :  ( ) $ ' < > = etc.
 * Same expansion table as asm_expand_name in emit_byrd_asm.c. */
static void jvm_expand_label(const char *src, char *dst, int dstlen) {
    static const struct { char ch; const char *nm; } tbl[] = {
        {'>', "GT"}, {'<', "LT"}, {'=', "EQ"}, {'+', "PL"}, {'-', "MI"},
        {'*', "ST"}, {'/', "SL"}, {'(', "LP"}, {')', "RP"}, {'$', "DL"},
        {'.', "DT"}, {'?', "QM"}, {'!', "BG"}, {'&', "AM"}, {'|', "OR"},
        {'@', "AT"}, {'~', "TL"}, {':', "CL"}, {',', "CM"}, {'#', "HS"},
        {'%', "PC"}, {'^', "CA"}, {'[', "LB"}, {']', "RB"}, {' ', "SP"},
        {'\'', "SQ"}, {'"', "DQ"}, {0, NULL}
    };
    int di = 0;
    for (const char *p = src; *p && di < dstlen - 1; p++) {
        if (isalnum((unsigned char)*p) || *p == '_') {
            dst[di++] = *p;
            continue;
        }
        const char *nm = NULL;
        for (int i = 0; tbl[i].ch; i++)
            if (tbl[i].ch == *p) { nm = tbl[i].nm; break; }
        if (!nm) nm = "XX";
        if (di > 0 && dst[di-1] != '_') dst[di++] = '_';
        for (const char *q = nm; *q && di < dstlen - 2; q++) dst[di++] = *q;
        if (di < dstlen - 1) dst[di++] = '_';
    }
    while (di > 0 && dst[di-1] == '_') di--;
    dst[di] = '\0';
}

/* Module-level label globals — set per-statement, used by emit_expr */
static char jvm_cur_stmt_fail_label[128];  /* INPUT EOF in expr → jump here (raw SNOBOL4 label) */
static int  jvm_expr_depth = 0;            /* nesting depth; INPUT null-check only at depth==0 */

/* Forward declaration — defined after pattern emitter */
static void emit_jvm_goto(const char *label);

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
    /* ABI §5: lowercase classnames — do NOT capitalize */
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
    for (int _i = 0; _i < e->nchildren; _i++) jvm_collect_vars_expr(e->children[_i]);
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

static void emit_jvm_expr(EXPR_t *e);

/* emit_jvm_to_double — safe numeric coercion helper.
 * Pops String, pushes double. Empty string/null → 0.0 (SNOBOL4 semantics). */
static int jvm_need_sno_parse_helper  = 0;
static int jvm_need_sno_fmt_double    = 0;
static int jvm_need_input_helper      = 0;
static int jvm_need_replace_helper   = 0;
static int jvm_need_lpad_helper      = 0;
static int jvm_need_rpad_helper      = 0;
static int jvm_need_integer_helper   = 0;
static int jvm_need_datatype_helper  = 0;
static int jvm_need_array_helpers    = 0;
static int jvm_need_data_helpers     = 0;
static int jvm_need_sort_helper      = 0;
static int jvm_need_convert_helper   = 0;
static int jvm_need_prototype_helper = 0;

/* Arithmetic scratch locals: double at [jvm_arith_local_base, +1],
 * long at [jvm_arith_local_base+2, +3].
 * Default 2 is safe in main() (locals 0-1 are args, 32 slots total).
 * emit_jvm_fn_method raises this above its save area before emitting the
 * body so dstore/lstore never clobber saved Object references. */
static int jvm_arith_local_base = 2;

/* Forward declarations for user-function support (defined after emit_jvm_header) */
#define JVM_FN_MAX_FWD  128
#define JVM_ARG_MAX_FWD  32
typedef struct JvmFnDef_s {
    char  *name;
    char  *args[JVM_ARG_MAX_FWD];
    int    nargs;
    char  *locals[JVM_ARG_MAX_FWD];
    int    nlocals;
    char  *end_label;
    char  *entry_label;
} JvmFnDef;
static JvmFnDef jvm_fn_table_fwd[JVM_FN_MAX_FWD];
static int       jvm_fn_count_fwd = 0;
static const JvmFnDef *jvm_cur_fn = NULL;
static Program *jvm_cur_prog = NULL;   /* set before main emit loop; visible to emit_jvm_expr */
static const JvmFnDef *jvm_find_fn(const char *name);  /* fwd decl */

/* Named pattern registry — compile-time table of VAR = <pattern-expr> assignments.
 * Mirrors ASM backend's AsmNamedPat/asm_named[] mechanism.
 * When E_VART("P") appears in a pattern context, we look up P here and
 * inline-expand its stored pattern tree via emit_jvm_pat_node. */
#define JVM_NAMED_PAT_MAX  512
#define JVM_NAMED_NAMELEN  128
typedef struct {
    char    varname[JVM_NAMED_NAMELEN];
    EXPR_t *pat;        /* pattern expression tree */
} JvmNamedPat;
static JvmNamedPat *jvm_named_pats = NULL;
static int          jvm_named_pat_count = 0;

static void jvm_named_pat_reset(void) {
    if (!jvm_named_pats)
        jvm_named_pats = (JvmNamedPat *)calloc(JVM_NAMED_PAT_MAX, sizeof(JvmNamedPat));
    /* zero contents to kill stale EXPR_t* pointers from prior file (M-G-INV-EMIT-FIX) */
    memset(jvm_named_pats, 0, JVM_NAMED_PAT_MAX * sizeof(JvmNamedPat));
    jvm_named_pat_count = 0;
}

static void jvm_named_pat_register(const char *varname, EXPR_t *pat) {
    if (!jvm_named_pats)
        jvm_named_pats = (JvmNamedPat *)calloc(JVM_NAMED_PAT_MAX, sizeof(JvmNamedPat));
    for (int i = 0; i < jvm_named_pat_count; i++) {
        if (strcasecmp(jvm_named_pats[i].varname, varname) == 0) {
            if (pat) jvm_named_pats[i].pat = pat;
            return;
        }
    }
    if (jvm_named_pat_count >= JVM_NAMED_PAT_MAX) return;
    JvmNamedPat *e = &jvm_named_pats[jvm_named_pat_count++];
    snprintf(e->varname, JVM_NAMED_NAMELEN, "%s", varname);
    e->pat = pat;
}

static const JvmNamedPat *jvm_named_pat_lookup(const char *varname) {
    for (int i = 0; i < jvm_named_pat_count; i++)
        if (strcasecmp(jvm_named_pats[i].varname, varname) == 0)
            return &jvm_named_pats[i];
    return NULL;
}

/* expr_is_pattern_expr — mirrors ASM backend: E_OR is always a pattern;
 * E_SEQ/E_CONCAT or E_FNC is a pattern if any descendant is E_FNC/E_NAM/E_DOL. */
static int jvm_expr_has_pat_fn(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC || e->kind == E_NAM || e->kind == E_DOL) return 1;
    for (int i = 0; i < e->nchildren; i++)
        if (jvm_expr_has_pat_fn(e->children[i])) return 1;
    return 0;
}
static int jvm_expr_is_pattern_expr(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_OR)   return 1;   /* alternation is always a pattern */
    if (e->kind == E_SEQ) return 1;
    if (e->kind == E_CONCAT) return 0;
    return jvm_expr_has_pat_fn(e);
}

/* jvm_scan_named_patterns — pre-pass over whole program, register every
 * pattern variable assignment before any code is emitted. */
static void jvm_scan_named_patterns(Program *prog) {
    jvm_named_pat_reset();
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* VAR = <pattern-expr>  — subject is E_VART, has_eq set, no pattern field */
        if (s->subject && s->subject->kind == E_VART && s->subject->sval &&
            s->has_eq && s->replacement && !s->pattern) {
            if (jvm_expr_is_pattern_expr(s->replacement)) {
                jvm_named_pat_register(s->subject->sval, s->replacement);
            }
        }
    }
}

/* DATA type registry */
#define JVM_DATA_MAX 32
typedef struct { char *type_name; char *fields[JVM_ARG_MAX_FWD]; int nfields; } JvmDataType;
static JvmDataType jvm_data_types[JVM_DATA_MAX];
static int jvm_data_type_count = 0;
static const JvmDataType *jvm_find_data_type(const char *name);   /* fwd decl */
static const JvmDataType *jvm_find_data_field(const char *field); /* fwd decl — returns type if field found */

static void emit_jvm_to_double(void) {
    /* Stack: String → double.  Empty/null → 0.0 */
    jvm_need_sno_parse_helper = 1;
    char desc[512];
    snprintf(desc, sizeof desc, "%s/sno_to_double(Ljava/lang/String;)D", jvm_classname);
    JI("invokestatic", desc);
}

static void emit_jvm_parse_helper(void) {
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

/* jvm_d2sno — convert double on stack to SNOBOL4 string.
 * Whole doubles emit as "N." (CSNOBOL4 format) to match E_FLIT literals. */
static void jvm_d2sno(void) {
    jvm_need_sno_fmt_double = 1;
    char desc[512];
    snprintf(desc, sizeof desc, "%s/sno_fmt_double(D)Ljava/lang/String;", jvm_classname);
    JI("invokestatic", desc);
}

static void emit_jvm_fmt_double_helper(void) {
    if (!jvm_need_sno_fmt_double) return;
    /* sno_fmt_double(D)String — CSNOBOL4 convention:
     *   whole doubles → "N."   (e.g. 5.0 → "5.")
     *   fractional    → Double.toString(d)  (e.g. 1.5 → "1.5") */
    J(".method static sno_fmt_double(D)Ljava/lang/String;\n");
    J("    .limit stack 6\n");
    J("    .limit locals 4\n");
    J("    dload_0\n");
    J("    dstore_0\n");
    /* check if d == (double)(long)d */
    J("    dload_0\n");
    J("    d2l\n");
    J("    lstore_2\n");
    J("    lload_2\n");
    J("    l2d\n");
    J("    dload_0\n");
    J("    dcmpl\n");
    J("    ifne Lsfd_frac\n");
    /* whole: format as long then append "." */
    J("    lload_2\n");
    J("    invokestatic java/lang/Long/toString(J)Ljava/lang/String;\n");
    J("    ldc \".\"\n");
    J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    areturn\n");
    J("Lsfd_frac:\n");
    J("    dload_0\n");
    J("    invokestatic java/lang/Double/toString(D)Ljava/lang/String;\n");
    J("    areturn\n");
    J(".end method\n\n");
    jvm_need_sno_fmt_double = 0;
}

/* Push the SNOBOL4 numeric string for an integer on stack as long */
static void jvm_l2sno(void) {
    JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
}

static void emit_jvm_expr(EXPR_t *e) {
    jvm_expr_depth++;
    if (!e) {
        /* null = unset */
        JI("ldc", "\"\"");
        jvm_expr_depth--;
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
            /* INPUT — reads one line from stdin; null on EOF.
             * The statement-level emitter pre-hoists INPUT to local 5 when
             * INPUT appears nested in an expression. At depth==1 (direct RHS),
             * jvm_cur_stmt_fail_label is set and we check inline. */
            char irdesc[512];
            snprintf(irdesc, sizeof irdesc,
                "%s/sno_input_read()Ljava/lang/String;", jvm_classname);
            jvm_need_input_helper = 1;
            if (jvm_expr_depth > 1) {
                /* Nested: use pre-hoisted local 5 (set by stmt emitter) */
                J("    aload 5\n");
            } else {
                /* Top-level: emit call + null check */
                JI("invokestatic", irdesc);
                if (jvm_cur_stmt_fail_label[0]) {
                    static int inp_uid_ctr = 0;
                    char inp_ok[64];
                    snprintf(inp_ok, sizeof inp_ok, "Jinp%d_ok", inp_uid_ctr++);
                    JI("dup", "");
                    JI("ifnonnull", inp_ok);
                    JI("pop", "");
                    emit_jvm_goto(jvm_cur_stmt_fail_label);
                    J("%s:\n", inp_ok);
                }
            }
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
        EXPR_t *_op = ECHILD(e,1) ? ECHILD(e,1) : ECHILD(e,0);
        emit_jvm_expr(_op);
        char ivdesc[512];
        snprintf(ivdesc, sizeof ivdesc, "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        JI("invokestatic", ivdesc);
        break;
    }
    case E_NAM: {
        /* .var in value context — name reference: push the variable name as a string.
         * Used in $.var (E_INDR wrapping E_NAM): the name is the lookup key,
         * not the variable's value. e->sval holds the name. */
        if (e->sval) {
            char nameesc[256];
            jvm_escape_string(e->sval, nameesc, sizeof nameesc);
            JI("ldc", nameesc);
        } else {
            JI("ldc", "\"\"");
        }
        break;
    }
    case E_DIV: {
        /* SNOBOL4 division: integer/integer -> integer (truncate toward zero).
         * Emit both operands as strings into scratch locals, check sno_is_integer
         * on each; if both integers use ldiv, else ddiv. */
        static int _divlbl = 0;
        int div_id = _divlbl++;
        char div_float[32], div_done[32];
        snprintf(div_float, sizeof div_float, "Ldivf_%d", div_id);
        snprintf(div_done,  sizeof div_done,  "Ldivd_%d", div_id);
        /* loc_s0, loc_s1: string operands (single-wide Object refs) */
        int loc_s0 = jvm_arith_local_base + 4;
        int loc_s1 = jvm_arith_local_base + 5;
        int loc_d  = jvm_arith_local_base;       /* double scratch */
        int loc_l  = jvm_arith_local_base + 2;   /* long   scratch */
        jvm_need_integer_helper = 1;
        /* Evaluate and stash operands */
        emit_jvm_expr(e->children[0]);
        J("    astore %d\n", loc_s0);
        emit_jvm_expr(e->children[1]);
        J("    astore %d\n", loc_s1);
        /* Check both are integers */
        char desc_isi[512];
        snprintf(desc_isi, sizeof desc_isi, "%s/sno_is_integer(Ljava/lang/String;)Z", jvm_classname);
        J("    aload %d\n", loc_s0);
        JI("invokestatic", desc_isi);
        J("    ifeq %s\n", div_float);    /* s0 not integer -> float path */
        J("    aload %d\n", loc_s1);
        JI("invokestatic", desc_isi);
        J("    ifeq %s\n", div_float);    /* s1 not integer -> float path */
        /* Integer path: Long.parseLong(s0) / Long.parseLong(s1) */
        J("    aload %d\n", loc_s0);
        JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        J("    aload %d\n", loc_s1);
        JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        JI("ldiv", "");
        jvm_l2sno();
        JI("goto", div_done);
        /* Float path: sno_to_double(s0) / sno_to_double(s1), then whole-check */
        J("%s:\n", div_float);
        J("    aload %d\n", loc_s0);
        emit_jvm_to_double();
        J("    aload %d\n", loc_s1);
        emit_jvm_to_double();
        JI("ddiv", "");
        /* whole-number check */
        J("    dstore %d\n", loc_d);
        J("    dload %d\n",  loc_d);
        JI("d2l",  "");
        J("    lstore %d\n", loc_l);
        J("    lload %d\n",  loc_l);
        JI("l2d",  "");
        J("    dload %d\n",  loc_d);
        JI("dcmpl", "");
        char div_frac[32];
        snprintf(div_frac, sizeof div_frac, "Ldivfr_%d", div_id);
        JI("ifne", div_frac);
        J("    dload %d\n", loc_d);
        jvm_d2sno();
        JI("goto", div_done);
        J("%s:\n", div_frac);
        J("    dload %d\n", loc_d);
        jvm_d2sno();
        J("%s:\n", div_done);
        break;
    }
    case E_ADD:
    case E_SUB:
    case E_MPY:
    case E_EXPOP: {
        /* Arithmetic: parse both sides to double, operate, convert back */
        emit_jvm_expr(e->children[0]);
        emit_jvm_to_double();
        emit_jvm_expr(e->children[1]);
        emit_jvm_to_double();
        switch (e->kind) {
        case E_ADD:   JI("dadd", ""); break;
        case E_SUB:   JI("dsub", ""); break;
        case E_MPY:   JI("dmul", ""); break;
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
        int loc_d = jvm_arith_local_base;     /* double stored at locals base, base+1 */
        int loc_l = jvm_arith_local_base + 2; /* long  stored at locals base+2, base+3 */
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
        /* Whole: reload double and format via sno_fmt_double → "N." */
        J("    dload %d\n", loc_d);
        jvm_d2sno();
        JI("goto", ardone);
        J("%s:\n", arfrac);
        /* Fractional: emit as double string */
        J("    dload %d\n", loc_d);
        jvm_d2sno();
        J("%s:\n", ardone);
        break;
    }
    case E_CONCAT: {  /* M-G4-SPLIT-SEQ-CONCAT: value context — StringBuilder concat */
        /* String concatenation: StringBuilder — n-ary, fold all children.
         * Null propagation: if any child returns null (failure), discard
         * the StringBuilder and return null to propagate the failure. */
        static int _conc_uid = 0;
        int conc_id = _conc_uid++;
        char conc_done[64];
        snprintf(conc_done, sizeof conc_done, "Lconc_done_%d", conc_id);
        JI("new", "java/lang/StringBuilder");
        JI("dup", "");
        JI("invokespecial", "java/lang/StringBuilder/<init>()V");
        for (int _ci = 0; _ci < e->nchildren; _ci++) {
            emit_jvm_expr(e->children[_ci]);
            /* stack: ..., sb, child_val */
            /* null check: if child is null, pop child + sb, push null */
            char conc_ok[64];
            snprintf(conc_ok, sizeof conc_ok, "Lconc_ok_%d_%d", conc_id, _ci);
            JI("dup", "");                    /* ..., sb, child_val, child_val */
            JI("ifnonnull", conc_ok);         /* if non-null, continue */
            /* child is null -- discard child_val, pop sb, push null */
            JI("pop", "");                    /* ..., sb */
            JI("pop", "");                    /* ... */
            JI("aconst_null", "");            /* ..., null */
            JI("goto", conc_done);
            J("%s:\n", conc_ok);
            JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
        }
        JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
        J("%s:\n", conc_done);
        break;
    }
    case E_MNS: {
        /* Unary minus: -expr */
        emit_jvm_expr(e->children[0]);
        emit_jvm_to_double();
        JI("dneg", "");
        static int _mnslbl = 0;
        int loc_d = jvm_arith_local_base, loc_l = jvm_arith_local_base + 2;
        char mfrac[32], mdone[32];
        snprintf(mfrac, sizeof mfrac, "Lmnsf_%d", _mnslbl);
        snprintf(mdone, sizeof mdone, "Lmnsd_%d", _mnslbl++);
        J("    dstore %d\n", loc_d);
        J("    dload %d\n",  loc_d);
        JI("d2l", ""); J("    lstore %d\n", loc_l);
        J("    lload %d\n", loc_l); JI("l2d", "");
        J("    dload %d\n", loc_d); JI("dcmpl", "");
        JI("ifne", mfrac);
        J("    dload %d\n", loc_d); jvm_d2sno(); JI("goto", mdone);
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
        if (is_arith && e->children && e->children[0] && e->children[1]) {
            emit_jvm_expr(e->children[0]);
            emit_jvm_to_double();
            emit_jvm_expr(e->children[1]);
            emit_jvm_to_double();
            switch (arith_op) {
            case 0: JI("dadd", ""); break;
            case 1: JI("dsub", ""); break;
            case 2: JI("dmul", ""); break;
            case 3: JI("ddiv", ""); break;
            case 4: JI("invokestatic", "java/lang/Math/pow(DD)D"); break;
            }
            static int _fnarlbl = 0;
            int loc_d = jvm_arith_local_base, loc_l = jvm_arith_local_base + 2;
            char ffrac[32], ffdone[32];
            snprintf(ffrac,  sizeof ffrac,  "Lfnarf_%d", _fnarlbl);
            snprintf(ffdone, sizeof ffdone, "Lfnard_%d", _fnarlbl++);
            J("    dstore %d\n", loc_d);
            J("    dload %d\n",  loc_d);
            JI("d2l", ""); J("    lstore %d\n", loc_l);
            J("    lload %d\n", loc_l); JI("l2d", "");
            J("    dload %d\n", loc_d); JI("dcmpl", "");
            JI("ifne", ffrac);
            J("    dload %d\n", loc_d); jvm_d2sno(); JI("goto", ffdone);
            J("%s:\n", ffrac); J("    dload %d\n", loc_d); jvm_d2sno();
            J("%s:\n", ffdone);
            break;
        }

        /* neg(x) — unary minus */
        if (strcasecmp(fname, "neg") == 0 && e->children && e->children[0]) {
            emit_jvm_expr(e->children[0]);
            emit_jvm_to_double();
            JI("dneg", "");
            /* convert back: whole-number check using jvm_arith_local_base slots */
            static int _neglbl = 0;
            int loc_d = jvm_arith_local_base, loc_l = jvm_arith_local_base + 2;
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
            J("    dload %d\n", loc_d);
            jvm_d2sno();
            JI("goto", ndone);
            J("%s:\n", nfrac);
            J("    dload %d\n", loc_d);
            jvm_d2sno();
            J("%s:\n", ndone);
            break;
        }
        /* abs(x) — absolute value */
        if (strcasecmp(fname, "abs") == 0 && e->children && e->children[0]) {
            emit_jvm_expr(e->children[0]);
            emit_jvm_to_double();
            JI("invokestatic", "java/lang/Math/abs(D)D");
            static int _abslbl = 0;
            int loc_d = jvm_arith_local_base, loc_l = jvm_arith_local_base + 2;
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
            J("    dload %d\n", loc_d);
            jvm_d2sno();
            JI("goto", adone);
            J("%s:\n", afrac);
            J("    dload %d\n", loc_d);
            jvm_d2sno();
            J("%s:\n", adone);
            break;
        }
        /* SIZE(str) → length as decimal string */
        if (strcasecmp(fname, "SIZE") == 0 && e->children && e->children[0]) {
            emit_jvm_expr(e->children[0]);
            JI("invokevirtual", "java/lang/String/length()I");
            JI("i2l", "");
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
            break;
        }
        /* DUPL(str, n) → str repeated n times (Java 11+ String.repeat) */
        if (strcasecmp(fname, "DUPL") == 0 && e->children && e->children[0] && e->children[1]) {
            for (int _i = 0; _i < e->nchildren; _i++) emit_jvm_expr(e->children[_i]);
            emit_jvm_to_double();
            JI("d2i", "");
            JI("invokevirtual", "java/lang/String/repeat(I)Ljava/lang/String;");
            break;
        }
        /* REMDR(a, b) → a mod b as decimal string */
        if (strcasecmp(fname, "REMDR") == 0 && e->children && e->children[0] && e->children[1]) {
            emit_jvm_expr(e->children[0]);
            emit_jvm_to_double(); JI("d2l", "");
            emit_jvm_expr(e->children[1]);
            emit_jvm_to_double(); JI("d2l", "");
            JI("lrem", "");
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
            break;
        }
        /* IDENT(a,b) → succeeds (returns a) if a equals b, fails (null) otherwise */
        if (strcasecmp(fname, "IDENT") == 0 && e->children && e->children[0]) {
            static int _identlbl = 0;
            char ifail[32], idone[32], inull[32];
            snprintf(ifail,  sizeof ifail,  "Lident_f_%d", _identlbl);
            snprintf(idone,  sizeof idone,  "Lident_d_%d", _identlbl);
            snprintf(inull,  sizeof inull,  "Lident_n_%d", _identlbl++);
            /* arg0: null (uninitialized table/array slot) → "" per SNOBOL4 semantics */
            emit_jvm_expr(e->children[0]);
            JI("dup", "");
            JI("ifnonnull", inull);
            JI("pop", "");
            JI("ldc", "\"\"");
            J("%s:\n", inull);
            if (ECHILD(e,1)) {
                emit_jvm_expr(ECHILD(e,1));
                /* arg1 null-coerce too */
                char inull2[32]; snprintf(inull2, sizeof inull2, "Lident_n2_%d", _identlbl-1);
                JI("dup", "");
                JI("ifnonnull", inull2);
                JI("pop", "");
                JI("ldc", "\"\"");
                J("%s:\n", inull2);
            } else {
                JI("ldc", "\"\"");
            }
            JI("invokevirtual", "java/lang/String/equals(Ljava/lang/Object;)Z");
            JI("ifeq", ifail);
            /* success: push arg0 again (re-evaluate; null → "") */
            emit_jvm_expr(e->children[0]);
            JI("dup", "");
            char inull3[32]; snprintf(inull3, sizeof inull3, "Lident_n3_%d", _identlbl-1);
            JI("ifnonnull", inull3);
            JI("pop", "");
            JI("ldc", "\"\"");
            J("%s:\n", inull3);
            JI("goto", idone);
            J("%s:\n", ifail);
            JI("aconst_null", "");
            J("%s:\n", idone);
            break;
        }
        /* DIFFER(a,b) → succeeds (returns "") if a differs from b, fails (null) if equal.
         * CSNOBOL4 semantics: DIFFER is a predicate — returns empty string on success,
         * not the first argument. Verified against CSNOBOL4 2.3.3. */
        if (strcasecmp(fname, "DIFFER") == 0 && e->children && e->children[0]) {
            static int _difflbl = 0;
            char dfail[32], ddone[32], dnull0[32];
            snprintf(dfail,  sizeof dfail,  "Ldiff_f_%d",  _difflbl);
            snprintf(ddone,  sizeof ddone,  "Ldiff_d_%d",  _difflbl);
            snprintf(dnull0, sizeof dnull0, "Ldiff_n0_%d", _difflbl++);
            emit_jvm_expr(e->children[0]);
            JI("dup", "");
            JI("ifnonnull", dnull0);
            JI("pop", "");
            JI("ldc", "\"\"");
            J("%s:\n", dnull0);
            if (ECHILD(e,1)) {
                char dnull1[32]; snprintf(dnull1, sizeof dnull1, "Ldiff_n1_%d", _difflbl-1);
                emit_jvm_expr(ECHILD(e,1));
                JI("dup", "");
                JI("ifnonnull", dnull1);
                JI("pop", "");
                JI("ldc", "\"\"");
                J("%s:\n", dnull1);
            } else {
                JI("ldc", "\"\"");
            }
            JI("invokevirtual", "java/lang/String/equals(Ljava/lang/Object;)Z");
            JI("ifne", dfail);
            JI("ldc", "\"\"");   /* success: return empty string (predicate semantics) */
            JI("goto", ddone);
            J("%s:\n", dfail);
            JI("aconst_null", "");
            J("%s:\n", ddone);
            break;
        }
        /* EQ/NE/LT/LE/GT/GE — numeric comparisons
         * EQ(a,b): succeeds (returns b) if a == b numerically, else null
         * NE/LT/LE/GT/GE: similar                                        */
        {
            static int _cmplbl = 0;
            /* dcmpl returns: 0 if a==b, -1 if a<b, 1 if a>b */
            /* We branch to fail when condition is NOT met */
            /* ifXX jumps to fail: */
            const char *jmp_to_fail = NULL;
            if      (strcasecmp(fname,"EQ")==0) jmp_to_fail = "ifne";
            else if (strcasecmp(fname,"NE")==0) jmp_to_fail = "ifeq";
            else if (strcasecmp(fname,"LT")==0) jmp_to_fail = "ifge";
            else if (strcasecmp(fname,"LE")==0) jmp_to_fail = "ifgt";
            else if (strcasecmp(fname,"GT")==0) jmp_to_fail = "ifle";
            else if (strcasecmp(fname,"GE")==0) jmp_to_fail = "iflt";

            if (jmp_to_fail) {
                char cfail[40], cdone[40];
                snprintf(cfail, sizeof cfail, "Lcmp_f_%d", _cmplbl);
                snprintf(cdone, sizeof cdone, "Lcmp_d_%d", _cmplbl++);
                EXPR_t *a0 = ECHILD(e,0);
                EXPR_t *a1 = ECHILD(e,1);
                if (a0) emit_jvm_expr(a0); else JI("ldc", "\"0\"");
                emit_jvm_to_double();
                if (a1) emit_jvm_expr(a1); else JI("ldc", "\"0\"");
                emit_jvm_to_double();
                JI("dcmpl", "");
                J("    %s %s\n", jmp_to_fail, cfail);
                /* success: return empty string (EQ/NE/LT etc. are predicates) */
                JI("ldc", "\"\"");
                JI("goto", cdone);
                J("%s:\n", cfail);
                JI("aconst_null", "");
                J("%s:\n", cdone);
                break;
            }
        }
        /* SUBSTR(str, start, len) → substring (1-based start, SNOBOL4 semantics) */
        if (strcasecmp(fname, "SUBSTR") == 0 && e->children && e->children[0]) {
            static int _sublbl = 0;
            char sfail[40], sdone[40];
            snprintf(sfail, sizeof sfail, "Lsub_f_%d", _sublbl);
            snprintf(sdone, sizeof sdone, "Lsub_d_%d", _sublbl++);
            /* SUBSTR(str, start [, len])
             * start is 1-based; len defaults to rest of string */
            emit_jvm_expr(e->children[0]);  /* str */
            /* get string length */
            JI("dup", "");
            JI("invokevirtual", "java/lang/String/length()I");
            /* store slen; we need: str, start0, end0 */
            /* Use Double.parseDouble approach for start */
            EXPR_t *a1 = ECHILD(e,1);
            EXPR_t *a2 = ECHILD(e,2);
            /* Stack: str, slen */
            /* store str and slen in scratch locals via helper approach:
             * emit inline without extra locals for simplicity — use StringBuilder
             * We'll use invokevirtual substring after computing start0/end0
             * approach: push str, push start-1, push end, call substring */
            /* Discard slen pushed above — just use str */
            JI("pop", "");   /* discard the length, recompute as needed */
            /* Stack: str */
            /* store str in scratch — use a static int approach: push str, then args */
            /* Simplest: str.substring(start-1, start-1+len) */
            /* We need start and optional len */
            if (a1) { emit_jvm_expr(a1); } else { JI("ldc", "\"1\""); }
            emit_jvm_to_double(); JI("d2i", "");  /* start (1-based) */
            JI("iconst_1", ""); JI("isub", "");   /* start0 = start-1 */
            if (a2) {
                /* end0 = start0 + len */
                JI("dup", "");  /* dup start0 */
                emit_jvm_expr(a2);
                emit_jvm_to_double(); JI("d2i", "");
                JI("iadd", ""); /* end0 */
                /* Stack: str, start0, end0 */
                JI("invokevirtual", "java/lang/String/substring(II)Ljava/lang/String;");
            } else {
                /* substring(start0) — to end */
                JI("invokevirtual", "java/lang/String/substring(I)Ljava/lang/String;");
            }
            break;
        }
        /* REPLACE(str, from, to) → translate chars in str: for each char in from replace with corresponding char in to */
        if (strcasecmp(fname, "REPLACE") == 0 && e->children && e->children[0] && e->children[1] && e->children[2]) {
            static int _replbl = 0;
            char rloop[40], rdone[40];
            snprintf(rloop, sizeof rloop, "Lrep_lp_%d", _replbl);
            snprintf(rdone, sizeof rdone, "Lrep_dn_%d", _replbl++);
            /* Use a static helper method sno_replace(str,from,to) */
            jvm_need_replace_helper = 1;
            for (int _i = 0; _i < e->nchildren; _i++) emit_jvm_expr(e->children[_i]);
            char rhdesc[512];
            snprintf(rhdesc, sizeof rhdesc,
                "%s/sno_replace(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                jvm_classname);
            JI("invokestatic", rhdesc);
            break;
        }
        /* TRIM(str) → remove trailing whitespace */
        if (strcasecmp(fname, "TRIM") == 0 && e->children && e->children[0]) {
            emit_jvm_expr(e->children[0]);
            /* replaceAll("\\s+$","") */
            JI("ldc", "\"\\\\s+$\"");
            JI("ldc", "\"\"");
            JI("invokevirtual", "java/lang/String/replaceAll(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
            break;
        }
        /* REVERSE(str) → reverse string */
        if (strcasecmp(fname, "REVERSE") == 0 && e->children && e->children[0]) {
            JI("new", "java/lang/StringBuilder");
            JI("dup", "");
            emit_jvm_expr(e->children[0]);
            JI("invokespecial", "java/lang/StringBuilder/<init>(Ljava/lang/String;)V");
            JI("invokevirtual", "java/lang/StringBuilder/reverse()Ljava/lang/StringBuilder;");
            JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
            break;
        }
        /* LPAD(str, n [, pad]) → left-pad string to length n */
        if (strcasecmp(fname, "LPAD") == 0 && ECHILD(e,0) && ECHILD(e,1)) {
            jvm_need_lpad_helper = 1;
            for (int _i = 0; _i < e->nchildren && _i < 2; _i++) emit_jvm_expr(e->children[_i]);
            emit_jvm_to_double(); JI("d2i", "");
            EXPR_t *pad_arg = ECHILD(e,2);
            if (pad_arg) { emit_jvm_expr(pad_arg); }
            else { JI("ldc", "\" \""); }
            char lpdesc[512];
            snprintf(lpdesc, sizeof lpdesc,
                "%s/sno_lpad(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;",
                jvm_classname);
            JI("invokestatic", lpdesc);
            break;
        }
        /* RPAD(str, n [, pad]) → right-pad string to length n */
        if (strcasecmp(fname, "RPAD") == 0 && ECHILD(e,0) && ECHILD(e,1)) {
            jvm_need_rpad_helper = 1;
            for (int _i = 0; _i < e->nchildren && _i < 2; _i++) emit_jvm_expr(e->children[_i]);
            emit_jvm_to_double(); JI("d2i", "");
            EXPR_t *pad_arg = ECHILD(e,2);
            if (pad_arg) { emit_jvm_expr(pad_arg); }
            else { JI("ldc", "\" \""); }
            char rpdesc[512];
            snprintf(rpdesc, sizeof rpdesc,
                "%s/sno_rpad(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;",
                jvm_classname);
            JI("invokestatic", rpdesc);
            break;
        }
        /* INTEGER(x) → succeeds (returns x) if x is an integer string, fails otherwise */
        if (strcasecmp(fname, "INTEGER") == 0 && e->children && e->children[0]) {
            static int _intlbl = 0;
            char ifail[40], idone[40];
            snprintf(ifail, sizeof ifail, "Lint_f_%d", _intlbl);
            snprintf(idone, sizeof idone, "Lint_d_%d", _intlbl++);
            jvm_need_integer_helper = 1;
            emit_jvm_expr(e->children[0]);
            char ihdesc[512];
            snprintf(ihdesc, sizeof ihdesc,
                "%s/sno_is_integer(Ljava/lang/String;)Z", jvm_classname);
            JI("dup", "");
            JI("invokestatic", ihdesc);
            JI("ifeq", ifail);
            JI("goto", idone);
            J("%s:\n", ifail);
            JI("pop", "");
            JI("aconst_null", "");
            J("%s:\n", idone);
            break;
        }
        /* DATATYPE(x) → type name string.
         * For DATA instances: returns the user-defined type name (stored as __type__).
         * For plain strings: returns "STRING", "INTEGER", or "REAL". */
        if (strcasecmp(fname, "DATATYPE") == 0 && e->children && e->children[0]) {
            jvm_need_datatype_helper = 1;
            jvm_need_data_helpers   = 1;
            jvm_need_array_helpers  = 1;
            emit_jvm_expr(e->children[0]);
            char dtdesc[512];
            snprintf(dtdesc, sizeof dtdesc,
                "%s/sno_datatype_ext(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", dtdesc);
            break;
        }
        /* Lexical comparison: LGT/LLT/LGE/LLE/LEQ/LNE */
        {
            static int _lcmplbl = 0;
            const char *ljmp = NULL;
            if      (strcasecmp(fname,"LGT")==0) ljmp = "ifle";
            else if (strcasecmp(fname,"LLT")==0) ljmp = "ifge";
            else if (strcasecmp(fname,"LGE")==0) ljmp = "iflt";
            else if (strcasecmp(fname,"LLE")==0) ljmp = "ifgt";
            else if (strcasecmp(fname,"LEQ")==0) ljmp = "ifne";
            else if (strcasecmp(fname,"LNE")==0) ljmp = "ifeq";
            if (ljmp) {
                char lcfail[40], lcdone[40];
                snprintf(lcfail, sizeof lcfail, "Llcmp_f_%d", _lcmplbl);
                snprintf(lcdone, sizeof lcdone, "Llcmp_d_%d", _lcmplbl++);
                EXPR_t *a0 = ECHILD(e,0);
                EXPR_t *a1 = ECHILD(e,1);
                if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
                if (a1) emit_jvm_expr(a1); else JI("ldc", "\"\"");
                JI("invokevirtual", "java/lang/String/compareTo(Ljava/lang/String;)I");
                J("    %s %s\n", ljmp, lcfail);
                JI("ldc", "\"\"");
                JI("goto", lcdone);
                J("%s:\n", lcfail);
                JI("aconst_null", "");
                J("%s:\n", lcdone);
                break;
            }
        }
        /* DEFINE('proto') — evaluated at runtime but a no-op in expression context
         * (function registration happens at compile time via jvm_collect_functions) */
        if (strcasecmp(fname, "DEFINE") == 0) {
            JI("ldc", "\"\"");
            break;
        }
        /* ARRAY(n) or ARRAY(n, init) — create indexed array, return an array-id string */
        if (strcasecmp(fname, "ARRAY") == 0) {
            jvm_need_array_helpers = 1;
            EXPR_t *a0 = ECHILD(e,0);
            EXPR_t *a1 = ECHILD(e,1);
            if (a0) emit_jvm_expr(a0); else JI("ldc", "\"1\"");
            if (a1) emit_jvm_expr(a1); else JI("ldc", "\"\"");
            char ardesc[512];
            snprintf(ardesc, sizeof ardesc,
                "%s/sno_array_new2(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", ardesc);
            break;
        }
        /* TABLE([n]) — create table (HashMap), return a table-id string */
        if (strcasecmp(fname, "TABLE") == 0) {
            jvm_need_array_helpers = 1;
            char tdesc[512];
            snprintf(tdesc, sizeof tdesc,
                "%s/sno_table_new()Ljava/lang/String;", jvm_classname);
            JI("invokestatic", tdesc);
            break;
        }
        /* SORT(table) → 2D array[1..n,1..2]: col1=key, col2=value, sorted by key */
        if (strcasecmp(fname, "SORT") == 0) {
            jvm_need_array_helpers = 1;
            EXPR_t *a0 = (e->children && e->children[0]) ? e->children[0] : NULL;
            if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
            char sdesc[512];
            snprintf(sdesc, sizeof sdesc,
                "%s/sno_sort(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", sdesc);
            jvm_need_sort_helper = 1;
            break;
        }
        /* CONVERT(val, type) — type coercions */
        if (strcasecmp(fname, "CONVERT") == 0) {
            EXPR_t *a0 = (e->children && e->children[0]) ? e->children[0] : NULL;
            EXPR_t *a1 = (e->children && e->children[1]) ? e->children[1] : NULL;
            /* If type arg is a string literal we can dispatch statically */
            if (a1 && a1->kind == E_QLIT && a1->sval) {
                if (strcasecmp(a1->sval, "integer") == 0) {
                    /* CONVERT(x,'integer'): parse to double, truncate to long, back to string */
                    if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
                    emit_jvm_to_double();
                    JI("d2l", "");
                    jvm_l2sno();
                    break;
                }
                if (strcasecmp(a1->sval, "real") == 0) {
                    /* CONVERT(x,'real'): parse to double, format as sno real */
                    if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
                    emit_jvm_to_double();
                    jvm_d2sno();
                    break;
                }
                if (strcasecmp(a1->sval, "string") == 0) {
                    /* CONVERT(x,'string'): already a string in SNOBOL4 JVM model */
                    if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
                    break;
                }
                /* 'ARRAY' type on a table — original implementation */
                if (strcasecmp(a1->sval, "array") == 0) {
                    jvm_need_array_helpers  = 1;
                    jvm_need_convert_helper = 1;
                    if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
                    char cvdesc[512];
                    snprintf(cvdesc, sizeof cvdesc,
                        "%s/sno_convert_table(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
                    JI("invokestatic", cvdesc);
                    break;
                }
            }
            /* Dynamic/unknown type: fall back to table convert (pre-existing behaviour) */
            jvm_need_array_helpers  = 1;
            jvm_need_convert_helper = 1;
            if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
            char cvdesc[512];
            snprintf(cvdesc, sizeof cvdesc,
                "%s/sno_convert_table(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", cvdesc);
            break;
        }
        /* PROTOTYPE(array) — returns "n,2" for a 2-D array of n rows, "n" for 1-D */
        if (strcasecmp(fname, "PROTOTYPE") == 0) {
            jvm_need_array_helpers    = 1;
            jvm_need_prototype_helper = 1;
            EXPR_t *a0 = (e->children && e->children[0]) ? e->children[0] : NULL;
            if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
            char ptdesc[512];
            snprintf(ptdesc, sizeof ptdesc,
                "%s/sno_prototype(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", ptdesc);
            break;
        }
        if (strcasecmp(fname, "DATA") == 0) {
            jvm_need_data_helpers = 1;
            EXPR_t *a0 = (e->children && e->children[0]) ? e->children[0] : NULL;
            if (a0) emit_jvm_expr(a0); else JI("ldc", "\"\"");
            char ddesc[512];
            snprintf(ddesc, sizeof ddesc,
                "%s/sno_data_define(Ljava/lang/String;)V", jvm_classname);
            JI("invokestatic", ddesc);
            JI("ldc", "\"\"");
            break;
        }
        /* M-LINK-JVM: cross-language IMPORT call (ARCH-scrip-abi.md §3)
         * Emits: pack args into Object[], invokestatic assembly/METHOD(...V),
         * read result from ByrdBoxLinkage/RESULT, cast to String. */
        {
            ImportEntry *ie = jvm_find_import(jvm_cur_prog, fname);
            if (ie) {
                int nargs = e->nchildren;
                /* Pack args into Object[] */
                if (nargs == 0) {
                    JI("aconst_null", "");
                } else {
                    J("    ldc %d\n", nargs);
                    JI("anewarray", "java/lang/Object");
                    for (int i = 0; i < nargs; i++) {
                        JI("dup", "");
                        J("    ldc %d\n", i);
                        if (e->children && e->children[i]) emit_jvm_expr(e->children[i]);
                        else JI("ldc", "\"\"");
                        JI("aastore", "");
                    }
                }
                /* null γ, null ω — synchronous call, result via RESULT */
                JI("aconst_null", "");
                JI("aconst_null", "");
                char sig[512];
                snprintf(sig, sizeof sig,
                    "%s/%s([Ljava/lang/Object;Ljava/lang/Runnable;Ljava/lang/Runnable;)V",
                    ie->name, ie->method);
                JI("invokestatic", sig);
                /* Read result from ByrdBoxLinkage.RESULT */
                J("    getstatic ByrdBoxLinkage/RESULT Ljava/util/concurrent/atomic/AtomicReference;\n");
                JI("invokevirtual", "java/util/concurrent/atomic/AtomicReference/get()Ljava/lang/Object;");
                JI("checkcast", "java/lang/String");
                break;
            }
        }
        /* User-defined function call */
        {
            const JvmFnDef *ufn = jvm_find_fn(fname);
            if (ufn) {
                /* Push args */
                for (int i = 0; i < ufn->nargs; i++) {
                    if (e->children && e->children[i]) emit_jvm_expr(e->children[i]);
                    else JI("ldc", "\"\"");
                }
                /* Build descriptor */
                char udesc[1024]; int dp2 = 0;
                udesc[dp2++] = '(';
                for (int i = 0; i < ufn->nargs; i++) {
                    const char *s2 = "Ljava/lang/String;";
                    for (const char *p = s2; *p; p++) udesc[dp2++] = *p;
                }
                udesc[dp2++] = ')';
                const char *rs = "Ljava/lang/String;";
                for (const char *p = rs; *p; p++) udesc[dp2++] = *p;
                udesc[dp2] = '\0';
                char umdesc[1536];
                snprintf(umdesc, sizeof umdesc, "%s/sno_userfn_%s%s",
                         jvm_classname, ufn->name, udesc);
                JI("invokestatic", umdesc);
                break;
            }
        }
        /* DATA type constructor call: typename(field1val, field2val, ...)
         * If fname matches a registered DATA type, create a HashMap instance,
         * store each field value keyed by field name, store __type__, return id. */
        {
            const JvmDataType *dt = jvm_find_data_type(fname);
            if (dt) {
                jvm_need_data_helpers  = 1;
                jvm_need_array_helpers = 1;
                /* Allocate new HashMap via sno_array_new("0") */
                JI("ldc", "\"0\"");
                char andesc[512]; snprintf(andesc, sizeof andesc,
                    "%s/sno_array_new(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
                JI("invokestatic", andesc);
                /* stack: instance_id — store each field */
                for (int fi = 0; fi < dt->nfields; fi++) {
                    /* dup instance_id, push field name, push value, call sno_array_put */
                    JI("dup", "");
                    char fnesc[256]; jvm_escape_string(dt->fields[fi], fnesc, sizeof fnesc);
                    JI("ldc", fnesc);
                    if (e->children && fi < e->nchildren && e->children[fi]) emit_jvm_expr(e->children[fi]);
                    else JI("ldc", "\"\"");
                    char apdesc[512]; snprintf(apdesc, sizeof apdesc,
                        "%s/sno_array_put(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                        jvm_classname);
                    JI("invokestatic", apdesc);
                }
                /* Store __type__ = fname */
                JI("dup", "");
                JI("ldc", "\"__type__\"");
                char typeesc[256]; jvm_escape_string(fname, typeesc, sizeof typeesc);
                JI("ldc", typeesc);
                char apdesc2[512]; snprintf(apdesc2, sizeof apdesc2,
                    "%s/sno_array_put(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                    jvm_classname);
                JI("invokestatic", apdesc2);
                /* stack: instance_id — return it */
                break;
            }
        }
        /* DATA field accessor call: fieldname(instance) → field value */
        {
            const JvmDataType *ft = jvm_find_data_field(fname);
            if (ft) {
                jvm_need_data_helpers = 1;
                /* push instance_id */
                if (e->children && e->children[0]) emit_jvm_expr(e->children[0]);
                else JI("ldc", "\"\"");
                /* push field name */
                char fnesc[256]; jvm_escape_string(fname, fnesc, sizeof fnesc);
                JI("ldc", fnesc);
                char dgfdesc[512]; snprintf(dgfdesc, sizeof dgfdesc,
                    "%s/sno_data_get_field(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                    jvm_classname);
                JI("invokestatic", dgfdesc);
                break;
            }
        }
        /* Unrecognised function — stub as empty string */
        JI("ldc", "\"\"");
        break;
    }
    case E_IDX: {
        /* E_IDX = canonical (absorbs E_ARY via compat alias — M-G1-IR-HEADER-WIRE).
         * Named-array path (sval set): load array by name, then subscript key.
         * Postfix-subscript path (sval NULL): emit expr, then subscript key. */
        jvm_need_array_helpers = 1;
        if (e->sval) {
            /* Named array: a[sub] — sno_indr_get(name) → sno_array_get(arr, key) */
            char nameesc_ary[256]; jvm_escape_string(e->sval, nameesc_ary, sizeof nameesc_ary);
            JI("ldc", nameesc_ary);
            char igdesc_ary[512]; snprintf(igdesc_ary, sizeof igdesc_ary,
                "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", igdesc_ary);
            if (e->children && e->children[0] && e->nchildren >= 2 && e->children[1]) {
                JI("new", "java/lang/StringBuilder");
                JI("dup", "");
                JI("invokespecial", "java/lang/StringBuilder/<init>()V");
                emit_jvm_expr(e->children[0]);
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                JI("ldc", "\",\"");
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                emit_jvm_expr(e->children[1]);
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
            } else if (e->children && e->children[0]) {
                emit_jvm_expr(e->children[0]);
            } else {
                JI("ldc", "\"1\"");
            }
        } else {
            /* Postfix subscript: expr[sub] — children[0]=array expr, [1..]=subscripts */
            emit_jvm_expr(e->children[0]);
            if (e->nchildren >= 3 && e->children[1] && e->children[2]) {
                JI("new", "java/lang/StringBuilder");
                JI("dup", "");
                JI("invokespecial", "java/lang/StringBuilder/<init>()V");
                emit_jvm_expr(e->children[1]);
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                JI("ldc", "\",\"");
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                emit_jvm_expr(e->children[2]);
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
            } else if (e->nchildren >= 2 && e->children[1]) {
                emit_jvm_expr(e->children[1]);
            } else if (e->children && e->children[0]) {
                emit_jvm_expr(e->children[0]);
            } else {
                JI("ldc", "\"0\"");
            }
        }
        char agdesc_idx[512]; snprintf(agdesc_idx, sizeof agdesc_idx,
            "%s/sno_array_get(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        JI("invokestatic", agdesc_idx);
        break;
    }
    default:
        /* Unsupported expr kind — push empty string as stub */
        JI("ldc", "\"\"");
        break;
    }
    jvm_expr_depth--;
}

/* -----------------------------------------------------------------------
 * Byrd box pattern node emitter — Sprint J4
 *
 * emit_jvm_pat_node(pat, γ, ω, loc_subj, loc_cursor, loc_len,
 *                   p_cap_local, out, classname)
 *
 * Emits Jasmin bytecode for one pattern node.
 * On match success:  falls through to / jumps to  γ
 * On match failure:  jumps to  ω
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
static void emit_jvm_pat_node(EXPR_t *pat,
                               const char *γ, const char *ω,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_cap_local,
                               FILE *out, const char *classname);

static void emit_jvm_pat_node(EXPR_t *pat,
                               const char *γ, const char *ω,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_cap_local,
                               FILE *out, const char *classname) {
    if (!pat) {
        /* empty pattern — always succeeds */
        fprintf(out, "    goto %s\n", γ);
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
         * On success: cursor += lit.length(); goto γ
         * On failure: goto ω                                          */
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
        PN("ifeq %s", ω);    /* false → fail */
        /* success: advance cursor */
        PN("iinc %d %d", loc_cursor, slen);
        PN("goto %s", γ);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* ------------------------------------------------------------------ */
    case E_SEQ: {  /* M-G4-SPLIT-SEQ-CONCAT: pattern context — Byrd-box SEQ */
        /* SEQ node.  Walk right-spine of left subtree to find trailing ARB or
         * ARB.NAM node; if found, emit greedy ARB+backtrack loop around right. */
        EXPR_t *arb_nam = NULL;
        {
            EXPR_t *cur = pat->children[0];
            while (cur && cur->kind == E_SEQ) cur = cur->children[1];
            if (cur && cur->kind == E_NAM && cur->children[0] &&
                ((cur->children[0]->kind == E_FNC  && cur->children[0]->sval && strcasecmp(cur->children[0]->sval, "ARB") == 0) ||
                 (cur->children[0]->kind == E_VART && cur->children[0]->sval && strcasecmp(cur->children[0]->sval, "ARB") == 0)))
                arb_nam = cur;
            else if (cur &&
                ((cur->kind == E_FNC  && cur->sval && strcasecmp(cur->sval, "ARB") == 0) ||
                 (cur->kind == E_VART && cur->sval && strcasecmp(cur->sval, "ARB") == 0)))
                arb_nam = cur;
        }

        if (arb_nam) {
            int loc_arb_start = (*p_cap_local)++;
            int loc_arb_len   = (*p_cap_local)++;
            char lbl_arb_loop[64], lbl_arb_decr[64];
            snprintf(lbl_arb_loop, sizeof lbl_arb_loop, "Jn%d_arb_loop", uid);
            snprintf(lbl_arb_decr, sizeof lbl_arb_decr, "Jn%d_arb_decr", uid);

            /* Emit prefix: all of pat->children[0] except the trailing arb_nam node */
            {
                EXPR_t *nodes[64]; int n = 0;
                EXPR_t *cur = pat->children[0];
                while (cur && cur->kind == E_SEQ) {
                    nodes[n++] = cur->children[0];
                    if (cur->children[1] == arb_nam) break;
                    cur = cur->children[1];
                    if (!cur || cur->kind != E_SEQ) break;
                }
                if (n > 0) {
                    char chain[64][64];
                    for (int i = 0; i < n-1; i++)
                        snprintf(chain[i], 64, "Jn%d_pre%d", uid, i);
                    emit_jvm_pat_node(nodes[0],
                                      n > 1 ? chain[0] : lbl_arb_loop, ω,
                                      loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
                    for (int i = 1; i < n; i++) {
                        PNLABEL(chain[i-1]);
                        emit_jvm_pat_node(nodes[i],
                                          i+1 < n ? chain[i] : lbl_arb_loop, ω,
                                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
                    }
                } else {
                    /* no prefix — fall straight to arb_loop setup */
                }
            }

            /* arb_loop: ARB is minimum-first — start at 0 chars, grow on backtrack */
            PNLABEL(lbl_arb_loop);
            /* On first entry cursor is already at ARB position — save it */
            PN("iload %d", loc_cursor);
            PN("istore %d", loc_arb_start);
            PN("ldc 0");
            PN("istore %d", loc_arb_len);   /* arb_len = 0 (minimum first) */

            /* arb_retry: bounds check + set cursor */
            char lbl_arb_retry[64];
            snprintf(lbl_arb_retry, sizeof lbl_arb_retry, "Jn%d_arb_retry", uid);
            PNLABEL(lbl_arb_retry);
            /* fail if arb_start + arb_len > len */
            PN("iload %d", loc_arb_start);
            PN("iload %d", loc_arb_len);
            PN("iadd");
            PN("iload %d", loc_len);
            PN("if_icmpgt %s", ω);
            PN("iload %d", loc_arb_start);
            PN("iload %d", loc_arb_len);
            PN("iadd");
            PN("istore %d", loc_cursor);

            /* Deferred capture: store ARB span in a temp local; only commit
             * (call sno_var_put) after right child SUCCEEDS.  This prevents
             * spurious output (e.g. OUTPUT =) on each backtrack attempt.    */
            if (arb_nam->kind == E_NAM && arb_nam->children[1] && arb_nam->children[1]->sval) {
                const char *capvar = arb_nam->children[1]->sval;
                int loc_tmp_cap = (*p_cap_local)++;

                char nameesc[256];
                { int o=0; nameesc[o++]='"';
                  for (const char *p=capvar; *p && o<(int)sizeof nameesc-4; p++) {
                      unsigned char c=(unsigned char)*p;
                      if (c=='"') { nameesc[o++]='\\'; nameesc[o++]='"'; }
                      else nameesc[o++]=(char)c;
                  }
                  nameesc[o++]='"'; nameesc[o]='\0'; }

                /* Store substring into tmp local — no side-effect yet */
                PN("aload %d", loc_subj);
                PN("iload %d", loc_arb_start);
                PN("iload %d", loc_cursor);
                PN("invokevirtual java/lang/String/substring(II)Ljava/lang/String;");
                PN("astore %d", loc_tmp_cap);

                /* Emit right child; on success → commit label; on fail → arb_decr */
                char lbl_arb_commit[64];
                snprintf(lbl_arb_commit, sizeof lbl_arb_commit, "Jn%d_arb_commit", uid);

                emit_jvm_pat_node(pat->children[1], lbl_arb_commit, lbl_arb_decr,
                                  loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

                /* Commit: right child succeeded — now store capture and goto γ */
                PNLABEL(lbl_arb_commit);
                PN("ldc %s", nameesc);
                PN("aload %d", loc_tmp_cap);
                char vpdesc[512];
                snprintf(vpdesc, sizeof vpdesc,
                         "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", classname);
                PN("invokestatic %s", vpdesc);
                PN("goto %s", γ);
            } else {
                /* No capture — emit right child directly */
                emit_jvm_pat_node(pat->children[1], γ, lbl_arb_decr,
                                  loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
            }

            PNLABEL(lbl_arb_decr);
            PN("iinc %d 1", loc_arb_len);   /* grow ARB by 1 and retry */
            PN("goto %s", lbl_arb_retry);
            break;
        }

        /* n-ary: right-fold via shared helper */
        if (pat->nchildren > 2) {
            int _nc = pat->nchildren;
            EXPR_t **_fn, **_fk;
            EXPR_t *_r = ir_nary_right_fold(pat, E_SEQ, &_fn, &_fk);
            emit_jvm_pat_node(_r, γ, ω, loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
            ir_nary_right_fold_free(_fn, _fk, _nc - 1);
            break;
        }
                /* Normal SEQ */
        char lmid[64];
        snprintf(lmid, sizeof lmid, "Jn%d_seq_mid", uid);
        emit_jvm_pat_node(pat->children[0],  lmid,  ω,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
        PNLABEL(lmid);
        emit_jvm_pat_node(pat->children[1], γ, ω,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
        break;
    }

    case E_OR: {
        /* ALT node: try left; on failure restore cursor and try right
         * Wiring:
         *   save cursor_save
         *   left: γ → γ, ω → restore+right_alpha
         *   right: γ → γ, ω → ω                          */
        /* n-ary: right-fold via shared helper */
        if (pat->nchildren > 2) {
            int _nc = pat->nchildren;
            EXPR_t **_fn, **_fk;
            EXPR_t *_r = ir_nary_right_fold(pat, E_OR, &_fn, &_fk);
            emit_jvm_pat_node(_r, γ, ω, loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
            ir_nary_right_fold_free(_fn, _fk, _nc - 1);
            break;
        }
                int loc_save = (*p_cap_local)++;   /* allocate a local int for saved cursor */

        char lbl_try_right[64], lbl_restore[64];
        snprintf(lbl_try_right, sizeof lbl_try_right, "Jn%d_alt_right", uid);
        snprintf(lbl_restore,   sizeof lbl_restore,   "Jn%d_alt_rst",   uid);

        /* save cursor */
        PN("iload %d", loc_cursor);
        PN("istore %d", loc_save);

        emit_jvm_pat_node(pat->children[0], γ, lbl_try_right,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

        PNLABEL(lbl_try_right);
        /* restore cursor */
        PN("iload %d", loc_save);
        PN("istore %d", loc_cursor);

        emit_jvm_pat_node(pat->children[1], γ, ω,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_ATP: {
        /* @VAR — cursor-position capture.
         * Zero-width: store current cursor as decimal string into VAR, always succeed.
         * Parser: unary node, children[0] = E_VART(varname). */
        const char *varname = (pat->children[0] && pat->children[0]->sval)
                              ? pat->children[0]->sval : "";
        char nameesc[256];
        { int o = 0; nameesc[o++] = '"';
          for (const char *p = varname; *p && o < (int)sizeof nameesc - 4; p++) {
              if (*p == '"') { nameesc[o++] = '\\'; nameesc[o++] = '"'; }
              else nameesc[o++] = *p;
          }
          nameesc[o++] = '"'; nameesc[o] = '\0'; }
        /* push varname, cursor-as-string, call sno_var_put */
        PN("ldc %s", nameesc);
        PN("iload %d", loc_cursor);
        PN("invokestatic java/lang/Integer/toString(I)Ljava/lang/String;");
        char vpdesc[512];
        snprintf(vpdesc, sizeof vpdesc,
                 "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", classname);
        PN("invokestatic %s", vpdesc);
        PN("goto %s", γ);  /* zero-width: cursor unchanged, always succeed */
        break;
    }

    /* ------------------------------------------------------------------ */
    case E_NAM: {
        /* Conditional assign:  pat . var
         * Match pat; on success capture matched substring into var.
         * cursor_before saved, on γ: var = subject.substring(cursor_before, cursor) */
        int loc_before = (*p_cap_local)++;

        char lbl_inner_ok[64];
        snprintf(lbl_inner_ok, sizeof lbl_inner_ok, "Jn%d_nam_ok", uid);

        /* save cursor before child */
        PN("iload %d", loc_cursor);
        PN("istore %d", loc_before);

        emit_jvm_pat_node(pat->children[0], lbl_inner_ok, ω,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

        PNLABEL(lbl_inner_ok);
        /* var = subject.substring(cursor_before, cursor) */
        const char *varname = (pat->children[1] && pat->children[1]->sval) ? pat->children[1]->sval : "";
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
        PN("goto %s", γ);
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

        emit_jvm_pat_node(pat->children[0], lbl_inner_ok, ω,
                          loc_subj, loc_cursor, loc_len, p_cap_local, out, classname);

        PNLABEL(lbl_inner_ok);
        const char *varname = (pat->children[1] && pat->children[1]->sval) ? pat->children[1]->sval : "";
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
        PN("goto %s", γ);
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
             * On loop exit: proceed to γ with cursor at end of matched span */
            int loc_save = (*p_cap_local)++;   /* cursor before each attempt */

            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Jn%d_arb_loop", uid);
            snprintf(lbl_done, sizeof lbl_done, "Jn%d_arb_done", uid);

            /* inner child success → back to loop top; failure → done */
            char lbl_child_ok[64], lbl_child_fail[64];
            snprintf(lbl_child_ok,   sizeof lbl_child_ok,   "Jn%d_arb_cok",  uid);
            snprintf(lbl_child_fail, sizeof lbl_child_fail, "Jn%d_arb_cfail",uid);

            EXPR_t *child = (pat->children && pat->children[0]) ? pat->children[0] : NULL;

            /* loop: save cursor, try child */
            PNLABEL(lbl_loop);
            PN("iload %d", loc_cursor);
            PN("istore %d", loc_save);

            emit_jvm_pat_node(child, lbl_child_ok, lbl_child_fail,
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
            PN("goto %s", γ);
            break;
        }

        /* ---- ANY(charset) ---- */
        if (strcasecmp(fname, "ANY") == 0) {
            /* cursor < len AND subject.charAt(cursor) in charset */
            EXPR_t *cs_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            char lbl_ok[64];
            snprintf(lbl_ok, sizeof lbl_ok, "Jn%d_any_ok", uid);

            /* bounds check: cursor < len */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", ω);

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
                /* We have access to jvm_out globally; emit via emit_jvm_expr */
                /* But we need to use the module-level emit_jvm_expr.
                 * jvm_out is the module-level FILE* — save and restore. */
                FILE *saved_out = jvm_out;
                jvm_out = out;
                emit_jvm_expr(cs_arg);
                jvm_out = saved_out;
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifeq %s", ω);
            } else {
                /* no charset — always fail */
                PN("pop");
                PN("goto %s", ω);
                break;
            }
            /* success: advance cursor */
            PN("iinc %d 1", loc_cursor);
            PN("goto %s", γ);
            break;
        }

        /* ---- NOTANY(charset) ---- */
        if (strcasecmp(fname, "NOTANY") == 0) {
            EXPR_t *cs_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            char lbl_ok[64];
            snprintf(lbl_ok, sizeof lbl_ok, "Jn%d_notany_ok", uid);

            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", ω);

            PN("aload %d", loc_subj);
            PN("iload %d", loc_cursor);
            PN("invokevirtual java/lang/String/charAt(I)C");
            PN("invokestatic java/lang/String/valueOf(C)Ljava/lang/String;");
            if (cs_arg) {
                int loc_ch = (*p_cap_local)++;
                PN("astore %d", loc_ch);
                FILE *saved_out = jvm_out; jvm_out = out;
                emit_jvm_expr(cs_arg);
                jvm_out = saved_out;
                PN("aload %d", loc_ch);
                PN("invokevirtual java/lang/String/contains(Ljava/lang/CharSequence;)Z");
                PN("ifne %s", ω);   /* in charset → fail */
            } else {
                PN("pop");
            }
            PN("iinc %d 1", loc_cursor);
            PN("goto %s", γ);
            break;
        }

        /* ---- SPAN(charset) ---- */
        if (strcasecmp(fname, "SPAN") == 0) {
            /* consume longest run of chars in charset (at least 1) */
            EXPR_t *cs_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_cs = (*p_cap_local)++;
            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Jn%d_span_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Jn%d_span_dn", uid);

            /* evaluate charset once */
            FILE *saved_out = jvm_out; jvm_out = out;
            if (cs_arg) emit_jvm_expr(cs_arg); else PN("ldc \"\"");
            jvm_out = saved_out;
            PN("astore %d", loc_cs);

            /* must match at least 1 */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", ω);
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
                PN("ifeq %s", ω);
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
            PN("goto %s", γ);
            break;
        }

        /* ---- BREAK(charset) ---- */
        if (strcasecmp(fname, "BREAK") == 0 || strcasecmp(fname, "BREAKX") == 0) {
            /* BREAK:  consume chars NOT in charset until one IS; fail if none found (0 advance)
             * BREAKX: same but succeeds even with zero chars consumed (allows cursor at end) */
            int is_breakx = (strcasecmp(fname, "BREAKX") == 0);
            EXPR_t *cs_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_cs = (*p_cap_local)++;
            char lbl_loop[64], lbl_done[64], lbl_eos[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Jn%d_brk_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Jn%d_brk_dn", uid);
            snprintf(lbl_eos,  sizeof lbl_eos,  "Jn%d_brk_eos", uid);

            FILE *saved_out = jvm_out; jvm_out = out;
            if (cs_arg) emit_jvm_expr(cs_arg); else PN("ldc \"\"");
            jvm_out = saved_out;
            PN("astore %d", loc_cs);

            /* Save cursor_start for BREAK zero-advance check */
            int loc_brk_start = (*p_cap_local)++;
            PN("iload %d", loc_cursor);
            PN("istore %d", loc_brk_start);

            PNLABEL(lbl_loop);
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_len);
            PN("if_icmpge %s", lbl_eos);   /* end-of-string: BREAK fails, BREAKX succeeds */
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
                PN("ifne %s", lbl_done);   /* found break char → stop, succeed */
                PN("iinc %d 1", loc_cursor);
                PN("goto %s", lbl_loop);
            }
            PNLABEL(lbl_eos);
            if (!is_breakx) {
                /* BREAK: end-of-string without finding break char = failure */
                PN("goto %s", ω);
            }
            PNLABEL(lbl_done);
            PN("goto %s", γ);
            break;
        }

        /* ---- LEN(n) ---- */
        if (strcasecmp(fname, "LEN") == 0) {
            EXPR_t *n_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_n = (*p_cap_local)++;
            /* evaluate n, convert to int */
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) emit_jvm_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor + n <= len? */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("iadd");
            PN("iload %d", loc_len);
            PN("if_icmpgt %s", ω);
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("iadd");
            PN("istore %d", loc_cursor);
            PN("goto %s", γ);
            break;
        }

        /* ---- POS(n) ---- */
        if (strcasecmp(fname, "POS") == 0) {
            EXPR_t *n_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) emit_jvm_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor == n? */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("if_icmpne %s", ω);
            PN("goto %s", γ);
            break;
        }

        /* ---- RPOS(n) ---- */
        if (strcasecmp(fname, "RPOS") == 0) {
            EXPR_t *n_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) emit_jvm_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor == len - n? */
            PN("iload %d", loc_len);
            PN("iload %d", loc_n);
            PN("isub");
            PN("iload %d", loc_cursor);
            PN("if_icmpne %s", ω);
            PN("goto %s", γ);
            break;
        }

        /* ---- TAB(n) ---- */
        if (strcasecmp(fname, "TAB") == 0) {
            EXPR_t *n_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) emit_jvm_expr(n_arg); else PN("ldc \"0\"");
            jvm_out = saved_out;
            PN("invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I");
            PN("istore %d", loc_n);
            /* cursor <= n <= len? */
            PN("iload %d", loc_cursor);
            PN("iload %d", loc_n);
            PN("if_icmpgt %s", ω);
            PN("iload %d", loc_n);
            PN("iload %d", loc_len);
            PN("if_icmpgt %s", ω);
            PN("iload %d", loc_n);
            PN("istore %d", loc_cursor);
            PN("goto %s", γ);
            break;
        }

        /* ---- RTAB(n) ---- */
        if (strcasecmp(fname, "RTAB") == 0) {
            EXPR_t *n_arg = (pat->children && pat->children[0]) ? pat->children[0] : NULL;
            int loc_n = (*p_cap_local)++;
            FILE *saved_out = jvm_out; jvm_out = out;
            if (n_arg) emit_jvm_expr(n_arg); else PN("ldc \"0\"");
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
                PN("if_icmpgt %s", ω);
                PN("iload %d", loc_tgt);
                PN("istore %d", loc_cursor);
            }
            PN("goto %s", γ);
            break;
        }

        /* ---- REM ---- */
        if (strcasecmp(fname, "REM") == 0) {
            /* always succeeds, cursor → len */
            PN("iload %d", loc_len);
            PN("istore %d", loc_cursor);
            PN("goto %s", γ);
            break;
        }

        /* ---- ARB ---- */
        if (strcasecmp(fname, "ARB") == 0) {
            /* ARB: greedy match — consume as many chars as possible (len-cursor),
             * then let the rest of the surrounding pattern try.  If the rest fails,
             * the outer retry loop advances the subject cursor by 1 and we try again
             * at a shorter effective length.  This gives correct results when ARB
             * appears in a SEQ followed by a literal anchor (e.g. ARB . V " :").
             *
             * For the non-SEQ (standalone ARB) case: match 0 chars (same as before). */
            PN("iload %d", loc_len);
            PN("istore %d", loc_cursor);   /* cursor = len (consume all remaining) */
            PN("goto %s", γ);
            break;
        }

        /* ---- FAIL ---- */
        if (strcasecmp(fname, "FAIL") == 0) {
            /* FAIL forces the entire match to fail — jump past retry loop */
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : ω);
            break;
        }

        /* ---- SUCCEED ---- */
        if (strcasecmp(fname, "SUCCEED") == 0) {
            PN("goto %s", γ);
            break;
        }

        /* ---- FENCE ---- */
        if (strcasecmp(fname, "FENCE") == 0) {
            PN("goto %s", γ);
            break;
        }

        /* ---- ABORT ---- */
        if (strcasecmp(fname, "ABORT") == 0) {
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : ω);
            break;
        }

        /* ---- Unknown function — stub as success ---- */
        fprintf(out, "    ; STUB: unknown pattern function %s\n", fname);
        PN("goto %s", γ);
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
            PN("goto %s", γ);
            break;
        }
        if (strcasecmp(vname, "FAIL") == 0) {
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : ω);
            break;
        }
        if (strcasecmp(vname, "SUCCEED") == 0) { PN("goto %s", γ); break; }
        if (strcasecmp(vname, "FENCE") == 0)   { PN("goto %s", γ); break; }
        if (strcasecmp(vname, "ABORT") == 0)   {
            PN("goto %s", jvm_cur_pat_abort_label[0] ? jvm_cur_pat_abort_label : ω);
            break;
        }
        if (strcasecmp(vname, "ARB") == 0) {
            /* Greedy: consume all remaining chars; outer scan retry handles backtrack */
            PN("iload %d", loc_len);
            PN("istore %d", loc_cursor);
            PN("goto %s", γ);
            break;
        }

        /* Check named-pattern registry (compile-time pattern variable assignments).
         * E.g.  P = ('a' | 'b' | 'c')  registers P → E_OR tree.
         * When we see P in pattern context, inline-expand its stored tree. */
        {
            const JvmNamedPat *np = jvm_named_pat_lookup(vname);
            if (np && np->pat) {
                fprintf(out, "    ; E_VART %s → named pattern inline expansion\n", vname);
                emit_jvm_pat_node(np->pat, γ, ω,
                                  loc_subj, loc_cursor, loc_len,
                                  p_cap_local, out, classname);
                break;
            }
        }

        /* Otherwise: variable holds a plain string at runtime — match as literal */
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
        PN("ifeq %s", ω);
        PN("iload %d", loc_cursor);
        PN("iload %d", loc_llen);
        PN("iadd");
        PN("istore %d", loc_cursor);
        PN("goto %s", γ);
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
        EXPR_t *inner = pat->children[1] ? pat->children[1] : pat->children[0];

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
        PN("ifeq %s", ω);
        PN("iload %d", loc_cursor);
        PN("iload %d", loc_llen);
        PN("iadd");
        PN("istore %d", loc_cursor);
        PN("goto %s", γ);
        break;
    }

    /* ------------------------------------------------------------------ */
    default:
        fprintf(out, "    ; STUB: unhandled pattern node kind %d\n", (int)pat->kind);
        PN("goto %s", γ);
        break;
    }

#undef PN
#undef PNL
#undef PNLABEL
}

/* -----------------------------------------------------------------------
 * Statement emitter
 * ----------------------------------------------------------------------- */

/* Returns 1 if expr tree contains INPUT reference */
static int expr_contains_input(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_VART && e->sval && strcasecmp(e->sval, "INPUT") == 0) return 1;
    for (int _i = 0; _i < e->nchildren; _i++)
        if (expr_contains_input(e->children[_i])) return 1;
    return 0;
}

/* Emit a goto to a SNOBOL4 label, intercepting RETURN/FRETURN when in a fn body */
static void emit_jvm_goto(const char *label) {
    if (!label || !label[0]) return;
    /* RETURN/FRETURN/NRETURN: route to function exit labels, or L_END if in main */
    if (strcasecmp(label,"RETURN")==0 || strcasecmp(label,"NRETURN")==0 ||
        strcasecmp(label,"FRETURN")==0) {
        if (jvm_cur_fn) {
            int fn_idx = (int)(jvm_cur_fn - jvm_fn_table_fwd);
            if (strcasecmp(label,"FRETURN")==0) {
                char lbl[64]; snprintf(lbl, sizeof lbl, "Jfn%d_freturn", fn_idx);
                J("    goto %s\n", lbl);
            } else {
                char lbl[64]; snprintf(lbl, sizeof lbl, "Jfn%d_return", fn_idx);
                J("    goto %s\n", lbl);
            }
        } else {
            /* Outside any function: RETURN/FRETURN in main body -> program end */
            J("    goto L_END\n");
        }
        return;
    }
    /* END -> program termination */
    if (strcasecmp(label,"END")==0) {
        J("    goto L_END\n");
        return;
    }
    /* $COMPUTED:expr — dynamic/computed goto: evaluate expr, dispatch via if-chain */
    if (strncasecmp(label,"$COMPUTED:",10)==0) {
        const char *expr_src = label + 10;
        /* Strip trailing ) left by parser capture */
        char expr_buf[4096];
        strncpy(expr_buf, expr_src, sizeof(expr_buf)-1);
        expr_buf[sizeof(expr_buf)-1] = '\0';
        int elen = (int)strlen(expr_buf);
        while (elen > 0 && (expr_buf[elen-1]==')' || expr_buf[elen-1]==' ' || expr_buf[elen-1]=='\t'))
            expr_buf[--elen] = '\0';
        EXPR_t *ce = parse_expr_from_str(expr_buf);
        if (!ce) { J("    goto L_END\n"); return; }
        /* Evaluate expression → string on stack */
        emit_jvm_expr(ce);
        /* Store in a local string variable via sno_indr_set/get — use temp var */
        static int _cgoto_uid = 0;
        char tmpvar[64]; snprintf(tmpvar, sizeof tmpvar, "_cgoto_%d", _cgoto_uid++);
        char tmpesc[128]; jvm_escape_string(tmpvar, tmpesc, sizeof tmpesc);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        char vgdesc[512]; snprintf(vgdesc, sizeof vgdesc,
            "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        /* stack: String (computed label value) */
        J("    ldc %s\n", tmpesc);
        J("    swap\n");
        J("    invokestatic %s\n", vpdesc);
        /* Now emit if-chain over reachable labels */
        /* Collect labels: if in function, use function body labels; else main labels */
        static int _cgoto_lbl = 0;
        char lbl_done[64]; snprintf(lbl_done, sizeof lbl_done, "Lcg_done_%d", _cgoto_lbl++);
        /* Walk program statements to find labels in scope */
        if (jvm_cur_prog) {
            int in_scope = 0;
            const char *scope_entry = jvm_cur_fn ?
                (jvm_cur_fn->entry_label ? jvm_cur_fn->entry_label : jvm_cur_fn->name) : NULL;
            const char *scope_end = jvm_cur_fn ? jvm_cur_fn->end_label : NULL;
            for (STMT_t *t = jvm_cur_prog->head; t; t = t->next) {
                if (t->is_end) break;
                if (jvm_cur_fn) {
                    /* function scope: include labels between entry and end */
                    if (scope_entry && t->label && strcasecmp(t->label, scope_entry)==0) in_scope=1;
                    if (scope_end && t->label && strcasecmp(t->label, scope_end)==0) break;
                } else {
                    /* main scope: skip function bodies */
                    if (t->label) {
                        int is_fn_entry = 0;
                        for (int fi=0; fi<jvm_fn_count_fwd; fi++) {
                            const char *e = jvm_fn_table_fwd[fi].entry_label ?
                                jvm_fn_table_fwd[fi].entry_label : jvm_fn_table_fwd[fi].name;
                            if (e && strcasecmp(t->label,e)==0) { is_fn_entry=1; break; }
                        }
                        if (is_fn_entry) { in_scope=0; continue; }
                        /* check if end of a fn body */
                        for (int fi=0; fi<jvm_fn_count_fwd; fi++) {
                            if (jvm_fn_table_fwd[fi].end_label &&
                                strcasecmp(t->label, jvm_fn_table_fwd[fi].end_label)==0) {
                                in_scope=1; break;
                            }
                        }
                    }
                    if (!jvm_cur_fn) in_scope=1; /* main: always in scope when not in fn */
                }
                if (!t->label) continue;
                if (jvm_cur_fn && !in_scope) continue;
                /* Emit: load computed value, compare, jump */
                char lsafe[128]; jvm_expand_label(t->label, lsafe, sizeof lsafe);
                char jlbl[256];
                if (jvm_cur_fn) {
                    int _fi = (int)(jvm_cur_fn - jvm_fn_table_fwd);
                    snprintf(jlbl, sizeof jlbl, "Lf%d_%s", _fi, lsafe);
                } else {
                    snprintf(jlbl, sizeof jlbl, "L_%s", lsafe);
                }
                J("    ldc %s\n", tmpesc);
                J("    invokestatic %s\n", vgdesc);
                char ciesc[256]; jvm_escape_string(t->label, ciesc, sizeof ciesc);
                J("    ldc %s\n", ciesc);
                char eqdesc[512]; snprintf(eqdesc, sizeof eqdesc,
                    "%s/sno_str_eq(Ljava/lang/String;Ljava/lang/String;)Z", jvm_classname);
                J("    invokestatic %s\n", eqdesc);
                char lbl_next[256]; snprintf(lbl_next, sizeof lbl_next, "Lcg_n_%d_%s", _cgoto_lbl, lsafe);
                J("    ifeq %s\n", lbl_next);
                J("    goto %s\n", jlbl);
                J("%s:\n", lbl_next);
            }
        }
        /* no-match fallback: freturn if in function, L_END if in main */
        if (jvm_cur_fn) {
            int fn_idx2 = (int)(jvm_cur_fn - jvm_fn_table_fwd);
            char lbl_fr[64]; snprintf(lbl_fr, sizeof lbl_fr, "Jfn%d_freturn", fn_idx2);
            J("    goto %s\n", lbl_fr);
        } else {
            J("    goto L_END\n");   /* no match in main → end */
        }
        J("%s:\n", lbl_done);
        return;
    }
    char safe[128]; jvm_expand_label(label, safe, sizeof safe);
    char glbl[256];
    if (jvm_cur_fn) {
        int _fi = (int)(jvm_cur_fn - jvm_fn_table_fwd);
        snprintf(glbl, sizeof glbl, "Lf%d_%s", _fi, safe);
    } else {
        snprintf(glbl, sizeof glbl, "L_%s", safe);
    }

    /* Cross-scope check: if we are inside a function method, verify that
     * the target label actually exists within this function's body range.
     * If not (e.g. ":F(error)" jumping to a main-level label), SNOBOL4
     * semantics treat it as FRETURN — the function fails.
     * See JVM.md J-212 sprint notes. */
    if (jvm_cur_fn && jvm_cur_prog) {
        const char *entry = jvm_cur_fn->entry_label
                            ? jvm_cur_fn->entry_label
                            : jvm_cur_fn->name;
        int in_body = 0;
        int found_in_fn = 0;
        for (STMT_t *t = jvm_cur_prog->head; t; t = t->next) {
            /* Start of this function's body */
            if (!in_body && t->label && entry &&
                strcasecmp(t->label, entry) == 0)
                in_body = 1;
            /* End of this function's body */
            if (in_body && jvm_cur_fn->end_label && t->label &&
                strcasecmp(t->label, jvm_cur_fn->end_label) == 0)
                break;
            if (in_body && t->label) {
                char ts[128];
                jvm_expand_label(t->label, ts, sizeof ts);
                if (strcmp(ts, safe) == 0) { found_in_fn = 1; break; }
            }
        }
        if (!found_in_fn) {
            /* Out-of-scope: SNOBOL4 semantics = FRETURN */
            int fn_idx = (int)(jvm_cur_fn - jvm_fn_table_fwd);
            char lbl_fr[64];
            snprintf(lbl_fr, sizeof lbl_fr, "Jfn%d_freturn", fn_idx);
            J("    goto %s\n", lbl_fr);
            return;
        }
    }

    JI("goto", glbl);
}

static void emit_jvm_stmt(STMT_t *s, int stmt_idx) {
    char next_lbl[64];
    snprintf(next_lbl, sizeof next_lbl, "Ln_%d", stmt_idx);

    /* Set module-level stmt fail label for INPUT EOF propagation in expressions */
    jvm_cur_stmt_fail_label[0] = '\0';
    if (s->go && s->go->onfailure && s->go->onfailure[0]) {
        snprintf(jvm_cur_stmt_fail_label, sizeof jvm_cur_stmt_fail_label,
                 "%s", s->go->onfailure);
    }

    /* Label */
    if (s->label) {
        JSep(s->label);
        char lsafe[128]; jvm_expand_label(s->label, lsafe, sizeof lsafe);
        char lbuf[256];
        if (jvm_cur_fn) {
            int _fi = (int)(jvm_cur_fn - jvm_fn_table_fwd);
            snprintf(lbuf, sizeof lbuf, "Lf%d_%s", _fi, lsafe);
        } else {
            snprintf(lbuf, sizeof lbuf, "L_%s", lsafe);
        }
        J("%s:\n", lbuf);
    }

    /* Label-only statement (no subject, no body) — just fall through */
    if (!s->subject && !s->has_eq && !s->pattern) {
        if (s->go && s->go->uncond && s->go->uncond[0]) {
            emit_jvm_goto(s->go->uncond);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            emit_jvm_goto(s->go->onsuccess);
        }
        return;
    }

    /* Increment &STNO before every real statement */
    {
        char stnodesc[512];
        snprintf(stnodesc, sizeof stnodesc, "%s/sno_kw_STNO I", jvm_classname);
        J("    getstatic %s\n", stnodesc);
        J("    iconst_1\n");
        J("    iadd\n");
        J("    putstatic %s\n", stnodesc);
    }
    /* Enforce &STLIMIT / increment &STCOUNT */
    {
        char tickdesc[512];
        snprintf(tickdesc, sizeof tickdesc, "%s/sno_stcount_tick()V", jvm_classname);
        J("    invokestatic %s\n", tickdesc);
    }

    /* If inside a user function, intercept RETURN/FRETURN/NRETURN gotos */
    /* (handled per-goto in emit_jvm_goto_target below) */

    /* Case 1c: DATA field setter — fieldname(instance) = value
     * subject is E_FNC where fname matches a known DATA field */
    if (s->has_eq && s->subject && s->subject->kind == E_FNC && !s->pattern) {
        const char *sfname = s->subject->sval ? s->subject->sval : "";
        const JvmDataType *ft = jvm_find_data_field(sfname);
        if (ft) {
            jvm_need_data_helpers  = 1;
            jvm_need_array_helpers = 1;
            /* push instance_id */
            if (s->subject->children && s->subject->children[0]) emit_jvm_expr(s->subject->children[0]);
            else JI("ldc", "\"\"");
            /* push field name */
            char fnesc[256]; jvm_escape_string(sfname, fnesc, sizeof fnesc);
            JI("ldc", fnesc);
            /* push value */
            if (!s->replacement || s->replacement->kind == E_NULV) JI("ldc", "\"\"");
            else emit_jvm_expr(s->replacement);
            char apdesc[512]; snprintf(apdesc, sizeof apdesc,
                "%s/sno_array_put(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                jvm_classname);
            JI("invokestatic", apdesc);
            if (s->go && s->go->uncond && s->go->uncond[0]) emit_jvm_goto(s->go->uncond);
            else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) emit_jvm_goto(s->go->onsuccess);
            return;
        }
    }

    /* Case 1b: array/table subscript assignment — A<I> = expr or T['key'] = expr */
    if (s->has_eq && s->subject &&
        s->subject->kind == E_IDX &&
        !s->pattern) {
        jvm_need_array_helpers = 1;
        /* Push array-id */
        if (s->subject->sval) {
            /* Named array: sval holds name */
            char nameesc[256]; jvm_escape_string(s->subject->sval, nameesc, sizeof nameesc);
            JI("ldc", nameesc);
            char igdesc[512]; snprintf(igdesc, sizeof igdesc,
                "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
            JI("invokestatic", igdesc);
            /* subscript */
            if (s->subject->children && s->subject->children[0]) emit_jvm_expr(s->subject->children[0]);
            else JI("ldc", "\"1\"");
        } else { /* postfix subscript: children[0]=array expr, [1..]=keys */
            emit_jvm_expr(s->subject->children[0]);
            if (s->subject->nchildren >= 3 && s->subject->children[1] && s->subject->children[2]) {
                /* 2D: build "row,col" key */
                JI("new", "java/lang/StringBuilder");
                JI("dup", "");
                JI("invokespecial", "java/lang/StringBuilder/<init>()V");
                emit_jvm_expr(s->subject->children[1]);
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                JI("ldc", "\",\"");
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                emit_jvm_expr(s->subject->children[2]);
                JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
            } else if (s->subject->nchildren >= 2 && s->subject->children[1]) {
                emit_jvm_expr(s->subject->children[1]);
            } else if (s->subject->children && s->subject->children[0]) {
                emit_jvm_expr(s->subject->children[0]);
            } else {
                JI("ldc", "\"0\"");
            }
        }
        /* value — evaluate first so we can null-check for :S/:F goto semantics */
        if (!s->replacement || s->replacement->kind == E_NULV) JI("ldc", "\"\"");
        else emit_jvm_expr(s->replacement);
        char apdesc[512]; snprintf(apdesc, sizeof apdesc,
            "%s/sno_array_put(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        int has_cond_s = (s->go && !s->go->uncond && s->go->onsuccess && s->go->onsuccess[0]);
        int has_cond_f = (s->go && s->go->onfailure && s->go->onfailure[0]);
        if (has_cond_s || has_cond_f) {
            static int _aaryasgn = 0;
            char lok[64], lfail[64], ldn[64];
            snprintf(lok,  sizeof lok,  "Laryok_%d",  _aaryasgn);
            snprintf(lfail,sizeof lfail,"Laryfail_%d",_aaryasgn);
            snprintf(ldn,  sizeof ldn,  "Larydn_%d",  _aaryasgn++);
            JI("dup", "");
            JI("ifnonnull", lok);
            /* null value → assignment failed: discard value+key+array_id */
            JI("pop", ""); JI("pop", ""); JI("pop", "");
            J("%s:\n", lfail);
            if (has_cond_f) emit_jvm_goto(s->go->onfailure);
            JI("goto", ldn);
            J("%s:\n", lok);
            JI("invokestatic", apdesc);
            if (s->go->uncond && s->go->uncond[0]) emit_jvm_goto(s->go->uncond);
            else if (has_cond_s) emit_jvm_goto(s->go->onsuccess);
            J("%s:\n", ldn);
        } else {
            JI("invokestatic", apdesc);
            if (s->go && s->go->uncond && s->go->uncond[0]) emit_jvm_goto(s->go->uncond);
        }
    }

    /* Case 1: pure assignment — no pattern */
    if (s->has_eq && s->subject &&
        (s->subject->kind == E_VART || s->subject->kind == E_KW) &&
        !s->pattern) {

        const char *subj = s->subject->sval ? s->subject->sval : "";
        int is_output = strcasecmp(subj, "OUTPUT") == 0;
        int is_kw     = (s->subject->kind == E_KW);

        /* Pre-hoist: if INPUT is nested inside the RHS expression (not direct),
         * read it into local slot 5 now with a null check, before any stack buildup. */
        int input_nested = (s->replacement &&
                            s->replacement->kind != E_VART &&  /* not direct INPUT */
                            expr_contains_input(s->replacement) &&
                            jvm_cur_stmt_fail_label[0]);
        if (input_nested) {
            static int _hoist_uid = 0;
            char hoist_ok[64];
            snprintf(hoist_ok, sizeof hoist_ok, "Jhoist%d_ok", _hoist_uid++);
            char irdesc[512];
            snprintf(irdesc, sizeof irdesc,
                "%s/sno_input_read()Ljava/lang/String;", jvm_classname);
            jvm_need_input_helper = 1;
            JI("invokestatic", irdesc);
            JI("dup", "");
            JI("ifnonnull", hoist_ok);
            JI("pop", "");
            emit_jvm_goto(jvm_cur_stmt_fail_label);
            J("%s:\n", hoist_ok);
            J("    astore 5\n");   /* local 5 = hoisted INPUT value */
        }

        if (is_output) {
            /* OUTPUT = expr → System.out.println(expr)
             * If expr returns null (failure), skip output and go to :F */
            char desc[512];
            snprintf(desc, sizeof desc, "%s/sno_stdout Ljava/io/PrintStream;", jvm_classname);
            if (!s->replacement || s->replacement->kind == E_NULV) {
                JI("getstatic", desc);
                JI("ldc", "\"\"");
                JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
            } else {
                /* Evaluate RHS first; check for null (statement failure) */
                emit_jvm_expr(s->replacement);
                /* null check: if null → statement fails (skip output) */
                {
                    static int _outnull = 0;
                    char onok[48], onfail[64];
                    snprintf(onok,   sizeof onok,   "Lout_ok_%d",   _outnull);
                    snprintf(onfail, sizeof onfail,  "Lout_fail_%d", _outnull++);
                    JI("dup", "");
                    JI("ifnonnull", onok);
                    JI("pop", "");
                    /* jump to explicit :F or skip to after */
                    if (s->go && s->go->onfailure && s->go->onfailure[0]) {
                        emit_jvm_goto(s->go->onfailure);
                    } else {
                        JI("goto", onfail);
                    }
                    J("%s:\n", onok);
                    /* stack: non-null String — println it */
                    JI("getstatic", desc);
                    JI("swap", "");
                    JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
                    /* :S/:uncond goto fires on success (after println) */
                    if (s->go && s->go->uncond && s->go->uncond[0]) {
                        emit_jvm_goto(s->go->uncond);
                    } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
                        emit_jvm_goto(s->go->onsuccess);
                    }
                    J("%s:\n", onfail);
                    /* unconditional goto also fires on failure path */
                    if (s->go && s->go->uncond && s->go->uncond[0]) {
                        emit_jvm_goto(s->go->uncond);
                    }
                }
            }
        } else if (is_kw) {
            /* &KEYWORD = expr → sno_kw_set(name, val) */
            char kwesc[128];
            jvm_escape_string(subj, kwesc, sizeof kwesc);
            JI("ldc", kwesc);
            if (!s->replacement || s->replacement->kind == E_NULV) {
                JI("ldc", "\"\"");
            } else {
                emit_jvm_expr(s->replacement);
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
                /* Emit INPUT call first, dup, ifnull → :F, then store.
                 * Clear stmt_fail_label so emit_jvm_expr doesn't double-emit. */
                char saved_fail[128];
                snprintf(saved_fail, sizeof saved_fail, "%s", jvm_cur_stmt_fail_label);
                jvm_cur_stmt_fail_label[0] = '\0';
                emit_jvm_expr(s->replacement);   /* → String | null */
                snprintf(jvm_cur_stmt_fail_label, sizeof jvm_cur_stmt_fail_label, "%s", saved_fail);
                JI("dup", "");
                /* ifnull would leave 1 item on stack at :F target; use ifnonnull+pop+goto
                 * so :F target always receives empty stack (consistent with other :F paths) */
                {
                    static int _inp_ok_uid = 0;
                    char inp_ok_lbl[64];
                    snprintf(inp_ok_lbl, sizeof inp_ok_lbl, "Linp_ok_%d", _inp_ok_uid++);
                    JI("ifnonnull", inp_ok_lbl);
                    JI("pop", "");    /* discard null — stack now empty */
                    emit_jvm_goto(s->go->onfailure);
                    J("%s:\n", inp_ok_lbl);
                }
                /* non-null: store via sno_var_put(name, val) */
                /* Stack: val — need to push name first: swap trick */
                JI("ldc", nameesc);
                JI("swap", "");
                char vpdesc2[512];
                snprintf(vpdesc2, sizeof vpdesc2, "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
                JI("invokestatic", vpdesc2);
            } else {
                /* General VAR = expr: evaluate RHS, check for null (= failure) */
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    /* null/empty assignment — always succeeds, store "" */
                    JI("ldc", nameesc);
                    JI("ldc", "\"\"");
                    char vpdesc0[512];
                    snprintf(vpdesc0, sizeof vpdesc0, "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
                    JI("invokestatic", vpdesc0);
                } else {
                    emit_jvm_expr(s->replacement);   /* → String | null */
                    static int _varnull = 0;
                    char vnok[48], vnfail[64];
                    snprintf(vnok,   sizeof vnok,   "Lvar_ok_%d",   _varnull);
                    snprintf(vnfail, sizeof vnfail,  "Lvar_fail_%d", _varnull++);
                    JI("dup", "");
                    JI("ifnonnull", vnok);
                    /* null → failure: pop and branch */
                    JI("pop", "");
                    if (s->go && s->go->onfailure && s->go->onfailure[0]) {
                        emit_jvm_goto(s->go->onfailure);
                    } else {
                        JI("goto", vnfail);
                    }
                    J("%s:\n", vnok);
                    /* non-null: store — stack has val, need (name, val) */
                    JI("ldc", nameesc);
                    JI("swap", "");
                    char vpdesc[512];
                    snprintf(vpdesc, sizeof vpdesc, "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
                    JI("invokestatic", vpdesc);
                    /* :S goto — emitted here, BEFORE vnfail label, so only success path takes it */
                    if (!is_output) {
                        if (s->go && s->go->uncond && s->go->uncond[0]) {
                            emit_jvm_goto(s->go->uncond);
                        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
                            emit_jvm_goto(s->go->onsuccess);
                        }
                    }
                    /* vnfail: — failure fall-through target (only reached when no explicit :F jump above) */
                    J("%s:\n", vnfail);
                    /* :F goto after vnfail (explicit failure branch, if any — uncond already handled above) */
                    /* Note: onfailure was already emitted inline in the null path above; no re-emit needed */
                }
            }
        }

        /* :S/:uncond already emitted inside the RHS-null block above for non-output assigns.
         * For output assigns or null/empty RHS (no vnfail block), emit :S/:uncond here. */
        if (!is_output) {
            /* Only emit if we did NOT enter the vnfail block (i.e. replacement was null/empty) */
            int has_rhs = (s->replacement && s->replacement->kind != E_NULV);
            if (!has_rhs) {
                if (s->go && s->go->uncond && s->go->uncond[0]) {
                    emit_jvm_goto(s->go->uncond);
                } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
                    emit_jvm_goto(s->go->onsuccess);
                }
            }
        }
        /* :F fallthrough (no-op — already jumped or falling through) */
        return;
    }

    /* Case 3: indirect assignment — $expr = val */
    if (s->has_eq && s->subject && s->subject->kind == E_INDR && !s->pattern) {
        /* Evaluate the indirect target name — parser uses unop→children[0];
         * guard nchildren before accessing to avoid OOB read. */
        EXPR_t *indr_operand = (s->subject->nchildren > 1 && s->subject->children[1])
                               ? s->subject->children[1] : s->subject->children[0];
        emit_jvm_expr(indr_operand);   /* → String: the variable name */
        /* Evaluate RHS value */
        if (!s->replacement || s->replacement->kind == E_NULV) {
            JI("ldc", "\"\"");
        } else {
            emit_jvm_expr(s->replacement);
        }
        /* sno_indr_set(name_str, value_str) */
        char isdesc[512];
        snprintf(isdesc, sizeof isdesc, "%s/sno_indr_set(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        JI("invokestatic", isdesc);

        if (s->go && s->go->uncond && s->go->uncond[0]) {
            emit_jvm_goto(s->go->uncond);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            emit_jvm_goto(s->go->onsuccess);
        }
        return;
    }

    /* Case 2: expression-only statement (no =, no pattern) — just evaluate */
    if (!s->has_eq && !s->pattern && s->subject) {
        emit_jvm_expr(s->subject);
        /* Result may be null (predicate failure) or non-null (success).
         * Must route: non-null → :S (or fall through), null → :F (or fall through).
         * Unconditional goto (:uncond) fires regardless of result. */
        int has_s  = (s->go && s->go->onsuccess  && s->go->onsuccess[0]);
        int has_f  = (s->go && s->go->onfailure  && s->go->onfailure[0]);
        int has_uc = (s->go && s->go->uncond      && s->go->uncond[0]);

        if (has_uc) {
            /* Unconditional — result irrelevant */
            JI("pop", "");
            emit_jvm_goto(s->go->uncond);
        } else if (has_s || has_f) {
            /* Need to branch on null/non-null */
            static int _ef_lbl = 0;
            char lbl_nonnull[48], lbl_done[48];
            snprintf(lbl_nonnull, sizeof lbl_nonnull, "Lef_nn_%d", _ef_lbl);
            snprintf(lbl_done,    sizeof lbl_done,    "Lef_dn_%d", _ef_lbl++);
            JI("dup", "");
            JI("ifnonnull", lbl_nonnull);
            /* null path → :F or fall through */
            JI("pop", "");
            if (has_f) {
                emit_jvm_goto(s->go->onfailure);
            } else {
                JI("goto", lbl_done);   /* :F falls through */
            }
            J("%s:\n", lbl_nonnull);
            /* non-null path → :S or fall through */
            JI("pop", "");
            if (has_s) {
                emit_jvm_goto(s->go->onsuccess);
            }
            /* else fall through on success */
            J("%s:\n", lbl_done);
        } else {
            JI("pop", "");  /* no gotos — just discard */
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
        emit_jvm_expr(s->subject);
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

        emit_jvm_pat_node(s->pattern,
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
        if (s->has_eq) {
            /* Subject replacement: rebuild subject.
             * new_subject = subject[0..cursor_start] + replacement + subject[cursor..end]
             * cursor_start lives in loc_retry_save (slot 9). */
            JC("J5: subject replacement");
            JI("new", "java/lang/StringBuilder");
            JI("dup", "");
            JI("invokespecial", "java/lang/StringBuilder/<init>()V");
            /* sb.append(subject.substring(0, cursor_start)) */
            J("    aload %d\n", loc_subj);
            JI("iconst_0", "");
            J("    iload %d\n", loc_retry_save);
            JI("invokevirtual", "java/lang/String/substring(II)Ljava/lang/String;");
            JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
            /* sb.append(replacement) */
            if (s->replacement) {
                emit_jvm_expr(s->replacement);
            } else {
                JI("ldc", "\"\"");
            }
            JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
            /* sb.append(subject.substring(cursor, subject.length())) */
            J("    aload %d\n", loc_subj);
            J("    iload %d\n", loc_cursor);
            J("    aload %d\n", loc_subj);
            JI("invokevirtual", "java/lang/String/length()I");
            JI("invokevirtual", "java/lang/String/substring(II)Ljava/lang/String;");
            JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
            JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
            /* store back into subject variable */
            if (s->subject && s->subject->sval && s->subject->sval[0] &&
                s->subject->kind == E_VART) {
                const char *vname = s->subject->sval;
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
                int loc_tmp = jvm_cap_local_counter++;
                J("    astore %d\n", loc_tmp);
                J("    ldc %s\n", nameesc);
                J("    aload %d\n", loc_tmp);
                char vpdesc[512];
                snprintf(vpdesc, sizeof vpdesc,
                         "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
                JI("invokestatic", vpdesc);
            } else {
                JI("pop", "");
            }
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
            emit_jvm_goto(s->go->uncond);
        } else if (s->go && s->go->onsuccess && s->go->onsuccess[0]) {
            emit_jvm_goto(s->go->onsuccess);
        } else if (s->go && s->go->onfailure && s->go->onfailure[0]) {
            /* No :S but there IS a :F — must jump past the fail block so
             * success falls through to the next statement, not into :F goto */
            char lbl_after[64];
            snprintf(lbl_after, sizeof lbl_after, "Jpat%d_after", uid);
            JI("goto", lbl_after);

            /* --- FAIL --- */
            J("%s:\n", lbl_fail);
            emit_jvm_goto(s->go->onfailure);

            J("%s:\n", lbl_after);
            return;
        }
        /* fall through if no :S and no :F */

        /* --- FAIL --- */
        J("%s:\n", lbl_fail);
        if (s->go && s->go->uncond && s->go->uncond[0]) {
            /* Unconditional goto fires on failure too */
            emit_jvm_goto(s->go->uncond);
        } else if (s->go && s->go->onfailure && s->go->onfailure[0]) {
            emit_jvm_goto(s->go->onfailure);
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

static void emit_jvm_runtime_helpers(void) {
    /* sno_str_eq(String a, String b) → boolean (case-insensitive, null-safe) */
    J(".method static sno_str_eq(Ljava/lang/String;Ljava/lang/String;)Z\n");
    J("    .limit stack 4\n");
    J("    .limit locals 2\n");
    J("    aload_0\n");
    J("    ifnull Lstreq_false\n");
    J("    aload_1\n");
    J("    ifnull Lstreq_false\n");
    J("    aload_0\n");
    J("    aload_1\n");
    J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
    J("    ireturn\n");
    J("Lstreq_false:\n");
    J("    iconst_0\n");
    J("    ireturn\n");
    J(".end method\n\n");

    /* sno_kw_get(String name) → String
     * Returns value of SNOBOL4 keyword &name.
     * For J2: ALPHABET=256-char string, TRIM/ANCHOR/FULLSCAN/STCOUNT/STLIMIT = integers.
     * Unrecognised keyword -> "" (unset). */
    if (jvm_need_kw_helpers) {
        J(".method static sno_kw_get(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 3\n");   /* local0=arg, local1=StringBuilder, local2=int counter */
        /* &ALPHABET */
        J("    aload_0\n");
        J("    ldc \"ALPHABET\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_alphabet\n");
        /* Build the 256-char alphabet string via StringBuilder */
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    astore_1\n");          /* store StringBuilder in local 1 */
        J("    iconst_0\n");
        J("    istore_2\n");          /* loop counter in local 2 */
        J("Lkwg_alpha_loop:\n");
        J("    iload_2\n");
        J("    sipush 256\n");
        J("    if_icmpge Lkwg_alpha_done\n");
        J("    aload_1\n");           /* push StringBuilder */
        J("    iload_2\n");
        J("    i2c\n");
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    pop\n");               /* discard returned StringBuilder ref */
        J("    iinc 2 1\n");          /* increment counter in local 2 */
        J("    goto Lkwg_alpha_loop\n");
        J("Lkwg_alpha_done:\n");
        J("    aload_1\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_alphabet:\n");
        /* &UCASE — uppercase alphabet A-Z */
        J("    aload_0\n");
        J("    ldc \"UCASE\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_ucase\n");
        J("    ldc \"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"\n");
        J("    areturn\n");
        J("Lkwg_not_ucase:\n");
        /* &LCASE — lowercase alphabet a-z */
        J("    aload_0\n");
        J("    ldc \"LCASE\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_lcase\n");
        J("    ldc \"abcdefghijklmnopqrstuvwxyz\"\n");
        J("    areturn\n");
        J("Lkwg_not_lcase:\n");
        /* &STNO — statement counter */
        J("    aload_0\n");
        J("    ldc \"STNO\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_stno\n");
        {
            char stnodesc[512];
            snprintf(stnodesc, sizeof stnodesc, "%s/sno_kw_STNO I", jvm_classname);
            J("    getstatic %s\n", stnodesc);
        }
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_stno:\n");
        /* &TRIM / &ANCHOR: stored in sno_kw_* static int fields */
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
        J("    aload_0\n");
        J("    ldc \"STLIMIT\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_stlimit\n");
        {
            char sldesc2[512];
            snprintf(sldesc2, sizeof sldesc2, "%s/sno_kw_STLIMIT I", jvm_classname);
            J("    getstatic %s\n", sldesc2);
        }
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_stlimit:\n");
        J("    aload_0\n");
        J("    ldc \"STCOUNT\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkwg_not_stcount\n");
        {
            char scdesc2[512];
            snprintf(scdesc2, sizeof scdesc2, "%s/sno_kw_STCOUNT I", jvm_classname);
            J("    getstatic %s\n", scdesc2);
        }
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lkwg_not_stcount:\n");
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
        J("    aload_0\n");
        J("    ldc \"STNO\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkws_not_stno\n");
        J("    aload_1\n");
        J("    invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I\n");
        {
            char stnodesc2[512];
            snprintf(stnodesc2, sizeof stnodesc2, "%s/sno_kw_STNO I", jvm_classname);
            J("    putstatic %s\n", stnodesc2);
        }
        J("    return\n");
        J("Lkws_not_stno:\n");
        J("    aload_0\n");
        J("    ldc \"STLIMIT\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkws_not_stlimit\n");
        J("    aload_1\n");
        J("    invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I\n");
        {
            char sldesc[512];
            snprintf(sldesc, sizeof sldesc, "%s/sno_kw_STLIMIT I", jvm_classname);
            J("    putstatic %s\n", sldesc);
        }
        J("    return\n");
        J("Lkws_not_stlimit:\n");
        J("    aload_0\n");
        J("    ldc \"STCOUNT\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lkws_not_stcount\n");
        J("    aload_1\n");
        J("    invokestatic java/lang/Integer/parseInt(Ljava/lang/String;)I\n");
        {
            char scdesc[512];
            snprintf(scdesc, sizeof scdesc, "%s/sno_kw_STCOUNT I", jvm_classname);
            J("    putstatic %s\n", scdesc);
        }
        J("    return\n");
        J("Lkws_not_stcount:\n");
        J("    return\n");
        J(".end method\n\n");

        jvm_need_kw_helpers = 0;
    }

    if (jvm_need_varmap || jvm_need_indr_helpers) {
        /* sno_var_put(String name, String val) → void
         * Stores val into sno_vars HashMap for indirect access.
         * Also calls sno_mon_var(name,val) when MONITOR_READY_PIPE is set (compiled trace pathway). */
        J(".method static sno_var_put(Ljava/lang/String;Ljava/lang/String;)V\n");
        J("    .limit stack 4\n");
        J("    .limit locals 2\n");
        /* If name == "OUTPUT", print to stdout and return */
        char stdesc[512];
        snprintf(stdesc, sizeof stdesc, "%s/sno_stdout Ljava/io/PrintStream;", jvm_classname);
        J("    aload_0\n");
        J("    ldc \"OUTPUT\"\n");
        J("    invokevirtual java/lang/String/equalsIgnoreCase(Ljava/lang/String;)Z\n");
        J("    ifeq Lsvp_not_output\n");
        J("    getstatic %s\n", stdesc);
        J("    aload_1\n");
        J("    invokevirtual java/io/PrintStream/println(Ljava/lang/String;)V\n");
        J("    return\n");
        J("Lsvp_not_output:\n");
        char vmdesc[512];
        snprintf(vmdesc, sizeof vmdesc, "%s/sno_vars Ljava/util/HashMap;", jvm_classname);
        J("    getstatic %s\n", vmdesc);
        J("    aload_0\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        /* Compiled trace pathway: call sno_mon_var(name, val) */
        {
            char mmdesc[512];
            snprintf(mmdesc, sizeof mmdesc,
                "%s/sno_mon_var(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
            J("    aload_0\n");
            J("    aload_1\n");
            J("    invokestatic %s\n", mmdesc);
        }
        J("    return\n");
        J(".end method\n\n");

        /* sno_mon_var(String name, String val) → void
         * Wire protocol: VALUE RS name US val RS
         * \x1E (RS 0x1E) = record terminator; \x1F (US 0x1F) = name/value separator.
         * Sync-step: after write, blocks reading 1 byte ack from sno_mon_go_fd.
         * 'G' → return normally. 'S' or error → System.exit(0).
         * No-op if sno_mon_fd is null. */
        J(".method static sno_mon_var(Ljava/lang/String;Ljava/lang/String;)V\n");
        J("    .limit stack 6\n");
        J("    .limit locals 3\n");
        /* local 0=name, 1=val, 2=record bytes */
        char monfd2[512], monackfd2[512];
        snprintf(monfd2,    sizeof monfd2,    "%s/sno_mon_fd Ljava/io/OutputStream;", jvm_classname);
        snprintf(monackfd2, sizeof monackfd2, "%s/sno_mon_go_fd Ljava/io/InputStream;", jvm_classname);
        J("    getstatic %s\n", monfd2);
        J("    ifnull Lsmv_done\n");
        /* Build: "VALUE" + RS + name + US + val + RS  (RS=\x1E, US=\x1F) */
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    ldc \"VALUE\"\n");
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    bipush 30\n");   /* RS = 0x1E */
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    aload_0\n");     /* name */
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    bipush 31\n");   /* US = 0x1F */
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    aload_1\n");     /* val */
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    bipush 30\n");   /* RS = 0x1E */
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    ldc \"UTF-8\"\n");
        J("    invokevirtual java/lang/String/getBytes(Ljava/lang/String;)[B\n");
        J("    astore_2\n");
        J("    getstatic %s\n", monfd2);
        J("    aload_2\n");
        J("    invokevirtual java/io/OutputStream/write([B)V\n");
        /* Sync-step: read 1-byte ack. 'S' or error → exit(0). */
        J("    getstatic %s\n", monackfd2);
        J("    ifnull Lsmv_done\n");
        J("    getstatic %s\n", monackfd2);
        J("    invokevirtual java/io/InputStream/read()I\n");
        /* read() returns ack byte as int (0-255) or -1 on EOF */
        /* 'G' = 71 → continue; anything else → exit(0) */
        /* Store to local 2 (reuse) so the exit path stack is clean */
        J("    istore_2\n");
        J("    iload_2\n");
        J("    bipush 71\n");    /* 'G' */
        J("    if_icmpeq Lsmv_done\n");
        J("    iconst_0\n");
        J("    invokestatic SnoRuntime/sno_exit(I)V\n");
        J("Lsmv_done:\n");
        J("    return\n");
        J(".end method\n\n");

        /* sno_stcount_tick() → void
         * Increments &STCOUNT. If &STLIMIT >= 0 and &STCOUNT > &STLIMIT → terminate. */
        J(".method static sno_stcount_tick()V\n");
        J("    .limit stack 4\n");
        J("    .limit locals 0\n");
        {
            char scdesc3[512], sldesc3[512];
            snprintf(scdesc3, sizeof scdesc3, "%s/sno_kw_STCOUNT I", jvm_classname);
            snprintf(sldesc3, sizeof sldesc3, "%s/sno_kw_STLIMIT I", jvm_classname);
            /* increment STCOUNT */
            J("    getstatic %s\n", scdesc3);
            J("    iconst_1\n");
            J("    iadd\n");
            J("    putstatic %s\n", scdesc3);
            /* check STLIMIT: if STLIMIT < 0, skip (unlimited) */
            J("    getstatic %s\n", sldesc3);
            J("    iflt Lstick_ok\n");
            /* STCOUNT > STLIMIT? */
            J("    getstatic %s\n", scdesc3);
            J("    getstatic %s\n", sldesc3);
            J("    if_icmple Lstick_ok\n");
            /* exceeded — print message and exit */
            J("    getstatic java/lang/System/err Ljava/io/PrintStream;\n");
            J("    ldc \"Termination: statement limit\"\n");
            J("    invokevirtual java/io/PrintStream/println(Ljava/lang/String;)V\n");
            J("    iconst_1\n");
            J("    invokestatic SnoRuntime/sno_exit(I)V\n");
            J("Lstick_ok:\n");
        }
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
        J("    ifnonnull Lsig_done_indr\n");
        J("    pop\n");
        J("    ldc \"\"\n");
        J("Lsig_done_indr:\n");
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

    /* sno_replace(str, from, to) → translate chars char-by-char */
    if (jvm_need_replace_helper) {
        J(".method static sno_replace(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 8\n");
        /* locals: 0=str, 1=from, 2=to, 3=sb, 4=i(loop), 5=slen, 6=ch, 7=idx */
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    astore 3\n");
        J("    iconst_0\n");
        J("    istore 4\n");   /* i = 0 */
        J("    aload_0\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    istore 5\n");   /* slen */
        J("Lrep_loop:\n");
        J("    iload 4\n");
        J("    iload 5\n");
        J("    if_icmpge Lrep_done\n");
        J("    aload_0\n");
        J("    iload 4\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    istore 6\n");   /* ch = str.charAt(i) */
        /* idx = from.indexOf(ch) */
        J("    aload_1\n");
        J("    iload 6\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    istore 7\n");
        J("    iload 7\n");
        J("    iflt Lrep_no_trans\n");
        /* translated: check idx < to.length() */
        J("    iload 7\n");
        J("    aload_2\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    if_icmpge Lrep_no_trans\n");
        /* append to.charAt(idx) */
        J("    aload 3\n");
        J("    aload_2\n");
        J("    iload 7\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    pop\n");
        J("    goto Lrep_next\n");
        J("Lrep_no_trans:\n");
        /* keep original char */
        J("    aload 3\n");
        J("    iload 6\n");
        J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
        J("    pop\n");
        J("Lrep_next:\n");
        J("    iinc 4 1\n");
        J("    goto Lrep_loop\n");
        J("Lrep_done:\n");
        J("    aload 3\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_replace_helper = 0;
    }

    /* sno_lpad(str, n, pad) → left-pad str to width n using pad char */
    if (jvm_need_lpad_helper) {
        J(".method static sno_lpad(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 5\n");
        /* locals: 0=str, 1=n, 2=pad, 3=slen, 4=sb */
        J("    aload_0\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    istore 3\n");
        J("    iload_1\n");
        J("    iload 3\n");
        J("    if_icmple Llpad_done\n");  /* n <= slen → no padding */
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    astore 4\n");
        J("Llpad_loop:\n");
        J("    iload_1\n");
        J("    iload 3\n");
        J("    if_icmple Llpad_append_str\n");
        J("    aload 4\n");
        J("    aload_2\n");
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    pop\n");
        J("    iinc 3 1\n");
        J("    goto Llpad_loop\n");
        J("Llpad_append_str:\n");
        J("    aload 4\n");
        J("    aload_0\n");
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    pop\n");
        J("    aload 4\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    areturn\n");
        J("Llpad_done:\n");
        J("    aload_0\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_lpad_helper = 0;
    }

    /* sno_rpad(str, n, pad) → right-pad str to width n using pad char */
    if (jvm_need_rpad_helper) {
        J(".method static sno_rpad(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 5\n");
        J("    aload_0\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    istore 3\n");
        J("    iload_1\n");
        J("    iload 3\n");
        J("    if_icmple Lrpad_done\n");
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    aload_0\n");
        J("    invokespecial java/lang/StringBuilder/<init>(Ljava/lang/String;)V\n");
        J("    astore 4\n");
        J("Lrpad_loop:\n");
        J("    iload_1\n");
        J("    iload 3\n");
        J("    if_icmple Lrpad_finish\n");
        J("    aload 4\n");
        J("    aload_2\n");
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    pop\n");
        J("    iinc 3 1\n");
        J("    goto Lrpad_loop\n");
        J("Lrpad_finish:\n");
        J("    aload 4\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lrpad_done:\n");
        J("    aload_0\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_rpad_helper = 0;
    }

    /* sno_is_integer(str) → boolean Z: true if str represents an integer */
    if (jvm_need_integer_helper) {
        J(".method static sno_is_integer(Ljava/lang/String;)Z\n");
        J("    .limit stack 4\n");
        J("    .limit locals 1\n");
        J("    aload_0\n");
        J("    ifnull Lisi_false\n");
        J("    aload_0\n");
        J("    invokevirtual java/lang/String/trim()Ljava/lang/String;\n");
        J("    invokevirtual java/lang/String/isEmpty()Z\n");
        J("    ifne Lisi_false\n");
        /* Try Long.parseLong; catch NumberFormatException → false */
        J("    aload_0\n");
        J("    invokevirtual java/lang/String/trim()Ljava/lang/String;\n");
        /* Use regex: optional sign, then digits */
        J("    ldc \"-?[0-9]+\"\n");
        J("    invokevirtual java/lang/String/matches(Ljava/lang/String;)Z\n");
        J("    ireturn\n");
        J("Lisi_false:\n");
        J("    iconst_0\n");
        J("    ireturn\n");
        J(".end method\n\n");
        jvm_need_integer_helper = 0;
    }

    /* sno_datatype(str) → "STRING", "INTEGER", or "REAL" */
    if (jvm_need_datatype_helper) {
        J(".method static sno_datatype(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 1\n");
        /* Check integer: -?[0-9]+ */
        J("    aload_0\n");
        J("    ldc \"-?[0-9]+\"\n");
        J("    invokevirtual java/lang/String/matches(Ljava/lang/String;)Z\n");
        J("    ifeq Ldt_not_int\n");
        J("    ldc \"integer\"\n");
        J("    areturn\n");
        J("Ldt_not_int:\n");
        /* Check real: contains digit and (. or e/E) */
        J("    aload_0\n");
        J("    ldc \"-?[0-9]*\\\\.?[0-9]+([eEdD][+-]?[0-9]+)?\"\n");
        J("    invokevirtual java/lang/String/matches(Ljava/lang/String;)Z\n");
        J("    ifeq Ldt_not_real\n");
        J("    ldc \"real\"\n");
        J("    areturn\n");
        J("Ldt_not_real:\n");
        J("    ldc \"string\"\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_datatype_ext(str) → user type name, "STRING", "INTEGER", or "REAL"
         * Checks sno_arrays for a __type__ entry first (DATA instances).
         * Falls back to sno_datatype for plain strings. */
        char am_dt[512]; snprintf(am_dt, sizeof am_dt,
            "%s/sno_arrays Ljava/util/HashMap;", jvm_classname);
        J(".method static sno_datatype_ext(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 2\n");
        /* Look up instance_id in sno_arrays */
        J("    getstatic %s\n", am_dt);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_1\n");            /* local 1 = inner HashMap or null */
        J("    aload_1\n");
        J("    ifnull Ldte_plain\n");   /* no DATA instance → plain type */
        /* Look up __type__ in the inner map */
        J("    aload_1\n");
        J("    ldc \"__type__\"\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/lang/String\n");
        J("    astore_1\n");            /* local 1 = type string or null */
        J("    aload_1\n");
        J("    ifnull Ldte_plain\n");   /* no __type__ → plain type */
        J("    aload_1\n");
        J("    areturn\n");             /* return user type name */
        /* Plain type fallback — stack is empty, local 1 is null */
        J("Ldte_plain:\n");
        J("    aload_0\n");
        char dtdesc2[512]; snprintf(dtdesc2, sizeof dtdesc2,
            "%s/sno_datatype(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        J("    invokestatic %s\n", dtdesc2);
        J("    areturn\n");
        J(".end method\n\n");

        jvm_need_datatype_helper = 0;
    }

    /* Array/Table helpers */
    if (jvm_need_array_helpers) {
        char am[512]; snprintf(am, sizeof am, "%s/sno_arrays Ljava/util/HashMap;", jvm_classname);
        /* sno_array_new(String size) → String (array-id) — no init, kept for 2D alloc */
        J(".method static sno_array_new(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 2\n");
        J("    new java/util/HashMap\n");
        J("    dup\n");
        J("    invokespecial java/util/HashMap/<init>()V\n");
        J("    astore_1\n");
        /* Generate unique id: System.identityHashCode as string */
        J("    aload_1\n");
        J("    invokestatic java/lang/System/identityHashCode(Ljava/lang/Object;)I\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        /* Store in sno_arrays: sno_arrays.put(id, map) */
        J("    dup\n");  /* id on stack twice */
        J("    getstatic %s\n", am);
        J("    swap\n");  /* sno_arrays, id */
        J("    aload_1\n");  /* map */
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_array_new2(String size, String init) → String (array-id)
         * Creates a 1-based array of <size> elements each pre-set to <init>. */
        J(".method static sno_array_new2(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 8\n");
        J("    .limit locals 6\n");
        /* local 0=size_str, 1=init_str, 2=HashMap, 3=id, 4=n(int), 5=i(int) */
        J("    new java/util/HashMap\n");
        J("    dup\n");
        J("    invokespecial java/util/HashMap/<init>()V\n");
        J("    astore_2\n");
        J("    aload_2\n");
        J("    invokestatic java/lang/System/identityHashCode(Ljava/lang/Object;)I\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    astore_3\n");
        /* sno_arrays.put(id, map) */
        J("    getstatic %s\n", am);
        J("    aload_3\n");
        J("    aload_2\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        /* n = Integer.parseInt(size_str) — use Double to be safe */
        J("    aload_0\n");
        J("    invokestatic java/lang/Double/parseDouble(Ljava/lang/String;)D\n");
        J("    d2i\n");
        J("    istore 4\n");
        /* if init is empty string, skip fill loop */
        J("    aload_1\n");
        J("    invokevirtual java/lang/String/isEmpty()Z\n");
        J("    ifne La2_done\n");
        /* fill loop: for i=1; i<=n; i++ put(i, init) */
        J("    iconst_1\n");
        J("    istore 5\n");
        J("La2_loop:\n");
        J("    iload 5\n");
        J("    iload 4\n");
        J("    if_icmpgt La2_done\n");
        J("    aload_2\n");
        J("    iload 5\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    iinc 5 1\n");
        J("    goto La2_loop\n");
        J("La2_done:\n");
        J("    aload_3\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_table_new() → String (table-id) */
        J(".method static sno_table_new()Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 2\n");
        J("    new java/util/HashMap\n");
        J("    dup\n");
        J("    invokespecial java/util/HashMap/<init>()V\n");
        J("    astore_0\n");
        J("    aload_0\n");
        J("    invokestatic java/lang/System/identityHashCode(Ljava/lang/Object;)I\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    dup\n");
        J("    getstatic %s\n", am);
        J("    swap\n");
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    areturn\n");
        J(".end method\n\n");

        J(".method static sno_array_get(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 3\n");
        J("    getstatic %s\n", am);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_2\n");
        J("    aload_2\n");
        J("    ifnull Lag_null\n");
        J("    aload_2\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/lang/String\n");
        J("    areturn\n");   /* return value or null if key not found — null = SNOBOL4 failure */
        J("Lag_null:\n");
        J("    aconst_null\n");   /* array-id not found → failure */
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_array_put(String array_id, String key, String val) → void */
        J(".method static sno_array_put(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V\n");
        J("    .limit stack 4\n");
        J("    .limit locals 4\n");
        J("    getstatic %s\n", am);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_3\n");
        J("    aload_3\n");
        J("    ifnull Lap_done\n");
        J("    aload_3\n");
        J("    aload_1\n");
        J("    aload_2\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("Lap_done:\n");
        J("    return\n");
        J(".end method\n\n");

        jvm_need_array_helpers = 0;
    }

    /* SORT helper — sno_sort(String table_id) → String array_id
     * Converts a TABLE (stored as HashMap of HashMaps in sno_arrays) into a
     * sorted 2D array [1..n, 1..2]: col 1 = key, col 2 = value.
     * Keys are sorted lexicographically via TreeMap. */
    if (jvm_need_sort_helper) {
        char am2[512]; snprintf(am2, sizeof am2, "%s/sno_arrays Ljava/util/HashMap;", jvm_classname);
        J(".method static sno_sort(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 8\n");
        J("    .limit locals 8\n");
        /* local 0 = table_id, 1 = inner/new HashMap, 2 = TreeMap,
         * 3 = iterator, 4 = entry, 5 = array_id, 6 = row counter, 7 = temp */
        J("    getstatic %s\n", am2);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_1\n");
        J("    aload_1\n");
        J("    ifnonnull Lsort_ok\n");
        J("    aconst_null\n");
        J("    areturn\n");
        J("Lsort_ok:\n");
        J("    new java/util/TreeMap\n");
        J("    dup\n");
        J("    aload_1\n");
        J("    invokespecial java/util/TreeMap/<init>(Ljava/util/Map;)V\n");
        J("    astore_2\n");
        /* Create the result array HashMap and get its id via identityHashCode */
        J("    new java/util/HashMap\n");
        J("    dup\n");
        J("    invokespecial java/util/HashMap/<init>()V\n");
        J("    astore_1\n");
        J("    aload_1\n");
        J("    invokestatic java/lang/System/identityHashCode(Ljava/lang/Object;)I\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    astore 5\n");    /* array_id */
        J("    getstatic %s\n", am2);
        J("    aload 5\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        /* Iterate TreeMap, build rows */
        J("    iconst_1\n");
        J("    istore 6\n");
        J("    aload_2\n");
        J("    invokevirtual java/util/TreeMap/entrySet()Ljava/util/Set;\n");
        J("    invokeinterface java/util/Set/iterator()Ljava/util/Iterator; 1\n");
        J("    astore_3\n");
        J("Lsort_loop:\n");
        J("    aload_3\n");
        J("    invokeinterface java/util/Iterator/hasNext()Z 1\n");
        J("    ifeq Lsort_done\n");
        J("    aload_3\n");
        J("    invokeinterface java/util/Iterator/next()Ljava/lang/Object; 1\n");
        J("    checkcast java/util/Map$Entry\n");
        J("    astore 4\n");
        /* store key at "row,1" */
        J("    aload_1\n");
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    iload 6\n");
        J("    invokevirtual java/lang/StringBuilder/append(I)Ljava/lang/StringBuilder;\n");
        J("    ldc \",1\"\n");
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    aload 4\n");
        J("    invokeinterface java/util/Map$Entry/getKey()Ljava/lang/Object; 1\n");
        J("    checkcast java/lang/String\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        /* store val at "row,2" */
        J("    aload_1\n");
        J("    new java/lang/StringBuilder\n");
        J("    dup\n");
        J("    invokespecial java/lang/StringBuilder/<init>()V\n");
        J("    iload 6\n");
        J("    invokevirtual java/lang/StringBuilder/append(I)Ljava/lang/StringBuilder;\n");
        J("    ldc \",2\"\n");
        J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n");
        J("    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
        J("    aload 4\n");
        J("    invokeinterface java/util/Map$Entry/getValue()Ljava/lang/Object; 1\n");
        J("    checkcast java/lang/String\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    iinc 6 1\n");
        J("    goto Lsort_loop\n");
        J("Lsort_done:\n");
        /* store __rows__ */
        J("    aload_1\n");
        J("    ldc \"__rows__\"\n");
        J("    iload 6\n");
        J("    iconst_1\n");
        J("    isub\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("    aload 5\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_sort_helper = 0;
    }
    if (jvm_need_convert_helper) {
        /* sno_convert_table(String table_id) → String array_id
         * CONVERT(t,'ARRAY'): build 2-D array rows[1..n, 1..2] where
         * col 1 = key, col 2 = value, ordered by insertion (HashMap order).
         * Uses same sno_arrays/sno_tables infrastructure as sno_sort. */
        J(".method static sno_convert_table(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 8\n");
        J("    .limit locals 8\n");
        /* local 0: table_id (arg)
         * local 1: inner HashMap (the table)
         * local 2: entry Set
         * local 3: Iterator
         * local 4: row counter (int)
         * local 5: result array_id (String)
         * local 6: current Map.Entry
         * local 7: scratch String */
        char ctam[512]; snprintf(ctam, sizeof ctam, "%s/sno_arrays Ljava/util/HashMap;", jvm_classname);
        /* Get inner HashMap for this table id */
        J("    getstatic %s\n", ctam);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_1\n");
        J("    aload_1\n");
        J("    ifnonnull Lct_have_table\n");
        /* no such table — return empty array id */
        J("    ldc \"\"\n");
        J("    areturn\n");
        J("Lct_have_table:\n");
        /* create result 2-D array: use sno_array_new2(nrows_str, "2") */
        /* first: count entries */
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/size()I\n");
        J("    istore 4\n");   /* row count */
        /* allocate array via sno_array_new2 */
        char ctanew[512]; snprintf(ctanew, sizeof ctanew,
            "%s/sno_array_new2(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        J("    iload 4\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    ldc \"2\"\n");
        J("    invokestatic %s\n", ctanew);
        J("    astore 5\n");   /* result array id */
        /* iterate: for each entry, store key→[i,1], value→[i,2] */
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/entrySet()Ljava/util/Set;\n");
        J("    invokeinterface java/util/Set/iterator()Ljava/util/Iterator; 1\n");
        J("    astore_3\n");
        J("    iconst_0\n");
        J("    istore 4\n");   /* reset row counter to 0 */
        char ctaput[512]; snprintf(ctaput, sizeof ctaput,
            "%s/sno_array_put(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("Lct_loop:\n");
        J("    aload_3\n");
        J("    invokeinterface java/util/Iterator/hasNext()Z 1\n");
        J("    ifeq Lct_done\n");
        J("    iinc 4 1\n");   /* row++ */
        J("    aload_3\n");
        J("    invokeinterface java/util/Iterator/next()Ljava/lang/Object; 1\n");
        J("    checkcast java/util/Map$Entry\n");
        J("    astore 6\n");
        /* store key at [row, 1] */
        J("    aload 5\n");    /* array id */
        /* composite key: row + "," + "1" */
        J("    iload 4\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    ldc \",1\"\n");
        J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    aload 6\n");
        J("    invokeinterface java/util/Map$Entry/getKey()Ljava/lang/Object; 1\n");
        J("    checkcast java/lang/String\n");
        J("    invokestatic %s\n", ctaput);
        /* store value at [row, 2] */
        J("    aload 5\n");
        J("    iload 4\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    ldc \",2\"\n");
        J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    aload 6\n");
        J("    invokeinterface java/util/Map$Entry/getValue()Ljava/lang/Object; 1\n");
        J("    checkcast java/lang/String\n");
        J("    invokestatic %s\n", ctaput);
        J("    goto Lct_loop\n");
        J("Lct_done:\n");
        /* store dimension metadata: array_id+"__nrows__" → row count */
        J("    aload 5\n");
        J("    ldc \"__nrows__\"\n");
        J("    iload 4\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    invokestatic %s\n", ctaput);
        J("    aload 5\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_convert_helper = 0;
    }
    if (jvm_need_prototype_helper) {
        /* sno_prototype(String array_id) → String
         * Returns "n,2" for 2-D arrays (those with __nrows__ metadata),
         * "n" for 1-D arrays.  Mirrors SNOBOL4 PROTOTYPE output. */
        J(".method static sno_prototype(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 6\n");
        J("    .limit locals 3\n");
        /* local 0: array_id
         * local 1: inner HashMap
         * local 2: nrows value String */
        char ptam[512]; snprintf(ptam, sizeof ptam, "%s/sno_arrays Ljava/util/HashMap;", jvm_classname);
        char ptaget[512]; snprintf(ptaget, sizeof ptaget,
            "%s/sno_array_get(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        /* Try to get __nrows__ metadata */
        J("    aload_0\n");
        J("    ldc \"__nrows__\"\n");
        J("    invokestatic %s\n", ptaget);
        J("    astore_1\n");
        J("    aload_1\n");
        J("    ifnull Lpt_1d\n");
        /* 2-D: return nrows + ",2" */
        J("    aload_1\n");
        J("    ldc \",2\"\n");
        J("    invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lpt_1d:\n");
        /* 1-D: count keys excluding metadata (approximate: use HashMap size) */
        J("    getstatic %s\n", ptam);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_1\n");
        J("    aload_1\n");
        J("    ifnull Lpt_empty\n");
        J("    aload_1\n");
        J("    invokevirtual java/util/HashMap/size()I\n");
        J("    invokestatic java/lang/Integer/toString(I)Ljava/lang/String;\n");
        J("    areturn\n");
        J("Lpt_empty:\n");
        J("    ldc \"0\"\n");
        J("    areturn\n");
        J(".end method\n\n");
        jvm_need_prototype_helper = 0;
    }
    if (jvm_need_data_helpers) {
        char dm[512]; snprintf(dm, sizeof dm, "%s/sno_data_types Ljava/util/HashMap;", jvm_classname);
        /* sno_data_define(String proto) → void
         * proto = "typename(field1,field2,...)" — registers fields */
        J(".method static sno_data_define(Ljava/lang/String;)V\n");
        J("    .limit stack 6\n");
        J("    .limit locals 4\n");
        /* parse: find '(' ')' to extract type name and field list */
        /* For simplicity, store proto string itself in sno_data_types keyed by type name */
        /* Extract type name up to '(' */
        J("    aload_0\n");
        J("    ldc \"(\"\n");
        J("    invokevirtual java/lang/String/indexOf(Ljava/lang/String;)I\n");
        J("    istore_1\n");  /* idx of '(' */
        J("    iload_1\n");
        J("    iflt Ldd_done\n");
        J("    aload_0\n");
        J("    iconst_0\n");
        J("    iload_1\n");
        J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
        J("    astore_2\n");  /* type name */
        /* fields: from '(' to ')' */
        J("    aload_0\n");
        J("    iload_1\n");
        J("    iconst_1\n");
        J("    iadd\n");
        J("    aload_0\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    iconst_1\n");
        J("    isub\n");
        J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
        J("    astore_3\n");  /* field list */
        J("    getstatic %s\n", dm);
        J("    aload_2\n");
        J("    aload_3\n");
        J("    invokevirtual java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    pop\n");
        J("Ldd_done:\n");
        J("    return\n");
        J(".end method\n\n");

        /* sno_data_new(String type_name, String[] field_values) → String (instance-id)
         * Implemented as sno_array_new with __type__ key set */
        /* sno_data_get_field(String instance_id, String field_name) → String
         * Stack-safe: store the inner HashMap in local 2, never leave it stranded. */
        J(".method static sno_data_get_field(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 3\n");
        char am2[512]; snprintf(am2, sizeof am2, "%s/sno_arrays Ljava/util/HashMap;", jvm_classname);
        J("    getstatic %s\n", am2);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_2\n");              /* local 2 = inner HashMap (or null) */
        J("    aload_2\n");
        J("    ifnull Ldgf_null\n");      /* no inner map → return "" */
        J("    aload_2\n");
        J("    aload_1\n");              /* field name */
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/lang/String\n");
        J("    dup\n");
        J("    ifnonnull Ldgf_done\n");  /* non-null value → return it */
        J("    pop\n");
        J("Ldgf_null:\n");
        J("    ldc \"\"\n");
        J("Ldgf_done:\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* sno_data_get_type(String instance_id) → String type_name
         * Stack-safe: same pattern as sno_data_get_field. */
        J(".method static sno_data_get_type(Ljava/lang/String;)Ljava/lang/String;\n");
        J("    .limit stack 4\n");
        J("    .limit locals 2\n");
        J("    getstatic %s\n", am2);
        J("    aload_0\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/util/HashMap\n");
        J("    astore_1\n");              /* local 1 = inner HashMap (or null) */
        J("    aload_1\n");
        J("    ifnull Ldgt_null\n");
        J("    aload_1\n");
        J("    ldc \"__type__\"\n");
        J("    invokevirtual java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
        J("    checkcast java/lang/String\n");
        J("    dup\n");
        J("    ifnonnull Ldgt_done\n");
        J("    pop\n");
        J("Ldgt_null:\n");
        J("    ldc \"\"\n");
        J("Ldgt_done:\n");
        J("    areturn\n");
        J(".end method\n\n");

        jvm_need_data_helpers = 0;
    }
}

/* -----------------------------------------------------------------------
 * User-defined function support (Sprint J-R4) — implementation
 * (JvmFnDef struct and jvm_fn_table_fwd/jvm_fn_count_fwd declared at top)
 * ----------------------------------------------------------------------- */

#define JVM_FN_MAX  JVM_FN_MAX_FWD
#define JVM_ARG_MAX JVM_ARG_MAX_FWD
#define jvm_fn_table jvm_fn_table_fwd
#define jvm_fn_count jvm_fn_count_fwd

static void jvm_parse_proto(const char *proto, JvmFnDef *fn) {
    int i=0; char buf[256]; int j=0;
    while (proto[i] && proto[i]!='(' && proto[i]!=')') buf[j++]=proto[i++];
    buf[j]='\0';
    while (j>0 && buf[j-1]==' ') buf[--j]='\0';
    fn->name = strdup(buf); fn->nargs = 0; fn->nlocals = 0;
    if (proto[i]=='(') {
        i++;
        while (proto[i] && proto[i]!=')') {
            j=0;
            while (proto[i] && proto[i]!=',' && proto[i]!=')') buf[j++]=proto[i++];
            buf[j]='\0';
            while (j>0&&(buf[j-1]==' '||buf[j-1]=='\t')) buf[--j]='\0';
            int k=0; while(buf[k]==' '||buf[k]=='\t') k++;
            if (buf[k] && fn->nargs < JVM_ARG_MAX) fn->args[fn->nargs++]=strdup(buf+k);
            if (proto[i]==',') i++;
        }
        if (proto[i]==')') i++;
    }
    while (proto[i]) {
        j=0;
        while (proto[i] && proto[i]!=',') buf[j++]=proto[i++];
        buf[j]='\0';
        int k=0; while(buf[k]==' '||buf[k]=='\t') k++;
        while (j>0&&(buf[j-1]==' '||buf[j-1]=='\t')) buf[--j]='\0';
        if (buf[k] && fn->nlocals < JVM_ARG_MAX) fn->locals[fn->nlocals++]=strdup(buf+k);
        if (proto[i]==',') i++;
    }
}

static const char *jvm_flatten_str(EXPR_t *e, char *buf, int bufsz) {
    if (!e) return NULL;
    if (e->kind == E_QLIT) { strncpy(buf, e->sval ? e->sval : "", bufsz-1); return buf; }
    if (e->kind == E_CONCAT) {
        char lb[2048], rb[2048];
        const char *l = jvm_flatten_str(e->children[0], lb, sizeof lb);
        const char *r = jvm_flatten_str(e->children[1], rb, sizeof rb);
        if (!l || !r) return NULL;
        snprintf(buf, bufsz, "%s%s", l, r); return buf;
    }
    return NULL;
}

static void jvm_collect_functions(Program *prog) {
    jvm_fn_count = 0;
    jvm_data_type_count = 0;
    char pbuf[4096];
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_FNC) continue;
        const char *sname = s->subject->sval ? s->subject->sval : "";

        /* Collect DATA type definitions */
        if (strcasecmp(sname, "DATA") == 0) {
            if (!s->subject->children || !s->subject->children[0]) continue;
            const char *proto = jvm_flatten_str(s->subject->children[0], pbuf, sizeof pbuf);
            if (!proto || jvm_data_type_count >= JVM_DATA_MAX) continue;
            /* Parse "typename(field1,field2,...)" */
            JvmDataType *dt = &jvm_data_types[jvm_data_type_count];
            memset(dt, 0, sizeof *dt);
            char tbuf[256]; int ti = 0, pi = 0;
            while (proto[pi] && proto[pi] != '(') tbuf[ti++] = proto[pi++];
            tbuf[ti] = '\0';
            /* trim trailing spaces */
            while (ti > 0 && tbuf[ti-1] == ' ') tbuf[--ti] = '\0';
            dt->type_name = strdup(tbuf);
            if (proto[pi] == '(') {
                pi++;
                while (proto[pi] && proto[pi] != ')') {
                    int j = 0; char fb[256];
                    while (proto[pi] && proto[pi] != ',' && proto[pi] != ')') fb[j++] = proto[pi++];
                    fb[j] = '\0';
                    int k = 0; while (fb[k] == ' ' || fb[k] == '\t') k++;
                    while (j > 0 && (fb[j-1] == ' ' || fb[j-1] == '\t')) fb[--j] = '\0';
                    if (fb[k] && dt->nfields < JVM_ARG_MAX) dt->fields[dt->nfields++] = strdup(fb+k);
                    if (proto[pi] == ',') pi++;
                }
            }
            jvm_data_type_count++;
            continue;
        }

        if (strcasecmp(sname, "DEFINE") != 0) continue;
        if (!s->subject->children || !s->subject->children[0]) continue;
        const char *proto = jvm_flatten_str(s->subject->children[0], pbuf, sizeof pbuf);
        if (!proto) continue;
        if (jvm_fn_count >= JVM_FN_MAX) break;
        JvmFnDef *fn = &jvm_fn_table[jvm_fn_count];
        memset(fn, 0, sizeof *fn);
        jvm_parse_proto(proto, fn);
        /* entry label: 2nd arg of DEFINE if present */
        if (s->subject->nchildren >= 2) {
            char ebuf[256];
            const char *el = jvm_flatten_str(s->subject->children[1], ebuf, sizeof ebuf);
            if (el && el[0]) fn->entry_label = strdup(el);
        }
        /* end label: from goto on DEFINE stmt */
        fn->end_label = NULL;
        if (s->go) {
            if (s->go->uncond) fn->end_label = strdup(s->go->uncond);
            else if (s->go->onsuccess) fn->end_label = strdup(s->go->onsuccess);
        }
        /* Fallback: DEFINE has no goto — check next stmt for :(endLabel) pattern.
         * Beauty.sno pattern: DEFINE('findRefs(x)n,v') on one line,
         * then next stmt has :(findRefsEnd) unconditional goto. */
        if (!fn->end_label && s->next && s->next->go) {
            if (s->next->go->uncond && s->next->go->uncond[0])
                fn->end_label = strdup(s->next->go->uncond);
            else if (s->next->go->onsuccess && s->next->go->onsuccess[0])
                fn->end_label = strdup(s->next->go->onsuccess);
        }
        jvm_fn_count++;
    }
}

/* Return FnDef if name matches a user function, else NULL */
static const JvmFnDef *jvm_find_fn(const char *name) {
    for (int i = 0; i < jvm_fn_count; i++)
        if (jvm_fn_table[i].name && strcasecmp(jvm_fn_table[i].name, name) == 0)
            return &jvm_fn_table[i];
    return NULL;
}

static const JvmDataType *jvm_find_data_type(const char *name) {
    for (int i = 0; i < jvm_data_type_count; i++)
        if (jvm_data_types[i].type_name && strcasecmp(jvm_data_types[i].type_name, name) == 0)
            return &jvm_data_types[i];
    return NULL;
}

static const JvmDataType *jvm_find_data_field(const char *field) {
    for (int i = 0; i < jvm_data_type_count; i++)
        for (int j = 0; j < jvm_data_types[i].nfields; j++)
            if (jvm_data_types[i].fields[j] && strcasecmp(jvm_data_types[i].fields[j], field) == 0)
                return &jvm_data_types[i];
    return NULL;
}

/* Return 1 if stmt is inside function fn's body (between entry label and end_label) */
static int jvm_stmt_in_fn(STMT_t *s, const JvmFnDef *fn, Program *prog) {
    const char *entry = fn->entry_label ? fn->entry_label : fn->name;
    int in_body = 0;
    for (STMT_t *t = prog->head; t; t = t->next) {
        if (t->label && strcasecmp(t->label, entry) == 0) in_body = 1;
        if (in_body && fn->end_label && t->label && strcasecmp(t->label, fn->end_label) == 0) in_body = 0;
        if (t == s) return in_body;
    }
    return 0;
}

/* Emit a single user-defined function as a static JVM method.
 * Strategy:
 *   - Method signature: static String sno_userfn_NAME(String arg0, String arg1, ...)
 *   - On entry: save old values of args/locals from sno_vars, bind new args
 *   - Body: inline all statements between entry label and end_label
 *   - RETURN / FRETURN: restore and areturn fn_name var / aconst_null areturn
 */
static void emit_jvm_fn_method(const JvmFnDef *fn, Program *prog, int fn_idx) {
    /* Build method descriptor: (Ljava/lang/String;...)Ljava/lang/String; */
    char desc[1024]; int dp = 0;
    desc[dp++] = '(';
    for (int i = 0; i < fn->nargs; i++) {
        const char *s2 = "Ljava/lang/String;";
        for (const char *p = s2; *p; p++) desc[dp++] = *p;
    }
    desc[dp++] = ')'; const char *ret = "Ljava/lang/String;";
    for (const char *p = ret; *p; p++) desc[dp++] = *p;
    desc[dp] = '\0';

    char mname[256]; snprintf(mname, sizeof mname, "sno_userfn_%s", fn->name);
    J(".method static %s%s\n", mname, desc);
    J("    .limit stack 16\n");
    J("    .limit locals 64\n");
    J("\n");

    /* Save old values of args+locals into local JVM vars, bind new args */
    /* JVM locals:
     *   0 .. nargs-1    : incoming argument strings (JVM params)
     *   nargs           : reserved (padding)
     *   locals start at nargs (old values of args[0], args[1], ..., locals[0]...):
     *   We'll use a flat save-array strategy: save old val of each arg/local
     *   into local JVM slot (nargs + index). Then restore on all exit paths.
     *
     * Simpler: use the sno_vars HashMap directly.
     * On entry: for each arg/local, save sno_vars[name] → JVM local, then put new value.
     * On exit: restore saved values.
     */
    int save_base = fn->nargs; /* first save slot (each occupies one JVM local) */
    /* save_base + fn->nargs = saves for args */
    /* save_base + fn->nargs + fn->nlocals = save for fn-return var */
    int save_fnret = save_base + fn->nargs + fn->nlocals;
    char vmdesc[512]; snprintf(vmdesc, sizeof vmdesc, "%s/sno_vars Ljava/util/HashMap;", jvm_classname);

    /* Save arg values from sno_vars */
    for (int i = 0; i < fn->nargs; i++) {
        char nameesc[256]; jvm_escape_string(fn->args[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        char igdesc[512]; snprintf(igdesc, sizeof igdesc,
            "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        J("    invokestatic %s\n", igdesc);
        J("    astore %d\n", save_base + i);
    }
    /* Save local values from sno_vars */
    for (int i = 0; i < fn->nlocals; i++) {
        char nameesc[256]; jvm_escape_string(fn->locals[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        char igdesc[512]; snprintf(igdesc, sizeof igdesc,
            "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        J("    invokestatic %s\n", igdesc);
        J("    astore %d\n", save_base + fn->nargs + i);
    }
    /* Save fn return-value var (fn->name itself) */
    {
        char nameesc[256]; jvm_escape_string(fn->name, nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        char igdesc[512]; snprintf(igdesc, sizeof igdesc,
            "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        J("    invokestatic %s\n", igdesc);
        J("    astore %d\n", save_fnret);
    }

    /* Bind incoming args: put each arg into sno_vars */
    for (int i = 0; i < fn->nargs; i++) {
        char nameesc[256]; jvm_escape_string(fn->args[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", i);  /* incoming arg */
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    /* Init locals to "" */
    for (int i = 0; i < fn->nlocals; i++) {
        char nameesc[256]; jvm_escape_string(fn->locals[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    ldc \"\"\n");
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    /* Init fn return-value var to "" */
    {
        char nameesc[256]; jvm_escape_string(fn->name, nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    ldc \"\"\n");
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }

    /* Helper labels */
    char lbl_return[64], lbl_freturn[64];
    snprintf(lbl_return,  sizeof lbl_return,  "Jfn%d_return",  fn_idx);
    snprintf(lbl_freturn, sizeof lbl_freturn, "Jfn%d_freturn", fn_idx);

    /* Emit function body statements */
    const char *entry = fn->entry_label ? fn->entry_label : fn->name;
    int in_body = 0;
    int stmt_idx = 0;
    const JvmFnDef *saved_cur_fn = jvm_cur_fn;
    jvm_cur_fn = fn;

    /* Raise arithmetic scratch locals above the save area so dstore/lstore
     * cannot clobber saved Object references.
     * Save area occupies slots: save_base .. save_fnret (each 1 JVM slot).
     * First free slot after save area: save_fnret + 1.
     * We need 4 consecutive slots for double (2) + long (2): base and base+2.
     * Round up to even for alignment: arith_base = (save_fnret + 1 + 1) & ~1 */
    int saved_arith_base = jvm_arith_local_base;
    int arith_base = save_fnret + 1;
    if (arith_base % 2 != 0) arith_base++;   /* align to even for double slots */
    jvm_arith_local_base = arith_base;

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->label && strcasecmp(s->label, entry) == 0) in_body = 1;
        if (in_body && fn->end_label && s->label && strcasecmp(s->label, fn->end_label) == 0) { in_body = 0; break; }
        if (!in_body) continue;
        if (s->is_end) break;
        /* Rewrite RETURN/FRETURN gotos to our local labels */
        /* We call emit_jvm_stmt but intercept RETURN/FRETURN in it via jvm_cur_fn */
        emit_jvm_stmt(s, 10000 + fn_idx * 1000 + stmt_idx++);
    }

    jvm_arith_local_base = saved_arith_base;
    jvm_cur_fn = saved_cur_fn;

    /* RETURN path: restore saved vars, return fn->name value */
    J("%s:\n", lbl_return);
    /* restore */
    for (int i = 0; i < fn->nargs; i++) {
        char nameesc[256]; jvm_escape_string(fn->args[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", save_base + i);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    for (int i = 0; i < fn->nlocals; i++) {
        char nameesc[256]; jvm_escape_string(fn->locals[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", save_base + fn->nargs + i);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    /* get return value before restoring fn name slot */
    {
        char nameesc[256]; jvm_escape_string(fn->name, nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        char igdesc[512]; snprintf(igdesc, sizeof igdesc,
            "%s/sno_indr_get(Ljava/lang/String;)Ljava/lang/String;", jvm_classname);
        J("    invokestatic %s\n", igdesc);
        /* restore fn name slot */
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", save_fnret);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    J("    areturn\n");

    /* FRETURN path: restore, return null */
    J("%s:\n", lbl_freturn);
    for (int i = 0; i < fn->nargs; i++) {
        char nameesc[256]; jvm_escape_string(fn->args[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", save_base + i);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    for (int i = 0; i < fn->nlocals; i++) {
        char nameesc[256]; jvm_escape_string(fn->locals[i], nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", save_base + fn->nargs + i);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    {
        char nameesc[256]; jvm_escape_string(fn->name, nameesc, sizeof nameesc);
        J("    ldc %s\n", nameesc);
        J("    aload %d\n", save_fnret);
        char vpdesc[512]; snprintf(vpdesc, sizeof vpdesc,
            "%s/sno_var_put(Ljava/lang/String;Ljava/lang/String;)V", jvm_classname);
        J("    invokestatic %s\n", vpdesc);
    }
    J("    aconst_null\n");
    J("    areturn\n");

    J(".end method\n\n");
}

static void emit_jvm_header(Program *prog) {
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
    J(".field static sno_kw_STNO I\n");
    J(".field static sno_kw_STLIMIT I\n");   /* -1 = unlimited */
    J(".field static sno_kw_STCOUNT I\n");   /* current step count */

    /* Dynamic variable map for indirect assignment (J2) */
    J(".field static sno_vars Ljava/util/HashMap;\n");
    /* Array/Table storage: maps array-id-string → HashMap */
    J(".field static sno_arrays Ljava/util/HashMap;\n");
    /* DATA type registry: maps type-name → comma-separated field list */
    J(".field static sno_data_types Ljava/util/HashMap;\n");
    /* Monitor trace fd — opened once from MONITOR_READY_PIPE env var, null if not set */
    J(".field static sno_mon_fd Ljava/io/OutputStream;\n");
    /* Monitor ack fd (sync-step) — opened once from MONITOR_GO_PIPE env var, null if not set */
    J(".field static sno_mon_go_fd Ljava/io/InputStream;\n");

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
    J("    iconst_0\n");
    J("    putstatic       %s/sno_kw_STNO I\n", jvm_classname);
    J("    iconst_m1\n");
    J("    putstatic       %s/sno_kw_STLIMIT I\n", jvm_classname);
    J("    iconst_0\n");
    J("    putstatic       %s/sno_kw_STCOUNT I\n", jvm_classname);
    /* init sno_vars HashMap */
    J("    new java/util/HashMap\n");
    J("    dup\n");
    J("    invokespecial java/util/HashMap/<init>()V\n");
    J("    putstatic       %s/sno_vars Ljava/util/HashMap;\n", jvm_classname);
    J("    new java/util/HashMap\n");
    J("    dup\n");
    J("    invokespecial java/util/HashMap/<init>()V\n");
    J("    putstatic       %s/sno_arrays Ljava/util/HashMap;\n", jvm_classname);
    J("    new java/util/HashMap\n");
    J("    dup\n");
    J("    invokespecial java/util/HashMap/<init>()V\n");
    J("    putstatic       %s/sno_data_types Ljava/util/HashMap;\n", jvm_classname);
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
    /* sno_mon_init() — opens MONITOR_READY_PIPE once, stores in sno_mon_fd static field.
     * Called from main() before any statements so FIFO is ready before first var write.
     * Separate from <clinit> because FileOutputStream open blocks on FIFO until
     * monitor_collect.py has the read side open — must happen at runtime, not class-load. */
    char monfd[512], monackfd[512];
    snprintf(monfd,    sizeof monfd,    "%s/sno_mon_fd Ljava/io/OutputStream;", jvm_classname);
    snprintf(monackfd, sizeof monackfd, "%s/sno_mon_go_fd Ljava/io/InputStream;", jvm_classname);
    J(".method static sno_mon_init()V\n");
    J("    .limit stack 6\n");
    J("    .limit locals 2\n");
    /* Open ready pipe (MONITOR_READY_PIPE) */
    J("    ldc \"MONITOR_READY_PIPE\"\n");
    J("    invokestatic java/lang/System/getenv(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    astore_0\n");
    J("    aload_0\n");
    J("    ifnull Lsmi_done\n");
    J("    aload_0\n");
    J("    invokevirtual java/lang/String/isEmpty()Z\n");
    J("    ifne Lsmi_done\n");
    J("    new java/io/FileOutputStream\n");
    J("    dup\n");
    J("    aload_0\n");
    J("    iconst_1\n");
    J("    invokespecial java/io/FileOutputStream/<init>(Ljava/lang/String;Z)V\n");
    J("    putstatic %s\n", monfd);
    /* Open go pipe (MONITOR_GO_PIPE) for sync-step — blocks until controller opens write end */
    J("    ldc \"MONITOR_GO_PIPE\"\n");
    J("    invokestatic java/lang/System/getenv(Ljava/lang/String;)Ljava/lang/String;\n");
    J("    astore_1\n");
    J("    aload_1\n");
    J("    ifnull Lsmi_done\n");
    J("    aload_1\n");
    J("    invokevirtual java/lang/String/isEmpty()Z\n");
    J("    ifne Lsmi_done\n");
    J("    new java/io/FileInputStream\n");
    J("    dup\n");
    J("    aload_1\n");
    J("    invokespecial java/io/FileInputStream/<init>(Ljava/lang/String;)V\n");
    J("    putstatic %s\n", monackfd);
    J("Lsmi_done:\n");
    J("    return\n");
    J(".end method\n\n");
}

static void emit_jvm_main_open(void) {
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 16\n");
    J("    .limit locals 32\n");
    J("\n");
    /* Open MONITOR_READY_PIPE (if set) before any statements — compiled trace pathway */
    {
        char initdesc[512];
        snprintf(initdesc, sizeof initdesc, "%s/sno_mon_init()V", jvm_classname);
        J("    invokestatic %s\n", initdesc);
    }
}

static void emit_jvm_main_close(void) {
    JC("program end");
    JI("return", "");
    J(".end method\n");
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

void jvm_emit(Program *prog, FILE *out, const char *filename) {
    jvm_out = out;
    /* Per-file reset — clear all counters so multi-file runs are independent.
     * (M-G-INV-EMIT-FIX) */
    jvm_nvar            = 0;
    jvm_pat_node_uid    = 0;
    jvm_fn_count_fwd    = 0;
    jvm_data_type_count = 0;
    jvm_named_pat_count = 0;
    /* zero array contents to kill stale pointers from prior file */
    memset(jvm_fn_table_fwd, 0, sizeof jvm_fn_table_fwd);
    memset(jvm_data_types,   0, sizeof jvm_data_types);
    jvm_named_pat_reset(); /* allocs if needed, then zeros array + count */
    jvm_need_sno_parse_helper = 0;
    jvm_need_input_helper = 0;
    jvm_need_kw_helpers      = 1;  /* always emit for J2 — callers may use &KEYWORD */
    jvm_need_indr_helpers    = 1;  /* always emit for J2 — indirect assign/get */
    jvm_need_varmap          = 1;
    jvm_need_replace_helper  = 0;
    jvm_need_lpad_helper     = 0;
    jvm_need_rpad_helper     = 0;
    jvm_need_integer_helper  = 0;
    jvm_need_datatype_helper = 0;
    jvm_need_array_helpers   = 0;
    jvm_need_data_helpers    = 0;
    jvm_need_sort_helper     = 0;
    jvm_need_convert_helper  = 0;
    jvm_need_prototype_helper = 0;
    jvm_set_classname(filename);

    jvm_cur_prog = prog;   /* make program visible to emit_jvm_goto for computed gotos */

    if (prog && prog->head) {
        jvm_collect_vars(prog);
        jvm_collect_functions(prog);
        jvm_scan_named_patterns(prog);   /* register pattern variables before emit */
    }

    JC("Generated by scrip-cc -jvm");
    JC("Assemble: java -jar jasmin.jar <file>.j -d .");
    JC("Run:      java <classname>");
    J("\n");

    emit_jvm_header(prog);
    emit_jvm_main_open();

    /* Walk statements — skip user function bodies (emitted separately as methods).
     *
     * A function body begins at the statement whose label == fn->entry_label
     * (or fn->name if entry_label is NULL) and ends just before the statement
     * whose label == fn->end_label.  We must NOT emit those statements here;
     * they are inlined into sno_userfn_NAME() by emit_jvm_fn_method().
     *
     * State machine: in_fn_body is set when we hit an entry label and cleared
     * when we hit the matching end label.  Multiple functions nest correctly
     * because SNOBOL4 function bodies never overlap (they are disjoint ranges).
     */
    int idx = 0;
    int in_fn_body = 0;   /* 1 while we are inside a function body */
    if (prog && prog->head) {
        for (STMT_t *s = prog->head; s; s = s->next, idx++) {
            if (s->is_end) {
                JSep("END");
                J("L_END:\n");
                JI("nop", "");
                break;
            }

            /* Check if this label is a function entry — start skipping */
            if (!in_fn_body && s->label) {
                for (int fi = 0; fi < jvm_fn_count; fi++) {
                    const JvmFnDef *fn = &jvm_fn_table[fi];
                    const char *entry = fn->entry_label ? fn->entry_label : fn->name;
                    if (entry && strcasecmp(s->label, entry) == 0) {
                        in_fn_body = 1;
                        break;
                    }
                }
            }

            /* Check if this label is a function end — stop skipping (emit this stmt) */
            if (in_fn_body && s->label) {
                for (int fi = 0; fi < jvm_fn_count; fi++) {
                    const JvmFnDef *fn = &jvm_fn_table[fi];
                    if (fn->end_label && strcasecmp(s->label, fn->end_label) == 0) {
                        in_fn_body = 0;
                        break;
                    }
                }
            }

            if (in_fn_body) continue;   /* skip — belongs to a function method */

            emit_jvm_stmt(s, idx);
        }
    }

    emit_jvm_main_close();

    /* Emit user-defined function methods */
    for (int i = 0; i < jvm_fn_count; i++)
        emit_jvm_fn_method(&jvm_fn_table[i], prog, i);

    /* M-LINK-JVM-1: EXPORT wrappers — public Byrd-box ABI (ARCH-scrip-abi.md §3) */
    for (int i = 0; i < jvm_fn_count; i++) {
        const JvmFnDef *fn = &jvm_fn_table[i];
        /* Find the matching ExportEntry to get verbatim method name */
        ExportEntry *matched_export = NULL;
        if (prog) for (ExportEntry *e = prog->exports; e; e = e->next)
            if (strcasecmp(e->name, fn->name) == 0) { matched_export = e; break; }
        if (!matched_export) continue;
        const char *export_name = matched_export->name;  /* verbatim from -EXPORT directive */
        J("\n; EXPORT wrapper for %s\n", export_name);
        J(".method public static %s([Ljava/lang/Object;Ljava/lang/Runnable;Ljava/lang/Runnable;)V\n", export_name);
        J("    .limit stack 16\n"); J("    .limit locals 4\n");
        for (int a = 0; a < fn->nargs; a++) {
            J("    aload_0\n"); J("    ldc %d\n", a);
            J("    aaload\n"); J("    checkcast java/lang/String\n");
        }
        char udesc[1024]; int dp = 0; udesc[dp++] = '(';
        for (int a = 0; a < fn->nargs; a++) {
            const char *s = "Ljava/lang/String;";
            for (const char *p = s; *p; p++) udesc[dp++] = *p;
        }
        udesc[dp++] = ')';
        const char *rs = "Ljava/lang/String;";
        for (const char *p = rs; *p; p++) udesc[dp++] = *p;
        udesc[dp] = '\0';
        J("    invokestatic %s/sno_userfn_%s%s\n", jvm_classname, fn->name, udesc);
        J("    astore_3\n");
        J("    aload_3\n"); J("    ifnull export_omega_%s\n", export_name);
        J("    getstatic ByrdBoxLinkage/RESULT Ljava/util/concurrent/atomic/AtomicReference;\n");
        J("    aload_3\n");
        J("    invokevirtual java/util/concurrent/atomic/AtomicReference/set(Ljava/lang/Object;)V\n");
        J("    aload_1\n"); J("    ifnull export_done_%s\n", export_name);
        J("    aload_1\n"); J("    invokeinterface java/lang/Runnable/run()V 1\n");
        J("    goto export_done_%s\n", export_name);
        J("export_omega_%s:\n", export_name);
        J("    aload_2\n"); J("    ifnull export_done_%s\n", export_name);
        J("    aload_2\n"); J("    invokeinterface java/lang/Runnable/run()V 1\n");
        J("export_done_%s:\n", export_name);
        J("    return\n"); J(".end method\n");
    }

    emit_jvm_parse_helper();
    emit_jvm_fmt_double_helper();
    emit_jvm_runtime_helpers();

    /* Free collected var names */
    for (int i = 0; i < jvm_nvar; i++) { free(jvm_vars[i]); jvm_vars[i] = NULL; }
    jvm_nvar = 0;
}
