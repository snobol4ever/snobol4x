/*
 * net_emit.c — .NET CIL (MSIL) text emitter for sno2c
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
 *   N-R0  M-NET-HELLO   — skeleton: null program → exit 0          ← NOW
 *   N-R1  M-NET-LIT     — OUTPUT = 'hello' correct
 *   N-R2  M-NET-ASSIGN  — variable assign + arithmetic
 *   N-R3  M-NET-GOTO    — :S/:F branching
 *   N-R4  M-NET-PATTERN — Byrd boxes in CIL: LIT/SEQ/ALT/ARBNO
 *   N-R5  M-NET-CAPTURE — . and $ capture
 *   N-R1  M-NET-R1      — hello/ output/ assign/ arith/ PASS
 *   N-R2  M-NET-R2      — control/ patterns/ capture/ PASS
 *   N-R3  M-NET-R3      — strings/ keywords/ PASS
 *   N-R4  M-NET-R4      — functions/ data/ PASS
 *   N-R5  M-NET-CROSSCHECK — 106/106 corpus PASS
 *
 * Design:
 *   Each SNOBOL4 program becomes one CIL class with a static main().
 *   Class name is derived from source filename (or "SnobolProg").
 *
 *   Three-column CIL layout mirrors the ASM and JVM backends:
 *     label:       instruction    operands
 *
 *   SNOBOL4 variables are stored as static string fields on the class.
 *   null/failure represented as ldnull / null reference on the eval stack.
 *   :S/:F goto uses brtrue / brfalse — mirrors JCON bc_conditional_transfer_to.
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
    else if ((c & 0xC0) != 0x80) net_col++;  /* skip UTF-8 continuations */
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
    /* reset col tracker on newline (approximate — good enough for our patterns) */
    const char *p = fmt;
    while (*p) { if (*p == '\n') net_col = 0; p++; }
}

/* NL(label, instr, ops) — three-column line */
static void NL(const char *label, const char *instr, const char *ops) {
    net_col = 0;
    if (label && label[0]) {
        ns(label); nc(':');
    }
    npad(COL_INSTR);
    ns(instr);
    if (ops && ops[0]) {
        npad(COL_OPS);
        ns(ops);
    }
    nc('\n');
}

/* NI(instr, ops) — instruction with no label */
static void NI(const char *instr, const char *ops) {
    NL("", instr, ops);
}

/* NC(comment) — comment line */
static void NC(const char *comment) {
    N("    // %s\n", comment);
}

/* NSep(tag) — section separator */
static void NSep(const char *tag) {
    N("\n    // --- %s ---\n", tag);
}

/* -----------------------------------------------------------------------
 * Class name derivation
 * ----------------------------------------------------------------------- */

static char net_classname[256];

static void net_set_classname(const char *filename) {
    if (!filename || strcmp(filename, "<stdin>") == 0) {
        strcpy(net_classname, "SnobolProg");
        return;
    }
    /* strip directory */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    /* strip extension */
    char buf[256];
    strncpy(buf, base, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    char *dot = strrchr(buf, '.');
    if (dot) *dot = '\0';
    /* sanitize: replace non-alnum with _ */
    char *p = buf;
    if (!isalpha((unsigned char)*p) && *p != '_') *p = '_';
    for (; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    /* Capitalise first letter (.NET class convention) */
    buf[0] = (char)toupper((unsigned char)buf[0]);
    strncpy(net_classname, buf, sizeof net_classname - 1);
    net_classname[sizeof net_classname - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * N-R0 skeleton emitters
 * ----------------------------------------------------------------------- */

static void net_emit_header(void) {
    N(".assembly extern mscorlib {}\n");
    N(".assembly %s {}\n", net_classname);
    N(".module %s.exe\n", net_classname);
    N("\n");
    N(".class public auto ansi beforefieldinit %s\n", net_classname);
    N("       extends [mscorlib]System.Object\n");
    N("{\n");
    N("\n");
    NC("SNOBOL4 variable fields — added per rung as needed (N-R2+)");
    NC("static string field example: .field static string sno_OUTPUT");
    N("\n");
}

static void net_emit_main_open(void) {
    N("  .method public static void main(string[] args) cil managed\n");
    N("  {\n");
    N("    .entrypoint\n");
    N("    .maxstack 8\n");
    N("\n");
}

static void net_emit_main_close(void) {
    NSep("END");
    NL("L_END", "nop", "");
    NI("ret", "");
    N("  }\n");
    N("\n");
}

static void net_emit_footer(void) {
    N("} // end class %s\n", net_classname);
}

/* -----------------------------------------------------------------------
 * Statement stub walker — N-R0 emits labels only; body filled in N-R1+
 * ----------------------------------------------------------------------- */

static void net_emit_stmts(Program *prog) {
    if (!prog || !prog->head) return;

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) {
            NSep("END statement");
            /* fall through to L_END + ret in main_close */
            break;
        }
        if (s->label) {
            NSep(s->label);
            N("    L_%s:\n", s->label);
            NI("nop", "// stub — N-R1+ will fill body");
        }
    }
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

void net_emit(Program *prog, FILE *out, const char *filename) {
    net_out = out;
    net_set_classname(filename);

    NC("Generated by sno2c -net");
    NC("Assemble: ilasm <file>.il /output:<file>.exe");
    NC("Run:      mono <file>.exe");
    N("\n");

    net_emit_header();
    net_emit_main_open();
    net_emit_stmts(prog);
    net_emit_main_close();
    net_emit_footer();
}
