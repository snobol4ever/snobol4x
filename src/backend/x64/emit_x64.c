/*
 * emit_byrd_asm.c — x64 ASM Byrd box emitter for scrip-cc
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
 *   SEQ (E_SEQ)      — recursive left/right wiring
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

#define EMIT_BYRD_ASM_C
#include "scrip_cc.h"
#include "ir/ir_emit_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>

/* -----------------------------------------------------------------------
 * Output helpers
 * ----------------------------------------------------------------------- */

static FILE *out;

/* Column alignment — every instruction starts at COL_W.
 * col tracks the current column (0 = start of line).
 * emit_to_col(n) pads with spaces until col == n.
 * If col >= n already (long label case), emit newline first.
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

static int col = 0;

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
        fprintf(out, "\n; %s\n", pending_sep);
        col = 0;
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
    fprintf(out, "\n");
    col = 0;
    /* NOTE: no second \n here — sep line itself provides the visual break,
     * and asmL will emit the label immediately after pending_sep is consumed. */
}

/* flush_pending_sep — called when no label follows: emit sep at col 1 */
static void flush_pending_sep(void) {
    if (pending_sep[0]) {
        fprintf(out, "; %s\n", pending_sep);
        col = 0;
        pending_sep[0] = '\0';
    }
}

/* emit_sep_minor — light horizontal rule: ; ----...  (SEP_W chars total)
 * Used for: γ/ω trampoline boundary within named pattern defs. */
static void emit_sep_minor(const char *tag) {
    int used = 2;
    fprintf(out, "; ");
    if (tag && tag[0]) {
        used += fprintf(out, " %s ", tag);
    }
    for (int i = used; i < SEP_W; i++) fputc('-', out);
    fputc('\n', out);
    col = 0;
}

static void oc_char(char c) {
    fputc(c, out);
    if (c == '\n') col = 0;
    /* Count only non-continuation UTF-8 bytes as display columns */
    else if ((c & 0xC0) != 0x80) col++;
}

static void oc_str(const char *s) {
    for (; *s; s++) oc_char(*s);
}

static void emit_to_col(int n) {
    if (col >= n) { oc_char('\n'); }
    while (col < n) oc_char(' ');
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
        if (col < col3) emit_to_col(col3);
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

/* -----------------------------------------------------------------------
 * emit3 — structured three-column instruction emitter.
 *
 * This is the single choke-point for all NASM output.  Every instruction
 * goes through here; directives and raw lines use emit_raw().
 *
 * Parameters:
 *   label    — NASM label string WITHOUT trailing colon, or NULL
 *   opcode   — opcode / macro name, e.g. "mov", "ASSIGN_INT"
 *   operands — operand string, e.g. "rdi, [rbp-16]", or NULL
 *
 * Column layout (defined by COL_W / COL2_W):
 *   col 0          : label: (if any)
 *   col COL_W      : opcode
 *   col COL_W+COL2_W : operands (if any)
 *
 * Label-fold rule: if pending_label is set, it is emitted on this same
 * line rather than flushed standalone.  Pending sep is attached before
 * the label when present.
 * ----------------------------------------------------------------------- */
static void emit3(const char *label, const char *opcode, const char *operands) {
    /* Resolve which label appears on this line: explicit arg wins over pending */
    const char *lbl = (label && *label) ? label : NULL;
    if (!lbl && pending_label[0]) {
        lbl = pending_label;
    }

    /* Attach pending sep immediately before the label line */
    if (pending_sep[0]) {
        oc_char(';'); oc_char(' '); oc_str(pending_sep); oc_char('\n');
        pending_sep[0] = '\0';
    }

    /* Emit label + colon if present */
    if (lbl) {
        oc_str(lbl);
        oc_char(':');
        pending_label[0] = '\0';
    }

    /* Pad to COL_W, emit opcode */
    emit_to_col(COL_W);
    oc_str(opcode);

    /* Pad to COL3, emit operands */
    if (operands && *operands) {
        int col3 = COL_W + COL2_W;
        if (col < col3) emit_to_col(col3);
        else oc_char(' ');
        oc_str(operands);
    }
    oc_char('\n');
}

/* emit_raw — emit a line that must not be column-formatted:
 * directives (section, global, extern, resq, db, dq, %include),
 * comment lines, blank lines, sep macros.
 * Flushes pending_label standalone first (a label before a directive
 * gets its own line). */
static void emit_raw(const char *text) {
    flush_pending_label();
    oc_str(text);
}

/* fmt_op — format operand string into a thread-local static buffer.
 * Used by A() to split "    MACRO  op1, op2\n" into opcode + operands. */
static char _fmt_op_buf[512];
static const char *fmt_op(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_fmt_op_buf, sizeof _fmt_op_buf, fmt, ap);
    va_end(ap);
    return _fmt_op_buf;
}

/* -----------------------------------------------------------------------
 * A — printf-style assembler output.
 *
 * Compatibility entry point: parses the formatted string to split it into
 * (label, opcode, operands) and delegates to emit3 / emit_raw.
 *
 * String shapes handled:
 *   "    OPCODE  operands\n"   → emit3(NULL, opcode, operands)
 *   "    OPCODE\n"             → emit3(NULL, opcode, NULL)
 *   "label:\n"                 → flush_pending_label; pending_label = label
 *                               (handled by asmL, not A — kept for safety)
 *   "\n"                       → emit_raw("\n")   blank line
 *   "    ; comment\n"          → emit_raw(text)   passthrough
 *   "    section ...\n" etc.   → emit_raw(text)   directive
 * ----------------------------------------------------------------------- */
static void A(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    const char *p = buf;

    /* Emit any leading newlines as raw blank lines */
    while (*p == '\n') { emit_raw("\n"); p++; }
    if (!*p) return;

    /* Indented content: strip leading whitespace, classify */
    if (*p == ' ' || *p == '\t') {
        while (*p == ' ' || *p == '\t') p++;

        /* Directives and passthrough lines — emit raw (label survives) */
        int is_directive =
            strncmp(p, "section", 7) == 0 ||
            strncmp(p, "global",  6) == 0 ||
            strncmp(p, "extern",  6) == 0 ||
            strncmp(p, "resq",    4) == 0 ||
            strncmp(p, "db ",     3) == 0 ||
            strncmp(p, "dq ",     3) == 0 ||
            strncmp(p, "dd ",     3) == 0 ||
            strncmp(p, "%include",8) == 0 ||
            strncmp(p, "STMT_SEP",8) == 0 ||
            strncmp(p, "PORT_SEP",8) == 0 ||
            p[0] == ';';
        if (is_directive) {
            /* Label survives directives — don't flush it */
            oc_str(p);
            return;
        }

        /* Instruction: split into opcode + operands, call emit3 */
        const char *op_start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        /* Grab opcode into a small buffer */
        char opcode[128];
        int oplen = (int)(p - op_start);
        if (oplen >= (int)sizeof opcode) oplen = (int)sizeof opcode - 1;
        memcpy(opcode, op_start, oplen);
        opcode[oplen] = '\0';
        /* Skip whitespace between opcode and operands */
        while (*p == ' ' || *p == '\t') p++;
        /* Strip trailing newline from operands */
        char operands[1024] = "";
        if (*p && *p != '\n') {
            int olen = (int)strlen(p);
            if (olen > 0 && p[olen-1] == '\n') olen--;
            if (olen >= (int)sizeof operands) olen = (int)sizeof operands - 1;
            memcpy(operands, p, olen);
            operands[olen] = '\0';
        }
        emit3(NULL, opcode, operands[0] ? operands : NULL);
        return;
    }

    /* Non-indented: raw passthrough (labels are handled by asmL, not A) */
    oc_str(p);
}

/* -----------------------------------------------------------------------
 * UID counter — never resets so labels never collide across patterns
 * ----------------------------------------------------------------------- */

static int uid = 0;
static int next_uid(void) { return uid++; }

/* Separate counter for user-function call sites — independent of main uid stream.
 * This allows pre-scanning all ucall slots before .bss is emitted. */
static int call_uid_counter = 0;
static int call_uid(void) { return call_uid_counter++; }

/* -----------------------------------------------------------------------
 * M-ASM-READABLE: special-char expansion table.
 * Converts a SNOBOL4 source name to a valid, readable ASM label fragment.
 * Each special char expands to a named token surrounded by underscores.
 * The mapping is injective: distinct source names produce distinct labels.
 * expand_name(src, dst, dstlen) — fills dst, always NUL-terminates.
 * ----------------------------------------------------------------------- */
static void expand_name(const char *src, char *dst, int dstlen) {
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
 * Buffer size constants — needed by both BoxDataCtx and .bss registry
 * ----------------------------------------------------------------------- */
#define LBUF 320
#define LBUF2 (LBUF * 2)
#define NAME_LEN 320

/* -----------------------------------------------------------------------
 * M-T2-EMIT-SPLIT: per-box DATA layout registry
 *
 * When emitting a named box (pattern or function), vars that belong to
 * that box move from flat .bss into a per-box DATA template section.
 * The box's code accesses them as [r12+offset] instead of [var_name].
 *
 * Usage:
 *   box_ctx_begin(safe_name)   — enter box context
 *   box_var_register(name)     — register a var in the current box
 *   bref(name)                 — returns "r12+N" or "name" if not in box
 *   bref2(name)                — returns "[r12+N]" or "[name]"
 *   box_ctx_end()              — leave box context
 *   box_data_emit_section(s)   — emit the DATA template section for a box
 *   box_data_size(safe_name)   — byte size of the DATA block
 * ----------------------------------------------------------------------- */

#define MAX_BOX_DATA_VARS 128
#define MAX_BOXES 512

typedef struct {
    char safe[NAME_LEN];                    /* box safe name */
    char vars[MAX_BOX_DATA_VARS][NAME_LEN]; /* var names in layout order */
    int  nvar;                              /* number of vars */
} BoxDataCtx;

/* Heap-allocated to avoid huge BSS segment (512*128*320 = 20MB static) */
static BoxDataCtx *box_data = NULL;
static int         box_data_count = 0;

static void box_data_init(void) {
    if (!box_data)
        box_data = (BoxDataCtx *)calloc(MAX_BOXES, sizeof(BoxDataCtx));
}

/* Currently active box context (-1 = none) */
static int box_ctx_idx = -1;

static BoxDataCtx *box_find(const char *safe) {
    for (int i = 0; i < box_data_count; i++)
        if (strcmp(box_data[i].safe, safe) == 0)
            return &box_data[i];
    return NULL;
}

static BoxDataCtx *box_get_or_create(const char *safe) {
    BoxDataCtx *b = box_find(safe);
    if (b) return b;
    if (box_data_count >= MAX_BOXES) return NULL;
    b = &box_data[box_data_count++];
    snprintf(b->safe, sizeof b->safe, "%s", safe);
    b->nvar = 0;
    return b;
}

/* Enter box-emit context: subsequent var_register calls route to this box */
static void box_ctx_begin(const char *safe) {
    BoxDataCtx *b = box_get_or_create(safe);
    if (!b) { box_ctx_idx = -1; return; }
    box_ctx_idx = (int)(b - box_data);
}

static void box_ctx_end(void) { box_ctx_idx = -1; }

/* Register a var in the active box context only (caller ensures box_ctx_idx >= 0) */
static void box_var_register(const char *name) {
    if (box_ctx_idx < 0) return;  /* safety: no active box — caller error */
    BoxDataCtx *b = &box_data[box_ctx_idx];
    for (int i = 0; i < b->nvar; i++)
        if (strcmp(b->vars[i], name) == 0) return;
    if (b->nvar < MAX_BOX_DATA_VARS)
        snprintf(b->vars[b->nvar++], NAME_LEN, "%s", name);
}

/* Look up a var's byte offset in a box DATA block, or -1 if not found */
static int box_var_offset(const char *safe, const char *name) {
    const BoxDataCtx *b = box_find(safe);
    if (!b) return -1;
    for (int i = 0; i < b->nvar; i++)
        if (strcmp(b->vars[i], name) == 0) return i * 8;
    return -1;
}

/* Return byte size of a box's DATA block (0 if not found) */
static int box_data_size(const char *safe) {
    const BoxDataCtx *b = box_find(safe);
    if (!b) return 0;
    return b->nvar * 8;
}

/*
 * bref/bref2 use a rotating pool of buffers so multiple calls in a single
 * printf-style format string (e.g. A("ARB_α %s, %s\n", bref(a), bref(b)))
 * don't alias each other.  8 slots is far more than any single emission uses.
 */
#define BREF_POOL 8
static char _bref_pool[BREF_POOL][72];
static int  _bref_slot = 0;

/*
 * bref — resolve a var reference to either "r12+N" or "var_name".
 * When emitting inside a named box (box_ctx_idx >= 0), vars that belong
 * to the box are addressed via r12.  All others fall back to their .bss name.
 */
static const char *bref(const char *name) {
    if (box_ctx_idx >= 0) {
        BoxDataCtx *b = &box_data[box_ctx_idx];
        for (int i = 0; i < b->nvar; i++) {
            if (strcmp(b->vars[i], name) == 0) {
                char *buf = _bref_pool[_bref_slot++ % BREF_POOL];
                snprintf(buf, 72, "r12+%d", i * 8);
                return buf;
            }
        }
    }
    return name;
}

/*
 * bref2 — like bref but wraps in brackets: returns "[r12+N]" or "[name]".
 * Use this when the caller would write A("    mov rax, [%s]\n", bref2(v)).
 */
static const char *bref2(const char *name) {
    const char *inner = bref(name);
    char *buf = _bref_pool[_bref_slot++ % BREF_POOL];
    snprintf(buf, 72, "[%s]", inner);
    return buf;
}

/* Emit the DATA template section for a box (all qwords zero-initialized) */
static void box_data_emit_section(const char *safe) {
    const BoxDataCtx *b = box_find(safe);
    if (!b || b->nvar == 0) return;
    A("section .data\n");
    A("    align 8\n");
    A("box_%s_data_size: dq %d\n", safe, b->nvar * 8);
    A("box_%s_data_template:\n", safe);
    for (int i = 0; i < b->nvar; i++)
        A("    dq 0  ; [r12+%d] = %s\n", i * 8, b->vars[i]);
    A("\n");
}

static void box_data_reset(void) {
    box_data_count = 0;
    box_ctx_idx = -1;
}

/* -----------------------------------------------------------------------
 * .bss slot registry — program-global (non-box) variables
 * ----------------------------------------------------------------------- */

#define MAX_VARS 8192
static char *vars[MAX_VARS];
static int   nvar = 0;

static void var_reset(void) { nvar = 0; box_data_reset(); }

static void var_register(const char *name) {
    /* When inside a named box, route to per-box DATA layout */
    if (box_ctx_idx >= 0) { box_var_register(name); return; }
    /* deduplicate */
    for (int i = 0; i < nvar; i++)
        if (strcmp(vars[i], name) == 0) return;
    if (nvar < MAX_VARS)
        vars[nvar++] = strdup(name);
}

/* Always register in flat .bss (never routed to box DATA).
 * Use for CALL_PAT slots which CALL_PAT_α/β macros address as absolute labels. */
static void flat_bss_register(const char *name) {
    for (int i = 0; i < nvar; i++)
        if (strcmp(vars[i], name) == 0) return;
    if (nvar < MAX_VARS)
        vars[nvar++] = strdup(name);
}

static void bss_emit(void) {
    if (nvar == 0) return;
    A("\nsection .bss\n");
    for (int i = 0; i < nvar; i++)
        A("%-24s resq 1\n", vars[i]);
}

/* -----------------------------------------------------------------------
 * .data string literals registry
 * ----------------------------------------------------------------------- */

#define MAX_LITS 1024
typedef struct { char label[LBUF + 16]; char *val; int len; } LitEntry;
/* Heap-allocated to avoid ~352KB BSS segment */
static LitEntry *lit_table = NULL;
static int       lit_count = 0;
static void lit_table_init(void) {
    if (!lit_table)
        lit_table = (LitEntry *)calloc(MAX_LITS, sizeof(LitEntry));
}

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
    const char *lbl = e->label;
    return lbl;
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
static char _alfc_ibuf[2048];
#define ALFC(lbl, comment, fmt, ...) do { \
    snprintf(_alfc_ibuf, sizeof _alfc_ibuf, fmt, ##__VA_ARGS__); \
    /* strip trailing newline from instruction */ \
    int _alfc_len = strlen(_alfc_ibuf); \
    if (_alfc_len > 0 && _alfc_ibuf[_alfc_len-1] == '\n') _alfc_ibuf[--_alfc_len] = '\0'; \
    /* emit label: at col 0, instruction at COL_W, then comment (no wrap) */ \
    flush_pending_label(); \
    if (*(lbl)) { oc_str(lbl); oc_char(':'); } \
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
            if (col < _col3) emit_to_col(_col3); else oc_char(' '); \
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
 * emit_lit — LIT node  (Sprint A14: one macro call per port)
 *
 *   α: LIT_α / LIT_α1   — one call site line
 *   β: LIT_β                 — one call site line
 * ----------------------------------------------------------------------- */

static void emit_lit(const char *s, int n,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved", α);
    var_register(saved);

    if (n == 1) {
        ALFC(α, "LIT α", "LIT_α1  %d, %s, %s, %s, %s, %s, %s\n",
            (unsigned char)s[0], bref(saved), cursor, subj, subj_len, γ, ω);
    } else {
        const char *lstr = lit_intern(s, n);
        ALFC(α, "LIT α", "LIT_α   %s, %d, %s, %s, %s, %s, %s, %s\n",
            lstr, n, bref(saved), cursor, subj, subj_len, γ, ω);
    }
    ALFC(β, "LIT β", "LIT_β    %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_pos / emit_rpos
 * ----------------------------------------------------------------------- */

static void emit_pos(long n,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor) {
    ALFC(α, "POS(%ld)", "POS_α   %ld, %s, %s, %s\n", n, cursor, γ, ω);
    ALF(β,  "POS_β    %s, %s\n", cursor, ω);
}

static void emit_pos_var(const char *varlab,
                              const char *α, const char *β,
                              const char *γ, const char *ω,
                              const char *cursor) {
    ALFC(α, "POS(var)", "POS_α_VAR %s, %s, %s, %s\n", varlab, cursor, γ, ω);
    ALF(β,  "POS_β    %s, %s\n", cursor, ω);
}

static void emit_rpos(long n,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *cursor,
                           const char *subj_len) {
    ALFC(α, "RPOS(%ld)", "RPOS_α  %ld, %s, %s, %s, %s\n", n, cursor, subj_len, γ, ω);
    ALF(β,  "RPOS_β   %s, %s\n", cursor, ω);
}

static void emit_rpos_var(const char *varlab,
                               const char *α, const char *β,
                               const char *γ, const char *ω,
                               const char *cursor,
                               const char *subj_len) {
    ALFC(α, "RPOS(var)", "RPOS_α_VAR %s, %s, %s, %s, %s\n", varlab, cursor, subj_len, γ, ω);
    ALF(β,  "RPOS_β   %s, %s\n", cursor, ω);
}

/* -----------------------------------------------------------------------
 * Forward declarations for named pattern machinery
 * (full definitions are after emit_pat_node, which uses them)
 * ----------------------------------------------------------------------- */

typedef struct NamedPat NamedPat;

/* Forward declarations for capture variable registry (defined after named-pat section) */
#define MAX_CAP_VARS 256
typedef struct {
    char varname[64];
    char safe[64];
    char buf_sym[128];
    char len_sym[128];
} CaptureVar;
static CaptureVar *cap_var_register(const char *varname);
/* MAX_PARAMS — max args for a user-defined Snocone procedure */
#define MAX_PARAMS 8

struct NamedPat {
    char varname[NAME_LEN];
    char safe[NAME_LEN];
    char α_lbl[NAME_LEN + 16];
    char β_lbl[NAME_LEN + 16];
    char ret_γ[NAME_LEN + 16];
    char ret_ω[NAME_LEN + 16];
    EXPR_t *pat;
    /* User-defined function fields (is_fn == 1) */
    int  is_fn;                              /* 1 = user Snocone procedure */
    int  nparams;
    char param_names[MAX_PARAMS][64]; /* parameter variable names */
    int  nlocals;
    char local_names[MAX_PARAMS][64]; /* local variable names (after ')' in prototype) */
    char body_label[NAME_LEN + 16]; /* NASM label of function body entry */
    int  uses_nreturn;               /* 1 = function has :(NRETURN) path */
    int  body_end_idx;               /* stmt index of last stmt in body (-1 = unknown) */
    char end_label[NAME_LEN + 16];   /* source label of stmt just after body (e.g. "TWEnd") */
};

static const NamedPat *named_pat_lookup(const char *varname);
static const NamedPat *named_pat_lookup_fn(const char *varname);
static void emit_named_ref(const NamedPat *np,
                                const char *α, const char *β,
                                const char *γ, const char *ω);

/* Forward declaration for plain-string variable registry (VAR = 'literal') */
typedef struct { char varname[NAME_LEN]; const char *sval; } StrVar;
static const StrVar *str_var_lookup(const char *varname);

/* Forward declarations for program-mode helpers used by emit_named_def */
static const char *str_intern(const char *s);
static const char *label_nasm(const char *lbl);

/* -----------------------------------------------------------------------
 * Forward declaration for recursive emit_pat_node
 * ----------------------------------------------------------------------- */

/* Forward — emit_pat_node calls emit_expr for CALL_PAT */
static int emit_expr(EXPR_t *e, int rbp_off);

static void emit_pat_node(EXPR_t *pat,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *cursor,
                           const char *subj, const char *subj_len,
                           int depth);

/* -----------------------------------------------------------------------
 * emit_seq — SEQ (E_SEQ / CAT)
 *
 * Wiring (Proebsting §4.3, oracle cat_pos_lit_rpos.s):
 *   α → left_α
 *   β → right_β
 *   left_γ  → right_α
 *   left_ω  → ω
 *   right_γ → γ
 *   right_ω → left_β
 * ----------------------------------------------------------------------- */

static void emit_seq(EXPR_t *left, EXPR_t *right,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj, const char *subj_len,
                          int depth) {
    int uid = next_uid();
    char lα[LBUF], lβ[LBUF], rα[LBUF], rβ[LBUF];
    snprintf(lα, LBUF, "seq_l%d_α", uid);
    snprintf(lβ, LBUF, "seq_l%d_β",  uid);
    snprintf(rα, LBUF, "seq_r%d_α", uid);
    snprintf(rβ, LBUF, "seq_r%d_β",  uid);

    ALFC(α, "SEQ", "jmp     %s\n", lα);
    ALF(β, "jmp     %s\n", rβ);

    emit_pat_node(left,  lα, lβ, rα, ω, cursor, subj, subj_len, depth+1);
    emit_pat_node(right, rα, rβ, γ, lβ, cursor, subj, subj_len, depth+1);
}

/* -----------------------------------------------------------------------
 * emit_alt — ALT (E_OR)
 *
 * Wiring (oracle alt_first.s, Proebsting §4.5 ifstmt):
 *   α → save cursor_at_alt; → left_α
 *   β → right_β
 *   left_γ  → γ
 *   left_ω  → restore cursor_at_alt; → right_α
 *   right_γ → γ
 *   right_ω → ω
 * ----------------------------------------------------------------------- */

static void emit_alt(EXPR_t *left, EXPR_t *right,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj, const char *subj_len,
                          int depth) {
    int uid = next_uid();
    char lα[LBUF], lβ[LBUF], rα[LBUF], rβ[LBUF];
    char cursor_save[LBUF], restore_lbl[LBUF];
    snprintf(lα,           LBUF, "alt_l%d_α",   uid);
    snprintf(lβ,           LBUF, "alt_l%d_β",    uid);
    snprintf(rα,           LBUF, "alt_r%d_α",   uid);
    snprintf(rβ,           LBUF, "alt_r%d_β",    uid);
    snprintf(cursor_save,  LBUF, "alt%d_cur_save",  uid);
    snprintf(restore_lbl,  LBUF, "alt%d_restore",   uid);
    var_register(cursor_save);

    char left_ω_tramp[LBUF];
    snprintf(left_ω_tramp, LBUF, "alt%d_left_ω", uid);

    ALFC(α, "ALT α — save cursor, enter left",
         "ALT_α   %s, %s, %s\n", bref(cursor_save), cursor, lα);
    ALFC(β,  "ALT β — resume right", "SEQ_β    %s\n", rβ);

    emit_pat_node(left, lα, lβ, γ, left_ω_tramp,
                  cursor, subj, subj_len, depth+1);

    ALFC(left_ω_tramp, "ALT left_ω — restore cursor, enter right",
         "ALT_ω   %s, %s, %s\n", bref(cursor_save), cursor, rα);

    emit_pat_node(right, rα, rβ, γ, ω,
                  cursor, subj, subj_len, depth+1);
}

/* -----------------------------------------------------------------------
 * emit_arbno — ARBNO
 *
 * Wiring (oracle arbno_match.s, v311.sil ARBN/EARB/ARBF):
 *   α: push cursor; goto γ  (zero reps = immediate success)
 *   β: pop cursor; try child_α
 *      child_γ → zero-advance guard; push new cursor; goto γ
 *      child_ω → ω
 * ----------------------------------------------------------------------- */

static void emit_arbno(EXPR_t *child,
                            const char *α, const char *β,
                            const char *γ, const char *ω,
                            const char *cursor,
                            const char *subj, const char *subj_len,
                            int depth) {
    int uid = next_uid();
    char stk[LBUF], dep[LBUF], cα[LBUF], cβ[LBUF];
    char child_ok[LBUF], child_ω[LBUF], cur_before[LBUF];
    char push_lbl[LBUF], pop_lbl[LBUF];

    snprintf(stk,        LBUF, "arb%d_stack",       uid);
    snprintf(dep,        LBUF, "arb%d_depth",        uid);
    snprintf(cur_before, LBUF, "arb%d_cur_before",   uid);
    snprintf(cα,         LBUF, "arb%d_child_α",  uid);
    snprintf(cβ,         LBUF, "arb%d_child_β",   uid);
    snprintf(child_ok,   LBUF, "arb%d_child_ok",     uid);
    snprintf(child_ω, LBUF, "arb%d_child_fail",   uid);
    snprintf(push_lbl,   LBUF, "arb%d_push",         uid);
    snprintf(pop_lbl,    LBUF, "arb%d_pop",          uid);

    /* stk needs 64 slots — we emit it specially */
    var_register(dep);
    var_register(cur_before);
    /* stk: emit as resq 64 — we track this separately */
    /* We'll add it to a special multi-slot list */
    static char arbno_stack_decls[512][LBUF + 16];
    static int  arbno_stack_count = 0;
    if (arbno_stack_count < 512) {
        snprintf(arbno_stack_decls[arbno_stack_count], sizeof arbno_stack_decls[0],
                 "%-24s resq 64", stk);
        arbno_stack_count++;
    }

    A("\n");

    /* α: init depth=0, push cursor onto stk[0], goto γ */
    ALFC(α, "ARBNO α",
         "ARBNO_α %s, %s, %s, %s\n", bref(dep), stk, cursor, γ);

    /* β: pop, save cursor-before-rep, try child (or ω if empty) */
    ALFC(β, "ARBNO β",
         "ARBNO_β  %s, %s, %s, %s, %s, %s\n",
         bref(dep), stk, bref(cur_before), cursor, cα, ω);

    /* child_ok: zero-advance guard, push new cursor, re-succeed */
    ALFC(child_ok, "ARBNO child_ok",
         "ARBNO_α1 %s, %s, %s, %s, %s, %s\n",
         bref(dep), stk, bref(cur_before), cursor, γ, ω);

    /* child_ω: ω */
    ALFC(child_ω, "ARBNO β1", "ARBNO_β1 %s\n", ω);

    emit_pat_node(child, cα, cβ, child_ok, child_ω,
                  cursor, subj, subj_len, depth+1);

    /* Patch: we need to emit the resq 64 for stk in section .bss.
     * We use a hack: emit it as a comment-tagged entry so bss_emit
     * can handle it. We store it directly since var_register only does resq 1. */
    /* Replace the var_register approach: use a global extra-bss list */
    extern char extra_slots[][LBUF + 16];
    extern int  extra_slot_count;
    if (extra_slot_count < 512) {
        snprintf(extra_slots[extra_slot_count++], sizeof extra_slots[0],
                 "%-24s resq 64", stk);
    }
}

/* -----------------------------------------------------------------------
 * emit_any — ANY(S)  (Sprint A14: one macro call per port)
 * ----------------------------------------------------------------------- */

static void emit_any(const char *charset, int cslen,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj, const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "any%d_saved", uid);
    var_register(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(α, "ANY α", "ANY_α   %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β, "ANY β", "ANY_β    %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_notany — NOTANY(S)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_notany(const char *charset, int cslen,
                             const char *α, const char *β,
                             const char *γ, const char *ω,
                             const char *cursor,
                             const char *subj, const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "nany%d_saved", uid);
    var_register(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(α, "NOTANY α", "NOTANY_α %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β, "NOTANY β", "NOTANY_β  %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_span — SPAN(S)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_span(const char *charset, int cslen,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *cursor,
                           const char *subj, const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "span%d_saved", uid);
    var_register(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(α, "SPAN α", "SPAN_α  %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β, "SPAN β", "SPAN_β   %s, %s, %s\n", bref(saved), cursor, ω);
}

static void emit_break(const char *charset, int cslen,
                            const char *α, const char *β,
                            const char *γ, const char *ω,
                            const char *cursor,
                            const char *subj, const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "brk%d_saved", uid);
    var_register(saved);
    const char *clabel = lit_intern(charset, cslen);

    ALFC(α, "BREAK α", "BREAK_α %s, %d, %s, %s, %s, %s, %s, %s\n",
      clabel, cslen, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β, "BREAK β", "BREAK_β  %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * Variable-charset emitters — SPAN/BREAK/ANY/NOTANY with E_VART arg
 * ----------------------------------------------------------------------- */

static void emit_span_var(const char *varlab,
                               const char *α, const char *β,
                               const char *γ, const char *ω,
                               const char *cursor,
                               const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "span%d_saved", next_uid());
    var_register(saved);
    ALFC(α, "SPAN(var) α", "SPAN_α_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β,  "SPAN(var) β", "SPAN_β_VAR  %s, %s, %s\n", bref(saved), cursor, ω);
}

static void emit_break_var(const char *varlab,
                                const char *α, const char *β,
                                const char *γ, const char *ω,
                                const char *cursor,
                                const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "brk%d_saved", next_uid());
    var_register(saved);
    ALFC(α, "BREAK(var) α", "BREAK_α_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β,  "BREAK(var) β", "BREAK_β_VAR  %s, %s, %s\n", bref(saved), cursor, ω);
}

static void emit_breakx_var(const char *varlab,
                                 const char *α, const char *β,
                                 const char *γ, const char *ω,
                                 const char *cursor,
                                 const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "brkx%d_saved", next_uid());
    var_register(saved);
    ALFC(α, "BREAKX(var) α", "BREAKX_α_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β,  "BREAKX(var) β", "BREAKX_β_VAR  %s, %s, %s\n", bref(saved), cursor, ω);
}

static void emit_breakx_lit(const char *charset, int cslen,
                                 const char *α, const char *β,
                                 const char *γ, const char *ω,
                                 const char *cursor,
                                 const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "brkx%d_saved", next_uid());
    var_register(saved);
    const char *clabel = lit_intern(charset, cslen);
    ALFC(α, "BREAKX(lit) α", "BREAKX_α_LIT %s, %s, %s, %s, %s, %s, %s\n",
         clabel, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β,  "BREAKX(lit) β", "BREAKX_β_LIT  %s, %s, %s\n", bref(saved), cursor, ω);
}

static void emit_any_var(const char *varlab,
                              const char *α, const char *β,
                              const char *γ, const char *ω,
                              const char *cursor,
                              const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "any%d_saved", next_uid());
    var_register(saved);
    ALFC(α, "ANY(var) α", "ANY_α_VAR   %s, %s, %s, %s, %s, %s, %s\n",
         varlab, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β,  "ANY(var) β", "ANY_β_VAR    %s, %s, %s\n", bref(saved), cursor, ω);
}

static void emit_notany_var(const char *varlab,
                                 const char *α, const char *β,
                                 const char *γ, const char *ω,
                                 const char *cursor,
                                 const char *subj, const char *subj_len) {
    char saved[LBUF];
    snprintf(saved, LBUF, "nany%d_saved", next_uid());
    var_register(saved);
    ALFC(α, "NOTANY(var) α", "NOTANY_α_VAR %s, %s, %s, %s, %s, %s, %s\n",
         varlab, bref(saved), cursor, subj, subj_len, γ, ω);
    ALFC(β,  "NOTANY(var) β", "NOTANY_β_VAR  %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_len — LEN(N)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_len(long n,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "len%d_saved", uid);
    var_register(saved);

    ALFC(α, "LEN(%ld)", "LEN_α   %ld, %s, %s, %s, %s, %s\n", n, bref(saved), cursor, subj_len, γ, ω);
    ALFC(β, "LEN β", "LEN_β    %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_tab — TAB(N)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_tab(long n,
                          const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "tab%d_saved", uid);
    var_register(saved);

    ALFC(α, "TAB(%ld)", "TAB_α   %ld, %s, %s, %s, %s\n", n, bref(saved), cursor, γ, ω);
    ALFC(β, "TAB β", "TAB_β    %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_rtab — RTAB(N)  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_rtab(long n,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *cursor,
                           const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "rtab%d_saved", uid);
    var_register(saved);

    ALFC(α, "RTAB(%ld)", "RTAB_α  %ld, %s, %s, %s, %s, %s\n", n, bref(saved), cursor, subj_len, γ, ω);
    ALFC(β, "RTAB β", "RTAB_β   %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_rem — REM  (Sprint A14)
 * ----------------------------------------------------------------------- */

static void emit_rem(const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj_len) {
    char saved[LBUF];
    int uid = next_uid();
    snprintf(saved, LBUF, "rem%d_saved", uid);
    var_register(saved);

    ALFC(α, "REM", "REM_α   %s, %s, %s, %s\n", bref(saved), cursor, subj_len, γ);
    ALFC(β, "REM β", "REM_β    %s, %s, %s\n", bref(saved), cursor, ω);
}

/* -----------------------------------------------------------------------
 * emit_arb — ARB: match minimum (0 chars), then backtrack to extend
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

static void emit_arb(const char *α, const char *β,
                          const char *γ, const char *ω,
                          const char *cursor,
                          const char *subj_len) {
    char arb_start[LBUF], arb_step[LBUF], chk[LBUF];
    int uid = next_uid();
    snprintf(arb_start, LBUF, "arb%d_start", uid);
    snprintf(arb_step,  LBUF, "arb%d_step",  uid);
    snprintf(chk,       LBUF, "arb%d_chk",   uid);
    var_register(arb_start);
    var_register(arb_step);

    ALFC(α, "ARB α", "ARB_α   %s, %s, %s, %s\n",
         bref(arb_start), bref(arb_step), cursor, γ);

    ALFC(β,  "ARB β", "ARB_β    %s, %s, %s, %s, %s, %s\n",
         bref(arb_start), bref(arb_step), cursor, subj_len, γ, ω);
    (void)chk;
}

/* -----------------------------------------------------------------------
 * emit_imm — E_DOL (expr $ var) — immediate assignment
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
 * The capture variable name (pat->children[1]->sval) is used to derive
 * the .bss buffer names:  cap_<varname>_N_buf  (resb 256)
 *                          cap_<varname>_N_len  (resq 1)
 *                          dol_<N>_entry        (resq 1)
 *
 * The buffer is emitted via extra_slots (like ARBNO stacks).
 * ----------------------------------------------------------------------- */

static void emit_imm(EXPR_t *child, const char *varname,
                             const char *α, const char *β,
                             const char *γ, const char *ω,
                             const char *cursor,
                             const char *subj, const char *subj_len,
                             int depth) {
    int uid = next_uid();
    char cα[LBUF], cβ[LBUF];
    char dol_γ[LBUF], dol_ω[LBUF];
    char entry_cur[LBUF], cap_buf[LBUF], cap_len[LBUF];

    /* derive safe varname prefix (expand special chars) */
    char safe[64];
    expand_name(varname ? varname : "", safe, sizeof safe);
    if (!safe[0]) { safe[0]='v'; safe[1]='\0'; }

    snprintf(cα,        LBUF, "dol%d_child_α", uid);
    snprintf(cβ,        LBUF, "dol%d_child_β",  uid);
    snprintf(dol_γ, LBUF, "dol%d_γ",        uid);
    snprintf(dol_ω, LBUF, "dol%d_ω",        uid);
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

    /* var_register is a no-op if already registered; harmless in single-pass mode */
    var_register(entry_cur);

    {
        char dol_hdr[LBUF];
        snprintf(dol_hdr, LBUF, "DOL(%s $  %s)", varname ? varname : "?", safe);
        A("\n");
        /* α: header comment on label line — "α: ; DOL(var $ var)" */
        asmLC(α, dol_hdr);
    }

    /* α instruction: save entry cursor, enter child — one macro call */
    ALFC("", "DOL α — save entry cursor",
         "DOL_SAVE    %s, %s, %s\n", bref(entry_cur), cursor, cα);

    /* β: transparent — backtrack into child */
    ALFC(β, "DOL β", "jmp     %s\n", cβ);

    /* Emit child subtree — child's γ goes to dol_γ, child's ω to dol_ω */
    emit_pat_node(child, cα, cβ, dol_γ, dol_ω,
                  cursor, subj, subj_len, depth + 1);

    /* dol_γ: compute span, copy to cap_buf, proceed to outer γ — one macro call */
    ALFC(dol_γ, "DOL γ — capture span",
         "DOL_CAPTURE %s, %s, %s, %s, %s, %s\n",
         bref(entry_cur), cursor, cap_buf, bref(cap_len), subj, γ);

    /* dol_ω: child failed — no assignment, propagate failure */
    ALFC(dol_ω, "DOL ω — child failed", "jmp     %s\n", ω);
}

/* -----------------------------------------------------------------------
 * emit_pat_node — recursive dispatch
 * ----------------------------------------------------------------------- */

static void emit_pat_node(EXPR_t *pat,
                           const char *α, const char *β,
                           const char *γ, const char *ω,
                           const char *cursor,
                           const char *subj, const char *subj_len,
                           int depth) {
    if (!pat) return;

    switch (pat->kind) {
    case E_QLIT:
        emit_lit(pat->sval, (int)strlen(pat->sval),
                     α, β, γ, ω,
                     cursor, subj, subj_len);
        break;

    case E_SEQ: {  /* M-G4-SPLIT-SEQ-CONCAT: pattern context — Byrd-box SEQ */
        int _nc = pat->nchildren;
        if (_nc == 0) break;
        if (_nc == 1) { emit_pat_node(pat->children[0], α, β, γ, ω, cursor, subj, subj_len, depth); break; }
        if (_nc == 2) { emit_seq(pat->children[0], pat->children[1], α, β, γ, ω, cursor, subj, subj_len, depth); break; }
        /* >2: right-fold via shared helper */
        { EXPR_t **_fn, **_fk;
          EXPR_t *_root = ir_nary_right_fold(pat, E_SEQ, &_fn, &_fk);
          emit_pat_node(_root, α, β, γ, ω, cursor, subj, subj_len, depth);
          ir_nary_right_fold_free(_fn, _fk, _nc - 1); }
        break;
    }

    case E_OR: {
        int _nc = pat->nchildren;
        if (_nc == 0) break;
        if (_nc == 1) { emit_pat_node(pat->children[0], α, β, γ, ω, cursor, subj, subj_len, depth); break; }
        if (_nc == 2) { emit_alt(pat->children[0], pat->children[1], α, β, γ, ω, cursor, subj, subj_len, depth); break; }
        /* >2: right-fold via shared helper */
        { EXPR_t **_fn, **_fk;
          EXPR_t *_root = ir_nary_right_fold(pat, E_OR, &_fn, &_fk);
          emit_pat_node(_root, α, β, γ, ω, cursor, subj, subj_len, depth);
          ir_nary_right_fold_free(_fn, _fk, _nc - 1); }
        break;
    }

    case E_VART: {
        /* Variable reference in pattern context — named pattern call site.
         * BUT: zero-arg builtins (REM, ARB, FAIL) appear as E_VART with no
         * parens. Intercept them before the named-pattern lookup. */
        const char *varname = pat->sval ? pat->sval : "";
        if (strcasecmp(varname, "REM") == 0) {
            emit_rem(α, β, γ, ω, cursor, subj_len);
            break;
        }
        if (strcasecmp(varname, "ARB") == 0) {
            emit_arb(α, β, γ, ω, cursor, subj_len);
            break;
        }
        if (strcasecmp(varname, "FAIL") == 0) {
            A("\n; FAIL  α=%s\n", α);
            asmL(α);
            ALF(β, "jmp     %s\n", ω);
            break;
        }
        /* Look up in the named-pattern registry */
        const NamedPat *np = named_pat_lookup(varname);
        if (np) {
            emit_named_ref(np, α, β, γ, ω);
        } else {
            /* Fall back to plain-string variable registry (PAT = 'literal') */
            const StrVar *sv = str_var_lookup(varname);
            if (sv && sv->sval) {
                A("\n; E_VART *%s → inline LIT '%s'\n", varname, sv->sval);
                emit_lit(sv->sval, (int)strlen(sv->sval),
                             α, β, γ, ω,
                             cursor, subj, subj_len);
            } else {
                /* Dynamic variable: may hold DT_P (pattern) or DT_S (string) at
                 * runtime.  B-288: M-BUG-BOOTSTRAP-PARSE — 'Cmd = Word Space Word'
                 * is a concat of E_VARTs; expr_is_pattern_expr returns 0 so Cmd is
                 * never registered as a named pattern.  At match time Cmd holds a
                 * DT_P descriptor — LIT_VAR_α (stmt_match_var) does a memcmp and
                 * always fails against a pattern object.
                 * Fix: use CALL_PAT_α (stmt_match_descr) which dispatches DT_P
                 * through the full engine OR does string match for DT_S.
                 * This is the same path used for E_STAR/*VAR runtime dispatch. */
                int vuid = next_uid();
                char dt_slot[64], dp_slot[64], saved_slot[64];
                snprintf(dt_slot,    sizeof dt_slot,    "rpat%d_t", vuid);
                snprintf(dp_slot,    sizeof dp_slot,    "rpat%d_p", vuid);
                snprintf(saved_slot, sizeof saved_slot, "rpat%d_s", vuid);
                /* B-288: use var_register (box-aware) so slots land in r12 DATA
                 * block when inside a named pattern, not flat .bss.  Flat .bss
                 * slots are global and get clobbered on re-entry via β. */
                var_register(dt_slot);
                var_register(dp_slot);
                var_register(saved_slot);
                const char *vlab = str_intern(varname);
                A("\n; E_VART %s → CALL_PAT (runtime DT_P/DT_S dispatch)\n", varname);
                asmL(α);
                A("    lea     rdi, [rel %s]\n", vlab);
                A("    call    stmt_get\n");
                /* Use explicit mov with bref2() addressing — avoids double-deref
                 * that occurs if CALL_PAT_α macro wraps 'r12+N' in extra brackets. */
                A("    mov     %s, rax\n", bref2(dt_slot));
                A("    mov     %s, rdx\n", bref2(dp_slot));
                /* Expand CALL_PAT_α inline: save cursor, load type+ptr, call engine */
                A("    mov     rax, [%s]\n", cursor);
                A("    mov     %s, rax\n",   bref2(saved_slot));
                A("    mov     rdi, %s\n",   bref2(dt_slot));
                A("    mov     rsi, %s\n",   bref2(dp_slot));
                A("    call    stmt_match_descr\n");
                A("    test    eax, eax\n");
                A("    jz      %s\n", ω);
                A("    jmp     %s\n", γ);
                /* β port: restore cursor, jump ω */
                asmL(β);
                A("    mov     rax, %s\n",   bref2(saved_slot));
                A("    mov     [%s], rax\n", cursor);
                A("    jmp     %s\n", ω);
            }
        }
        break;
    }

    case E_DOL: {
        /* expr $ var — immediate assignment.
         * left = sub-pattern, right = capture variable (E_VART). */
        const char *varname = (pat->children[1] && pat->children[1]->sval)
                              ? pat->children[1]->sval : "cap";
        emit_imm(pat->children[0], varname,
                        α, β, γ, ω,
                        cursor, subj, subj_len, depth);
        break;
    }

    case E_NAM: {
        /* expr . var — conditional assignment. */
        const char *varname = (pat->children[1] && pat->children[1]->sval)
                              ? pat->children[1]->sval : "cap";
        emit_imm(pat->children[0], varname,
                        α, β, γ, ω,
                        cursor, subj, subj_len, depth);
        break;
    }

    case E_STAR:    /* *VAR — deferred pattern ref (value context split in B-280) */
    case E_INDR: {
        /* *VAR — indirect pattern reference.
         * pat->children[0] is E_VART holding the variable name.
         * Look it up in the named-pattern registry and call it.
         * If not a named pattern, try str_vars for plain-string vars
         * (e.g. PAT = 'hello' → *PAT emitted as inline LIT). */
        const char *varname = (pat->children[0] && pat->children[0]->sval)
                              ? pat->children[0]->sval
                              : (pat->sval ? pat->sval : NULL);
        if (varname) {
            const NamedPat *np = named_pat_lookup(varname);
            if (np) {
                emit_named_ref(np, α, β, γ, ω);
            } else {
                /* Try plain-string variable registry */
                const StrVar *sv = str_var_lookup(varname);
                if (sv && sv->sval) {
                    A("\n; E_INDR *%s → inline LIT '%s'\n", varname, sv->sval);
                    emit_lit(sv->sval, (int)strlen(sv->sval),
                                 α, β, γ, ω,
                                 cursor, subj, subj_len);
                } else {
                    /* Runtime DT_P dispatch: *VAR where VAR holds a pattern at runtime.
                     * B-280: M-BEAUTIFY-BOOTSTRAP — *Parse, *Command, *Stmt etc. are
                     * runtime-built patterns (nPush/ARBNO/nPop), not compile-time named pats.
                     * Use CALL_PAT_α/β machinery (same as unknown E_FNC path). */
                    int ruid = next_uid();
                    char dt_slot[64], dp_slot[64], saved_slot[64];
                    snprintf(dt_slot,    sizeof dt_slot,    "rpat%d_t", ruid);
                    snprintf(dp_slot,    sizeof dp_slot,    "rpat%d_p", ruid);
                    snprintf(saved_slot, sizeof saved_slot, "rpat%d_s", ruid);
                    flat_bss_register(dt_slot);
                    flat_bss_register(dp_slot);
                    flat_bss_register(saved_slot);
                    const char *varlab = str_intern(varname);
                    A("\n; E_STAR/E_INDR *%s → runtime DT_P dispatch\n", varname);
                    asmL(α);
                    A("    lea     rdi, [rel %s]\n", varlab);
                    A("    call    stmt_get\n");
                    A("    mov     [%s], rax\n", dt_slot);
                    A("    mov     [%s], rdx\n", dp_slot);
                    A("    CALL_PAT_α %s, %s, %s, %s, %s, %s\n",
                      dt_slot, dp_slot, saved_slot, cursor, γ, ω);
                    ALFC(β, "E_STAR β", "CALL_PAT_β %s, %s, %s\n",
                         saved_slot, cursor, ω);
                }
            }
        } else {
            A("\n; E_INDR no varname → ω\n");
            asmL(α); ALF(β, "jmp     %s\n", ω);
        }
        break;
    }

    case E_FNC:
        if (pat->sval && strcasecmp(pat->sval, "POS") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                const char *varlab = str_intern(arg->sval);
                emit_pos_var(varlab, α, β, γ, ω, cursor);
            } else {
                long n = (arg->kind == E_ILIT) ? arg->ival : 0;
                emit_pos(n, α, β, γ, ω, cursor);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "RPOS") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                const char *varlab = str_intern(arg->sval);
                emit_rpos_var(varlab, α, β, γ, ω, cursor, subj_len);
            } else {
                long n = (arg->kind == E_ILIT) ? arg->ival : 0;
                emit_rpos(n, α, β, γ, ω, cursor, subj_len);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "ARBNO") == 0 && pat->nchildren == 1) {
            emit_arbno(pat->children[0],
                           α, β, γ, ω,
                           cursor, subj, subj_len, depth);
        } else if (pat->sval && strcasecmp(pat->sval, "ANY") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_any_var(str_intern(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else if (arg->kind == E_QLIT && arg->sval) {
                const char *cs = arg->sval;
                int cslen = (int)strlen(arg->sval);
                emit_any(cs, cslen, α, β, γ, ω, cursor, subj, subj_len);
            } else {
                /* Runtime expression (e.g. E_SEQ/E_CONCAT of vars like &UCASE &LCASE):
                 * Evaluate into raw BSS temp slots and dispatch via ANY_α_PTR.
                 * We do NOT call var_register() here — doing so would emit
                 * S_any_expr_tmp_N in BOTH the BSS (resq 1) and the string
                 * literal table (db ...), causing a NASM duplicate-label error.
                 * ANY_α_PTR calls stmt_any_ptr(vtype,vptr,...) directly. */
                char tmplab[LBUF];
                snprintf(tmplab, LBUF, "any_expr_tmp_%d", next_uid());
                char tlab[LBUF], plab[LBUF], saved[LBUF], dispatch_lbl[LBUF];
                snprintf(tlab,         sizeof tlab,         "%s_t", tmplab);
                snprintf(plab,         sizeof plab,         "%s_p", tmplab);
                snprintf(saved,        sizeof saved,        "any%d_saved", next_uid());
                snprintf(dispatch_lbl, sizeof dispatch_lbl, "%s_dispatch", tmplab);
                var_register(saved);
                /* §22 fix: register tlab/plab in the file-level BSS block
                 * (via var_register deferred list) instead of an inline
                 * section .bss/.text switch mid-function, which causes the
                 * slots to be overwritten by recursive ucall stack frames.
                 * NB: tlab/plab are used as direct labels in ANY_α_PTR macro
                 * (not via bref), so they MUST live in flat .bss even inside
                 * a function box — use flat_bss_register, not var_register. */
                flat_bss_register(tlab);
                flat_bss_register(plab);
                /* α label points HERE — at the eval code — so DOL_SAVE
                 * and other jumps to α land at the eval, not past it. */
                ALF(α, "; ANY(expr) α — eval arg then dispatch\n");
                emit_expr(arg, -32);
                A("    mov     [rel %s], rax\n", tlab);
                A("    mov     [rel %s], rdx\n", plab);
                ALFC(dispatch_lbl, "ANY(expr) α dispatch",
                     "ANY_α_PTR   %s, %s, %s, %s, %s, %s, %s, %s\n",
                     tlab, plab, bref(saved),
                     cursor, subj, subj_len, γ, ω);
                ALFC(β,  "ANY(expr) β",
                     "ANY_β_PTR    %s, %s, %s\n",
                     bref(saved), cursor, ω);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "NOTANY") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_notany_var(str_intern(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_notany(cs, cslen, α, β, γ, ω, cursor, subj, subj_len);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "SPAN") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_span_var(str_intern(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else if (arg->kind == E_QLIT && arg->sval) {
                emit_span(arg->sval, (int)strlen(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else {
                char tmplab[LBUF];
                snprintf(tmplab, LBUF, "spn_expr_tmp_%d", next_uid());
                char tlab[LBUF], plab[LBUF], saved[LBUF], dispatch_lbl[LBUF];
                snprintf(tlab,         sizeof tlab,         "%s_t", tmplab);
                snprintf(plab,         sizeof plab,         "%s_p", tmplab);
                snprintf(saved,        sizeof saved,        "spn%d_saved", next_uid());
                snprintf(dispatch_lbl, sizeof dispatch_lbl, "%s_dispatch", tmplab);
                var_register(saved); flat_bss_register(tlab); flat_bss_register(plab);
                ALF(α, "; SPAN(expr) α — eval arg then dispatch\n");
                emit_expr(arg, -32);
                A("    mov     [rel %s], rax\n", tlab);
                A("    mov     [rel %s], rdx\n", plab);
                ALFC(dispatch_lbl, "SPAN(expr) α dispatch",
                     "SPAN_α_PTR  %s, %s, %s, %s, %s, %s, %s, %s\n",
                     tlab, plab, bref(saved), cursor, subj, subj_len, γ, ω);
                ALFC(β, "SPAN(expr) β",
                     "SPAN_β_PTR   %s, %s, %s\n", bref(saved), cursor, ω);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "BREAK") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_break_var(str_intern(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else if (arg->kind == E_QLIT && arg->sval) {
                emit_break(arg->sval, (int)strlen(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else {
                /* Runtime expression (e.g. E_SEQ/E_CONCAT of vars like sq dq):
                 * Evaluate into BSS temp slots, dispatch via BREAK_α_PTR. */
                char tmplab[LBUF];
                snprintf(tmplab, LBUF, "brk_expr_tmp_%d", next_uid());
                char tlab[LBUF], plab[LBUF], saved[LBUF], dispatch_lbl[LBUF];
                snprintf(tlab,         sizeof tlab,         "%s_t", tmplab);
                snprintf(plab,         sizeof plab,         "%s_p", tmplab);
                snprintf(saved,        sizeof saved,        "brk%d_saved", next_uid());
                snprintf(dispatch_lbl, sizeof dispatch_lbl, "%s_dispatch", tmplab);
                var_register(saved);
                flat_bss_register(tlab);
                flat_bss_register(plab);
                ALF(α, "; BREAK(expr) α — eval arg then dispatch\n");
                emit_expr(arg, -32);
                A("    mov     [rel %s], rax\n", tlab);
                A("    mov     [rel %s], rdx\n", plab);
                ALFC(dispatch_lbl, "BREAK(expr) α dispatch",
                     "BREAK_α_PTR %s, %s, %s, %s, %s, %s, %s, %s\n",
                     tlab, plab, bref(saved),
                     cursor, subj, subj_len, γ, ω);
                ALFC(β, "BREAK(expr) β",
                     "BREAK_β_PTR  %s, %s, %s\n",
                     bref(saved), cursor, ω);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "BREAKX") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            if (arg->kind == E_VART && arg->sval) {
                emit_breakx_var(str_intern(arg->sval), α, β, γ, ω, cursor, subj, subj_len);
            } else {
                const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
                int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
                emit_breakx_lit(cs, cslen, α, β, γ, ω, cursor, subj, subj_len);
            }
        } else if (pat->sval && strcasecmp(pat->sval, "LEN") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_len(n, α, β, γ, ω, cursor, subj_len);
        } else if (pat->sval && strcasecmp(pat->sval, "TAB") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_tab(n, α, β, γ, ω, cursor);
        } else if (pat->sval && strcasecmp(pat->sval, "RTAB") == 0 && pat->nchildren == 1) {
            EXPR_t *arg = pat->children[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_rtab(n, α, β, γ, ω, cursor, subj_len);
        } else if (pat->sval && strcasecmp(pat->sval, "REM") == 0 && pat->nchildren == 0) {
            emit_rem(α, β, γ, ω, cursor, subj_len);
        } else if (pat->sval && strcasecmp(pat->sval, "ARB") == 0 && pat->nchildren == 0) {
            emit_arb(α, β, γ, ω, cursor, subj_len);
        } else if (pat->sval && strcasecmp(pat->sval, "FAIL") == 0) {
            /* FAIL always fails — α and β both jump to ω */
            A("\n; FAIL  α=%s\n", α);
            asmL(α);
            A("    jmp     %s\n", ω);
            ALF(β, "jmp     %s\n", ω);
        } else {
            /* Unknown function in pattern position — evaluate at runtime.
             * Strategy: call the function (ucall or builtin), store DESCR_t
             * result in two .bss qwords, then dispatch via CALL_PAT_α which
             * calls stmt_match_descr (handles DT_P pattern or string literal).
             * Fix: B-263 icase('hello') in pattern position. */
            int cuid2 = next_uid();
            char dt_slot[64], dp_slot[64], saved_slot[64];
            snprintf(dt_slot,    sizeof dt_slot,    "cpat%d_t", cuid2);
            snprintf(dp_slot,    sizeof dp_slot,    "cpat%d_p", cuid2);
            snprintf(saved_slot, sizeof saved_slot, "cpat%d_saved", cuid2);
            flat_bss_register(dt_slot);
            flat_bss_register(dp_slot);
            flat_bss_register(saved_slot);

            /* Emit the function call evaluation BEFORE α (at stmt init time).
             * We use a pre-eval label so the call happens once when first entered.
             * For the pattern match scan loop, re-evaluate each attempt by
             * inlining the call at α entry. */
            A("\n; CALL_PAT %s() — runtime pattern/string dispatch\n",
              pat->sval ? pat->sval : "?");

            /* α: evaluate call, store result, then match */
            asmL(α);
            /* Evaluate the function call into [rbp-32/24] */
            emit_expr(pat, -32);    /* re-uses existing emit_expr for E_FNC */
            /* Store result into .bss slots */
            A("    mov     [%s], rax\n", dt_slot);
            A("    mov     [%s], rdx\n", dp_slot);
            /* Match via stmt_match_descr */
            A("    CALL_PAT_α %s, %s, %s, %s, %s, %s\n",
              dt_slot, dp_slot, saved_slot, cursor, γ, ω);

            /* β: restore cursor and go to ω */
            ALFC(β, "CALL_PAT β", "CALL_PAT_β %s, %s, %s\n",
                 saved_slot, cursor, ω);
        }
        break;

    case E_ATP: {
        /* @VAR — cursor-position capture: store cursor as integer into VAR.
         *
         * Two forms:
         *   Unary:  @x     — nchildren==1, children[0] = E_VART("x")
         *                    Always succeeds. AT_α stores cursor into x.
         *   Binary: pat @x — nchildren==2, children[0]=sub-pat, children[1]=E_VART("x")
         *                    Wire child; on child γ store cursor into x, then outer γ.
         *                    On child ω propagate ω (no capture).
         */
        if (pat->nchildren >= 2) {
            /* Binary: sub-pattern @x */
            const char *varname = (pat->children[1] && pat->children[1]->sval)
                                  ? pat->children[1]->sval : "";
            const char *varlab  = str_intern(varname);
            int uid = next_uid();
            char cα[LBUF], cβ[LBUF], cap_γ[LBUF], cap_ω[LBUF];
            snprintf(cα,    LBUF, "at%d_child_α", uid);
            snprintf(cβ,    LBUF, "at%d_child_β",  uid);
            snprintf(cap_γ, LBUF, "at%d_γ",        uid);
            snprintf(cap_ω, LBUF, "at%d_ω",        uid);

            char hdr[LBUF];
            snprintf(hdr, LBUF, "AT(%s @ %s)", varname, varname);
            asmLC(α, hdr);
            A("jmp     %s\n", cα);
            ALFC(β, "AT β", "jmp     %s\n", cβ);

            /* Emit child sub-pattern */
            emit_pat_node(pat->children[0], cα, cβ, cap_γ, cap_ω,
                          cursor, subj, subj_len, depth + 1);

            /* cap_γ: child succeeded — store cursor as integer into varname */
            ALFC(cap_γ, "AT γ — store cursor",
                 "AT_α        %s, %s, %s, %s\n", varlab, cursor, γ, ω);

            /* cap_ω: child failed */
            ALFC(cap_ω, "AT ω — child failed", "jmp     %s\n", ω);
        } else {
            /* Unary: @x — store cursor immediately, always succeed */
            const char *varname = (pat->children[0] && pat->children[0]->sval)
                                  ? pat->children[0]->sval
                                  : (pat->sval ? pat->sval : "");
            const char *varlab  = str_intern(varname);
            ALFC(α, "@VAR α", "AT_α        %s, %s, %s, %s\n",
                 varlab, cursor, γ, ω);
            ALFC(β,  "@VAR β", "AT_β         %s\n", ω);
        }
        break;
    }

    default:
        A("\n; UNIMPLEMENTED node kind %d → ω\n", pat->kind);
        asmL(α);
        ALF(β, "jmp     %s\n", ω);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Extra .bss entries for ARBNO stacks (resq 64 each)
 * ----------------------------------------------------------------------- */

char extra_slots[512][LBUF + 16];
int  extra_slot_count = 0;

static void extra_bss_emit(void) {
    for (int i = 0; i < extra_slot_count; i++)
        A("%s\n", extra_slots[i]);
}

/* Per-call-uid .bss slots — declared here, emitted as part of .bss section */
/* Heap-allocated to avoid ~1.3MB BSS segment */
#define CALL_SLOTS_MAX 4096
static char (*call_slots)[LBUF] = NULL;
static int   call_slot_count = 0;
static void call_slots_init(void) {
    if (!call_slots)
        call_slots = (char (*)[LBUF])calloc(CALL_SLOTS_MAX, LBUF);
}

static void call_slot_add(const char *name) {
    if (call_slot_count >= CALL_SLOTS_MAX) return;
    /* deduplicate */
    for (int i = 0; i < call_slot_count; i++)
        if (strcmp(call_slots[i], name) == 0) return;
    snprintf(call_slots[call_slot_count++], LBUF, "%s", name);
}

static void call_slot_emit(void) {
    for (int i = 0; i < call_slot_count; i++)
        A("%-24s resq 1\n", call_slots[i]);
}

static void prescan_ucall_expr(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_FNC && e->sval) {
        const NamedPat *ufn = named_pat_lookup_fn(e->sval);
        if (ufn) {
            int uid = call_uid();
            int na = e->nchildren;
            int actual_args = (na < ufn->nparams) ? na : ufn->nparams;
            for (int ai = 0; ai < actual_args; ai++) {
                char sv_t[LBUF], sv_p[LBUF];
                snprintf(sv_t, LBUF, "ucall%d_sv_%d_t", uid, ai);
                snprintf(sv_p, LBUF, "ucall%d_sv_%d_p", uid, ai);
                call_slot_add(sv_t); call_slot_add(sv_p);
            }
            char rsv_g[LBUF], rsv_o[LBUF];
            snprintf(rsv_g, LBUF, "ucall%d_rsv_g", uid);
            snprintf(rsv_o, LBUF, "ucall%d_rsv_o", uid);
            call_slot_add(rsv_g); call_slot_add(rsv_o);
            /* recurse into args */
            for (int i = 0; i < e->nchildren; i++)
                prescan_ucall_expr(e->children[i]);
            return;
        }
    }

    for (int i = 0; i < e->nchildren; i++)
        prescan_ucall_expr(e->children[i]);
}

static void prescan_ucall(Program *prog) {
    call_uid_counter = 0;
    call_slot_count = 0;
    if (!prog) { call_uid_counter = 0; return; }
    for (STMT_t *s = prog->head; s; s = s->next) {
        prescan_ucall_expr(s->subject);
        prescan_ucall_expr(s->pattern);
        prescan_ucall_expr(s->replacement);
    }
    call_uid_counter = 0;  /* reset for real emission pass */
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
    expand_name(varname, cv->safe, sizeof cv->safe);
    if (!cv->safe[0]) { cv->safe[0] = 'v'; cv->safe[1] = '\0'; }
    /* copy safe to temp to avoid GCC -Wrestrict false-positive (distinct fields) */
    char _safe_tmp[sizeof cv->safe];
    memcpy(_safe_tmp, cv->safe, sizeof cv->safe);
    snprintf(cv->buf_sym, sizeof cv->buf_sym, "cap_%s_buf", _safe_tmp);
    snprintf(cv->len_sym, sizeof cv->len_sym, "cap_%s_len", _safe_tmp);
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
 *   pat_NAME_α  — initial match entry
 *   pat_NAME_β   — backtrack entry
 * and two .bss continuation slots:
 *   pat_NAME_ret_γ  — caller fills with γ address before jmp
 *   pat_NAME_ret_ω  — caller fills with ω address before jmp
 * (NamedPat struct defined above as forward decl)
 * ----------------------------------------------------------------------- */

#define NAMED_PAT_MAX 512

/* Heap-allocated to avoid ~1.9MB BSS segment */
static NamedPat *named_pats = NULL;
static int        named_pat_count = 0;
static void named_pats_init(void) {
    if (!named_pats)
        named_pats = (NamedPat *)calloc(NAMED_PAT_MAX, sizeof(NamedPat));
}

static void named_pat_reset(void);  /* forward decl — defined below after nreturn_fns */

/* nreturn_fns — set of function names that use :(NRETURN).
 * Populated during scan_named_patterns. Used at fast-path guard to ensure
 * CALL1_* is not used for NRETURN functions even when ufn lookup misses. */
#define NRETURN_FNS_MAX 256
static char nreturn_fns[NRETURN_FNS_MAX][64];
static int  nreturn_fn_count = 0;
static void nreturn_fns_reset(void) { nreturn_fn_count = 0; }
static void nreturn_fns_add(const char *name) {
    for (int i = 0; i < nreturn_fn_count; i++)
        if (strcasecmp(nreturn_fns[i], name) == 0) return;
    if (nreturn_fn_count < NRETURN_FNS_MAX)
        snprintf(nreturn_fns[nreturn_fn_count++], 64, "%s", name);
}
static int nreturn_fns_has(const char *name) {
    for (int i = 0; i < nreturn_fn_count; i++)
        if (strcasecmp(nreturn_fns[i], name) == 0) return 1;
    return 0;
}

static void named_pat_reset(void) { named_pat_count = 0; nreturn_fns_reset(); }
/* str_vars — registry for plain-string variable assignments (VAR = 'literal').
 * Used by E_INDR (*VAR) when the variable holds a string, not a named pattern.
 * At compile time we know the literal value, so we emit an inline LIT. */
#define STR_VARS_MAX 256
static StrVar str_vars[STR_VARS_MAX];
static int       str_vars_count = 0;

static void str_vars_reset(void) { str_vars_count = 0; }

static void str_var_register(const char *varname, const char *sval) {
    for (int i = 0; i < str_vars_count; i++)
        if (strcasecmp(str_vars[i].varname, varname) == 0) {
            str_vars[i].sval = sval; return;
        }
    if (str_vars_count >= STR_VARS_MAX) return;
    StrVar *e = &str_vars[str_vars_count++];
    snprintf(e->varname, NAME_LEN, "%s", varname);
    e->sval = sval;
}

static const StrVar *str_var_lookup(const char *varname) {
    for (int i = 0; i < str_vars_count; i++)
        if (strcasecmp(str_vars[i].varname, varname) == 0)
            return &str_vars[i];
    return NULL;
}

/* Make a label-safe version of a SNOBOL4 variable name — use expansion table */
static void safe_name(const char *src, char *dst, int dstlen) {
    expand_name(src, dst, dstlen);
    if (!dst[0]) { dst[0] = 'v'; dst[1] = '\0'; }
}

/* Register a named pattern — called during program scan */
static NamedPat *named_pat_register(const char *varname, EXPR_t *pat) {
    /* dedup */
    for (int i = 0; i < named_pat_count; i++)
        if (strcmp(named_pats[i].varname, varname) == 0) {
            if (pat) named_pats[i].pat = pat; /* update expr if provided */
            return &named_pats[i];
        }
    if (named_pat_count >= NAMED_PAT_MAX) return NULL;
    NamedPat *e = &named_pats[named_pat_count++];
    snprintf(e->varname, NAME_LEN, "%s", varname);
    safe_name(varname, e->safe, NAME_LEN);
    /* copy safe to temp to avoid GCC -Wrestrict false-positive (distinct fields) */
    char _ns[NAME_LEN];
    memcpy(_ns, e->safe, NAME_LEN);
    snprintf(e->α_lbl, sizeof e->α_lbl, "P_%s_α", _ns);
    snprintf(e->β_lbl,  sizeof e->β_lbl,  "P_%s_β", _ns);
    snprintf(e->ret_γ, sizeof e->ret_γ, "P_%s_ret_γ", _ns);
    snprintf(e->ret_ω, sizeof e->ret_ω, "P_%s_ret_ω", _ns);
    e->pat = pat;
    e->is_fn = 0;
    e->nparams = 0;
    e->nlocals = 0;
    e->body_label[0] = '\0';
    e->uses_nreturn = 0;
    e->body_end_idx = -1;
    e->end_label[0] = '\0';
    memset(e->param_names, 0, sizeof e->param_names);
    memset(e->local_names, 0, sizeof e->local_names);
    return e;
}

static const NamedPat *named_pat_lookup(const char *varname) {
    for (int i = 0; i < named_pat_count; i++)
        if (strcasecmp(named_pats[i].varname, varname) == 0)
            return &named_pats[i];
    return NULL;
}

static const NamedPat *named_pat_lookup_fn(const char *varname) {
    for (int i = 0; i < named_pat_count; i++)
        if (named_pats[i].is_fn && strcasecmp(named_pats[i].varname, varname) == 0)
            return &named_pats[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * emit_named_ref — call site for a named pattern reference (E_VART)
 *
 * Emits:
 *   α:  store γ → pat_NAME_ret_γ; store ω → pat_NAME_ret_ω
 *           jmp pat_NAME_α
 *   β:   store γ → pat_NAME_ret_γ; store ω → pat_NAME_ret_ω
 *           jmp pat_NAME_β
 * ----------------------------------------------------------------------- */

static void emit_named_ref(const NamedPat *np,
                                const char *α, const char *β,
                                const char *γ, const char *ω) {
    int uid = next_uid();
    char glbl[LBUF], olbl[LBUF], r12_save[LBUF];
    snprintf(glbl,     LBUF, "nref%d_γ",    uid);
    snprintf(olbl,     LBUF, "nref%d_ω",    uid);
    snprintf(r12_save, LBUF, "nref%d_r12",  uid);

    /* Save slot for caller's r12 — must survive across the nested call.
     * Use flat_bss_register (not var_register) so the slot always lands in
     * the flat .bss section, not inside a named-pattern DATA block. */
    if (!np->is_fn)
        flat_bss_register(r12_save);

    {
        char ref_hdr[LBUF + 8];
        snprintf(ref_hdr, sizeof ref_hdr, "REF(%s)", np->varname);
        A("\n");
        asmLC(α, ref_hdr);
    }

    /* α: save caller r12, set r12 to callee DATA block, jump to α */
    A("    lea     rax, [rel %s]\n", glbl);
    A("    mov     [%s], rax\n", np->ret_γ);
    A("    lea     rax, [rel %s]\n", olbl);
    A("    mov     [%s], rax\n", np->ret_ω);
    if (!np->is_fn) {
        A("    mov     [%s], r12\n", r12_save);   /* save caller r12 */
        A("    lea     r12, [rel box_%s_data_template]\n", np->safe);
    }
    A("    jmp     %s\n", np->α_lbl);

    /* β: same save/restore, jump to β */
    ALFC(β, "REF(%s)", "lea     rax, [rel %s]\n", glbl);
    A("    mov     [%s], rax\n", np->ret_γ);
    A("    lea     rax, [rel %s]\n", olbl);
    A("    mov     [%s], rax\n", np->ret_ω);
    if (!np->is_fn) {
        A("    mov     [%s], r12\n", r12_save);   /* save caller r12 */
        A("    lea     r12, [rel box_%s_data_template]\n", np->safe);
    }
    A("    jmp     %s\n", np->β_lbl);

    /* γ trampoline: restore caller r12, then forward to caller γ */
    A("\n%s:\n", glbl);
    if (!np->is_fn)
        A("    mov     r12, [%s]\n", r12_save);   /* restore caller r12 */
    asmJ(γ);

    /* ω trampoline: restore caller r12, then forward to caller ω */
    asmL(olbl);
    if (!np->is_fn)
        A("    mov     r12, [%s]\n", r12_save);   /* restore caller r12 */
    asmJ(ω);
}

/* -----------------------------------------------------------------------
 * emit_named_def — emit the body of a named pattern
 *
 * Emits:
 *   pat_NAME_α:  <recursive Byrd box for the pattern expression>
 *                    (inner γ → jmp [pat_NAME_ret_γ])
 *                    (inner ω → jmp [pat_NAME_ret_ω])
 *   pat_NAME_β:   <same, backtrack entry>
 * ----------------------------------------------------------------------- */

static void emit_named_def(const NamedPat *np,
                                const char *cursor,
                                const char *subj,
                                const char *subj_len) {
    /* User-defined function (Snocone procedure): is_fn=1
     * α port: save old param var values → param_save slots,
     *         load arg descriptors from call-site stack → param vars,
     *         jump to function body label.
     * γ port: restore param vars from save slots, jmp [ret_γ].
     * ω port: same restore, jmp [ret_ω] (for FRETURN path).
     * The RETURN inside the body is already routed to jmp [ret_γ]
     * by emit_jmp/prog_emit_goto when cur_fn != NULL. */
    if (np->is_fn) {
        char safe[NAME_LEN];
        snprintf(safe, sizeof safe, "%s", np->safe);

        /* Establish per-invocation DATA block context.  All var_register calls below
         * route to the per-box DATA template instead of flat .bss. */
        box_ctx_begin(safe);

        /* Reserve first 48 bytes of DATA as DESCR_t scratch slots:
         *   [r12+0 / r12+8]   = tmp1_t / tmp1_p  (was [rbp-16] / [rbp-8])
         *   [r12+16/ r12+24]  = tmp2_t / tmp2_p  (was [rbp-32] / [rbp-24])
         *   [r12+32/ r12+40]  = tmp3_t / tmp3_p  (was [rbp-48] / [rbp-40])
         * These are the three DESCR_t temporaries that the near-term bridge
         * allocated with sub rsp, 56. */
        char t2_tmp1_t[NAME_LEN], t2_tmp1_p[NAME_LEN];
        char t2_tmp2_t[NAME_LEN], t2_tmp2_p[NAME_LEN];
        char t2_tmp3_t[NAME_LEN], t2_tmp3_p[NAME_LEN];
        snprintf(t2_tmp1_t, sizeof t2_tmp1_t, "fn_%s_tmp1_t", safe);
        snprintf(t2_tmp1_p, sizeof t2_tmp1_p, "fn_%s_tmp1_p", safe);
        snprintf(t2_tmp2_t, sizeof t2_tmp2_t, "fn_%s_tmp2_t", safe);
        snprintf(t2_tmp2_p, sizeof t2_tmp2_p, "fn_%s_tmp2_p", safe);
        snprintf(t2_tmp3_t, sizeof t2_tmp3_t, "fn_%s_tmp3_t", safe);
        snprintf(t2_tmp3_p, sizeof t2_tmp3_p, "fn_%s_tmp3_p", safe);
        var_register(t2_tmp1_t); /* offset 0  — was [rbp-16] */
        var_register(t2_tmp1_p); /* offset 8  — was [rbp-8]  */
        var_register(t2_tmp2_t); /* offset 16 — was [rbp-32] */
        var_register(t2_tmp2_p); /* offset 24 — was [rbp-24] */
        var_register(t2_tmp3_t); /* offset 32 — was [rbp-48] */
        var_register(t2_tmp3_p); /* offset 40 — was [rbp-40] */

        emit_sep_major(np->varname);
        A("; %s — user function α entry (%d param%s) [r12=DATA block]\n",
          np->α_lbl, np->nparams, np->nparams==1?"":"s");

        /* α entry: r12 points to this invocation's DATA block.
         * M-T2-INVOKE will emit blk_alloc+memcpy+mov r12,new_data at call sites.
         * Until then: self-initialize r12 to the static DATA template so that
         * single-invocation (non-recursive) calls work correctly now.
         * Recursive calls will corrupt the static template — fixed by M-T2-INVOKE.
         *
         * The near-term bridge (push rbp / sub rsp, 56) is REMOVED.
         * Scratch DESCR_t slots are now [r12+0/8], [r12+16/24], [r12+32/40]. */
        asmL(np->α_lbl);
        A("    FN_α_INIT %s\n", safe);

        /* Load args from .bss arg slots into param variables */
        for (int i = 0; i < np->nparams; i++) {
            char arg_slot_t[LBUF2 + 16], arg_slot_p[LBUF2 + 16];
            snprintf(arg_slot_t, sizeof arg_slot_t, "fn_%s_arg_%d_t", safe, i);
            snprintf(arg_slot_p, sizeof arg_slot_p, "fn_%s_arg_%d_p", safe, i);
            const char *plab = str_intern(np->param_names[i]);
            A("    mov     rsi, [%s]\n", arg_slot_t);
            A("    mov     rdx, [%s]\n", arg_slot_p);
            A("    FN_SET_PARAM %s\n", plab);
        }
        /* Clear locals + retval to NULVCL */
        A("    LOAD_NULVCL\n");
        A("    mov     [%s], rax\n", bref(t2_tmp1_t));
        A("    mov     [%s], rdx\n", bref(t2_tmp1_p));
        {
            /* Clear return-value variable ONLY if it is not also a parameter.
             * When fn name == param name (e.g. DEFINE('lwr(lwr)')) the
             * FN_SET_PARAM above already initialised it; clearing here
             * would discard the caller's argument.  Fix: B-263. */
            const char *fnlab_clr = str_intern(np->varname);
            int is_param = 0;
            for (int pi = 0; pi < np->nparams; pi++) {
                if (strcasecmp(np->param_names[pi], np->varname) == 0) {
                    is_param = 1; break;
                }
            }
            if (!is_param) {
                A("    mov     rsi, [%s]\n", bref(t2_tmp1_t));
                A("    mov     rdx, [%s]\n", bref(t2_tmp1_p));
                A("    FN_CLEAR_VAR %s\n", fnlab_clr);
            }
        }
        for (int i = 0; i < np->nlocals; i++) {
            const char *llab = str_intern(np->local_names[i]);
            A("    mov     rsi, [%s]\n", bref(t2_tmp1_t));
            A("    mov     rdx, [%s]\n", bref(t2_tmp1_p));
            A("    FN_CLEAR_VAR %s\n", llab);
        }
        A("    jmp     %s\n", label_nasm(np->body_label));

        emit_sep_minor("γ/ω");

        /* γ — return side (success / RETURN).
         * No frame to tear down (stack-frame bridge removed).
         * Caller's r12 is restored by M-T2-INVOKE after we return.
         * (M-T2-INVOKE will emit blk_free + restore caller r12 at the return
         *  labels.  For now we just dispatch via the ret slot.) */
        char γ_lbl[LBUF2], ω_lbl[LBUF2];
        snprintf(γ_lbl, sizeof γ_lbl, "fn_%s_γ", safe);
        snprintf(ω_lbl, sizeof ω_lbl, "fn_%s_ω", safe);
        ALFC(γ_lbl, "fn γ", "FN_γ %s\n", np->ret_γ);
        ALFC(ω_lbl, "fn ω", "FN_ω %s\n", np->ret_ω);
        /* β — backtrack into function call = failure; standalone stub after body */
        ALFC(np->β_lbl, "fn β — backtrack = fail", "jmp     [%s]\n", np->ret_ω);
        box_ctx_end();
        return;
    }

    if (!np->pat) {
        /* No pattern expression — emit stubs that always fail */
        A("\n; Named pattern %s — no expression, always fails\n", np->varname);
        asmL(np->α_lbl);
        ALF(np->β_lbl, "jmp     [%s]\n", np->ret_ω);
        return;
    }

    /* --- Non-function named pattern box --- */

    /* T2: register ret_γ / ret_ω in per-box DATA layout (reserved for M-T2-INVOKE).
     * They remain in .bss too — call sites and trampolines use .bss for now. */
    box_ctx_begin(np->safe);
    var_register(np->ret_γ);
    var_register(np->ret_ω);

    /* The named pattern's inner γ and ω connect back via the ret_ slots */
    char inner_γ[LBUF2], inner_ω[LBUF2];
    snprintf(inner_γ, sizeof inner_γ, "patdef_%s_γ", np->safe);
    snprintf(inner_ω, sizeof inner_ω, "patdef_%s_ω", np->safe);

    /* Major separator: named pattern header */
    emit_sep_major(np->varname);

    /* α entry — initial match.
     * B-288: zero all mutable DATA block slots (offset ≥16) so ARBNO depth/
     * state from a previous scan attempt doesn't corrupt re-entry.  Slots 0/8
     * are ret_γ/ret_ω, written by the call site — don't touch them.
     * (M-T2-INVOKE will replace this with blk_alloc+memcpy per-invocation.) */
    A("; %s (α entry) [r12=DATA block]\n", np->α_lbl);
    {
        int dsz = box_data_size(np->safe);
        if (dsz > 16) {
            /* Emit α label + slot zeroing, then redirect emit_pat_node to a
             * fall-through stub label so the pattern body follows immediately
             * without re-emitting P_<name>_α. */
            asmL(np->α_lbl);
            for (int off = 16; off < dsz; off += 8)
                A("    mov     qword [r12+%d], 0\n", off);
            char after_reset[LBUF2];
            snprintf(after_reset, sizeof after_reset, "pat_%s_body", np->safe);
            NamedPat *np_mut = (NamedPat *)np;
            char saved_α[sizeof np_mut->α_lbl];
            memcpy(saved_α, np_mut->α_lbl, sizeof saved_α);
            snprintf(np_mut->α_lbl, sizeof np_mut->α_lbl, "%s", after_reset);
            emit_pat_node(np->pat,
                          np_mut->α_lbl, np->β_lbl,
                          inner_γ, inner_ω,
                          cursor, subj, subj_len,
                          1);
            memcpy(np_mut->α_lbl, saved_α, sizeof saved_α);
        } else {
            emit_pat_node(np->pat,
                          np->α_lbl, np->β_lbl,
                          inner_γ, inner_ω,
                          cursor, subj, subj_len,
                          1);
        }
    }
    /* Minor separator before γ/ω trampolines */
    emit_sep_minor("γ/ω");

    /* γ/ω trampolines */
    ALFC(inner_γ, "named pat γ", "NAMED_PAT_γ %s\n", np->ret_γ);
    ALFC(inner_ω, "named pat ω", "NAMED_PAT_ω %s\n", np->ret_ω);

    box_ctx_end();
}

/* -----------------------------------------------------------------------
 * emit_null_program — Sprint A0 fallback
 * ----------------------------------------------------------------------- */

/* Forward declaration — defined after emit_program() */
static void emit_blk_reloc_tables(void);

static void emit_null_program(void) {
    A("; generated by scrip-cc -asm\n");
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
 * pattern-building expression (contains E_FNC, E_OR, or E_SEQ with
 * at least one non-literal/non-value child).
 * Pure value assignments (E_VART, E_QLIT, E_ILIT alone, or
 * E_CONCAT/E_OR where ALL leaves are literals or variable refs with no
 * pattern-function calls) are NOT named patterns in program context. */
static int expr_has_pattern_fn(EXPR_t *e) {
    if (!e) return 0;
    /* E_FNC nodes are pattern exprs only if the function name is a known
     * pattern-building builtin.  Value functions like REPLACE/SIZE/DIFFER
     * are NOT pattern builders — they return strings/integers, not patterns. */
    if (e->kind == E_FNC) {
        static const char *pat_fns[] = {
            "ARBNO","ANY","NOTANY","SPAN","BREAK","BREAKX",
            "LEN","POS","RPOS","TAB","RTAB","REM","ARB",
            "FAIL","SUCCEED","FENCE","ABORT","BAL", NULL };
        if (e->sval)
            for (int i = 0; pat_fns[i]; i++)
                if (strcasecmp(e->sval, pat_fns[i]) == 0) return 1;
        return 0;
    }
    /* E_NAM (. capture) and E_DOL ($ capture) wrap a pattern child */
    if (e->kind == E_NAM || e->kind == E_DOL) return 1;
    /* E_ATP (@ cursor capture) is always a pattern element:
     * unary @x or binary pat @x — both produce pattern values.
     * B-276: M-BEAUTY-OMEGA fix — without this, TZ = LE(xTrace,0) pat @txOfs
     * was not recognised as a pattern expression and fell into OPSYN dispatch. */
    if (e->kind == E_ATP) return 1;
    /* E_STAR (*VAR deferred pattern ref) is always a pattern-valued expression */
    if (e->kind == E_STAR) return 1;
    for (int i = 0; i < e->nchildren; i++)
        if (expr_has_pattern_fn(e->children[i])) return 1;
    return 0;
}
static int expr_is_pattern_expr(EXPR_t *e) {
    if (!e) return 0;
    /* E_OR (alternation) is a named-pattern expression only when at least one
     * child contains a compile-time pattern function (ANY, LEN, etc.).
     * If both sides are pure value expressions (e.g. upr('h') | lwr('H')),
     * the result is a runtime pattern VALUE — treat as value assignment so
     * the match 'subj' p uses XDSAR (runtime deref), not a broken static box.
     * Fix: B-263 icase. */
    if (e->kind == E_OR) return expr_has_pattern_fn(e);
    /* E_SEQ = goal-directed sequence — always a pattern expression.
     * E_CONCAT = pure value concat — never a pattern expression. */
    if (e->kind == E_SEQ) return 1;
    if (e->kind == E_CONCAT) return 0;
    /* E_ATP (@var cursor capture) is always a pattern expression:
     * unary @x or binary pat @x — both yield a pattern value. */
    if (e->kind == E_ATP) return 1;
    /* Any function call is a pattern expression */
    return expr_has_pattern_fn(e);
}

/* scan_named_patterns — walk the program and register all
 * pattern assignments (VAR = <pattern-expr>) before emitting.
 * Also detects Snocone sc_cf-generated DEFINE stmts and registers
 * user-defined procedures as is_fn=1 entries.
 * ----------------------------------------------------------------------- */

/* Parse "fname(arg1,arg2,...)" from a DEFINE string literal.
 * Writes function name into fname_out (fname_max bytes),
 * param names into params[][] (nparams_out count),
 * local names into locals[][] (nlocals_out count).
 * SNOBOL4 prototype: 'fname(p1,p2)l1,l2'  — params inside (), locals after ')'.
 * Returns 1 on success, 0 on parse failure. */

/* Flatten an EXPR_t tree of E_CONCAT / E_QLIT nodes into a single string.
 * Handles multi-line DEFINE specs built by continuation (+) lines, e.g.:
 *   DEFINE('Read(fileName,rdMapName)'
 *   +      'rdInput,rdIn,...')
 * which parses as E_CONCAT(E_QLIT("Read(fileName,rdMapName)"), E_QLIT("rdInput,...")).
 * Writes into buf[bufsz]. Returns buf on success, NULL on truncation/failure. */
static const char *expr_flatten_str(const EXPR_t *e, char *buf, size_t bufsz) {
    if (!e || bufsz == 0) return NULL;
    if (e->kind == E_QLIT) {
        if (!e->sval) { buf[0] = '\0'; return buf; }
        size_t len = strlen(e->sval);
        if (len >= bufsz) return NULL;  /* truncation */
        memcpy(buf, e->sval, len + 1);
        return buf;
    }
    if (e->kind == E_CONCAT || e->kind == E_SEQ) {
        /* Left-fold: flatten left child first, then append right */
        char *pos = buf;
        size_t rem = bufsz;
        /* Iterative in-order traversal — collect all E_QLIT leaves */
        const EXPR_t *leaves[256];
        int nleaves = 0;
        /* Iterative in-order via explicit stack */
        const EXPR_t *stk2[128];
        int t2 = 0;
        stk2[t2++] = e;
        while (t2 > 0 && nleaves < 255) {
            const EXPR_t *cur = stk2[--t2];
            if (!cur) continue;
            if (cur->kind == E_QLIT) {
                leaves[nleaves++] = cur;
            } else if (cur->kind == E_CONCAT || cur->kind == E_SEQ) {
                /* Push right then left so left is processed first */
                if (cur->nchildren >= 2 && cur->children[1]) stk2[t2++] = cur->children[1];
                if (cur->nchildren >= 1 && cur->children[0]) stk2[t2++] = cur->children[0];
            }
        }
        for (int i = 0; i < nleaves; i++) {
            const char *s = leaves[i]->sval ? leaves[i]->sval : "";
            size_t slen = strlen(s);
            if (slen >= rem) return NULL;  /* truncation */
            memcpy(pos, s, slen);
            pos += slen;
            rem -= slen;
        }
        *pos = '\0';
        return buf;
    }
    return NULL;  /* unsupported expr kind */
}

static int parse_define_str(const char *def,
                             char *fname_out, int fname_max,
                             char params[MAX_PARAMS][64],
                             int *nparams_out,
                             char locals[MAX_PARAMS][64],
                             int *nlocals_out) {
    *nparams_out = 0;
    *nlocals_out = 0;
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
            if (*nparams_out >= MAX_PARAMS) break;
        }
        if (*p == ',') p++;
    }
    if (*p == ')') p++; /* skip ')' */
    /* Parse locals: comma-separated names after ')' */
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        int li = 0;
        while (*p && *p != ',' && li < 63)
            locals[*nlocals_out][li++] = *p++;
        while (li > 0 && (locals[*nlocals_out][li-1]==' '||
                           locals[*nlocals_out][li-1]=='\t')) li--;
        locals[*nlocals_out][li] = '\0';
        if (li > 0) {
            (*nlocals_out)++;
            if (*nlocals_out >= MAX_PARAMS) break;
        }
    }
    return 1;
}

static void scan_named_patterns(Program *prog) {
    named_pat_reset();
    str_vars_reset();
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* Detect Snocone sc_cf DEFINE statement:
         * subject = E_FNC("DEFINE", E_QLIT("fname(args)"))
         * has_eq = 0, no pattern.
         * Register as user function (is_fn=1). */
        if (s->subject && s->subject->kind == E_FNC &&
            s->subject->sval && strcasecmp(s->subject->sval, "DEFINE") == 0 &&
            s->subject->nchildren >= 1 &&
            s->subject->children[0] &&
            (s->subject->children[0]->kind == E_QLIT ||
             s->subject->children[0]->kind == E_CONCAT)) {
            /* B-275: Handle multi-line DEFINE specs built via continuation (+) lines.
             * Single-line: DEFINE('fname(p)loc')  → children[0] is E_QLIT.
             * Multi-line:  DEFINE('fname(p)'      → children[0] is E_CONCAT of E_QLITs.
             *              +      'loc1,loc2')
             * expr_flatten_str() handles both cases by DFS-collecting E_QLIT leaves. */
            char def_buf[1024];
            const char *def_str = expr_flatten_str(s->subject->children[0],
                                                    def_buf, sizeof def_buf);
            if (!def_str) continue;  /* truncation or unsupported shape — skip */
            char fname[128];
            char params[MAX_PARAMS][64];
            int nparams = 0;
            char locals[MAX_PARAMS][64];
            int nlocals = 0;
            if (parse_define_str(def_str, fname, sizeof fname, params, &nparams, locals, &nlocals)) {
                NamedPat *e = named_pat_register(fname, NULL);
                if (e) {
                    e->is_fn = 1;
                    e->nparams = nparams;
                    for (int i = 0; i < nparams; i++)
                        snprintf(e->param_names[i], 64, "%s", params[i]);
                    e->nlocals = nlocals;
                    for (int i = 0; i < nlocals; i++)
                        snprintf(e->local_names[i], 64, "%s", locals[i]);
                    /* Default body_label = fname; overridden by 2nd arg if present */
                    snprintf(e->body_label, sizeof e->body_label, "%s", fname);
                    /* Two-arg DEFINE('fname(args)', .entryLabel):
                     * args[1] is the explicit entry label (E_VART with leading '.' or plain name) */
                    if (s->subject->nchildren >= 2 && s->subject->children[1]) {
                        EXPR_t *a1 = s->subject->children[1];
                        const char *entry = NULL;
                        if (a1->kind == E_VART && a1->sval) entry = a1->sval;
                        else if (a1->kind == E_QLIT && a1->sval) entry = a1->sval;
                        if (entry) {
                            /* Strip leading '.' if present (SNOBOL4 label reference) */
                            if (*entry == '.') entry++;
                            snprintf(e->body_label, sizeof e->body_label, "%s", entry);
                        }
                    }
                    /* Capture end_label from DEFINE goto target: DEFINE('F()'):(FEnd)
                     * The goto is uncond or onsuccess pointing to the post-body label. */
                    if (s->go) {
                        const char *endlbl = s->go->uncond ? s->go->uncond
                                           : s->go->onsuccess;
                        if (endlbl && endlbl[0])
                            snprintf(e->end_label, sizeof e->end_label, "%s", endlbl);
                    }
                }
            }
            continue;
        }

        /* A pattern assignment: subject is a variable, no pattern field, has_eq set.
         * Register pattern-valued assignments as named patterns so the ASM backend
         * can emit compiled Byrd-box code with direct ASM user-function calls.
         * The runtime dispatch path (stmt_match_descr) cannot call back into
         * compiled ASM SNOBOL4 user functions (nPush/nPop/nInc etc.), so named-pattern
         * compilation is essential for patterns containing semantic action callbacks. */
        if (s->subject && s->subject->kind == E_VART &&
            s->has_eq && s->replacement && !s->pattern) {
            if (expr_is_pattern_expr(s->replacement)) {
                named_pat_register(s->subject->sval, s->replacement);
            } else if (s->replacement->kind == E_QLIT && s->replacement->sval) {
                str_var_register(s->subject->sval, s->replacement->sval);
            }
        }
    }

    /* Second pass: detect which user functions use NRETURN.
     * Walk all statements; when inside a named-function body (between its
     * entry label and the next label), check goto targets for NRETURN. */
    {
        NamedPat *cur_np = NULL;
        for (STMT_t *s = prog->head; s; s = s->next) {
            /* Entering a labeled statement — check if this label is a function entry */
            if (s->label && s->label[0]) {
                NamedPat *found = NULL;
                for (int i = 0; i < named_pat_count; i++) {
                    if (named_pats[i].is_fn &&
                        strcasecmp(named_pats[i].varname, s->label) == 0) {
                        found = &named_pats[i];
                        break;
                    }
                }
                if (found) cur_np = found;
                else cur_np = NULL; /* new label = new block, not this fn */
            }
            if (!cur_np) continue;
            /* Check all goto targets for NRETURN */
            if (s->go) {
                const char *targets[3] = {s->go->onsuccess, s->go->onfailure, s->go->uncond};
                for (int ti = 0; ti < 3; ti++) {
                    if (targets[ti] && strcasecmp(targets[ti], "NRETURN") == 0) {
                        cur_np->uses_nreturn = 1;
                        nreturn_fns_add(cur_np->varname);
                    }
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * emit_pattern — emit a single statement's pattern as a standalone
 * match program (for crosscheck testing).
 *
 * Emits:
 *   section .data  — subject, literals
 *   section .bss   — cursor + saved slots + named-pattern ret slots
 *   section .text  — _start + root pattern + named pattern bodies
 * ----------------------------------------------------------------------- */

static void emit_pattern(STMT_t *stmt) {
    if (!stmt || !stmt->pattern) {
        emit_null_program();
        return;
    }

    /* Reset state */
    var_reset();
    lit_reset();
    extra_slot_count = 0;

    /* Fixed names */
    const char *cursor_sym   = "cursor";
    const char *subj_sym     = "subject_data";
    const char *subj_len_label = "subject_len_val";

    var_register(cursor_sym);

    /* Register .bss slots for all named patterns' ret_ pointers */
    for (int i = 0; i < named_pat_count; i++) {
        var_register(named_pats[i].ret_γ);
        var_register(named_pats[i].ret_ω);
    }

    /* Collect root pattern code + named pattern bodies into a temp buffer */
    char *code_buf = NULL; size_t code_size = 0;
    FILE *code_f = open_memstream(&code_buf, &code_size);
    if (!code_f) { emit_null_program(); return; }

    FILE *real_out = out;
    out = code_f;

    /* Determine subject from stmt->subject (E_QLIT) */
    const char *subj_str = "";
    int         subj_len = 0;
    if (stmt->subject && stmt->subject->kind == E_QLIT) {
        subj_str = stmt->subject->sval;
        subj_len = (int)strlen(subj_str);
    }

    /* Emit the root pattern tree */
    emit_pat_node(stmt->pattern,
                  "root_α", "root_β",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_label,
                  0);

    /* Emit named pattern bodies */
    for (int i = 0; i < named_pat_count; i++)
        emit_named_def(&named_pats[i], cursor_sym, subj_sym, subj_len_label);

    fclose(code_f);

    /* Now write the full .s file in correct section order */
    out = real_out;

    A("; generated by scrip-cc -asm\n");
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
    A("%-24s resq 1\n", subj_len_label);
    for (int i = 0; i < nvar; i++)
        A("%-24s resq 1\n", vars[i]);
    extra_bss_emit();

    /* .text */
    A("\nsection .text\n");
    A("_start:\n");
    A("    ; init\n");
    A("    mov     qword [%s], %d\n", subj_len_label, subj_len);
    A("    mov     qword [%s], 0\n", cursor_sym);
    A("    jmp     root_α\n");

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
 * emit_stmt — body-only mode (-asm-body flag)
 *
 * Emits the pattern code for linking with snobol4_asm_harness.c.
 * Instead of _start / match_success / match_fail:
 *   - Declares:  global root_α, root_β
 *   - Externs:   cursor, subject_data, subject_len_val, match_success, match_fail
 * The harness C file provides all those externs.
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * scan_capture_vars — pre-pass: walk pattern tree and register every
 * DOL/NAM capture variable so cap_vars[] is fully populated before we
 * emit any sections.  Eliminates the need for a two-pass memstream.
 * ----------------------------------------------------------------------- */

static void scan_expr_for_caps(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_DOL || e->kind == E_NAM) {
        const char *vn = (e->children[1] && e->children[1]->sval) ? e->children[1]->sval : "cap";
        cap_var_register(vn);
    }
    /* recurse into all children */

    for (int i = 0; i < e->nchildren; i++)
        scan_expr_for_caps(e->children[i]);
}

static void scan_capture_vars(STMT_t *stmt) {
    cap_vars_reset();
    if (!stmt || !stmt->pattern) return;
    scan_expr_for_caps(stmt->pattern);
    /* Also scan named pattern bodies */
    for (int i = 0; i < named_pat_count; i++)
        scan_expr_for_caps(named_pats[i].pat);
}

/* -----------------------------------------------------------------------
 * emit_stmt — body-only mode (-asm-body flag)
 *
 * Single-pass: pre-scan registers all caps + named patterns first,
 * then emit header → .data → .bss → .text in one forward pass.
 * No open_memstream / two-pass needed.
 * ----------------------------------------------------------------------- */

static void emit_stmt(STMT_t *s) {
    if (!s || !s->pattern) return;

    var_reset();
    lit_reset();
    cap_vars_reset();
    extra_slot_count = 0;

    const char *cursor_sym   = "cursor";
    const char *subj_sym     = "subject_data";
    const char *subj_len_label = "subject_len_val";

    /* Named-pattern ret_ slots always known up front */
    for (int i = 0; i < named_pat_count; i++) {
        var_register(named_pats[i].ret_γ);
        var_register(named_pats[i].ret_ω);
    }

    /* ── Collection pass: redirect output to /dev/null, advance uid counter,
     *    fire all var_register / lit_intern / cap_var_register calls.
     *    uid sequence is identical to the real pass that follows, so every
     *    derived name (lit_str_N, dol_entry_X, root_α_saved, …) is
     *    registered before we emit any section headers.            ── */
    FILE *devnull = fopen("/dev/null", "w");
    FILE *real_out = out;
    out = devnull;

    int uid_before_dry = uid;   /* save uid counter state */

    emit_pat_node(s->pattern,
                  "root_α", "root_β",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_label, 0);
    for (int i = 0; i < named_pat_count; i++)
        emit_named_def(&named_pats[i], cursor_sym, subj_sym, subj_len_label);

    fclose(devnull);
    out = real_out;
    uid = uid_before_dry;       /* reset so real pass generates same labels */

    /* Body mode: box DATA vars must also appear in .bss — the harness has no
     * per-box DATA template.  Flush all box DATA registrations into global .bss. */
    for (int bi = 0; bi < box_data_count; bi++) {
        BoxDataCtx *b = &box_data[bi];
        for (int vi = 0; vi < b->nvar; vi++)
            var_register(b->vars[vi]);  /* box_ctx_idx==-1 here → routes to .bss */
    }

    /* ── Emit pass: all symbols now registered; emit sections in order ── */

    /* Header */
    A("; generated by scrip-cc -asm-body — link with snobol4_asm_harness.o\n");
    A("; assemble: nasm -f elf64 -I<runtime/asm/> body.s -o body.o\n");
    A("; link:     gcc -no-pie -o prog body.o snobol4_asm_harness.o\n");

    A("%%include \"snobol4_asm.mac\"\n");

    A("    global root_α, root_β\n");
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
    int has_bss = nvar > 0 || extra_slot_count > 0 || cap_var_count > 0;
    if (has_bss) {
        A("\nsection .bss\n");
        for (int i = 0; i < nvar; i++)
            A("%-24s resq 1\n", vars[i]);
        extra_bss_emit();
        cap_vars_emit_bss();
    }

    /* .text — real emit; uid counter continues from where dry run left off,
     *         so all generated labels are identical to the collected names  */
    A("\nsection .text\n");

    emit_pat_node(s->pattern,
                  "root_α", "root_β",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_label, 0);

    for (int i = 0; i < named_pat_count; i++)
        emit_named_def(&named_pats[i], cursor_sym, subj_sym, subj_len_label);
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */

int asm_body_mode = 0; /* set by -asm-body flag */

/* -----------------------------------------------------------------------
 * emit_program — full-program mode (-asm flag without -asm-body)
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


/* ---- data section string registry ---- */
#define MAX_STRS 8192
typedef struct { char label[LBUF + 16]; char *val; } StrEntry;
/* Heap-allocated to avoid ~2.8MB BSS segment */
static StrEntry *str_table = NULL;
static int       str_count = 0;
static void str_table_init(void) {
    if (!str_table)
        str_table = (StrEntry *)calloc(MAX_STRS, sizeof(StrEntry));
}

static void str_reset(void) { str_count = 0; }

static const char *str_intern(const char *s) {
    if (!s) s = "";  /* guard against NULL sval */
    for (int i = 0; i < str_count; i++)
        if (strcmp(str_table[i].val, s) == 0) return str_table[i].label;
    if (str_count >= MAX_STRS) return "S_overflow";
    StrEntry *e = &str_table[str_count++];
    /* M-ASM-READABLE-A: expand special chars for readability.
     * _ passes through (common word-separator — keep readable).
     * On collision (two distinct source names expand identically),
     * append _N uid to the second — rare, visible, honest. */
    char exp[LBUF];
    expand_name(s, exp, sizeof exp);
    if (exp[0]) {
        snprintf(e->label, sizeof e->label, "S_%s", exp);
        for (int i = 0; i < str_count - 1; i++) {
            if (strcmp(str_table[i].label, e->label) == 0) {
                snprintf(e->label, sizeof e->label, "S_%s_%d", exp, str_count);
                break;
            }
        }
    } else {
        snprintf(e->label, sizeof e->label, "S_%d", str_count);
    }
    e->val = strdup(s);
    return e->label;
}

static void str_emit(void) {
    for (int i = 0; i < str_count; i++) {
        const char *v = str_table[i].val;
        int len = (int)strlen(v);
        if (len == 0) {
            /* empty string — just null terminator */
            A("%-20s db 0  ; ""\n", str_table[i].label);
        } else {
            A("%-20s db ", str_table[i].label);
            for (int j = 0; j < len; j++) {
                if (j) A(", ");
                A("%d", (unsigned char)v[j]);
            }
            A(", 0  ; \"%s\"\n", v);
        }
    }
}

/* ---- float literal registry ---- */
#define MAX_FLTS 1024
typedef struct { char label[32]; double val; } FltEntry;
static FltEntry flt_table[MAX_FLTS];
static int     flt_count = 0;

static void flt_reset(void) { flt_count = 0; }

static const char *flt_intern(double d) {
    uint64_t b; memcpy(&b, &d, 8);
    for (int i = 0; i < flt_count; i++) {
        uint64_t a; memcpy(&a, &flt_table[i].val, 8);
        if (a == b) return flt_table[i].label;
    }
    if (flt_count >= MAX_FLTS) return "F_overflow";
    FltEntry *e = &flt_table[flt_count++];
    snprintf(e->label, sizeof e->label, "F_%d", flt_count);
    e->val = d;
    return e->label;
}

static void flt_emit(void) {
    for (int i = 0; i < flt_count; i++) {
        uint64_t bits; memcpy(&bits, &flt_table[i].val, 8);
        A("%-20s dq 0x%016llx  ; %g\n",
          flt_table[i].label, (unsigned long long)bits, flt_table[i].val);
    }
}


/* ---- expression value into DESCR_t on stack ----
 * Emits code to evaluate expr and store DESCR_t at [rbp+off].
 * Returns 1 if value might fail (needs is_fail check), 0 if always succeeds.
 * Simple cases only: E_QLIT, E_ILIT, E_VART. Complex exprs use stmt_apply. */
static int emit_expr(EXPR_t *e, int rbp_off) {
    if (!e) {
        /* null expr → NULVCL (DT_SNUL=1, ptr=0) */
        A("    mov     qword [rbp%+d], 1\n", rbp_off);   /* v=DT_SNUL */
        A("    mov     qword [rbp%+d], 0\n", rbp_off+8);
        return 0;
    }
    switch (e->kind) {
    case E_QLIT: {
        const char *lab = str_intern(e->sval);
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
        const char *flab = flt_intern(e->dval);
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
        const char *lab = str_intern(e->sval);
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
        /* &KEYWORD — runtime stores keywords uppercase (e.g. "ALPHABET" not "alphabet").
         * Upcase e->sval and strip any & so stmt_get finds it correctly. */
        char kwbuf[128];
        const char *src = e->sval ? e->sval : "";
        if (*src == '&') src++;  /* strip leading & if present */
        int ki = 0;
        for (; src[ki] && ki < (int)sizeof(kwbuf)-1; ki++)
            kwbuf[ki] = (char)toupper((unsigned char)src[ki]);
        kwbuf[ki] = '\0';
        const char *lab = str_intern(kwbuf);
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
        int na = e->nchildren;
        if (na > 8) na = 8;
        const char *fnlab = str_intern(e->sval);

        /* Check if this is a user-defined Snocone function */
        const NamedPat *ufn = named_pat_lookup(e->sval);
        if (ufn && ufn->is_fn) {
            /* User function call-site:
             *   1. Evaluate each arg → store into fn_FNAME_arg_N_t/p .bss slots
             *   2. Store return-γ address → fn_FNAME_ret_γ
             *   3. Store return-ω address → fn_FNAME_ret_ω
             *   4. jmp fn_α
             *   5. return_γ_N: result value comes back in [rbp-16/rbp-8]
             *      (the function body assigns its return value to OUTPUT or
             *       a variable; we need the return value in [rbp-32/24])
             *   For now: result of user fn call is the value of the expression
             *   that was the last assignment in the function.  We implement
             *   this via a dedicated return-value .bss slot: fn_FNAME_retval_t/p.
             *   The γ/ω restore code already runs stmt_set to restore params,
             *   then jumps here — the function must have set fn_retval before
             *   jumping to [ret_γ]. */
            int cuid = call_uid();
            char ret_γ_lbl[LBUF], ret_ω_lbl[LBUF];
            snprintf(ret_γ_lbl, LBUF, "ucall%d_ret_g", cuid);
            snprintf(ret_ω_lbl, LBUF, "ucall%d_ret_o", cuid);

            int actual_args = (na < ufn->nparams) ? na : ufn->nparams;
            int actual_locals = ufn->nlocals;

            /* Recursion-safe calling convention using the C stack.
             * Saves params AND locals before jmp, restores at both return labels.
             * n_pushed = 2 (ret addrs) + (actual_args + actual_locals) * 2 qwords.
             * +1 for push r12 (caller DATA block ptr save).
             */
            int n_pushed = 2 + (actual_args + actual_locals) * 2 + 1;
            int extra_align = (n_pushed % 2) ? 1 : 0;
            if (extra_align) A("    sub     rsp, 8          ; align pad\n");

            /* Step 1: save old param variable values onto the stack (reverse order) */
            for (int ai = actual_args - 1; ai >= 0; ai--) {
                const char *plab = str_intern(ufn->param_names[ai]);
                A("    GET_VAR     %s\n", plab);
                A("    push    qword [rbp-8]\n");
                A("    push    qword [rbp-16]\n");
            }

            /* Step 1b: save old local variable values onto the stack (reverse order).
             * SNOBOL4 locals (declared after ')' in DEFINE prototype) are global
             * variables — they must be saved/restored exactly like params so that
             * recursive calls don't clobber the caller's locals. */
            for (int li = actual_locals - 1; li >= 0; li--) {
                const char *llab = str_intern(ufn->local_names[li]);
                A("    GET_VAR     %s\n", llab);
                A("    push    qword [rbp-8]\n");
                A("    push    qword [rbp-16]\n");
            }

            /* push callee DATA block ptr (popped as rdi at return for blk_free) */
            A("    push    r12\n");

            /* Step 2: save old ret addresses onto the stack */
            A("    push    qword [%s]\n", ufn->ret_ω);
            A("    push    qword [%s]\n", ufn->ret_γ);

            /* Step 3: evaluate args and store into fn_arg slots */
            for (int ai = 0; ai < actual_args && e->children[ai]; ai++) {
                char arg_slot_t[LBUF2+16], arg_slot_p[LBUF2+16];
                snprintf(arg_slot_t, sizeof arg_slot_t, "fn_%s_arg_%d_t", ufn->safe, ai);
                snprintf(arg_slot_p, sizeof arg_slot_p, "fn_%s_arg_%d_p", ufn->safe, ai);
                emit_expr(e->children[ai], -32);
                A("    mov     rax, [rbp-32]\n");
                A("    mov     rcx, [rbp-24]\n");
                A("    mov     [%s], rax\n", arg_slot_t);
                A("    mov     [%s], rcx\n", arg_slot_p);
            }

            /* Step 4: install new return addresses and jump */
            /* alloc fresh DATA block for this invocation */
            A("    mov     rdi, [rel box_%s_data_size]\n", ufn->safe);
            A("    call    blk_alloc\n");
            A("    mov     rdi, rax\n");  /* dst = new DATA block */
            A("    lea     rsi, [rel box_%s_data_template]\n", ufn->safe);
            A("    mov     rdx, [rel box_%s_data_size]\n", ufn->safe);
            A("    call    memcpy\n");
            A("    mov     r12, rax\n");  /* r12 = new DATA ptr */
            A("    lea     rax, [rel %s]\n", ret_γ_lbl);
            A("    mov     [%s], rax\n", ufn->ret_γ);
            A("    lea     rax, [rel %s]\n", ret_ω_lbl);
            A("    mov     [%s], rax\n", ufn->ret_ω);
            /* Save outer match subject globals — the callee may run its own
             * stmt_setup_subject() internally (e.g. icase scanning str),
             * which would clobber subject_data/subject_len_val/cursor. */
            A("    call    stmt_save_subject\n");
            A("    jmp     %s\n", ufn->α_lbl);

            /* Helper: pop saved frame off stack.
             * Stack layout (rsp→top): ret_γ, ret_ω, param[0].t, param[0].p, ...
             * We pop ret addresses back into the ret slots, then restore params
             * via stmt_set (which needs rdi/rsi/rdx — so we pop into temporaries
             * first, then call stmt_set for each param).
             *
             * For param restore we pop type+ptr into r8+r9 (callee-saved in
             * System V), then call stmt_set(name, r8, r9).  Actually stmt_set
             * takes DESCR_t by value (rsi=type, rdx=ptr per ABI), so:
             *   lea rdi, [param_name]
             *   pop rsi   ; type
             *   pop rdx   ; ptr  — wait, these are 64-bit fields; DESCR_t is
             *              ; passed as two registers per System V aggregate rules.
             * Simpler: pop into [rbp-32/24] then call stmt_set with rbp-32/24.
             * But stmt_set signature is stmt_set(const char *name, DESCR_t d)
             * where d is passed as rsi (type) + rdx (ptr) by value.
             */

            /* Step 5a: γ return */
            A("%s:\n", ret_γ_lbl);
            A("    pop     qword [%s]\n", ufn->ret_γ);
            A("    pop     qword [%s]\n", ufn->ret_ω);
            /* pop callee DATA block ptr, free it */
            A("    pop     rdi\n");
            A("    mov     rsi, [rel box_%s_data_size]\n", ufn->safe);
            A("    call    blk_free\n");
            /* Restore old local values from stack (forward order — pushed in reverse) */
            for (int li = 0; li < actual_locals; li++) {
                const char *llab = str_intern(ufn->local_names[li]);
                A("    pop     rsi\n");
                A("    pop     rdx\n");
                A("    lea     rdi, [rel %s]\n", llab);
                A("    call    stmt_set\n");
            }
            /* Get return value BEFORE restoring params — param restore would
             * overwrite the return-value variable when fn name == param name.
             * Fix: B-263. Restore outer subject globals first (B-263 icase fix). */
            A("    call    stmt_restore_subject\n");
            A("    GET_VAR     %s\n", fnlab);
            /* NRETURN deref: if retval is a NAME string (e.g. Pop = .dummy),
             * resolve it to the variable's value. If retval is already a typed
             * value (e.g. Top = .value($'@S') resolved by E_NAM+E_FNC fix),
             * stmt_nreturn_deref passes it through unchanged. */
            int emit_nret = ufn->uses_nreturn || nreturn_fns_has(fnlab);
            if (emit_nret) {
                A("    mov     rdi, [rbp-16]\n");
                A("    mov     rsi, [rbp-8]\n");
                A("    call    stmt_nreturn_deref\n");
                A("    STORE_RESULT16\n");
            }
            /* Restore old param values from stack */
            for (int ai = 0; ai < actual_args; ai++) {
                const char *plab = str_intern(ufn->param_names[ai]);
                A("    pop     rsi\n");
                A("    pop     rdx\n");
                A("    lea     rdi, [rel %s]\n", plab);
                A("    call    stmt_set\n");
            }
            if (extra_align) A("    add     rsp, 8          ; remove align pad\n");
            /* Return value already in [rbp-16/8] from GET_VAR above */
            A("    mov     rax, [rbp-16]\n");
            A("    mov     rdx, [rbp-8]\n");
            A("    call    stmt_is_fail\n");
            A("    test    eax, eax\n");
            A("    jz      ucall%d_has_val\n", cuid);
            A("    LOAD_NULVCL32\n");
            A("    jmp     ucall%d_done\n", cuid);
            A("ucall%d_has_val:\n", cuid);
            A("    mov     rax, [rbp-16]\n");
            A("    mov     rdx, [rbp-8]\n");
            A("    mov     [rbp-32], rax\n");
            A("    mov     [rbp-24], rdx\n");
            A("    jmp     ucall%d_done\n", cuid);

            /* Step 5b: ω return (FRETURN) */
            A("%s:\n", ret_ω_lbl);
            A("    pop     qword [%s]\n", ufn->ret_γ);
            A("    pop     qword [%s]\n", ufn->ret_ω);
            /* pop callee DATA block ptr, free it */
            A("    pop     rdi\n");
            A("    mov     rsi, [rel box_%s_data_size]\n", ufn->safe);
            A("    call    blk_free\n");
            /* Restore old local values from stack */
            for (int li = 0; li < actual_locals; li++) {
                const char *llab = str_intern(ufn->local_names[li]);
                A("    pop     rsi\n");
                A("    pop     rdx\n");
                A("    lea     rdi, [rel %s]\n", llab);
                A("    call    stmt_set\n");
            }
            for (int ai = 0; ai < actual_args; ai++) {
                const char *plab = str_intern(ufn->param_names[ai]);
                A("    pop     rsi\n");
                A("    pop     rdx\n");
                A("    lea     rdi, [rel %s]\n", plab);
                A("    call    stmt_set\n");
            }
            if (extra_align) A("    add     rsp, 8          ; remove align pad\n");
            /* FRETURN: restore outer subject globals then signal failure */
            A("    call    stmt_restore_subject\n");
            A("    LOAD_FAILDESCR32\n");
            A("ucall%d_done:\n", cuid);
            return 1;
        }
        /* Fast path: 1-arg call with simple literal arg → CALL1_INT / CALL1_STR
         * Skip fast path if function uses NRETURN — needs full ucall + deref. */
        int fn_uses_nreturn = (ufn && ufn->uses_nreturn) ||
                              (e->sval && nreturn_fns_has(e->sval));
        if (na == 1 && e->children[0] && rbp_off == -32 && !fn_uses_nreturn) {
            EXPR_t *arg0 = e->children[0];
            if (arg0->kind == E_ILIT) {
                A("    CALL1_INT   %s, %ld\n", fnlab, (long)arg0->ival);
                return 1;
            }
            if (arg0->kind == E_QLIT) {
                const char *alab = str_intern(arg0->sval);
                A("    CALL1_STR   %s, %s\n", fnlab, alab);
                return 1;
            }
            if (arg0->kind == E_VART) {
                const char *alab = str_intern(arg0->sval);
                A("    CALL1_VAR   %s, %s\n", fnlab, alab);
                return 1;
            }
        }
        /* Fast path: 2-arg call with atom args → CONC2_* macros.
         * ONLY valid for CONCAT and ALT — these macros call stmt_concat directly.
         * All other 2-arg functions (LGT, DIFFER, GT, etc.) must use APPLY_FN_N. */
        int is_concat_or_alt = e->sval && (strcasecmp(e->sval, "concat") == 0 ||
                                            strcasecmp(e->sval, "alt")    == 0);
        if (is_concat_or_alt && na == 2 && e->children[0] &&
            (rbp_off == -32 || rbp_off == -16)) {
            EXPR_t *a0 = e->children[0];
            EXPR_t *a1 = e->children[1];  /* may be NULL → treated as E_NULV */
            int a0s = (a0->kind == E_QLIT);
            int a0v = (a0->kind == E_VART);
            int a1s = (a1 && a1->kind == E_QLIT);
            int a1v = (a1 && a1->kind == E_VART);
            int a1n = (!a1 || a1->kind == E_NULV);
            int a1i = (a1 && a1->kind == E_ILIT);
            int r16  = (rbp_off == -16);
            const char *m_ss = r16 ? "CONC2_16  " : "CONC2   ";
            const char *m_sn = r16 ? "CONC2_N16 " : "CONC2_N ";
            const char *m_sv = r16 ? "CONC2_SV16" : "CONC2_SV";
            const char *m_vs = r16 ? "CONC2_VS16" : "CONC2_VS";
            const char *m_vn = r16 ? "CONC2_VN16" : "CONC2_VN";
            const char *m_vv = r16 ? "CONC2_VV16" : "CONC2_VV";
            if (a0s && a1n) {
                A("    %s %s, %s\n", m_sn, fnlab, str_intern(a0->sval));
                return 1;
            }
            if (a0s && a1s) {
                A("    %s %s, %s, %s\n", m_ss, fnlab,
                  str_intern(a0->sval), str_intern(a1->sval));
                return 1;
            }
            if (a0s && a1v) {
                A("    %s %s, %s, %s\n", m_sv, fnlab,
                  str_intern(a0->sval), str_intern(a1->sval));
                return 1;
            }
            if (a0v && a1s) {
                A("    %s %s, %s, %s\n", m_vs, fnlab,
                  str_intern(a0->sval), str_intern(a1->sval));
                return 1;
            }
            if (a0v && a1n) {
                A("    %s %s, %s\n", m_vn, fnlab, str_intern(a0->sval));
                return 1;
            }
            int a0n = (a0->kind == E_NULV);
            if (a0n && a1n && !r16) {
                A("    CONC2_NN %s\n", fnlab);
                return 1;
            }
            if (a0v && a1v) {
                A("    %s %s, %s, %s\n", m_vv, fnlab,
                  str_intern(a0->sval), str_intern(a1->sval));
                return 1;
            }
            /* Integer-literal fast paths (rbp_off==-32 only) */
            int a0i = (a0->kind == E_ILIT);
            int a0nv = (a0->kind == E_NULV);
            if (!r16) {
                if (a0v && a1i) {
                    A("    CONC2_VI %s, %s, %ld\n", fnlab,
                      str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1v) {
                    A("    CONC2_IV %s, %ld, %s\n", fnlab,
                      (long)a0->ival, str_intern(a1->sval));
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
                      str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1s) {
                    A("    CONC2_IS %s, %ld, %s\n", fnlab,
                      (long)a0->ival, str_intern(a1->sval));
                    return 1;
                }
            } else {
                /* rbp_off == -16 integer fast paths */
                if (a0v && a1i) {
                    A("    CONC2_VI16 %s, %s, %ld\n", fnlab,
                      str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1v) {
                    A("    CONC2_IV16 %s, %ld, %s\n", fnlab,
                      (long)a0->ival, str_intern(a1->sval));
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
                      str_intern(a0->sval), (long)a1->ival);
                    return 1;
                }
                if (a0i && a1s) {
                    A("    CONC2_IS16 %s, %ld, %s\n", fnlab,
                      (long)a0->ival, str_intern(a1->sval));
                    return 1;
                }
            }
        }
        if (na == 0) {
            A("    APPLY_FN_0  %s\n", fnlab);
        } else {
            int arr_bytes = na * 16;
            A("    sub     rsp, %d\n", arr_bytes);
            for (int ai = 0; ai < na && e->children[ai]; ai++) {
                /* B-286: always use -32 for arg staging. Using the outer rbp_off
                 * (e.g. -16) causes E_VART to emit GET_VAR->rbp-16/8, but
                 * STORE_ARG32 reads from rbp-32/24 -- stale data. The arg
                 * staging slot is always rbp-32/24 regardless of outer context. */
                emit_expr(e->children[ai], -32);
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
    case E_IDX: {
        /* arr<i> or arr['key'] or tbl['key']  →  stmt_aref(arr, key)
         * arr[i,j]                             →  stmt_aref2(arr, key1, key2)
         * SysV AMD64: DESCR_t args passed in pairs of int regs.
         *   arg0 (arr):  rdi=type, rsi=ptr
         *   arg1 (key1): rdx=type, rcx=ptr
         *   arg2 (key2): r8=type,  r9=ptr    (2D only)
         * Returns DESCR_t in rax:rdx.
         *
         * ORDERING FIX: evaluate arr first, push to C stack, then evaluate
         * keys so that [rbp-32/24] clobbers are safe. */
        if (e->nchildren < 2 || !e->children[0] || !e->children[1]) goto fallback;

        /* 2D subscript: arr[i,j] — nchildren==3 */
        if (e->nchildren >= 3 && e->children[2]) {
            /* Step 1: evaluate array into [rbp-32/24], push to stack */
            emit_expr(e->children[0], -32);
            A("    push    qword [rbp-24]\n");   /* arr.p */
            A("    push    qword [rbp-32]\n");   /* arr.v */
            /* Step 2: evaluate key1 into [rbp-32/24], push to stack */
            emit_expr(e->children[1], -32);
            A("    push    qword [rbp-24]\n");   /* key1.p */
            A("    push    qword [rbp-32]\n");   /* key1.v */
            /* Step 3: evaluate key2 into [rbp-32/24] */
            emit_expr(e->children[2], -32);
            /* Step 4: load key2 into r8:r9, pop key1 into rdx:rcx, pop arr into rdi:rsi */
            A("    mov     r8,  [rbp-32]\n");   /* key2.v */
            A("    mov     r9,  [rbp-24]\n");   /* key2.p */
            A("    pop     rdx\n");              /* key1.v */
            A("    pop     rcx\n");              /* key1.p */
            A("    pop     rdi\n");              /* arr.v */
            A("    pop     rsi\n");              /* arr.p */
            A("    call    stmt_aref2\n");
            if (rbp_off == -16) {
                A("    mov     [rbp-16], rax\n");
                A("    mov     [rbp-8],  rdx\n");
            } else {
                A("    mov     [rbp-32], rax\n");
                A("    mov     [rbp-24], rdx\n");
            }
            return 1;
        }

        /* 1D subscript: arr[i] or tbl[key] */
        /* Step 1: evaluate array into [rbp-32/24] */
        emit_expr(e->children[0], -32);
        /* Step 2: push array descriptor onto C stack to protect from key eval */
        A("    push    qword [rbp-24]\n");   /* arr.p */
        A("    push    qword [rbp-32]\n");   /* arr.v */
        /* Step 3: evaluate key into [rbp-32/24] (safe now — arr saved on stack) */
        emit_expr(e->children[1], -32);
        /* Step 4: move key into rdx:rcx, pop arr into rdi:rsi */
        A("    mov     rdx, [rbp-32]\n");   /* key.v */
        A("    mov     rcx, [rbp-24]\n");   /* key.p */
        A("    pop     rdi\n");              /* arr.v */
        A("    pop     rsi\n");              /* arr.p */
        A("    call    stmt_aref\n");
        if (rbp_off == -16) {
            A("    mov     [rbp-16], rax\n");
            A("    mov     [rbp-8],  rdx\n");
        } else {
            A("    mov     [rbp-32], rax\n");
            A("    mov     [rbp-24], rdx\n");
        }
        return 1;
    }

    case E_OR:
    case E_CONCAT: {  /* M-G4-SPLIT-SEQ-CONCAT: value context — string concat */
        /* Unary | in value context — OPSYN dispatch: opsyn('|',.size,1) → APPLY_fn("|",arg,1).
         * Parser creates E_OR with 1 child for prefix |expr. */
        if (e->kind == E_OR && e->nchildren == 1 && e->children[0]) {
            const char *fnlab = str_intern("|");
            A("    sub     rsp, 16\n");
            emit_expr(e->children[0], -32);
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
        /*
         * n-ary E_CONCAT (string concat): inline left-fold avoids slot aliasing.
         * Each step: push accumulator, eval next child, pop+call stmt_concat.
         * n-ary E_OR (pattern ALT): right-fold into binary nodes (unchanged).
         */
        if (e->nchildren > 2 && e->kind == E_CONCAT) {
            int _nc = e->nchildren;
            emit_expr(e->children[0], -32);
            for (int _i = 1; _i < _nc; _i++) {
                A("    push    rdx\n");
                A("    push    rax\n");
                emit_expr(e->children[_i], -32);
                A("    mov     rcx, rdx\n");
                A("    mov     rdx, rax\n");
                A("    pop     rdi\n");
                A("    pop     rsi\n");
                A("    call    stmt_concat\n");
            }
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off + 8);
            return 1;
        }
        if (e->nchildren > 2) {
            /* E_OR right-fold via shared helper */
            int _nc = e->nchildren;
            EXPR_t **_fn, **_fk;
            EXPR_t *_r = ir_nary_right_fold(e, e->kind, &_fn, &_fk);
            int _ret = emit_expr(_r, rbp_off);
            ir_nary_right_fold_free(_fn, _fk, _nc - 1);
            return _ret;
        }
        /* E_CONCAT = string CAT (value context): call stmt_concat directly.
         * E_OR     = pattern ALT (alternation):  call stmt_apply("ALT").
         *
         * Naming: E_CONCAT for string concatenation, E_SEQ/E_ALT for patterns.
         * E_CONCAT in emit_expr is always value-context — pattern-context
         * sequences use E_SEQ and are handled by emit_pat_node, not here. */

        int left_is_str  = e->children[0]  && e->children[0]->kind  == E_QLIT;
        int left_is_var  = e->children[0]  && e->children[0]->kind  == E_VART;
        int right_is_nul = !e->children[1] ||
                            e->children[1]->kind == E_NULV ||
                           (e->children[1]->kind == E_ILIT && e->children[1]->ival == 0);
        int right_is_str = e->children[1] && e->children[1]->kind == E_QLIT;
        int right_is_var = e->children[1] && e->children[1]->kind == E_VART;

        if (e->kind == E_CONCAT) {
            /* CAT fast paths — CAT2_* macros call stmt_concat(a,b) */
            if (rbp_off == -32) {
                if (left_is_str && right_is_str) {
                    A("    CAT2_SS  %s, %s\n",
                      str_intern(e->children[0]->sval),
                      str_intern(e->children[1]->sval));
                    return 1;
                }
                if (left_is_str && right_is_nul) {
                    A("    CAT2_SN  %s\n", str_intern(e->children[0]->sval));
                    return 1;
                }
                if (left_is_str && right_is_var) {
                    A("    CAT2_SV  %s, %s\n",
                      str_intern(e->children[0]->sval),
                      str_intern(e->children[1]->sval));
                    return 1;
                }
                if (left_is_var && right_is_str) {
                    A("    CAT2_VS  %s, %s\n",
                      str_intern(e->children[0]->sval),
                      str_intern(e->children[1]->sval));
                    return 1;
                }
                if (left_is_var && right_is_nul) {
                    A("    CAT2_VN  %s\n", str_intern(e->children[0]->sval));
                    return 1;
                }
                if (left_is_var && right_is_var) {
                    A("    CAT2_VV  %s, %s\n",
                      str_intern(e->children[0]->sval),
                      str_intern(e->children[1]->sval));
                    return 1;
                }
            }
            /* CAT generic fallback: push left result, eval right, pop+call.
             * push/pop is stack-safe regardless of depth — conc_tmp0 aliased. */
            emit_expr(e->children[0], -32);
            A("    push    rdx\n");
            A("    push    rax\n");
            emit_expr(e->children[1], -32);
            A("    mov     rcx, rdx\n");
            A("    mov     rdx, rax\n");
            A("    pop     rdi\n");
            A("    pop     rsi\n");
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
        const char *fnlab  = str_intern(opname);

        if (left_is_str && right_is_nul && rbp_off == -32) {
            const char *slab = str_intern(e->children[0]->sval);
            A("    %s%s, %s\n", mac_sn, fnlab, slab);
            return 1;
        }
        if (left_is_str && right_is_str && rbp_off == -32) {
            const char *s1 = str_intern(e->children[0]->sval);
            const char *s2 = str_intern(e->children[1]->sval);
            A("    %s%s, %s, %s\n", mac_ss, fnlab, s1, s2);
            return 1;
        }
        if (left_is_str && right_is_var && rbp_off == -32) {
            const char *slab = str_intern(e->children[0]->sval);
            const char *vlab = str_intern(e->children[1]->sval);
            A("    %s %s, %s, %s\n", mac_sv, fnlab, slab, vlab);
            return 1;
        }
        if (left_is_var && right_is_str && rbp_off == -32) {
            const char *vlab = str_intern(e->children[0]->sval);
            const char *slab = str_intern(e->children[1]->sval);
            A("    %s %s, %s, %s\n", mac_vs, fnlab, vlab, slab);
            return 1;
        }
        if (left_is_var && right_is_nul && rbp_off == -32) {
            const char *vlab = str_intern(e->children[0]->sval);
            A("    %s %s, %s\n", mac_vn, fnlab, vlab);
            return 1;
        }
        if (left_is_var && right_is_var && rbp_off == -32) {
            const char *v1 = str_intern(e->children[0]->sval);
            const char *v2 = str_intern(e->children[1]->sval);
            A("    %s %s, %s, %s\n", mac_vv, fnlab, v1, v2);
            return 1;
        }
        if (left_is_str && right_is_nul && rbp_off == -16) {
            const char *slab = str_intern(e->children[0]->sval);
            A("    %s%s, %s\n", mac_sn16, fnlab, slab);
            return 1;
        }
        if (left_is_str && right_is_str && rbp_off == -16) {
            const char *s1 = str_intern(e->children[0]->sval);
            const char *s2 = str_intern(e->children[1]->sval);
            A("    %s%s, %s, %s\n", mac_ss16, fnlab, s1, s2);
            return 1;
        }
        if (left_is_str && right_is_var && rbp_off == -16) {
            const char *slab = str_intern(e->children[0]->sval);
            const char *vlab = str_intern(e->children[1]->sval);
            A("    %s %s, %s, %s\n", mac_sv16, fnlab, slab, vlab);
            return 1;
        }
        if (left_is_var && right_is_str && rbp_off == -16) {
            const char *vlab = str_intern(e->children[0]->sval);
            const char *slab = str_intern(e->children[1]->sval);
            A("    %s %s, %s, %s\n", mac_vs16, fnlab, vlab, slab);
            return 1;
        }
        if (left_is_var && right_is_nul && rbp_off == -16) {
            const char *vlab = str_intern(e->children[0]->sval);
            A("    %s %s, %s\n", mac_vn16, fnlab, vlab);
            return 1;
        }
        if (left_is_var && right_is_var && rbp_off == -16) {
            const char *v1 = str_intern(e->children[0]->sval);
            const char *v2 = str_intern(e->children[1]->sval);
            A("    %s %s, %s, %s\n", mac_vv16, fnlab, v1, v2);
            return 1;
        }

        /* Generic ALT fallback */
        A("    sub     rsp, 32\n");
        emit_expr(e->children[0],  rbp_off);
        A("    STORE_ARG32 0\n");
        emit_expr(e->children[1], rbp_off);
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
        const char *fnlab = str_intern(fn);
        EXPR_t *l = e->children[0], *r = e->children[1];
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
                  str_intern(l->sval), str_intern(r->sval));
                return 1;
            }
            if (lv && ri) {
                A("    CONC2_VI %s, %s, %ld\n", fnlab,
                  str_intern(l->sval), (long)r->ival);
                return 1;
            }
            if (li && rv) {
                A("    CONC2_IV %s, %ld, %s\n", fnlab,
                  (long)l->ival, str_intern(r->sval));
                return 1;
            }
            if (li && ri) {
                A("    CONC2_II %s, %ld, %ld\n", fnlab,
                  (long)l->ival, (long)r->ival);
                return 1;
            }
            if (lv && rs) {
                A("    CONC2_VS %s, %s, %s\n", fnlab,
                  str_intern(l->sval), str_intern(r->sval));
                return 1;
            }
            if (ls && rv) {
                A("    CONC2_SV %s, %s, %s\n", fnlab,
                  str_intern(l->sval), str_intern(r->sval));
                return 1;
            }
            if (lv && rn) {
                A("    CONC2_VN %s, %s\n", fnlab, str_intern(l->sval));
                return 1;
            }
        }
        /* Generic 2-arg path: evaluate each child, call via stmt_apply */
        A("    sub     rsp, 32\n");
        emit_expr(l, -32);
        A("    STORE_ARG32 0\n");
        emit_expr(r, -32);
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
        const char *fnlab = str_intern("neg");
        EXPR_t *operand = e->children[0];   /* unop() puts operand in left */
        if (!operand) goto fallback;
        if (rbp_off == -32) {
            if (operand->kind == E_ILIT) {
                A("    CALL1_INT   %s, %ld\n", fnlab, (long)operand->ival);
                return 1;
            }
            if (operand->kind == E_VART) {
                A("    CALL1_VAR   %s, %s\n", fnlab,
                  str_intern(operand->sval));
                return 1;
            }
            if (operand->kind == E_QLIT) {
                A("    CALL1_STR   %s, %s\n", fnlab,
                  str_intern(operand->sval));
                return 1;
            }
        }
        /* Generic 1-arg path */
        A("    sub     rsp, 16\n");
        emit_expr(operand, -32);
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

    /* ---- E_ATP: unary @VAR (cursor capture) or binary pat@VAR ---- */
    case E_ATP: {
        if (e->nchildren >= 2 && e->children[0] && e->children[1]) {
            /* Binary pat @VAR in value context — SNOBOL4 pattern concatenation.
             * Semantics: evaluate LHS (a pattern or value), capture cursor into VAR
             * as a side effect, return LHS value unchanged.
             * In production beauty.sno, "TZ = LE(xTrace,0) pat @txOfs $ *assign(...)"
             * constructs a pattern value (from LE()/pat) and attaches cursor-capture.
             * The runtime pat_cat() handles DT_P concat correctly; we use stmt_concat
             * which delegates to pat_cat when either side is DT_P.
             * B-276: M-BEAUTY-OMEGA — binary @ was incorrectly emitted as OPSYN call.
             *
             * Emit: (1) evaluate LHS into result slot; (2) apply cursor capture to VAR;
             * (3) stmt_concat(LHS, cursor_val) so pat_cat wires the AT element into the
             *     pattern when LHS is DT_P.
             *
             * Note: @VAR cursor-capture in SNOBOL4 pattern context is handled by the
             * pattern emitter (emit_pat_node E_ATP).  Here in value-expression context
             * we produce the equivalent runtime effect via stmt_at_capture + result passthrough.
             */
            EXPR_t *lhs = e->children[0];
            EXPR_t *rhs = e->children[1];  /* E_VART("varname") */
            const char *varname = (rhs && rhs->sval) ? rhs->sval : "";
            const char *varlab  = str_intern(varname);

            /* Step 1: evaluate LHS → [rbp-32/rbp-24] */
            emit_expr(lhs, -32);

            /* Step 2: push LHS result so we can restore after at_capture call */
            A("    push    rdx\n");    /* LHS ptr */
            A("    push    rax\n");    /* LHS type */

            /* Step 3: call stmt_at_capture(varname, cursor) — side effect only */
            A("    lea     rdi, [rel %s]\n", varlab);
            A("    mov     rsi, [cursor]\n");
            A("    call    stmt_at_capture\n");

            /* Step 4: restore LHS → result slot */
            A("    pop     rax\n");    /* LHS type */
            A("    pop     rdx\n");    /* LHS ptr */
            if (rbp_off == -32 || rbp_off == -16) {
                A("    mov     [rbp-32], rax\n");
                A("    mov     [rbp-24], rdx\n");
            } else {
                A("    mov     [rbp%+d], rax\n", rbp_off);
                A("    mov     [rbp%+d], rdx\n", rbp_off+8);
            }
            return 1;
        }
        /* Unary @VAR — cursor capture: load cursor as integer into variable.
         * In value-expression context (outside pattern), returns cursor value. */
        {
            const char *varname = (e->children[0] && e->children[0]->sval) ? e->children[0]->sval
                                : (e->sval ? e->sval : "");
            const char *varlab = str_intern(varname);
            /* Store cursor as integer into variable; result = cursor value */
            A("    mov     rax, [cursor]\n");
            if (*varname) {
                A("    ASSIGN_INT_RAX  %s\n", varlab);
            }
            if (rbp_off == -32 || rbp_off == -16) {
                A("    mov     qword [rbp-16], 2\n");  /* DT_I */
                A("    mov     [rbp-8], rax\n");
            } else {
                A("    mov     qword [rbp%+d], 2\n", rbp_off);
                A("    mov     [rbp%+d], rax\n", rbp_off+8);
            }
            return 1;
        }
    }

    /* ---- Unary | — OPSYN dispatch via APPLY_fn("|", args, 1) ---- */
    /* Handled inside E_OR above when nchildren == 1 */

    /* ---- E_NAM in value context: .VAR yields the name as a string value ---- */
    case E_NAM: {
        /* .dupl → STRVAL("dupl"). Used by OPSYN(.facto,'fact'), APPLY(.eq,…),
         * NRETURN (.a), and any other place a name-value is passed as an arg.
         * In value (non-pattern) context E_NAM(child=E_VART("varname")) → name string.
         *
         * BINARY E_NAM: expr . var → pattern with conditional capture.
         * e.g. 'epsilon . *PushCounter()' in nPush_ body.
         * Emit as: stmt_concat(eval(children[0]), DT_N(children[1])).
         * stmt_concat detects DT_N right operand and builds pat_assign_cond.
         *
         * EXCEPTION: .fieldFn(obj) — when child is E_FNC (a DATA field accessor call
         * like .value($'@S')), we must EVALUATE the call to get the field value,
         * not emit the function name as a literal string. This pattern appears in
         * Push, Top, Pop1 where NRETURN returns through a field getter. */
        EXPR_t *child = e->nchildren > 0 ? e->children[0] : NULL;
        if (child && child->kind == E_FNC) {
            /* Evaluate the function call — result lands in rbp_off slot */
            return emit_expr(child, rbp_off);
        }
        /* Binary E_NAM: expr . var — build a conditional-capture pattern at runtime */
        if (e->nchildren >= 2 && e->children[1]) {
            EXPR_t *right = e->children[1];
            /* Emit left operand into [rbp-32], push it */
            emit_expr(child, -32);
            A("    push    rdx\n");
            A("    push    rax\n");
            /* Emit right operand (the capture var) as DT_N into [rbp-32] */
            emit_expr(right, -32);   /* for E_STAR/E_VART this gives a name or value */
            /* Force right to DT_N if it's E_VART or E_STAR pointing to a varname */
            const char *rname = NULL;
            if (right->kind == E_VART && right->sval) rname = right->sval;
            else if (right->kind == E_STAR && right->nchildren > 0
                     && right->children[0] && right->children[0]->sval)
                rname = right->children[0]->sval;
            else if (right->kind == E_INDR && right->nchildren > 0
                     && right->children[0] && right->children[0]->sval)
                rname = right->children[0]->sval;
            if (rname) {
                /* Emit DT_N for the capture variable name */
                const char *rvlab = str_intern(rname);
                A("    mov     qword [rbp-32], 9\n");  /* DT_N */
                A("    lea     rax, [rel %s]\n", rvlab);
                A("    mov     [rbp-24], rax\n");
            }
            /* Call stmt_concat(left, DT_N(right)) → builds pat_assign_cond */
            A("    mov     rcx, [rbp-24]\n");
            A("    mov     rdx, [rbp-32]\n");
            A("    pop     rdi\n");
            A("    pop     rsi\n");
            A("    call    stmt_concat\n");
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off + 8);
            return 1;
        }
        const char *varname = (child && child->sval)
                              ? child->sval
                              : (e->sval ? e->sval : "");
        const char *vlab = str_intern(varname);
        /* B-282: .VAR must emit DT_N (NAME, type=9), not DT_S (STRING, type=1).
         * stmt_nreturn_deref checks DT_S only, so DT_S was passed through as the
         * string "dummy" instead of being resolved to dummy's value (empty string). */
        if (rbp_off == -32 || rbp_off == -16) {
            A("    mov     qword [rbp%+d], 9\n", rbp_off);   /* DT_N */
            A("    lea     rax, [rel %s]\n", vlab);
            A("    mov     [rbp%+d], rax\n", rbp_off+8);
        } else {
            A("    mov     qword [rbp%+d], 9\n", rbp_off);   /* DT_N */
            A("    lea     rax, [rel %s]\n", vlab);
            A("    mov     [rbp%+d], rax\n", rbp_off+8);
        }
        return 1;
    }

    case E_STAR: {
        /* *VAR — deferred/indirect pattern reference in value context.
         * B-287: M-BUG-BOOTSTRAP-PARSE — *VAR in value context (e.g. ARBNO(*Command))
         * must emit pat_ref(name) to create an XDSAR deferred-lookup node, NOT
         * stmt_get(name) which snapshots the variable at assignment time.
         * When "Parse = nPush() ARBNO(*Command) ..." runs, Command may not yet be
         * assigned. pat_ref() defers the lookup to match time, when Command is valid.
         * The XDSAR engine node (snobol4_pattern.c:XDSAR) resolves the name via
         * NV_GET_fn at every match, giving correct indirect semantics. */
        EXPR_t *operand = e->children[0];
        const char *varname = (operand && operand->sval) ? operand->sval : "";
        const char *varlab  = str_intern(varname);
        if (*varname) {
            A("    lea     rdi, [rel %s]\n", varlab);
            A("    call    pat_ref\n");
        } else {
            A("    mov     eax, 1\n");   /* DT_S */
            A("    xor     edx, edx\n");
        }
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

    case E_INDR: {
        /* $X — indirect read in value context.
         * Parser puts operand in children[1] (SNOBOL4 front) or children[0] (snocone).
         * Emits: name_expr → [rbp-48/40] (temp), then call stmt_get_indirect → rax:rdx. */
        EXPR_t *name_expr = (e->nchildren > 1 && e->children[1])
                            ? e->children[1] : e->children[0];
        /* Use a deeper temp slot for the name so we don't clobber rbp_off slot */
        A("    sub     rsp, 16\n");          /* temp frame for name */
        emit_expr(name_expr, -32);           /* name → [rbp-32/24] */
        A("    mov     rdi, [rbp-32]\n");
        A("    mov     rsi, [rbp-24]\n");
        A("    call    stmt_get_indirect\n");
        A("    add     rsp, 16\n");          /* pop temp frame */
        /* Write result (rax:rdx) to the requested slot */
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
static const char *label_nasm(const char *lbl); /* forward */

/* Returns 1 if target is a SNOBOL4 special goto (RETURN/FRETURN/END etc.)
 * that should map to _SNO_END rather than a user label. */
static int is_special_goto(const char *t) {
    if (!t || !*t) return 0;
    return (strcasecmp(t,"RETURN")==0  || strcasecmp(t,"FRETURN")==0 ||
            strcasecmp(t,"NRETURN")==0 || strcasecmp(t,"SRETURN")==0 ||
            strcasecmp(t,"END")==0);
}

/* Emit a jmp to a goto target, handling special targets. */
/* cur_fn — set to the NamedPat* of the user function being emitted.
 * NULL when outside any user function body.
 * Used by emit_jmp / prog_emit_goto to route RETURN → [fn_ret_γ]. */
static const NamedPat *cur_fn = NULL;

static void emit_jmp(const char *tgt, const char *fallthrough) {
    if (!tgt || !*tgt) {
        if (fallthrough) A("    jmp     %s\n", fallthrough);
        return;
    }
    /* If inside a user function, RETURN → fn_NAME_γ (teardown frame, then ret_γ).
     * FRETURN/NRETURN → fn_NAME_ω (teardown frame, then ret_ω).
     * Must go via the named γ/ω labels — not directly to [ret_slot] —
     * so the per-invocation stack frame (push rbp/sub rsp,56 at α) is torn down
     * before the call-site's ucall_ret_g pop sequence runs. */
    if (cur_fn && (strcasecmp(tgt,"RETURN")==0 ||
                       strcasecmp(tgt,"FRETURN")==0 ||
                       strcasecmp(tgt,"NRETURN")==0)) {
        char lbl[NAME_LEN + 32];
        if (strcasecmp(tgt,"RETURN")==0 || strcasecmp(tgt,"NRETURN")==0)
            snprintf(lbl, sizeof lbl, "fn_%s_γ", cur_fn->safe);
        else
            snprintf(lbl, sizeof lbl, "fn_%s_ω", cur_fn->safe);
        A("    jmp     %s     ; %s\n", lbl, tgt);
        return;
    }
    if (is_special_goto(tgt)) { A("    GOTO_ALWAYS  L_SNO_END     ; %s\n", tgt); return; }
    A("    jmp     %s\n", label_nasm(tgt));
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
        if (cur_fn) {
            char lbl[NAME_LEN + 32];
            if (strcasecmp(target,"RETURN")==0 || strcasecmp(target,"SRETURN")==0 ||
                strcasecmp(target,"NRETURN")==0)
                snprintf(lbl, sizeof lbl, "fn_%s_γ", cur_fn->safe);
            else
                snprintf(lbl, sizeof lbl, "fn_%s_ω", cur_fn->safe);
            A("    jmp     %s     ; %s\n", lbl, target);
        } else {
            A("    GOTO_ALWAYS  L_SNO_END     ; %s\n", target);
        }
        return;
    }
    A("    jmp     %s\n", label_nasm(target));
}
/* ---- scan all labels in program for the jump table ---- */
#define MAX_LABELS 4096
/* Numeric label registry: each unique SNOBOL4 label name gets an integer ID.
 * Emitted as _L_<base>_N in NASM — number guarantees uniqueness, base aids readability.
 * TODO M-ASM-READABLE: named expansion (pp_>= → pp_GT_EQ_N) post M-ASM-BEAUTY. */
static char *label_table[MAX_LABELS];
static int   label_defined[MAX_LABELS]; /* 1 if _L_X: was emitted */
static int   label_count = 0;
static void label_reset(void) {
    label_count = 0;
    memset(label_defined, 0, sizeof label_defined);
}

static int label_id(const char *lbl) {
    if (!lbl || !*lbl) return -1;
    /* Special SNOBOL4 goto targets are handled by emit_jmp/prog_emit_goto directly.
     * Never register them as program labels — they must not become stubs. */
    if (strcasecmp(lbl,"RETURN")==0  || strcasecmp(lbl,"FRETURN")==0 ||
        strcasecmp(lbl,"NRETURN")==0 || strcasecmp(lbl,"SRETURN")==0 ||
        strcasecmp(lbl,"END")==0)
        return -1;
    for (int i = 0; i < label_count; i++)
        if (strcmp(label_table[i], lbl) == 0) return i;
    if (label_count >= MAX_LABELS) return -1;
    label_table[label_count++] = strdup(lbl);
    return label_count - 1;
}

/* Returns a collision-free NASM label: _L_<clean_base>_<N>
 * Base: keep only alphanumerics, collapse runs of non-alnum to single underscore,
 * strip leading/trailing underscores.  Number N alone guarantees uniqueness —
 * the base is pure decoration for readability.
 * TODO M-ASM-READABLE: post M-ASM-BEAUTY sprint to improve base quality. */
static const char *label_nasm(const char *lbl) {
    static char buf[256];
    int uid = label_id(lbl);
    if (uid < 0) { snprintf(buf, sizeof buf, "L_unk_%d", uid); return buf; }
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
    snprintf(buf, sizeof buf, "L_%s_%d", base, uid);
    return buf;
}

static void label_register(const char *lbl) {
    label_id(lbl); /* ensure registered; discard return value */
}

static void emit_prolog_choice(EXPR_t *choice);  /* defined at end of file */

static void emit_program(Program *prog) {
    if (!prog || !prog->head) { emit_null_program(); return; }

    cur_fn = NULL;

    str_reset();
    flt_reset();
    label_reset();
    var_reset();
    lit_reset();
    extra_slot_count = 0;

    /* Pass 1: collect all labels + string literals (recursive expr walk) */
    /* Recursive helper: intern all string/var names reachable from an expr */
    #define WALK_EXPR(root) do {         EXPR_t *_stk[256]; int _top = 0;         if (root) _stk[_top++] = (root);         while (_top > 0) {             EXPR_t *_e = _stk[--_top];             if (!_e) continue;             if (_e->kind == E_QLIT && _e->sval) str_intern(_e->sval);             if (_e->kind == E_VART && _e->sval) str_intern(_e->sval);             if (_e->kind == E_KW   && _e->sval) str_intern(_e->sval);             if (_e->kind == E_FNC  && _e->sval) str_intern(_e->sval);             for (int _i = 0; _i < _e->nchildren && _top < 254; _i++)                 if (_e->children[_i]) _stk[_top++] = _e->children[_i];         }     } while(0)

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->label) { label_register(s->label); str_intern(s->label); }
        if (s->go) {
            /* Special SNOBOL4 goto targets — handled at emit time, not as labels */
            const char *special[] = {"RETURN","FRETURN","NRETURN","SRETURN","END",NULL};
            if (s->go->onsuccess) {
                int sp = 0;
                for (int i = 0; special[i]; i++)
                    if (strcasecmp(s->go->onsuccess, special[i]) == 0) { sp=1; break; }
                if (!sp) label_register(s->go->onsuccess);
            }
            if (s->go->onfailure) {
                int sp = 0;
                for (int i = 0; special[i]; i++)
                    if (strcasecmp(s->go->onfailure, special[i]) == 0) { sp=1; break; }
                if (!sp) label_register(s->go->onfailure);
            }
            if (s->go->uncond) {
                int sp = 0;
                for (int i = 0; special[i]; i++)
                    if (strcasecmp(s->go->uncond, special[i]) == 0) { sp=1; break; }
                if (!sp) label_register(s->go->uncond);
            }
        }
        WALK_EXPR(s->subject);
        WALK_EXPR(s->replacement);
        WALK_EXPR(s->pattern);
    }
    #undef WALK_EXPR

    /* Pass 2: collect .bss slots for pattern stmts (named-pat ret_ pointers).
     * In program mode, skip entries whose replacement is a plain value
     * (E_QLIT/E_ILIT/E_VART) — those are variable assignments, not patterns.
     *
     * ret_γ/ret_ω for named patterns and fn tmp slots go into per-box DATA.
     * arg_slots and save_slots remain in .bss (call-site communication). */
    var_register("cursor");
    var_register("subject_len_val");
    /* subject_data is a byte array — emitted separately in .bss */

    for (int i = 0; i < named_pat_count; i++) {
        if (named_pats[i].is_fn) {
            /* User function:
             * ret_γ/ret_ω go into per-box DATA layout.
             * arg_slots remain in .bss (call-site sets them before jmp α). */
            box_ctx_begin(named_pats[i].safe);
            var_register(named_pats[i].ret_γ);
            var_register(named_pats[i].ret_ω);
            /* Also reserve the 6 DESCR_t scratch slots in DATA (tmp1/tmp2/tmp3) */
            {
                char t[NAME_LEN], p[NAME_LEN];
                snprintf(t, NAME_LEN, "fn_%s_tmp1_t", named_pats[i].safe); var_register(t);
                snprintf(p, NAME_LEN, "fn_%s_tmp1_p", named_pats[i].safe); var_register(p);
                snprintf(t, NAME_LEN, "fn_%s_tmp2_t", named_pats[i].safe); var_register(t);
                snprintf(p, NAME_LEN, "fn_%s_tmp2_p", named_pats[i].safe); var_register(p);
                snprintf(t, NAME_LEN, "fn_%s_tmp3_t", named_pats[i].safe); var_register(t);
                snprintf(p, NAME_LEN, "fn_%s_tmp3_p", named_pats[i].safe); var_register(p);
            }
            box_ctx_end();
            /* Also register ret_γ/ret_ω in .bss — fn_γ/fn_ω jmp still uses them */
            var_register(named_pats[i].ret_γ);
            var_register(named_pats[i].ret_ω);
            /* Per-param save slots (old value backup) and arg slots (call-site input)
             * stay in .bss — they are the call-site communication channel. */
            for (int pi = 0; pi < named_pats[i].nparams; pi++) {
                char pname_safe[NAME_LEN];
                expand_name(named_pats[i].param_names[pi], pname_safe, sizeof pname_safe);
                if (!pname_safe[0]) snprintf(pname_safe, sizeof pname_safe, "p%d", pi);
                char save_slot[LBUF2 + 16], arg_slot[LBUF2 + 16];
                snprintf(save_slot, sizeof save_slot, "fn_%s_save_%s",
                         named_pats[i].safe, pname_safe);
                snprintf(arg_slot,  sizeof arg_slot,  "fn_%s_arg_%d",
                         named_pats[i].safe, pi);
                /* save_slot holds a DESCR_t (2 qwords): _t = type, _p = ptr */
                char save_slot_t[LBUF2 + 32], save_slot_p[LBUF2 + 32];
                snprintf(save_slot_t, sizeof save_slot_t, "%s_t", save_slot);
                snprintf(save_slot_p, sizeof save_slot_p, "%s_p", save_slot);
                var_register(save_slot_t);
                var_register(save_slot_p);
                /* arg_slot holds a DESCR_t (2 qwords): _t = type, _p = ptr */
                char arg_slot_t[LBUF2 + 32], arg_slot_p[LBUF2 + 32];
                snprintf(arg_slot_t, sizeof arg_slot_t, "%s_t", arg_slot);
                snprintf(arg_slot_p, sizeof arg_slot_p, "%s_p", arg_slot);
                var_register(arg_slot_t);
                var_register(arg_slot_p);
            }
            continue;
        }
        if (named_pats[i].pat) {
            EKind rk = named_pats[i].pat->kind;
            if (rk == E_QLIT || rk == E_ILIT || rk == E_FLIT || rk == E_NULV ||
                rk == E_VART || rk == E_KW)
                continue; /* plain value assignment — not a real named pattern */
        }
        /* Non-fn named pattern: ret_γ/ret_ω go into per-box DATA (M-T2-INVOKE).
         * Also keep in .bss so call sites and trampolines work unchanged. */
        box_ctx_begin(named_pats[i].safe);
        var_register(named_pats[i].ret_γ);
        var_register(named_pats[i].ret_ω);
        box_ctx_end();
        /* Also register in .bss for current call-site compatibility */
        var_register(named_pats[i].ret_γ);
        var_register(named_pats[i].ret_ω);
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
            var_register(ss);
        }
    }

    /* ---- Pre-scan: compute body_end_idx for DEFINE-style functions ----
     * Must run BEFORE Pass 3 so dry_cur_fn clearing in Pass 3 uses correct values.
     * For each function with a known end_label (the goto target from the DEFINE stmt,
     * e.g. "TWEnd"), scan stmts to find that label and set body_end_idx = stmt_before_it.
     * For functions without end_label, fall back to "next body_label - 1".
     * scan_idx mirrors uid: skip Prolog stmts same as real pass. */
    {
        /* First pass: assign body_end_idx for functions with end_label */
        for (int fi = 0; fi < named_pat_count; fi++) {
            NamedPat *np = &named_pats[fi];
            if (!np->is_fn || !np->end_label[0]) continue;
            int idx = 0, body_started = 0, last_before_end = -1;
            for (STMT_t *sp = prog->head; sp; sp = sp->next) {
                if (sp->is_end) break;
                if (sp->subject && sp->subject->kind == E_CHOICE) continue;
                if (sp->subject && (sp->subject->kind == E_UNIFY ||
                                    sp->subject->kind == E_CLAUSE)) continue;
                if (!body_started && sp->label && sp->label[0] &&
                    strcasecmp(sp->label, np->body_label) == 0)
                    body_started = 1;
                if (body_started && sp->label && sp->label[0] &&
                    strcasecmp(sp->label, np->end_label) == 0) {
                    np->body_end_idx = idx - 1;
                    break;
                }
                if (body_started) last_before_end = idx;
                idx++;
            }
            if (np->body_end_idx < 0 && last_before_end >= 0)
                np->body_end_idx = last_before_end;
        }
        /* Second pass: functions without end_label — use "next body_label - 1" */
        {
            int scan_idx = 0;
            NamedPat *active_fn = NULL;
            for (STMT_t *sp = prog->head; sp; sp = sp->next) {
                if (sp->is_end) break;
                if (sp->subject && sp->subject->kind == E_CHOICE) continue;
                if (sp->subject && (sp->subject->kind == E_UNIFY ||
                                    sp->subject->kind == E_CLAUSE)) continue;
                if (sp->label && sp->label[0]) {
                    NamedPat *new_fn = NULL;
                    for (int fi = 0; fi < named_pat_count; fi++) {
                        if (named_pats[fi].is_fn &&
                            named_pats[fi].body_label[0] &&
                            strcasecmp(named_pats[fi].body_label, sp->label) == 0) {
                            new_fn = &named_pats[fi];
                            break;
                        }
                    }
                    if (new_fn) {
                        if (active_fn && active_fn->body_end_idx < 0)
                            active_fn->body_end_idx = scan_idx - 1;
                        active_fn = (new_fn->body_end_idx < 0) ? new_fn : NULL;
                    }
                }
                scan_idx++;
            }
            /* Last unresolved function: do NOT extend to end of program */
            (void)active_fn;
        }
    }

    /* ---- Pass 3: dry-run all pattern emissions to collect bss/lit slots ----
     * Mirrors emit_stmt dry-run. Redirects output to /dev/null,
     * runs emit_pat_node for every pattern stmt, then restores.
     * This ensures span_saved/lit_str slots are registered before .bss/.data. */
    {
        FILE *devnull = fopen("/dev/null", "w");
        FILE *real_out_p3 = out;
        out = devnull;
        int uid_save = uid;
        /* Walk statements using the same uid sequence as the real pass */
        int dry_stmt_uid = 0;
        const NamedPat *dry_cur_fn = NULL; /* mirrors cur_fn in the real pass */
        for (STMT_t *sp = prog->head; sp; sp = sp->next) {
            if (sp->is_end) break;
            int dry_uid = dry_stmt_uid++;
        /* Clear dry_cur_fn when past function body range */
        if (dry_cur_fn && dry_cur_fn->body_end_idx >= 0 && dry_uid > dry_cur_fn->body_end_idx)
            dry_cur_fn = NULL;
            /* Mirror cur_fn tracking from the real pass */
            if (sp->label && sp->label[0]) {
                const NamedPat *fn_entry = named_pat_lookup(sp->label);
                if (fn_entry && fn_entry->is_fn) {
                    dry_cur_fn = fn_entry;
                } else {
                    /* DEFINE-style: match by body_label */
                    for (int _fi = 0; _fi < named_pat_count; _fi++) {
                        if (named_pats[_fi].is_fn &&
                            named_pats[_fi].body_label[0] &&
                            strcasecmp(named_pats[_fi].body_label, sp->label) == 0) {
                            dry_cur_fn = &named_pats[_fi];
                            break;
                        }
                    }
                }
                if (dry_cur_fn) {
                    char end_lbl[LBUF + 16];
                    snprintf(end_lbl, sizeof end_lbl, "%s.END", dry_cur_fn->varname);
                    if (strcasecmp(sp->label, end_lbl) == 0)
                        dry_cur_fn = NULL;
                }
            }
            if (!sp->pattern) continue;
            /* Use real label names so var_register registers the correct slot names */
            char dry_α[64]; snprintf(dry_α, sizeof dry_α, "P_%d_α", dry_uid);
            char dry_β[64];  snprintf(dry_β,  sizeof dry_β,  "P_%d_β", dry_uid);
            char dry_γ[64]; snprintf(dry_γ, sizeof dry_γ, "P_%d_γ", dry_uid);
            char dry_ω[64]; snprintf(dry_ω, sizeof dry_ω, "P_%d_ω", dry_uid);
            /* Route sub-node BSS slots into the function's per-box DATA when
             * inside a function body — mirrors the real pass box_ctx logic */
            if (dry_cur_fn) box_ctx_begin(dry_cur_fn->safe);
            emit_pat_node(sp->pattern,
                          dry_α, dry_β, dry_γ, dry_ω,
                          "cursor", "subject_data", "subject_len_val", 0);
            if (dry_cur_fn) box_ctx_end();
        }
        /* also dry-run named pattern bodies */
        for (int i = 0; i < named_pat_count; i++) {
            EKind rk = named_pats[i].pat ? named_pats[i].pat->kind : E_NULV;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
            emit_named_def(&named_pats[i], "cursor","subject_data","subject_len_val");
        }
        fclose(devnull);
        out = real_out_p3;
        uid = uid_save; /* reset so real pass generates same labels */
    }

    /* ---- emit header ---- */
    A("; generated by scrip-cc -asm (program mode)\n");
    A("; link: gcc -no-pie out.s stmt_rt.o snobol4.o mock_includes.o\n");
    A(";             snobol4_pattern.o engine.o -lgc -lm -o prog\n");
    A("%%include \"snobol4_asm.mac\"\n");
    A("    global  main\n");
    A("    extern  stmt_init, stmt_strval, stmt_intval\n");
    A("    extern  stmt_realval, stmt_set_null, stmt_set_indirect, stmt_get_indirect, stmt_nreturn_deref\n");
    A("    extern  stmt_get, stmt_set, stmt_output, stmt_input\n");
    A("    extern  stmt_concat, stmt_is_fail, stmt_finish\n");
    A("    extern  stmt_realval, stmt_set_null, stmt_set_indirect\n");
    A("    extern  stmt_apply, stmt_goto_dispatch\n");
    A("    extern  stmt_setup_subject, stmt_apply_replacement\n");
    A("    extern  stmt_apply_replacement_splice\n");
    A("    extern  stmt_set_capture, stmt_match_var, stmt_match_descr\n");
    A("    extern  stmt_pos_var, stmt_rpos_var\n");
    A("    extern  stmt_save_subject, stmt_restore_subject\n");
    A("    extern  stmt_span_var, stmt_break_var\n");
    A("    extern  stmt_breakx_var, stmt_breakx_lit\n");
    A("    extern  stmt_any_var, stmt_notany_var, stmt_any_ptr\n");
    A("    extern  stmt_break_ptr, stmt_span_ptr\n");
    A("    extern  stmt_at_capture\n");
    A("    extern  kw_anchor\n");
    A("    extern  stmt_aref, stmt_aset, stmt_field_set\n");
    A("    extern  stmt_aref2, stmt_aset2\n");
    A("    extern  comm_stno\n");
    A("    extern  blk_alloc, blk_free, memcpy  ; per-invocation DATA block runtime\n");
    A("    extern  pat_ref  ; B-287: deferred *VAR pattern ref (XDSAR)\n");
    A("    global  cursor, subject_data, subject_len_val\n");
    A("\n");
    /* subject_data/subject_len_val/cursor: defined here, exported for stmt_rt.c */


    /* .data emitted at end of file (after .text) so late str_intern()
     * calls from emit time are captured. See str_emit() below. */

    /* ---- .note.GNU-stack ---- */
    flush_pending_label();
    A("section .note.GNU-stack noalloc noexec nowrite progbits\n");

    /* ---- .bss ---- */
    /* Always emit .bss — subject_data is always needed for pattern stmts */
    flush_pending_label();
    A("section .bss\n");
    for (int i = 0; i < nvar; i++)
        A("%-24s resq 1\n", vars[i]);
    /* Byrd box scratch slots (saved_cursor, arbno stack, etc.) */
    extra_bss_emit();
    /* Per-call-uid user-function save slots (recursion-safe) */
    call_slot_emit();
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

        /* Prolog E_CHOICE: delegate entirely to Prolog emitter */
        if (s->subject && s->subject->kind == E_CHOICE) {
            emit_prolog_choice(s->subject);
            continue;
        }
        /* Prolog directives (E_FNC from :- goal) — skip silently for now */
        if (s->subject && (s->subject->kind == E_UNIFY ||
                           s->subject->kind == E_CLAUSE)) {
            continue;
        }

        int uid = stmt_uid++;
        char next_lbl[64]; snprintf(next_lbl, sizeof next_lbl, "Ln_%d", uid);
        char sfail_lbl[64]; snprintf(sfail_lbl, sizeof sfail_lbl, "Lf_%d", uid);

        /* Clear cur_fn when we've passed the end of its body range.
         * This handles DEFINE-style functions whose body_end_idx was computed
         * by the pre-scan above — prevents cur_fn leaking into top-level stmts. */
        if (cur_fn && cur_fn->body_end_idx >= 0 && uid > cur_fn->body_end_idx)
            cur_fn = NULL;

        /* Major separator: SNOBOL4 statement boundary.
         * Emit "; === label ===..." with the SNOBOL4 source label embedded
         * when present, or plain "; ======..." for unlabelled statements.
         * Then queue the generated label to fold onto the first instruction. */
        emit_sep_major(s->label ? s->label : NULL);
        if (s->label) {
            asmL(label_nasm(s->label));
            { int _id = label_id(s->label);
              if (_id >= 0) label_defined[_id] = 1; }

            /* cur_fn tracking: entering a user function body.
             * Two cases:
             *   1. Snocone-style: label == function varname (e.g. "nPush")
             *   2. DEFINE-style:  label == body_label (e.g. "L_nPush_136")
             * Both must set cur_fn so RETURN routes to the right fn_NAME_γ. */
            const NamedPat *fn_entry = named_pat_lookup(s->label);
            if (fn_entry && fn_entry->is_fn) {
                cur_fn = fn_entry;
            } else {
                /* Try matching by body_label for DEFINE-style functions */
                for (int _fi = 0; _fi < named_pat_count; _fi++) {
                    if (named_pats[_fi].is_fn &&
                        named_pats[_fi].body_label[0] &&
                        strcasecmp(named_pats[_fi].body_label, s->label) == 0) {
                        cur_fn = &named_pats[_fi];
                        break;
                    }
                }
            }
            /* Leaving a user function body: label ends with ".END" */
            if (cur_fn) {
                char end_lbl[LBUF + 16];
                snprintf(end_lbl, sizeof end_lbl, "%s.END", cur_fn->varname);
                if (strcasecmp(s->label, end_lbl) == 0)
                    cur_fn = NULL;
            }
        }

        /* Determine S/F/uncond targets */
        const char *tgt_s = s->go ? s->go->onsuccess : NULL;
        const char *tgt_f = s->go ? s->go->onfailure : NULL;
        const char *tgt_u = s->go ? s->go->uncond    : NULL;

        int id_s = tgt_s ? label_id(tgt_s) : -1;
        int id_f = tgt_f ? label_id(tgt_f) : -1;
        int id_u = tgt_u ? label_id(tgt_u) : -1;

        /* Increment &STCOUNT / &STNO: call comm_stno(lineno) at top of every stmt.
         * Mirrors trampoline_stno() in the C backend. */
        A("    mov     edi, %d\n", s->lineno);
        A("    call    comm_stno\n");

        /* Case 1: pattern-free assignment or expression */
        if (!s->pattern) {
            /* Skip DEFINE stmts — user function registration is compile-time only.
             * The α entry and body are emitted in the NAMED PATTERN BODIES section. */
            if (s->subject && s->subject->kind == E_FNC &&
                s->subject->sval && strcasecmp(s->subject->sval, "DEFINE") == 0) {
                asmL(next_lbl);
                if (tgt_u) { A("    GOTO_ALWAYS  %s\n", label_nasm(tgt_u)); }
                continue;
            }
            /* Evaluate subject (the LHS or expression).
             * Skip for indirect-assignment ($X=val): has_eq + E_INDR/E_DOL subject.
             * Also skip for plain assignment (has_eq + VART/KW): no need to load LHS.
             * The indirect handler below evaluates the inner name expression itself. */
            if (s->subject &&
                !(s->has_eq && (s->subject->kind == E_INDR || s->subject->kind == E_DOL)) &&
                !(s->has_eq && (s->subject->kind == E_VART || s->subject->kind == E_KW)) &&
                !(s->has_eq && s->subject->kind == E_IDX) &&
                !(s->has_eq && s->subject->kind == E_FNC && s->subject->nchildren == 1)) {
                int may_fail = emit_expr(s->subject, -32);
                /* If subject may fail AND there are S/F targets, dispatch.
                 * id_s/id_f == -1 for special targets (RETURN/FRETURN/END) —
                 * they are not in the label registry but emit_jmp handles them
                 * correctly (routes through fn_NAME_γ/ω when inside a fn). */
                int has_sf = (id_s >= 0 || id_f >= 0 ||
                              (tgt_s && is_special_goto(tgt_s)) ||
                              (tgt_f && is_special_goto(tgt_f)));
                if (may_fail && !s->has_eq && has_sf) {
                    int need_fail_lbl = (id_f >= 0 ||
                                        (tgt_f && is_special_goto(tgt_f)));
                    A("    FAIL_BR     %s\n", need_fail_lbl ? sfail_lbl : next_lbl);
                    /* success path */
                    emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                    /* failure path */
                    if (need_fail_lbl) {
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
                int has_f_tgt = (id_f >= 0 || (tgt_f && is_special_goto(tgt_f)));
                /* When stmt has unconditional goto and no :F target, failure on RHS
                 * (e.g. NRETURN) must still reach the unconditional target, not fall
                 * through to next_lbl.  We emit FAIL_BR to a local stub that jumps there. */
                int has_u_only = (!has_f_tgt && tgt_u);
                const char *fail_target = has_f_tgt ? sfail_lbl : next_lbl;
                int is_output = strcasecmp(s->subject->sval, "OUTPUT") == 0;
                /* For keyword LHS (&VAR), NV_SET_fn expects bare name "ANCHOR" not "&ANCHOR" */
                const char *subj_name = s->subject->sval ? s->subject->sval : "";
                /* (E_KW: sval is already the bare keyword name e.g. "ANCHOR") */
                /* Null RHS: X = (no replacement or E_NULV) → clear variable.
                 * OUTPUT = (null RHS) → print a blank line (emit NULVCL → SET_OUTPUT). */
                if (!s->replacement ||
                    (s->replacement->kind == E_NULV)) {
                    if (is_output) {
                        A("    LOAD_NULVCL32\n");
                        A("    SET_OUTPUT\n");
                    } else {
                        const char *vlab = str_intern(subj_name);
                        A("    ASSIGN_NULL %s\n", vlab);
                    }
                /* Fast path: simple literal RHS + non-OUTPUT target → ASSIGN_INT/ASSIGN_STR */
                } else if (!is_output && s->replacement->kind == E_ILIT) {
                    const char *vlab = str_intern(subj_name);
                    A("    ASSIGN_INT  %s, %ld, %s\n", vlab,
                      (long)s->replacement->ival, fail_target);
                } else if (!is_output && s->replacement->kind == E_QLIT) {
                    const char *vlab = str_intern(subj_name);
                    const char *rlab = str_intern(s->replacement->sval);
                    A("    ASSIGN_STR  %s, %s, %s\n", vlab, rlab, fail_target);
                } else {
                    /* General path */
                    emit_expr(s->replacement, -32);
                    /* stmt_is_fail check on RHS.
                     * When stmt has unconditional goto and no :F target, a failing RHS
                     * (e.g. NRETURN) must still reach tgt_u — not fall to next_lbl.
                     * Emit FAIL_BR to sfail_lbl stub which then jumps to tgt_u. */
                    if (has_u_only) {
                        A("    FAIL_BR     %s\n", sfail_lbl);
                    } else {
                        A("    FAIL_BR     %s\n", fail_target);
                    }
                    /* Assign: if subject is OUTPUT, call stmt_output */
                    if (is_output) {
                        A("    SET_OUTPUT\n");
                    } else {
                        const char *vlab = str_intern(subj_name);
                        A("    SET_VAR     %s\n", vlab);
                    }
                }
                /* success path */
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                /* failure path */
                if (has_f_tgt) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                } else if (has_u_only) {
                    /* NRETURN/fail on RHS with unconditional goto: still go to tgt_u */
                    asmL(sfail_lbl);
                    emit_jmp(tgt_u, next_lbl);
                }
            } else if (s->has_eq && s->subject &&
                       (s->subject->kind == E_DOL || s->subject->kind == E_INDR)) {
                /* $expr = val  — indirect assignment.
                 * Parser uses E_INDR (right=operand) for $X in subject position.
                 * E_DOL (left=operand) is the binary capture op; support both. */
                int has_f_tgt = (id_f >= 0 || (tgt_f && is_special_goto(tgt_f)));
                int has_u_only = (!has_f_tgt && tgt_u);
                const char *fail_target = has_f_tgt ? sfail_lbl : next_lbl;
                /* Eval the name expression → [rbp-16/8] */
                /* Parser uses unop() → operand in children[0].
                 * Snocone sc_lower also uses children[0].
                 * Guard nchildren before accessing to avoid OOB read. */
                EXPR_t *indir_name;
                EXPR_t *subj = s->subject;
                EXPR_t *c0 = subj->nchildren > 0 ? subj->children[0] : NULL;
                EXPR_t *c1 = subj->nchildren > 1 ? subj->children[1] : NULL;
                if (subj->kind == E_INDR)
                    indir_name = c1 ? c1 : c0;
                else
                    indir_name = c0 ? c0 : c1;
                emit_expr(indir_name, -16);
                /* Eval the RHS → [rbp-32/24] */
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    /* null RHS for indirect: load NULVCL into [rbp-32/24] */
                    A("    mov     qword [rbp-32], 1\n");
                    A("    mov     qword [rbp-24], 0\n");
                } else {
                    emit_expr(s->replacement, -32);
                    A("    FAIL_BR     %s\n", has_u_only ? sfail_lbl : fail_target);
                }
                A("    SET_VAR_INDIR\n");
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                if (has_f_tgt) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                } else if (has_u_only) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_u, next_lbl);
                }
            } else if (s->has_eq && s->subject &&
                       s->subject->kind == E_IDX &&
                       s->subject->children[0] && s->subject->nchildren >= 2) {
                /* A<i> = val  or  T['key'] = val  →  stmt_aset(arr, key, val)
                 * A[i,j] = val                    →  stmt_aset2(arr, k1, k2, val)
                 *
                 * SysV AMD64 calling convention for stmt_aset(arr, key, val):
                 *   arr: rdi=type, rsi=ptr
                 *   key: rdx=type, rcx=ptr
                 *   val: r8=type,  r9=ptr
                 *
                 * Evaluation order: arr first, then key(s), then RHS val.
                 * We push all onto the C stack to survive RHS evaluation.
                 */
                int has_f_tgt_idx = (id_f >= 0 || (tgt_f && is_special_goto(tgt_f)));
                int has_u_only_idx = (!has_f_tgt_idx && tgt_u);
                const char *fail_target = has_f_tgt_idx ? sfail_lbl : next_lbl;
                static int idx_uid_counter = 0;
                int idx_uid = idx_uid_counter++;

                /* Declare two .bss scratch slots for arr and key */
                char arr_t_lab[64], arr_p_lab[64];
                char key_t_lab[64], key_p_lab[64];
                snprintf(arr_t_lab, sizeof arr_t_lab, "idx_arr%d_t", idx_uid);
                snprintf(arr_p_lab, sizeof arr_p_lab, "idx_arr%d_p", idx_uid);
                snprintf(key_t_lab, sizeof key_t_lab, "idx_key%d_t", idx_uid);
                snprintf(key_p_lab, sizeof key_p_lab, "idx_key%d_p", idx_uid);

                int is_2d = (s->subject->nchildren >= 3 && s->subject->children[2]);

                if (is_2d) {
                    /* 2D: arr[i,j] = val → stmt_aset2(arr, key1, key2, val)
                     * stmt_aset2 signature: (DESCR_t arr, DESCR_t k1, DESCR_t k2, DESCR_t val)
                     * SysV: arr→rdi:rsi, k1→rdx:rcx, k2→r8:r9, val passed on stack
                     * We push: arr, k1, k2 onto C stack, evaluate RHS, then load regs */
                    /* Eval arr → [rbp-16/8], push */
                    emit_expr(s->subject->children[0], -16);
                    A("    push    qword [rbp-8]\n");   /* arr.p */
                    A("    push    qword [rbp-16]\n");  /* arr.v */
                    /* Eval key1 → [rbp-32/24], push */
                    emit_expr(s->subject->children[1], -32);
                    A("    push    qword [rbp-24]\n");  /* k1.p */
                    A("    push    qword [rbp-32]\n");  /* k1.v */
                    /* Eval key2 → [rbp-32/24], push */
                    emit_expr(s->subject->children[2], -32);
                    A("    push    qword [rbp-24]\n");  /* k2.p */
                    A("    push    qword [rbp-32]\n");  /* k2.v */
                    /* Eval RHS → [rbp-32/24] */
                    if (!s->replacement || s->replacement->kind == E_NULV) {
                        A("    mov     qword [rbp-32], 1\n");
                        A("    mov     qword [rbp-24], 0\n");
                    } else {
                        emit_expr(s->replacement, -32);
                        A("    FAIL_BR     %s\n", has_u_only_idx ? sfail_lbl : fail_target);
                    }
                    /* Stack layout (top=rsp): k2.v k2.p k1.v k1.p arr.v arr.p
                     * [rsp+0]=k2.v [rsp+8]=k2.p [rsp+16]=k1.v [rsp+24]=k1.p
                     * [rsp+32]=arr.v [rsp+40]=arr.p */
                    /* val → push on stack for 7th+ arg (beyond 6-reg limit) */
                    /* stmt_aset2(arr,k1,k2,val): 4 DESCR_t = 8 int64 regs; val overflows → stack */
                    /* Push val onto stack first (rightmost arg goes first in stack frame) */
                    A("    sub     rsp, 16\n");         /* space for val on stack */
                    A("    mov     rax, [rbp-32]\n");   /* val.v */
                    A("    mov     [rsp], rax\n");
                    A("    mov     rax, [rbp-24]\n");   /* val.p */
                    A("    mov     [rsp+8], rax\n");
                    /* Load k2 into r8:r9, k1 into rdx:rcx, arr into rdi:rsi */
                    A("    mov     r8,  [rsp+16]\n");   /* k2.v (shifted by 16 for val) */
                    A("    mov     r9,  [rsp+24]\n");   /* k2.p */
                    A("    mov     rdx, [rsp+32]\n");   /* k1.v */
                    A("    mov     rcx, [rsp+40]\n");   /* k1.p */
                    A("    mov     rdi, [rsp+48]\n");   /* arr.v */
                    A("    mov     rsi, [rsp+56]\n");   /* arr.p */
                    A("    call    stmt_aset2\n");
                    A("    add     rsp, 64\n");         /* pop 8 saved qwords (val+k2+k1+arr) */
                } else {
                /* 1D: arr[i] = val or tbl[k] = val */
                /* Evaluate arr → [rbp-16/8] */
                emit_expr(s->subject->children[0],      -16);
                /* Save arr onto C stack */
                A("    push    qword [rbp-8]\n");   /* arr ptr  (high) */
                A("    push    qword [rbp-16]\n");  /* arr type (low) */
                /* Evaluate key → [rbp-32/24] */
                emit_expr(s->subject->children[1],  -32);
                /* Save key onto C stack (now arr is at [rsp+16]..[rsp+23],
                 * key at [rsp+0]..[rsp+7] and [rsp+8]..[rsp+15]) */
                A("    push    qword [rbp-24]\n");  /* key ptr  (high) */
                A("    push    qword [rbp-32]\n");  /* key type (low) */
                /* Evaluate RHS → [rbp-32/24] */
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    A("    mov     qword [rbp-32], 1\n");  /* DT_SNUL */
                    A("    mov     qword [rbp-24], 0\n");
                } else {
                    emit_expr(s->replacement, -32);
                    A("    FAIL_BR     %s\n", has_u_only_idx ? sfail_lbl : fail_target);
                }
                /* Restore key (was at rsp+0..15) and arr (was at rsp+16..31) */
                /* Stack layout (top→bottom): key_t, key_p, arr_t, arr_p
                 * i.e. [rsp+0]=key_t, [rsp+8]=key_p, [rsp+16]=arr_t, [rsp+24]=arr_p */
                A("    mov     r8,  [rbp-32]\n");   /* val type */
                A("    mov     r9,  [rbp-24]\n");   /* val ptr  */
                A("    mov     rdx, [rsp]\n");      /* key type */
                A("    mov     rcx, [rsp+8]\n");    /* key ptr  */
                A("    mov     rdi, [rsp+16]\n");   /* arr type */
                A("    mov     rsi, [rsp+24]\n");   /* arr ptr  */
                A("    add     rsp, 32\n");         /* pop 4 saved qwords */
                A("    call    stmt_aset\n");
                } /* end 1D */
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                if (has_f_tgt_idx) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                } else if (has_u_only_idx) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_u, next_lbl);
                }
            } else if (s->has_eq && s->subject &&
                       s->subject->kind == E_FNC &&
                       s->subject->nchildren == 1 &&
                       s->subject->sval) {
                /* field(obj) = val  →  stmt_field_set(obj, "field", val) */
                int has_f_tgt_fnc = (id_f >= 0 || (tgt_f && is_special_goto(tgt_f)));
                int has_u_only_fnc = (!has_f_tgt_fnc && tgt_u);
                const char *fail_target = has_f_tgt_fnc ? sfail_lbl : next_lbl;
                const char *flab = str_intern(s->subject->sval);

                /* Evaluate obj → push onto stack */
                emit_expr(s->subject->children[0], -16);
                A("    push    qword [rbp-8]\n");   /* obj ptr  */
                A("    push    qword [rbp-16]\n");  /* obj type */
                /* Evaluate val → [rbp-32/24] */
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    A("    mov     qword [rbp-32], 1\n");
                    A("    mov     qword [rbp-24], 0\n");
                } else {
                    emit_expr(s->replacement, -32);
                    A("    FAIL_BR     %s\n", has_u_only_fnc ? sfail_lbl : fail_target);
                }
                /* Set up args: obj in rdi:rsi, field name in rdx, val in rcx:r8 */
                A("    mov     rcx, [rbp-32]\n");   /* val type */
                A("    mov     r8,  [rbp-24]\n");   /* val ptr  */
                A("    lea     rdx, [rel %s]\n", flab);  /* field name */
                A("    mov     rdi, [rsp]\n");      /* obj type */
                A("    mov     rsi, [rsp+8]\n");    /* obj ptr  */
                A("    add     rsp, 16\n");         /* pop 2 saved qwords */
                A("    call    stmt_field_set\n");
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                if (has_f_tgt_fnc) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                } else if (has_u_only_fnc) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_u, next_lbl);
                }
            } else if (s->has_eq && s->subject &&
                       s->subject->kind == E_FNC &&
                       s->subject->sval &&
                       strcasecmp(s->subject->sval, "ITEM") == 0 &&
                       s->subject->nchildren >= 2) {
                /* item(arr, key) = val  →  stmt_aset(arr, key, val) */
                int has_f_tgt_item = (id_f >= 0 || (tgt_f && is_special_goto(tgt_f)));
                int has_u_only_item = (!has_f_tgt_item && tgt_u);
                const char *fail_target = has_f_tgt_item ? sfail_lbl : next_lbl;
                EXPR_t *arr_expr = s->subject->children[0];
                EXPR_t *key_expr = s->subject->children[1];

                /* Evaluate arr → push */
                emit_expr(arr_expr, -16);
                A("    push    qword [rbp-8]\\n");   /* arr ptr  */
                A("    push    qword [rbp-16]\\n");  /* arr type */
                /* Evaluate key → push */
                emit_expr(key_expr, -16);
                A("    push    qword [rbp-8]\\n");   /* key ptr  */
                A("    push    qword [rbp-16]\\n");  /* key type */
                /* Evaluate val → [rbp-32/24] */
                if (!s->replacement || s->replacement->kind == E_NULV) {
                    A("    mov     qword [rbp-32], 1\\n");
                    A("    mov     qword [rbp-24], 0\\n");
                } else {
                    emit_expr(s->replacement, -32);
                    A("    FAIL_BR     %s\\n", has_u_only_item ? sfail_lbl : fail_target);
                }
                /* stmt_aset(arr, key, val): arr→rdi:rsi, key→rdx:rcx, val→r8:r9 */
                A("    mov     r8,  [rbp-24]\\n");   /* val ptr  */
                A("    mov     r9,  [rbp-32]\\n");   /* val type — NOTE: r9=type for r8:r9 pair */
                /* Swap so r8=type r9=ptr per our convention — check stmt_aset sig */
                A("    mov     rdx, [rsp+8]\\n");    /* key type */
                A("    mov     rcx, [rsp]\\n");      /* key ptr  */
                A("    mov     rdi, [rsp+24]\\n");   /* arr type */
                A("    mov     rsi, [rsp+16]\\n");   /* arr ptr  */
                A("    add     rsp, 32\\n");
                /* Pass val: need r8=val_type, r9=val_ptr — but SysV only has 6 reg args.
                 * stmt_aset takes (arr DESCR_t, key DESCR_t, val DESCR_t) = 6 int64 regs:
                 * rdi=arr.v, rsi=arr.uid, rdx=key.v, rcx=key.uid, r8=val.v, r9=val.uid */
                A("    mov     r8,  [rbp-32]\\n");   /* val type */
                A("    mov     r9,  [rbp-24]\\n");   /* val ptr  */
                A("    call    stmt_aset\\n");
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                if (has_f_tgt_item) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                } else if (has_u_only_item) {
                    asmL(sfail_lbl);
                    emit_jmp(tgt_u, next_lbl);
                }
            } else {
                /* expression-only or complex case: just evaluate and branch.
                 * Must use tgt_u (not id_u >= 0) — special gotos like FRETURN/RETURN/END
                 * have id == -1 (not in label registry) but emit_jmp handles them correctly
                 * via fn_NAME_γ/ω when cur_fn != NULL.
                 * Also handle S/F-only branches (no unconditional) for completeness. */
                if (tgt_u) {
                    emit_jmp(tgt_u, next_lbl);
                } else {
                    if (tgt_s || tgt_f) {
                        /* No expression to evaluate — statement always succeeds (null subject).
                         * Treat as success: take S branch, or fall to F/next. */
                        emit_jmp(tgt_s ? tgt_s : NULL, next_lbl);
                        if (tgt_f && (id_f >= 0 || is_special_goto(tgt_f))) {
                            /* failure path unreachable here — null subject always succeeds */
                            (void)tgt_f;
                        }
                    }
                }
            }
            asmL(next_lbl);
            continue;
        }

        /* Case 2: pattern-match statement
         * subject pattern [= replacement] :S(L)F(L)
         *
         * Four-port Byrd box execution (Proebsting):
         *   α = entry, β = resume, γ = succeed, ω = fail
         *
         *   1. Eval subject → DESCR_t, call stmt_setup_subject()
         *      (copies string into subject_data[], sets subject_len_val, cursor=0)
         *   2. jmp pat_N_α  — start the pattern
         *   3. pat_N_γ:     — match succeeded
         *      apply replacement (if any), emit_jmp S-target
         *   4. pat_N_ω:     — match failed
         *      emit_jmp F-target
         *
         * The Byrd box code is emitted inline via emit_pat_node().
         */

        /* Greek suffixes: internal Byrd box ports — not exportable as linker symbols.
         * α=entry β=resume γ=succeed ω=fail  (Proebsting four-port model) */
        char pat_α[64]; snprintf(pat_α, sizeof pat_α, "P_%d_α", uid);
        char pat_β[64];  snprintf(pat_β,  sizeof pat_β,  "P_%d_β", uid);
        char pat_γ[64]; snprintf(pat_γ, sizeof pat_γ, "P_%d_γ", uid);
        char pat_ω[64]; snprintf(pat_ω, sizeof pat_ω, "P_%d_ω", uid);

        /* -- subject eval -- */
        emit_expr(s->subject, -16);
        /* stmt_setup_subject — copies into subject_data[], resets cursor=0.
         * B-282: returns 1 if subject is FAILDESCR → fail the whole statement. */
        A("    SUBJ_FROM16\n");
        A("    test    eax, eax\n");
        A("    jnz     %s\n", pat_ω);

        /* Unanchored scan: try pattern at positions 0..subject_len.
         * scan_start_N tracks the current attempt start position.
         * On ω: if scan_start < subject_len, advance and retry α.
         * On γ: match succeeded from scan_start_N. */
        char scan_start[64]; snprintf(scan_start, sizeof scan_start, "scan_start_%d", uid);
        char scan_retry[64]; snprintf(scan_retry, sizeof scan_retry, "scan_retry_%d", uid);
        /* Route scan_start and all pattern sub-node slots into per-box DATA when
         * inside a user function body — they must be pre-registered in Pass 3
         * under the same box context, otherwise var_register here is a no-op
         * (bss section already emitted). */
        if (cur_fn) box_ctx_begin(cur_fn->safe);
        var_register(scan_start);
        /* reset scan_start to 0 (subject_data cursor already 0 from SUBJ_FROM16) */
        A("    mov     qword [%s], 0\n", scan_start);

        /* -- scan retry entry: set cursor = scan_start, then try pattern -- */
        A("%s:\n", scan_retry);
        A("    mov     rax, [%s]\n", scan_start);
        A("    mov     [cursor], rax\n");
        A("    jmp     %s\n", pat_α);

        /* -- Byrd box inline: α/β → γ/ω -- */
        A("\n");
        /* Collect capture varnames reachable from THIS statement's pattern —
         * including captures inside named patterns called by this stmt.
         * cap_vars[] is global (dry-run pre-registered all stmts + named bodies).
         * We must not emit SET_CAPTURE for caps that belong to OTHER stmts' inline
         * patterns but were never touched by this match. */
        #define MAX_STMT_CAPS 64
        const char *stmt_cap_names[MAX_STMT_CAPS];
        int stmt_cap_count = 0;
        {
            /* Track which named-pattern indices we've already walked (cycle guard) */
            int np_visited[128] = {0};
            /* Iterative tree walk collecting E_DOL / E_NAM right-child varnames.
             * When we see E_VART/E_INDR that refers to a named pattern, recurse
             * into that named pattern's body too. */
            EXPR_t *stk[512]; int top = 0;
            if (s->pattern) stk[top++] = s->pattern;
            while (top > 0) {
                EXPR_t *e = stk[--top];
                if (!e) continue;
                if ((e->kind == E_DOL || e->kind == E_NAM) &&
                    e->children[1] && e->children[1]->sval &&
                    stmt_cap_count < MAX_STMT_CAPS)
                    stmt_cap_names[stmt_cap_count++] = e->children[1]->sval;
                /* Follow named pattern references */
                if (e->kind == E_VART || e->kind == E_INDR) {
                    const char *vn = e->sval ? e->sval
                                   : (e->children[0] && e->children[0]->sval ? e->children[0]->sval : NULL);
                    if (vn) {
                        for (int ni = 0; ni < named_pat_count; ni++) {
                            if (!np_visited[ni] && ni < 128 &&
                                strcasecmp(named_pats[ni].varname, vn) == 0) {
                                np_visited[ni] = 1;
                                if (named_pats[ni].pat && top < 510)
                                    stk[top++] = named_pats[ni].pat;
                                break;
                            }
                        }
                    }
                }
                for (int _i = 0; _i < e->nchildren && top < 510; _i++)
                    if (e->children[_i]) stk[top++] = e->children[_i];
            }
        }
        emit_pat_node(s->pattern,
                      pat_α, pat_β,
                      pat_γ, pat_ω,
                      "cursor", "subject_data", "subject_len_val",
                      0 /* depth */);
        if (cur_fn) box_ctx_end();
        A("\n");

        /* -- γ: match succeeded -- */
        asmL(pat_γ);
        /* For ? stmts (no replacement): advance scan_start BEFORE SET_CAPTURE.
         * SET_CAPTURE calls stmt_set_capture (C ABI), trashing rax.
         * [cursor] memory is safe across the call, but we save it early to
         * keep the advance adjacent to the match point and avoid confusion. */
        if (!s->has_eq) {
            A("    mov     rax, [cursor]\n");
            A("    mov     [%s], rax\n", scan_start);
        }
        /* Materialise DOL/NAM captures — only those reachable from this stmt. */
        for (int ci = 0; ci < cap_var_count; ci++) {
            int found = 0;
            for (int si = 0; si < stmt_cap_count; si++)
                if (strcasecmp(cap_vars[ci].varname, stmt_cap_names[si]) == 0)
                    { found = 1; break; }
            if (!found) continue;
            const char *vnlab = str_intern(cap_vars[ci].varname);
            A("    SET_CAPTURE %s, %s, %s\n",
              vnlab, cap_vars[ci].buf_sym, cap_vars[ci].len_sym);
        }
        #undef MAX_STMT_CAPS
        if (s->has_eq && s->subject && s->subject->kind == E_VART) {
            /* apply replacement → subject variable (splice: prefix+repl+suffix) */
            const char *vlab = str_intern(s->subject->sval);
            if (!s->replacement || s->replacement->kind == E_NULV) {
                /* null replacement: delete matched span — emit NULVCL */
                A("    mov     qword [rbp-32], 1\n"); /* DT_SNUL */
                A("    mov     qword [rbp-24], 0\n");
            } else {
                emit_expr(s->replacement, -32);
            }
            A("    APPLY_REPL_SPLICE  %s, %s\n", vlab, scan_start);
        }
        emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);

        /* -- ω: match failed at this scan_start position --
         * When scan_fail_tgt is RETURN/FRETURN inside a user function, the
         * conditional branches need a plain label — emit a local trampoline
         * that routes through fn_NAME_γ/ω for frame teardown. */
        asmL(pat_ω);
        {
            const char *scan_fail_tgt = tgt_f ? tgt_f : tgt_u;
            const char *scan_fail;
            char tramp[LBUF];
            int need_tramp = (cur_fn && scan_fail_tgt
                              && is_special_goto(scan_fail_tgt)
                              && strcasecmp(scan_fail_tgt,"END") != 0);
            if (need_tramp) {
                static int tramp_uid = 0;
                snprintf(tramp, sizeof tramp, "scan_fail_tramp_%d", tramp_uid++);
                scan_fail = tramp;
            } else if (scan_fail_tgt && is_special_goto(scan_fail_tgt)) {
                /* Special goto at top level (e.g. END outside a function):
                 * label_nasm() can't resolve it — use the known ASM label directly. */
                if (strcasecmp(scan_fail_tgt, "END") == 0)
                    scan_fail = "L_SNO_END";
                else {
                    /* RETURN/FRETURN outside a function — treat as END */
                    scan_fail = "L_SNO_END";
                }
            } else {
                scan_fail = scan_fail_tgt ? label_nasm(scan_fail_tgt) : next_lbl;
            }
            A("    cmp     qword [rel kw_anchor], 0\n");
            A("    jne     %s\n", scan_fail);
            A("    mov     rax, [%s]\n", scan_start);
            A("    inc     rax\n");
            A("    cmp     rax, [subject_len_val]\n");
            A("    jg      %s\n", scan_fail);
            A("    mov     [%s], rax\n", scan_start);
            A("    jmp     %s\n", scan_retry);
            if (need_tramp) {
                A("%s:\n", tramp);
                emit_jmp(scan_fail_tgt, next_lbl);
            }
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
    for (int i = 0; i < named_pat_count; i++) {
        if (named_pats[i].pat) {
            EKind rk = named_pats[i].pat->kind;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
        }
        emit_named_def(&named_pats[i],
                           "cursor","subject_data","subject_len_val");
    }

    flush_pending_sep(); emit_sep_major("STUB LABELS");
    A("section .text\n"); /* stubs must also be in .text */
    /* ---- Stub definitions for referenced-but-undefined labels ----
     * These are dangling gotos (e.g. :F(error) with no "error" label defined,
     * or computed goto dispatch labels not yet implemented).
     * Map them to _SNO_END so the program assembles and terminates cleanly. */
    for (int i = 0; i < label_count; i++) {
        if (!label_defined[i]) {
            A("%s:  ; STUB → _SNO_END (dangling or computed goto)\n",
              label_nasm(label_table[i]));
            A("    GOTO_ALWAYS  L_SNO_END\n");
        }
    }

    /* ---- per-box relocation tables ---- */
    emit_blk_reloc_tables();

    /* ---- .data emitted last so all str_intern() calls are captured ---- */
    flush_pending_sep(); emit_sep_major("STRING TABLE");
    A("section .data\n");
    str_emit();
    flt_emit();
}

/* -----------------------------------------------------------------------
 * emit_blk_reloc_tables — emit per-box relocation tables (.rodata) and
 *                        per-box DATA template sections (.data)
 *
 * M-T2-EMIT-TABLE: reloc tables have count=0 (entries added by M-T2-INVOKE).
 * M-T2-EMIT-SPLIT: DATA templates emitted here; box_SAFE_data_template global.
 * blk_reloc_kind: BLK_RELOC_REL32=1  BLK_RELOC_ABS64=2  (matches blk_reloc.h)
 * ----------------------------------------------------------------------- */
static void emit_blk_reloc_tables(void) {
    /* Collect boxes: named pattern bodies (non-trivial) + is_fn functions */
    int box_count = 0;
    for (int i = 0; i < named_pat_count; i++) {
        if (named_pats[i].is_fn) { box_count++; continue; }
        if (!named_pats[i].pat) continue;
        EKind rk = named_pats[i].pat->kind;
        if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW)
            continue;
        box_count++;
    }

    flush_pending_sep(); emit_sep_major("BOX RELOCATION TABLES");
    A("section .rodata\n");

    if (box_count == 0) {
        A("; (no named pattern boxes in this program)\n");
        return;
    }

    /* .rodata: reloc counts + tables (empty for now — M-T2-INVOKE fills them) */
    for (int i = 0; i < named_pat_count; i++) {
        int is_real_box = named_pats[i].is_fn;
        if (!is_real_box) {
            if (!named_pats[i].pat) continue;
            EKind rk = named_pats[i].pat->kind;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
            is_real_box = 1;
        }
        if (!is_real_box) continue;
        A("    global  box_%s_reloc_count, box_%s_reloc_table\n",
          named_pats[i].safe, named_pats[i].safe);
    }
    for (int i = 0; i < named_pat_count; i++) {
        int is_real_box = named_pats[i].is_fn;
        if (!is_real_box) {
            if (!named_pats[i].pat) continue;
            EKind rk = named_pats[i].pat->kind;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
            is_real_box = 1;
        }
        if (!is_real_box) continue;
        const char *safe = named_pats[i].safe;
        A("; --- box %s ---\n", named_pats[i].varname);
        A("box_%s_reloc_count: dq 0\n", safe);
        A("box_%s_reloc_table:\n", safe);
        A("; (entries added by M-T2-INVOKE)\n");
        A("\n");
    }

    /* .data: per-box DATA templates (M-T2-EMIT-SPLIT) */
    flush_pending_sep(); emit_sep_major("BOX DATA TEMPLATES");
    for (int i = 0; i < named_pat_count; i++) {
        int is_real_box = named_pats[i].is_fn;
        if (!is_real_box) {
            if (!named_pats[i].pat) continue;
            EKind rk = named_pats[i].pat->kind;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV||rk==E_VART||rk==E_KW) continue;
            is_real_box = 1;
        }
        if (!is_real_box) continue;
        const char *safe = named_pats[i].safe;
        int dsz = box_data_size(safe);
        if (dsz == 0) {
            A("; box %s: no DATA slots\n", named_pats[i].varname);
            continue;
        }
        A("    global  box_%s_data_template, box_%s_data_size\n", safe, safe);
        box_data_emit_section(safe);
    }
}

static void emit_prolog_program(Program *prog);  /* forward */

void asm_emit(Program *prog, FILE *f) {
    out = f;

    /* Per-file reset — allocate once, zero contents on every call.
     * Without this, multi-file runs carry stale EXPR_t* pointers and table
     * state across files → corrupted labels, SIGSEGV. (M-G-INV-EMIT-FIX) */
    box_data_init();    named_pats_init();  lit_table_init();
    call_slots_init();  str_table_init();
    /* zero array contents (not just counts) to kill stale EXPR_t* pointers */
    memset(named_pats, 0, NAMED_PAT_MAX * sizeof(NamedPat));
    memset(call_slots, 0, CALL_SLOTS_MAX * LBUF);
    memset(_bref_pool, 0, sizeof _bref_pool);
    /* counts */
    uid = 0;        call_uid_counter = 0;
    named_pat_reset();  /* named_pat_count=0, nreturn_fn_count=0 */
    lit_reset();        str_reset();
    var_reset();        /* nvar=0, box_data_count=0, box_ctx_idx=-1 */
    cap_vars_reset();
    /* formatting */
    col = 0;  pending_sep[0] = '\0';  pending_label[0] = '\0';
    _bref_slot = 0;

    /* Pass 1: scan for named pattern assignments */
    scan_named_patterns(prog);

    /* Pass 1b: pre-scan user-function call sites → register per-call .bss slots */
    prescan_ucall(prog);

    /* Pass 2: find first statement with a pattern field (needed for body mode) */
    STMT_t *s = prog ? prog->head : NULL;
    while (s && !s->pattern) s = s->next;

    if (asm_body_mode) {
        /* -asm-body: emit pattern-only body for harness linking */
        if (!s) { emit_null_program(); return; }
        emit_stmt(s);
    } else {
        /* -asm: full program mode — walk all statements */
        emit_program(prog);
    }
}


#include "emit_x64_prolog.c"  /* M-G2-MOVE-PROLOG-ASM-b: Prolog emitter */
