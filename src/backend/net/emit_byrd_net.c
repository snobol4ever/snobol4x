/*
 * emit_byrd_net.c — .NET CIL (MSIL) text emitter for sno2c
 *
 * Consumes the same Program* IR as emit_byrd_asm.c and emit_byrd_jvm.c.
 * Emits Mono CIL assembler text (.il files) assembled by ilasm.
 *
 * Pipeline:
 *   sno2c -net prog.sno > prog.il
 *   ilasm prog.il /output:prog.exe
 *   mono prog.exe
 *
 * Sprint map:
 *   N-R0  M-NET-HELLO   — skeleton: null program → exit 0          ✅ session195
 *   N-R1  M-NET-LIT     — OUTPUT = 'hello' correct                 ← NOW
 *   N-R2  M-NET-ASSIGN  — variable assign + arithmetic
 *   N-R3  M-NET-GOTO    — :S/:F branching
 *   N-R4  M-NET-PATTERN — Byrd boxes in CIL: LIT/SEQ/ALT/ARBNO
 *   N-R5  M-NET-CAPTURE — . and $ capture
 *   M-NET-R1            — hello/ output/ assign/ arith/ PASS
 *   M-NET-R2            — control/ patterns/ capture/ PASS
 *   M-NET-R3            — strings/ keywords/ PASS
 *   M-NET-R4            — functions/ data/ PASS
 *   M-NET-CROSSCHECK    — 106/106 corpus PASS
 *
 * Design:
 *   Each SNOBOL4 program becomes one CIL class with a static main().
 *   Class name is derived from source filename (or "SnobolProg").
 *
 *   Three-column CIL layout mirrors the ASM and JVM backends:
 *     label:       instruction    operands
 *
 *   SNOBOL4 variables are stored as static string fields on the class.
 *   Representation: all values are strings (SNOBOL4 untyped).
 *   null/empty string is "" (not null reference) — SNOBOL4 has no null.
 *   OUTPUT = x  →  Console.WriteLine(x)
 *   :S(L)       →  brtrue  L_<name>   (last result non-empty = success)
 *   :F(L)       →  brfalse L_<name>
 *   :(L)        →  br      L_<name>
 *
 *   eval stack convention: every expr leaves one string on stack.
 *   Success/failure flag: local 0 (int32) — 1=success, 0=fail.
 *
 * References:
 *   emit_byrd_asm.c  — structural oracle (same IR, same corpus)
 *   emit_byrd_jvm.c  — JVM twin (same sprint pattern)
 *   BACKEND-NET.md   — design notes (snobol4ever/.github)
 *   Proebsting 1996  — Byrd Box four-port translation scheme
 */

#include "sno2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------
 * Column constants — three-column layout
 * ----------------------------------------------------------------------- */

#define COL_LBL   0
#define COL_INSTR 24
#define COL_OPS   40

/* -----------------------------------------------------------------------
 * Output state
 * ----------------------------------------------------------------------- */

static FILE *net_out;
static int   net_col = 0;

static void nc(char c) {
    fputc(c, net_out);
    if (c == '\n') net_col = 0;
    else if ((c & 0xC0) != 0x80) net_col++;
}

static void ns(const char *s) { for (; *s; s++) nc(*s); }

static void npad(int col) {
    if (net_col >= col) nc('\n');
    while (net_col < col) nc(' ');
}

/* N(fmt, ...) — emit raw text */
static void N(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(net_out, fmt, ap);
    va_end(ap);
    const char *p = fmt;
    while (*p) { if (*p == '\n') net_col = 0; p++; }
}

/* NL(label, instr, ops) — three-column line */
static void NL(const char *label, const char *instr, const char *ops) {
    net_col = 0;
    if (label && label[0]) { ns(label); nc(':'); }
    npad(COL_INSTR);
    ns(instr);
    if (ops && ops[0]) { npad(COL_OPS); ns(ops); }
    nc('\n');
}

/* NI(instr, ops) — instruction with no label */
static void NI(const char *instr, const char *ops) { NL("", instr, ops); }

/* NC(comment) — comment line */
static void NC(const char *comment) { N("    // %s\n", comment); }

/* NSep(tag) — section separator */
static void NSep(const char *tag) { N("\n    // --- %s ---\n", tag); }

/* -----------------------------------------------------------------------
 * Class name derivation
 * ----------------------------------------------------------------------- */

static char net_classname[256];

static void net_set_classname(const char *filename) {
    if (!filename || strcmp(filename, "<stdin>") == 0) {
        strcpy(net_classname, "SnobolProg");
        return;
    }
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    char buf[256];
    strncpy(buf, base, sizeof buf - 1); buf[sizeof buf - 1] = '\0';
    char *dot = strrchr(buf, '.'); if (dot) *dot = '\0';
    char *p = buf;
    if (!isalpha((unsigned char)*p) && *p != '_') *p = '_';
    for (; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    buf[0] = (char)toupper((unsigned char)buf[0]);
    strncpy(net_classname, buf, sizeof net_classname - 1);
    net_classname[sizeof net_classname - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * Variable registry — collect all variables for .field declarations
 * ----------------------------------------------------------------------- */

#define MAX_VARS 512
static char *net_vars[MAX_VARS];
static int   net_nvar = 0;

static void net_var_register(const char *name) {
    if (!name || !name[0]) return;
    /* case-insensitive dedup */
    for (int i = 0; i < net_nvar; i++)
        if (strcasecmp(net_vars[i], name) == 0) return;
    if (net_nvar < MAX_VARS)
        net_vars[net_nvar++] = strdup(name);
}

/* Safe field name: uppercase, replace special chars with _ */
static void net_field_name(char *out, size_t sz, const char *var) {
    size_t i = 0;
    for (; *var && i < sz - 1; var++, i++) {
        char c = (char)toupper((unsigned char)*var);
        out[i] = isalnum((unsigned char)c) ? c : '_';
    }
    out[i] = '\0';
}

static int net_is_output(const char *name) {
    return name && strcasecmp(name, "OUTPUT") == 0;
}

/* -----------------------------------------------------------------------
 * Expr scanner — collect all variable refs before emitting
 * ----------------------------------------------------------------------- */

static void scan_expr_vars(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_VART && e->sval && !net_is_output(e->sval))
        net_var_register(e->sval);
    scan_expr_vars(e->left);
    scan_expr_vars(e->right);
}

static void scan_prog_vars(Program *prog) {
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* subject variable (LHS of assignment) */
        if (s->subject && s->subject->kind == E_VART && s->subject->sval
                && !net_is_output(s->subject->sval))
            net_var_register(s->subject->sval);
        scan_expr_vars(s->subject);
        scan_expr_vars(s->pattern);
        scan_expr_vars(s->replacement);
    }
}

/* -----------------------------------------------------------------------
 * CIL string escape — escape backslash and double-quote inside ldstr
 * ----------------------------------------------------------------------- */

static void net_ldstr(const char *s) {
    /* emit:  ldstr  "<escaped>" */
    N("    ldstr      \"");
    for (; *s; s++) {
        if (*s == '"')       N("\\\"");
        else if (*s == '\\') N("\\\\");
        else                 nc(*s);
    }
    N("\"\n");
}

/* -----------------------------------------------------------------------
 * Expr emitter — leaves one string on the CIL eval stack
 * ----------------------------------------------------------------------- */

static void net_emit_expr(EXPR_t *e) {
    if (!e) {
        /* null expr → empty string */
        net_ldstr("");
        return;
    }
    switch (e->kind) {
    case E_QLIT:
        net_ldstr(e->sval ? e->sval : "");
        break;
    case E_ILIT: {
        /* integer literal → push as string directly (avoids runtime parse) */
        char buf[64];
        snprintf(buf, sizeof buf, "%ld", e->ival);
        net_ldstr(buf);
        break;
    }
    case E_FLIT: {
        /* real literal — SNOBOL4 prints 1.0 as "1.", not "1" */
        char buf[64];
        double v = e->dval;
        if (v == (double)(long)v && v >= -1e15 && v <= 1e15)
            snprintf(buf, sizeof buf, "%ld.", (long)v);
        else
            snprintf(buf, sizeof buf, "%g", v);
        net_ldstr(buf);
        break;
    }
    case E_NULV:
        net_ldstr("");
        break;
    case E_VART: {
        if (!e->sval) { net_ldstr(""); break; }
        if (net_is_output(e->sval)) {
            /* reading OUTPUT not common but handle gracefully */
            net_ldstr("");
            break;
        }
        char fn[256];
        net_field_name(fn, sizeof fn, e->sval);
        N("    ldsfld     string %s::%s\n", net_classname, fn);
        break;
    }
    case E_KW:
        /* &ALPHABET → call sno_alphabet(); others → "" stub */
        if (e->sval && strcasecmp(e->sval, "ALPHABET") == 0) {
            N("    call       string %s::sno_alphabet()\n", net_classname);
        } else {
            net_ldstr("");
        }
        break;
    case E_FNC: {
        /* Builtin functions: set local 0 (success flag), leave string on stack */
        const char *fn = e->sval ? e->sval : "";
        /* SIZE(x) — returns length string, always succeeds */
        if (strcasecmp(fn, "SIZE") == 0) {
            net_emit_expr(e->nargs >= 1 ? e->args[0] : NULL);
            N("    call       string %s::sno_size(string)\n", net_classname);
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* Numeric comparison: GT LT GE LE EQ NE — int32 helper */
        const char *cmp_helper = NULL;
        if      (strcasecmp(fn, "GT") == 0) cmp_helper = "sno_gt";
        else if (strcasecmp(fn, "LT") == 0) cmp_helper = "sno_lt";
        else if (strcasecmp(fn, "GE") == 0) cmp_helper = "sno_ge";
        else if (strcasecmp(fn, "LE") == 0) cmp_helper = "sno_le";
        else if (strcasecmp(fn, "EQ") == 0) cmp_helper = "sno_eq";
        else if (strcasecmp(fn, "NE") == 0) cmp_helper = "sno_ne";
        if (cmp_helper) {
            net_emit_expr(e->nargs >= 1 ? e->args[0] : NULL);
            net_emit_expr(e->nargs >= 2 ? e->args[1] : NULL);
            N("    call       int32 %s::%s(string, string)\n", net_classname, cmp_helper);
            N("    stloc.0\n");
            /* push right-arg value as expression result */
            net_emit_expr(e->nargs >= 2 ? e->args[1] : NULL);
            break;
        }
        /* String equality: IDENT DIFFER */
        const char *str_helper = NULL;
        if      (strcasecmp(fn, "IDENT")  == 0) str_helper = "sno_ident";
        else if (strcasecmp(fn, "DIFFER") == 0) str_helper = "sno_differ";
        if (str_helper) {
            net_emit_expr(e->nargs >= 1 ? e->args[0] : NULL);
            net_emit_expr(e->nargs >= 2 ? e->args[1] : NULL);
            N("    call       int32 %s::%s(string, string)\n", net_classname, str_helper);
            N("    stloc.0\n");
            net_ldstr("");
            break;
        }
        /* Unhandled FNC — stub */
        NC("unhandled E_FNC stub");
        net_ldstr("");
        break;
    }
    case E_CONC:
        /* string concatenation: eval both sides, String.Concat */
        net_emit_expr(e->left);
        net_emit_expr(e->right);
        N("    call       string [mscorlib]System.String::Concat(string, string)\n");
        break;
    case E_ADD:
        net_emit_expr(e->left);
        net_emit_expr(e->right);
        N("    call       string %s::sno_add(string, string)\n", net_classname);
        break;
    case E_SUB:
        net_emit_expr(e->left);
        net_emit_expr(e->right);
        N("    call       string %s::sno_sub(string, string)\n", net_classname);
        break;
    case E_MPY:
        net_emit_expr(e->left);
        net_emit_expr(e->right);
        N("    call       string %s::sno_mpy(string, string)\n", net_classname);
        break;
    case E_DIV:
        net_emit_expr(e->left);
        net_emit_expr(e->right);
        N("    call       string %s::sno_div(string, string)\n", net_classname);
        break;
    case E_MNS:
        /* unary minus */
        net_emit_expr(e->left ? e->left : e->right);
        N("    call       string %s::sno_neg(string)\n", net_classname);
        break;
    default:
        /* unhandled — push empty string stub */
        NC("unhandled expr kind — stub");
        net_ldstr("");
        break;
    }
}

/* -----------------------------------------------------------------------
 * Goto emitter
 * ----------------------------------------------------------------------- */

/* Emit a branch to target label, or fall through if NULL */
static void net_emit_goto(const char *target, const char *next_lbl) {
    if (!target) return;
    if (next_lbl && strcmp(target, next_lbl) == 0) return; /* fall-through */
    N("    br         L_%s\n", target);
}

static void net_emit_branch_success(const char *target) {
    /* local 0 = success flag (1=success,0=fail); brtrue if success */
    if (!target) return;
    N("    ldloc.0\n");
    N("    brtrue     L_%s\n", target);
}

static void net_emit_branch_fail(const char *target) {
    if (!target) return;
    N("    ldloc.0\n");
    N("    brfalse    L_%s\n", target);
}

/* -----------------------------------------------------------------------
 * Statement emitter — N-R1: assignments + OUTPUT + goto
 * ----------------------------------------------------------------------- */

static void net_emit_one_stmt(STMT_t *s, const char *next_lbl) {
    const char *tgt_s = s->go ? s->go->onsuccess : NULL;
    const char *tgt_f = s->go ? s->go->onfailure : NULL;
    const char *tgt_u = s->go ? s->go->uncond    : NULL;

    /* Emit statement label if present */
    if (s->label) {
        NSep(s->label);
        N("  L_%s:\n", s->label);
    }

    if (s->is_end) {
        NSep("END");
        net_emit_goto(tgt_u, "END");
        return;
    }

    /* Case 1: pure assignment — subject is a variable or OUTPUT, has_eq set */
    if (s->has_eq && s->subject &&
        (s->subject->kind == E_VART || s->subject->kind == E_KW)) {

        int is_out = net_is_output(s->subject->sval);
        EXPR_t *rhs = s->replacement;

        /* eval RHS onto stack */
        net_emit_expr(rhs);

        if (is_out) {
            /* OUTPUT = expr → Console.WriteLine */
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
        } else {
            /* VAR = expr → stsfld */
            char fn[256];
            net_field_name(fn, sizeof fn, s->subject->sval);
            N("    stsfld     string %s::%s\n", net_classname, fn);
        }

        /* success: set flag=1 */
        N("    ldc.i4.1\n");
        N("    stloc.0\n");

        /* goto */
        if (tgt_u) net_emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) net_emit_branch_success(tgt_s);
            if (tgt_f) net_emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 2: bare expression predicate — no has_eq, subject only, no pattern */
    if (!s->has_eq && s->subject && !s->pattern) {
        /* Evaluate subject as expression — sets local 0 (success flag) via E_FNC */
        net_emit_expr(s->subject);
        N("    pop\n");  /* discard string result — we only care about local 0 */
        if (tgt_u) {
            net_emit_goto(tgt_u, next_lbl);
        } else {
            if (tgt_s) net_emit_branch_success(tgt_s);
            if (tgt_f) net_emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 3: pattern match — subject has_eq=0, subject=var/expr, pattern present */
    if (!s->has_eq && s->subject && s->pattern) {
        /* Simple containment match using sno_litmatch for E_QLIT patterns */
        int is_qlit_pat = (s->pattern->kind == E_QLIT);
        if (is_qlit_pat) {
            /* Load subject string */
            net_emit_expr(s->subject);
            /* Load pattern literal */
            net_emit_expr(s->pattern);
            N("    call       int32 %s::sno_litmatch(string, string)\n", net_classname);
            N("    stloc.0\n");
        } else {
            /* Non-literal pattern — stub as failure */
            N("    ldc.i4.0\n");
            N("    stloc.0\n");
        }
        if (tgt_u) {
            net_emit_goto(tgt_u, next_lbl);
        } else {
            if (tgt_s) net_emit_branch_success(tgt_s);
            if (tgt_f) net_emit_branch_fail(tgt_f);
        }
        return;
    }

    /* No-op / unhandled statement — emit nop + goto only */
    N("    nop\n");
    if (tgt_u) net_emit_goto(tgt_u, next_lbl);
}

static void net_emit_stmts(Program *prog) {
    if (!prog || !prog->head) return;

    STMT_t *stmts[4096];
    int n = 0;
    for (STMT_t *s = prog->head; s && n < 4095; s = s->next)
        stmts[n++] = s;

    for (int i = 0; i < n; i++) {
        STMT_t *s = stmts[i];
        /* compute next_lbl for fall-through optimisation */
        const char *next_lbl = NULL;
        if (i + 1 < n && stmts[i+1]->label)
            next_lbl = stmts[i+1]->label;
        else if (i + 1 < n && stmts[i+1]->is_end)
            next_lbl = "END";

        if (s->is_end) {
            NSep("END statement");
            break;
        }
        net_emit_one_stmt(s, next_lbl);
    }
}

/* -----------------------------------------------------------------------
 * SNOBOL4 arithmetic helpers emitted into each class
 *
 * SNOBOL4 numeric coercion rules:
 *   - empty string coerces to 0
 *   - strings that look like integers → integer arithmetic, result is integer string
 *   - strings that look like reals → real arithmetic, result printed as SNOBOL4 real
 *   - SNOBOL4 real format: integer-valued reals print with trailing dot (e.g. "1.")
 *
 * We implement each op as a static CIL method on the class using try/catch
 * to handle parse failures (fall back to 0).
 * We use float64 (double) arithmetic throughout for generality; then format
 * the result as integer-string if result is whole, or with "." suffix if
 * it was already integer-valued in input.
 * ----------------------------------------------------------------------- */

/* Emit a single CIL helper: sno_parse_dbl — parses a string to float64,
 * empty/unparseable → 0.0 */
static void net_emit_helper_parse(void) {
    N("  .method private static float64 sno_parse_dbl(string s) cil managed\n");
    N("  {\n");
    N("    .maxstack 2\n");
    N("    .locals init (float64 V_0, bool V_1)\n");
    N("    ldarg.0\n");
    N("    ldloca.s V_0\n");
    N("    call       bool [mscorlib]System.Double::TryParse(string, float64&)\n");
    N("    pop\n");
    N("    ldloc.0\n");
    N("    ret\n");
    N("  }\n\n");
}

/* Emit sno_fmt_num — formats a float64 as SNOBOL4 numeric string.
 * integer-valued → no decimal (e.g. 3 → "3"), except when we want "1." for
 * real context.  Here we mirror the SNOBOL4 convention: integer arithmetic
 * with integer inputs → no dot; any real input → dot notation. */
static void net_emit_helper_fmt(void) {
    /* sno_fmt_int: just call Int32 conversion for integer results */
    N("  .method private static string sno_fmt_dbl(float64 v) cil managed\n");
    N("  {\n");
    N("    .maxstack 3\n");
    N("    .locals init (float64 V_0, int64 V_1)\n");
    N("    ldarg.0\n");
    N("    stloc.0\n");
    /* check if v == floor(v) */
    N("    ldloc.0\n");
    N("    conv.i8\n");
    N("    stloc.1\n");
    N("    ldloc.0\n");
    N("    ldloc.1\n");
    N("    conv.r8\n");
    N("    beq        IS_INT\n");
    /* real — use G format */
    N("    ldloc.0\n");
    N("    box        [mscorlib]System.Double\n");
    N("    callvirt   instance string object::ToString()\n");
    N("    ret\n");
    N("  IS_INT:\n");
    N("    ldloc.1\n");
    N("    box        [mscorlib]System.Int64\n");
    N("    callvirt   instance string object::ToString()\n");
    N("    ret\n");
    N("  }\n\n");
}

/* sno_add(a,b): SNOBOL4 add — numeric if both can be coerced (empty→0), else concat.
 * SNOBOL4 rule: if EITHER operand is non-numeric non-empty → concat.
 * Empty string is numeric (= 0). */
static void net_emit_helper_add(void) {
    N("  .method private static string sno_add(string a, string b) cil managed\n");
    N("  {\n");
    N("    .maxstack 4\n");
    N("    .locals init (float64 V_0, float64 V_1, bool V_2, bool V_3, string V_4, string V_5)\n");
    /* Trim a and b — empty/whitespace coerce to "0" for numeric test */
    N("    ldarg.0\n");
    N("    callvirt   instance string [mscorlib]System.String::Trim()\n");
    N("    stloc.s V_4\n");
    N("    ldarg.1\n");
    N("    callvirt   instance string [mscorlib]System.String::Trim()\n");
    N("    stloc.s V_5\n");
    /* if trimmed a == "" → treat as "0" for parse attempt */
    N("    ldloc.s V_4\n");
    N("    ldstr      \"\"\n");
    N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
    N("    brfalse    SNO_ADD_PARSE_A\n");
    N("    ldstr      \"0\"\n");
    N("    stloc.s V_4\n");
    N("  SNO_ADD_PARSE_A:\n");
    N("    ldloc.s V_4\n");
    N("    ldloca.s V_0\n");
    N("    call       bool [mscorlib]System.Double::TryParse(string, float64&)\n");
    N("    stloc.2\n");
    /* if trimmed b == "" → treat as "0" */
    N("    ldloc.s V_5\n");
    N("    ldstr      \"\"\n");
    N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
    N("    brfalse    SNO_ADD_PARSE_B\n");
    N("    ldstr      \"0\"\n");
    N("    stloc.s V_5\n");
    N("  SNO_ADD_PARSE_B:\n");
    N("    ldloc.s V_5\n");
    N("    ldloca.s V_1\n");
    N("    call       bool [mscorlib]System.Double::TryParse(string, float64&)\n");
    N("    stloc.3\n");
    /* both numeric? → add */
    N("    ldloc.2\n");
    N("    ldloc.3\n");
    N("    and\n");
    N("    brfalse    SNO_ADD_CONCAT\n");
    N("    ldloc.0\n");
    N("    ldloc.1\n");
    N("    add\n");
    N("    call       string %s::sno_fmt_dbl(float64)\n", net_classname);
    N("    ret\n");
    N("  SNO_ADD_CONCAT:\n");
    N("    ldarg.0\n");
    N("    ldarg.1\n");
    N("    call       string [mscorlib]System.String::Concat(string, string)\n");
    N("    ret\n");
    N("  }\n\n");
}

/* sno_sub, sno_mpy, sno_div — numeric only (no concat fallback for these) */
static void net_emit_helper_binop(const char *name, const char *op) {
    N("  .method private static string %s(string a, string b) cil managed\n", name);
    N("  {\n");
    N("    .maxstack 3\n");
    N("    .locals init (float64 V_0, float64 V_1)\n");
    N("    ldarg.0\n");
    N("    call       float64 %s::sno_parse_dbl(string)\n", net_classname);
    N("    stloc.0\n");
    N("    ldarg.1\n");
    N("    call       float64 %s::sno_parse_dbl(string)\n", net_classname);
    N("    stloc.1\n");
    N("    ldloc.0\n");
    N("    ldloc.1\n");
    N("    %s\n", op);
    N("    call       string %s::sno_fmt_dbl(float64)\n", net_classname);
    N("    ret\n");
    N("  }\n\n");
}

static void net_emit_helper_neg(void) {
    N("  .method private static string sno_neg(string a) cil managed\n");
    N("  {\n");
    N("    .maxstack 2\n");
    N("    ldarg.0\n");
    N("    call       float64 %s::sno_parse_dbl(string)\n", net_classname);
    N("    neg\n");
    N("    call       string %s::sno_fmt_dbl(float64)\n", net_classname);
    N("    ret\n");
    N("  }\n\n");
}

/* sno_gt(a,b): numeric GT — returns b (right operand) on success, "" on fail.
 * Sets local 0 success flag inside CIL caller via convention:
 * helper returns non-empty string on success, "" on fail.
 * Caller sets local 0 from ldloc analysis... simpler: we use a two-value return
 * convention where the helper itself takes/sets no flags — instead we check the
 * returned string: if it is "" that could be ambiguous. Better: return "1" on
 * success and use brfalse/brtrue on int returned.
 * 
 * CIL design: sno_cmp(a,b,op_int) → returns int32: 1=success,0=fail.
 * The stmt emitter pops the int, stores to local 0. Avoids ambiguous empty string.
 */
static void net_emit_helper_cmp(const char *name, const char *cil_brop) {
    /* name: "sno_gt", cil_brop: "bgt" etc. Returns int32 1 or 0. */
    N("  .method private static int32 %s(string a, string b) cil managed\n", name);
    N("  {\n");
    N("    .maxstack 4\n");
    N("    .locals init (float64 V_0, float64 V_1)\n");
    N("    ldarg.0\n");
    N("    call       float64 %s::sno_parse_dbl(string)\n", net_classname);
    N("    stloc.0\n");
    N("    ldarg.1\n");
    N("    call       float64 %s::sno_parse_dbl(string)\n", net_classname);
    N("    stloc.1\n");
    N("    ldloc.0\n");
    N("    ldloc.1\n");
    N("    %s         CMP_TRUE\n", cil_brop);
    N("    ldc.i4.0\n");
    N("    ret\n");
    N("  CMP_TRUE:\n");
    N("    ldc.i4.1\n");
    N("    ret\n");
    N("  }\n\n");
}

/* sno_ident(a,b): string equality — 1 if identical, 0 if different */
static void net_emit_helper_ident(void) {
    N("  .method private static int32 sno_ident(string a, string b) cil managed\n");
    N("  {\n");
    N("    .maxstack 2\n");
    N("    ldarg.0\n");
    N("    ldarg.1\n");
    N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
    N("    ret\n");  /* bool is int32 on CLI */
    N("  }\n\n");
}

/* sno_differ(a,b): 1 if NOT identical */
static void net_emit_helper_differ(void) {
    N("  .method private static int32 sno_differ(string a, string b) cil managed\n");
    N("  {\n");
    N("    .maxstack 2\n");
    N("    ldarg.0\n");
    N("    ldarg.1\n");
    N("    call       bool [mscorlib]System.String::op_Inequality(string, string)\n");
    N("    ret\n");
    N("  }\n\n");
}

/* sno_size(a): returns length as integer string, always succeeds (sets flag=1) */
static void net_emit_helper_size(void) {
    N("  .method private static string sno_size(string a) cil managed\n");
    N("  {\n");
    N("    .maxstack 2\n");
    N("    ldarg.0\n");
    N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
    N("    box        [mscorlib]System.Int32\n");
    N("    callvirt   instance string object::ToString()\n");
    N("    ret\n");
    N("  }\n\n");
}

/* sno_litmatch(subj,pat): 1 if subj contains pat, 0 otherwise */
static void net_emit_helper_litmatch(void) {
    N("  .method private static int32 sno_litmatch(string subj, string pat) cil managed\n");
    N("  {\n");
    N("    .maxstack 2\n");
    N("    ldarg.0\n");
    N("    ldarg.1\n");
    N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
    N("    ret\n");
    N("  }\n\n");
}

/* &ALPHABET — 256-char string of all bytes 0..255 */
static void net_emit_helper_alphabet(void) {
    N("  .method private static string sno_alphabet() cil managed\n");
    N("  {\n");
    N("    .maxstack 3\n");
    N("    .locals init (char[] V_0, int32 V_1)\n");
    N("    ldc.i4     256\n");
    N("    newarr     [mscorlib]System.Char\n");
    N("    stloc.0\n");
    N("    ldc.i4.0\n");
    N("    stloc.1\n");
    N("  ALPHA_LOOP:\n");
    N("    ldloc.1\n");
    N("    ldc.i4     256\n");
    N("    bge        ALPHA_DONE\n");
    N("    ldloc.0\n");
    N("    ldloc.1\n");
    N("    ldloc.1\n");
    N("    conv.u2\n");
    N("    stelem.i2\n");
    N("    ldloc.1\n");
    N("    ldc.i4.1\n");
    N("    add\n");
    N("    stloc.1\n");
    N("    br         ALPHA_LOOP\n");
    N("  ALPHA_DONE:\n");
    N("    ldloc.0\n");
    N("    newobj     instance void [mscorlib]System.String::.ctor(char[])\n");
    N("    ret\n");
    N("  }\n\n");
}

static void net_emit_sno_helpers(void) {
    net_emit_helper_parse();
    net_emit_helper_fmt();
    net_emit_helper_add();
    net_emit_helper_binop("sno_sub", "sub");
    net_emit_helper_binop("sno_mpy", "mul");
    net_emit_helper_binop("sno_div", "div");
    net_emit_helper_neg();
    /* comparison helpers — return int32 1/0 */
    net_emit_helper_cmp("sno_gt",  "bgt");
    net_emit_helper_cmp("sno_lt",  "blt");
    net_emit_helper_cmp("sno_ge",  "bge");
    net_emit_helper_cmp("sno_le",  "ble");
    net_emit_helper_cmp("sno_eq",  "beq");
    net_emit_helper_cmp("sno_ne",  "bne.un");
    net_emit_helper_ident();
    net_emit_helper_differ();
    net_emit_helper_size();
    net_emit_helper_litmatch();
    net_emit_helper_alphabet();
}

/* -----------------------------------------------------------------------
 * Header / footer emitters
 * ----------------------------------------------------------------------- */

static void net_emit_header(Program *prog) {
    N(".assembly extern mscorlib {}\n");
    N(".assembly %s {}\n", net_classname);
    N(".module %s.exe\n", net_classname);
    N("\n");
    N(".class public auto ansi beforefieldinit %s\n", net_classname);
    N("       extends [mscorlib]System.Object\n");
    N("{\n");
    N("\n");

    /* Emit one static string field per SNOBOL4 variable */
    if (net_nvar > 0) {
        NC("SNOBOL4 variable fields");
        for (int i = 0; i < net_nvar; i++) {
            char fn[256];
            net_field_name(fn, sizeof fn, net_vars[i]);
            N("  .field static string %s\n", fn);
        }
        N("\n");
    }

    /* Static initialiser: set all variables to "" */
    if (net_nvar > 0) {
        N("  .method static void .cctor() cil managed\n");
        N("  {\n");
        N("    .maxstack 1\n");
        for (int i = 0; i < net_nvar; i++) {
            char fn[256];
            net_field_name(fn, sizeof fn, net_vars[i]);
            N("    ldstr      \"\"\n");
            N("    stsfld     string %s::%s\n", net_classname, fn);
        }
        N("    ret\n");
        N("  }\n");
        N("\n");
    }

    (void)prog; /* suppress unused warning; prog used by scan_prog_vars above */

    /* ---- SNOBOL4 runtime arithmetic helpers ---- */
    net_emit_sno_helpers();
}

static void net_emit_main_open(void) {
    N("  .method public static void main(string[] args) cil managed\n");
    N("  {\n");
    N("    .entrypoint\n");
    N("    .maxstack 8\n");
    N("    .locals init (int32 V_0)  // V_0 = success flag\n");
    N("\n");
}

static void net_emit_main_close(void) {
    NSep("END");
    N("  L_END:\n");
    NI("nop", "");
    NI("ret", "");
    N("  }\n");
    N("\n");
}

static void net_emit_footer(void) {
    N("} // end class %s\n", net_classname);
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

void net_emit(Program *prog, FILE *out, const char *filename) {
    net_out = out;
    net_nvar = 0;
    net_set_classname(filename);

    /* Two-pass: scan variables first, then emit */
    scan_prog_vars(prog);

    NC("Generated by sno2c -net");
    NC("Assemble: ilasm <file>.il /output:<file>.exe");
    NC("Run:      mono <file>.exe");
    N("\n");

    net_emit_header(prog);
    net_emit_main_open();
    net_emit_stmts(prog);
    net_emit_main_close();
    net_emit_footer();

    /* free registered variable names */
    for (int i = 0; i < net_nvar; i++) { free(net_vars[i]); net_vars[i] = NULL; }
    net_nvar = 0;
}
