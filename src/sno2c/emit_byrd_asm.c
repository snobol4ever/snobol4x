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

/* -----------------------------------------------------------------------
 * Output helpers
 * ----------------------------------------------------------------------- */

static FILE *asm_out;

static void A(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(asm_out, fmt, ap);
    va_end(ap);
}

/* -----------------------------------------------------------------------
 * UID counter — never resets so labels never collide across patterns
 * ----------------------------------------------------------------------- */

static int asm_uid_ctr = 0;
static int asm_uid(void) { return asm_uid_ctr++; }

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
    A("\nsection .bss\n\n");
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
    A("\nsection .data\n\n");
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
    /* emit a standalone label line (for raw-asm blocks and trampolines) */
    A("%s:\n", lbl);
}

/* asmLB(lbl, instr) — label and pre-built instruction string on one line */
static void asmLB(const char *lbl, const char *instr) {
    char lc[LBUF+2];
    snprintf(lc, sizeof lc, "%s:", lbl);
    while (*instr == ' ') instr++;
    A("%-28s%s", lc, instr);
}

/* ALF — emit "label:  instruction\n" where instruction is printf-formatted.
 * This is the main beauty helper: one source call = one .s line. */
static char _alf_ibuf[1024];
#define ALF(lbl, fmt, ...) do { \
    snprintf(_alf_ibuf, sizeof _alf_ibuf, fmt, ##__VA_ARGS__); \
    asmLB(lbl, _alf_ibuf); \
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

    A("\n; LIT(\"%.*s\")  α=%s\n", n, s, alpha);
    if (n == 1) {
        ALF(alpha, "LIT_ALPHA1  %d, %s, %s, %s, %s, %s, %s\n",
            (unsigned char)s[0], saved, cursor, subj, subj_len_sym, gamma, omega);
    } else {
        const char *lstr = lit_intern(s, n);
        ALF(alpha, "LIT_ALPHA   %s, %d, %s, %s, %s, %s, %s, %s\n",
            lstr, n, saved, cursor, subj, subj_len_sym, gamma, omega);
    }
    A("; LIT β\n");
    ALF(beta, "LIT_BETA    %s, %s, %s\n", saved, cursor, omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_pos / emit_asm_rpos
 * ----------------------------------------------------------------------- */

static void emit_asm_pos(long n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor) {
    A("\n; POS(%ld)  α=%s\n", n, alpha);
    ALF(alpha, "POS_ALPHA   %ld, %s, %s, %s\n", n, cursor, gamma, omega);
    ALF(beta,  "POS_BETA    %s, %s\n", cursor, omega);
}

static void emit_asm_rpos(long n,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj_len_sym) {
    A("\n; RPOS(%ld)  α=%s\n", n, alpha);
    ALF(alpha, "RPOS_ALPHA  %ld, %s, %s, %s, %s\n", n, cursor, subj_len_sym, gamma, omega);
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
struct AsmNamedPat {
    char varname[128];
    char safe[128];
    char alpha_lbl[128];
    char beta_lbl[128];
    char ret_gamma[128];
    char ret_omega[128];
    EXPR_t *pat;
};

static const AsmNamedPat *asm_named_lookup(const char *varname);
static void emit_asm_named_ref(const AsmNamedPat *np,
                                const char *alpha, const char *beta,
                                const char *gamma, const char *omega);

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

    A("\n; SEQ α=%s\n", alpha);
    ALF(alpha, "jmp     %s\n", lα);
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

    A("\n; ALT α=%s\n", alpha);
    /* α: save cursor, try left */
    ALF(alpha, "mov     rax, [%s]\n\n", cursor);
    A("    mov     [%s], rax\n", cursor_save);
    asmJ(lα);

    /* β: backtrack into right arm */
    ALF(beta, "jmp     %s\n", rβ);

    /* Inject restore before right_α — called from left_ω */
    /* We emit left_ω trampoline that restores cursor then jumps right_α */
    char left_omega_tramp[LBUF];
    snprintf(left_omega_tramp, LBUF, "alt%d_left_omega", uid);

    emit_asm_node(left, lα, lβ, gamma, left_omega_tramp,
                  cursor, subj, subj_len_sym, depth+1);

    /* left_ω trampoline: restore cursor, jump right_α */
    A("\n; ALT left_ω trampoline\n");
    ALF(left_omega_tramp, "mov     rax, [%s]\n\n", cursor_save);
    A("    mov     [%s], rax\n", cursor);
    asmJ(rα);

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

    A("\n; ARBNO α=%s\n", alpha);

    /* α: initialize depth=0, push cursor, goto γ */
    ALF(alpha, "mov     qword [%s], 0\n\n", dep);
    /* push cursor onto stack slot 0 */
    A("    lea     rbx, [rel %s]\n", stk);
    A("    mov     rax, [%s]\n", cursor);
    A("    mov     [rbx], rax\n");
    A("    mov     qword [%s], 1\n", dep);
    asmJ(gamma);

    /* β: pop, save cursor-before-rep, try child */
    A("\n; ARBNO β=%s\n", beta);
    ALF(beta, "mov     rax, [%s]\n\n", dep);
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
    A("\n; ARBNO child_ok\n");
    ALF(child_ok, "mov     rax, [%s]\n\n", cursor);
    A("    mov     rbx, [%s]\n", cur_before);
    A("    cmp     rax, rbx\n");
    asmJe(omega);                  /* stalled → ω */
    A("    mov     rdx, [%s]\n", dep);
    A("    lea     rbx, [rel %s]\n", stk);
    A("    mov     [rbx + rdx*8], rax\n");
    A("    inc     qword [%s]\n", dep);
    asmJ(gamma);

    /* child_fail: ω */
    A("\n; ARBNO child_fail\n");
    ALF(child_fail, "jmp     %s\n", omega);

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

    A("\n; ANY(\"%.*s\")  α=%s\n", cslen, charset, alpha);
    ALF(alpha, "ANY_ALPHA   %s, %d, %s, %s, %s, %s, %s, %s\n\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    A("; ANY β=%s\n", beta);
    ALF(beta, "ANY_BETA    %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; NOTANY(\"%.*s\")  α=%s\n", cslen, charset, alpha);
    ALF(alpha, "NOTANY_ALPHA %s, %d, %s, %s, %s, %s, %s, %s\n\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    A("; NOTANY β=%s\n", beta);
    ALF(beta, "NOTANY_BETA  %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; SPAN(\"%.*s\")  α=%s\n", cslen, charset, alpha);
    ALF(alpha, "SPAN_ALPHA  %s, %d, %s, %s, %s, %s, %s, %s\n\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    A("; SPAN β=%s\n", beta);
    ALF(beta, "SPAN_BETA   %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; BREAK(\"%.*s\")  α=%s\n", cslen, charset, alpha);
    ALF(alpha, "BREAK_ALPHA %s, %d, %s, %s, %s, %s, %s, %s\n\n",
      clabel, cslen, saved, cursor, subj, subj_len_sym, gamma, omega);
    A("; BREAK β=%s\n", beta);
    ALF(beta, "BREAK_BETA  %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; LEN(%ld)  α=%s\n", n, alpha);
    ALF(alpha, "LEN_ALPHA   %ld, %s, %s, %s, %s, %s\n\n", n, saved, cursor, subj_len_sym, gamma, omega);
    A("; LEN β=%s\n", beta);
    ALF(beta, "LEN_BETA    %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; TAB(%ld)  α=%s\n", n, alpha);
    ALF(alpha, "TAB_ALPHA   %ld, %s, %s, %s, %s\n\n", n, saved, cursor, gamma, omega);
    A("; TAB β=%s\n", beta);
    ALF(beta, "TAB_BETA    %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; RTAB(%ld)  α=%s\n", n, alpha);
    ALF(alpha, "RTAB_ALPHA  %ld, %s, %s, %s, %s, %s\n\n", n, saved, cursor, subj_len_sym, gamma, omega);
    A("; RTAB β=%s\n", beta);
    ALF(beta, "RTAB_BETA   %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; REM  α=%s\n", alpha);
    ALF(alpha, "REM_ALPHA   %s, %s, %s, %s\n\n", saved, cursor, subj_len_sym, gamma);
    A("; REM β=%s\n", beta);
    ALF(beta, "REM_BETA    %s, %s, %s\n\n", saved, cursor, omega);
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

    A("\n; ARB  α=%s\n", alpha);
    ALF(alpha, "mov     rax, [%s]\n\n", cursor);
    A("    mov     [%s], rax\n", arb_start);
    A("    mov     qword [%s], 0\n", arb_step);
    /* cursor stays at start (0-length match first) */
    asmJ(gamma);

    A("\n; ARB β=%s\n", beta);
    ALF(beta, "mov     rax, [%s]\n\n", arb_step);
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

    /* derive safe varname prefix (strip non-alnum) */
    char safe[64]; int si = 0;
    if (varname) {
        for (const char *p = varname; *p && si < 60; p++)
            safe[si++] = (isalnum((unsigned char)*p) || *p=='_') ? *p : '_';
    }
    safe[si] = '\0';
    if (!si) { safe[0]='v'; safe[1]='\0'; }

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

    A("\n; DOL(%s $  %s)  α=%s\n", varname ? varname : "?", safe, alpha);

    /* α: save entry cursor, enter child */
    ALF(alpha, "mov     rax, [%s]\n\n", cursor);
    A("    mov     [%s], rax\n", entry_cur);
    asmJ(cα);

    /* β: transparent — backtrack into child */
    ALF(beta, "jmp     %s\n", cβ);

    /* Emit child subtree — child's γ goes to dol_gamma, child's ω to dol_omega */
    emit_asm_node(child, cα, cβ, dol_gamma, dol_omega,
                  cursor, subj, subj_len_sym, depth + 1);

    /* dol_γ: compute span, copy to cap_buf, proceed to outer γ */
    A("\n; DOL γ — capture span into %s\n", cap_buf);
    asmL(dol_gamma);
    /* rax = cursor - entry_cursor = span length */
    A("    mov     rax, [%s]\n", cursor);
    A("    mov     rbx, [%s]\n", entry_cur);
    A("    sub     rax, rbx\n");
    A("    mov     [%s], rax\n", cap_len);
    /* rep movsb: rsi = &subj[entry_cursor], rdi = cap_buf, rcx = len */
    A("    lea     rsi, [rel %s]\n", subj);
    A("    add     rsi, rbx\n");
    A("    lea     rdi, [rel %s]\n", cap_buf);
    A("    mov     rcx, rax\n");
    A("    rep     movsb\n");
    asmJ(gamma);

    /* dol_ω: child failed — no assignment, propagate failure */
    A("\n; DOL ω — child failed, no capture\n");
    ALF(dol_omega, "jmp     %s\n", omega);
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
            A("\n; UNRESOLVED named pattern ref: %s → ω\n", varname);
            asmL(alpha);
            ALF(beta, "jmp     %s\n", omega);
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
         * Look it up in the named-pattern registry and call it. */
        const char *varname = (pat->left && pat->left->sval)
                              ? pat->left->sval
                              : (pat->sval ? pat->sval : NULL);
        if (varname) {
            const AsmNamedPat *np = asm_named_lookup(varname);
            if (np) {
                emit_asm_named_ref(np, alpha, beta, gamma, omega);
            } else {
                A("\n; E_INDR unresolved: %s → ω\n", varname);
                asmL(alpha); ALF(beta, "jmp     %s\n", omega);
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
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_asm_pos(n, alpha, beta, gamma, omega, cursor);
        } else if (pat->sval && strcasecmp(pat->sval, "RPOS") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            long n = (arg->kind == E_ILIT) ? arg->ival : 0;
            emit_asm_rpos(n, alpha, beta, gamma, omega, cursor, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "ARBNO") == 0 && pat->nargs == 1) {
            emit_asm_arbno(pat->args[0],
                           alpha, beta, gamma, omega,
                           cursor, subj, subj_len_sym, depth);
        } else if (pat->sval && strcasecmp(pat->sval, "ANY") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
            int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
            emit_asm_any(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "NOTANY") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
            int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
            emit_asm_notany(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "SPAN") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
            int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
            emit_asm_span(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
        } else if (pat->sval && strcasecmp(pat->sval, "BREAK") == 0 && pat->nargs == 1) {
            EXPR_t *arg = pat->args[0];
            const char *cs = (arg->kind == E_QLIT && arg->sval) ? arg->sval : "";
            int cslen = (arg->kind == E_QLIT && arg->sval) ? (int)strlen(arg->sval) : 0;
            emit_asm_break(cs, cslen, alpha, beta, gamma, omega, cursor, subj, subj_len_sym);
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
    /* build safe label fragment */
    int si = 0;
    for (const char *p = varname; *p && si < 60; p++)
        cv->safe[si++] = (isalnum((unsigned char)*p) || *p == '_') ? *p : '_';
    cv->safe[si] = '\0';
    if (!si) { cv->safe[0] = 'v'; cv->safe[1] = '\0'; }
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

/* Make a label-safe version of a SNOBOL4 variable name */
static void asm_safe_name(const char *src, char *dst, int dstlen) {
    int i = 0;
    for (const char *p = src; *p && i < dstlen - 1; p++)
        dst[i++] = (isalnum((unsigned char)*p) || *p == '_') ? *p : '_';
    dst[i] = '\0';
    if (!i) { dst[0] = 'v'; dst[1] = '\0'; }
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
    snprintf(e->alpha_lbl, ASM_NAMED_NAMELEN, "P_%s_alpha", e->safe);
    snprintf(e->beta_lbl,  ASM_NAMED_NAMELEN, "P_%s_beta",  e->safe);
    snprintf(e->ret_gamma, ASM_NAMED_NAMELEN, "P_%s_ret_gamma", e->safe);
    snprintf(e->ret_omega, ASM_NAMED_NAMELEN, "P_%s_ret_omega", e->safe);
    e->pat = pat;
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

    A("\n; REF(%s) α=%s\n", np->varname, alpha);

    /* α: load continuations, jump to named pattern α */
    ALF(alpha, "lea     rax, [rel %s]\n\n", glbl);
    A("    mov     [%s], rax\n", np->ret_gamma);
    A("    lea     rax, [rel %s]\n", olbl);
    A("    mov     [%s], rax\n", np->ret_omega);
    A("    jmp     %s\n", np->alpha_lbl);

    /* β: reload continuations (caller may have changed them), jump to β */
    A("\n; REF(%s) β=%s\n", np->varname, beta);
    ALF(beta, "lea     rax, [rel %s]\n\n", glbl);
    A("    mov     [%s], rax\n", np->ret_gamma);
    A("    lea     rax, [rel %s]\n", olbl);
    A("    mov     [%s], rax\n", np->ret_omega);
    A("    jmp     %s\n", np->beta_lbl);

    /* γ and ω trampolines — named pattern jumps here, we forward */
    A("\n%s:\n", glbl);
    asmJ(gamma);
    A("%s:\n", olbl);
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
    if (!np->pat) {
        /* No pattern expression — emit stubs that always fail */
        A("\n; Named pattern %s — no expression, always fails\n", np->varname);
        asmL(np->alpha_lbl);
        ALF(np->beta_lbl, "jmp     [%s]\n\n", np->ret_omega);
        return;
    }

    /* The named pattern's inner γ and ω connect back via the ret_ slots */
    char inner_gamma[LBUF], inner_omega[LBUF];
    snprintf(inner_gamma, LBUF, "patdef_%s_gamma", np->safe);
    snprintf(inner_omega, LBUF, "patdef_%s_omega", np->safe);

    A("\n; ============ Named pattern: %s ============\n", np->varname);

    /* α entry — initial match */
    A("; %s (α entry)\n", np->alpha_lbl);
    emit_asm_node(np->pat,
                  np->alpha_lbl, np->beta_lbl,
                  inner_gamma, inner_omega,
                  cursor, subj, subj_len_sym,
                  1);

    /* γ trampoline: indirect jump to caller's continuation */
    A("\n%s:\n", inner_gamma);
    A("    jmp     [%s]\n", np->ret_gamma);

    /* ω trampoline */
    A("%s:\n", inner_omega);
    A("    jmp     [%s]\n", np->ret_omega);
}

/* -----------------------------------------------------------------------
 * asm_emit_null_program — Sprint A0 fallback
 * ----------------------------------------------------------------------- */

static void asm_emit_null_program(void) {
    A("; generated by sno2c -asm\n");
    A("    global _start\n\n");
    A("section .text\n\n");
    A("_start:\n");
    A("    mov     eax, 60\n");
    A("    xor     edi, edi\n");
    A("    syscall\n");
}

/* -----------------------------------------------------------------------
 * asm_scan_named_patterns — walk the program and register all
 * pattern assignments (VAR = <pattern-expr>) before emitting.
 * ----------------------------------------------------------------------- */

static void asm_scan_named_patterns(Program *prog) {
    asm_named_reset();
    if (!prog) return;
    for (STMT_t *s = prog->head; s; s = s->next) {
        /* A pattern assignment: subject is a variable, no pattern field,
         * has_eq is set, replacement holds the pattern expression.
         * E.g.   ASTAR = ARBNO(LIT("a"))
         * In sno2c IR: subject=E_VART("ASTAR"), pattern=NULL, replacement=<expr> */
        if (s->subject && s->subject->kind == E_VART &&
            s->has_eq && s->replacement && !s->pattern) {
            asm_named_register(s->subject->sval, s->replacement);
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
    A("; assemble: nasm -f elf64 out.s -o out.o && ld out.o -o out\n\n");
    A("    global _start\n");

    /* .data */
    A("\nsection .data\n\n");
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
        A("%-20s db ", lit_table[i].label);
        for (int j = 0; j < lit_table[i].len; j++) {
            if (j) A(", ");
            A("%d", (unsigned char)lit_table[i].val[j]);
        }
        A("\n");
    }

    /* .bss */
    A("\nsection .bss\n\n");
    A("%-24s resq 1\n", subj_len_sym);
    for (int i = 0; i < bss_count; i++)
        A("%-24s resq 1\n", bss_slots[i]);
    extra_bss_emit();

    /* .text */
    A("\nsection .text\n\n");
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
    A("; link:     gcc -no-pie -o prog body.o snobol4_asm_harness.o\n\n");

    A("%%include \"snobol4_asm.mac\"\n\n");

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
        A("\nsection .data\n\n");
        for (int i = 0; i < lit_count; i++) {
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
        A("\nsection .bss\n\n");
        for (int i = 0; i < bss_count; i++)
            A("%-24s resq 1\n", bss_slots[i]);
        extra_bss_emit();
        cap_vars_emit_bss();
    }

    /* .text — real emit; uid counter continues from where dry run left off,
     *         so all generated labels are identical to the collected names  */
    A("\nsection .text\n\n");

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
    snprintf(e->label, sizeof e->label, "S_%d", prog_str_count);
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
        if (rbp_off == -16) {
            A("    mov     [rbp-16], rax\n");
            A("    mov     [rbp-8],  rdx\n");
        } else {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 0;
    }
    case E_ILIT:
        A("    LOAD_INT    %ld\n", (long)e->ival);
        if (rbp_off == -16) {
            A("    mov     [rbp-16], rax\n");
            A("    mov     [rbp-8],  rdx\n");
        } else {
            A("    mov     [rbp%+d], rax\n", rbp_off);
            A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        }
        return 0;
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
        A("    mov     [rbp%+d], rax\n", rbp_off);
        A("    mov     [rbp%+d], rdx\n", rbp_off+8);
        return 1;
    }
    case E_OR:
    case E_CONC: {
        /* Binary pattern operators: ALT (|) and CONCAT (juxtaposition).
         * Treat as 2-arg function call: ALT(left,right) or CONCAT(left,right).
         * Allocate 2-slot args array on stack (32 bytes), evaluate each child,
         * store into array, then call stmt_apply. */
        const char *opname = (e->kind == E_OR) ? "ALT" : "CONCAT";
        const char *fnlab  = prog_str_intern(opname);
        /* Reserve 32 bytes (2 × 16-byte DESCR_t) for args array */
        A("    sub     rsp, 32\n");
        /* Evaluate left child → [rsp+0] */
        prog_emit_expr(e->left,  rbp_off);
        A("    STORE_ARG32 0\n");
        /* Evaluate right child → [rsp+16] */
        prog_emit_expr(e->right, rbp_off);
        A("    STORE_ARG32 16\n");
        /* Call stmt_apply(name, args_ptr, 2) */
        A("    APPLY_FN_N  %s, 2\n", fnlab);
        A("    add     rsp, 32\n");
        A("    mov     [rbp%+d], rax\n", rbp_off);
        A("    mov     [rbp%+d], rdx\n", rbp_off + 8);
        return 1; /* may fail */
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
static void emit_jmp(const char *tgt, const char *fallthrough) {
    if (!tgt || !*tgt) {
        if (fallthrough) A("    jmp     %s\n", fallthrough);
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
    if (strcasecmp(target,"END")==0 || strcasecmp(target,"RETURN")==0 ||
        strcasecmp(target,"FRETURN")==0 || strcasecmp(target,"NRETURN")==0 ||
        strcasecmp(target,"SRETURN")==0) {
        A("    GOTO_ALWAYS  L_SNO_END     ; %s\n", target);
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

    prog_str_reset();
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
        if (asm_named[i].pat) {
            EKind rk = asm_named[i].pat->kind;
            if (rk == E_QLIT || rk == E_ILIT || rk == E_FLIT || rk == E_NULV)
                continue; /* plain value assignment — not a real named pattern */
        }
        bss_add(asm_named[i].ret_gamma);
        bss_add(asm_named[i].ret_omega);
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
        for (STMT_t *sp = prog->head; sp; sp = sp->next) {
            if (sp->is_end) break;
            if (!sp->pattern) continue;
            /* real pass uses stmt_uid++ (not asm_uid) — no uid to consume here */
            /* throwaway port labels — only bss_add/lit_intern side-effects matter */
            emit_asm_node(sp->pattern,
                          "P_dry_a","P_dry_b","P_dry_g","P_dry_o",
                          "cursor", "subject_data", "subject_len_val", 0);
        }
        /* also dry-run named pattern bodies */
        for (int i = 0; i < asm_named_count; i++) {
            EKind rk = asm_named[i].pat ? asm_named[i].pat->kind : E_NULV;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV) continue;
            emit_asm_named_def(&asm_named[i], "cursor","subject_data","subject_len_val");
        }
        fclose(devnull);
        asm_out = real_out_p3;
        asm_uid_ctr = uid_save; /* reset so real pass generates same labels */
    }

    /* ---- emit header ---- */
    A("; generated by sno2c -asm (program mode)\n");
    A("; link: gcc -no-pie out.s stmt_rt.o snobol4.o mock_includes.o\n");
    A(";             snobol4_pattern.o mock_engine.o -lgc -lm -o prog\n\n");
    A("%%include \"snobol4_asm.mac\"\n\n");
    A("    global  main\n");
    A("    extern  stmt_init, stmt_strval, stmt_intval\n");
    A("    extern  stmt_get, stmt_set, stmt_output, stmt_input\n");
    A("    extern  stmt_concat, stmt_is_fail, stmt_finish\n");
    A("    extern  stmt_apply, stmt_goto_dispatch\n");
    A("    extern  stmt_setup_subject, stmt_apply_replacement\n");
    A("    extern  stmt_set_capture\n");
    A("    global  cursor, subject_data, subject_len_val\n");
    A("\n");
    /* subject_data/subject_len_val/cursor: defined here, exported for stmt_rt.c */


    /* .data emitted at end of file (after .text) so late prog_str_intern()
     * calls from emit time are captured. See prog_str_emit_data() below. */

    /* ---- .note.GNU-stack ---- */
    A("section .note.GNU-stack noalloc noexec nowrite progbits\n\n");

    /* ---- .bss ---- */
    /* Always emit .bss — subject_data is always needed for pattern stmts */
    A("section .bss\n\n");
    for (int i = 0; i < bss_count; i++)
        A("%-24s resq 1\n", bss_slots[i]);
    /* Byrd box scratch slots (saved_cursor, arbno stack, etc.) */
    extra_bss_emit();
    /* DOL $ capture buffers (cap_VAR_buf/cap_VAR_len) */
    cap_vars_emit_bss();
    /* subject buffer for pattern matching */
    A("%-24s resb 65536\n", "subject_data");
    A("\n");

    /* ---- .text ---- */
    A("section .text\n\n");
    A("main:\n");
    A("    PROG_INIT\n\n");

    /* Walk statements */
    int stmt_uid = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) {
            /* END statement — fall through to L_SNO_END at bottom */
            A("    GOTO_ALWAYS  L_SNO_END\n");
            break;
        }

        int uid = stmt_uid++;
        char next_lbl[64]; snprintf(next_lbl, sizeof next_lbl, "L_sn_%d", uid);
        char sfail_lbl[64]; snprintf(sfail_lbl, sizeof sfail_lbl, "L_sf_%d", uid);

        /* STMT_SEP before label so label has an instruction following it */
        A("\n    STMT_SEP\n");
        if (s->label) {
            A("%s:\n", prog_label_nasm(s->label));
            { int _id = prog_label_id(s->label);
              if (_id >= 0) prog_label_defined[_id] = 1; }
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
            /* Evaluate subject (the LHS or expression) */
            if (s->subject) {
                int may_fail = prog_emit_expr(s->subject, -16);
                /* If subject may fail AND there are S/F targets, dispatch */
                if (may_fail && !s->has_eq && (id_s >= 0 || id_f >= 0)) {
                    A("    IS_FAIL_BRANCH16  %s\n", id_f >= 0 ? sfail_lbl : next_lbl);
                    /* success path */
                    emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                    /* failure path */
                    if (id_f >= 0) {
                        A("%s:\n", sfail_lbl);
                        emit_jmp(tgt_f, next_lbl);
                    }
                    A("%s:\n", next_lbl);
                    continue;
                }
            }

            /* If has_eq: assign replacement to subject variable */
            if (s->has_eq && s->replacement && s->subject &&
                s->subject->kind == E_VART) {
                /* RHS */
                prog_emit_expr(s->replacement, -32);
                /* stmt_is_fail check on RHS */
                A("    IS_FAIL_BRANCH  %s\n", id_f >= 0 ? sfail_lbl : next_lbl);
                /* Assign: if subject is OUTPUT, call stmt_output */
                if (strcasecmp(s->subject->sval, "OUTPUT") == 0) {
                    A("    SET_OUTPUT\n");
                } else {
                    /* generic variable set */
                    const char *vlab = prog_str_intern(s->subject->sval);
                    A("    SET_VAR     %s\n", vlab);
                }
                /* success path */
                emit_jmp(tgt_s ? tgt_s : tgt_u, next_lbl);
                /* failure path */
                if (id_f >= 0) {
                    A("%s:\n", sfail_lbl);
                    emit_jmp(tgt_f, next_lbl);
                }
            } else {
                /* expression-only or complex case: just evaluate and branch */
                if (id_u >= 0) emit_jmp(tgt_u, next_lbl);
            }
            A("%s:\n", next_lbl);
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
        /* stmt_setup_subject — copies into subject_data[], resets cursor */
        A("    SETUP_SUBJECT_FROM16\n");

        /* -- start the pattern -- */
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
        A("%s:\n", pat_gamma);
        /* Materialise DOL/NAM captures into SNOBOL4 variables */
        for (int ci = 0; ci < cap_var_count; ci++) {
            const char *vnlab = prog_str_intern(cap_vars[ci].varname);
            A("    SET_CAPTURE %s, %s, %s\n",
              vnlab, cap_vars[ci].buf_sym, cap_vars[ci].len_sym);
        }
        if (s->has_eq && s->replacement && s->subject &&
            s->subject->kind == E_VART) {
            /* apply replacement → subject variable */
            prog_emit_expr(s->replacement, -32);
            const char *vlab = prog_str_intern(s->subject->sval);
            A("    APPLY_REPL  %s\n", vlab);
        }
        emit_jmp(tgt_s, next_lbl);

        /* -- omega: match failed -- */
        A("%s:\n", pat_omega);
        emit_jmp(tgt_f, next_lbl);

        A("%s:\n", next_lbl);
    }

    /* Safety net end — also serves as RETURN/FRETURN/END target */
    A("\nL_SNO_END:\n");
    A("    PROG_END\n");

    /* ---- Emit lit_table strings used by Byrd box LIT nodes ---- */
    if (lit_count > 0) {
        A("\nsection .data\n\n");
        for (int i = 0; i < lit_count; i++) {
            A("%-20s db ", lit_table[i].label);
            for (int j = 0; j < lit_table[i].len; j++) {
                if (j) A(", ");
                A("%d", (unsigned char)lit_table[i].val[j]);
            }
            A("\n");
        }
    }

    /* ---- Named pattern bodies — must be in .text (executable) ---- */
    A("\nsection .text\n");
    A("\n; ======== named pattern bodies ========\n");
    for (int i = 0; i < asm_named_count; i++) {
        if (asm_named[i].pat) {
            EKind rk = asm_named[i].pat->kind;
            if (rk==E_QLIT||rk==E_ILIT||rk==E_FLIT||rk==E_NULV) continue;
        }
        emit_asm_named_def(&asm_named[i],
                           "cursor","subject_data","subject_len_val");
    }

    A("\nsection .text\n"); /* stubs must also be in .text */
    /* ---- Stub definitions for referenced-but-undefined labels ----
     * These are dangling gotos (e.g. :F(error) with no "error" label defined,
     * or computed goto dispatch labels not yet implemented).
     * Map them to _SNO_END so the program assembles and terminates cleanly. */
    A("\n; --- stub labels (dangling gotos / computed goto TBD) ---\n");
    for (int i = 0; i < prog_label_count; i++) {
        if (!prog_label_defined[i]) {
            A("%s:  ; STUB → _SNO_END (dangling or computed goto)\n",
              prog_label_nasm(prog_labels[i]));
            A("    GOTO_ALWAYS  L_SNO_END\n");
        }
    }

    /* ---- .data emitted last so all prog_str_intern() calls are captured ---- */
    A("\nsection .data\n\n");
    prog_str_emit_data();
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
