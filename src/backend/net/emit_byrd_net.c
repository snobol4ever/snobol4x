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
#define NET_FN_MAX      32
#define NET_FN_NAMELEN  128
#define NET_FN_ARGMAX   16
typedef struct {
    char name[NET_FN_NAMELEN];
    char args[NET_FN_ARGMAX][NET_FN_NAMELEN];
    int  nargs;
    char locals[NET_FN_ARGMAX][NET_FN_NAMELEN];
    int  nlocals;
    char entry_label[NET_FN_NAMELEN];
    char end_label[NET_FN_NAMELEN];
} NetFnDef;
static NetFnDef  net_fn_table[NET_FN_MAX];
static int       net_fn_count = 0;
static const NetFnDef *net_cur_fn = NULL;
static char net_fn_return_lbl[128];
static char net_fn_freturn_lbl[128];
static const NetFnDef *net_find_fn(const char *name);
static int net_pat_uid_early = 0;  /* uid counter used before pattern section */

/* DATA type support */
#define NET_DATA_MAX     32
typedef struct {
    char type_name[NET_FN_NAMELEN];
    char fields[NET_FN_ARGMAX][NET_FN_NAMELEN];
    int  nfields;
} NetDataType;
static NetDataType net_data_types[NET_DATA_MAX];
static int         net_data_type_count = 0;
static const NetDataType *net_find_data_type(const char *name);
static const NetDataType *net_find_data_field(const char *field);

/* Deferred helper emission flags */
static int net_need_array_helpers = 0;
static int net_need_data_helpers  = 0;
#define net_pat_uid net_pat_uid_early

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

/* -----------------------------------------------------------------------
 * Named pattern registry — VAR = <pattern-expr> assignments.
 * When E_VART appears in pattern context, inline-expand stored pattern tree.
 * ----------------------------------------------------------------------- */
#define NET_NAMED_PAT_MAX 64
typedef struct { char varname[128]; EXPR_t *pat; } NetNamedPat;
static NetNamedPat net_named_pats[NET_NAMED_PAT_MAX];
static int         net_named_pat_count = 0;

static void net_named_pat_reset(void) { net_named_pat_count = 0; }

static void net_named_pat_register(const char *varname, EXPR_t *pat) {
    for (int i = 0; i < net_named_pat_count; i++) {
        if (strcasecmp(net_named_pats[i].varname, varname) == 0) {
            if (pat) net_named_pats[i].pat = pat;
            return;
        }
    }
    if (net_named_pat_count >= NET_NAMED_PAT_MAX) return;
    NetNamedPat *e = &net_named_pats[net_named_pat_count++];
    snprintf(e->varname, sizeof e->varname, "%s", varname);
    e->pat = pat;
}

static const NetNamedPat *net_named_pat_lookup(const char *varname) {
    for (int i = 0; i < net_named_pat_count; i++)
        if (strcasecmp(net_named_pats[i].varname, varname) == 0)
            return &net_named_pats[i];
    return NULL;
}

static int net_expr_has_pat_fn(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC || e->kind == E_NAM || e->kind == E_DOL) return 1;
    for (int i = 0; i < expr_nargs(e); i++)
        if (net_expr_has_pat_fn(expr_arg(e, i))) return 1;
    return 0;
}

static int net_expr_is_pattern_expr(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_OR)   return 1;
    if (e->kind == E_CONC) return net_expr_has_pat_fn(e);
    return net_expr_has_pat_fn(e);
}

static void net_scan_named_patterns(Program *prog) {
    net_named_pat_reset();
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->subject && s->subject->kind == E_VART && s->subject->sval &&
            s->has_eq && s->replacement && !s->pattern) {
            if (net_expr_is_pattern_expr(s->replacement))
                net_named_pat_register(s->subject->sval, s->replacement);
        }
    }
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

static int net_is_input(const char *name) {
    return name && strcasecmp(name, "INPUT") == 0;
}

/* Current statement fail label — set by stmt emitter so net_emit_expr
 * can emit an inline null-check when it encounters INPUT (EOF → fail). */
static char net_cur_stmt_fail_label[128];
static int  net_input_uid = 0;

/* -----------------------------------------------------------------------
 * Expr scanner — collect all variable refs before emitting
 * ----------------------------------------------------------------------- */

static void scan_expr_vars(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_VART && e->sval && !net_is_output(e->sval)
            && !net_is_input(e->sval))
        net_var_register(e->sval);
    /* E_NAM (. capture) and E_DOL ($ capture): sval = target variable name */
    if ((e->kind == E_NAM || e->kind == E_DOL) && e->sval
            && !net_is_output(e->sval) && !net_is_input(e->sval))
        net_var_register(e->sval);
    /* E_ATP (@VAR): varname in children[0]->sval */
    if (e->kind == E_ATP && expr_left(e) && expr_left(e)->sval
            && !net_is_output(expr_left(e)->sval) && !net_is_input(expr_left(e)->sval))
        net_var_register(expr_left(e)->sval);
    for (int i = 0; i < expr_nargs(e); i++)
        scan_expr_vars(expr_arg(e, i));
}

static void scan_prog_vars(Program *prog) {
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* subject variable (LHS of assignment) */
        if (s->subject && s->subject->kind == E_VART && s->subject->sval
                && !net_is_output(s->subject->sval)
                && !net_is_input(s->subject->sval))
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

/* net_expr_can_fail: returns 1 if expression can set local 0 = 0 (failure).
 * Used by E_CONC to decide whether to emit a goal-directed short-circuit check.
 * Only predicate/comparison functions actually fail — pattern constructors and
 * string functions always succeed and must NOT be treated as failing. */
static int net_expr_can_fail(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC && e->sval) {
        /* Predicate functions that can fail (return int32 success flag) */
        const char *fn = e->sval;
        if (strcasecmp(fn, "DIFFER") == 0) return 1;
        if (strcasecmp(fn, "IDENT")  == 0) return 1;
        if (strcasecmp(fn, "GT")     == 0) return 1;
        if (strcasecmp(fn, "LT")     == 0) return 1;
        if (strcasecmp(fn, "GE")     == 0) return 1;
        if (strcasecmp(fn, "LE")     == 0) return 1;
        if (strcasecmp(fn, "EQ")     == 0) return 1;
        if (strcasecmp(fn, "NE")     == 0) return 1;
        if (strcasecmp(fn, "LGT")    == 0) return 1;
        if (strcasecmp(fn, "LLT")    == 0) return 1;
        if (strcasecmp(fn, "LGE")    == 0) return 1;
        if (strcasecmp(fn, "LLE")    == 0) return 1;
        if (strcasecmp(fn, "LEQ")    == 0) return 1;
        if (strcasecmp(fn, "LNE")    == 0) return 1;
        if (strcasecmp(fn, "INTEGER") == 0) return 1;
        /* User-defined functions: checked via net_find_fn */
        if (net_find_fn(fn)) return 1;
        /* Pattern constructors and string functions: always succeed */
        return 0;
    }
    if (e->kind == E_CONC) {
        for (int i = 0; i < e->nchildren; i++)
            if (net_expr_can_fail(e->children[i])) return 1;
    }
    return 0;
}

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
    case E_INDR: {
        /* $expr — indirect variable read: eval name, call net_indr_get */
        net_emit_expr(expr_arg(e, 0));
        N("    call       string %s::net_indr_get(string)\n", net_classname);
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
        if (net_is_input(e->sval)) {
            /* INPUT — call sno_input(); null → EOF → statement fails.
             * Stack convention: on success, leaves string on stack.
             * On EOF, branches directly to fail label (or pops + pushes ""
             * if no fail label available), keeping stack depth consistent. */
            int uid = net_input_uid++;
            char inp_ok[64];
            snprintf(inp_ok, sizeof inp_ok, "Ninp%d_ok", uid);
            N("    call       string [snobol4run]Snobol4Run::sno_input()\n");
            N("    dup\n");
            N("    brtrue     %s\n", inp_ok);
            /* null = EOF path: pop null off stack, set fail flag */
            N("    pop\n");
            N("    ldc.i4.0\n");
            N("    stloc.0\n");
            if (net_cur_stmt_fail_label[0]) {
                /* Branch to fail — stack depth is 0 here (null was popped),
                 * fail label entry must also expect depth 0 (it's a stmt top) */
                N("    br         L_%s\n", net_cur_stmt_fail_label);
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
        net_field_name(fn, sizeof fn, e->sval);
        N("    ldsfld     string %s::%s\n", net_classname, fn);
        break;
    }
    case E_KW:
        /* &ALPHABET → call sno_alphabet(); &STNO → statement counter; others → "" stub */
        if (e->sval && strcasecmp(e->sval, "ALPHABET") == 0) {
            N("    call       string [snobol4lib]Snobol4Lib::sno_alphabet()\n");
        } else if (e->sval && strcasecmp(e->sval, "STNO") == 0) {
            N("    ldsfld     string %s::kw_stno\n", net_classname);
        } else if (e->sval && strcasecmp(e->sval, "UCASE") == 0) {
            net_ldstr("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        } else if (e->sval && strcasecmp(e->sval, "LCASE") == 0) {
            net_ldstr("abcdefghijklmnopqrstuvwxyz");
        } else {
            net_ldstr("");
        }
        break;
    case E_FNC: {
        /* Builtin functions: set local 0 (success flag), leave string on stack */
        const char *fn = e->sval ? e->sval : "";
        /* SIZE(x) — returns length string, always succeeds */
        if (strcasecmp(fn, "SIZE") == 0) {
            net_emit_expr(expr_arg(e, 0));
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
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            N("    call       int32 [snobol4lib]Snobol4Lib::%s(string, string)\n", cmp_helper);
            N("    stloc.0\n");
            /* push right-arg value as expression result */
            net_emit_expr(expr_arg(e, 1));
            break;
        }
        /* String equality: IDENT DIFFER */
        const char *str_helper = NULL;
        if      (strcasecmp(fn, "IDENT")  == 0) str_helper = "sno_ident";
        else if (strcasecmp(fn, "DIFFER") == 0) str_helper = "sno_differ";
        if (str_helper) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            N("    call       int32 [snobol4lib]Snobol4Lib::%s(string, string)\n", str_helper);
            N("    stloc.0\n");
            net_ldstr("");
            break;
        }
        /* DATATYPE(x) — returns user DATA type name, or "string"/"integer"/"real" */
        if (strcasecmp(fn, "DATATYPE") == 0) {
            net_need_array_helpers = 1;
            int uid = net_pat_uid++;
            char lbl_prim[64], lbl_done[64];
            snprintf(lbl_prim, sizeof lbl_prim, "Ndt%d_prim", uid);
            snprintf(lbl_done, sizeof lbl_done, "Ndt%d_done", uid);
            /* Evaluate arg, dup it so we can use it twice */
            net_emit_expr(expr_arg(e, 0));
            N("    dup\n");                   /* stack: arg arg */
            net_ldstr("__type__");            /* stack: arg arg "__type__" */
            N("    call       string %s::net_array_get(string, string)\n", net_classname);
            /* stack: arg type_or_empty */
            N("    dup\n");
            N("    ldstr      \"\"\n");
            N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
            N("    brtrue     %s\n", lbl_prim);
            /* non-empty __type__: discard original arg, keep type string */
            N("    stloc.s    V_22\n");       /* save type string */
            N("    pop\n");                   /* discard original arg */
            N("    ldloc.s    V_22\n");
            N("    br         %s\n", lbl_done);
            N("  %s:\n", lbl_prim);
            N("    pop\n");                   /* discard empty type string */
            /* original arg still on stack */
            N("    call       string [snobol4lib]Snobol4Lib::sno_datatype(string)\n");
            N("  %s:\n", lbl_done);
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
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            N("    call       int32 [snobol4lib]Snobol4Lib::%s(string, string)\n", lcmp_helper);
            N("    stloc.0\n");
            net_ldstr("");
            break;
        }
        /* SUBSTR(str, start [, len]) — 1-based substring */
        if (strcasecmp(fn, "SUBSTR") == 0) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            if (expr_arg(e, 2))
                net_emit_expr(expr_arg(e, 2));
            else
                net_ldstr("-1");
            N("    call       string [snobol4lib]Snobol4Lib::sno_substr(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* REPLACE(str, from, to) — char-by-char translation */
        if (strcasecmp(fn, "REPLACE") == 0) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            net_emit_expr(expr_arg(e, 2));
            N("    call       string [snobol4lib]Snobol4Lib::sno_replace(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* DUPL(str, n) — repeat string n times */
        if (strcasecmp(fn, "DUPL") == 0) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            N("    call       string [snobol4lib]Snobol4Lib::sno_dupl(string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* TRIM(str) — remove trailing whitespace */
        if (strcasecmp(fn, "TRIM") == 0) {
            net_emit_expr(expr_arg(e, 0));
            N("    call       string [snobol4lib]Snobol4Lib::sno_trim(string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* REVERSE(str) — reverse a string */
        if (strcasecmp(fn, "REVERSE") == 0) {
            net_emit_expr(expr_arg(e, 0));
            N("    call       string [snobol4lib]Snobol4Lib::sno_reverse(string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* UCASE(str) — uppercase */
        if (strcasecmp(fn, "UCASE") == 0) {
            net_emit_expr(expr_arg(e, 0));
            N("    callvirt   instance string [mscorlib]System.String::ToUpper()\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* LCASE(str) — lowercase */
        if (strcasecmp(fn, "LCASE") == 0) {
            net_emit_expr(expr_arg(e, 0));
            N("    callvirt   instance string [mscorlib]System.String::ToLower()\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* LPAD(str, n [, pad]) — left-pad to width n */
        if (strcasecmp(fn, "LPAD") == 0) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            if (expr_arg(e, 2)) net_emit_expr(expr_arg(e, 2));
            else net_ldstr(" ");
            N("    call       string [snobol4lib]Snobol4Lib::sno_lpad(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* RPAD(str, n [, pad]) — right-pad to width n */
        if (strcasecmp(fn, "RPAD") == 0) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            if (expr_arg(e, 2)) net_emit_expr(expr_arg(e, 2));
            else net_ldstr(" ");
            N("    call       string [snobol4lib]Snobol4Lib::sno_rpad(string, string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* INTEGER(x) — succeeds if x is integer string */
        if (strcasecmp(fn, "INTEGER") == 0) {
            net_emit_expr(expr_arg(e, 0));
            N("    call       int32 [snobol4lib]Snobol4Lib::sno_is_integer(string)\n");
            N("    stloc.0\n");
            /* push arg back as result value */
            net_emit_expr(expr_arg(e, 0));
            break;
        }
        /* REMDR(a, b) — integer remainder */
        if (strcasecmp(fn, "REMDR") == 0) {
            net_emit_expr(expr_arg(e, 0));
            net_emit_expr(expr_arg(e, 1));
            N("    call       string [snobol4lib]Snobol4Lib::sno_remdr(string, string)\n");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* ARRAY(n) — create indexed array, return an array-id string */
        if (strcasecmp(fn, "ARRAY") == 0) {
            net_need_array_helpers = 1;
            EXPR_t *a0 = expr_arg(e, 0);
            if (a0) net_emit_expr(a0); else net_ldstr("1");
            N("    call       string %s::net_array_new(string)\n", net_classname);
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* TABLE([n]) — create associative table, return a table-id string */
        if (strcasecmp(fn, "TABLE") == 0) {
            net_need_array_helpers = 1;
            net_ldstr("0");
            N("    call       string %s::net_array_new(string)\n", net_classname);
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* DATA('proto') — register a data type, return "" */
        if (strcasecmp(fn, "DATA") == 0) {
            net_need_data_helpers = 1;
            EXPR_t *a0 = expr_arg(e, 0);
            if (a0) net_emit_expr(a0); else net_ldstr("");
            N("    call       void %s::net_data_define(string)\n", net_classname);
            net_ldstr("");
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            break;
        }
        /* DATA type constructor call: typename(field1val, ...) */
        {
            const NetDataType *dt = net_find_data_type(fn);
            if (dt) {
                net_need_data_helpers  = 1;
                net_need_array_helpers = 1;
                /* allocate new array (size=0 → plain table) */
                net_ldstr("0");
                N("    call       string %s::net_array_new(string)\n", net_classname);
                /* store each field */
                for (int fi = 0; fi < dt->nfields; fi++) {
                    N("    dup\n");
                    net_ldstr(dt->fields[fi]);
                    EXPR_t *fv = expr_arg(e, fi);
                    if (fv) net_emit_expr(fv); else net_ldstr("");
                    N("    call       void %s::net_array_put(string, string, string)\n", net_classname);
                }
                /* store __type__ */
                N("    dup\n");
                net_ldstr("__type__");
                net_ldstr(fn);
                N("    call       void %s::net_array_put(string, string, string)\n", net_classname);
                N("    ldc.i4.1\n");
                N("    stloc.0\n");
                break;
            }
        }
        /* DATA field accessor: fieldname(instance) → field value */
        {
            const NetDataType *ft = net_find_data_field(fn);
            if (ft) {
                net_need_data_helpers  = 1;
                net_need_array_helpers = 1;
                EXPR_t *inst = expr_arg(e, 0);
                if (inst) net_emit_expr(inst); else net_ldstr("");
                net_ldstr(fn);
                N("    call       string %s::net_array_get(string, string)\n", net_classname);
                N("    ldc.i4.1\n");
                N("    stloc.0\n");
                break;
            }
        }
        /* User-defined function call */
        {
            const NetFnDef *ufn = net_find_fn(fn);
            if (ufn) {
                int na = expr_nargs(e);
                for (int i = 0; i < ufn->nargs && i < na; i++)
                    net_emit_expr(expr_arg(e, i));
                /* pad missing args with "" */
                for (int i = na; i < ufn->nargs; i++) net_ldstr("");
                N("    call       string %s::net_fn_%s(", net_classname, ufn->name);
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
                N("    brtrue     Nfc_%d_ok\n", net_pat_uid);
                N("    pop\n");
                N("    ldstr      \"\"\n");
                N("  Nfc_%d_ok:\n", net_pat_uid++);
                break;
            }
        }
        /* Unhandled FNC — stub */
        NC("unhandled E_FNC stub");
        net_ldstr("");
        break;
    }
    case E_CONC: {
        /* String concatenation with goal-directed short-circuit.
         * After each child that can set local 0 = 0 (failure), check and branch
         * to the statement fail label WITHOUT leaving any string on the stack.
         * Stack discipline: after the check+branch the stack depth is the same
         * as it was before this E_CONC started (zero strings contributed).
         * On success path: one accumulated string remains on stack (as expected).
         *
         * CIL stack balance rule: every branch target must have consistent depth.
         * Fail path: pop all accumulated strings, branch with depth unchanged.
         * Success path: one string on stack (the concat result).
         */
        static int _cconc_uid = 0;
        int conc_id = _cconc_uid++;
        if (expr_nargs(e) == 0) { net_ldstr(""); break; }
        /* depth_on_stack: how many strings we have accumulated on the eval stack
         * when we reach a fail check.  We must pop this many before branching. */
        net_emit_expr(expr_arg(e, 0));
        /* depth = 1 (one string from first child) */
        if (net_expr_can_fail(expr_arg(e, 0)) && net_cur_stmt_fail_label[0]) {
            char lbl_ok[64]; snprintf(lbl_ok, sizeof lbl_ok, "Ncc%d_ok0", conc_id);
            N("    ldloc.0\n");
            N("    brtrue     %s\n", lbl_ok);
            /* fail: 1 string on stack — pop it, then branch with depth=0 */
            N("    pop\n");
            N("    br         L_%s\n", net_cur_stmt_fail_label);
            N("  %s:\n", lbl_ok);
        }
        for (int i = 1; i < expr_nargs(e); i++) {
            net_emit_expr(expr_arg(e, i));
            /* stack: accumulated(1) + new_child(1) = 2 strings before Concat */
            if (net_expr_can_fail(expr_arg(e, i)) && net_cur_stmt_fail_label[0]) {
                char lbl_ok[64]; snprintf(lbl_ok, sizeof lbl_ok, "Ncc%d_ok%d", conc_id, i);
                N("    ldloc.0\n");
                N("    brtrue     %s\n", lbl_ok);
                /* fail: 2 strings on stack (accumulated + new child) — pop both */
                N("    pop\n");
                N("    pop\n");
                N("    br         L_%s\n", net_cur_stmt_fail_label);
                N("  %s:\n", lbl_ok);
            }
            N("    call       string [mscorlib]System.String::Concat(string, string)\n");
            /* after Concat: 1 string on stack */
        }
        break;
    }
    case E_OR:
        /* Pattern alternation used as an expression value (e.g. P = 'a' | 'b').
         * The string value is a placeholder — pattern matching uses the structural
         * node tree, not this string.  Push non-empty sentinel so assignment succeeds. */
        net_ldstr("*pat*");
        break;
    case E_ADD:
        net_emit_expr(expr_arg(e, 0));
        net_emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_add(string, string)\n");
        break;
    case E_SUB:
        net_emit_expr(expr_arg(e, 0));
        net_emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_sub(string, string)\n");
        break;
    case E_MPY:
        net_emit_expr(expr_arg(e, 0));
        net_emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_mpy(string, string)\n");
        break;
    case E_DIV:
        net_emit_expr(expr_arg(e, 0));
        net_emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_div(string, string)\n");
        break;
    case E_EXPOP:
        net_emit_expr(expr_arg(e, 0));
        net_emit_expr(expr_arg(e, 1));
        N("    call       string [snobol4lib]Snobol4Lib::sno_pow(string, string)\n");
        break;
    case E_MNS:
        /* unary minus */
        net_emit_expr(expr_arg(e, 0));
        N("    call       string [snobol4lib]Snobol4Lib::sno_neg(string)\n");
        break;
    case E_ARY: {
        /* varname<subscript> — indexed array/table read */
        net_need_array_helpers = 1;
        char fn_ary[256];
        net_field_name(fn_ary, sizeof fn_ary, e->sval ? e->sval : "");
        N("    ldsfld     string %s::%s\n", net_classname, fn_ary);
        EXPR_t *sub_ary = (e->nchildren > 0 && e->children) ? e->children[0] : NULL;
        if (sub_ary) net_emit_expr(sub_ary); else net_ldstr("1");
        N("    call       string %s::net_array_get(string, string)\n", net_classname);
        break;
    }
    case E_IDX: {
        /* expr[subscript] — subscript on expression value (TABLE or DATA) */
        net_need_array_helpers = 1;
        if (e->nchildren >= 1 && e->children && e->children[0])
            net_emit_expr(e->children[0]);
        else net_ldstr("");
        if (e->nchildren >= 2 && e->children && e->children[1])
            net_emit_expr(e->children[1]);
        else net_ldstr("0");
        N("    call       string %s::net_array_get(string, string)\n", net_classname);
        break;
    }
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
    /* RETURN/FRETURN inside a function body → branch to function return labels */
    if (net_cur_fn) {
        if (strcasecmp(target, "RETURN") == 0) {
            N("    br         %s\n", net_fn_return_lbl);
            return;
        }
        if (strcasecmp(target, "FRETURN") == 0) {
            N("    br         %s\n", net_fn_freturn_lbl);
            return;
        }
    }
    if (next_lbl && strcmp(target, next_lbl) == 0) return; /* fall-through */
    N("    br         L_%s\n", target);
}

static void net_emit_branch_success(const char *target) {
    if (!target) return;
    N("    ldloc.0\n");
    if (net_cur_fn) {
        if (strcasecmp(target, "RETURN")  == 0) { N("    brtrue     %s\n", net_fn_return_lbl);  return; }
        if (strcasecmp(target, "FRETURN") == 0) { N("    brtrue     %s\n", net_fn_freturn_lbl); return; }
    }
    N("    brtrue     L_%s\n", target);
}

static void net_emit_branch_fail(const char *target) {
    if (!target) return;
    N("    ldloc.0\n");
    if (net_cur_fn) {
        if (strcasecmp(target, "RETURN")  == 0) { N("    brfalse    %s\n", net_fn_return_lbl);  return; }
        if (strcasecmp(target, "FRETURN") == 0) { N("    brfalse    %s\n", net_fn_freturn_lbl); return; }
    }
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
 * p_next_int: int32 slot counter, starts at 6
 * p_next_str: string slot counter, starts at 20
 * ----------------------------------------------------------------------- */

/* net_pat_uid defined via macro above */
static char net_pat_abort_label[128];
/* ARB backtrack label — set by ARB emitter, used by SEQ to wire continuation omega */
static char net_arb_incr_label[128];

/* Int32 slot load/store */
static void net_ldloc_i(int idx) {
    if      (idx == 0) N("    ldloc.0\n");
    else if (idx == 1) N("    ldloc.1\n");
    else if (idx == 2) N("    ldloc.2\n");
    else if (idx == 3) N("    ldloc.3\n");
    else               N("    ldloc.s    V_%d\n", idx);
}
static void net_stloc_i(int idx) {
    if      (idx == 0) N("    stloc.0\n");
    else if (idx == 1) N("    stloc.1\n");
    else if (idx == 2) N("    stloc.2\n");
    else if (idx == 3) N("    stloc.3\n");
    else               N("    stloc.s    V_%d\n", idx);
}
/* String slot load/store */
static void net_ldloc_s(int idx) { N("    ldloc.s    V_%d\n", idx); }
static void net_stloc_s(int idx) { N("    stloc.s    V_%d\n", idx); }

/* Forward declaration */
static void net_emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_next_int, int *p_next_str);

static void net_pat_emit_expr(EXPR_t *e) { net_emit_expr(e); }

static void net_emit_pat_node(EXPR_t *pat,
                               const char *gamma, const char *omega,
                               int loc_subj, int loc_cursor, int loc_len,
                               int *p_next_int, int *p_next_str) {
    if (!pat) { N("    br         %s\n", gamma); return; }

    int uid = net_pat_uid++;

    switch (pat->kind) {

    case E_QLIT: {
        const char *s = pat->sval ? pat->sval : "";
        int slen = (int)strlen(s);
        /* cursor + slen <= len? */
        net_ldloc_i(loc_cursor); N("    ldc.i4     %d\n", slen); N("    add\n");
        net_ldloc_i(loc_len); N("    bgt        %s\n", omega);
        /* subject.Substring(cursor, slen) == lit? */
        net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor); N("    ldc.i4     %d\n", slen);
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        N("    ldstr      \"%s\"\n", s);
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", omega);
        net_ldloc_i(loc_cursor); N("    ldc.i4     %d\n", slen); N("    add\n");
        net_stloc_i(loc_cursor);
        N("    br         %s\n", gamma);
        break;
    }

    case E_CONC: {
        /* SEQ: chain gamma/omega left-to-right.
         *
         * Deferred-commit for NAM(ARB,...):
         *   ARB backtracks by trying cursor+0, +1, +2 ...  If the capture
         *   target is OUTPUT, firing Console.WriteLine on every candidate
         *   produces spurious lines.  Solution:
         *     1. Pre-scan children for NAM(ARB,...) occurrences.
         *     2. If any found, the last child's gamma becomes lbl_dc
         *        (deferred-commit label) instead of the outer gamma.
         *     3. NAM(ARB,...) children store the captured substring in a
         *        temp string slot without side-effects.
         *     4. lbl_dc emits all committed stores, then branches to gamma.
         *
         * seq_omega is stored in a local buffer (not pointing into the
         * global net_arb_incr_label) so clearing that global after capture
         * does not clobber the accumulated omega value.
         */
        int nkids = expr_nargs(pat);
        if (nkids == 0) { N("    br         %s\n", gamma); break; }
        if (nkids == 1) {
            net_emit_pat_node(expr_arg(pat, 0), gamma, omega,
                              loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
            break;
        }

        /* Pre-scan: detect NAM(ARB,...) children */
        #define SEQ_MAX_DEF 8
        static int  def_slot_s[SEQ_MAX_DEF];
        static char def_var_s[SEQ_MAX_DEF][256];
        int ndef = 0;
        int has_dc = 0;
        for (int i = 0; i < nkids; i++) {
            EXPR_t *c = expr_arg(pat, i);
            if (c && c->kind == E_NAM) {
                EXPR_t *lc = expr_left(c);
                if (lc && ((lc->kind == E_FNC  && lc->sval && strcasecmp(lc->sval,"ARB")==0) ||
                           (lc->kind == E_VART && lc->sval && strcasecmp(lc->sval,"ARB")==0)))
                    has_dc = 1;
            }
        }

        char lbl_dc[64];
        snprintf(lbl_dc, sizeof lbl_dc, "Nn%d_dc", uid);
        /* true_gamma: last child aims here; if deferred commits needed, it's lbl_dc */
        const char *true_gamma = has_dc ? lbl_dc : gamma;

        /* Build mid-point label array */
        char **mids = malloc(nkids * sizeof(char *));
        for (int i = 0; i < nkids - 1; i++) {
            mids[i] = malloc(64);
            snprintf(mids[i], 64, "Nn%d_sm%d", uid, i);
        }

        char seq_omega_buf[128];
        snprintf(seq_omega_buf, sizeof seq_omega_buf, "%s", omega);
        const char *seq_omega = seq_omega_buf;

        for (int i = 0; i < nkids; i++) {
            EXPR_t *child = expr_arg(pat, i);
            /* Last child uses true_gamma (may be lbl_dc); others use mids */
            const char *kg = (i < nkids - 1) ? mids[i] : true_gamma;
            net_arb_incr_label[0] = '\0';

            /* NAM(ARB,...) — deferred capture */
            int is_nam_arb = 0;
            if (child && child->kind == E_NAM) {
                EXPR_t *lc = expr_left(child);
                if (lc && ((lc->kind == E_FNC  && lc->sval && strcasecmp(lc->sval,"ARB")==0) ||
                           (lc->kind == E_VART && lc->sval && strcasecmp(lc->sval,"ARB")==0)))
                    is_nam_arb = 1;
            }

            if (is_nam_arb && ndef < SEQ_MAX_DEF) {
                EXPR_t *arb_child = expr_left(child);
                const char *capvar = (expr_right(child) && expr_right(child)->sval)
                                     ? expr_right(child)->sval : "";
                int loc_bef = (*p_next_int)++;
                int loc_tmp = (*p_next_str)++;
                char lbl_arb_ok[64];
                snprintf(lbl_arb_ok, sizeof lbl_arb_ok, "Nn%d_%d_aok", uid, i);

                net_ldloc_i(loc_cursor); net_stloc_i(loc_bef);
                net_emit_pat_node(arb_child, lbl_arb_ok, seq_omega,
                                  loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
                N("  %s:\n", lbl_arb_ok);
                /* Tentative capture — no side-effect */
                net_ldloc_i(loc_subj); net_ldloc_i(loc_bef);
                net_ldloc_i(loc_cursor); net_ldloc_i(loc_bef); N("    sub\n");
                N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
                net_stloc_s(loc_tmp);
                N("    br         %s\n", kg);

                def_slot_s[ndef] = loc_tmp;
                snprintf(def_var_s[ndef], 256, "%s", capvar);
                ndef++;
            } else {
                net_emit_pat_node(child, kg, seq_omega,
                                  loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
            }

            /* Update seq_omega if ARB was just emitted */
            if (net_arb_incr_label[0]) {
                snprintf(seq_omega_buf, sizeof seq_omega_buf, "%s", net_arb_incr_label);
                net_arb_incr_label[0] = '\0';
            }
            if (i < nkids - 1) N("  %s:\n", mids[i]);
        }

        /* Deferred-commit block — only reachable when all SEQ children succeed */
        if (has_dc) {
            N("  %s:\n", lbl_dc);
            for (int d = 0; d < ndef; d++) {
                net_ldloc_s(def_slot_s[d]);
                if (net_is_output(def_var_s[d])) {
                    N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
                } else {
                    char fn[256]; net_field_name(fn, sizeof fn, def_var_s[d]);
                    N("    stsfld     string %s::%s\n", net_classname, fn);
                }
            }
            N("    br         %s\n", gamma);
        }
        #undef SEQ_MAX_DEF

        for (int i = 0; i < nkids - 1; i++) free(mids[i]);
        free(mids);
        break;
    }

    case E_OR: {
        /* ALT: try each arm; restore cursor on failure, try next */
        int nkids = expr_nargs(pat);
        if (nkids == 0) { N("    br         %s\n", omega); break; }
        if (nkids == 1) {
            net_emit_pat_node(expr_arg(pat, 0), gamma, omega,
                              loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
            break;
        }
        int loc_save = (*p_next_int)++;
        net_ldloc_i(loc_cursor); net_stloc_i(loc_save);
        for (int i = 0; i < nkids; i++) {
            const char *kid_omega;
            char lbl_next[64];
            if (i < nkids - 1) {
                snprintf(lbl_next, sizeof lbl_next, "Nn%d_alt_n%d", uid, i);
                kid_omega = lbl_next;
            } else {
                kid_omega = omega;
            }
            net_emit_pat_node(expr_arg(pat, i), gamma, kid_omega,
                              loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
            if (i < nkids - 1) {
                N("  %s:\n", lbl_next);
                net_ldloc_i(loc_save); net_stloc_i(loc_cursor);
            }
        }
        break;
    }

    case E_NAM: {
        int loc_before = (*p_next_int)++;
        char lbl_ok[64]; snprintf(lbl_ok, sizeof lbl_ok, "Nn%d_nam_ok", uid);
        const char *varname = (expr_right(pat) && expr_right(pat)->sval) ? expr_right(pat)->sval : "";
        net_ldloc_i(loc_cursor); net_stloc_i(loc_before);
        net_emit_pat_node(expr_left(pat), lbl_ok, omega, loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
        N("  %s:\n", lbl_ok);
        net_ldloc_i(loc_subj); net_ldloc_i(loc_before);
        net_ldloc_i(loc_cursor); net_ldloc_i(loc_before); N("    sub\n");
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        if (net_is_output(varname)) {
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
        } else {
            char fn[256]; net_field_name(fn, sizeof fn, varname);
            N("    stsfld     string %s::%s\n", net_classname, fn);
        }
        N("    br         %s\n", gamma);
        break;
    }

    case E_DOL: {
        int loc_before = (*p_next_int)++;
        char lbl_ok[64]; snprintf(lbl_ok, sizeof lbl_ok, "Nn%d_dol_ok", uid);
        net_ldloc_i(loc_cursor); net_stloc_i(loc_before);
        net_emit_pat_node(expr_left(pat), lbl_ok, omega, loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
        N("  %s:\n", lbl_ok);
        const char *varname = (expr_right(pat) && expr_right(pat)->sval) ? expr_right(pat)->sval : "";
        net_ldloc_i(loc_subj); net_ldloc_i(loc_before);
        net_ldloc_i(loc_cursor); net_ldloc_i(loc_before); N("    sub\n");
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        if (net_is_output(varname)) {
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
        } else {
            char fn[256]; net_field_name(fn, sizeof fn, varname);
            N("    stsfld     string %s::%s\n", net_classname, fn);
        }
        N("    br         %s\n", gamma);
        break;
    }

    case E_FNC: {
        const char *fname = pat->sval ? pat->sval : "";
        EXPR_t *arg0 = (expr_nargs(pat) >= 1) ? expr_arg(pat, 0) : NULL;

        if (strcasecmp(fname, "ARBNO") == 0) {
            int loc_save = (*p_next_int)++;
            char lbl_loop[64], lbl_done[64], lbl_cok[64], lbl_cfail[64];
            snprintf(lbl_loop,  sizeof lbl_loop,  "Nn%d_arb_loop", uid);
            snprintf(lbl_done,  sizeof lbl_done,  "Nn%d_arb_done", uid);
            snprintf(lbl_cok,   sizeof lbl_cok,   "Nn%d_arb_cok",  uid);
            snprintf(lbl_cfail, sizeof lbl_cfail,  "Nn%d_arb_cf",   uid);
            EXPR_t *child = arg0;
            N("  %s:\n", lbl_loop);
            net_ldloc_i(loc_cursor); net_stloc_i(loc_save);
            net_emit_pat_node(child, lbl_cok, lbl_cfail, loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
            N("  %s:\n", lbl_cok);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_save);
            N("    beq        %s\n", lbl_done);
            N("    br         %s\n", lbl_loop);
            N("  %s:\n", lbl_cfail);
            net_ldloc_i(loc_save); net_stloc_i(loc_cursor);
            N("  %s:\n", lbl_done);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "ANY") == 0) {
            int loc_ch = (*p_next_str)++;   /* string: single-char string */
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_len); N("    bge        %s\n", omega);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            net_stloc_s(loc_ch);
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            net_ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brfalse    %s\n", omega);
            net_ldloc_i(loc_cursor); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "NOTANY") == 0) {
            int loc_ch = (*p_next_str)++;
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_len); N("    bge        %s\n", omega);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            net_stloc_s(loc_ch);
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            net_ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brtrue     %s\n", omega);
            net_ldloc_i(loc_cursor); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "SPAN") == 0) {
            int loc_cs = (*p_next_str)++;   /* string: charset */
            int loc_ch = (*p_next_str)++;   /* string: char */
            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Nn%d_spn_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Nn%d_spn_dn", uid);
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            net_stloc_s(loc_cs);
            /* must match at least 1 */
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_len); N("    bge        %s\n", omega);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            net_stloc_s(loc_ch);
            net_ldloc_s(loc_cs); net_ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brfalse    %s\n", omega);
            net_ldloc_i(loc_cursor); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_cursor);
            N("  %s:\n", lbl_loop);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_len); N("    bge        %s\n", lbl_done);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            net_stloc_s(loc_ch);
            net_ldloc_s(loc_cs); net_ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brfalse    %s\n", lbl_done);
            net_ldloc_i(loc_cursor); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", lbl_loop);
            N("  %s:\n", lbl_done);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "BREAK") == 0) {
            int loc_cs = (*p_next_str)++;
            int loc_ch = (*p_next_str)++;
            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Nn%d_brk_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Nn%d_brk_dn", uid);
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            net_stloc_s(loc_cs);
            N("  %s:\n", lbl_loop);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_len); N("    bge        %s\n", omega);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            net_stloc_s(loc_ch);
            net_ldloc_s(loc_cs); net_ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brtrue     %s\n", lbl_done);
            net_ldloc_i(loc_cursor); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", lbl_loop);
            N("  %s:\n", lbl_done);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "BREAKX") == 0) {
            /* BREAKX(x): like BREAK but fails if no chars consumed (cursor unchanged).
             * Scans forward skipping chars NOT in x; if it reaches EOS, fails.
             * Unlike BREAK it requires at least one char consumed.            */
            int loc_cs   = (*p_next_str)++;
            int loc_ch   = (*p_next_str)++;
            int loc_save = (*p_next_int)++;
            char lbl_loop[64], lbl_done[64];
            snprintf(lbl_loop, sizeof lbl_loop, "Nn%d_brkx_lp", uid);
            snprintf(lbl_done, sizeof lbl_done, "Nn%d_brkx_dn", uid);
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"\"\n");
            net_stloc_s(loc_cs);
            /* Save cursor to verify progress */
            net_ldloc_i(loc_cursor); net_stloc_i(loc_save);
            N("  %s:\n", lbl_loop);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_len); N("    bge        %s\n", omega);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor);
            N("    callvirt   instance char [mscorlib]System.String::get_Chars(int32)\n");
            N("    call       string [mscorlib]System.Char::ToString(char)\n");
            net_stloc_s(loc_ch);
            net_ldloc_s(loc_cs); net_ldloc_s(loc_ch);
            N("    callvirt   instance bool [mscorlib]System.String::Contains(string)\n");
            N("    brtrue     %s\n", lbl_done);
            net_ldloc_i(loc_cursor); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", lbl_loop);
            N("  %s:\n", lbl_done);
            /* BREAKX fails if no progress (cursor == save) */
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_save);
            N("    beq        %s\n", omega);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "LEN") == 0) {
            int loc_n = (*p_next_int)++;
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            net_stloc_i(loc_n);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_n); N("    add\n");
            net_ldloc_i(loc_len); N("    bgt        %s\n", omega);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_n); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "POS") == 0) {
            int loc_n = (*p_next_int)++;
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            net_stloc_i(loc_n);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_n); N("    bne.un     %s\n", omega);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "RPOS") == 0) {
            int loc_n = (*p_next_int)++;
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            net_stloc_i(loc_n);
            net_ldloc_i(loc_len); net_ldloc_i(loc_n); N("    sub\n");
            net_ldloc_i(loc_cursor); N("    bne.un     %s\n", omega);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "TAB") == 0) {
            int loc_n = (*p_next_int)++;
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            net_stloc_i(loc_n);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_n); N("    bgt        %s\n", omega);
            net_ldloc_i(loc_n); net_ldloc_i(loc_len); N("    bgt        %s\n", omega);
            net_ldloc_i(loc_n); net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "RTAB") == 0) {
            int loc_n   = (*p_next_int)++;
            int loc_tgt = (*p_next_int)++;
            if (arg0) net_pat_emit_expr(arg0); else N("    ldstr      \"0\"\n");
            N("    call       int32 [mscorlib]System.Int32::Parse(string)\n");
            net_stloc_i(loc_n);
            net_ldloc_i(loc_len); net_ldloc_i(loc_n); N("    sub\n"); net_stloc_i(loc_tgt);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_tgt); N("    bgt        %s\n", omega);
            net_ldloc_i(loc_tgt); net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            break;
        }

        if (strcasecmp(fname, "REM") == 0) {
            net_ldloc_i(loc_len); net_stloc_i(loc_cursor);
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
            int loc_arb_save = (*p_next_int)++;
            int loc_arb_try  = (*p_next_int)++;
            char arb_loop[64], arb_incr[64], arb_fail[64];
            snprintf(arb_loop, sizeof arb_loop, "Nn%d_arb_lp",  uid);
            snprintf(arb_incr, sizeof arb_incr, "Nn%d_arb_inc", uid);
            snprintf(arb_fail, sizeof arb_fail, "Nn%d_arb_fx",  uid);
            net_ldloc_i(loc_cursor); net_stloc_i(loc_arb_save);
            N("    ldc.i4.0\n"); net_stloc_i(loc_arb_try);
            N("  %s:\n", arb_loop);
            net_ldloc_i(loc_arb_save); net_ldloc_i(loc_arb_try); N("    add\n");
            net_ldloc_i(loc_len); N("    bgt        %s\n", arb_fail);
            net_ldloc_i(loc_arb_save); net_ldloc_i(loc_arb_try); N("    add\n");
            net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            /* arb_incr: continuation failed, try one more char */
            N("  %s:\n", arb_incr);
            net_ldloc_i(loc_arb_try); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_arb_try);
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
            snprintf(net_arb_incr_label, sizeof net_arb_incr_label, "%s", arb_incr);
            break;
        }
        if (strcasecmp(fname, "FAIL")  == 0 || strcasecmp(fname, "ABORT")   == 0) {
            N("    br         %s\n", net_pat_abort_label[0] ? net_pat_abort_label : omega); break;
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
        if (strcasecmp(vname, "REM")     == 0) { net_ldloc_i(loc_len); net_stloc_i(loc_cursor); N("    br %s\n", gamma); break; }
        if (strcasecmp(vname, "ARB") == 0) {
            int loc_arb_save = (*p_next_int)++;
            int loc_arb_try  = (*p_next_int)++;
            char arb_loop[64], arb_incr[64], arb_fail[64];
            snprintf(arb_loop, sizeof arb_loop, "Nn%d_varb_lp",  uid);
            snprintf(arb_incr, sizeof arb_incr, "Nn%d_varb_inc", uid);
            snprintf(arb_fail, sizeof arb_fail, "Nn%d_varb_fx",  uid);
            net_ldloc_i(loc_cursor); net_stloc_i(loc_arb_save);
            N("    ldc.i4.0\n"); net_stloc_i(loc_arb_try);
            N("  %s:\n", arb_loop);
            net_ldloc_i(loc_arb_save); net_ldloc_i(loc_arb_try); N("    add\n");
            net_ldloc_i(loc_len); N("    bgt        %s\n", arb_fail);
            net_ldloc_i(loc_arb_save); net_ldloc_i(loc_arb_try); N("    add\n");
            net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
            N("  %s:\n", arb_incr);
            net_ldloc_i(loc_arb_try); N("    ldc.i4.1\n"); N("    add\n"); net_stloc_i(loc_arb_try);
            N("    br         %s\n", arb_loop);
            N("  %s:\n", arb_fail);
            N("    br         %s\n", omega);
            snprintf(net_arb_incr_label, sizeof net_arb_incr_label, "%s", arb_incr);
            break;
        }
        if (strcasecmp(vname, "SUCCEED") == 0) { N("    br         %s\n", gamma); break; }
        if (strcasecmp(vname, "FENCE")   == 0) { N("    br         %s\n", gamma); break; }
        if (strcasecmp(vname, "FAIL")    == 0 || strcasecmp(vname, "ABORT") == 0) {
            N("    br         %s\n", net_pat_abort_label[0] ? net_pat_abort_label : omega); break;
        }
        /* Check named-pattern registry — inline expand if registered */
        {
            const NetNamedPat *np = net_named_pat_lookup(vname);
            if (np && np->pat) {
                net_emit_pat_node(np->pat, gamma, omega,
                                  loc_subj, loc_cursor, loc_len, p_next_int, p_next_str);
                break;
            }
        }
        /* Otherwise: load variable's string value, match as literal */
        {
            int loc_lit  = (*p_next_str)++;
            int loc_llen = (*p_next_int)++;
            char fn[256]; net_field_name(fn, sizeof fn, vname);
            N("    ldsfld     string %s::%s\n", net_classname, fn);
            net_stloc_s(loc_lit);
            net_ldloc_s(loc_lit);
            N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
            net_stloc_i(loc_llen);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_llen); N("    add\n");
            net_ldloc_i(loc_len); N("    bgt        %s\n", omega);
            net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor); net_ldloc_i(loc_llen);
            N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
            net_ldloc_s(loc_lit);
            N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
            N("    brfalse    %s\n", omega);
            net_ldloc_i(loc_cursor); net_ldloc_i(loc_llen); N("    add\n"); net_stloc_i(loc_cursor);
            N("    br         %s\n", gamma);
        }
        break;
    }

    case E_INDR: {
        /* *VAR — indirect pattern ref: get variable name, load its value,
         * match the value as a literal string (same logic as VART literal path).
         * Inner child (children[1] or children[0]) holds the E_VART whose sval
         * is the variable NAME (not value) to dereference.                      */
        EXPR_t *inner = (pat->nchildren > 1 && pat->children[1])
                        ? pat->children[1] : pat->children[0];
        const char *vname = (inner && inner->sval) ? inner->sval : "";
        int loc_lit  = (*p_next_str)++;
        int loc_llen = (*p_next_int)++;
        char fn[256]; net_field_name(fn, sizeof fn, vname);
        N("    ldsfld     string %s::%s\n", net_classname, fn);
        net_stloc_s(loc_lit);
        net_ldloc_s(loc_lit);
        N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
        net_stloc_i(loc_llen);
        net_ldloc_i(loc_cursor); net_ldloc_i(loc_llen); N("    add\n");
        net_ldloc_i(loc_len); N("    bgt        %s\n", omega);
        net_ldloc_i(loc_subj); net_ldloc_i(loc_cursor); net_ldloc_i(loc_llen);
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)\n");
        net_ldloc_s(loc_lit);
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", omega);
        net_ldloc_i(loc_cursor); net_ldloc_i(loc_llen); N("    add\n"); net_stloc_i(loc_cursor);
        N("    br         %s\n", gamma);
        break;
    }

    case E_ATP: {
        /* @VAR — capture current cursor position (0-based) into var.
         * Zero-width: does not advance cursor. Always succeeds.
         * Stores integer string of current cursor position.
         * Parser builds @VAR as unop(E_ATP, E_VART("VAR")), varname in children[0]->sval. */
        const char *varname = (expr_left(pat) && expr_left(pat)->sval) ? expr_left(pat)->sval
                            : (pat->sval ? pat->sval : "");
        /* Convert cursor (int32) to string via Int32.ToString() */
        net_ldloc_i(loc_cursor);
        N("    box        [mscorlib]System.Int32\n");
        N("    callvirt   instance string object::ToString()\n");
        if (net_is_output(varname)) {
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
        } else {
            char fn[256]; net_field_name(fn, sizeof fn, varname);
            N("    stsfld     string %s::%s\n", net_classname, fn);
        }
        N("    br         %s\n", gamma);
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

static void net_emit_one_stmt(STMT_t *s, const char *next_lbl) {
    const char *tgt_s = s->go ? s->go->onsuccess : NULL;
    const char *tgt_f = s->go ? s->go->onfailure : NULL;
    const char *tgt_u = s->go ? s->go->uncond    : NULL;
    net_cur_stmt_fail_label[0] = '\0';  /* reset per-stmt */

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

    /* Case 1: pure assignment — subject is a variable or OUTPUT, has_eq set, NO pattern */
    if (s->has_eq && s->subject && !s->pattern &&
        (s->subject->kind == E_VART || s->subject->kind == E_KW)) {

        int is_out = net_is_output(s->subject->sval);
        EXPR_t *rhs = s->replacement;

        /* Set fail label so INPUT/CONC inside rhs can branch directly on failure.
         * When tgt_f is NULL, generate a local skip label so that a failing
         * sub-expression in the RHS (e.g. DIFFER()) can still abort the assignment. */
        static int _asgn_uid = 0;
        char local_fail_lbl[64];
        int need_local_fail = 0;
        if (tgt_f) {
            snprintf(net_cur_stmt_fail_label, sizeof net_cur_stmt_fail_label, "%s", tgt_f);
        } else {
            snprintf(local_fail_lbl, sizeof local_fail_lbl, "Nasgn%d_skip", _asgn_uid++);
            snprintf(net_cur_stmt_fail_label, sizeof net_cur_stmt_fail_label, "%s", local_fail_lbl);
            need_local_fail = 1;
        }

        /* eval RHS onto stack */
        net_emit_expr(rhs);
        net_cur_stmt_fail_label[0] = '\0';  /* reset after expr */

        if (is_out) {
            /* OUTPUT = expr → Console.WriteLine + monitor */
            N("    dup\n");
            N("    call       void [mscorlib]System.Console::WriteLine(string)\n");
            N("    stloc.s    V_20\n");
            N("    ldstr      \"OUTPUT\"\n");
            N("    ldloc.s    V_20\n");
            N("    call       void %s::net_monitor_write(string, string)\n", net_classname);
        } else if (s->subject->kind == E_KW) {
            /* &KEYWORD = expr — write to known keyword field */
            const char *kw = s->subject->sval ? s->subject->sval : "";
            if (strcasecmp(kw, "ANCHOR") == 0)
                N("    stsfld     string %s::kw_anchor\n", net_classname);
            else if (strcasecmp(kw, "TRIM") == 0)
                N("    pop\n");  /* &TRIM: ignore for now (plain pop) */
            else
                N("    pop\n");  /* unknown keyword: discard */
        } else {
            /* VAR = expr → stsfld + monitor */
            char fn[256];
            net_field_name(fn, sizeof fn, s->subject->sval);
            N("    dup\n");
            N("    stsfld     string %s::%s\n", net_classname, fn);
            N("    stloc.s    V_20\n");
            N("    ldstr      \"%s\"\n", s->subject->sval);
            N("    ldloc.s    V_20\n");
            N("    call       void %s::net_monitor_write(string, string)\n", net_classname);
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
        /* emit local skip label if we generated one for RHS failure */
        if (need_local_fail)
            N("  L_%s:\n", local_fail_lbl);
        return;
    }

    /* Case 1b: indirect assignment — $expr = val */
    if (s->has_eq && s->subject && s->subject->kind == E_INDR && !s->pattern) {
        EXPR_t *indr_operand = expr_arg(s->subject, 0);
        EXPR_t *rhs = s->replacement;
        /* push name string */
        net_emit_expr(indr_operand);
        /* push value string */
        if (!rhs || rhs->kind == E_NULV) net_ldstr("");
        else net_emit_expr(rhs);
        N("    call       void %s::net_indr_set(string, string)\n", net_classname);
        N("    ldc.i4.1\n");
        N("    stloc.0\n");
        if (tgt_u) net_emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) net_emit_branch_success(tgt_s);
            if (tgt_f) net_emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 1c: array/table subscript assignment — A<I> = expr or T<'key'> = expr */
    if (s->has_eq && s->subject &&
        (s->subject->kind == E_ARY || s->subject->kind == E_IDX) &&
        !s->pattern) {
        net_need_array_helpers = 1;
        if (s->subject->kind == E_ARY) {
            /* push array-id from the variable holding the array */
            char fn_ary[256];
            net_field_name(fn_ary, sizeof fn_ary, s->subject->sval ? s->subject->sval : "");
            N("    ldsfld     string %s::%s\n", net_classname, fn_ary);
            /* push subscript */
            EXPR_t *sub = (s->subject->nchildren > 0 && s->subject->children)
                          ? s->subject->children[0] : NULL;
            if (sub) net_emit_expr(sub); else net_ldstr("1");
        } else { /* E_IDX */
            if (s->subject->nchildren >= 1 && s->subject->children && s->subject->children[0])
                net_emit_expr(s->subject->children[0]);
            else net_ldstr("");
            if (s->subject->nchildren >= 2 && s->subject->children && s->subject->children[1])
                net_emit_expr(s->subject->children[1]);
            else net_ldstr("0");
        }
        /* push value */
        if (!s->replacement || s->replacement->kind == E_NULV) net_ldstr("");
        else net_emit_expr(s->replacement);
        N("    call       void %s::net_array_put(string, string, string)\n", net_classname);
        N("    ldc.i4.1\n");
        N("    stloc.0\n");
        if (tgt_u) net_emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) net_emit_branch_success(tgt_s);
            if (tgt_f) net_emit_branch_fail(tgt_f);
        }
        return;
    }

    /* Case 1d: DATA field setter — fieldname(instance) = val */
    if (s->has_eq && s->subject && s->subject->kind == E_FNC && !s->pattern) {
        const char *sfname = s->subject->sval ? s->subject->sval : "";
        const NetDataType *ft = net_find_data_field(sfname);
        if (ft) {
            net_need_data_helpers  = 1;
            net_need_array_helpers = 1;
            /* push instance-id */
            EXPR_t *inst = expr_arg(s->subject, 0);
            if (inst) net_emit_expr(inst); else net_ldstr("");
            /* push field name */
            net_ldstr(sfname);
            /* push value */
            if (!s->replacement || s->replacement->kind == E_NULV) net_ldstr("");
            else net_emit_expr(s->replacement);
            N("    call       void %s::net_array_put(string, string, string)\n", net_classname);
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            if (tgt_u) net_emit_goto(tgt_u, next_lbl);
            else {
                if (tgt_s) net_emit_branch_success(tgt_s);
                if (tgt_f) net_emit_branch_fail(tgt_f);
            }
            return;
        }
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

    /* Case 4: pattern replacement — has_eq=1, pattern present: X 'pat' = 'repl' */
    if (s->has_eq && s->subject && s->pattern) {
        static int net_prepl_uid = 0;
        int suid = net_prepl_uid++;

        char lbl_tok[64], lbl_tfail[64], lbl_retry[64], lbl_end[64];
        snprintf(lbl_tok,   sizeof lbl_tok,   "Npr%d_tok",   suid);
        snprintf(lbl_tfail, sizeof lbl_tfail,  "Npr%d_fail",  suid);
        snprintf(lbl_retry, sizeof lbl_retry,  "Npr%d_retry", suid);
        snprintf(lbl_end,   sizeof lbl_end,    "Npr%d_end",   suid);
        snprintf(net_pat_abort_label, sizeof net_pat_abort_label, "Npr%d_abort", suid);

        int loc_subj   = 2;
        int loc_cursor = 3;
        int loc_len    = 4;
        int loc_mstart = 5;
        int next_int = 6;
        int next_str = 20;

        /* load subject into V_2 */
        net_emit_expr(s->subject);
        N("    stloc.s    V_2\n");
        N("    ldloc.s    V_2\n");
        N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
        N("    stloc.s    V_4\n");
        N("    ldc.i4.0\n");
        N("    stloc.s    V_3\n");

        N("  %s:\n", lbl_retry);
        N("    ldloc.s    V_3\n");
        N("    stloc.s    V_5\n");

        net_emit_pat_node(s->pattern, lbl_tok, lbl_tfail,
                          loc_subj, loc_cursor, loc_len, &next_int, &next_str);

        /* fail path */
        N("  %s:\n", lbl_tfail);
        N("    ldsfld     string %s::kw_anchor\n", net_classname);
        N("    ldstr      \"0\"\n");
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", net_pat_abort_label);
        N("    ldloc.s    V_5\n");
        N("    ldc.i4.1\n");
        N("    add\n");
        N("    stloc.s    V_3\n");
        N("    ldloc.s    V_3\n");
        N("    ldloc.s    V_4\n");
        N("    ble        %s\n", lbl_retry);
        N("  %s:\n", net_pat_abort_label);
        N("    ldc.i4.0\n");
        N("    stloc.0\n");
        if (tgt_f) net_emit_branch_fail(tgt_f);
        else if (tgt_u) net_emit_goto(tgt_u, next_lbl);
        N("    br         %s\n", lbl_end);

        /* success path — replace matched region in subject */
        N("  %s:\n", lbl_tok);
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
            net_ldstr("");
        else
            net_emit_expr(s->replacement);
        N("    call       string [mscorlib]System.String::Concat(string, string)\n");
        /* suffix = subject[cursor .. end] */
        N("    ldloc.s    V_2\n");
        N("    ldloc.s    V_3\n");       /* cursor = end of match */
        N("    callvirt   instance string [mscorlib]System.String::Substring(int32)\n");
        N("    call       string [mscorlib]System.String::Concat(string, string)\n");
        /* store back to subject variable */
        if (s->subject->kind == E_VART) {
            char fn[256]; net_field_name(fn, sizeof fn, s->subject->sval);
            N("    stsfld     string %s::%s\n", net_classname, fn);
        } else {
            N("    pop\n");  /* can't assign to non-var */
        }
        if (tgt_u) net_emit_goto(tgt_u, next_lbl);
        else {
            if (tgt_s) net_emit_branch_success(tgt_s);
            if (tgt_f) net_emit_branch_fail(tgt_f);
        }
        N("  %s:\n", lbl_end);
        return;
    }

    /* Case 3: pattern match — subject has_eq=0, subject=var/expr, pattern present */
    if (!s->has_eq && s->subject && s->pattern) {
        static int net_pat_stmt_uid = 0;
        int suid = net_pat_stmt_uid++;

        char lbl_tok[64], lbl_tfail[64], lbl_retry[64];
        snprintf(lbl_tok,   sizeof lbl_tok,   "Npat%d_tok",   suid);
        snprintf(lbl_tfail, sizeof lbl_tfail,  "Npat%d_fail",  suid);
        snprintf(lbl_retry, sizeof lbl_retry,  "Npat%d_retry", suid);
        snprintf(net_pat_abort_label, sizeof net_pat_abort_label, "Npat%d_abort", suid);

        /* locals: V_0=success, V_1=unused,
         *         V_2=subj(string), V_3=cursor(int32), V_4=len(int32),
         *         V_5=mstart(int32), V_6..=pattern temporaries */
        int loc_subj   = 2;
        int loc_cursor = 3;
        int loc_len    = 4;
        int loc_mstart = 5;
        int next_int = 6;   /* int32 slots: V_6..V_19 */
        int next_str = 20;  /* string slots: V_20..V_29 */

        /* load subject */
        net_emit_expr(s->subject);
        N("    stloc.s    V_2\n");   /* loc_subj */
        N("    ldloc.s    V_2\n");
        N("    callvirt   instance int32 [mscorlib]System.String::get_Length()\n");
        N("    stloc.s    V_4\n");   /* loc_len */
        N("    ldc.i4.0\n");
        N("    stloc.s    V_3\n");   /* loc_cursor = 0 */

        /* retry loop: scan start position */
        N("  %s:\n", lbl_retry);
        N("    ldloc.s    V_3\n");
        N("    stloc.s    V_5\n");   /* mstart = cursor */

        net_emit_pat_node(s->pattern, lbl_tok, lbl_tfail,
                          loc_subj, loc_cursor, loc_len, &next_int, &next_str);

        N("  %s:\n", lbl_tfail);
        /* if anchor, fail; else advance start and retry */
        N("    ldsfld     string %s::kw_anchor\n", net_classname);
        N("    ldstr      \"0\"\n");
        N("    call       bool [mscorlib]System.String::op_Equality(string, string)\n");
        N("    brfalse    %s\n", net_pat_abort_label);  /* anchored → fail */
        N("    ldloc.s    V_5\n");  /* mstart */
        N("    ldc.i4.1\n");
        N("    add\n");
        N("    stloc.s    V_3\n");  /* cursor = mstart+1 */
        N("    ldloc.s    V_3\n");
        N("    ldloc.s    V_4\n");
        N("    ble        %s\n", lbl_retry);
        /* fell off end — fail */
        N("  %s:\n", net_pat_abort_label);
        N("    ldc.i4.0\n");
        N("    stloc.0\n");
        if (tgt_f) net_emit_branch_fail(tgt_f);
        else if (tgt_u) net_emit_goto(tgt_u, next_lbl);
        /* fall through on fail */
        /* jump past success block */
        {
            char lbl_end[64]; snprintf(lbl_end, sizeof lbl_end, "Npat%d_end", suid);
            N("    br         %s\n", lbl_end);
            N("  %s:\n", lbl_tok);
            N("    ldc.i4.1\n");
            N("    stloc.0\n");
            if (tgt_u) net_emit_goto(tgt_u, next_lbl);
            else {
                if (tgt_s) net_emit_branch_success(tgt_s);
                if (tgt_f) net_emit_branch_fail(tgt_f);
            }
            N("  %s:\n", lbl_end);
        }
        return;
    }

    /* No-op / unhandled statement — emit nop + goto only */
    N("    nop\n");
    if (tgt_u) net_emit_goto(tgt_u, next_lbl);
}

/* Returns 1 if statement s is inside any user function body. */
static int net_stmt_in_any_fn(STMT_t *s) {
    for (int fi = 0; fi < net_fn_count; fi++) {
        const NetFnDef *fn = &net_fn_table[fi];
        const char *entry = fn->entry_label[0] ? fn->entry_label : fn->name;
        int in_body = 0;
        /* Walk program to find whether s is between entry and end_label */
        /* We need prog here — use a simple label-range check instead */
        (void)entry; (void)in_body;
    }
    return 0;  /* determined per-statement during emit via in_fn_body flag below */
}

static void net_emit_stmts(Program *prog) {
    if (!prog || !prog->head) return;

    STMT_t *stmts[4096];
    int n = 0;
    for (STMT_t *s = prog->head; s && n < 4095; s = s->next)
        stmts[n++] = s;

    /* Build a bitmask of which statements are inside function bodies */
    char in_fn_body[4096];
    memset(in_fn_body, 0, sizeof in_fn_body);
    for (int fi = 0; fi < net_fn_count; fi++) {
        const NetFnDef *fn = &net_fn_table[fi];
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
        /* Skip statements inside function bodies — they are emitted by net_emit_fn_method */
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
        N("    ldsfld     string %s::kw_stno\n", net_classname);
        N("    ldstr      \"1\"\n");
        N("    call       string [snobol4lib]Snobol4Lib::sno_add(string, string)\n");
        N("    stsfld     string %s::kw_stno\n", net_classname);
        net_emit_one_stmt(s, next_lbl);
    }
}

/* helpers live in snobol4lib.dll — see src/runtime/net/snobol4lib.il */

/* -----------------------------------------------------------------------
 * Header / footer emitters
 * ----------------------------------------------------------------------- */

static void net_emit_header(Program *prog) {
    N(".assembly extern mscorlib {}\n");
    N(".assembly extern snobol4lib {}\n");
    N(".assembly extern snobol4run {}\n");
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
    /* &STNO and &ANCHOR keyword fields */
    N("  .field static string kw_stno\n");
    N("  .field static string kw_anchor\n");
    N("  .field static class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> sno_vars\n");
    N("  .field static class [mscorlib]System.Collections.Generic.Dictionary`2<string,class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>> sno_arrays\n");
    N("  .field static class [mscorlib]System.IO.StreamWriter sno_monitor_out\n");
    N("\n");

    /* Static initialiser: set all variables to "" */
    {
        N("  .method static void .cctor() cil managed\n");
        N("  {\n");
        N("    .maxstack 5\n");
        for (int i = 0; i < net_nvar; i++) {
            char fn[256];
            net_field_name(fn, sizeof fn, net_vars[i]);
            N("    ldstr      \"\"\n");
            N("    stsfld     string %s::%s\n", net_classname, fn);
        }
        N("    ldstr      \"0\"\n");
        N("    stsfld     string %s::kw_stno\n", net_classname);
        N("    ldstr      \"0\"\n");
        N("    stsfld     string %s::kw_anchor\n", net_classname);
        N("    newobj     instance void class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>::.ctor()\n");
        N("    stsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> %s::sno_vars\n", net_classname);
        N("    newobj     instance void class [mscorlib]System.Collections.Generic.Dictionary`2<string,class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>>::.ctor()\n");
        N("    stsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>> %s::sno_arrays\n", net_classname);
        /* open MONITOR_FIFO if set */
        N("    ldstr      \"MONITOR_FIFO\"\n");
        N("    call       string [mscorlib]System.Environment::GetEnvironmentVariable(string)\n");
        N("    dup\n");
        N("    brfalse    Ncctor_no_mon\n");
        N("    ldc.i4.3\n");   /* FileMode.Open */
        N("    ldc.i4.2\n");   /* FileAccess.Write */
        N("    newobj     instance void [mscorlib]System.IO.FileStream::.ctor(string, valuetype [mscorlib]System.IO.FileMode, valuetype [mscorlib]System.IO.FileAccess)\n");
        N("    newobj     instance void [mscorlib]System.IO.StreamWriter::.ctor(class [mscorlib]System.IO.Stream)\n");
        N("    dup\n");
        N("    ldc.i4.1\n");
        N("    callvirt   instance void [mscorlib]System.IO.StreamWriter::set_AutoFlush(bool)\n");
        N("    stsfld     class [mscorlib]System.IO.StreamWriter %s::sno_monitor_out\n", net_classname);
        N("    br         Ncctor_mon_done\n");
        N("  Ncctor_no_mon:\n");
        N("    pop\n");
        N("    ldnull\n");
        N("    stsfld     class [mscorlib]System.IO.StreamWriter %s::sno_monitor_out\n", net_classname);
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
    N("    ldsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> %s::sno_vars\n", net_classname);
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
    N("    ldsfld     class [mscorlib]System.Collections.Generic.Dictionary`2<string,string> %s::sno_vars\n", net_classname);
    N("    ldarg.0\n");
    N("    ldarg.1\n");
    N("    callvirt   instance void class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>::set_Item(!0, !1)\n");
    // Also update static field via reflection
    N("    ldtoken    %s\n", net_classname);
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
    N("    ldsfld     class [mscorlib]System.IO.StreamWriter %s::sno_monitor_out\n", net_classname);
    N("    stloc      V_mon\n");
    N("    ldloc      V_mon\n");
    N("    brfalse    Nmw_done\n");
    N("    ldloc      V_mon\n");
    N("    ldstr      \"VAR \"\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    N("    ldloc      V_mon\n");
    N("    ldarg.0\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    N("    ldloc      V_mon\n");
    N("    ldstr      \" \\u0022\"\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    N("    ldloc      V_mon\n");
    N("    ldarg.1\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::Write(string)\n");
    N("    ldloc      V_mon\n");
    N("    ldstr      \"\\u0022\"\n");
    N("    callvirt   instance void [mscorlib]System.IO.TextWriter::WriteLine(string)\n");
    N("  Nmw_done:\n");
    N("    ret\n");
    N("  }\n\n");
    (void)prog;
}

static void net_emit_main_open(void) {
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

static void net_emit_main_close(void) {
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
/* NetFnDef, net_fn_table, net_fn_count, net_cur_fn declared at top of file */

static const NetFnDef *net_find_fn(const char *name) {
    for (int i = 0; i < net_fn_count; i++)
        if (net_fn_table[i].name[0] && strcasecmp(net_fn_table[i].name, name) == 0)
            return &net_fn_table[i];
    return NULL;
}

static const NetDataType *net_find_data_type(const char *name) {
    for (int i = 0; i < net_data_type_count; i++)
        if (net_data_types[i].type_name[0] && strcasecmp(net_data_types[i].type_name, name) == 0)
            return &net_data_types[i];
    return NULL;
}

static const NetDataType *net_find_data_field(const char *field) {
    for (int i = 0; i < net_data_type_count; i++)
        for (int j = 0; j < net_data_types[i].nfields; j++)
            if (net_data_types[i].fields[j][0] &&
                strcasecmp(net_data_types[i].fields[j], field) == 0)
                return &net_data_types[i];
    return NULL;
}

/* Parse DEFINE prototype string "fname(a,b)l1,l2" into NetFnDef. */
static int net_parse_proto(const char *proto, NetFnDef *fn) {
    fn->nargs = 0; fn->nlocals = 0;
    const char *p = proto;
    int ni = 0;
    while (*p && *p != '(' && ni < NET_FN_NAMELEN - 1)
        fn->name[ni++] = *p++;
    fn->name[ni] = '\0';
    /* trim whitespace */
    while (ni > 0 && (fn->name[ni-1]==' '||fn->name[ni-1]=='\t')) fn->name[--ni] = '\0';
    if (!ni) return 0;
    /* default entry label = name */
    snprintf(fn->entry_label, NET_FN_NAMELEN, "%s", fn->name);
    if (*p != '(') return 1;
    p++; /* skip '(' */
    while (*p && *p != ')') {
        while (*p==' '||*p=='\t') p++;
        if (*p==')'||!*p) break;
        int ai=0;
        while (*p && *p!=',' && *p!=')' && ai < NET_FN_NAMELEN-1)
            fn->args[fn->nargs][ai++] = *p++;
        while (ai>0 && (fn->args[fn->nargs][ai-1]==' '||fn->args[fn->nargs][ai-1]=='\t')) ai--;
        fn->args[fn->nargs][ai] = '\0';
        if (ai>0 && fn->nargs < NET_FN_ARGMAX) fn->nargs++;
        if (*p==',') p++;
    }
    if (*p==')') p++;
    /* locals after ')' */
    while (*p) {
        while (*p==' '||*p=='\t'||*p==',') p++;
        if (!*p) break;
        int li=0;
        while (*p && *p!=',' && li < NET_FN_NAMELEN-1)
            fn->locals[fn->nlocals][li++] = *p++;
        while (li>0 && (fn->locals[fn->nlocals][li-1]==' '||fn->locals[fn->nlocals][li-1]=='\t')) li--;
        fn->locals[fn->nlocals][li] = '\0';
        if (li>0 && fn->nlocals < NET_FN_ARGMAX) fn->nlocals++;
    }
    return 1;
}

/* Flatten a simple literal/var expr to a string buffer (for DEFINE arg parsing). */
static const char *net_flatten_str(EXPR_t *e, char *buf, int sz) {
    if (!e) return NULL;
    if (e->kind == E_QLIT && e->sval) { snprintf(buf, sz, "%s", e->sval); return buf; }
    if (e->kind == E_VART && e->sval) { snprintf(buf, sz, "%s", e->sval); return buf; }
    return NULL;
}

static void net_scan_fndefs(Program *prog) {
    net_fn_count = 0;
    net_data_type_count = 0;
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject || s->subject->kind != E_FNC) continue;
        const char *sname = s->subject->sval ? s->subject->sval : "";
        /* Collect DATA type definitions */
        if (strcasecmp(sname, "DATA") == 0) {
            if (expr_nargs(s->subject) < 1) continue;
            char pbuf[256];
            const char *proto = net_flatten_str(expr_arg(s->subject, 0), pbuf, sizeof pbuf);
            if (!proto || net_data_type_count >= NET_DATA_MAX) continue;
            NetDataType *dt = &net_data_types[net_data_type_count];
            memset(dt, 0, sizeof *dt);
            int pi = 0, ti = 0;
            while (proto[pi] && proto[pi] != '(' && ti < NET_FN_NAMELEN-1)
                dt->type_name[ti++] = proto[pi++];
            dt->type_name[ti] = '\0';
            while (ti > 0 && (dt->type_name[ti-1]==' '||dt->type_name[ti-1]=='\t'))
                dt->type_name[--ti] = '\0';
            if (proto[pi] == '(') {
                pi++;
                while (proto[pi] && proto[pi] != ')') {
                    while (proto[pi]==' '||proto[pi]=='\t') pi++;
                    if (proto[pi]==')'||!proto[pi]) break;
                    int j = 0; char fb[NET_FN_NAMELEN];
                    while (proto[pi] && proto[pi]!=',' && proto[pi]!=')' && j < NET_FN_NAMELEN-1)
                        fb[j++] = proto[pi++];
                    while (j > 0 && (fb[j-1]==' '||fb[j-1]=='\t')) j--;
                    fb[j] = '\0';
                    if (j > 0 && dt->nfields < NET_FN_ARGMAX)
                        snprintf(dt->fields[dt->nfields++], NET_FN_NAMELEN, "%s", fb);
                    if (proto[pi]==',') pi++;
                }
            }
            net_data_type_count++;
            continue;
        }
        if (strcasecmp(sname, "DEFINE") != 0) continue;
        if (expr_nargs(s->subject) < 1) continue;
        char pbuf[256];
        const char *proto = net_flatten_str(expr_arg(s->subject, 0), pbuf, sizeof pbuf);
        if (!proto) continue;
        if (net_fn_count >= NET_FN_MAX) break;
        NetFnDef *fn = &net_fn_table[net_fn_count];
        memset(fn, 0, sizeof *fn);
        if (!net_parse_proto(proto, fn)) continue;
        /* Optional 2nd arg: explicit entry label */
        if (expr_nargs(s->subject) >= 2) {
            char ebuf[128];
            const char *el = net_flatten_str(expr_arg(s->subject, 1), ebuf, sizeof ebuf);
            if (el && el[0]) snprintf(fn->entry_label, NET_FN_NAMELEN, "%s", el);
        }
        /* end_label from goto on DEFINE stmt */
        fn->end_label[0] = '\0';
        if (s->go) {
            const char *gl = s->go->uncond ? s->go->uncond
                           : s->go->onsuccess ? s->go->onsuccess : NULL;
            if (gl) snprintf(fn->end_label, NET_FN_NAMELEN, "%s", gl);
        }
        /* Register fn name, args, locals as SNOBOL4 variables (need static fields) */
        net_var_register(fn->name);
        for (int ai = 0; ai < fn->nargs; ai++) net_var_register(fn->args[ai]);
        for (int li = 0; li < fn->nlocals; li++) net_var_register(fn->locals[li]);
        net_fn_count++;
    }
}

/* Emit one user function as a static CIL method. */
static void net_emit_fn_method(const NetFnDef *fn, Program *prog, int fn_idx) {
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

    /* Save old arg values — direct ldsfld, no reflection */
    for (int i = 0; i < fn->nargs; i++) {
        char af[256]; net_field_name(af, sizeof af, fn->args[i]);
        N("    ldsfld     string %s::%s\n", net_classname, af);
        N("    stloc.s    V_%d\n", save_base + i);
    }
    /* Save old local values — direct ldsfld */
    for (int i = 0; i < fn->nlocals; i++) {
        char lf[256]; net_field_name(lf, sizeof lf, fn->locals[i]);
        N("    ldsfld     string %s::%s\n", net_classname, lf);
        N("    stloc.s    V_%d\n", save_base + fn->nargs + i);
    }
    /* Save old fn-name value — direct ldsfld */
    {
        char ff[256]; net_field_name(ff, sizeof ff, fn->name);
        N("    ldsfld     string %s::%s\n", net_classname, ff);
        N("    stloc.s    V_%d\n", save_fnret);
    }

    /* Bind incoming args — direct stsfld, no reflection */
    for (int i = 0; i < fn->nargs; i++) {
        char af[256]; net_field_name(af, sizeof af, fn->args[i]);
        N("    ldarg.s    %d\n", i);
        N("    stsfld     string %s::%s\n", net_classname, af);
    }
    /* Init locals to "" — direct stsfld */
    for (int i = 0; i < fn->nlocals; i++) {
        char lf[256]; net_field_name(lf, sizeof lf, fn->locals[i]);
        N("    ldstr      \"\"\n");
        N("    stsfld     string %s::%s\n", net_classname, lf);
    }
    /* Init fn-name var to "" — direct stsfld */
    {
        char ff[256]; net_field_name(ff, sizeof ff, fn->name);
        N("    ldstr      \"\"\n");
        N("    stsfld     string %s::%s\n", net_classname, ff);
    }

    /* Return/freturn labels */
    snprintf(net_fn_return_lbl,  sizeof net_fn_return_lbl,  "Nfn%d_return",  fn_idx);
    snprintf(net_fn_freturn_lbl, sizeof net_fn_freturn_lbl, "Nfn%d_freturn", fn_idx);

    /* Emit function body statements */
    const char *entry = fn->entry_label[0] ? fn->entry_label : fn->name;
    int in_body = 0;
    int si = 0;
    const NetFnDef *saved_fn = net_cur_fn;
    net_cur_fn = fn;

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
        N("    ldsfld     string %s::kw_stno\n", net_classname);
        N("    ldstr      \"1\"\n");
        N("    call       string [snobol4lib]Snobol4Lib::sno_add(string, string)\n");
        N("    stsfld     string %s::kw_stno\n", net_classname);
        net_emit_one_stmt(s, next_lbl);
        si++;
    }
    net_cur_fn = saved_fn;

    /* RETURN path: restore args/locals via direct stsfld, return fn->name value */
    N("  %s:\n", net_fn_return_lbl);
    for (int i = 0; i < fn->nargs; i++) {
        char af[256]; net_field_name(af, sizeof af, fn->args[i]);
        N("    ldloc.s    V_%d\n", save_base + i);
        N("    stsfld     string %s::%s\n", net_classname, af);
    }
    for (int i = 0; i < fn->nlocals; i++) {
        char lf[256]; net_field_name(lf, sizeof lf, fn->locals[i]);
        N("    ldloc.s    V_%d\n", save_base + fn->nargs + i);
        N("    stsfld     string %s::%s\n", net_classname, lf);
    }
    /* Get retval from static field then restore fn-name field */
    {
        char fn_field[256]; net_field_name(fn_field, sizeof fn_field, fn->name);
        N("    ldsfld     string %s::%s\n", net_classname, fn_field);
        N("    ldloc.s    V_%d\n", save_fnret);
        N("    stsfld     string %s::%s\n", net_classname, fn_field);
    }
    N("    ret\n");

    /* FRETURN path: restore args/locals via direct stsfld, return null */
    N("  %s:\n", net_fn_freturn_lbl);
    for (int i = 0; i < fn->nargs; i++) {
        char af[256]; net_field_name(af, sizeof af, fn->args[i]);
        N("    ldloc.s    V_%d\n", save_base + i);
        N("    stsfld     string %s::%s\n", net_classname, af);
    }
    for (int i = 0; i < fn->nlocals; i++) {
        char lf[256]; net_field_name(lf, sizeof lf, fn->locals[i]);
        N("    ldloc.s    V_%d\n", save_base + fn->nargs + i);
        N("    stsfld     string %s::%s\n", net_classname, lf);
    }
    {
        char fn_field[256]; net_field_name(fn_field, sizeof fn_field, fn->name);
        N("    ldloc.s    V_%d\n", save_fnret);
        N("    stsfld     string %s::%s\n", net_classname, fn_field);
    }
    N("    ldnull\n");
    N("    ret\n");
    N("  }\n\n");
}

static void net_emit_fn_methods(Program *prog) {
    for (int i = 0; i < net_fn_count; i++)
        net_emit_fn_method(&net_fn_table[i], prog, i);
}

/* Emit CIL helper methods for ARRAY/TABLE/DATA support */
static void net_emit_array_helpers(void) {
    /* Shorthand for the sno_arrays field type */
    const char *AFT = "class [mscorlib]System.Collections.Generic.Dictionary`2<string,class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>>";
    const char *IFT = "class [mscorlib]System.Collections.Generic.Dictionary`2<string,string>";

    if (net_need_array_helpers) {
        /* net_array_new(string size) : string
         * Creates a new inner Dictionary<string,string>, stores it in sno_arrays
         * keyed by its hash code string, returns that key as the array-id. */
        N("  .method static string net_array_new(string) cil managed\n");
        N("  {\n");
        N("    .maxstack 4\n");
        N("    .locals init (%s V_0, string V_1, int32 V_2)\n", IFT);
        N("    newobj     instance void %s::.ctor()\n", IFT);
        N("    stloc.0\n");
        /* id = RuntimeHelpers.GetHashCode(map) boxed to string */
        N("    ldloc.0\n");
        N("    call       int32 [mscorlib]System.Runtime.CompilerServices.RuntimeHelpers::GetHashCode(object)\n");
        N("    stloc.2\n");
        N("    ldloca.s   V_2\n");
        N("    call       instance string [mscorlib]System.Int32::ToString()\n");
        N("    stloc.1\n");
        /* sno_arrays[id] = map */
        N("    ldsfld     %s %s::sno_arrays\n", AFT, net_classname);
        N("    ldloc.1\n");
        N("    ldloc.0\n");
        N("    callvirt   instance void %s::set_Item(!0, !1)\n", AFT);
        N("    ldloc.1\n");
        N("    ret\n");
        N("  }\n\n");

        /* net_array_get(string array_id, string key) : string */
        N("  .method static string net_array_get(string, string) cil managed\n");
        N("  {\n");
        N("    .maxstack 4\n");
        N("    .locals init (%s V_0, string V_1)\n", IFT);
        N("    ldsfld     %s %s::sno_arrays\n", AFT, net_classname);
        N("    ldarg.0\n");
        N("    ldloca.s   V_0\n");
        N("    callvirt   instance bool %s::TryGetValue(!0, !1&)\n", AFT);
        N("    brfalse    Nag_miss\n");
        N("    ldloc.0\n");
        N("    ldarg.1\n");
        N("    ldloca.s   V_1\n");
        N("    callvirt   instance bool %s::TryGetValue(!0, !1&)\n", IFT);
        N("    brfalse    Nag_miss\n");
        N("    ldloc.1\n");
        N("    ret\n");
        N("  Nag_miss:\n");
        N("    ldstr      \"\"\n");
        N("    ret\n");
        N("  }\n\n");

        /* net_array_put(string array_id, string key, string val) : void */
        N("  .method static void net_array_put(string, string, string) cil managed\n");
        N("  {\n");
        N("    .maxstack 4\n");
        N("    .locals init (%s V_0)\n", IFT);
        N("    ldsfld     %s %s::sno_arrays\n", AFT, net_classname);
        N("    ldarg.0\n");
        N("    ldloca.s   V_0\n");
        N("    callvirt   instance bool %s::TryGetValue(!0, !1&)\n", AFT);
        N("    brfalse    Nap_done\n");
        N("    ldloc.0\n");
        N("    ldarg.1\n");
        N("    ldarg.2\n");
        N("    callvirt   instance void %s::set_Item(!0, !1)\n", IFT);
        N("  Nap_done:\n");
        N("    ret\n");
        N("  }\n\n");

        net_need_array_helpers = 0;
    }

    if (net_need_data_helpers) {
        /* net_data_define(string proto) : void
         * proto = "typename(field1,field2,...)" — no-op at runtime, type is pre-parsed */
        N("  .method static void net_data_define(string) cil managed\n");
        N("  {\n");
        N("    .maxstack 1\n");
        N("    ret\n");
        N("  }\n\n");

        net_need_data_helpers = 0;
    }
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

    /* Multi-pass: scan functions first (registers vars), then vars, named patterns */
    net_scan_fndefs(prog);
    scan_prog_vars(prog);
    net_scan_named_patterns(prog);

    NC("Generated by sno2c -net");
    NC("Assemble: ilasm <file>.il /output:<file>.exe");
    NC("Run:      mono <file>.exe");
    N("\n");

    net_emit_header(prog);
    net_emit_main_open();
    net_emit_stmts(prog);
    net_emit_main_close();
    net_emit_fn_methods(prog);
    net_emit_array_helpers();
    net_emit_footer();

    /* free registered variable names */
    for (int i = 0; i < net_nvar; i++) { free(net_vars[i]); net_vars[i] = NULL; }
    net_nvar = 0;
}
