/*
 * emit_byrd_asm.c — x64 ASM Byrd box emitter for sno2c
 *
 * Mirrors the recursive structure of emit_byrd.c but outputs NASM ELF64
 * instead of C labeled-goto code.
 *
 * Design:
 *   α/β/γ/ω are real NASM labels.
 *   All pattern variables live flat in .bss as QWORD (resq 1) slots.
 *   Named patterns are plain labels with a 2-way jmp dispatch.
 *
 * Implemented nodes (matching hand-written oracles):
 *   LIT (E_QLIT)     — inline repe cmpsb or single-byte cmp
 *   SEQ (E_CONC)     — recursive left/right wiring
 *   ALT (E_OR)       — cursor-save, left-fail-try-right wiring
 *   POS              — cursor == n check
 *   RPOS             — cursor == subj_len - n check
 *   ARBNO            — flat .bss cursor stack, zero-advance guard
 *
 * .bss slots are collected into asm_bss[] during the tree walk and
 * emitted in a single section .bss block at the top of the file.
 *
 * The SNOBOL4 runtime (OUTPUT, assignment, goto) is NOT implemented yet —
 * pattern match nodes only.  The emitter emits a harness that runs the
 * root pattern against a hardcoded subject and exits 0/1.
 */

#include "sno2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Output helpers
 * ----------------------------------------------------------------------- */

static FILE *asm_out;

/* Column alignment — every instruction starts at COL_W.
 * out_col tracks the current column (0 = start of line).
 * emit_to_col(n) pads with spaces until out_col == n.
 * If out_col >= n already (long label case), emit newline first.
 *
 * Three-column layout:
 *   col1: label:        — starts at 0, ends at COL_W
 *   col2: MACRO_NAME    — starts at COL_W, width = COL2_W (12)
 *   col3: operands      — starts at COL_W + COL2_W + 1
 *   comment:            — starts at COL_CMT
 *
 * COL2_W=12 accommodates all macro names (longest: SUBJ_FROM16=11,
 * DOL_CAPTURE=11, STORE_ARG32=11, GOTO_ALWAYS=11, LOAD_NULVCL=11).
 */
#define COL_W   28
#define COL2_W  12
#define COL_CMT (COL_W + COL2_W + 32)   /* 72 — comment column in ALFC lines */

/* Comment separator width (configurable).
 * SEP_W = total width of ; === / ; --- lines, including the leading "; ".
 * 80 is the classic terminal width; 120 suits wide monitors. */
#define SEP_W 120

static int out_col = 0;

/* emit_sep_major — strong horizontal rule: ; ====...  (SEP_W chars total)
 * Embeds optional tag: ";  tag ===..."
 * Used for: SNOBOL4 statement boundaries, section headers, named pattern headers. */
/* pending_sep: buffered separator text to attach to next label.
 * When emit_sep_major fires and a label follows, we fold:
 *   label:  ; === tag ===...
 * instead of emitting the sep at col 1 then the label on the next line. */
static char pending_sep[256] = "";

static void emit_sep_major(const char *tag) {
    /* Build the sep text into pending_sep — asmL will attach it */
    int used = 2;
    char *p = pending_sep;
    int rem = (int)sizeof pending_sep;
    int n;
    /* flush any existing pending_sep that was never consumed */
    if (pending_sep[0]) {
        fprintf(asm_out, "\n; %s\n", pending_sep);
        out_col = 0;
        pending_sep[0] = '\0';
    }
    /* build new sep line in pending_sep as plain text (no leading "; ") */
    p = pending_sep; rem = (int)sizeof pending_sep;
    if (tag && tag[0]) {
        n = snprintf(p, rem, " %s ", tag); p += n; rem -= n; used += n;
    }
    /* fill remainder with '=' */
    int eq_count = SEP_W - 2 - (int)(p - pending_sep);
    for (int i = 0; i < eq_count && rem > 1; i++, p++, rem--) *p = '=';
    *p = '\0';
    /* Emit leading blank line immediately */
    fprintf(asm_out, "\n");
    out_col = 0;
    /* NOTE: no second \n here — sep line itself provides the visual break,
     * and asmL will emit the label immediately after pending_sep is consumed. */
}

/* flush_pending_sep — called when no label follows: emit sep at col 1 */
static void flush_pending_sep(void) {
    if (pending_sep[0]) {
        fprintf(asm_out, "; %s\n", pending_sep);
        out_col = 0;
        pending_sep[0] = '\0';
    }
}

/* emit_sep_minor — light horizontal rule: ; ----...  (SEP_W chars total)
 * Used for: γ/ω trampoline boundary within named pattern defs. */
static void emit_sep_minor(const char *tag) {
    int used = 2;
    fprintf(asm_out, "; ");
    if (tag && tag[0]) {
        used += fprintf(asm_out, " %s ", tag);
    }
    for (int i = used; i < SEP_W; i++) fputc('-', asm_out);
    fputc('\n', asm_out);
    out_col = 0;
}

static void oc_char(char c) {
    fputc(c, asm_out);
    if (c == '\n') out_col = 0;
    /* Count only non-continuation UTF-8 bytes as display columns */
    else if ((c & 0xC0) != 0x80) out_col++;
}

static void oc_str(const char *s) {
    for (; *s; s++) oc_char(*s);
}

static void emit_to_col(int n) {
    if (out_col >= n) { oc_char('\n'); }
    while (out_col < n) oc_char(' ');
}

/* emit_instr — emit opcode + operands with col3 alignment.
 * Caller has already padded to COL_W (col1 done).
 * We emit the opcode word, then pad to COL_W+COL2_W for operands.
 * instr must have leading whitespace already stripped. */
static void emit_instr(const char *instr) {
    /* find end of opcode word */
    const char *sp = instr;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    /* emit opcode */
    const char *op = instr;
    while (op < sp) oc_char(*op++);
    /* skip whitespace between opcode and operands */
    while (*sp == ' ' || *sp == '\t') sp++;
    if (*sp) {
        /* pad to COL3 for operands */
        int col3 = COL_W + COL2_W;
        if (out_col < col3) emit_to_col(col3);
        else oc_char(' ');
        oc_str(sp);
    } else {
        oc_char('\n');
    }
}

/* -----------------------------------------------------------------------
 * Pending-label mechanism (Sprint A14 beauty):
 * Instead of emitting "label:\n" immediately, we hold it and prepend it
 * to the next instruction on the same line: "label:  INSTR\n"
 * If two labels arrive consecutively, the first is flushed standalone.
 * ----------------------------------------------------------------------- */
static char pending_label[256] = "";

/* flush_pending_label — emit pending label standalone (consecutive-label case) */
static void flush_pending_label(void) {
    if (pending_label[0]) {
        oc_str(pending_label);
        oc_char(':');
        oc_char('\n');
        pending_label[0] = '\0';
    }
}

static void A(const char *fmt, ...) {
    /* Build the full output string first */
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (pending_label[0]) {
        /* Find first non-newline char to decide if this is an instruction */
        const char *p = buf;
        while (*p == '\n') p++;
        /* Treat as instruction only if it starts with space/tab AND is not STMT_SEP/PORT_SEP */
        int is_instr = (*p == ' ' || *p == '\t');
        int is_passthrough = 0;  /* separators and blank lines pass through without flushing */
        if (is_instr) {
            const char *kw = p;
            while (*kw == ' ' || *kw == '\t') kw++;
            if (strncmp(kw, "STMT_SEP", 8) == 0 ||
                strncmp(kw, "PORT_SEP", 8) == 0 ||
                strncmp(kw, ";", 1) == 0) {
                is_instr = 0;
                is_passthrough = 1;  /* visual separator — label survives it */
            }
        }
        int is_blank = (*p == '\0');
        if (is_blank || is_passthrough) {
            oc_str(buf);
            return;  /* pending label survives blank lines and separators */
        }
        if (is_instr) {
            /* Emit any leading newlines first, then sep (if any), then fold label+instruction. */
            const char *start = buf;
            while (*start == '\n') { oc_char('\n'); start++; }
            /* Rule 3: sep immediately precedes label:  INSTR with no blank between */
            if (pending_sep[0]) {
                oc_char(';'); oc_char(' '); oc_str(pending_sep); oc_char('\n');
                pending_sep[0] = '\0';
            }
            /* emit label: then pad to COL_W */
            oc_str(pending_label);
            oc_char(':');
            pending_label[0] = '\0';
            emit_to_col(COL_W);
            /* strip leading whitespace from instruction content */
            while (*start == ' ' || *start == '\t') start++;
            emit_instr(start);
            return;
        } else {
            flush_pending_label();
        }
    }
    /* No pending label — emit instruction at COL_W if it's indented content */
    {
        const char *p = buf;
        /* skip leading newlines — emit them, then handle the instruction part */
        while (*p == '\n') { oc_char('\n'); p++; }
        if (*p == ' ' || *p == '\t') {
            /* indented instruction — align to COL_W */
            while (*p == ' ' || *p == '\t') p++;
            if (*p) {  /* non-empty after strip */
                /* check it's not a .section / global / extern / resq directive */
                int is_directive = (strncmp(p, "section", 7) == 0 ||
                                    strncmp(p, "global",  6) == 0 ||
                                    strncmp(p, "extern",  6) == 0 ||
                                    strncmp(p, "resq",    4) == 0 ||
                                    strncmp(p, "db ",     3) == 0 ||
                                    strncmp(p, "dq ",     3) == 0 ||
                                    strncmp(p, "dd ",     3) == 0 ||
                                    strncmp(p, "%include",8) == 0 ||
                                    strncmp(p, "STMT_SEP",8) == 0 ||
                                    strncmp(p, "PORT_SEP",8) == 0 ||
                                    p[0] == ';');
                if (!is_directive) {
                    emit_to_col(COL_W);
                    /* Col3 alignment: emit opcode word, then pad to COL3 before operands */
                    const char *op = p;
                    const char *sp = p;
                    while (*sp && *sp != ' ' && *sp != '\t') sp++; /* end of opcode */
                    /* emit the opcode word */
                    while (op < sp) oc_char(*op++);
                    /* skip whitespace in source */
                    while (*sp == ' ' || *sp == '\t') sp++;
                    if (*sp) {
                        /* pad to COL3 for operands */
                        int col3 = COL_W + COL2_W;
                        if (out_col < col3) emit_to_col(col3);
                        else oc_char(' ');
                        oc_str(sp);
                    } else {
                        oc_char('\n');
                    }
                    return;
                }
            }
        }
        /* fallback: emit as-is from current position */
        oc_str(p);
    }
}

/* -----------------------------------------------------------------------
 * UID counter — never resets so labels never collide across patterns
 * ----------------------------------------------------------------------- */

static int asm_uid_ctr = 0;
static int asm_uid(void) { return asm_uid_ctr++; }

/* -----------------------------------------------------------------------
 * M-ASM-READABLE: special-char expansion table.
 * Converts a SNOBOL4 source name to a valid, readable ASM label fragment.
 * Each special char expands to a named token surrounded by underscores.
 * The mapping is injective: distinct source names produce distinct labels.
 * asm_expand_name(src, dst, dstlen) — fills dst, always NUL-terminates.
 * ----------------------------------------------------------------------- */
static void asm_expand_name(const char *src, char *dst, int dstlen) {
    static const struct { char ch; const char *nm; } tbl[] = {
        {'>', "GT"}, {'<', "LT"}, {'=', "EQ"}, {'+', "PL"}, {'-', "MI"},
        {'*', "ST"}, {'/', "SL"}, {'(', "LP"}, {')', "RP"}, {'$', "DL"},
        {'.', "DT"}, {'?', "QM"}, {'!', "BG"}, {'&', "AM"}, {'|', "OR"},
        {'@', "AT"}, {'~', "TL"}, {':', "CL"}, {',', "CM"}, {'#', "HS"},
        {'%', "PC"}, {'^', "CA"}, {'[', "LB"}, {']', "RB"}, {' ', "SP"},
        {0, NULL}
    };
    int di = 0;
    for (const char *p = src; *p && di < dstlen - 1; p++) {
        /* plain alnum or underscore — copy directly */
        if (isalnum((unsigned char)*p) || *p == '_') {
            dst[di++] = *p;
            continue;
        }
        /* special char — expand to _NM_ */
        const char *nm = NULL;
        for (int i = 0; tbl[i].ch; i++)
            if (tbl[i].ch == *p) { nm = tbl[i].nm; break; }
        if (!nm) nm = "XX"; /* unknown char fallback */
        /* emit underscore separator only if needed */
        if (di > 0 && dst[di-1] != '_') dst[di++] = '_';
        for (const char *q = nm; *q && di < dstlen - 2; q++) dst[di++] = *q;
        dst[di++] = '_';
    }
    /* trim trailing underscores */
    while (di > 0 && dst[di-1] == '_') di--;
    dst[di] = '\0';
}

/* -----------------------------------------------------------------------
 * .bss slot registry
 * ----------------------------------------------------------------------- */

#define MAX_BSS 512
static char *bss_slots[MAX_BSS];
static int   bss_count = 0;

static void bss_reset(void) { bss_count = 0; }

static void bss_add(const char *name) {
    /* deduplicate */
    for (int i = 0; i < bss_count; i++)
        if (strcmp(bss_slots[i], name) == 0) return;
    if (bss_count < MAX_BSS)
        bss_slots[bss_count++] = strdup(name);
}

static void bss_emit(void) {
    if (bss_count == 0) return;
    A("\nsection .bss\n");
    for (int i = 0; i < bss_count; i++)
        A("%-24s resq 1\n", bss_slots[i]);
}

/* -----------------------------------------------------------------------
 * .data string literals registry
 * ----------------------------------------------------------------------- */

#define MAX_LITS 128
typedef struct { char label[64]; char *val; int len; } LitEntry;
static LitEntry lit_table[MAX_LITS];
static int lit_count = 0;

static void lit_reset(void) { lit_count = 0; }

static const char *lit_intern(const char *s, int n) {
    for (int i = 0; i < lit_count; i++)
        if (lit_table[i].len == n && memcmp(lit_table[i].val, s, n) == 0)
            return lit_table[i].label;
    if (lit_count >= MAX_LITS) return "lit_overflow";
    LitEntry *e = &lit_table[lit_count++];
    snprintf(e->label, sizeof e->label, "lit_str_%d", lit_count);
    e->val = malloc(n + 1); memcpy(e->val, s, n); e->val[n] = 0;
    e->len = n;
    return e->label;
}

static void lit_emit_data(void) {
    if (lit_count == 0) return;
    A("\nsection .data\n");
    A("subject_str:    db 0        ; placeholder — harness fills this\n");
    A("newline:        db 10\n");
    for (int i = 0; i < lit_count; i++) {
        if (lit_table[i].len == 0) {
            A("%-20s db 0    ; \"\"\n", lit_table[i].label);
        } else {
            A("%-20s db ", lit_table[i].label);
            for (int j = 0; j < lit_table[i].len; j++) {
                if (j) A(", ");
                A("%d", (unsigned char)lit_table[i].val[j]);
            }
            A("    ; \"%.*s\"\n", lit_table[i].len, lit_table[i].val);
        }
    }
}

/* -----------------------------------------------------------------------
 * NASM label helpers
 * ----------------------------------------------------------------------- */

#define LBUF 128

static void asmL(const char *lbl) {
    flush_pending_label();
    snprintf(pending_label, sizeof pending_label, "%s", lbl);
    /* pending_sep will be consumed by A() when it folds the label+instruction.
     * It is printed just before "label:  INSTR" via the A() pending-label path. */
}

/* asmLB(lbl, instr) — label and pre-built instruction string on one line */
static void asmLB(const char *lbl, const char *instr) {
    flush_pending_label();
    oc_str(lbl); oc_char(':');
    emit_to_col(COL_W);
    while (*instr == ' ') instr++;
    emit_instr(instr);
}

/* asmLC — emit "label: ; comment\n" — label with col2 comment, no instruction.
 * Used for block-header comments (REF/DOL/ARBNO) that describe the whole block.
 * The instructions that follow are emitted normally via A()/ALF()/ALFC(). */
static void asmLC(const char *lbl, const char *comment) {
    flush_pending_label();
    oc_str(lbl); oc_char(':');
    oc_char(' '); oc_char(';'); oc_char(' ');
    oc_str(comment);
    oc_char('\n');
}

/* ALF — emit "label:  instruction\n" where instruction is printf-formatted.
 * This is the main beauty helper: one source call = one .s line. */
static char _alf_ibuf[1024];
#define ALF(lbl, fmt, ...) do { \
    snprintf(_alf_ibuf, sizeof _alf_ibuf, fmt, ##__VA_ARGS__); \
    asmLB(lbl, _alf_ibuf); \
} while(0)

/* ALFC — emit "label:  instruction ; comment\n" — three columns, snug */
static char _alfc_ibuf[768];
#define ALFC(lbl, comment, fmt, ...) do { \
    snprintf(_alfc_ibuf, sizeof _alfc_ibuf, fmt, ##__VA_ARGS__); \
    /* strip trailing newline from instruction */ \
    int _alfc_len = strlen(_alfc_ibuf); \
    if (_alfc_len > 0 && _alfc_ibuf[_alfc_len-1] == '\n') _alfc_ibuf[--_alfc_len] = '\0'; \
    /* emit label: at col 0, instruction at COL_W, then comment (no wrap) */ \
    flush_pending_label(); \
    if ((lbl) && *(lbl)) { oc_str(lbl); oc_char(':'); } \
    emit_to_col(COL_W); \
    const char *_alfc_p = _alfc_ibuf; \
    while (*_alfc_p == ' ') _alfc_p++; \
    /* emit opcode, pad to COL3, emit operands (if any), then comment */ \
    { \
        const char *_sp = _alfc_p; \
        while (*_sp && *_sp != ' ' && *_sp != '\t') _sp++; \
        const char *_op = _alfc_p; \
        while (_op < _sp) oc_char(*_op++); \
        while (*_sp == ' ' || *_sp == '\t') _sp++; \
        if (*_sp) { \
            int _col3 = COL_W + COL2_W; \
            if (out_col < _col3) emit_to_col(_col3); else oc_char(' '); \
            oc_str(_sp); \
        } \
    } \
    /* one space before comment — rule: no fourth column */ \
    oc_char(' '); \
    oc_str("; "); oc_str(comment); oc_char('\n'); \
} while(0)


static void asmJ(const char *lbl) {
    A("    jmp     %s\n", lbl);
}

static void asmJne(const char *lbl) {
    A("    jne     %s\n", lbl);
}

static void asmJg(const char *lbl) {
    A("    jg      %s\n", lbl);
}

static void asmJge(const char *lbl) {
    A("    jge     %s\n", lbl);
}

static void asmJe(const char *lbl) {
    A("    je      %s\n", lbl);
}

/* -----------------------------------------------------------------------
 * emit_asm_lit — LIT node  (Sprint A14: one macro call per port)
 *
 *   α: LIT_ALPHA / LIT_ALPHA1   — one call site line
 *   β: LIT_BETA                 — one call site line
 * ----------------------------------------------------------------------- */

static void emit_asm_lit(const char *s, int n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved", alpha);
    bss_add(saved);

    if (n == 1) {
        ALFC(alpha, "LIT α", "LIT_ALPHA1  %d, %s, %s, %s, %s, %s, %s\n",
            (unsigned char)s[0], saved, cursor, subj, subj_len_sym, gamma, omega);
    } else {
        const char *lstr = lit_intern(s, n);
        ALFC(alpha, "LIT α", "LIT_ALPHA   %s, %d, %s, %s, %s, %s, %s, %s\n",
            lstr, n, saved, cursor, subj, subj_len_sym, gamma, omega);
    }
    ALFC(beta, "LIT β", "LIT_BETA    %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_pos / emit_asm_rpos
 * ----------------------------------------------------------------------- */

static void emit_asm_pos(long n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor) {
    ALFC(alpha, "POS(%ld)", "POS_ALPHA   %ld, %s, %s, %s\n", n, cursor, gamma, omega);
    ALF(beta,  "POS_BETA    %s, %s\n", cursor, omega);
}

static void emit_asm_pos_var(const char *varlab,
                              const char *alpha, const char *beta,
                              const char *gamma, const char *omega,
                              const char *cursor) {
    ALFC(alpha, "POS(var)", "POS_ALPHA_VAR %s, %s, %s, %s\n", varlab, cursor, gamma, omega);
    ALF(beta,  "POS_BETA    %s, %s\n", cursor, omega);
}

static void emit_asm_rpos(long n,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj_len_sym) {
    ALFC(alpha, "RPOS(%ld)", "RPOS_ALPHA  %ld, %s, %s, %s, %s\n", n, cursor, subj_len_sym, gamma, omega);
    ALF(beta,  "RPOS_BETA   %s, %s\n", cursor, omega);
}

static void emit_asm_rpos_var(const char *varlab,
                               const char *alpha, const char *beta,
                               const char *gamma, const char *omega,
                               const char *cursor,
                               const char *subj_len_sym) {
    ALFC(alpha, "RPOS(var)", "RPOS_ALPHA_VAR %s, %s, %s, %s, %s\n", varlab, cursor, subj_len_sym, gamma, omega);
    ALF(beta,  "RPOS_BETA   %s, %s\n", cursor, omega);
}

/* -----------------------------------------------------------------------
 * Forward declarations for named pattern machinery
 * (full definitions are after emit_asm_node, which uses them)
 * ----------------------------------------------------------------------- */

typedef struct AsmNamedPat AsmNamedPat;

/* Forward declarations for capture variable registry (defined after named-pat section) */
#define MAX_CAP_VARS 64
typedef struct {
    char varname[64];
    char safe[64];
    char buf_sym[128];
    char len_sym[128];
} CaptureVar;
static CaptureVar *cap_var_register(const char *varname);
/* ASM_NAMED_MAXPARAMS — max args for a user-defined Snocone procedure */
#define ASM_NAMED_MAXPARAMS 8

struct AsmNamedPat {
    char varname[128];
    char safe[128];
    char alpha_lbl[128];
    char beta_lbl[128];
    char ret_gamma[128];
    char ret_omega[128];
    EXPR_t *pat;
    /* User-defined function fields (is_fn == 1) */
    int  is_fn;                              /* 1 = user Snocone procedure */
    int  nparams;
    char param_names[ASM_NAMED_MAXPARAMS][64]; /* parameter variable names */
    char body_label[128];                    /* NASM label of function body entry */
};

static const AsmNamedPat *asm_named_lookup(const char *varname);
static void emit_asm_named_ref(const AsmNamedPat *np,
                                const char *alpha, const char *beta,
                                const char *gamma, const char *omega);

/* Forward declaration for plain-string variable registry (VAR = 'literal') */
typedef struct { char varname[128]; const char *sval; } AsmStrVar;
static const AsmStrVar *asm_str_var_lookup(const char *varname);

/* Forward declarations for program-mode helpers used by emit_asm_named_def */
static const char *prog_str_intern(const char *s);
static const char *prog_label_nasm(const char *lbl);

/* -----------------------------------------------------------------------
 * Forward declaration for recursive emit_asm_node
 * ----------------------------------------------------------------------- */

static void emit_asm_node(EXPR_t *pat,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj, const char *subj_len_sym,
                           int depth);

/* -----------------------------------------------------------------------
 * emit_asm_seq — SEQ (E_CONC / CAT)
 *
 * Wiring (Proebsting §4.3, oracle cat_pos_lit_rpos.s):
 *   α → left_α
 *   β → right_β
 *   left_γ  → right_α
 *   left_ω  → ω
 *   right_γ → γ
 *   right_ω → left_β
 * ----------------------------------------------------------------------- */

static void emit_asm_seq(EXPR_t *left, EXPR_t *right,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj, const char *subj_len_sym,
                          int depth) {
    int uid = asm_uid();
    char lα[LBUF], lβ[LBUF], rα[LBUF], rβ[LBUF];
    snprintf(lα, LBUF, "seq_l%d_alpha", uid);
    snprintf(lβ, LBUF, "seq_l%d_beta",  uid);
    snprintf(rα, LBUF, "seq_r%d_alpha", uid);
    snprintf(rβ, LBUF, "seq_r%d_beta",  uid);

    ALFC(alpha, "SEQ", "jmp     %s\n", lα);
    ALF(beta, "jmp     %s\n", rβ);

    emit_asm_node(left,  lα, lβ, rα, omega, cursor, subj, subj_len_sym, depth+1);
    emit_asm_node(right, rα, rβ, gamma, lβ, cursor, subj, subj_len_sym, depth+1);
}

/* -----------------------------------------------------------------------
 * emit_asm_alt — ALT (E_OR)
 *
 * Wiring (oracle alt_first.s, Proebsting §4.5 ifstmt):
 *   α → save cursor_at_alt; → left_α
 *   β → right_β
 *   left_γ  → γ
 *   left_ω  → restore cursor_at_alt; → right_α
 *   right_γ → γ
 *   right_ω → ω
 * ----------------------------------------------------------------------- */

static void emit_asm_alt(EXPR_t *left, EXPR_t *right,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj, const char *subj_len_sym,
                          int depth) {
    int uid = asm_uid();
    char lα[LBUF], lβ[LBUF], rα[LBUF], rβ[LBUF];
    char cursor_save[LBUF], restore_lbl[LBUF];
    snprintf(lα,           LBUF, "alt_l%d_alpha",   uid);
    snprintf(lβ,           LBUF, "alt_l%d_beta",    uid);
    snprintf(rα,           LBUF, "alt_r%d_alpha",   uid);
    snprintf(rβ,           LBUF, "alt_r%d_beta",    uid);
    snprintf(cursor_save,  LBUF, "alt%d_cur_save",  uid);
    snprintf(restore_lbl,  LBUF, "alt%d_restore",   uid);
    bss_add(cursor_save);

    char left_omega_tramp[LBUF];
    snprintf(left_omega_tramp, LBUF, "alt%d_left_omega", uid);

    ALFC(alpha, "ALT α — save cursor, enter left",
         "ALT_ALPHA   %s, %s, %s\n", cursor_save, cursor, lα);
    ALFC(beta,  "ALT β — resume right", "SEQ_BETA    %s\n", rβ);

    emit_asm_node(left, lα, lβ, gamma, left_omega_tramp,
                  cursor, subj, subj_len_sym, depth+1);

    ALFC(left_omega_tramp, "ALT left_ω — restore cursor, enter right",
         "ALT_OMEGA   %s, %s, %s\n", cursor_save, cursor, rα);

    emit_asm_node(right, rα, rβ, gamma, omega,
                  cursor, subj, subj_len_sym, depth+1);
}

/* -----------------------------------------------------------------------
 * emit_asm_arbno — ARBNO
 *
 * Wiring (oracle arbno_match.s, v311.sil ARBN/EARB/ARBF):
 *   α: push cursor; goto γ  (zero reps = immediate success)
 *   β: pop cursor; try child_α
 *      child_γ → zero-advance guard; push new cursor; goto γ
 *      child_ω → ω
 * ----------------------------------------------------------------------- */

static void emit_asm_arbno(EXPR_t *child,
                            const char *alpha, const char *beta,
                            const char *gamma, const char *omega,
                            const char *cursor,
                            const char *subj, const char *subj_len_sym,
                            int depth) {
    int uid = asm_uid();
    char stk[LBUF], dep[LBUF], cα[LBUF], cβ[LBUF];
    char child_ok[LBUF], child_fail[LBUF], cur_before[LBUF];
    char push_lbl[LBUF], pop_lbl[LBUF];

    snprintf(stk,        LBUF, "arb%d_stack",       uid);
    snprintf(dep,        LBUF, "arb%d_depth",        uid);
    snprintf(cur_before, LBUF, "arb%d_cur_before",   uid);
    snprintf(cα,         LBUF, "arb%d_child_alpha",  uid);
    snprintf(cβ,         LBUF, "arb%d_child_beta",   uid);
    snprintf(child_ok,   LBUF, "arb%d_child_ok",     uid);
    snprintf(child_fail, LBUF, "arb%d_child_fail",   uid);
    snprintf(push_lbl,   LBUF, "arb%d_push",         uid);
    snprintf(pop_lbl,    LBUF, "arb%d_pop",          uid);

    /* stk needs 64 slots — we emit it specially */
    bss_add(dep);
    bss_add(cur_before);
    /* stk: emit as resq 64 — we track this separately */
    /* We'll add it to a special multi-slot list */
    static char arbno_stack_decls[512][LBUF];
    static int  arbno_stack_count = 0;
    if (arbno_stack_count < 512) {
        snprintf(arbno_stack_decls[arbno_stack_count], LBUF,
                 "%-24s resq 64", stk);
        arbno_stack_count++;
    }

    A("\n");

    /* α: header comment on label line, then initialize depth=0, push cursor, goto γ */
    asmLC(alpha, "ARBNO");
    A("    mov     qword [%s], 0\n", dep);
    /* push cursor onto stack slot 0 */
    A("    lea     rbx, [rel %s]\n", stk);
    A("    mov     rax, [%s]\n", cursor);
    A("    mov     [rbx], rax\n");
    A("    mov     qword [%s], 1\n", dep);
    asmJ(gamma);

    /* β: pop, save cursor-before-rep, try child */
    ALFC(beta, "ARBNO", "mov     rax, [%s]\n", dep);
    A("    test    rax, rax\n");
    asmJe(omega);                  /* stack empty → ω */
    A("    dec     rax\n");
    A("    mov     [%s], rax\n", dep);
    A("    lea     rbx, [rel %s]\n", stk);
    A("    mov     rcx, [rbx + rax*8]\n");
    A("    mov     [%s], rcx\n", cursor);        /* restore cursor */
    A("    mov     [%s], rcx\n", cur_before);    /* save pre-rep cursor */
    asmJ(cα);

    /* child_ok: zero-advance guard, push new cursor, re-succeed */
    ALFC(child_ok, "ARBNO child_ok", "mov     rax, [%s]\n", cursor);
    A("    mov     rbx, [%s]\n", cur_before);
    A("    cmp     rax, rbx\n");
    asmJe(omega);                  /* stalled → ω */
    A("    mov     rdx, [%s]\n", dep);
    A("    lea     rbx, [rel %s]\n", stk);
    A("    mov     [rbx + rdx*8], rax\n");
    A("    inc     qword [%s]\n", dep);
    asmJ(gamma);

    /* child_fail: ω */
    ALFC(child_fail, "ARBNO child_fail", "jmp     %s\n", omega);

    emit_asm_node(child, cα, cβ, child_ok, child_fail,
                  cursor, subj, subj_len_sym, depth+1);

    /* Patch: we need to emit the resq 64 for stk in section .bss.
     * We use a hack: emit it as a comment-tagged entry so bss_emit
     * can handle it. We store it directly since bss_add only does resq 1. */
    /* Replace the bss_add approach: use a global extra-bss list */
    extern char asm_extra_bss[][128];
    extern int  asm_extra_bss_count;
    if (asm_extra_bss_count < 64) {
        snprintf(asm_extra_bss[asm_extra_bss_count++], 128,
                 "%-24s resq 64", stk);
    }
}

/* -----------------------------------------------------------------------
 * emit_asm_any — ANY(S)  (Sprint A14: one macro call per port)
 * ----------------------------------------------------------------------- */

static void emit_asm_any(const char *charset, int cslen,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "any%d_saved", uid);
    bss_add(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(alpha, "ANY α", "ANY_ALPHA   %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta, "ANY β", "ANY_BETA    %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_notany — NOTANY(S)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_asm_notany(const char *charset, int cslen,
                             const char *alpha, const char *beta,
                             const char *gamma, const char *omega,
                             const char *cursor,
                             const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "nany%d_saved", uid);
    bss_add(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(alpha, "NOTANY α", "NOTANY_ALPHA %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta, "NOTANY β", "NOTANY_BETA  %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_span — SPAN(S)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_asm_span(const char *charset, int cslen,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "span%d_saved", uid);
    bss_add(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(alpha, "SPAN α", "SPAN_ALPHA  %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta, "SPAN β", "SPAN_BETA   %s, %s, %s\n", saved, cursor, omega);
}

static void emit_asm_break(const char *charset, int cslen,
                            const char *alpha, const char *beta,
                            const char *gamma, const char *omega,
                            const char *cursor,
                            const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "brk%d_saved", uid);
    bss_add(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(alpha, "BREAK α", "BREAK_ALPHA %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta, "BREAK β", "BREAK_BETA  %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * Variable-charset emitters — SPAN/BREAK/ANY/NOTANY with E_VART arg
 * ----------------------------------------------------------------------- */

static void emit_asm_span_var(const char *varlab,
                               const char *alpha, const char *beta,
                               const char *gamma, const char *omega,
                               const char *cursor,
                               const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "span%d_saved", asm_uid());
    bss_add(saved);
    ALFC(alpha, "SPAN(var) α", "SPAN_ALPHA_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta,  "SPAN(var) β", "SPAN_BETA_VAR  %s, %s, %s\n", saved, cursor, omega);
}

static void emit_asm_break_var(const char *varlab,
                                const char *alpha, const char *beta,
                                const char *gamma, const char *omega,
                                const char *cursor,
                                const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "brk%d_saved", asm_uid());
    bss_add(saved);
    ALFC(alpha, "BREAK(var) α", "BREAK_ALPHA_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta,  "BREAK(var) β", "BREAK_BETA_VAR  %s, %s, %s\n", saved, cursor, omega);
}

static void emit_asm_breakx_var(const char *varlab,
                                 const char *alpha, const char *beta,
                                 const char *gamma, const char *omega,
                                 const char *cursor,
                                 const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "brkx%d_saved", asm_uid());
    bss_add(saved);
    ALFC(alpha, "BREAKX(var) α", "BREAKX_ALPHA_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta,  "BREAKX(var) β", "BREAKX_BETA_VAR  %s, %s, %s\n", saved, cursor, omega);
}

static void emit_asm_breakx_lit(const char *charset, int cslen,
                                 const char *alpha, const char *beta,
                                 const char *gamma, const char *omega,
                                 const char *cursor,
                                 const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "brkx%d_saved", asm_uid());
    bss_add(saved);
    const char *clabel = lit_intern(charset, cslen);
    ALFC(alpha, "BREAKX(lit) α", "BREAKX_ALPHA_LIT %s, %s, %s, %s, %s, %s, %s\n",
         clabel, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta,  "BREAKX(lit) β", "BREAKX_BETA_LIT  %s, %s, %s\n", saved, cursor, omega);
}

static void emit_asm_any_var(const char *varlab,
                              const char *alpha, const char *beta,
                              const char *gamma, const char *omega,
                              const char *cursor,
                              const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "any%d_saved", asm_uid());
    bss_add(saved);
    ALFC(alpha, "ANY(var) α", "ANY_ALPHA_VAR   %s, %s, %s, %s, %s, %s, %s\n",
         varlab, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta,  "ANY(var) β", "ANY_BETA_VAR    %s, %s, %s\n", saved, cursor, omega);
}

static void emit_asm_notany_var(const char *varlab,
                                 const char *alpha, const char *beta,
                                 const char *gamma, const char *omega,
                                 const char *cursor,
                                 const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "nany%d_saved", asm_uid());
    bss_add(saved);
    ALFC(alpha, "NOTANY(var) α", "NOTANY_ALPHA_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, saved, cursor, subj, subj_len_sym, gamma, omega);
    ALFC(beta,  "NOTANY(var) β", "NOTANY_BETA_VAR  %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_len — LEN(N)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_asm_len(long n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "len%d_saved", uid);
    bss_add(saved);

    ALFC(alpha, "LEN(%ld)", "LEN_ALPHA   %ld, %s, %s, %s, %s, %s\n", n, saved, cursor, subj_len_sym, gamma, omega);
    ALFC(beta, "LEN β", "LEN_BETA    %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_tab — TAB(N)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_asm_tab(long n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "tab%d_saved", uid);
    bss_add(saved);

    ALFC(alpha, "TAB(%ld)", "TAB_ALPHA   %ld, %s, %s, %s, %s\n", n, saved, cursor, gamma, omega);
    ALFC(beta, "TAB β", "TAB_BETA    %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_rtab — RTAB(N)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_asm_rtab(long n,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "rtab%d_saved", uid);
    bss_add(saved);

    ALFC(alpha, "RTAB(%ld)", "RTAB_ALPHA  %ld, %s, %s, %s, %s, %s\n", n, saved, cursor, subj_len_sym, gamma, omega);
    ALFC(beta, "RTAB β", "RTAB_BETA   %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_rem — REM  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_asm_rem(const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj_len_sym) {
    char saved[LBUF];
    int uid = asm_uid();
    snprintf(saved, LBUF, "rem%d_saved", uid);
    bss_add(saved);

    ALFC(alpha, "REM", "REM_ALPHA   %s, %s, %s, %s\n", saved, cursor, subj_len_sym, gamma);
    ALFC(beta, "REM β", "REM_BETA    %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_arb — ARB: match minimum (0 chars), then backtrack to extend
 *
 * ARB is like ARBNO(''): tries 0 chars first, then on backtrack extends
 * by 1 char at a time up to subj_len - cursor.
 *
 * Design (flat .bss):
 *   arb_N_start: cursor position when ARB entered (for β restore)
 *   arb_N_step:  current step count (0, 1, 2, ...)
 *
 *   α: save cursor + step=0; set cursor=start+0; goto γ
 *   β: step++; if start+step > subj_len: goto ω; set cursor=start+step; goto γ
 * ----------------------------------------------------------------------- */

static void emit_asm_arb(const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj_len_sym) {
    char arb_start[LBUF], arb_step[LBUF], chk[LBUF];
    int uid = asm_uid();
    snprintf(arb_start, LBUF, "arb%d_start", uid);
    snprintf(arb_step,  LBUF, "arb%d_step",  uid);
    snprintf(chk,       LBUF, "arb%d_chk",   uid);
    bss_add(arb_start);
    bss_add(arb_step);

    ALFC(alpha, "ARB", "mov     rax, [%s]\n", cursor);
    A("    mov     [%s], rax\n", arb_start);
    A("    mov     qword [%s], 0\n", arb_step);
    /* cursor stays at start (0-length match first) */
    asmJ(gamma);

    ALFC(beta, "ARB", "mov     rax, [%s]\n", arb_step);
    A("    inc     rax\n");
    A("    mov     [%s], rax\n", arb_step);
    /* check start + step <= subj_len */
    A("    mov     rbx, [%s]\n", arb_start);
    A("    add     rbx, rax\n");
    A("    cmp     rbx, [%s]\n", subj_len_sym);
    asmJg(omega);
    A("    mov     [%s], rbx\n", cursor);
    asmJ(gamma);
    (void)chk;
}

/* -----------------------------------------------------------------------
 * emit_asm_assign — E_DOL (expr $ var) — immediate assignment
 *
 * Byrd Box (v311.sil ENMI, Proebsting §4.3):
 *   dol_α:    save cursor → entry_cursor; goto child_α
 *   dol_β:    → child_β           (transparent backtrack)
 *   child_γ → dol_γ:
 *               span = subject[entry_cursor .. cursor]
 *               memcpy → cap_buf; cap_len = cursor - entry_cursor
 *               goto γ            (succeed; assignment done — no rollback)
 *   child_ω → dol_ω: goto ω      (no assignment performed)
 *
 * The capture variable name (pat->right->sval) is used to derive
 * the .bss buffer names:  cap_<varname>_N_buf  (resb 256)
 *                          cap_<varname>_N_len  (resq 1)
 *                          dol_<N>_entry        (resq 1)
 *
 * The buffer is emitted via asm_extra_bss (like ARBNO stacks).
 * ----------------------------------------------------------------------- */

static void emit_asm_assign(EXPR_t *child, const char *varname,
                             const char *alpha, const char *beta,
                             const char *gamma, const char *omega,
                             const char *cursor,
                             const char *subj, const char *subj_len_sym,
                             int depth) {
    int uid = asm_uid();
    char cα[LBUF], cβ[LBUF];
    char dol_gamma[LBUF], dol_omega[LBUF];
    char entry_cur[LBUF], cap_buf[LBUF], cap_len[LBUF];

    /* derive safe varname prefix (expand special chars) */
    char safe[64];
    asm_expand_name(varname ? varname : "", safe, sizeof safe);
    if (!safe[0]) { safe[0]='v'; safe[1]='\0'; }

    snprintf(cα,        LBUF, "dol%d_child_alpha", uid);
    snprintf(cβ,        LBUF, "dol%d_child_beta",  uid);
    snprintf(dol_gamma, LBUF, "dol%d_gamma",        uid);
    snprintf(dol_omega, LBUF, "dol%d_omega",        uid);
    snprintf(entry_cur, LBUF, "dol%d_entry",        uid);

    /* Per-variable capture buffers — register var and get its symbols */
    CaptureVar *cv = cap_var_register(varname ? varname : "cap");
    if (cv) {
        snprintf(cap_buf, LBUF, "%s", cv->buf_sym);
        snprintf(cap_len, LBUF, "%s", cv->len_sym);
        /* Use per-var entry cursor (pre-registered in .bss by pre-scan) */
        snprintf(entry_cur, LBUF, "dol_entry_%s", cv->safe);
    } else {
        snprintf(cap_buf,   LBUF, "cap_fallback_buf");
        snprintf(cap_len,   LBUF, "cap_fallback_len");
        snprintf(entry_cur, LBUF, "dol%d_entry", uid);
    }

    /* bss_add is a no-op if already registered; harmless in single-pass mode */
    bss_add(entry_cur);

    {
        char dol_hdr[LBUF];
        snprintf(dol_hdr, LBUF, "DOL(%s $  %s)", varname ? varname : "?", safe);
        A("\n");
        /* α: header comment on label line — "alpha: ; DOL(var $ var)" */
        asmLC(alpha, dol_hdr);
    }

    /* α instruction: save entry cursor, enter child — one macro call */
    ALFC("", "DOL α — save entry cursor",
         "DOL_SAVE    %s, %s, %s\n", entry_cur, cursor, cα);

    /* β: transparent — backtrack into child */
    ALFC(beta, "DOL β", "jmp     %s\n", cβ);

    /* Emit child subtree — child's γ goes to dol_gamma, child's ω to dol_omega */
    emit_asm_node(child, cα, cβ, dol_gamma, dol_omega,
                  cursor, subj, subj_len_sym, depth + 1);

    /* dol_γ: compute span, copy to cap_buf, proceed to outer γ — one macro call */
    ALFC(dol_gamma, "DOL γ — capture span",
         "DOL_CAPTURE %s, %s, %s, %s, %s, %s\n",
         entry_cur, cursor, cap_buf, cap_len, subj, gamma);

    /* dol_ω: child failed — no assignment, propagate failure */
    ALFC(dol_omega, "DOL ω — child failed", "jmp     %s\n", omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_node — recursive dispatch
 * ----------------------------------------------------------------------- */

static void emit_asm_node(EXPR_t *pat,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj, const char *subj_len_sym,
                           int depth) {
    if (!pat) return;

    switch (pat->kind) {
    case E_QLIT:
        emit_asm_lit(pat->sval, (int)strlen(pat->sval),
                     alpha, beta, gamma, omega,
                     cursor, subj, subj_len_sym);
        break;

    case E_CONC:
        emit_asm_seq(pat->left, pat->right,
                     alpha, beta, gamma, omega,
                     cursor, subj, subj_len_sym, depth);
        break;

    case E_OR:
        emit_asm_alt(pat->left, pat->right,
                     alpha, beta, gamma, omega,
                     cursor, subj, subj_len_sym, depth);
        break;

    case E_VART: {
        /* Variable reference in pattern context — named pattern call site.
         * BUT: zero-arg builtins (REM, ARB, FAIL) appear as E_VART with no
         * parens. Intercept them before the named-pattern lookup. */
        const char *varname = pat->sval ? pat->sval : "";
        if (strcasecmp(varname, "REM") == 0) {
            emit_asm_rem(alpha, beta, gamma, omega, cursor, subj_len_sym);
            break;
        }
        if (strcasecmp(varname, "ARB") == 0) {
            emit_asm_arb(alpha, beta, gamma, omega, cursor, subj_len_sym);
            break;
        }
        if (strcasecmp(varname, "FAIL") == 0) {
            A("\n; FAIL  α=%s\n", alpha);
            asmL(alpha);
            ALF(beta, "jmp     %s\n", omega);
            break;
        }
        /* Look up in the named-pattern registry */
        const AsmNamedPat *np = asm_named_lookup(varname);
        if (np) {
            emit_asm_named_ref(np, alpha, beta, gamma, omega);
        } else {
            /* Fall back to plain-string variable registry (PAT = 'literal') */
            const AsmStrVar *sv = asm_str_var_lookup(varname);
            if (sv && sv->sval) {
                A("\n; E_VART *%s → inline LIT '%s'\n", varname, sv->sval);
                emit_asm_lit(sv->sval, (int)strlen(sv->sval),
                             alpha, beta, gamma, omega,
                             cursor, subj, subj_len_sym);
            } else {
                A("\n; UNRESOLVED named pattern ref: %s → ω\n", varname);
                asmL(alpha);
                ALF(beta, "jmp     %s\n", omega);
            }
        }
        break;
    }

    case E_DOL: {
        /* expr $ var — immediate assignment.
         * left = sub-pattern, right = capture variable (E_VART). */
        const char *varname = (pat->right && pat->right->sval)
                              ? pat->right->sval : "cap";
        emit_asm_assign(pat->left, varname,
                        alpha, beta, gamma, omega,
                        cursor, subj, subj_len_sym, depth);
        break;
    }

    case E_NAM: {
        /* expr . var — conditional assignment. */
        const char *varname = (pat->right && pat->right->sval)
                              ? pat->right->sval : "cap";
        emit_asm_assign(pat->left, varname,
                        alpha, beta, gamma, omega,
                        cursor, subj, subj_len_sym, depth);
        break;
    }

    case E_INDR: {
        /* *VAR — indirect pattern reference.
         * pat->left is E_VART holding the variable name.
         * Look it up in the named-pattern registry and call it.
         * If not a named pattern, try asm_str_vars for plain-string vars
         * (e.g. PAT = 'hello' → *PAT emitted as inline LIT). */
        const char *varname = (pat->left && pat->left->sval)
                              ? pat->left->sval
                              : (pat->sval ? pat->sval : NULL);
        if (varname) {
            const AsmNamedPat *np = asm_named_lookup(varname);
            if (np) {
                emit_asm_named_ref(np, alpha, beta, gamma, omega);
            } else {
                /* Try plain-string variable registry */
                const AsmStrVar *sv = asm_str_var_lookup(varname);
                if (sv && sv->sval) {
                    A("\n; E_INDR *%s → inline LIT '%s'\n", varname, sv->sval);
                    emit_asm_lit(sv->sval, (int)strlen(sv->sval),
                                 alpha, beta, gamma, omega,
                                 cursor, subj, subj_len_sym);
                } else {
                    A("\n; E_INDR unresolved: %s → ω\n", varname);
                    asmL(alpha); ALF(beta, "jmp     %s\n", omega);
                }
            }
        } else {
            A("\n; E_INDR no varname → ω\n");
            asmL(alpha); ALF(beta, "jmp     %s\n", omega);
        }
        break;
    }

    case E_FNC:
        if (pat->sval && strcasecmp(pat->sval, "POS") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                const char *varlab = prog_str_intern(arg->sval);
                emit_asm_pos_var(varlab, alpha, beta, gamma, omega, cursor);
            } else {
                long n = (arg->kind == E_ILIT) ? arg->ival : 0;
                emit_asm_pos(n, alpha, beta, gamma, omega, cursor);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "RPOS") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                const char *varlab = prog_str_intern(arg->sval);
                emit_asm_rpos_var(varlab, alpha, beta, gamma, omega, cursor, subj_len_sym);
            } else {
                long n = (arg->kind == E_ILIT) ? arg->ival : 0;
                emit_asm_rpos(n, alpha, beta, gamma, omega, cursor, subj_len_sym);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "ARBNO") == 0 && pat->nargs == 1) {
            emit_asm_arbno(pat->args[0],
                           alpha, beta, gamma, omega,
                           cursor, subj, subj_len_sym, depth);
        } else if (pat->sval && strcasecmp(pat->sval, "ANY") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_asm_any_var(prog_str_intern(arg->sval), alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_asm_any(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "NOTANY") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_asm_notany_var(prog_str_intern(arg->sval), alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_asm_notany(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "SPAN") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_asm_span_var(prog_str_intern(arg->sval), alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_asm_span(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "BREAK") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_asm_break_var(prog_str_intern(arg->sval), alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_asm_break(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "BREAKX") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_asm_breakx_var(prog_str_intern(arg->sval), alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_asm_breakx_lit(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "LEN") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_asm_len(n, alpha, beta, gamma, omega, cursor, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "TAB") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_asm_tab(n, alpha, beta, gamma, omega, cursor);
        } else if (pat->sval && strcasecmp(pat->sval, "RTAB") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_asm_rtab(n, alpha, beta, gamma, omega, cursor, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "REM") == 0 && pat->nargs == 0) {
            emit_asm_rem(alpha, beta, gamma, omega, cursor, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "ARB") == 0 && pat->nargs == 0) {
            emit_asm_arb(alpha, beta, gamma, omega, cursor, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "FAIL") == 0) {
            /* FAIL always fails — alpha and beta both jump to omega */
            A("\n; FAIL  α=%s\n", alpha);
            asmL(alpha);
            ALF(beta, "jmp     %s\n", omega);
        } else {
            /* Unimplemented function — emit a comment + omega jump */
            A("\n; UNIMPLEMENTED: %s() → ω\n", pat->sval ? pat->sval : "?");
            asmL(alpha);
            ALF(beta, "jmp     %s\n", omega);
        }
        break;

    case E_ATP: {
        /* @VAR — cursor-position capture: store cursor as integer into VAR, always succeed */
        const char *varname = pat->sval ? pat->sval : "";
        const char *varlab  = prog_str_intern(varname);
        ALFC(alpha, "@VAR α", "AT_ALPHA        %s, %s, %s, %s\n",
             varlab, cursor, gamma, omega);
        ALFC(beta,  "@VAR β", "AT_BETA         %s\n", omega);
        break;
    }

    default:
        A("\n; UNIMPLEMENTED node kind %d → ω\n", pat->kind);
        asmL(alpha);
        ALF(beta, "jmp     %s\n", omega);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Extra .bss entries for ARBNO stacks (resq 64 each)
 * ----------------------------------------------------------------------- */

char asm_extra_bss[64][128];
int  asm_extra_bss_count = 0;

static void extra_bss_emit(void) {
    for (int i = 0; i < asm_extra_bss_count; i++)
        A("%s\n", asm_extra_bss[i]);
}

/* -----------------------------------------------------------------------
 * Per-variable capture registry
 *
 * Each . or $ capture against a distinct variable gets its own pair:
 *   cap_VAR_buf  resb 256   — captured bytes
 *   cap_VAR_len  resq 1     — captured length (UINT64_MAX = no capture)
 *
 * The harness walks cap_order[] at match_success to print non-empty caps.
 * ----------------------------------------------------------------------- */

static CaptureVar cap_vars[MAX_CAP_VARS];
static int cap_var_count = 0;

static void cap_vars_reset(void) { cap_var_count = 0; }

/* Register (or look up) a capture variable; returns the entry. */
static CaptureVar *cap_var_register(const char *varname) {
    for (int i = 0; i < cap_var_count; i++)
        if (strcmp(cap_vars[i].varname, varname) == 0)
            return &cap_vars[i];
    if (cap_var_count >= MAX_CAP_VARS) return NULL;
    CaptureVar *cv = &cap_vars[cap_var_count++];
    strncpy(cv->varname, varname, sizeof cv->varname - 1);
    cv->varname[sizeof cv->varname - 1] = '\0';
    /* build safe label fragment using expansion table */
    asm_expand_name(varname, cv->safe, sizeof cv->safe);
    if (!cv->safe[0]) { cv->safe[0] = 'v'; cv->safe[1] = '\0'; }
    snprintf(cv->buf_sym, sizeof cv->buf_sym, "cap_%s_buf", cv->safe);
    snprintf(cv->len_sym, sizeof cv->len_sym, "cap_%s_len", cv->safe);
    return cv;
}

/* Emit .bss lines for all registered capture variables */
static void cap_vars_emit_bss(void) {
    for (int i = 0; i < cap_var_count; i++) {
        A("%-24s resb 256\n", cap_vars[i].buf_sym);
        A("%-24s resq 1\n",   cap_vars[i].len_sym);
    }
}

/* Emit section .data cap_order table for the harness */
static void cap_vars_emit_order(void) {
    if (cap_var_count == 0) return;
    A("\n; cap_order table — harness walks this at match_success\n");
    /* Emit name strings */
    for (int i = 0; i < cap_var_count; i++)
        A("cap_name_%s  db  ", cap_vars[i].safe);
    /* We can't easily put a for loop with string literals cleanly, so
     * emit each cap_name_X as: db 'A', 0 */
    /* Reset and do it properly */
    /* (The A() calls above emitted "cap_name_X  db  " — we need to back out.
     *  Better: just do it all in one pass.) */
    /* NOTE: the above approach is broken — we need to emit complete lines.
     * Start over with a clean approach using a separate pass. */
    /* This function is called after header externs, in section .data context. */
}

/* Emit cap_order in .data section — each entry: {ptr-to-name, ptr-to-buf, ptr-to-len} */
static void cap_vars_emit_data_section(void) {
    if (cap_var_count == 0) return;
    A("\n; per-variable capture name strings\n");
    for (int i = 0; i < cap_var_count; i++) {
        A("cap_name_%-16s db  ", cap_vars[i].safe);
        for (int j = 0; cap_vars[i].varname[j]; j++) {
            if (j) A(", ");
            A("%d", (unsigned char)cap_vars[i].varname[j]);
        }
        A(", 0\n");
    }
    A("\n; cap_order[] = { name*, buf*, len* } ... null-terminated\n");
    A("cap_order:\n");
    for (int i = 0; i < cap_var_count; i++) {
        A("    dq  cap_name_%-12s", cap_vars[i].safe);
        A(", %s", cap_vars[i].buf_sym);
        A(", %s\n", cap_vars[i].len_sym);
    }
    A("    dq  0, 0, 0\n");  /* sentinel */
    A("cap_order_count  dq  %d\n", cap_var_count);
}

/* -----------------------------------------------------------------------
 * Named pattern registry
 *
 * Maps SNOBOL4 variable names (e.g. "ASTAR") to their flat ASM labels.
 * Each named pattern gets two entry labels:
 *   pat_NAME_alpha  — initial match entry
 *   pat_NAME_beta   — backtrack entry
 * and two .bss continuation slots:
 *   pat_NAME_ret_gamma  — caller fills with γ address before jmp
 *   pat_NAME_ret_omega  — caller fills with ω address before jmp
 * (AsmNamedPat struct defined above as forward decl)
 * ----------------------------------------------------------------------- */

#define ASM_NAMED_MAX 64
#define ASM_NAMED_NAMELEN 128

static AsmNamedPat asm_named[ASM_NAMED_MAX];
static int         asm_named_count = 0;

static void asm_named_reset(void) { asm_named_count = 0; }

/* asm_str_vars — registry for plain-string variable assignments (VAR = 'literal').
 * Used by E_INDR (*VAR) when the variable holds a string, not a named pattern.
 * At compile time we know the literal value, so we emit an inline LIT. */
#define ASM_STR_VARS_MAX 64
static AsmStrVar asm_str_vars[ASM_STR_VARS_MAX];
static int       asm_str_vars_count = 0;

static void asm_str_vars_reset(void) { asm_str_vars_count = 0; }

static void asm_str_var_register(const char *varname, const char *sval) {
    for (int i = 0; i < asm_str_vars_count; i++)
        if (strcasecmp(asm_str_vars[i].varname, varname) == 0) {
            asm_str_vars[i].sval = sval; return;
        }
    if (asm_str_vars_count >= ASM_STR_VARS_MAX) return;
    AsmStrVar *e = &asm_str_vars[asm_str_vars_count++];
    snprintf(e->varname, ASM_NAMED_NAMELEN, "%s", varname);
    e->sval = sval;
}

static const AsmStrVar *asm_str_var_lookup(const char *varname) {
    for (int i = 0; i < asm_str_vars_count; i++)
        if (strcasecmp(asm_str_vars[i].varname, varname) == 0)
            return &asm_str_vars[i];
    return NULL;
}

/* Make a label-safe version of a SNOBOL4 variable name — use expansion table */
static void asm_safe_name(const char *src, char *dst, int dstlen) {
    asm_expand_name(src, dst, dstlen);
    if (!dst[0]) { dst[0] = 'v'; dst[1] = '\0'; }
}

/* Register a named pattern — called during program scan */
static AsmNamedPat *asm_named_register(const char *varname, EXPR_t *pat) {
    /* dedup */
    for (int i = 0; i < asm_named_count; i++)
        if (strcmp(asm_named[i].varname, varname) == 0) {
            if (pat) asm_named[i].pat = pat; /* update expr if provided */
            return &asm_named[i];
        }
    if (asm_named_count >= ASM_NAMED_MAX) return NULL;
    AsmNamedPat *e = &asm_named[asm_named_count++];
    snprintf(e->varname, ASM_NAMED_NAMELEN, "%s", varname);
    asm_safe_name(varname, e->safe, ASM_NAMED_NAMELEN);
    snprintf(e->alpha_lbl, ASM_NAMED_NAMELEN, "P_%s_α", e->safe);
    snprintf(e->beta_lbl,  ASM_NAMED_NAMELEN, "P_%s_β", e->safe);
    snprintf(e->ret_gamma, ASM_NAMED_NAMELEN, "P_%s_ret_γ", e->safe);
    snprintf(e->ret_omega, ASM_NAMED_NAMELEN, "P_%s_ret_ω", e->safe);
    e->pat = pat;
    e->is_fn = 0;
    e->nparams = 0;
    e->body_label[0] = '\0';
    memset(e->param_names, 0, sizeof e->param_names);
    return e;
}

static const AsmNamedPat *asm_named_lookup(const char *varname) {
    for (int i = 0; i < asm_named_count; i++)
        if (strcasecmp(asm_named[i].varname, varname) == 0)
            return &asm_named[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * emit_asm_named_ref — call site for a named pattern reference (E_VART)
 *
 * Emits:
 *   alpha:  store γ → pat_NAME_ret_gamma; store ω → pat_NAME_ret_omega
 *           jmp pat_NAME_alpha
 *   beta:   store γ → pat_NAME_ret_gamma; store ω → pat_NAME_ret_omega
 *           jmp pat_NAME_beta
 * ----------------------------------------------------------------------- */

static void emit_asm_named_ref(const AsmNamedPat *np,
                                const char *alpha, const char *beta,
                                const char *gamma, const char *omega) {
    int uid = asm_uid();
    char glbl[LBUF], olbl[LBUF];
    snprintf(glbl, LBUF, "nref%d_gamma", uid);
    snprintf(olbl, LBUF, "nref%d_omega", uid);

    {
        char ref_hdr[LBUF];
        snprintf(ref_hdr, LBUF, "REF(%s)", np->varname);
        A("\n");
        /* α: header comment on label line — "alpha: ; REF(PatName)" */
        asmLC(alpha, ref_hdr);
    }

    /* α instructions: load continuations, jump to named pattern α */
    A("    lea     rax, [rel %s]\n", glbl);
    A("    mov     [%s], rax\n", np->ret_gamma);
    A("    lea     rax, [rel %s]\n", olbl);
    A("    mov     [%s], rax\n", np->ret_omega);
    A("    jmp     %s\n", np->alpha_lbl);

    /* β: reload continuations (caller may have changed them), jump to β */
    ALFC(beta, "REF(%s)", "lea     rax, [rel %s]\n", glbl);
    A("    mov     [%s], rax\n", np->ret_gamma);
    A("    lea     rax, [rel %s]\n", olbl);
    A("    mov     [%s], rax\n", np->ret_omega);
    A("    jmp     %s\n", np->beta_lbl);

    /* γ and ω trampolines — named pattern jumps here, we forward */
    A("\n%s:\n", glbl);
    asmJ(gamma);
    asmL(olbl);
    asmJ(omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_named_def — emit the body of a named pattern
 *
 * Emits:
 *   pat_NAME_alpha:  <recursive Byrd box for the pattern expression>
 *                    (inner γ → jmp [pat_NAME_ret_gamma])
 *                    (inner ω → jmp [pat_NAME_ret_omega])
 *   pat_NAME_beta:   <same, backtrack entry>
 * ----------------------------------------------------------------------- */

static void emit_asm_named_def(const AsmNamedPat *np,
                                const char *cursor,
                                const char *subj,
                                const char *subj_len_sym) {
    /* User-defined function (Snocone procedure): is_fn=1
     * α port: save old param var values → param_save slots,
     *         load arg descriptors from call-site stack → param vars,
     *         jump to function body label.
     * γ port: restore param vars from save slots, jmp [ret_γ].
     * ω port: same restore, jmp [ret_ω] (for FRETURN path).
     * The RETURN inside the body is already routed to jmp [ret_γ]
     * by emit_jmp/prog_emit_goto when current_fn != NULL. */
    if (np->is_fn) {
        char safe[ASM_NAMED_NAMELEN];
        snprintf(safe, sizeof safe, "%s", np->safe);

        emit_sep_major(np->varname);
        A("; %s — user function α entry (%d param%s)\n",
          np->alpha_lbl, np->nparams, np->nparams==1?"":"s");

        /* α label */
        asmL(np->alpha_lbl);

        /* Save old param variable values and load new args from call-site stack.
         * Call-site convention (see call-site emission below):
         *   The caller pushes args as DESCR_t pairs on the stack BEFORE jumping.
         *   The call-site stack frame (rsp at time of jmp) holds:
         *     [rsp + 0]  = arg0.type (qword)
         *     [rsp + 8]  = arg0.ptr  (qword)
         *     [rsp + 16] = arg1.type, etc.
         *   The caller also sets ret_γ and ret_ω before jumping.
         * We access args via a dedicated .bss arg-slot array. */
        for (int i = 0; i < np->nparams; i++) {
            char pname_safe[128]; asm_expand_name(np->param_names[i], pname_safe, sizeof pname_safe);
            if (!pname_safe[0]) snprintf(pname_safe, sizeof pname_safe, "p%d", i);
            char save_slot_t[260], save_slot_p[260];
            snprintf(save_slot_t, sizeof save_slot_t, "fn_%s_save_%s_t", safe, pname_safe);
            snprintf(save_slot_p, sizeof save_slot_p, "fn_%s_save_%s_p", safe, pname_safe);
            char arg_slot_t[260], arg_slot_p[260];
            snprintf(arg_slot_t, sizeof arg_slot_t, "fn_%s_arg_%d_t", safe, i);
            snprintf(arg_slot_p, sizeof arg_slot_p, "fn_%s_arg_%d_p", safe, i);
            const char *plab = prog_str_intern(np->param_names[i]);

            /* Save old value of param variable: GET_VAR → [rbp-16/rbp-8], then stash */
            A("    GET_VAR     %s\n", plab);
            A("    mov     rax, [rbp-16]\n");
            A("    mov     rdx, [rbp-8]\n");
            A("    mov     [%s], rax\n", save_slot_t);
            A("    mov     [%s], rdx\n", save_slot_p);

            /* Load arg from arg_slot_t/p into param variable */
            A("    lea     rdi, [rel %s]\n", plab);
            A("    mov     rsi, [%s]\n", arg_slot_t);
            A("    mov     rdx, [%s]\n", arg_slot_p);
            A("    call    stmt_set\n");
        }
        /* Jump to function body */
        A("    jmp     %s\n", prog_label_nasm(np->body_label));

        emit_sep_minor("γ/ω");

        /* γ: restore params, jump via ret_γ (RETURN path) */
        char gamma_lbl[LBUF], omega_lbl[LBUF];
        snprintf(gamma_lbl, LBUF, "fn_%s_gamma", safe);
        snprintf(omega_lbl, LBUF, "fn_%s_omega", safe);
        asmL(gamma_lbl);
        for (int i = np->nparams - 1; i >= 0; i--) {
            char pname_safe[128]; asm_expand_name(np->param_names[i], pname_safe, sizeof pname_safe);
            if (!pname_safe[0]) snprintf(pname_safe, sizeof pname_safe, "p%d", i);
            char save_slot_t[260], save_slot_p[260];
            snprintf(save_slot_t, sizeof save_slot_t, "fn_%s_save_%s_t", safe, pname_safe);
            snprintf(save_slot_p, sizeof save_slot_p, "fn_%s_save_%s_p", safe, pname_safe);
            const char *plab = prog_str_intern(np->param_names[i]);
            A("    lea     rdi, [rel %s]\n", plab);
            A("    mov     rsi, [%s]\n", save_slot_t);
            A("    mov     rdx, [%s]\n", save_slot_p);
            A("    call    stmt_set\n");
        }
        A("    jmp     [%s]\n", np->ret_gamma);

        /* ω: restore params, jump via ret_ω (FRETURN path) */
        asmL(omega_lbl);
        for (int i = np->nparams - 1; i >= 0; i--) {
            char pname_safe[128]; asm_expand_name(np->param_names[i], pname_safe, sizeof pname_safe);
            if (!pname_safe[0]) snprintf(pname_safe, sizeof pname_safe, "p%d", i);
            char save_slot_t[260], save_slot_p[260];
            snprintf(save_slot_t, sizeof save_slot_t, "fn_%s_save_%s_t", safe, pname_safe);
            snprintf(save_slot_p, sizeof save_slot_p, "fn_%s_save_%s_p", safe, pname_safe);
            const char *plab = prog_str_intern(np->param_names[i]);
            A("    lea     rdi, [rel %s]\n", plab);
            A("    mov     rsi, [%s]\n", save_slot_t);
            A("    mov     rdx, [%s]\n", save_slot_p);
            A("    call    stmt_set\n");
        }
        A("    jmp     [%s]\n", np->ret_omega);
        return;
    }

    if (!np->pat) {
        /* No pattern expression — emit stubs that always fail */
        A("\n; Named pattern %s — no expression, always fails\n", np->varname);
        asmL(np->alpha_lbl);
        ALF(np->beta_lbl, "jmp     [%s]\n", np->ret_omega);
        return;
    }

    /* The named pattern's inner γ and ω connect back via the ret_ slots */
    char inner_gamma[LBUF], inner_omega[LBUF];
    snprintf(inner_gamma, LBUF, "patdef_%s_gamma", np->safe);
    snprintf(inner_omega, LBUF, "patdef_%s_omega", np->safe);

    /* Major separator: named pattern header */
    emit_sep_major(np->varname);

    /* α entry — initial match */
    A("; %s (α entry)\n", np->alpha_lbl);
    emit_asm_node(np->pat,
                  np->alpha_lbl, np->beta_lbl,
                  inner_gamma, inner_omega,
                  cursor, subj, subj_len_sym,
                  1);

    /* Minor separator before γ/ω trampolines */
    emit_sep_minor("γ/ω");

    /* γ trampoline: indirect jump to caller's continuation */
    A("%s:\n", inner_gamma);
    A("    jmp     [%s]\n", np->ret_gamma);

    /* ω trampoline */
    asmL(inner_omega);
    A("    jmp     [%s]\n", np->ret_omega);
}

/* -----------------------------------------------------------------------
 * asm_emit_null_program — Sprint A0 fallback
 * ----------------------------------------------------------------------- */

static void asm_emit_null_program(void) {
    A("; generated by sno2c -asm\n");
    A("    global _start\n");
    flush_pending_label();
    A("section .text\n");
    A("_start:\n");
    A("    mov     eax, 60\n");
    A("    xor     edi, edi\n");
    A("    syscall\n");
}

/* -----------------------------------------------------------------------
 * expr_is_pattern_expr — return 1 if expression tree is a genuine
 * pattern-building expression (contains E_FNC, E_OR, or E_CONC with
 * at least one non-literal/non-value child).
 * Pure value assignments (E_VART, E_QLIT, E_ILIT alone, or
 * E_CONC/E_OR where ALL leaves are literals or variable refs with no
 * pattern-function calls) are NOT named patterns in program context. */
static int expr_has_pattern_fn(EXPR_t *e) {
    if (!e) return 0;
    /* E_FNC nodes that are pattern builtins make this a pattern expr */
    if (e->kind == E_FNC) return 1;
    /* E_NAM (. capture) and E_DOL ($ capture) wrap a pattern child */
    if (e->kind == E_NAM || e->kind == E_DOL) return 1;
    if (expr_has_pattern_fn(e->left))  return 1;
    if (expr_has_pattern_fn(e->right)) return 1;
    for (int i = 0; i < e->nargs; i++)
        if (expr_has_pattern_fn(e->args[i])) return 1;
    return 0;
}
static int expr_is_pattern_expr(EXPR_t *e) {
    if (!e) return 0;
    /* E_OR (alternation, i.e. P = 'a' | 'b') is always a pattern expression —
     * alternation has no meaning in pure value context. */
    if (e->kind == E_OR) return 1;
    /* E_CONC (concatenation) is a pattern expression only when it contains
     * a pattern function call. Pure literal concat like 'hello' ' world'
     * is a VALUE expression (string join), not a pattern. */
    if (e->kind == E_CONC) return expr_has_pattern_fn(e);
    /* Any function call is a pattern expression */
    return expr_has_pattern_fn(e);
}

/* asm_scan_named_patterns — walk the program and register all
 * pattern assignments (VAR = <pattern-expr>) before emitting.
 * Also detects Snocone sc_cf-generated DEFINE stmts and registers
 * user-defined procedures as is_fn=1 entries.
 * ----------------------------------------------------------------------- */

/* Parse "fname(arg1,arg2,...)" from a DEFINE string literal.
 * Writes function name into fname_out (fname_max bytes),
 * param names into params[][] (nparams_out count).
 * Returns 1 on success, 0 on parse failure. */
static int parse_define_str(const char *def,
                             char *fname_out, int fname_max,
                             char params[ASM_NAMED_MAXPARAMS][64],
                             int *nparams_out) {
    *nparams_out = 0;
    /* Copy up to '(' into fname */
    const char *p = def;
    int fi = 0;
    while (*p && *p != '(' && fi < fname_max - 1)
        fname_out[fi++] = *p++;
    fname_out[fi] = '\0';
    if (!fi) return 0;  /* no function name */
    if (*p != '(') return 1; /* no args — zero params */
    p++; /* skip '(' */
    while (*p && *p != ')') {
        /* skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ')' || !*p) break;
        /* read param name up to ',' or ')' */
        int pi = 0;
        while (*p && *p != ',' && *p != ')' && pi < 63)
            params[*nparams_out][pi++] = *p++;
        /* trim trailing whitespace */
        while (pi > 0 && (params[*nparams_out][pi-1]==' '||
                           params[*nparams_out][pi-1]=='\t')) pi--;
        params[*nparams_out][pi] = '\0';
        if (pi > 0) {
            (*nparams_out)++;
            if (*nparams_out >= ASM_NAMED_MAXPARAMS) break;
        }
        if (*p == ',') p++;
    }
    return 1;
}

static void asm_scan_named_patterns(Program *prog) {
    asm_named_reset();
    asm_str_vars_reset();
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* Detect Snocone sc_cf DEFINE statement:
         * subject = E_FNC("DEFINE", E_QLIT("fname(args)"))
         * has_eq = 0, no pattern.
         * Register as user function (is_fn=1). */
        if (s->subject && s->subject->kind == E_FNC &&
            s->subject->sval && strcasecmp(s->subject->sval, "DEFINE") == 0 &&
            s->subject->nargs == 1 &&
            s->subject->args[0] && s->subject->args[0]->kind == E_QLIT &&
            s->subject->args[0]->sval) {
            const char *def_str = s->subject->args[0]->sval;
            char fname[128];
            char params[ASM_NAMED_MAXPARAMS][64];
            int nparams = 0;
            if (parse_define_str(def_str, fname, sizeof fname, params, &nparams)) {
                AsmNamedPat *e = asm_named_register(fname, NULL);
                if (e) {
                    e->is_fn = 1;
                    e->nparams = nparams;
                    for (int i = 0; i < nparams; i++)
                        snprintf(e->param_names[i], 64, "%s", params[i]);
                    /* body_label = NASM form of fname (the function entry label) */
                    snprintf(e->body_label, sizeof e->body_label, "%s", fname);
                }
            }
            continue;
        }

        /* A pattern assignment: subject is a variable, no pattern field,
         * has_eq is set, replacement holds the pattern expression.
         * E.g.   ASTAR = ARBNO(LIT("a"))
         * In sno2c IR: subject=E_VART("ASTAR"), pattern=NULL, replacement=<expr>
         * Only register if the replacement is genuinely a pattern-building
         * expression (contains E_FNC).  Plain value assignments like
         * X = 'hello' or OUTPUT = X 'world' are NOT named patterns. */
        if (s->subject && s->subject->kind == E_VART &&
            s->has_eq && s->replacement && !s->pattern) {
            if (expr_is_pattern_expr(s->replacement)) {
                asm_named_register(s->subject->sval, s->replacement);
            } else if (s->replacement->kind == E_QLIT && s->replacement->sval) {
                /* Plain string assignment — register for *VAR indirect use */
                asm_str_var_register(s->subject->sval, s->replacement->sval);
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * asm_emit_pattern — emit a single statement's pattern as a standalone
 * match program (for crosscheck testing).
 *
 * Emits:
 *   section .data  — subject, literals
 *   section .bss   — cursor + saved slots + named-pattern ret slots
 *   section .text  — _start + root pattern + named pattern bodies
 * ----------------------------------------------------------------------- */

static void asm_emit_pattern(STMT_t *stmt) {
    if (!stmt || !stmt->pattern) {
        asm_emit_null_program();
        return;
    }

    /* Reset state */
    bss_reset();
    lit_reset();
    asm_extra_bss_count = 0;

    /* Fixed names */
    const char *cursor_sym   = "cursor";
    const char *subj_sym     = "subject_data";
    const char *subj_len_sym = "subject_len_val";

    bss_add(cursor_sym);

    /* Register .bss slots for all named patterns' ret_ pointers */
    for (int i = 0; i < asm_named_count; i++) {
        bss_add(asm_named[i].ret_gamma);
        bss_add(asm_named[i].ret_omega);
    }

    /* Collect root pattern code + named pattern bodies into a temp buffer */
    char *code_buf = NULL; size_t code_size = 0;
    FILE *code_f = open_memstream(&code_buf, &code_size);
    if (!code_f) { asm_emit_null_program(); return; }

    FILE *real_out = asm_out;
    asm_out = code_f;

    /* Determine subject from stmt->subject (E_QLIT) */
    const char *subj_str = "";
    int         subj_len = 0;
    if (stmt->subject && stmt->subject->kind == E_QLIT) {
        subj_str = stmt->subject->sval;
        subj_len = (int)strlen(subj_str);
    }

    /* Emit the root pattern tree */
    emit_asm_node(stmt->pattern,
                  "root_alpha", "root_beta",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_sym,
                  0);

    /* Emit named pattern bodies */
    for (int i = 0; i < asm_named_count; i++)
        emit_asm_named_def(&asm_named[i], cursor_sym, subj_sym, subj_len_sym);

    fclose(code_f);

    /* Now write the full .s file in correct section order */
    asm_out = real_out;

    A("; generated by sno2c -asm\n");
    A("; assemble: nasm -f elf64 out.s -o out.o && ld out.o -o out\n");
    A("    global _start\n");

    /* .data */
    A("\nsection .data\n");
    A("%-20s db ", subj_sym);
    for (int i = 0; i < subj_len; i++) {
        if (i) A(", ");
        A("%d", (unsigned char)subj_str[i]);
    }
    if (subj_len == 0) A("0");
    A("    ; \"%s\"\n", subj_str);
    A("%-20s equ %d\n", "subject_len_imm", subj_len);
    A("newline:             db 10\n");
    for (int i = 0; i < lit_count; i++) {
        if (lit_table[i].len == 0) {
            A("%-20s db 0  ; \"\"\n", lit_table[i].label);
            continue;
        }
        A("%-20s db ", lit_table[i].label);
        for (int j = 0; j < lit_table[i].len; j++) {
            if (j) A(", ");
            A("%d", (unsigned char)lit_table[i].val[j]);
        }
        A("\n");
    }

    /* .bss */
    A("\nsection .bss\n");
    A("%-24s resq 1\n", subj_len_sym);
    for (int i = 0; i < bss_count; i++)
        A("%-24s resq 1\n", bss_slots[i]);
    extra_bss_emit();

    /* .text */
    A("\nsection .text\n");
    A("_start:\n");
    A("    ; init\n");
    A("    mov     qword [%s], %d\n", subj_len_sym, subj_len);
    A("    mov     qword [%s], 0\n", cursor_sym);
    A("    jmp     root_alpha\n");

    /* Paste collected pattern + named pattern code */
    A("%.*s", (int)code_size, code_buf);
    free(code_buf);

    /* match_success / match_fail */
    A("\nmatch_success:\n");
    A("    mov     eax, 60\n");
    A("    xor     edi, edi\n");
    A("    syscall\n");
    A("\nmatch_fail:\n");
    A("    mov     eax, 60\n");
    A("    mov     edi, 1\n");
    A("    syscall\n");
}

/* -----------------------------------------------------------------------
 * asm_emit_body — body-only mode (-asm-body flag)
 *
 * Emits the pattern code for linking with snobol4_asm_harness.c.
 * Instead of _start / match_success / match_fail:
 *   - Declares:  global root_alpha, root_beta
 *   - Externs:   cursor, subject_data, subject_len_val, match_success, match_fail
 * The harness C file provides all those externs.
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * asm_scan_capture_vars — pre-pass: walk pattern tree and register every
 * DOL/NAM capture variable so cap_vars[] is fully populated before we
 * emit any sections.  Eliminates the need for a two-pass memstream.
 * ----------------------------------------------------------------------- */

static void asm_scan_expr_for_caps(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_DOL || e->kind == E_NAM) {
        const char *vn = (e->right && e->right->sval) ? e->right->sval : "cap";
        cap_var_register(vn);
    }
    /* recurse into all children */
    asm_scan_expr_for_caps(e->left);
    asm_scan_expr_for_caps(e->right);
    for (int i = 0; i < e->nargs; i++)
        asm_scan_expr_for_caps(e->args[i]);
}

static void asm_scan_capture_vars(STMT_t *stmt) {
    cap_vars_reset();
    if (!stmt || !stmt->pattern) return;
    asm_scan_expr_for_caps(stmt->pattern);
    /* Also scan named pattern bodies */
    for (int i = 0; i < asm_named_count; i++)
        asm_scan_expr_for_caps(asm_named[i].pat);
}

/* -----------------------------------------------------------------------
 * asm_emit_body — body-only mode (-asm-body flag)
 *
 * Single-pass: pre-scan registers all caps + named patterns first,
 * then emit header → .data → .bss → .text in one forward pass.
 * No open_memstream / two-pass needed.
 * ----------------------------------------------------------------------- */

static void asm_emit_body(STMT_t *stmt) {
    if (!stmt || !stmt->pattern) return;

    bss_reset();
    lit_reset();
    cap_vars_reset();
    asm_extra_bss_count = 0;

    const char *cursor_sym   = "cursor";
    const char *subj_sym     = "subject_data";
    const char *subj_len_sym = "subject_len_val";

    /* Named-pattern ret_ slots always known up front */
    for (int i = 0; i < asm_named_count; i++) {
        bss_add(asm_named[i].ret_gamma);
        bss_add(asm_named[i].ret_omega);
    }

    /* ── Collection pass: redirect output to /dev/null, advance uid counter,
     *    fire all bss_add / lit_intern / cap_var_register calls.
     *    uid sequence is identical to the real pass that follows, so every
     *    derived name (lit_str_N, dol_entry_X, root_alpha_saved, …) is
     *    registered before we emit any section headers.            ── */
    FILE *devnull = fopen("/dev/null", "w");
    FILE *real_out = asm_out;
    asm_out = devnull;

    int uid_before_dry = asm_uid_ctr;   /* save uid counter state */

    emit_asm_node(stmt->pattern,
                  "root_alpha", "root_beta",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_sym, 0);
    for (int i = 0; i < asm_named_count; i++)
        emit_asm_named_def(&asm_named[i], cursor_sym, subj_sym, subj_len_sym);

    fclose(devnull);
    asm_out = real_out;
    asm_uid_ctr = uid_before_dry;       /* reset so real pass generates same labels */

    /* ── Emit pass: all symbols now registered; emit sections in order ── */

    /* Header */
    A("; generated by sno2c -asm-body — link with snobol4_asm_harness.o\n");
    A("; assemble: nasm -f elf64 -I<runtime/asm/> body.s -o body.o\n");
    A("; link:     gcc -no-pie -o prog body.o snobol4_asm_harness.o\n");

    A("%%include \"snobol4_asm.mac\"\n");

    A("    global root_alpha, root_beta\n");
    A("    extern cursor, subject_data, subject_len_val\n");
    A("    extern match_success, match_fail\n");
    A("    extern outer_cursor\n");
    if (cap_var_count > 0)
        A("    global cap_order, cap_order_count\n");
    A("\n");

    /* .data — string literals + cap_order table */
    int has_data = lit_count > 0 || cap_var_count > 0;
    if (has_data) {
        A("\nsection .data\n");
        for (int i = 0; i < lit_count; i++) {
            if (lit_table[i].len == 0) {
                A("%-20s db 0  ; \"\"\n", lit_table[i].label);
                continue;
            }
            A("%-20s db ", lit_table[i].label);
            for (int j = 0; j < lit_table[i].len; j++) {
                if (j) A(", ");
                A("%d", (unsigned char)lit_table[i].val[j]);
            }
            A("\n");
        }
        cap_vars_emit_data_section();
    }

    /* .bss — all slots collected during dry run */
    int has_bss = bss_count > 0 || asm_extra_bss_count > 0 || cap_var_count > 0;
    if (has_bss) {
        A("\nsection .bss\n");
        for (int i = 0; i < bss_count; i++)
            A("%-24s resq 1\n", bss_slots[i]);
        extra_bss_emit();
        cap_vars_emit_bss();
    }

    /* .text — real emit; uid counter continues from where dry run left off,
     *         so all generated labels are identical to the collected names  */
    A("\nsection .text\n");

    emit_asm_node(stmt->pattern,
                  "root_alpha", "root_beta",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_sym, 0);

    for (int i = 0; i < asm_named_count; i++)
        emit_asm_named_def(&asm_named[i], cursor_sym, subj_sym, subj_len_sym);
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

int asm_body_mode = 0; /* set by -asm-body flag */

/* -----------------------------------------------------------------------
 * asm_emit_program — full-program mode (-asm flag without -asm-body)
 *
 * Walks ALL statements and emits a complete NASM program that:
 *   - calls stmt_init() at startup
 *   - translates each SNOBOL4 statement to stmt_* C-shim calls
 *   - handles pattern-match statements via inline Byrd boxes
 *   - handles labels and goto (unconditional, :S, :F)
 *   - handles computed goto via stmt_goto_dispatch()
 *
 * Generated .s links against snobol4_stmt_rt.o + the runtime .a.
 * ----------------------------------------------------------------------- */

/* ---- string-safe variable name for NASM labels ---- */
static void prog_safe(const char *src, char *dst, int dstlen) {
    int j = 0;
    for (int i = 0; src[i] && j < dstlen-1; i++) {
        char c = src[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')
            dst[j++] = c;
        else
            dst[j++] = '_';
    }
    dst[j] = '\0';
}

/* ---- data section string registry ---- */
#define MAX_PROG_STRS 2048
typedef struct { char label[64]; char *val; } ProgStr;
static ProgStr prog_strs[MAX_PROG_STRS];
static int     prog_str_count = 0;

static void prog_str_reset(void) { prog_str_count = 0; }

static const char *prog_str_intern(const char *s) {
    for (int i = 0; i < prog_str_count; i++)
        if (strcmp(prog_strs[i].val, s) == 0) return prog_strs[i].label;
    if (prog_str_count >= MAX_PROG_STRS) return "S_overflow";
    ProgStr *e = &prog_strs[prog_str_count++];
    /* M-ASM-READABLE-A: expand special chars for readability.
     * _ passes through (common word-separator — keep readable).
     * On collision (two distinct source names expand identically),
     * append _N uid to the second — rare, visible, honest. */
    char exp[128];
    asm_expand_name(s, exp, sizeof exp);
    if (exp[0]) {
        snprintf(e->label, sizeof e->label, "S_%s", exp);
        for (int i = 0; i < prog_str_count - 1; i++) {
            if (strcmp(prog_strs[i].label, e->label) == 0) {
                snprintf(e->label, sizeof e->label, "S_%s_%d", exp, prog_str_count);
                break;
            }
        }
    } else {
        snprintf(e->label, sizeof e->label, "S_%d", prog_str_count);
    }
    e->val = strdup(s);
    return e->label;
}

static void prog_str_emit_data(void) {
    for (int i = 0; i < prog_str_count; i++) {
        const char *v = prog_strs[i].val;
        int len = (int)strlen(v);
        if (len == 0) {
            /* empty string — just null terminator */
            A("%-20s db 0  ; ""\n", prog_strs[i].label);
        } else {
            A("%-20s db ", prog_strs[i].label);
            for (int j = 0; j < len; j++) {
                if (j) A(", ");
                A("%d", (unsigned char)v[j]);
            }
            A(", 0  ; \"%s\"\n", v);
        }
    }
}

/* ---- float literal registry ---- */
#define MAX_PROG_FLTS 256
typedef struct { char label[32]; double val; } ProgFlt;
static ProgFlt prog_flts[MAX_PROG_FLTS];
static int     prog_flt_count = 0;

static void prog_flt_reset(void) { prog_flt_count = 0; }

static const char *prog_flt_intern(double d) {
    uint64_t b; memcpy(&b, &d, 8);
    for (int i = 0; i < prog_flt_count; i++) {
        uint64_t a; memcpy(&a, &prog_flts[i].val, 8);
        if (a == b) return prog_flts[i].label;
    }
    if (prog_flt_count >= MAX_PROG_FLTS) return "F_overflow";
    ProgFlt *e = &prog_flts[prog_flt_count++];
    snprintf(e->label, sizeof e->label, "F_%d", prog_flt_count);
    e->val = d;
    return e->label;
}

static void prog_flt_emit_data(void) {
    for (int i = 0; i < prog_flt_count; i++) {
        uint64_t bits; memcpy(&bits, &prog_flts[i].val, 8);
        A("%-20s dq 0x%016llx  ; %g\n",
          prog_flts[i].label, (unsigned long long)bits, prog_flts[i].val);
    }
}


/* ---- expression value into DESCR_t on stack ----
 * Emits code to evaluate expr and store DESCR_t at [rbp+off].
 * Returns 1 if value might fail (needs is_fail check), 0 if always succeeds.
 * Simple cases only: E_QLIT, E_ILIT, E_VART. Complex exprs use stmt_apply. */
static int prog_emit_expr(EXPR_t *e, int rbp_off) {
    if (!e) {
        /* null expr → NULVCL (DT_SNUL=1, ptr=0) */
        A("    mov     qword [rbp%+d], 1\n", rbp_off);   /* v=DT_SNUL */
        A("    mov     qword [rbp%+d], 0\n", rbp_off+8);
        return 0;
    }
    switch (e->kind) {
    case E_QLIT: {
        const char *lab = prog_str_intern(e->sval);
        A("    LOAD_STR    %s\n", lab);
        /* LOAD_STR already writes rax/rdx → [rbp-32/24]; skip redundant mov */
        if (rbp_off == -16) {
            A("    mov     [rbp-16], rax\n");
            A("    mov     [rbp-8],  rdx\n");
        } else if (rbp_off != -32) {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 0;
    }
    case E_ILIT:
        A("    LOAD_INT    %ld\n", (long)e->ival);
        /* LOAD_INT already writes rax/rdx → [rbp-32/24]; skip redundant mov */
        if (rbp_off == -16) {
            A("    mov     [rbp-16], rax\n");
            A("    mov     [rbp-8],  rdx\n");
        } else if (rbp_off != -32) {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 0;
    case E_FLIT: {
        const char *flab = prog_flt_intern(e->dval);
        A("    LOAD_REAL   %s\n", flab);
        if (rbp_off == -16) {
            A("    mov     [rbp-16], rax\n");
            A("    mov     [rbp-8],  rdx\n");
        } else if (rbp_off != -32) {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 0;
    }
    case E_VART: {
        const char *lab = prog_str_intern(e->sval);
        if (rbp_off == -16) {
            A("    GET_VAR     %s\n", lab);
        } else {
            A("    lea     rdi, [rel %s]\n", lab);
            A("    call    stmt_get\n");
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 1; /* might be fail if var undefined */
    }
    case E_KW: {
        /* &KEYWORD — treat as named variable lookup */
        char kwbuf[128];
        snprintf(kwbuf, sizeof kwbuf, "&%s", e->sval ? e->sval : "");
        const char *lab = prog_str_intern(kwbuf);
        if (rbp_off == -16) {
            A("    GET_VAR     %s\n", lab);
        } else {
            A("    lea     rdi, [rel %s]\n", lab);
            A("    call    stmt_get\n");
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 1;
    }
    case E_FNC: {
        if (!e->sval) goto fallback;
        int na = e->nargs;
        if (na > 8) na = 8;
        const char *fnlab = prog_str_intern(e->sval);

        /* Check if this is a user-defined Snocone function */
        const AsmNamedPat *ufn = asm_named_lookup(e->sval);
        if (ufn && ufn->is_fn) {
            /* User function call-site:
             *   1. Evaluate each arg → store into fn_FNAME_arg_N_t/p .bss slots
             *   2. Store return-γ address → fn_FNAME_ret_γ
             *   3. Store return-ω address → fn_FNAME_ret_ω
             *   4. jmp fn_alpha
             *   5. return_γ_N: result value comes back in [rbp-16/rbp-8]
             *      (the function body assigns its return value to OUTPUT or
             *       a variable; we need the return value in [rbp-32/24])
             *   For now: result of user fn call is the value of the expression
             *   that was the last assignment in the function.  We implement
             *   this via a dedicated return-value .bss slot: fn_FNAME_retval_t/p.
             *   The γ/ω restore code already runs stmt_set to restore params,
             *   then jumps here — the function must have set fn_retval before
             *   jumping to [ret_γ]. */
            int call_uid = asm_uid();
            char ret_gamma_lbl[LBUF], ret_omega_lbl[LBUF];
            snprintf(ret_gamma_lbl, LBUF, "ucall%d_ret_g", call_uid);
            snprintf(ret_omega_lbl, LBUF, "ucall%d_ret_o", call_uid);

            /* Step 1: evaluate args into arg slots */
            int actual_args = (na < ufn->nparams) ? na : ufn->nparams;
            for (int ai = 0; ai < actual_args && e->args[ai]; ai++) {
                char pname_safe[128];
                asm_expand_name(ufn->param_names[ai], pname_safe, sizeof pname_safe);
                if (!pname_safe[0]) snprintf(pname_safe, sizeof pname_safe, "p%d", ai);
                char arg_slot_t[260], arg_slot_p[260];
                snprintf(arg_slot_t, sizeof arg_slot_t, "fn_%s_arg_%d_t", ufn->safe, ai);
                snprintf(arg_slot_p, sizeof arg_slot_p, "fn_%s_arg_%d_p", ufn->safe, ai);
                /* Evaluate arg into [rbp-32/24] */
                prog_emit_expr(e->args[ai], -32);
                A("    mov     rax, [rbp-32]\n");
                A("    mov     rcx, [rbp-24]\n");
                A("    mov     [%s], rax\n", arg_slot_t);
                A("    mov     [%s], rcx\n", arg_slot_p);
            }

            /* Step 2: store return addresses */
            A("    lea     rax, [rel %s]\n", ret_gamma_lbl);
            A("    mov     [%s], rax\n", ufn->ret_gamma);
            A("    lea     rax, [rel %s]\n", ret_omega_lbl);
            A("    mov     [%s], rax\n", ufn->ret_omega);

            /* Step 3: call — jump to function alpha */
            A("    jmp     %s\n", ufn->alpha_lbl);

            /* Step 4: return landing — gamma means success.
             * Return value convention (sc_cf.c do_return_stmt):
             *   `return expr` emits `fname = expr` then `RETURN`.
             * So after γ return, the result is in the SNOBOL4 variable
             * named after the function (e->sval). Just GET_VAR it. */
            A("%s:\n", ret_gamma_lbl);
            A("    GET_VAR     %s\n", fnlab);
            A("    mov     [rbp-32], rax\n");
            A("    mov     [rbp-24], rdx\n");
            /* omega: function failed (FRETURN) */
            A("    jmp     ucall%d_done\n", call_uid);
            A("%s:\n", ret_omega_lbl);
            A("    LOAD_NULVCL32\n");  /* fail value */
            A("ucall%d_done:\n", call_uid);
            return 1;
        }
        /* Fast path: 1-arg call with simple literal arg → CALL1_INT / CALL1_STR */
        if (na == 1 && e->args[0] && rbp_off == -32) {
            EXPR_t *arg0 = e->args[0];
            if (arg0->kind == E_ILIT) {
                A("    CALL1_INT   %s, %ld\n", fnlab, (long)arg0->ival);
                return 1;
            }
            if (arg0->kind == E_QLIT) {
                const char *alab = prog_str_intern(arg0->sval);
                A("    CALL1_STR   %s, %s\n", fnlab, alab);
                return 1;
            }
            if (arg0->kind == E_VART) {
                const char *alab = prog_str_intern(arg0->sval);
                A("    CALL1_VAR   %s, %s\n", fnlab, alab);
                return 1;
            }
        }
        /* Fast path: 2-arg call with atom args → CONC2_* macros (work for any fn) */
        if (na == 2 && e->args[0] &&
            (rbp_off == -32 || rbp_off == -16)) {
            EXPR_t *a0 = e->args[0];
            EXPR_t *a1 = e->args[1];  /* may be NULL → treated as E_NULV */
            int a0s = (a0->kind == E_QLIT);
            int a0v = (a0->kind == E_VART);
            int a1s = (a1 && a1->kind == E_QLIT);
            int a1v = (a1 && a1->kind == E_VART);
            int a1n = (!a1 || a1->kind == E_NULV ||
                      (a1->kind == E_ILIT && a1->ival == 0));
            int a1i = (a1 && a1->kind == E_ILIT);
            int r16  = (rbp_off == -16);
            const char *m_ss = r16 ? "CONC2_16  " : "CONC2   ";
            const char *m_sn = r16 ? "CONC2_N16 " : "CONC2_N ";
            const char *m_sv = r16 ? "CONC2_SV16" : "CONC2_SV";
            const char *m_vs = r16 ? "CONC2_VS16" : "CONC2_VS";
            const char *m_vn = r16 ? "CONC2_VN16" : "CONC2_VN";
            const char *m_vv = r16 ? "CONC2_VV16" : "CONC2_VV";
            if (a0s && a1n) {
                A("    %s %s, %s\n", m_sn, fnlab, prog_str_intern(a0->sval));
                return 1;
            }
            if (a0s && a1s) {
                A("    %s %s, %s, %s\n", m_ss, fnlab,
                  prog_str_intern(a0->sval), prog_str_intern(a1->sval));
                return 1;
            }
            if (a0s && a1v) {
                A("    %s %s, %s, %s\n", m_sv, fnlab,
                  prog_str_intern(a0->sval), prog_str_intern(a1->sval));
                return 1;
            }
            if (a0v && a1s) {
                A("    %s %s, %s, %s\n", m_vs, fnlab,
                  prog_str_intern(a0->sval), prog_str_intern(a1->sval));
                return 1;
            }
            if (a0v && a1n) {
                A("    %s %s, %s\n", m_vn, fnlab, prog_str_intern(a0->sval));
                return 1;
            }
            int a0n = (a0->kind == E_NULV);
            if (a0n && a1n && !r16) {
                A("    CONC2_NN %s\n", fnlab);
                return 1;
            }
            if (a0v && a1v) {
                A("    %s %s, %s, %s\n", m_vv, fnlab,
                  prog_str_intern(a0->sval), prog_str_intern(a1->sval));
                return 1;
            }
            /* Integer-literal fast paths (rbp_off==-32 only) */
            int a0i = (a0->kind == E_ILIT);
            int a0nv = (a0->kind == E_NULV);
            if (!r16) {
                if (a0v && a1i) {
                    A("    CONC2_VI %s, %s, %ld\n", fnlab,
                      prog_str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1v) {
                    A("    CONC2_IV %s, %ld, %s\n", fnlab,
                      (long)a0->ival, prog_str_intern(a1->sval));
                    return 1;
                }
                if (a0i && a1i) {
                    A("    CONC2_II %s, %ld, %ld\n", fnlab,
                      (long)a0->ival, (long)a1->ival);
                    return 1;
                }
                if (a0nv && a1i) {
                    A("    CONC2_NI %s, %ld\n", fnlab, (long)a1->ival);
                    return 1;
                }
                if (a0s && a1i) {
                    A("    CONC2_SI %s, %s, %ld\n", fnlab,
                      prog_str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1s) {
                    A("    CONC2_IS %s, %ld, %s\n", fnlab,
                      (long)a0->ival, prog_str_intern(a1->sval));
                    return 1;
                }
            } else {
                /* rbp_off == -16 integer fast paths */
                if (a0v && a1i) {
                    A("    CONC2_VI16 %s, %s, %ld\n", fnlab,
                      prog_str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1v) {
                    A("    CONC2_IV16 %s, %ld, %s\n", fnlab,
                      (long)a0->ival, prog_str_intern(a1->sval));
                    return 1;
                }
                if (a0i && a1i) {
                    A("    CONC2_II16 %s, %ld, %ld\n", fnlab,
                      (long)a0->ival, (long)a1->ival);
                    return 1;
                }
                if (a0nv && a1i) {
                    A("    CONC2_NI16 %s, %ld\n", fnlab, (long)a1->ival);
                    return 1;
                }
                if (a0s && a1i) {
                    A("    CONC2_SI16 %s, %s, %ld\n", fnlab,
                      prog_str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1s) {
                    A("    CONC2_IS16 %s, %ld, %s\n", fnlab,
                      (long)a0->ival, prog_str_intern(a1->sval));
                    return 1;
                }
            }
        }
        if (na == 0) {
            A("    APPLY_FN_0  %s\n", fnlab);
        } else {
            int arr_bytes = na * 16;
            A("    sub     rsp, %d\n", arr_bytes);
            for (int ai = 0; ai < na && e->args[ai]; ai++) {
                prog_emit_expr(e->args[ai], -(rbp_off < 0 ? -rbp_off : 32) - 0);
                A("    STORE_ARG32 %d\n", ai * 16);
            }
            A("    APPLY_FN_N  %s, %d\n", fnlab, na);
            A("    add     rsp, %d\n", arr_bytes);
        }
        /* Result in rax/rdx → store to target slot */
        if (rbp_off == -16) {
            A("    STORE_RESULT16\n");
        } else if (rbp_off == -32) {
            A("    STORE_RESULT\n");
        } else {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 1;
    }
    case E_OR:
    case E_CONC: {
        /* E_CONC = string CAT  (value context): call stmt_concat directly.
         * E_OR   = pattern SEQ (alternation):   call stmt_apply("ALT").
         *
         * Naming: CAT for string concatenation, SEQ/ALT for pattern alternation.
         * E_CONC in prog_emit_expr is always value-context — pattern-context
         * E_CONC is handled by emit_asm_node, not here. */

        int left_is_str  = e->left  && e->left->kind  == E_QLIT;
        int left_is_var  = e->left  && e->left->kind  == E_VART;
        int right_is_nul = !e->right ||
                            e->right->kind == E_NULV ||
                           (e->right->kind == E_ILIT && e->right->ival == 0);
        int right_is_str = e->right && e->right->kind == E_QLIT;
        int right_is_var = e->right && e->right->kind == E_VART;

        if (e->kind == E_CONC) {
            /* CAT fast paths — CAT2_* macros call stmt_concat(a,b) */
            if (rbp_off == -32) {
                if (left_is_str && right_is_str) {
                    A("    CAT2_SS  %s, %s\n",
                      prog_str_intern(e->left->sval),
                      prog_str_intern(e->right->sval));
                    return 1;
                }
                if (left_is_str && right_is_nul) {
                    A("    CAT2_SN  %s\n", prog_str_intern(e->left->sval));
                    return 1;
                }
                if (left_is_str && right_is_var) {
                    A("    CAT2_SV  %s, %s\n",
                      prog_str_intern(e->left->sval),
                      prog_str_intern(e->right->sval));
                    return 1;
                }
                if (left_is_var && right_is_str) {
                    A("    CAT2_VS  %s, %s\n",
                      prog_str_intern(e->left->sval),
                      prog_str_intern(e->right->sval));
                    return 1;
                }
                if (left_is_var && right_is_nul) {
                    A("    CAT2_VN  %s\n", prog_str_intern(e->left->sval));
                    return 1;
                }
                if (left_is_var && right_is_var) {
                    A("    CAT2_VV  %s, %s\n",
                      prog_str_intern(e->left->sval),
                      prog_str_intern(e->right->sval));
                    return 1;
                }
            }
            /* CAT generic fallback: evaluate both, call stmt_concat */
            prog_emit_expr(e->left, -32);
            A("    mov     [conc_tmp0_rax], rax\n");
            A("    mov     [conc_tmp0_rdx], rdx\n");
            prog_emit_expr(e->right, -32);
            A("    mov     rcx, rdx\n");
            A("    mov     rdx, rax\n");
            A("    mov     rdi, [conc_tmp0_rax]\n");
            A("    mov     rsi, [conc_tmp0_rdx]\n");
            A("    call    stmt_concat\n");
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off + 8);
            return 1;
        }

        /* E_OR — pattern ALT via stmt_apply("ALT") — unchanged */
        const char *opname = "ALT";
        const char *mac_ss = "ALT2    ";
        const char *mac_sn = "ALT2_N  ";
        const char *mac_sv = "ALT2_SV ";
        const char *mac_vs = "ALT2_VS ";
        const char *mac_vn = "ALT2_VN ";
        const char *mac_vv = "ALT2_VV ";
        const char *mac_ss16 = "ALT2_16   ";
        const char *mac_sn16 = "ALT2_N16  ";
        const char *mac_sv16 = "ALT2_SV16 ";
        const char *mac_vs16 = "ALT2_VS16 ";
        const char *mac_vn16 = "ALT2_VN16 ";
        const char *mac_vv16 = "ALT2_VV16 ";
        const char *fnlab  = prog_str_intern(opname);

        if (left_is_str && right_is_nul && rbp_off == -32) {
            const char *slab = prog_str_intern(e->left->sval);
            A("    %s%s, %s\n", mac_sn, fnlab, slab);
            return 1;
        }
        if (left_is_str && right_is_str && rbp_off == -32) {
            const char *s1 = prog_str_intern(e->left->sval);
            const char *s2 = prog_str_intern(e->right->sval);
            A("    %s%s, %s, %s\n", mac_ss, fnlab, s1, s2);
            return 1;
        }
        if (left_is_str && right_is_var && rbp_off == -32) {
            const char *slab = prog_str_intern(e->left->sval);
            const char *vlab = prog_str_intern(e->right->sval);
            A("    %s %s, %s, %s\n", mac_sv, fnlab, slab, vlab);
            return 1;
        }
        if (left_is_var && right_is_str && rbp_off == -32) {
            const char *vlab = prog_str_intern(e->left->sval);
            const char *slab = prog_str_intern(e->right->sval);
            A("    %s %s, %s, %s\n", mac_vs, fnlab, vlab, slab);
            return 1;
        }
        if (left_is_var && right_is_nul && rbp_off == -32) {
            const char *vlab = prog_str_intern(e->left->sval);
            A("    %s %s, %s\n", mac_vn, fnlab, vlab);
            return 1;
        }
        if (left_is_var && right_is_var && rbp_off == -32) {
            const char *v1 = prog_str_intern(e->left->sval);
            const char *v2 = prog_str_intern(e->right->sval);
            A("    %s %s, %s, %s\n", mac_vv, fnlab, v1, v2);
            return 1;
        }
        if (left_is_str && right_is_nul && rbp_off == -16) {
            const char *slab = prog_str_intern(e->left->sval);
            A("    %s%s, %s\n", mac_sn16, fnlab, slab);
            return 1;
        }
        if (left_is_str && right_is_str && rbp_off == -16) {
            const char *s1 = prog_str_intern(e->left->sval);
            const char *s2 = prog_str_intern(e->right->sval);
            A("    %s%s, %s, %s\n", mac_ss16, fnlab, s1, s2);
            return 1;
        }
        if (left_is_str && right_is_var && rbp_off == -16) {
            const char *slab = prog_str_intern(e->left->sval);
            const char *vlab = prog_str_intern(e->right->sval);
            A("    %s %s, %s, %s\n", mac_sv16, fnlab, slab, vlab);
            return 1;
        }
        if (left_is_var && right_is_str && rbp_off == -16) {
            const char *vlab = prog_str_intern(e->left->sval);
            const char *slab = prog_str_intern(e->right->sval);
            A("    %s %s, %s, %s\n", mac_vs16, fnlab, vlab, slab);
            return 1;
        }
        if (left_is_var && right_is_nul && rbp_off == -16) {
            const char *vlab = prog_str_intern(e->left->sval);
            A("    %s %s, %s\n", mac_vn16, fnlab, vlab);
            return 1;
        }
        if (left_is_var && right_is_var && rbp_off == -16) {
            const char *v1 = prog_str_intern(e->left->sval);
            const char *v2 = prog_str_intern(e->right->sval);
            A("    %s %s, %s, %s\n", mac_vv16, fnlab, v1, v2);
            return 1;
        }

        /* Generic ALT fallback */
        A("    sub     rsp, 32\n");
        prog_emit_expr(e->left,  rbp_off);
        A("    STORE_ARG32 0\n");
        prog_emit_expr(e->right, rbp_off);
        A("    STORE_ARG32 16\n");
        A("    APPLY_FN_N  %s, 2\n", fnlab);
        A("    add     rsp, 32\n");
        A("    mov     [rbp%+d], rax\n", rbp_off);
        A("    mov     [rbp%+d], rdx\n", rbp_off + 8);
        return 1;
    }
    /* ---- arithmetic binary operators ---- */
    case E_ADD:
    case E_SUB:
    case E_MPY:
    case E_DIV:
    case E_EXPOP: {
        const char *fn = (e->kind == E_ADD)   ? "add"       :
                         (e->kind == E_SUB)   ? "sub"       :
                         (e->kind == E_MPY)   ? "mul"       :
                         (e->kind == E_DIV)   ? "DIVIDE_fn" :
                                                "POWER_fn";
        const char *fnlab = prog_str_intern(fn);
        EXPR_t *l = e->left, *r = e->right;
        int ls = (l && l->kind == E_QLIT);
        int lv = (l && l->kind == E_VART);
        int li = (l && l->kind == E_ILIT);
        int rs = (r && r->kind == E_QLIT);
        int rv = (r && r->kind == E_VART);
        int ri = (r && r->kind == E_ILIT);
        int rn = (!r || r->kind == E_NULV);
        /* Fast paths when both args are atoms (same macros used for CONC2/ALT2) */
        if (rbp_off == -32) {
            if (lv && rv) {
                A("    CONC2_VV %s, %s, %s\n", fnlab,
                  prog_str_intern(l->sval), prog_str_intern(r->sval));
                return 1;
            }
            if (lv && ri) {
                A("    CONC2_VI %s, %s, %ld\n", fnlab,
                  prog_str_intern(l->sval), (long)r->ival);
                return 1;
            }
            if (li && rv) {
                A("    CONC2_IV %s, %ld, %s\n", fnlab,
                  (long)l->ival, prog_str_intern(r->sval));
                return 1;
            }
            if (li && ri) {
                A("    CONC2_II %s, %ld, %ld\n", fnlab,
                  (long)l->ival, (long)r->ival);
                return 1;
            }
            if (lv && rs) {
                A("    CONC2_VS %s, %s, %s\n", fnlab,
                  prog_str_intern(l->sval), prog_str_intern(r->sval));
                return 1;
            }
            if (ls && rv) {
                A("    CONC2_SV %s, %s, %s\n", fnlab,
                  prog_str_intern(l->sval), prog_str_intern(r->sval));
                return 1;
            }
            if (lv && rn) {
                A("    CONC2_VN %s, %s\n", fnlab, prog_str_intern(l->sval));
                return 1;
            }
        }
        /* Generic 2-arg path: evaluate each child, call via stmt_apply */
        A("    sub     rsp, 32\n");
        prog_emit_expr(l, -32);
        A("    STORE_ARG32 0\n");
        prog_emit_expr(r, -32);
        A("    STORE_ARG32 16\n");
        A("    APPLY_FN_N  %s, 2\n", fnlab);
        A("    add     rsp, 32\n");
        if (rbp_off == -32 || rbp_off == -16) {
            A("    STORE_RESULT\n");
        } else {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 1;
    }
    /* ---- unary minus ---- */
    case E_MNS: {
        const char *fnlab = prog_str_intern("neg");
        EXPR_t *operand = e->left;   /* unop() puts operand in left */
        if (!operand) goto fallback;
        if (rbp_off == -32) {
            if (operand->kind == E_ILIT) {
                A("    CALL1_INT   %s, %ld\n", fnlab, (long)operand->ival);
                return 1;
            }
            if (operand->kind == E_VART) {
                A("    CALL1_VAR   %s, %s\n", fnlab,
                  prog_str_intern(operand->sval));
                return 1;
            }
            if (operand->kind == E_QLIT) {
                A("    CALL1_STR   %s, %s\n", fnlab,
                  prog_str_intern(operand->sval));
                return 1;
            }
        }
        /* Generic 1-arg path */
        A("    sub     rsp, 16\n");
        prog_emit_expr(operand, -32);
        A("    STORE_ARG32 0\n");
        A("    APPLY_FN_N  %s, 1\n", fnlab);
        A("    add     rsp, 16\n");
        if (rbp_off == -32 || rbp_off == -16) {
            A("    STORE_RESULT\n");
        } else {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 1;
    }

    fallback:
    default:
        /* Fallback: emit NULVCL — complex exprs not yet supported */
        if (rbp_off == -16) {
            A("    LOAD_NULVCL\n");
        } else {
            A("    mov     qword [rbp%+d], 1\n", rbp_off);
            A("    mov     qword [rbp%+d], 0\n", rbp_off+8);
        }
        return 0;
    }
}

/* ---- emit goto target (resolves label to NASM label) ---- */
static const char *prog_label_nasm(const char *lbl); /* forward */

/* Returns 1 if target is a SNOBOL4 special goto (RETURN/FRETURN/END etc.)
 * that should map to _SNO_END rather than a user label. */
static int is_special_goto(const char *t) {
    if (!t || !*t) return 0;
    return (strcasecmp(t,"RETURN")==0  || strcasecmp(t,"FRETURN")==0 ||
            strcasecmp(t,"NRETURN")==0 || strcasecmp(t,"SRETURN")==0 ||
            strcasecmp(t,"END")==0);
}

/* Emit a jmp to a goto target, handling special targets. */
/* current_fn — set to the AsmNamedPat* of the user function being emitted.
 * NULL when outside any user function body.
 * Used by emit_jmp / prog_emit_goto to route RETURN → [fn_ret_γ]. */
static const AsmNamedPat *current_fn = NULL;

static void emit_jmp(const char *tgt, const char *fallthrough) {
    if (!tgt || !*tgt) {
        if (fallthrough) A("    jmp     %s\n", fallthrough);
        return;
    }
    /* If inside a user function, RETURN/FRETURN/NRETURN → indirect jmp via ret_γ */
    if (current_fn && (strcasecmp(tgt,"RETURN")==0 ||
                       strcasecmp(tgt,"FRETURN")==0 ||
                       strcasecmp(tgt,"NRETURN")==0)) {
        A("    jmp     [%s]     ; %s\n", current_fn->ret_gamma, tgt);
        return;
    }
    if (is_special_goto(tgt)) { A("    GOTO_ALWAYS  L_SNO_END     ; %s\n", tgt); return; }
    A("    jmp     %s\n", prog_label_nasm(tgt));
}

static void prog_emit_goto(const char *target, const char *fallthrough) {
    if (!target || strcmp(target, "") == 0) {
        if (fallthrough) A("    jmp     %s\n", fallthrough);
        return;
    }
    /* Special SNOBOL4 goto targets */
    if (strcasecmp(target,"END")==0) {
        A("    GOTO_ALWAYS  L_SNO_END     ; %s\n", target);
        return;
    }
    if (strcasecmp(target,"RETURN")==0 || strcasecmp(target,"FRETURN")==0 ||
        strcasecmp(target,"NRETURN")==0 || strcasecmp(target,"SRETURN")==0) {
        if (current_fn) {
            A("    jmp     [%s]     ; %s\n", current_fn->ret_gamma, target);
        } else {
            A("    GOTO_ALWAYS  L_SNO_END     ; %s\n", target);
        }
        return;
    }
    A("    jmp     %s\n", prog_label_nasm(target));
}
/* ---- scan all labels in program for the jump table ---- */
#define MAX_PROG_LABELS 1024
/* Numeric label registry: each unique SNOBOL4 label name gets an integer ID.
 * Emitted as _L_<base>_N in NASM — number guarantees uniqueness, base aids readability.
 * TODO M-ASM-READABLE: named expansion (pp_>= → pp_GT_EQ_N) post M-ASM-BEAUTY. */
static char *prog_labels[MAX_PROG_LABELS];
static int   prog_label_defined[MAX_PROG_LABELS]; /* 1 if _L_X: was emitted */
static int   prog_label_count = 0;
static void prog_labels_reset(void) {
    prog_label_count = 0;
    memset(prog_label_defined, 0, sizeof prog_label_defined);
}

static int prog_label_id(const char *lbl) {
    if (!lbl || !*lbl) return -1;
    for (int i = 0; i < prog_label_count; i++)
        if (strcmp(prog_labels[i], lbl) == 0) return i;
    if (prog_label_count >= MAX_PROG_LABELS) return -1;
    prog_labels[prog_label_count++] = strdup(lbl);
    return prog_label_count - 1;
}

/* Returns a collision-free NASM label: _L_<clean_base>_<N>
 * Base: keep only alphanumerics, collapse runs of non-alnum to single underscore,
 * strip leading/trailing underscores.  Number N alone guarantees uniqueness —
 * the base is pure decoration for readability.
 * TODO M-ASM-READABLE: post M-ASM-BEAUTY sprint to improve base quality. */
static const char *prog_label_nasm(const char *lbl) {
    static char buf[256];
    int id = prog_label_id(lbl);
    if (id < 0) { snprintf(buf, sizeof buf, "L_unk_%d", id); return buf; }
    /* Build clean base: alnum only, runs of others → single _ */
    char base[128]; int bi = 0; int in_sep = 0;
    for (const char *p = lbl; *p && bi < 120; p++) {
        if (isalnum((unsigned char)*p)) {
            base[bi++] = *p; in_sep = 0;
        } else if (!in_sep && bi > 0) {
            base[bi++] = '_'; in_sep = 1;
        }
    }
    /* strip trailing underscore */
    while (bi > 0 && base[bi-1] == '_') bi--;
    base[bi] = '\0';
    if (bi == 0) snprintf(base, sizeof base, "L");
    snprintf(buf, sizeof buf, "L_%s_%d", base, id);
    return buf;
}

static void prog_label_register(const char *lbl) {
    prog_label_id(lbl); /* ensure registered; discard return value */
}

static void asm_emit_program(Program *prog) {
    if (!prog || !prog->head) { asm_emit_null_program(); return; }

    current_fn = NULL;

    prog_str_reset();
    prog_flt_reset();
    prog_labels_reset();
    bss_reset();
    lit_reset();
    asm_extra_bss_count = 0;

    /* Pass 1: collect all labels + string literals (recursive expr walk) */
    /* Recursive helper: intern all string/var names reachable from an expr */
    #define WALK_EXPR(root) do {         EXPR_t *_stk[256]; int _top = 0;         if (root) _stk[_top++] = (root);         while (_top > 0) {             EXPR_t *_e = _stk[--_top];             if (!_e) continue;             if (_e->kind == E_QLIT && _e->sval) prog_str_intern(_e->sval);             if (_e->kind == E_VART && _e->sval) prog_str_intern(_e->sval);             if (_e->kind == E_KW   && _e->sval) prog_str_intern(_e->sval);             if (_e->kind == E_FNC  && _e->sval) prog_str_intern(_e->sval);             if (_e->left  && _top < 255) _stk[_top++] = _e->left;             if (_e->right && _top < 255) _stk[_top++] = _e->right;             for (int _i = 0; _i < _e->nargs && _top < 254; _i++)                 if (_e->args[_i]) _stk[_top++] = _e->args[_i];         }     } while(0)

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->label) { prog_label_register(s->label); prog_str_intern(s->label); }
        if (s->go) {
            /* Special SNOBOL4 goto targets — handled at emit time, not as labels */
            const char *special[] = {"RETURN","FRETURN","NRETURN","SRETURN","END",NULL};
            if (s->go->onsuccess) {
                int sp = 0;
                for (int i = 0; special[i]; i++)
                    if (strcasecmp(s->go->onsuccess, special[i]) == 0) { sp=1; break; }
                if (!sp) prog_label_register(s->go->onsuccess);
            }
            if (s->go->onfailure) {
                int sp = 0;
                for (int i = 0; special[i]; i++)
                    if (strcasecmp(s->go->onfailure, special[i]) == 0) { sp=1; break; }
                if (!sp) prog_label_register(s->go->onfailure);
            }
            if (s->go->uncond) {
                int sp = 0;
                for (int i = 0; special[i]; i++)
                    if (strcasecmp(s->go->uncond, special[i]) == 0) { sp=1; break; }
                if (!sp) prog_label_register(s->go->uncond);
            }
        }
        WALK_EXPR(s->subject);
        WALK_EXPR(s->replacement);
        WALK_EXPR(s->pattern);
    }
    #undef WALK_EXPR

    /* Pass 2: collect .bss slots for pattern stmts (named-pat ret_ pointers).
     * In program mode, skip entries whose replacement is a plain value
     * (E_QLIT/E_ILIT/E_VART) — those are variable assignments, not patterns. */
    bss_add("cursor");
    bss_add("subject_len_val");
    /* subject_data is a byte array — emitted separately in .bss */

    for (int i = 0; i < asm_named_count; i++) {
        if (asm_named[i].is_fn) {
            /* User function: ret_γ/ret_ω return-address slots */
            bss_add(asm_named[i].ret_gamma);
            bss_add(asm_named[i].ret_omega);
            /* Per-param save slots (old value backup) and arg slots (call-site input) */
            for (int pi = 0; pi < asm_named[i].nparams; pi++) {
                char pname_safe[128];
                asm_expand_name(asm_named[i].param_names[pi], pname_safe, sizeof pname_safe);
                if (!pname_safe[0]) snprintf(pname_safe, sizeof pname_safe, "p%d", pi);
                char save_slot[256], arg_slot[256];
                snprintf(save_slot, sizeof save_slot, "fn_%s_save_%s",
                         asm_named[i].safe, pname_safe);
                snprintf(arg_slot,  sizeof arg_slot,  "fn_%s_arg_%d",
                         asm_named[i].safe, pi);
                /* save_slot holds a DESCR_t (2 qwords): _t = type, _p = ptr */
                char save_slot_t[260], save_slot_p[260];
                snprintf(save_slot_t, sizeof save_slot_t, "%s_t", save_slot);
                snprintf(save_slot_p, sizeof save_slot_p, "%s_p", save_slot);
                bss_add(save_slot_t);
                bss_add(save_slot_p);
                /* arg_slot holds a DESCR_t (2 qwords): _t = type, _p = ptr */
                char arg_slot_t[260], arg_slot_p[260];
                snprintf(arg_slot_t, sizeof arg_slot_t, "%s_t", arg_slot);
                snprintf(arg_slot_p, sizeof arg_slot_p, "%s_p", arg_slot);
                bss_add(arg_slot_t);
                bss_add(arg_slot_p);
            }
            continue;
        }
        if (asm_named[i].pat) {
            EKind rk = asm_named[i].pat->kind;
            if (rk == E_QLIT || rk == E_ILIT || rk == E_FLIT || rk == E_NULV ||
                rk == E_VART || rk == E_KW)
                continue; /* plain value assignment — not a real named pattern */
        }
        bss_add(asm_named[i].ret_gamma);
        bss_add(asm_named[i].ret_omega);
    }

    /* Register scan_start_N bss slots for every pattern-match statement.
     * uid sequence mirrors the real emit pass (one uid per statement). */
    {
        int p2_uid = 0;
        for (STMT_t *sp = prog->head; sp; sp = sp->next) {
            if (sp->is_end) break;
            int this_uid = p2_uid++;
            if (!sp->pattern) continue;
            char ss[64]; snprintf(ss, sizeof ss, "scan_start_%d", this_uid);
            bss_add(ss);
        }
    }

    /* ---- Pass 3: dry-run all pattern emissions to collect bss/lit slots ----
     * Mirrors asm_emit_body dry-run. Redirects output to /dev/null,
     * runs emit_asm_node for every pattern stmt, then restores.
     * This ensures span_saved/lit_str slots are registered before .bss/.data. */
    {
        FILE *devnull = fopen("/dev/null", "w");
        FILE *real_out_p3 = asm_out;
        asm_out = devnull;
        int uid_save = asm_uid_ctr;
        /* Walk statements using the same uid sequence as the real pass */
        int dry_stmt_uid = 0;
        for (STMT_t *sp = prog->head; sp; sp = sp->next) {
            if (sp->is_end) break;
            int dry_uid = dry_stmt_uid++;
            if (!sp->pattern) continue;
            /* Use real label names so bss_add registers the correct slot names */
            char dry_alpha[64]; snprintf(dry_alpha, sizeof dry_alpha, "P_%d_α", dry_uid);
            char dry_beta[64];  snprintf(dry_beta,  sizeof dry_beta,  "P_%d_β", dry_uid);
            char dry_gamma[64]; snprintf(dry_gamma, sizeof dry_gamma, "P_%d_γ", dry_uid);
            char dry_omega[64]; snprintf(dry_omega, sizeof dry_omega, "P_%d_ω", dry_uid);
            emit_asm_node(sp->pattern,
                          dry_alpha, dry_beta, dry_gamma, dry_omega,
                          "cursor", "subject_data", "subject_len_val", 0);
        }
        /* also dry-run named pattern bodies */
        for (int i = 0; i < asm_named_count; i++) {
            EKind rk = asm_named[i].pat ? asm_named[i].pat->kind : E_NULV;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
            emit_asm_named_def(&asm_named[i], "cursor","subject_data","subject_len_val");
        }
        fclose(devnull);
        asm_out = real_out_p3;
        asm_uid_ctr = uid_save; /* reset so real pass generates same labels */
    }

    /* ---- emit header ---- */
    A("; generated by sno2c -asm (program mode)\n");
    A("; link: gcc -no-pie out.s stmt_rt.o snobol4.o mock_includes.o\n");
    A(";             snobol4_pattern.o mock_engine.o -lgc -lm -o prog\n");
    A("%%include \"snobol4_asm.mac\"\n");
    A("    global  main\n");
    A("    extern  stmt_init, stmt_strval, stmt_intval\n");
    A("    extern  stmt_realval, stmt_set_null, stmt_set_indirect\n");
    A("    extern  stmt_get, stmt_set, stmt_output, stmt_input\n");
    A("    extern  stmt_concat, stmt_is_fail, stmt_finish\n");
    A("    extern  stmt_realval, stmt_set_null, stmt_set_indirect\n");
    A("    extern  stmt_apply, stmt_goto_dispatch\n");
    A("    extern  stmt_setup_subject, stmt_apply_replacement\n");
    A("    extern  stmt_apply_replacement_splice\n");
    A("    extern  stmt_set_capture, stmt_match_var\n");
    A("    extern  stmt_pos_var, stmt_rpos_var\n");
    A("    extern  stmt_span_var, stmt_break_var\n");
    A("    extern  stmt_breakx_var, stmt_breakx_lit\n");
    A("    extern  stmt_any_var, stmt_notany_var\n");
    A("    extern  stmt_at_capture\n");
    A("    extern  kw_anchor\n");
    A("    global  cursor, subject_data, subject_len_val\n");
    A("\n");
    /* subject_data/subject_len_val/cursor: defined here, exported for stmt_rt.c */


    /* .data emitted at end of file (after .text) so late prog_str_intern()
     * calls from emit time are captured. See prog_str_emit_data() below. */

    /* ---- .note.GNU-stack ---- */
    flush_pending_label();
    A("section .note.GNU-stack noalloc noexec nowrite progbits\n");

    /* ---- .bss ---- */
    /* Always emit .bss — subject_data is always needed for pattern stmts */
    flush_pending_label();
    A("section .bss\n");
    for (int i = 0; i < bss_count; i++)
        A("%-24s resq 1\n", bss_slots[i]);
    /* Byrd box scratch slots (saved_cursor, arbno stack, etc.) */
    extra_bss_emit();
    /* DOL $ capture buffers (cap_VAR_buf/cap_VAR_len) */
    cap_vars_emit_bss();
    /* Result-temp scratch pair for complex-child CONC2/ALT2 generic path */
    A("%-24s resq 1\n", "conc_tmp0_rax");
    A("%-24s resq 1\n", "conc_tmp0_rdx");
    /* subject buffer for pattern matching */
    A("%-24s resb 65536\n", "subject_data");
    A("\n");

    /* ---- .text ---- */
    flush_pending_label();
    flush_pending_sep();
    emit_sep_major("PROGRAM BODY");
    A("section .text\n");
    A("main:\n");
    A("    PROG_INIT\n");

    /* Walk statements */
    int stmt_uid = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) {
            /* END statement — fall through to L_SNO_END at bottom */
            A("    GOTO_ALWAYS  L_SNO_END\n");
            break;
        }

        int uid = stmt_uid++;
        char next_lbl[64]; snprintf(next_lbl, sizeof next_lbl, "Ln_%d", uid);
        char sfail_lbl[64]; snprintf(sfail_lbl, sizeof sfail_lbl, "Lf_%d", uid);

        /* Major separator: SNOBOL4 statement boundary.
         * Emit "; === label ===..." with the SNOBOL4 source label embedded
         * when present, or plain "; ======..." for unlabelled statements.
         * Then queue the generated label to fold onto the first instruction. */
        emit_sep_major(s->label ? s->label : NULL);
        if (s->label) {
            asmL(prog_label_nasm(s->label));
            { int _id = prog_label_id(s->label);
              if (_id >= 0) prog_label_defined[_id] = 1; }

            /* current_fn tracking: entering a user function body */
            const AsmNamedPat *fn_entry = asm_named_lookup(s->label);
            if (fn_entry && fn_entry->is_fn) {
                current_fn = fn_entry;
            }
            /* Leaving a user function body: label ends with ".END" */
            if (current_fn) {
                char end_lbl[256];
                snprintf(end_lbl, sizeof end_lbl, "%s.END", current_fn->varname);
                if (strcasecmp(s->label, end_lbl) == 0)
                    current_fn = NULL;
            }
        }

        /* Determine S/F/uncond targets */
        const char *tgt_s = s->go ? s->go->onsuccess : NULL;
        const char *tgt_f = s->go ? s->go->onfailure : NULL;
        const char *tgt_u = s->go ? s->go->uncond    : NULL;

        int id_s = tgt_s ? prog_label_id(tgt_s) : -1;
        int id_f = tgt_f ? prog_label_id(tgt_f) : -1;
        int id_u = tgt_u ? prog_label_id(tgt_u) : -1;

        /* Case 1: pattern-free assignment or expression */
        if (!s->pattern) {
            /* Skip DEFINE stmts — user function registration is compile-time only.
             * The α entry and body are emitted in the NAMED PATTERN BODIES section. */
            if (s->subject && s->subject->kind == E_FNC &&
                s->subject->sval && strcasecmp(s->subject->sval, "DEFINE") == 0) {
                asmL(next_lbl);
                continue;
            }
            /* Evaluate subject (the LHS or expression).
             * Skip for indirect-assignment ($X=val): has_eq + E_INDR/E_DOL subject.
             * Also skip for plain assignment (has_eq + VART/KW): no need to load LHS.
             * The indirect handler below evaluates the inner name expression itself. */
            if (s->subject &&
                !(s->has_eq && (s->subject->kind == E_INDR || s->subject->kind == E_DOL)) &&
                !(s->has_eq && (s->subject->kind == E_VART || s->subject->kind == E_KW))) {
                int may_fail = prog_emit_expr(s->subject, -32);
                /* If subject may fail AND there are S/F targets, dispatch */
                if (may_fail && !s->has_eq && (id_s >= 0 || id_f >= 0)) {
                    A("    FAIL_BR     %s\n", id_f >= 0 ? sfail_lbl : next_lbl);
                    /* success path */
                    emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                    /* failure path */
                    if (id_f >= 0) {
                        asmL(sfail_lbl);
                        emit_jmp(tgt_f, next_lbl);
                    }
                    asmL(next_lbl);
                    continue;
                }
            }

            /* If has_eq: assign replacement to subject variable */
            if (s->has_eq && s->subject &&
                (s->subject->kind == E_VART || s->subject->kind == E_KW)) {
                const char *fail_target = id_f >= 0 ? sfail_lbl : next_lbl;
                int is_output = strcasecmp(s->subject->sval, "OUTPUT") == 0;
                /* For keyword LHS (&VAR), NV_SET_fn expects bare name "ANCHOR" not "&ANCHOR" */
                const char *subj_name = s->subject->sval ? s->subject->sval : "";
                /* (E_KW: sval is already the bare keyword name e.g. "ANCHOR") */
                /* Null RHS: X = (no replacement or E_NULV) → clear variable.
                 * OUTPUT = (null RHS) → print a blank line (emit NULVCL → SET_OUTPUT). */
                if (!s->replacement ||
                    (s->replacement->kind == E_NULV)) {
                    if (is_output) {
                        A("    LOAD_NULVCL\n");
                        A("    SET_OUTPUT\n");
                    } else {
                        const char *vlab = prog_str_intern(subj_name);
                        A("    ASSIGN_NULL %s\n", vlab);
                    }
                /* Fast path: simple literal RHS + non-OUTPUT target → ASSIGN_INT/ASSIGN_STR */
                } else if (!is_output && s->replacement->kind == E_ILIT) {
                    const char *vlab = prog_str_intern(subj_name);
                    A("    ASSIGN_INT  %s, %ld, %s\n", vlab,
                      (long)s->replacement->ival, fail_target);
                } else if (!is_output && s->replacement->kind == E_QLIT) {
                    const char *vlab = prog_str_intern(subj_name);
                    const char *rlab = prog_str_intern(s->replacement->sval);
                    A("    ASSIGN_STR  %s, %s, %s\n", vlab, rlab, fail_target);
                } else {
                    /* General path */
                    prog_emit_expr(s->replacement, -32);
                    /* stmt_is_fail check on RHS */
                    A("    FAIL_BR     %s\n", fail_target);
                    /* Assign: if subject is OUTPUT, call stmt_output */
                    if (is_output) {
                        A("    SET_OUTPUT\n");
                    } else {
                        const char *vlab = prog_str_intern(subj_name);
                        A("    SET_VAR     %s\n", vlab);
                    }
                }
                /* success path */
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                /* failure path */
                if (id_f >= 0) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                }
            } else if (s->has_eq && s->subject &&
                       (s->subject->kind == E_DOL || s->subject->kind == E_INDR)) {
                /* $expr = val  — indirect assignment.
                 * Parser uses E_INDR (right=operand) for $X in subject position.
                 * E_DOL (left=operand) is the binary capture op; support both. */
                const char *fail_target = id_f >= 0 ? sfail_lbl : next_lbl;
                /* Eval the name expression → [rbp-16/8] */
                /* SNOBOL4 parser puts operand in ->right for E_INDR.
                 * Snocone sc_lower puts it in ->left.  Accept either. */
                EXPR_t *indir_name;
                if (s->subject->kind == E_INDR)
                    indir_name = s->subject->right ? s->subject->right : s->subject->left;
                else
                    indir_name = s->subject->left  ? s->subject->left  : s->subject->right;
                prog_emit_expr(indir_name, -16);
                /* Eval the RHS → [rbp-32/24] */
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    /* null RHS for indirect: load NULVCL into [rbp-32/24] */
                    A("    mov     qword [rbp-32], 1\n");
                    A("    mov     qword [rbp-24], 0\n");
                } else {
                    prog_emit_expr(s->replacement, -32);
                    A("    FAIL_BR     %s\n", fail_target);
                }
                A("    SET_VAR_INDIR\n");
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                if (id_f >= 0) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                }
            } else {
                /* expression-only or complex case: just evaluate and branch */
                if (id_u >= 0) emit_jmp(tgt_u, next_lbl);
            }
            asmL(next_lbl);
            continue;
        }

        /* Case 2: pattern-match statement
         * subject pattern [= replacement] :S(L)F(L)
         *
         * Four-port Byrd box execution (Proebsting):
         *   alpha = entry, beta = resume, gamma = succeed, omega = fail
         *
         *   1. Eval subject → DESCR_t, call stmt_setup_subject()
         *      (copies string into subject_data[], sets subject_len_val, cursor=0)
         *   2. jmp pat_N_alpha  — start the pattern
         *   3. pat_N_gamma:     — match succeeded
         *      apply replacement (if any), emit_jmp S-target
         *   4. pat_N_omega:     — match failed
         *      emit_jmp F-target
         *
         * The Byrd box code is emitted inline via emit_asm_node().
         */

        /* Greek suffixes: internal Byrd box ports — not exportable as linker symbols.
         * α=entry β=resume γ=succeed ω=fail  (Proebsting four-port model) */
        char pat_alpha[64]; snprintf(pat_alpha, sizeof pat_alpha, "P_%d_α", uid);
        char pat_beta[64];  snprintf(pat_beta,  sizeof pat_beta,  "P_%d_β", uid);
        char pat_gamma[64]; snprintf(pat_gamma, sizeof pat_gamma, "P_%d_γ", uid);
        char pat_omega[64]; snprintf(pat_omega, sizeof pat_omega, "P_%d_ω", uid);

        /* -- subject eval -- */
        prog_emit_expr(s->subject, -16);
        /* stmt_setup_subject — copies into subject_data[], resets cursor=0 */
        A("    SUBJ_FROM16\n");

        /* Unanchored scan: try pattern at positions 0..subject_len.
         * scan_start_N tracks the current attempt start position.
         * On omega: if scan_start < subject_len, advance and retry alpha.
         * On gamma: match succeeded from scan_start_N. */
        char scan_start[64]; snprintf(scan_start, sizeof scan_start, "scan_start_%d", uid);
        char scan_retry[64]; snprintf(scan_retry, sizeof scan_retry, "scan_retry_%d", uid);
        bss_add(scan_start);
        /* reset scan_start to 0 (subject_data cursor already 0 from SUBJ_FROM16) */
        A("    mov     qword [%s], 0\n", scan_start);

        /* -- scan retry entry: set cursor = scan_start, then try pattern -- */
        A("%s:\n", scan_retry);
        A("    mov     rax, [%s]\n", scan_start);
        A("    mov     [cursor], rax\n");
        A("    jmp     %s\n", pat_alpha);

        /* -- Byrd box inline: alpha/beta → gamma/omega -- */
        A("\n");
        emit_asm_node(s->pattern,
                      pat_alpha, pat_beta,
                      pat_gamma, pat_omega,
                      "cursor", "subject_data", "subject_len_val",
                      0 /* depth */);
        A("\n");

        /* -- gamma: match succeeded -- */
        asmL(pat_gamma);
        /* Materialise DOL/NAM captures into SNOBOL4 variables */
        for (int ci = 0; ci < cap_var_count; ci++) {
            const char *vnlab = prog_str_intern(cap_vars[ci].varname);
            A("    SET_CAPTURE %s, %s, %s\n",
              vnlab, cap_vars[ci].buf_sym, cap_vars[ci].len_sym);
        }
        if (s->has_eq && s->subject && s->subject->kind == E_VART) {
            /* apply replacement → subject variable (splice: prefix+repl+suffix) */
            const char *vlab = prog_str_intern(s->subject->sval);
            if (!s->replacement || s->replacement->kind == E_NULV) {
                /* null replacement: delete matched span — emit NULVCL */
                A("    mov     qword [rbp-32], 1\n"); /* DT_SNUL */
                A("    mov     qword [rbp-24], 0\n");
            } else {
                prog_emit_expr(s->replacement, -32);
            }
            A("    APPLY_REPL_SPLICE  %s, %s\n", vlab, scan_start);
        }
        emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);

        /* -- omega: match failed at this scan_start position --
         * Anchored (&ANCHOR != 0): go directly to F-target, no retry.
         * Unanchored: advance scan_start by 1, retry if not past subject end. */
        asmL(pat_omega);
        {
            /* scan_fail = F-target if explicit :F(), else unconditional target if :(L), else fall-through */
            const char *fail_dest = tgt_f ? tgt_f : (tgt_u ? tgt_u : NULL);
            const char *scan_fail = fail_dest ? prog_label_nasm(fail_dest) : next_lbl;
            /* &ANCHOR check: if kw_anchor != 0, skip retry entirely */
            A("    cmp     qword [rel kw_anchor], 0\n");
            A("    jne     %s\n", scan_fail);
            /* Unanchored retry: advance scan_start, retry if not exhausted */
            A("    mov     rax, [%s]\n", scan_start);
            A("    inc     rax\n");
            A("    cmp     rax, [subject_len_val]\n");
            A("    jg      %s\n", scan_fail);
            A("    mov     [%s], rax\n", scan_start);
            A("    jmp     %s\n", scan_retry);
        }
        emit_jmp(tgt_f ? tgt_f : tgt_u, next_lbl);

        asmL(next_lbl);
    }

    /* Safety net end — also serves as RETURN/FRETURN/END target */
    flush_pending_sep(); emit_sep_major("END");
    A("L_SNO_END:\n");
    flush_pending_label();
    A("    PROG_END\n");

    /* ---- Emit lit_table strings used by Byrd box LIT nodes ---- */
    if (lit_count > 0) {
        A("\nsection .data\n");
        for (int i = 0; i < lit_count; i++) {
            if (lit_table[i].len == 0) {
                A("%-20s db 0  ; \"\"\n", lit_table[i].label);
                continue;
            }
            A("%-20s db ", lit_table[i].label);
            for (int j = 0; j < lit_table[i].len; j++) {
                if (j) A(", ");
                A("%d", (unsigned char)lit_table[i].val[j]);
            }
            A("\n");
        }
    }

    /* ---- Named pattern bodies — must be in .text (executable) ---- */
    flush_pending_sep(); emit_sep_major("NAMED PATTERN BODIES");
    A("section .text\n");
    for (int i = 0; i < asm_named_count; i++) {
        if (asm_named[i].pat) {
            EKind rk = asm_named[i].pat->kind;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
        }
        emit_asm_named_def(&asm_named[i],
                           "cursor","subject_data","subject_len_val");
    }

    flush_pending_sep(); emit_sep_major("STUB LABELS");
    A("section .text\n"); /* stubs must also be in .text */
    /* ---- Stub definitions for referenced-but-undefined labels ----
     * These are dangling gotos (e.g. :F(error) with no "error" label defined,
     * or computed goto dispatch labels not yet implemented).
     * Map them to _SNO_END so the program assembles and terminates cleanly. */
    for (int i = 0; i < prog_label_count; i++) {
        if (!prog_label_defined[i]) {
            A("%s:  ; STUB → _SNO_END (dangling or computed goto)\n",
              prog_label_nasm(prog_labels[i]));
            A("    GOTO_ALWAYS  L_SNO_END\n");
        }
    }

    /* ---- .data emitted last so all prog_str_intern() calls are captured ---- */
    flush_pending_sep(); emit_sep_major("STRING TABLE");
    A("section .data\n");
    prog_str_emit_data();
    prog_flt_emit_data();
}

void asm_emit(Program *prog, FILE *f) {
    asm_out = f;

    /* Pass 1: scan for named pattern assignments */
    asm_scan_named_patterns(prog);

    /* Pass 2: find first statement with a pattern field (needed for body mode) */
    STMT_t *s = prog ? prog->head : NULL;
    while (s && !s->pattern) s = s->next;

    if (asm_body_mode) {
        /* -asm-body: emit pattern-only body for harness linking */
        if (!s) { asm_emit_null_program(); return; }
        asm_emit_body(s);
    } else {
        /* -asm: full program mode — walk all statements */
        asm_emit_program(prog);
    }
}
