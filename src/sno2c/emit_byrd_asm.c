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
        A("%-20s db ", lit_table[i].label);
        for (int j = 0; j < lit_table[i].len; j++) {
            if (j) A(", ");
            A("%d", (unsigned char)lit_table[i].val[j]);
        }
        A("    ; \"%.*s\"\n", lit_table[i].len, lit_table[i].val);
    }
}

/* -----------------------------------------------------------------------
 * NASM label helpers
 * ----------------------------------------------------------------------- */

#define LBUF 128

static void asmL(const char *lbl) {
    /* emit a label definition */
    A("%s:\n", lbl);
}

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
 * emit_asm_lit — LIT node
 *
 * Mirrors the hand-written oracle lit_hello.s:
 *   α: bounds check; compare; save cursor; advance; goto γ
 *   β: restore cursor; fall to ω
 * ----------------------------------------------------------------------- */

static void emit_asm_lit(const char *s, int n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor,
                          const char *subj, const char *subj_len_sym) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved", alpha);
    bss_add(saved);

    const char *lstr = lit_intern(s, n);

    A("\n; LIT(\"%.*s\")  α=%s\n", n, s, alpha);

    /* α */
    asmL(alpha);
    A("    mov     rax, [%s]\n", cursor);
    A("    add     rax, %d\n", n);
    A("    cmp     rax, [%s]\n", subj_len_sym);
    asmJg(omega);

    if (n == 1) {
        A("    lea     rbx, [rel %s]\n", subj);
        A("    mov     rcx, [%s]\n", cursor);
        A("    movzx   eax, byte [rbx + rcx]\n");
        A("    cmp     al, %d\n", (unsigned char)s[0]);
        asmJne(omega);
    } else {
        A("    lea     rsi, [rel %s]\n", subj);
        A("    mov     rcx, [%s]\n", cursor);
        A("    add     rsi, rcx\n");
        A("    lea     rdi, [rel %s]\n", lstr);
        A("    mov     rcx, %d\n", n);
        A("    repe    cmpsb\n");
        asmJne(omega);
    }

    A("    mov     rax, [%s]\n", cursor);
    A("    mov     [%s], rax\n", saved);
    A("    add     rax, %d\n", n);
    A("    mov     [%s], rax\n", cursor);
    asmJ(gamma);

    /* β */
    A("\n; LIT β=%s\n", beta);
    asmL(beta);
    A("    mov     rax, [%s]\n", saved);
    A("    mov     [%s], rax\n", cursor);
    asmJ(omega);
}

/* -----------------------------------------------------------------------
 * emit_asm_pos / emit_asm_rpos
 * ----------------------------------------------------------------------- */

static void emit_asm_pos(long n,
                          const char *alpha, const char *beta,
                          const char *gamma, const char *omega,
                          const char *cursor) {
    A("\n; POS(%ld)  α=%s\n", n, alpha);
    asmL(alpha);
    A("    cmp     qword [%s], %ld\n", cursor, n);
    asmJne(omega);
    asmJ(gamma);
    A("\n; POS β=%s\n", beta);
    asmL(beta);
    asmJ(omega);
}

static void emit_asm_rpos(long n,
                           const char *alpha, const char *beta,
                           const char *gamma, const char *omega,
                           const char *cursor,
                           const char *subj_len_sym) {
    A("\n; RPOS(%ld)  α=%s\n", n, alpha);
    asmL(alpha);
    A("    mov     rax, [%s]\n", subj_len_sym);
    if (n != 0) A("    sub     rax, %ld\n", n);
    A("    cmp     [%s], rax\n", cursor);
    asmJne(omega);
    asmJ(gamma);
    A("\n; RPOS β=%s\n", beta);
    asmL(beta);
    asmJ(omega);
}

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
    asmL(alpha); asmJ(lα);
    asmL(beta);  asmJ(rβ);

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
    asmL(alpha);
    A("    mov     rax, [%s]\n", cursor);
    A("    mov     [%s], rax\n", cursor_save);
    asmJ(lα);

    /* β: backtrack into right arm */
    asmL(beta); asmJ(rβ);

    /* Inject restore before right_α — called from left_ω */
    /* We emit left_ω trampoline that restores cursor then jumps right_α */
    char left_omega_tramp[LBUF];
    snprintf(left_omega_tramp, LBUF, "alt%d_left_omega", uid);

    emit_asm_node(left, lα, lβ, gamma, left_omega_tramp,
                  cursor, subj, subj_len_sym, depth+1);

    /* left_ω trampoline: restore cursor, jump right_α */
    A("\n; ALT left_ω trampoline\n");
    asmL(left_omega_tramp);
    A("    mov     rax, [%s]\n", cursor_save);
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
    asmL(alpha);
    A("    mov     qword [%s], 0\n", dep);
    /* push cursor onto stack slot 0 */
    A("    lea     rbx, [rel %s]\n", stk);
    A("    mov     rax, [%s]\n", cursor);
    A("    mov     [rbx], rax\n");
    A("    mov     qword [%s], 1\n", dep);
    asmJ(gamma);

    /* β: pop, save cursor-before-rep, try child */
    A("\n; ARBNO β=%s\n", beta);
    asmL(beta);
    A("    mov     rax, [%s]\n", dep);
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
    asmL(child_ok);
    A("    mov     rax, [%s]\n", cursor);
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
    asmL(child_fail);
    asmJ(omega);

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
    snprintf(cap_buf,   LBUF, "cap_%s_%d_buf",      safe, uid);
    snprintf(cap_len,   LBUF, "cap_%s_%d_len",      safe, uid);

    /* register .bss slots */
    bss_add(entry_cur);
    bss_add(cap_len);
    /* cap_buf needs resb 256, not resq 1 — use extra_bss */
    extern char asm_extra_bss[][128];
    extern int  asm_extra_bss_count;
    if (asm_extra_bss_count < 64) {
        snprintf(asm_extra_bss[asm_extra_bss_count++], 128,
                 "%-24s resb 256", cap_buf);
    }

    A("\n; DOL(%s $  %s)  α=%s\n", varname ? varname : "?", safe, alpha);

    /* α: save entry cursor, enter child */
    asmL(alpha);
    A("    mov     rax, [%s]\n", cursor);
    A("    mov     [%s], rax\n", entry_cur);
    asmJ(cα);

    /* β: transparent — backtrack into child */
    asmL(beta);
    asmJ(cβ);

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
    asmL(dol_omega);
    asmJ(omega);
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
        /* expr . var — conditional assignment.
         * Semantics identical to $ in the pattern match phase
         * (assignment deferred to after full match for ., but for
         * our standalone crosscheck harness the distinction doesn't
         * matter yet — emit same as E_DOL). */
        const char *varname = (pat->right && pat->right->sval)
                              ? pat->right->sval : "cap";
        emit_asm_assign(pat->left, varname,
                        alpha, beta, gamma, omega,
                        cursor, subj, subj_len_sym, depth);
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
        } else {
            /* Unimplemented function — emit a comment + omega jump */
            A("\n; UNIMPLEMENTED: %s() → ω\n", pat->sval ? pat->sval : "?");
            asmL(alpha);
            asmL(beta);
            asmJ(omega);
        }
        break;

    default:
        A("\n; UNIMPLEMENTED node kind %d → ω\n", pat->kind);
        asmL(alpha);
        asmL(beta);
        asmJ(omega);
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
 * asm_emit_pattern — emit a single statement's pattern as a standalone
 * match program (for crosscheck testing).
 *
 * Emits:
 *   section .data  — subject, literals
 *   section .bss   — cursor + saved slots
 *   section .text  — _start: init cursor, jmp root_alpha, match_success, match_fail
 *                    + recursive Byrd box code for the pattern
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

    /* Collect data in a temp buffer, emit sections in correct order */
    char *code_buf = NULL; size_t code_size = 0;
    FILE *code_f = open_memstream(&code_buf, &code_size);
    if (!code_f) { asm_emit_null_program(); return; }

    FILE *real_out = asm_out;
    asm_out = code_f;

    /* Fixed names */
    const char *cursor_sym   = "cursor";
    const char *subj_sym     = "subject_data";
    const char *subj_len_sym = "subject_len_val";

    bss_add(cursor_sym);

    /* Determine subject from stmt->subject (E_QLIT) */
    const char *subj_str = "";
    int         subj_len = 0;
    if (stmt->subject && stmt->subject->kind == E_QLIT) {
        subj_str = stmt->subject->sval;
        subj_len = (int)strlen(subj_str);
    }

    /* Emit the pattern tree */
    emit_asm_node(stmt->pattern,
                  "root_alpha", "root_beta",
                  "match_success", "match_fail",
                  cursor_sym, subj_sym, subj_len_sym,
                  0);

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
    A("%-24s resq 1\n", subj_len_sym);  /* runtime-filled subj_len */
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

    /* Paste collected pattern code */
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
 * Public entry point
 * ----------------------------------------------------------------------- */

void asm_emit(Program *prog, FILE *f) {
    asm_out = f;

    /* Find first statement with a pattern */
    STMT_t *s = prog ? prog->head : NULL;
    while (s && !s->pattern) s = s->next;

    if (!s) {
        asm_emit_null_program();
        return;
    }

    asm_emit_pattern(s);
}
