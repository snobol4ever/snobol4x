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
 *   N-R1  M-NET-LIT     — OUTPUT = 'hello' correct                 ✅ N-197
 *   N-R2  M-NET-ASSIGN  — variable assign + arithmetic             ✅ N-199
 *   N-R3  M-NET-GOTO    — :S/:F branching                         ✅ N-199
 *   N-R4  M-NET-PATTERN — Byrd boxes in CIL: LIT/SEQ/ALT/ARBNO   ✅ N-200
 *   N-R5  M-NET-CAPTURE — . and $ capture                         <- NOW (N-201)
 *   M-NET-R1            — hello/ output/ assign/ arith/ PASS
 *   M-NET-R2            — control/ patterns/ capture/ PASS
 *   M-NET-R3            — strings/ keywords/ PASS
 *   M-NET-R4            — functions/ data/ PASS
 *   M-NET-CROSSCHECK    — 106/106 corpus PASS
 *
 * Runtime DLL architecture (N-201):
 *   snobol4lib.dll  [snobol4lib]Snobol4Lib -- all sno_* helpers (no longer inlined)
 *   snobol4run.dll  [snobol4run]Snobol4Run -- keyword state, I/O
 *   Source: src/runtime/net/snobol4lib.il + snobol4run.il
 *   Compile once; every .il references extern assemblies -- much faster ilasm per prog.
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

#define EMIT_BYRD_NET_C
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

/* Function support (DEFINE/RETURN/FRETURN) */
#define FN_MAX      32
#define NAME_LEN  128
#define FN_ARGMAX   16
typedef struct {
    char name[NAME_LEN];
    char args[FN_ARGMAX][NAME_LEN];
    int  nargs;
    char locals[FN_ARGMAX][NAME_LEN];
    int  nlocals;
    char entry_label[NAME_LEN];
    char end_label[NAME_LEN];
} FnDef;
static FnDef  fn_table[FN_MAX];
static int       fn_count = 0;
static const FnDef *cur_fn = NULL;
static char fn_return_lbl[128];
static char fn_freturn_lbl[128];
static const FnDef *find_fn(const char *name);
static int uid_ctr = 0;
static int next_uid(void) { return uid_ctr++; }

static FILE *out;
static int   col = 0;

static void nc(char c) {
    fputc(c, out);
    if (c == '\n') col = 0;
    else if ((c & 0xC0) != 0x80) col++;
}

static void ns(const char *s) { for (; *s; s++) nc(*s); }

static void npad(int col) {
    if (col >= col) nc('\n');
    while (col < col) nc(' ');
}

/* N(fmt, ...) — emit raw text */
static void N(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    const char *p = fmt;
    while (*p) { if (*p == '\n') col = 0; p++; }
}

/* NL(label, instr, ops) — three-column line */
static void NL(const char *label, const char *instr, const char *ops) {
    col = 0;
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

static char classname[256];

static void set_classname(const char *filename) {
    if (!filename || strcmp(filename, "<stdin>") == 0) {
        strcpy(classname, "SnobolProg");
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
    strncpy(classname, buf, sizeof classname - 1);
    classname[sizeof classname - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * Variable registry — collect all variables for .field declarations
 * ----------------------------------------------------------------------- */

#define MAX_VARS 512
static char *vars[MAX_VARS];
static int   nvar = 0;

static void var_register(const char *name) {
    if (!name || !name[0]) return;
    /* case-insensitive dedup */
    for (int i = 0; i < nvar; i++)
        if (strcasecmp(vars[i], name) == 0) return;
    if (nvar < MAX_VARS)
        vars[nvar++] = strdup(name);
}

/* -----------------------------------------------------------------------
 * Named pattern registry — VAR = <pattern-expr> assignments.
 * When E_VART appears in pattern context, inline-expand stored pattern tree.
 * ----------------------------------------------------------------------- */
#define NAMED_PAT_MAX 64
typedef struct { char varname[128]; EXPR_t *pat; } NamedPat;
static NamedPat named_pats[NAMED_PAT_MAX];
static int         named_pat_count = 0;

static void named_pat_reset(void) { named_pat_count = 0; }

static void named_pat_register(const char *varname, EXPR_t *pat) {
    for (int i = 0; i < named_pat_count; i++) {
        if (strcasecmp(named_pats[i].varname, varname) == 0) {
            if (pat) named_pats[i].pat = pat;
            return;
        }
    }
    if (named_pat_count >= NAMED_PAT_MAX) return;
    NamedPat *e = &named_pats[named_pat_count++];
    snprintf(e->varname, sizeof e->varname, "%s", varname);
    e->pat = pat;
}

static const NamedPat *named_pat_lookup(const char *varname) {
    for (int i = 0; i < named_pat_count; i++)
        if (strcasecmp(named_pats[i].varname, varname) == 0)
            return &named_pats[i];
    return NULL;
}

static int expr_has_pat_fn(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC || e->kind == E_NAM || e->kind == E_DOL) return 1;
    for (int i = 0; i < expr_nargs(e); i++)
        if (expr_has_pat_fn(expr_arg(e, i))) return 1;
    return 0;
}

static int expr_is_pattern_expr(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_OR)   return 1;
    if (e->kind == E_CONC) return expr_has_pat_fn(e);
    return expr_has_pat_fn(e);
}

static void scan_named_patterns(Program *prog) {
    named_pat_reset();
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->subject && s->subject->kind == E_VART && s->subject->sval &&
            s->has_eq && s->replacement && !s->pattern) {
            if (expr_is_pattern_expr(s->replacement))
                named_pat_register(s->subject->sval, s->replacement);
        }
    }
}

/* Safe field name: uppercase, replace special chars with _ */
static void field_name(char *out, size_t sz, const char *var) {
    size_t i = 0;
    for (; *var && i < sz - 1; var++, i++) {
        char c = (char)toupper((unsigned char)*var);
        out[i] = isalnum((unsigned char)c) ? c : '_';
    }
    out[i] = '\0';
}

static int is_output(const char *name) {
    return name && strcasecmp(name, "OUTPUT") == 0;
}

static int is_input(const char *name) {
    return name && strcasecmp(name, "INPUT") == 0;
}

/* Current statement fail label — set by stmt emitter so emit_expr
 * can emit an inline null-check when it encounters INPUT (EOF → fail). */
static char cur_stmt_fail_label[128];
static int  input_uid = 0;

/* -----------------------------------------------------------------------
 * Expr scanner — collect all variable refs before emitting
 * ----------------------------------------------------------------------- */

static void collect_vars_expr(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_VART && e->sval && !is_output(e->sval)
            && !is_input(e->sval))
        var_register(e->sval);
    /* E_NAM (. capture) and E_DOL ($ capture): sval = target variable name */
    if ((e->kind == E_NAM || e->kind == E_DOL) && e->sval
            && !is_output(e->sval) && !is_input(e->sval))
        var_register(e->sval);
    for (int i = 0; i < expr_nargs(e); i++)
        collect_vars_expr(expr_arg(e, i));
}

static void collect_vars(Program *prog) {
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* subject variable (LHS of assignment) */
        if (s->subject && s->subject->kind == E_VART && s->subject->sval
                && !is_output(s->subject->sval)
                && !is_input(s->subject->sval))
            var_register(s->subject->sval);
        collect_vars_expr(s->subject);
        collect_vars_expr(s->pattern);
        collect_vars_expr(s->replacement);
    }
}

/* -----------------------------------------------------------------------
 * CIL string escape — escape backslash and double-quote inside ldstr
 * ----------------------------------------------------------------------- */

static void ldstr(const char *s) {
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

static void emit_expr(EXPR_t *e) {
    if (!e) {
        /* null expr → empty string */
        ldstr("");
        return;
    }
    switch (e->kind) {
    case E_QLIT:
        ldstr(e->sval ? e->sval : "");
        break;
    case E_ILIT: {
        /* integer literal → push as string directly (avoids runtime parse) */
        char buf[64];
        snprintf(buf, sizeof buf, "%ld", e->ival);
        ldstr(buf);
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
        ldstr(buf);
        break;
    }
    case E_INDR: {
        /* $expr — indirect variable read: eval name, call net_indr_get */
        emit_expr(expr_arg(e, 0));
        N("    call       string %s::net_indr_get(string)\n", classname);
        break;
    }
    case E_NULV:
        ldstr("");
        break;
    case E_VART: {
        if (!e->sval) { ldstr(""); break; }
        if (is_output(e->sval)) {
            /* reading OUTPUT not common but handle gracefully */
            ldstr("");
            break;
        }
        if (is_input(e->sval)) {
            /* INPUT — call sno_input(); null → EOF → statement fails.
             * Stack convention: on success, leaves string on stack.
             * On EOF, branches directly to fail label (or pops + pushes ""
             * if no fail label available), keeping stack depth consistent. */
            int uid = input_uid++;
            char inp_ok[64];
            snprintf(inp_ok, sizeof inp_ok, "Ninp%d_ok", uid);
            N("    call       string [snobol4run]Snobol4Run::sno_input()\n");
            N("    dup\n");
            N("    brtrue     %s\n", inp_ok);
            /* null = EOF path: pop null off stack, set fail flag */
            N("    pop\n");
            N("    ldc.i4.0\n");
            N("    stloc.0\n");
            if (cur_stmt_fail_label[0]) {
                /* Branch to fail — stack depth is 0 here (null was popped),
                 * fail label entry must also expect depth 0 (it's a stmt top) */
                N("    br         L_%s\n", cur_stmt_fail_label);
            }
            /* If no fail label: fall through with stack empty — stsfld below
             * would underflow, but that only happens with no :F and EOF, which
             * means program would have no way to handle it anyway. */
            /* Pad stack to match success path depth (1) so verifier is happy */
            N("    ldstr      \"\"\n");
            N("  %s:\n", inp_ok);
            /* Success path falls here: string (non-null) already on stack.
             * EOF path falls here with "" — stsfld stores it but flag=0 means
             * subsequent :F branch will exit. */
            break;
        }
        char fn[256];
        field_name(fn, sizeof fn, e->sval);
        N("    ldsfld     string %s::%s\n", classname, fn);
        break;
    }
    case E_KW:
        /* &ALPHABET → call sno_alphabet(); &STNO → statement counter; others → "" stub */
        if (e->sval && strcasecmp(e->sval, "ALPHABET") == 0) {
            N("    call       string [snobol4lib]Snobol4Lib::sno_alphabet()\n");
        } else if (e->sval && strcasecmp(e->sval, "STNO") == 0) {
            N("    ldsfld     string %s::kw_stno\n", classname);
        } else if (e->sval && strcasecmp(e->sval, "UCASE") == 0) {
            ldstr("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        } else if (e->sval && strcasecmp(e->sval, "LCASE") == 0) {
            ldstr("abcdefghijklmnopqrstuvwxyz");
        } else {
            ldstr("");
        }
        break;
    case E_FNC: {
        /* Builtin functions: set local 0 (success flag), leave string on stack */
        const char *fn = e->sval ? e->sval : "";
        /* SIZE(x) — returns length string, always succeeds */
        if (strcasecmp(fn, "SIZE") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    call       string [snobol4lib]Snobol4Lib::sno_size(string)\n");
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
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            N("    call       int32 [snobol4lib]Snobol4Lib::%s(string, string)\n", cmp_helper);
            N("    stloc.0\n");
            /* push right-arg value as expression result */
            emit_expr(expr_arg(e, 1));
            break;
        }
        /* String equality: IDENT DIFFER */
        const char *str_helper = NULL;
        if      (strcasecmp(fn, "IDENT")  == 0) str_helper = "sno_ident";
        else if (strcasecmp(fn, "DIFFER") == 0) str_helper = "sno_differ";
        if (str_helper) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            N("    call       int32 [snobol4lib]Snobol4Lib::%s(string, string)\n", str_helper);
            N("    stloc.0\n");
            ldstr("");
            break;
        }
        /* DATATYPE(x) — returns "string", "integer", or "real" */
        if (strcasecmp(fn, "DATATYPE") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    call       string [snobol4lib]Snobol4Lib::sno_datatype(string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* Lexical string comparators: LGT LLT LGE LLE LEQ LNE */
        const char *lcmp_helper = NULL;
        if      (strcasecmp(fn, "LGT") == 0) lcmp_helper = "sno_lgt";
        else if (strcasecmp(fn, "LLT") == 0) lcmp_helper = "sno_llt";
        else if (strcasecmp(fn, "LGE") == 0) lcmp_helper = "sno_lge";
        else if (strcasecmp(fn, "LLE") == 0) lcmp_helper = "sno_lle";
        else if (strcasecmp(fn, "LEQ") == 0) lcmp_helper = "sno_leq";
        else if (strcasecmp(fn, "LNE") == 0) lcmp_helper = "sno_lne";
        if (lcmp_helper) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            N("    call       int32 [snobol4lib]Snobol4Lib::%s(string, string)\n", lcmp_helper);
            N("    stloc.0\n");
            ldstr("");
            break;
        }
        /* SUBSTR(str, start [, len]) — 1-based substring */
        if (strcasecmp(fn, "SUBSTR") == 0) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            if (expr_arg(e, 2))
                emit_expr(expr_arg(e, 2));
            else
                ldstr("-1");
            N("    call       string [snobol4lib]Snobol4Lib::sno_substr(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* REPLACE(str, from, to) — char-by-char translation */
        if (strcasecmp(fn, "REPLACE") == 0) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            emit_expr(expr_arg(e, 2));
            N("    call       string [snobol4lib]Snobol4Lib::sno_replace(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* DUPL(str, n) — repeat string n times */
        if (strcasecmp(fn, "DUPL") == 0) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            N("    call       string [snobol4lib]Snobol4Lib::sno_dupl(string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* TRIM(str) — remove trailing whitespace */
        if (strcasecmp(fn, "TRIM") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    call       string [snobol4lib]Snobol4Lib::sno_trim(string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* REVERSE(str) — reverse a string */
        if (strcasecmp(fn, "REVERSE") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    call       string [snobol4lib]Snobol4Lib::sno_reverse(string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* UCASE(str) — uppercase */
        if (strcasecmp(fn, "UCASE") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    callvirt   instance string [mscorlib]System.String::ToUpper()\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* LCASE(str) — lowercase */
        if (strcasecmp(fn, "LCASE") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    callvirt   instance string [mscorlib]System.String::ToLower()\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* LPAD(str, n [, pad]) — left-pad to width n */
        if (strcasecmp(fn, "LPAD") == 0) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            if (expr_arg(e, 2)) emit_expr(expr_arg(e, 2));
            else ldstr(" ");
            N("    call       string [snobol4lib]Snobol4Lib::sno_lpad(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* RPAD(str, n [, pad]) — right-pad to width n */
        if (strcasecmp(fn, "RPAD") == 0) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            if (expr_arg(e, 2)) emit_expr(expr_arg(e, 2));
            else ldstr(" ");
            N("    call       string [snobol4lib]Snobol4Lib::sno_rpad(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* INTEGER(x) — succeeds if x is integer string */
        if (strcasecmp(fn, "INTEGER") == 0) {
            emit_expr(expr_arg(e, 0));
            N("    call       int32 [snobol4lib]Snobol4Lib::sno_is_integer(string)\n");
            N("    stloc.0\n");
            /* push arg back as result value */
            emit_expr(expr_arg(e, 0));
            break;
        }
        /* REMDR(a, b) — integer remainder */
        if (strcasecmp(fn, "REMDR") == 0) {
            emit_expr(expr_arg(e, 0));
            emit_expr(expr_arg(e, 1));
            N("    call       string [snobol4lib]Snobol4Lib::sno_remdr(string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* User-defined function call */
        {
            const FnDef *ufn = find_fn(fn);
            if (ufn) {
                int na = expr_nargs(e);
                for (int i = 0; i < ufn->nargs && i < na; i++)
                    emit_expr(expr_arg(e, i));
                /* pad missing args with "" */
                for (int i = na; i < ufn->nargs; i++) ldstr("");
                N("    call       string %s::net_fn_%s(", classname, ufn->name);
                for (int i = 0; i < ufn->nargs; i++) { if (i>0) N(", "); N("string"); }
                N(")\n");
                /* null return = FRETURN = failure */
                N("    dup\n");
                N("    ldnull\n");
                N("    ceq\n");
                N("    ldc.i4.1\n");
                N("    xor\n");
                N("    stloc.0\n");
                /* if null, replace with "" so stack is always a string */
                N("    dup\n");
                N("    brtrue     Nfc_%d_ok\n", uid_ctr);
                N("    pop\n");
                N("    ldstr      \"\"\n");
                N("  Nfc_%d_ok:\n", next_uid());
                break;
            }
        }
        /* Unhandled FNC — stub */
        NC("unhandled E_FNC stub");
        ldstr("");
        break;
    }
    case E_CONC:
        /* string concatenation: eval all arms, fold with String.Concat */
        if (expr_nargs(e) > 0) {
            emit_expr(expr_arg(e, 0));
            for (int i = 1; i < expr_nargs(e); i++) {
                emit_expr(expr_arg(e, i));
                N("    call       string [mscorlib]System.String::Concat(string, string)\n");
            }
        } else {
            ldstr("");
        }
        break;
    case E_OR:
        /* Pattern alternation used as an expression value (e.g. P = 'a' | 'b').
         * The string value is a placeholder — pattern matching uses the structural
         * node tree, not this string.  Push non-empty sentinel so assignment succeeds. */
        ldstr("*pat*");
        break;
    case E_ADD:
        emit_expr(expr_arg(e, 0));
        emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_add(string, string)\n");
        break;
    case E_SUB:
        emit_expr(expr_arg(e, 0));
        emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_sub(string, string)\n");
        break;
    case E_MPY:
        emit_expr(expr_arg(e, 0));
        emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_mpy(string, string)\n");
        break;
    case E_DIV:
        emit_expr(expr_arg(e, 0));
        emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_div(string, string)\n");
        break;
    case E_MNS:
        /* unary minus */
        emit_expr(expr_arg(e, 0));
        N("    call       string [snobol4lib]Snobol4Lib::sno_neg(string)\n");
        break;
    default:
        /* unhandled — push empty string stub */
        NC("unhandled expr kind — stub");
        ldstr("");
        break;
    }
}

/* -----------------------------------------------------------------------
 * Goto emitter
 * ----------------------------------------------------------------------- */

/* Emit a branch to target label, or fall through if NULL */
static void emit_goto(const char *target, const char *next_lbl) {
    if (!target) return;
    /* RETURN/FRETURN inside a function body → branch to function return labels */
    if (cur_fn) {
        if (strcasecmp(target, "RETURN") == 0) {
            N("    br         %s\n", fn_return_lbl);
            return;
        }
        if (strcasecmp(target, "FRETURN") == 0) {
            N("    br         %s\n", fn_freturn_lbl);
            return;
        }
    }
    if (next_lbl && strcmp(target, next_lbl) == 0) return; /* fall-through */
    N("    br         L_%s\n", target);
}

static void emit_branch_success(const char *target) {
    /* local 0 = success flag (1=success,0=fail); brtrue if success */
    if (!target) return;
    N("    ldloc.0\n");
    N("    brtrue     L_%s\n", target);
}

static void emit_branch_fail(const char *target) {
    if (!target) return;
    N("    ldloc.0\n");
    N("    brfalse    L_%s\n", target);
}

/* -----------------------------------------------------------------------
 * Pattern emitter — N-R3: Byrd box patterns in CIL
 *
 * Two separate local-slot pools to satisfy CIL type safety:
 *   Int32 slots: V_6 .. V_19   (14 slots) — cursor saves, counters, lengths
 *   String slots: V_20 .. V_29 (10 slots) — charset strings, char strings, lit strings
 *
 * Fixed pattern locals:
 *   V_2 = pat_subj (string)
 *   V_3 = pat_cursor (int32)
 *   V_4 = pat_len (int32)
 *   V_5 = pat_mstart (int32)
 *
 * cap_slot: int32 slot counter, starts at 6
 * str_slot: string slot counter, starts at 20
 * ----------------------------------------------------------------------- */

/* uid_ctr / next_uid() defined above */
static char pat_abort_label[128];
/* ARB backtrack label — set by ARB emitter, used by SEQ to wire continuation omega */
static char arb_incr_label[128];

/* Int32 slot load/store */
static void ldloc_i(int idx) {
    if      (idx == 0) N("    ldloc.0\n");
    else if (idx == 1) N("    ldloc.1\n");
    else if (idx == 2) N("    ldloc.2\n");
    else if (idx == 3) N("    ldloc.3\n");
    else               N("    ldloc.s    V_%d\n", idx);
}
static void stloc_i(int idx) {
    if      (idx == 0) N("    stloc.0\n");
    else if (idx == 1) N("    stloc.1\n");
    else if (idx == 2) N("    stloc.2\n");
    else if (idx == 3) N("    stloc.3\n");
    else               N("    stloc.s    V_%d\n", idx);
}
/* String slot load/store */
static void ldloc_s(int idx) { N("    ldloc.s    V_%d\n", idx); }
static void stloc_s(int idx) { N("    stloc.s    V_%d\n", idx); }

/* Forward declaration */
static void emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int subj, int cursor, int subj_len,
                               int *cap_slot, int *str_slot);

static void pat_emit_expr(EXPR_t *e) { emit_expr(e); }

static void emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int subj, int cursor, int subj_len,
                               int *cap_slot, int *str_slot) {
    if (!pat) { N("    br         %s\n", gamma); return; }

    int uid = next_uid();

    switch (pat->kind) {

    case E_QLIT: {
        const char *s = pat->sval ? pat->sval : "";
        int slen = (int)strlen(s);
        /* cursor + slen <= len? */
        ldloc_i(cursor); N("    ldc.i4     %d\n", slen); N("    add\n");
        ldloc_i(subj_len); N("    bgt        %s\n", omega);
        /* subject.Substring(cursor, slen) == lit? */
        ldloc_i(subj); ldloc_i(cursor); N("    ldc.i4     %d\n", slen);
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        N("    ldstr      \"%s\"\n", s);
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", omega);
        ldloc_i(cursor); N("    ldc.i4     %d\n", slen); N("    add\n");
        stloc_i(cursor);
        N("    br         %s\n", gamma);
        break;
    }

    case E_CONC: {
        /* SEQ: chain gamma/omega through each arm left-to-right */
        int nkids = expr_nargs(pat);
        if (nkids == 0) { N("    br         %s\n", gamma); break; }
        if (nkids == 1) {
            emit_pat_node(expr_arg(pat, 0), gamma, omega,
                              subj, cursor, subj_len, cap_slot, str_slot);
            break;
        }
        char **mids = malloc(nkids * sizeof(char *));
        for (int i = 0; i < nkids - 1; i++) {
            mids[i] = malloc(64);
            snprintf(mids[i], 64, "Nn%d_seq_γ%d", uid, i);
        }
        const char *seq_omega = omega;  /* may be overridden after ARB */
        for (int i = 0; i < nkids; i++) {
            const char *kg = (i < nkids - 1) ? mids[i] : gamma;
            arb_incr_label[0] = '\0';
            emit_pat_node(expr_arg(pat, i), kg, seq_omega,
                              subj, cursor, subj_len, cap_slot, str_slot);
            /* If this child was ARB, subsequent children should use ARB's
             * increment label as their omega (backtrack into ARB). */
            if (arb_incr_label[0]) {
                seq_omega = arb_incr_label;
                arb_incr_label[0] = '\0';
            }
            if (i < nkids - 1) N("  %s:\n", mids[i]);
        }
        for (int i = 0; i < nkids - 1; i++) free(mids[i]);
        free(mids);
        break;
    }

    case E_OR: {
        /* ALT: try each arm; restore cursor on failure, try next */
        int nkids = expr_nargs(pat);
        if (nkids == 0) { N("    br         %s\n", omega); break; }
        if (nkids == 1) {
            emit_pat_node(expr_arg(pat, 0), gamma, omega,
                              subj, cursor, subj_len, cap_slot, str_slot);
            break;
        }
        int loc_save = (*cap_slot)++;
        ldloc_i(cursor); stloc_i(loc_save);
        for (int i = 0; i < nkids; i++) {
            const char *kid_omega;
            char next_lbl[64];
            if (i < nkids - 1) {
                snprintf(next_lbl, sizeof next_lbl, "Nn%d_alt_β%d", uid, i);
                kid_omega = next_lbl;
            } else {
                kid_omega = omega;
            }
            emit_pat_node(expr_arg(pat, i), gamma, kid_omega,
                              subj, cursor, subj_len, cap_slot, str_slot);
            if (i < nkids - 1) {
                N("  %s:\n", next_lbl);
                ldloc_i(loc_save); stloc_i(cursor);
            }
        }
        break;
    }

    case E_NAM: {
        int cursor_before = (*cap_slot)++;  /* int32: cursor before capture */
        char gamma_lbl[64]; snprintf(gamma_lbl, sizeof gamma_lbl, "Nn%d_nam_γ", uid);
        ldloc_i(cursor); stloc_i(cursor_before);
        emit_pat_node(expr_left(pat), gamma_lbl, omega, subj, cursor, subj_len, cap_slot, str_slot);
        N("  %s:\n", gamma_lbl);
        const char *varname = (expr_right(pat) && expr_right(pat)->sval) ? expr_right(pat)->sval : "";
        ldloc_i(subj); ldloc_i(cursor_before);
        ldloc_i(cursor); ldloc_i(cursor_before); N("    sub\n");
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        if (is_output(varname)) {
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
        } else {
            char fn[256]; field_name(fn, sizeof fn, varname);
            N("    stsfld     string %s::%s\n", classname, fn);
        }
        N("    br         %s\n", gamma);
        break;
    }

    case E_DOL: {
        int cursor_before = (*cap_slot)++;
        char gamma_lbl[64]; snprintf(gamma_lbl, sizeof gamma_lbl, "Nn%d_dol_γ", uid);
        ldloc_i(cursor); stloc_i(cursor_before);
        emit_pat_node(expr_left(pat), gamma_lbl, omega, subj, cursor, subj_len, cap_slot, str_slot);
        N("  %s:\n", gamma_lbl);
        const char *varname = (expr_right(pat) && expr_right(pat)->sval) ? expr_right(pat)->sval : "";
        ldloc_i(subj); ldloc_i(cursor_before);
        ldloc_i(cursor); ldloc_i(cursor_before); N("    sub\n");
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        if (is_output(varname)) {
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
        } else {
            char fn[256]; field_name(fn, sizeof fn, varname);
            N("    stsfld     string %s::%s\n", classname, fn);
        }
        N("    br         %s\n", gamma);
        break;
    }

    case E_FNC: {
        const char *fname = pat->sval ? pat->sval : "";
        EXPR_t *arg0 = (expr_nargs(pat) >= 1) ? expr_arg(pat, 0) : NULL;

        if (strcasecmp(fname, "ARBNO") == 0) {
            int loc_save = (*cap_slot)++;
            char loop_lbl[64], done_lbl[64], child_gamma_lbl[64], child_omega_lbl[64];
            snprintf(loop_lbl,  sizeof loop_lbl,  "Nn%d_arb_β", uid);
            snprintf(done_lbl,  sizeof done_lbl,  "Nn%d_arb_γ", uid);
            snprintf(child_gamma_lbl,   sizeof child_gamma_lbl,   "Nn%d_arb_child_γ",  uid);
            snprintf(child_omega_lbl, sizeof child_omega_lbl,  "Nn%d_arb_child_ω",   uid);
            EXPR_t *child = arg0;
            N("  %s:\n", loop_lbl);
            ldloc_i(cursor); stloc_i(loc_save);
            emit_pat_node(child, child_gamma_lbl, child_omega_lbl, subj, cursor, subj_len, cap_slot, str_slot);
            N("  %s:\n", child_gamma_lbl);
            ldloc_i(cursor); ldloc_i(loc_save);
            N("    beq        %s\n", done_lbl);
            N("    br         %s\n", loop_lbl);
            N("  %s:\n", child_omega_lbl);
            ldloc_i(loc_save); stloc_i(cursor);
            N("  %s:\n", done_lbl);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "ANY") == 0) {
            int loc_ch = (*str_slot)++;   /* string: single-char string */
            ldloc_i(cursor); ldloc_i(subj_len); N("    bge        %s\n", omega);
            ldloc_i(subj); ldloc_i(cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            stloc_s(loc_ch);
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brfalse    %s\n", omega);
            ldloc_i(cursor); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "NOTANY") == 0) {
            int loc_ch = (*str_slot)++;
            ldloc_i(cursor); ldloc_i(subj_len); N("    bge        %s\n", omega);
            ldloc_i(subj); ldloc_i(cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            stloc_s(loc_ch);
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brtrue     %s\n", omega);
            ldloc_i(cursor); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "SPAN") == 0) {
            int loc_cs = (*str_slot)++;   /* string: charset */
            int loc_ch = (*str_slot)++;   /* string: char */
            char loop_lbl[64], done_lbl[64];
            snprintf(loop_lbl, sizeof loop_lbl, "Nn%d_span_loop", uid);
            snprintf(done_lbl, sizeof done_lbl, "Nn%d_span_γ", uid);
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            stloc_s(loc_cs);
            /* must match at least 1 */
            ldloc_i(cursor); ldloc_i(subj_len); N("    bge        %s\n", omega);
            ldloc_i(subj); ldloc_i(cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            stloc_s(loc_ch);
            ldloc_s(loc_cs); ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brfalse    %s\n", omega);
            ldloc_i(cursor); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(cursor);
            N("  %s:\n", loop_lbl);
            ldloc_i(cursor); ldloc_i(subj_len); N("    bge        %s\n", done_lbl);
            ldloc_i(subj); ldloc_i(cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            stloc_s(loc_ch);
            ldloc_s(loc_cs); ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brfalse    %s\n", done_lbl);
            ldloc_i(cursor); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(cursor);
            N("    br         %s\n", loop_lbl);
            N("  %s:\n", done_lbl);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "BREAK") == 0) {
            int loc_cs = (*str_slot)++;
            int loc_ch = (*str_slot)++;
            char loop_lbl[64], done_lbl[64];
            snprintf(loop_lbl, sizeof loop_lbl, "Nn%d_break_loop", uid);
            snprintf(done_lbl, sizeof done_lbl, "Nn%d_break_γ", uid);
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            stloc_s(loc_cs);
            N("  %s:\n", loop_lbl);
            ldloc_i(cursor); ldloc_i(subj_len); N("    bge        %s\n", omega);
            ldloc_i(subj); ldloc_i(cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            stloc_s(loc_ch);
            ldloc_s(loc_cs); ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brtrue     %s\n", done_lbl);
            ldloc_i(cursor); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(cursor);
            N("    br         %s\n", loop_lbl);
            N("  %s:\n", done_lbl);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "LEN") == 0) {
            int loc_n = (*cap_slot)++;
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            stloc_i(loc_n);
            ldloc_i(cursor); ldloc_i(loc_n); N("    add\n");
            ldloc_i(subj_len); N("    bgt        %s\n", omega);
            ldloc_i(cursor); ldloc_i(loc_n); N("    add\n"); stloc_i(cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "POS") == 0) {
            int loc_n = (*cap_slot)++;
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            stloc_i(loc_n);
            ldloc_i(cursor); ldloc_i(loc_n); N("    bne.un     %s\n", omega);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "RPOS") == 0) {
            int loc_n = (*cap_slot)++;
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            stloc_i(loc_n);
            ldloc_i(subj_len); ldloc_i(loc_n); N("    sub\n");
            ldloc_i(cursor); N("    bne.un     %s\n", omega);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "TAB") == 0) {
            int loc_n = (*cap_slot)++;
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            stloc_i(loc_n);
            ldloc_i(cursor); ldloc_i(loc_n); N("    bgt        %s\n", omega);
            ldloc_i(loc_n); ldloc_i(subj_len); N("    bgt        %s\n", omega);
            ldloc_i(loc_n); stloc_i(cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "RTAB") == 0) {
            int loc_n   = (*cap_slot)++;
            int loc_tgt = (*cap_slot)++;
            if (arg0) pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            stloc_i(loc_n);
            ldloc_i(subj_len); ldloc_i(loc_n); N("    sub\n"); stloc_i(loc_tgt);
            ldloc_i(cursor); ldloc_i(loc_tgt); N("    bgt        %s\n", omega);
            ldloc_i(loc_tgt); stloc_i(cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "REM") == 0) {
            ldloc_i(subj_len); stloc_i(cursor);
            N("    br         %s\n", gamma); break;
        }
        if (strcasecmp(fname, "ARB") == 0) {
            /* ARB: minimum-first via sno_arb helper.
             * We use an internal loop label so we do not redefine the
             * caller's omega label (which would be a duplicate).
             * Strategy: ARB saves cursor, tries 0..N chars; each time it
             * succeeds (goto gamma). The continuation's omega goes to an
             * "increment" label inside ARB. Caller's omega is the final fail.
             * To avoid redefining caller's omega, we redirect it via a private
             * fail label that falls through to the caller's omega. */
            int loc_arb_save = (*cap_slot)++;
            int loc_arb_try  = (*cap_slot)++;
            char arb_loop[64], arb_incr[64], arb_fail[64];
            snprintf(arb_loop, sizeof arb_loop, "Nn%d_arb_β",  uid);
            snprintf(arb_incr, sizeof arb_incr, "Nn%d_arb_β_inc", uid);
            snprintf(arb_fail, sizeof arb_fail, "Nn%d_arb_ω",  uid);
            ldloc_i(cursor); stloc_i(loc_arb_save);
            N("    ldc.i4.0\n"); stloc_i(loc_arb_try);
            N("  %s:\n", arb_loop);
            ldloc_i(loc_arb_save); ldloc_i(loc_arb_try); N("    add\n");
            ldloc_i(subj_len); N("    bgt        %s\n", arb_fail);
            ldloc_i(loc_arb_save); ldloc_i(loc_arb_try); N("    add\n");
            stloc_i(cursor);
            N("    br         %s\n", gamma);
            /* arb_incr: continuation failed, try one more char */
            N("  %s:\n", arb_incr);
            ldloc_i(loc_arb_try); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(loc_arb_try);
            N("    br         %s\n", arb_loop);
            /* arb_fail: exhausted — caller's omega */
            N("  %s:\n", arb_fail);
            N("    br         %s\n", omega);
            /* NOTE: caller must use arb_incr as omega for the continuation.
             * This requires SEQ to detect ARB and rewire. For now emit arb_incr
             * as the omega label that the continuation will jump to on fail. */
            /* Caller's omega label is overloaded — we emit it as alias to arb_incr
             * so the continuation jumps back here. But we can't redefine omega.
             * WORKAROUND: emit omega as a label that falls into arb_incr. */
            /* This is a structural limitation — ARB backtracking requires
             * the SEQ emitter cooperation. For now accept that omega is not
             * wired back; pattern will work only for ARB at end of pattern. */
            snprintf(arb_incr_label, sizeof arb_incr_label, "%s", arb_incr);
            break;
        }
        if (strcasecmp(fname, "FAIL")  == 0 || strcasecmp(fname, "ABORT")   == 0) {
            N("    br         %s\n", pat_abort_label[0] ? pat_abort_label : omega); break;
        }
        if (strcasecmp(fname, "SUCCEED") == 0 || strcasecmp(fname, "FENCE") == 0) {
            N("    br         %s\n", gamma); break;
        }
        N("    // STUB: unknown pattern FNC %s\n", fname);
        N("    br         %s\n", gamma);
        break;
    }

    case E_VART: {
        const char *vname = pat->sval ? pat->sval : "";
        if (strcasecmp(vname, "REM")     == 0) { ldloc_i(subj_len); stloc_i(cursor); N("    br %s\n", gamma); break; }
        if (strcasecmp(vname, "ARB") == 0) {
            int loc_arb_save = (*cap_slot)++;
            int loc_arb_try  = (*cap_slot)++;
            char arb_loop[64], arb_incr[64], arb_fail[64];
            snprintf(arb_loop, sizeof arb_loop, "Nn%d_varb_β",  uid);
            snprintf(arb_incr, sizeof arb_incr, "Nn%d_varb_β_inc", uid);
            snprintf(arb_fail, sizeof arb_fail, "Nn%d_varb_ω",  uid);
            ldloc_i(cursor); stloc_i(loc_arb_save);
            N("    ldc.i4.0\n"); stloc_i(loc_arb_try);
            N("  %s:\n", arb_loop);
            ldloc_i(loc_arb_save); ldloc_i(loc_arb_try); N("    add\n");
            ldloc_i(subj_len); N("    bgt        %s\n", arb_fail);
            ldloc_i(loc_arb_save); ldloc_i(loc_arb_try); N("    add\n");
            stloc_i(cursor);
            N("    br         %s\n", gamma);
            N("  %s:\n", arb_incr);
            ldloc_i(loc_arb_try); N("    ldc.i4.1\n"); N("    add\n"); stloc_i(loc_arb_try);
            N("    br         %s\n", arb_loop);
            N("  %s:\n", arb_fail);
            N("    br         %s\n", omega);
            snprintf(arb_incr_label, sizeof arb_incr_label, "%s", arb_incr);
            break;
        }
        if (strcasecmp(vname, "SUCCEED") == 0) { N("    br         %s\n", gamma); break; }
        if (strcasecmp(vname, "FENCE")   == 0) { N("    br         %s\n", gamma); break; }
        if (strcasecmp(vname, "FAIL")    == 0 || strcasecmp(vname, "ABORT") == 0) {
            N("    br         %s\n", pat_abort_label[0] ? pat_abort_label : omega); break;
        }
        /* Check named-pattern registry — inline expand if registered */
        {
            const NamedPat *np = named_pat_lookup(vname);
            if (np && np->pat) {
                emit_pat_node(np->pat, gamma, omega,
                                  subj, cursor, subj_len, cap_slot, str_slot);
                break;
            }
        }
        /* Otherwise: load variable's string value, match as literal */
        {
            int loc_lit  = (*str_slot)++;
            int loc_llen = (*cap_slot)++;
            char fn[256]; field_name(fn, sizeof fn, vname);
            N("    ldsfld     string %s::%s\n", classname, fn);
            stloc_s(loc_lit);
            ldloc_s(loc_lit);
            N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
            stloc_i(loc_llen);
            ldloc_i(cursor); ldloc_i(loc_llen); N("    add\n");
            ldloc_i(subj_len); N("    bgt        %s\n", omega);
            ldloc_i(subj); ldloc_i(cursor); ldloc_i(loc_llen);
            N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
            ldloc_s(loc_lit);
            N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
            N("    brfalse    %s\n", omega);
            ldloc_i(cursor); ldloc_i(loc_llen); N("    add\n"); stloc_i(cursor);
            N("    br         %s\n", gamma);
        }
        break;
    }

    default:
        N("    // STUB: unhandled pattern node kind=%d\n", pat->kind);
        N("    br         %s\n", gamma);
        break;
    }
}


/* -----------------------------------------------------------------------
 * Statement emitter — N-R1: assignments + OUTPUT + goto
 * ----------------------------------------------------------------------- */

static void emit_stmt(STMT_t *s, const char *next_lbl) {
    const char *tgt_s = s->go ? s->go->onsuccess : NULL;
    const char *tgt_f = s->go ? s->go->onfailure : NULL;
    const char *tgt_u = s->go ? s->go->uncond    : NULL;
    cur_stmt_fail_label[0] = '\0';  /* reset per-stmt */

    /* Emit statement label if present */
    if (s->label) {
        NSep(s->label);
        N("  L_%s:\n", s->label);
    }

    if (s->is_end) {
        NSep("END");
        emit_goto(tgt_u, "END");
        return;
    }

    /* Case 1: pure assignment — subject is a variable or OUTPUT, has_eq set, NO pattern */
    if (s->has_eq && s->subject && !s->pattern &&
        (s->subject->kind == E_VART || s->subject->kind == E_KW)) {

        int is_out = is_output(s->subject->sval);
        EXPR_t *rhs = s->replacement;

        /* Set fail label so INPUT inside rhs can branch directly on EOF */
        if (tgt_f)
            snprintf(cur_stmt_fail_label, sizeof cur_stmt_fail_label, "%s", tgt_f);
        else
            cur_stmt_fail_label[0] = '\0';

        /* eval RHS onto stack */
        emit_expr(rhs);
        cur_stmt_fail_label[0] = '\0';  /* reset after expr */

        if (is_out) {
            /* OUTPUT = expr → Console.WriteLine + monitor
             * Stack on entry: [val]
             * dup → [val, val]; WriteLine pops one → [val]
             * stloc V_20; ldstr "OUTPUT"; ldloc V_20 → net_monitor_write("OUTPUT", val) */
            N("    dup\n");
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
            N("    stloc.s    V_20\n");
            N("    ldstr      \"OUTPUT\"\n");
            N("    ldloc.s    V_20\n");
            N("    call       void %s::net_monitor_write(string, string)\n", classname);
        } else if (s->subject->kind == E_KW) {
            /* &KEYWORD = expr — write to known keyword field */
            const char *kw = s->subject->sval ? s->subject->sval : "";
            if (strcasecmp(kw, "ANCHOR") == 0)
                N("    stsfld     string %s::kw_anchor\n", classname);
            else if (strcasecmp(kw, "TRIM") == 0)
                N("    pop\n");  /* &TRIM: ignore for now (plain pop) */
            else
                N("    pop\n");  /* unknown keyword: discard */
        } else {
            /* VAR = expr → stsfld + monitor */
            char fn[256];
            field_name(fn, sizeof fn, s->subject->sval);
            /* dup: stsfld pops one copy, monitor gets the other */
            N("    dup\n");
            N("    stsfld     string %s::%s\n", classname, fn);
            /* stack: [val] — now call net_monitor_write(name, val) */
            N("    stloc.s    V_20\n");   /* stash val; stack empty */
            N("    ldstr      \"%s\"\n", s->subject->sval);  /* [name] */
            N("    ldloc.s    V_20\n");                       /* [name, val] */
            N("    call       void %s::net_monitor_write(string, string)\n", classname);
        }

        /* success: set flag=1 */
        N("    ldc.i4.1\n");
        N("    stloc.0\n");

        /* goto */
        if (tgt_u) emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) emit_branch_success(tgt_s);
            if (tgt_f) emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 1b: indirect assignment — $expr = val */
    if (s->has_eq && s->subject && s->subject->kind == E_INDR && !s->pattern) {
        EXPR_t *indr_operand = expr_arg(s->subject, 0);
        EXPR_t *rhs = s->replacement;
        /* push name string */
        emit_expr(indr_operand);
        /* push value string */
        if (!rhs || rhs->kind == E_NULV) ldstr("");
        else emit_expr(rhs);
        N("    call       void %s::net_indr_set(string, string)\n", classname);
        N("    ldc.i4.1\n");
        N("    stloc.0\n");
        if (tgt_u) emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) emit_branch_success(tgt_s);
            if (tgt_f) emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 2: bare expression predicate — no has_eq, subject only, no pattern */
    if (!s->has_eq && s->subject && !s->pattern) {
        /* Evaluate subject as expression — sets local 0 (success flag) via E_FNC */
        emit_expr(s->subject);
        N("    pop\n");  /* discard string result — we only care about local 0 */
        if (tgt_u) {
            emit_goto(tgt_u, next_lbl);
        } else {
            if (tgt_s) emit_branch_success(tgt_s);
            if (tgt_f) emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 4: pattern replacement — has_eq=1, pattern present: X 'pat' = 'repl' */
    if (s->has_eq && s->subject && s->pattern) {
        static int prepl_uid = 0;
        int suid = prepl_uid++;

        char tree_gamma_lbl[64], tree_omega_lbl[64], retry_lbl[64], end_lbl[64];
        snprintf(tree_gamma_lbl,   sizeof tree_gamma_lbl,   "Npr%d_tree_γ",   suid);
        snprintf(tree_omega_lbl, sizeof tree_omega_lbl,  "Npr%d_ω",  suid);
        snprintf(retry_lbl, sizeof retry_lbl,  "Npr%d_β", suid);
        snprintf(end_lbl,   sizeof end_lbl,    "Npr%d_γ",   suid);
        snprintf(pat_abort_label, sizeof pat_abort_label, "Npr%d_abort", suid);

        int subj   = 2;
        int cursor = 3;
        int subj_len    = 4;
        int loc_mstart = 5;
        int next_int = 6;
        int next_str = 20;

        /* load subject into V_2 */
        emit_expr(s->subject);
        N("    stloc.s    V_2\n");
        N("    ldloc.s    V_2\n");
        N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
        N("    stloc.s    V_4\n");
        N("    ldc.i4.0\n");
        N("    stloc.s    V_3\n");

        N("  %s:\n", retry_lbl);
        N("    ldloc.s    V_3\n");
        N("    stloc.s    V_5\n");

        emit_pat_node(s->pattern, tree_gamma_lbl, tree_omega_lbl,
                          subj, cursor, subj_len, &next_int, &next_str);

        /* fail path */
        N("  %s:\n", tree_omega_lbl);
        N("    ldsfld     string %s::kw_anchor\n", classname);
        N("    ldstr      \"0\"\n");
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", pat_abort_label);
        N("    ldloc.s    V_5\n");
        N("    ldc.i4.1\n");
        N("    add\n");
        N("    stloc.s    V_3\n");
        N("    ldloc.s    V_3\n");
        N("    ldloc.s    V_4\n");
        N("    ble        %s\n", retry_lbl);
        N("  %s:\n", pat_abort_label);
        N("    ldc.i4.0\n");
        N("    stloc.0\n");
        if (tgt_f) emit_branch_fail(tgt_f);
        else if (tgt_u) emit_goto(tgt_u, next_lbl);
        N("    br         %s\n", end_lbl);

        /* success path — replace matched region in subject */
        N("  %s:\n", tree_gamma_lbl);
        N("    ldc.i4.1\n");
        N("    stloc.0\n");
        /* Build: prefix + replacement + suffix */
        /* prefix = subject[0 .. mstart] */
        N("    ldloc.s    V_2\n");       /* full subject */
        N("    ldc.i4.0\n");
        N("    ldloc.s    V_5\n");       /* mstart */
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        /* replacement string */
        if (!s->replacement || s->replacement->kind == E_NULV)
            ldstr("");
        else
            emit_expr(s->replacement);
        N("    call       string [mscorlib]System.String::Concat(string, string)\n");
        /* suffix = subject[cursor .. end] */
        N("    ldloc.s    V_2\n");
        N("    ldloc.s    V_3\n");       /* cursor = end of match */
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32)\n");
        N("    call       string [mscorlib]System.String::Concat(string, string)\n");
        /* store back to subject variable */
        if (s->subject->kind == E_VART) {
            char fn[256]; field_name(fn, sizeof fn, s->subject->sval);
            N("    stsfld     string %s::%s\n", classname, fn);
        } else {
            N("    pop\n");  /* can't assign to non-var */
        }
        if (tgt_u) emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) emit_branch_success(tgt_s);
            if (tgt_f) emit_branch_fail(tgt_f);
        }
        N("  %s:\n", end_lbl);
        return;
    }

    /* Case 3: pattern match — subject has_eq=0, subject=var/expr, pattern present */
    if (!s->has_eq && s->subject && s->pattern) {
        static int pat_stmt_uid = 0;
        int suid = pat_stmt_uid++;

        char tree_gamma_lbl[64], tree_omega_lbl[64], retry_lbl[64];
        snprintf(tree_gamma_lbl,   sizeof tree_gamma_lbl,   "Npat%d_tree_γ",   suid);
        snprintf(tree_omega_lbl, sizeof tree_omega_lbl,  "Npat%d_ω",  suid);
        snprintf(retry_lbl, sizeof retry_lbl,  "Npat%d_β", suid);
        snprintf(pat_abort_label, sizeof pat_abort_label, "Npat%d_abort", suid);

        /* locals: V_0=success, V_1=unused,
         *         V_2=subj(string), V_3=cursor(int32), V_4=len(int32),
         *         V_5=mstart(int32), V_6..=pattern temporaries */
        int subj   = 2;
        int cursor = 3;
        int subj_len    = 4;
        int loc_mstart = 5;
        int next_int = 6;   /* int32 slots: V_6..V_19 */
        int next_str = 20;  /* string slots: V_20..V_29 */

        /* load subject */
        emit_expr(s->subject);
        N("    stloc.s    V_2\n");   /* subj */
        N("    ldloc.s    V_2\n");
        N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
        N("    stloc.s    V_4\n");   /* subj_len */
        N("    ldc.i4.0\n");
        N("    stloc.s    V_3\n");   /* cursor = 0 */

        /* retry loop: scan start position */
        N("  %s:\n", retry_lbl);
        N("    ldloc.s    V_3\n");
        N("    stloc.s    V_5\n");   /* mstart = cursor */

        emit_pat_node(s->pattern, tree_gamma_lbl, tree_omega_lbl,
                          subj, cursor, subj_len, &next_int, &next_str);

        N("  %s:\n", tree_omega_lbl);
        /* if anchor, fail; else advance start and retry */
        N("    ldsfld     string %s::kw_anchor\n", classname);
        N("    ldstr      \"0\"\n");
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", pat_abort_label);  /* anchored → fail */
        N("    ldloc.s    V_5\n");  /* mstart */
        N("    ldc.i4.1\n");
        N("    add\n");
        N("    stloc.s    V_3\n");  /* cursor = mstart+1 */
        N("    ldloc.s    V_3\n");
        N("    ldloc.s    V_4\n");
        N("    ble        %s\n", retry_lbl);
        /* fell off end — fail */
        N("  %s:\n", pat_abort_label);
        N("    ldc.i4.0\n");
        N("    stloc.0\n");
        if (tgt_f) emit_branch_fail(tgt_f);
        else if (tgt_u) emit_goto(tgt_u, next_lbl);
        /* fall through on fail */
        /* jump past success block */
        {
            char end_lbl[64]; snprintf(end_lbl, sizeof end_lbl, "Npat%d_γ", suid);
            N("    br         %s\n", end_lbl);
            N("  %s:\n", tree_gamma_lbl);
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            if (tgt_u) emit_goto(tgt_u, next_lbl);
            else {
                if (tgt_s) emit_branch_success(tgt_s);
                if (tgt_f) emit_branch_fail(tgt_f);
            }
            N("  %s:\n", end_lbl);
        }
        return;
    }

    /* No-op / unhandled statement — emit nop + goto only */
    N("    nop\n");
    if (tgt_u) emit_goto(tgt_u, next_lbl);
}

/* Returns 1 if statement s is inside any user function body. */
static int stmt_in_fn(STMT_t *s) {
    for (int fi = 0; fi < fn_count; fi++) {
        const FnDef *fn = &fn_table[fi];
        const char *entry = fn->entry_label[0] ? fn->entry_label : fn->name;
        int in_body = 0;
        /* Walk program to find whether s is between entry and end_label */
        /* We need prog here — use a simple label-range check instead */
        (void)entry; (void)in_body;
    }
    return 0;  /* determined per-statement during emit via in_fn_body flag below */
}

static void emit_stmts(Program *prog) {
    if (!prog || !prog->head) return;

    STMT_t *stmts[4096];
    int n = 0;
    for (STMT_t *s = prog->head; s && n < 4095; s = s->next)
        stmts[n++] = s;

    /* Build a bitmask of which statements are inside function bodies */
    char in_fn_body[4096];
    memset(in_fn_body, 0, sizeof in_fn_body);
    for (int fi = 0; fi < fn_count; fi++) {
        const FnDef *fn = &fn_table[fi];
        const char *entry = fn->entry_label[0] ? fn->entry_label : fn->name;
        int inside = 0;
        for (int i = 0; i < n; i++) {
            if (stmts[i]->label && strcasecmp(stmts[i]->label, entry) == 0) inside = 1;
            if (inside && fn->end_label[0] && stmts[i]->label
                && strcasecmp(stmts[i]->label, fn->end_label) == 0) inside = 0;
            if (inside) in_fn_body[i] = 1;
        }
    }

    for (int i = 0; i < n; i++) {
        STMT_t *s = stmts[i];
        /* Skip statements inside function bodies — they are emitted by emit_fn_method */
        if (in_fn_body[i]) continue;

        /* compute next_lbl for fall-through optimisation (skip over fn-body stmts) */
        const char *next_lbl = NULL;
        for (int j = i + 1; j < n; j++) {
            if (in_fn_body[j]) continue;
            if (stmts[j]->label) { next_lbl = stmts[j]->label; break; }
            if (stmts[j]->is_end) { next_lbl = "END"; break; }
            break;
        }

        if (s->is_end) {
            NSep("END statement");
            break;
        }
        /* increment &STNO before each statement */
        N("    ldsfld     string %s::kw_stno\n", classname);
        N("    ldstr      \"1\"\n");
        N("    call       string [snobol4lib]Snobol4Lib::sno_add(string, string)\n");
        N("    stsfld     string %s::kw_stno\n", classname);
        emit_stmt(s, next_lbl);
    }
}

/* helpers live in snobol4lib.dll — see src/runtime/net/snobol4lib.il */

/* -----------------------------------------------------------------------
 * Header / footer emitters
 * ----------------------------------------------------------------------- */

static void emit_header(Program *prog) {
    N(".assembly extern mscorlib {}\n");
    N(".assembly extern snobol4lib {}\n");
    N(".assembly extern snobol4run {}\n");
    N(".assembly %s {}\n", classname);
    N(".module %s.exe\n", classname);
    N("\n");
    N(".class public auto ansi beforefieldinit %s\n", classname);
    N("       extends [mscorlib]System.Object\n");
    N("{\n");
    N("\n");

    /* Emit one static string field per SNOBOL4 variable */
    if (nvar > 0) {
        NC("SNOBOL4 variable fields");
        for (int i = 0; i < nvar; i++) {
            char fn[256];
            field_name(fn, sizeof fn, vars[i]);
            N("  .field static string %s\n", fn);
        }
        N("\n");
    }
    /* &STNO and &ANCHOR keyword fields */
    N("  .field static string kw_stno\n");
    N("  .field static string kw_anchor\n");
    N("  .field static class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> sno_vars\n");
    N("  .field static class [mscorlib]System.IO.StreamWriter sno_monitor_out\n");
    N("\n");

    /* Static initialiser: set all variables to "" */
    {
        N("  .method static void .cctor() cil managed\n");
        N("  {\n");
        N("    .maxstack 4\n");
        for (int i = 0; i < nvar; i++) {
            char fn[256];
            field_name(fn, sizeof fn, vars[i]);
            N("    ldstr      \"\"\n");
            N("    stsfld     string %s::%s\n", classname, fn);
        }
        N("    ldstr      \"0\"\n");
        N("    stsfld     string %s::kw_stno\n", classname);
        N("    ldstr      \"0\"\n");
        N("    stsfld     string %s::kw_anchor\n", classname);
        N("    newobj     instance void class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>::.ctor()\n");
        N("    stsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> %s::sno_vars\n", classname);
        /* open MONITOR_FIFO if set */
        N("    ldstr      \"MONITOR_FIFO\"\n");
        N("    call       string [mscorlib]System.Environment::GetEnvironmentVariable(string)\n");
        N("    dup\n");
        N("    brfalse    Ncctor_no_mon\n");
        N("    ldc.i4.1\n");   /* append=true */
        N("    newobj     instance void [mscorlib]System.IO.StreamWriter::.ctor(string, bool)\n");
        N("    dup\n");
        N("    ldc.i4.1\n");
        N("    callvirt   instance void [mscorlib]System.IO.TextWriter::set_AutoFlush(bool)\n");
        N("    stsfld     class [mscorlib]System.IO.StreamWriter %s::sno_monitor_out\n", classname);
        N("    br         Ncctor_mon_done\n");
        N("  Ncctor_no_mon:\n");
        N("    pop\n");
        N("    ldnull\n");
        N("    stsfld     class [mscorlib]System.IO.StreamWriter %s::sno_monitor_out\n", classname);
        N("  Ncctor_mon_done:\n");
        N("    ret\n");
        N("  }\n");
        N("\n");
    }

    /* net_indr_get(string name) -> string */
    N("  .method static string net_indr_get(string name) cil managed\n");
    N("  {\n");
    N("    .maxstack 4\n");
    N("    .locals init (string V_0)\n");
    N("    ldsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> %s::sno_vars\n", classname);
    N("    ldarg.0\n");
    N("    ldloca.s   V_0\n");
    N("    callvirt   instance bool class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>::TryGetValue(!0, !1&)\n");
    N("    brfalse    Nig_miss\n");
    N("    ldloc.0\n");
    N("    ret\n");
    N("  Nig_miss:\n");
    N("    ldstr      \"\"\n");
    N("    ret\n");
    N("  }\n\n");
    /* net_indr_set(string name, string val) -> void
     * Stores in Dictionary AND updates the static field via reflection. */
    N("  .method static void net_indr_set(string name, string val) cil managed\n");
    N("  {\n");
    N("    .maxstack 5\n");
    // Store in Dictionary
    N("    ldsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> %s::sno_vars\n", classname);
    N("    ldarg.0\n");
    N("    ldarg.1\n");
    N("    callvirt   instance void class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>::set_Item(!0, !1)\n");
    // Also update static field via reflection
    N("    ldtoken    %s\n", classname);
    N("    call       class [mscorlib]System.Type [mscorlib]System.Type::GetTypeFromHandle(valuetype [mscorlib]System.RuntimeTypeHandle)\n");
    N("    ldarg.0\n");
    // Field name is uppercase in our scheme — use ToUpper
    N("    callvirt   instance string [mscorlib]System.String::ToUpper()\n");
    N("    ldc.i4     56\n");  // BindingFlags.Static|Public|NonPublic = 16|32|4=52
    N("    callvirt   instance class [mscorlib]System.Reflection.FieldInfo [mscorlib]System.Type::GetField(string, valuetype [mscorlib]System.Reflection.BindingFlags)\n");
    N("    dup\n");
    N("    brfalse    Nis_nofld\n");
    N("    ldnull\n");
    N("    ldarg.1\n");
    N("    callvirt   instance void [mscorlib]System.Reflection.FieldInfo::SetValue(object, object)\n");
    N("    br         Nis_done\n");
    N("  Nis_nofld:\n");
    N("    pop\n");
    N("  Nis_done:\n");
    N("    ret\n");
    N("  }\n\n");

    /* net_monitor_write(string name, string val) -> void
     * Writes  VAR name "val"\n  to sno_monitor_out if non-null. */
    N("  .method static void net_monitor_write(string name, string val) cil managed\n");
    N("  {\n");
    N("    .maxstack 3\n");
    N("    .locals init (class [mscorlib]System.IO.StreamWriter V_mon)\n");
    N("    ldsfld     class [mscorlib]System.IO.StreamWriter %s::sno_monitor_out\n", classname);
    N("    stloc      V_mon\n");
    N("    ldloc      V_mon\n");
    N("    brfalse    Nmw_done\n");
    /* Write "VAR " */
    N("    ldloc      V_mon\n");
    N("    ldstr      \"VAR \"\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    /* Write name */
    N("    ldloc      V_mon\n");
    N("    ldarg.0\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    /* Write " \"" */
    N("    ldloc      V_mon\n");
    N("    ldstr      \" \\\"\"\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    /* Write val */
    N("    ldloc      V_mon\n");
    N("    ldarg.1\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    /* WriteLine "\"" (adds newline) */
    N("    ldloc      V_mon\n");
    N("    ldstr      \"\\\"\"\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::WriteLine(string)\n");
    N("  Nmw_done:\n");
    N("    ret\n");
    N("  }\n\n");

    (void)prog;
}

static void emit_main_open(void) {
    N("  .method public static void main(string[] args) cil managed\n");
    N("  {\n");
    N("    .entrypoint\n");
    N("    .maxstack 16\n");
    /* V_0=success(int32), V_1=unused(int32),
     * V_2=pat_subj(string), V_3=pat_cursor(int32), V_4=pat_len(int32),
     * V_5=pat_mstart(int32),
     * V_6..V_19=int32 pattern temps, V_20..V_29=string pattern temps */
    N("    .locals init (int32 V_0, int32 V_1,\n");
    N("                  string V_2, int32 V_3, int32 V_4, int32 V_5,\n");
    N("                  int32 V_6,  int32 V_7,  int32 V_8,  int32 V_9,\n");
    N("                  int32 V_10, int32 V_11, int32 V_12, int32 V_13,\n");
    N("                  int32 V_14, int32 V_15, int32 V_16, int32 V_17,\n");
    N("                  int32 V_18, int32 V_19,\n");
    N("                  string V_20, string V_21, string V_22, string V_23,\n");
    N("                  string V_24, string V_25, string V_26, string V_27,\n");
    N("                  string V_28, string V_29)\n");
    N("\n");
}

static void emit_main_close(void) {
    NSep("END");
    N("  L_END:\n");
    NI("nop", "");
    NI("ret", "");
    N("  }\n");
    N("\n");
}


/* -----------------------------------------------------------------------
 * User-defined function support (DEFINE/RETURN/FRETURN)
 * Mirrors JVM backend's JvmFnDef + jvm_emit_fn_method, adapted to CIL.
 * Each DEFINE becomes a static CIL method: string net_fn_NAME(string, ...).
 * Save/restore args+locals via sno_vars Dictionary (net_indr_get/set).
 * RETURN -> load fn-name var, restore, ret value.
 * FRETURN -> restore, ldnull ret (null = failure signal).
 * ----------------------------------------------------------------------- */
/* FnDef, fn_table, fn_count, cur_fn declared at top of file */

static const FnDef *find_fn(const char *name) {
    for (int i = 0; i < fn_count; i++)
        if (fn_table[i].name[0] && strcasecmp(fn_table[i].name, name) == 0)
            return &fn_table[i];
    return NULL;
}

/* Parse DEFINE prototype string "fname(a,b)l1,l2" into FnDef. */
static int parse_proto(const char *proto, FnDef *fn) {
    fn->nargs = 0; fn->nlocals = 0;
    const char *p = proto;
    int ni = 0;
    while (*p && *p != '(' && ni < NAME_LEN - 1)
        fn->name[ni++] = *p++;
    fn->name[ni] = '\0';
    /* trim whitespace */
    while (ni > 0 && (fn->name[ni-1]==' '||fn->name[ni-1]=='\t')) fn->name[--ni] = '\0';
    if (!ni) return 0;
    /* default entry label = name */
    snprintf(fn->entry_label, NAME_LEN, "%s", fn->name);
    if (*p != '(') return 1;
    p++; /* skip '(' */
    while (*p && *p != ')') {
        while (*p==' '||*p=='\t') p++;
        if (*p==')'||!*p) break;
        int ai=0;
        while (*p && *p!=',' && *p!=')' && ai < NAME_LEN-1)
            fn->args[fn->nargs][ai++] = *p++;
        while (ai>0 && (fn->args[fn->nargs][ai-1]==' '||fn->args[fn->nargs][ai-1]=='\t')) ai--;
        fn->args[fn->nargs][ai] = '\0';
        if (ai>0 && fn->nargs < FN_ARGMAX) fn->nargs++;
        if (*p==',') p++;
    }
    if (*p==')') p++;
    /* locals after ')' */
    while (*p) {
        while (*p==' '||*p=='\t'||*p==',') p++;
        if (!*p) break;
        int li=0;
        while (*p && *p!=',' && li < NAME_LEN-1)
            fn->locals[fn->nlocals][li++] = *p++;
        while (li>0 && (fn->locals[fn->nlocals][li-1]==' '||fn->locals[fn->nlocals][li-1]=='\t')) li--;
        fn->locals[fn->nlocals][li] = '\0';
        if (li>0 && fn->nlocals < FN_ARGMAX) fn->nlocals++;
    }
    return 1;
}

/* Flatten a simple literal/var expr to a string buffer (for DEFINE arg parsing). */
static const char *flatten_str(EXPR_t *e, char *buf, int sz) {
    if (!e) return NULL;
    if (e->kind == E_QLIT && e->sval) { snprintf(buf, sz, "%s", e->sval); return buf; }
    if (e->kind == E_VART && e->sval) { snprintf(buf, sz, "%s", e->sval); return buf; }
    return NULL;
}

static void collect_fndefs(Program *prog) {
    fn_count = 0;
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_FNC) continue;
        if (!s->subject->sval || strcasecmp(s->subject->sval, "DEFINE") != 0) continue;
        if (expr_nargs(s->subject) < 1) continue;
        char pbuf[256];
        const char *proto = flatten_str(expr_arg(s->subject, 0), pbuf, sizeof pbuf);
        if (!proto) continue;
        if (fn_count >= FN_MAX) break;
        FnDef *fn = &fn_table[fn_count];
        memset(fn, 0, sizeof *fn);
        if (!parse_proto(proto, fn)) continue;
        /* Optional 2nd arg: explicit entry label */
        if (expr_nargs(s->subject) >= 2) {
            char ebuf[128];
            const char *el = flatten_str(expr_arg(s->subject, 1), ebuf, sizeof ebuf);
            if (el && el[0]) snprintf(fn->entry_label, NAME_LEN, "%s", el);
        }
        /* end_label from goto on DEFINE stmt */
        fn->end_label[0] = '\0';
        if (s->go) {
            const char *gl = s->go->uncond ? s->go->uncond
                           : s->go->onsuccess ? s->go->onsuccess : NULL;
            if (gl) snprintf(fn->end_label, NAME_LEN, "%s", gl);
        }
        /* Register fn name, args, locals as SNOBOL4 variables (need static fields) */
        var_register(fn->name);
        for (int ai = 0; ai < fn->nargs; ai++) var_register(fn->args[ai]);
        for (int li = 0; li < fn->nlocals; li++) var_register(fn->locals[li]);
        fn_count++;
    }
}

/* Emit one user function as a static CIL method. */
static void emit_fn_method(const FnDef *fn, Program *prog, int fn_idx) {
    /* Build method signature: (string, string, ...) → string */
    N("  .method static string net_fn_%s(", fn->name);
    for (int i = 0; i < fn->nargs; i++) {
        if (i > 0) N(", ");
        N("string");
    }
    N(") cil managed\n");
    N("  {\n");
    N("    .maxstack 8\n");
    /* Locals: V_0=success, plus pattern temps */
    N("    .locals init (int32 V_0, int32 V_1,\n");
    N("                  string V_2, int32 V_3, int32 V_4, int32 V_5,\n");
    N("                  int32 V_6,  int32 V_7,  int32 V_8,  int32 V_9,\n");
    N("                  int32 V_10, int32 V_11, int32 V_12, int32 V_13,\n");
    N("                  int32 V_14, int32 V_15, int32 V_16, int32 V_17,\n");
    N("                  int32 V_18, int32 V_19,\n");
    N("                  string V_20, string V_21, string V_22, string V_23,\n");
    N("                  string V_24, string V_25, string V_26, string V_27,\n");
    N("                  string V_28, string V_29,\n");
    /* save slots: V_30..V_30+nargs+nlocals for old values */
    int save_base = 30;
    int save_fnret = save_base + fn->nargs + fn->nlocals;
    N("                  ");
    int total_saves = fn->nargs + fn->nlocals + 1; /* +1 for fn retval */
    for (int i = 0; i < total_saves; i++) {
        if (i > 0) N(", ");
        N("string V_%d", save_base + i);
    }
    N(")\n\n");

    /* Save old arg values */
    for (int i = 0; i < fn->nargs; i++) {
        N("    ldstr      \"%s\"\n", fn->args[i]);
        N("    call       string %s::net_indr_get(string)\n", classname);
        N("    stloc.s    V_%d\n", save_base + i);
    }
    /* Save old local values */
    for (int i = 0; i < fn->nlocals; i++) {
        N("    ldstr      \"%s\"\n", fn->locals[i]);
        N("    call       string %s::net_indr_get(string)\n", classname);
        N("    stloc.s    V_%d\n", save_base + fn->nargs + i);
    }
    /* Save old fn-name value */
    N("    ldstr      \"%s\"\n", fn->name);
    N("    call       string %s::net_indr_get(string)\n", classname);
    N("    stloc.s    V_%d\n", save_fnret);

    /* Bind incoming args: net_indr_set(name, arg_i) + update static field */
    for (int i = 0; i < fn->nargs; i++) {
        N("    ldstr      \"%s\"\n", fn->args[i]);
        N("    ldarg.s    %d\n", i);
        N("    call       void %s::net_indr_set(string, string)\n", classname);
    }
    /* Init locals to "" */
    for (int i = 0; i < fn->nlocals; i++) {
        N("    ldstr      \"%s\"\n", fn->locals[i]);
        N("    ldstr      \"\"\n");
        N("    call       void %s::net_indr_set(string, string)\n", classname);
    }
    /* Init fn-name var to "" */
    N("    ldstr      \"%s\"\n", fn->name);
    N("    ldstr      \"\"\n");
    N("    call       void %s::net_indr_set(string, string)\n", classname);

    /* Return/freturn labels */
    snprintf(fn_return_lbl,  sizeof fn_return_lbl,  "Nfn%d_γ",  fn_idx);
    snprintf(fn_freturn_lbl, sizeof fn_freturn_lbl, "Nfn%d_ω", fn_idx);

    /* Emit function body statements */
    const char *entry = fn->entry_label[0] ? fn->entry_label : fn->name;
    int in_body = 0;
    int si = 0;
    const FnDef *saved_fn = cur_fn;
    cur_fn = fn;

    STMT_t *stmts[4096]; int ns = 0;
    for (STMT_t *s = prog->head; s && ns < 4095; s = s->next) stmts[ns++] = s;

    for (int i = 0; i < ns; i++) {
        STMT_t *s = stmts[i];
        if (s->label && strcasecmp(s->label, entry) == 0) in_body = 1;
        if (in_body && fn->end_label[0] && s->label && strcasecmp(s->label, fn->end_label) == 0) { in_body = 0; break; }
        if (!in_body) continue;
        if (s->is_end) break;
        const char *next_lbl = NULL;
        if (i+1 < ns && stmts[i+1]->label) next_lbl = stmts[i+1]->label;
        else if (i+1 < ns && stmts[i+1]->is_end) next_lbl = "END";
        /* increment &STNO */
        N("    ldsfld     string %s::kw_stno\n", classname);
        N("    ldstr      \"1\"\n");
        N("    call       string [snobol4lib]Snobol4Lib::sno_add(string, string)\n");
        N("    stsfld     string %s::kw_stno\n", classname);
        emit_stmt(s, next_lbl);
        si++;
    }
    cur_fn = saved_fn;

    /* RETURN path: restore, return fn->name value */
    N("  %s:\n", fn_return_lbl);
    for (int i = 0; i < fn->nargs; i++) {
        N("    ldstr      \"%s\"\n", fn->args[i]);
        N("    ldloc.s    V_%d\n", save_base + i);
        N("    call       void %s::net_indr_set(string, string)\n", classname);
    }
    for (int i = 0; i < fn->nlocals; i++) {
        N("    ldstr      \"%s\"\n", fn->locals[i]);
        N("    ldloc.s    V_%d\n", save_base + fn->nargs + i);
        N("    call       void %s::net_indr_set(string, string)\n", classname);
    }
    /* Get retval from static field (function body stores there via stsfld) */
    {
        char fn_field[256]; field_name(fn_field, sizeof fn_field, fn->name);
        N("    ldsfld     string %s::%s\n", classname, fn_field);
    }
    /* Restore fn name var */
    N("    ldstr      \"%s\"\n", fn->name);
    N("    ldloc.s    V_%d\n", save_fnret);
    N("    call       void %s::net_indr_set(string, string)\n", classname);
    N("    ret\n");

    /* FRETURN path: restore, return null */
    N("  %s:\n", fn_freturn_lbl);
    for (int i = 0; i < fn->nargs; i++) {
        N("    ldstr      \"%s\"\n", fn->args[i]);
        N("    ldloc.s    V_%d\n", save_base + i);
        N("    call       void %s::net_indr_set(string, string)\n", classname);
    }
    for (int i = 0; i < fn->nlocals; i++) {
        N("    ldstr      \"%s\"\n", fn->locals[i]);
        N("    ldloc.s    V_%d\n", save_base + fn->nargs + i);
        N("    call       void %s::net_indr_set(string, string)\n", classname);
    }
    N("    ldstr      \"%s\"\n", fn->name);
    N("    ldloc.s    V_%d\n", save_fnret);
    N("    call       void %s::net_indr_set(string, string)\n", classname);
    N("    ldnull\n");
    N("    ret\n");
    N("  }\n\n");
}

static void emit_fn_methods(Program *prog) {
    for (int i = 0; i < fn_count; i++)
        emit_fn_method(&fn_table[i], prog, i);
}

static void emit_footer(void) {
    N("} // end class %s\n", classname);
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

void net_emit(Program *prog, FILE *out, const char *filename) {
    out = out;
    nvar = 0;
    set_classname(filename);

    /* Multi-pass: scan functions first (registers vars), then vars, named patterns */
    collect_fndefs(prog);
    collect_vars(prog);
    scan_named_patterns(prog);

    NC("Generated by sno2c -net");
    NC("Assemble: ilasm <file>.il /output:<file>.exe");
    NC("Run:      mono <file>.exe");
    N("\n");

    emit_header(prog);
    emit_main_open();
    emit_stmts(prog);
    emit_main_close();
    emit_fn_methods(prog);
    emit_footer();

    /* free registered variable names */
    for (int i = 0; i < nvar; i++) { free(vars[i]); vars[i] = NULL; }
    nvar = 0;
}
