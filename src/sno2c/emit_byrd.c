/*
 * emit_byrd.c — Compiled Byrd box emitter for sno2c
 *
 * C port of:
 *   src/ir/lower.py       (four-port lowering: α/β/γ/ω)
 *   src/codegen/emit_c_byrd.py  (labeled-goto C emission)
 *
 * This replaces emit_pat() for the compiled path.  The stopgap
 * sno_pat_* / engine.c interpreter is NOT used here.
 *
 * API (called from emit.c):
 *   void byrd_emit_pattern(Expr *pat, FILE *out,
 *                          const char *root_name,
 *                          const char *subject_var,
 *                          const char *subject_len_var,
 *                          const char *cursor_var,
 *                          const char *gamma_label,   // on success
 *                          const char *omega_label);  // on failure
 *
 * The caller (emit.c, emit_stmt pattern-match case) is responsible for:
 *   - emitting subject/cursor/subject_len setup
 *   - jumping to root_alpha
 *   - handling gamma_label (match succeeded) and omega_label (match failed)
 *
 * Byrd four-port model per node:
 *   α (alpha) — entry / start
 *   β (beta)  — resume (backtrack re-entry)
 *   γ (gamma) — succeed continuation  [inherited from parent]
 *   ω (omega) — concede continuation  [inherited from parent]
 *
 * Label convention matching the oracle files:
 *   <role>_<uid>_alpha, <role>_<uid>_beta, etc.
 *   role is derived from the node type: lit, pos, rpos, seq, alt, arbno, ...
 *
 * Reference oracles:
 *   test/sprint0/null.c          — skeleton
 *   test/sprint1/lit_hello.c     — LIT
 *   test/sprint2/cat_pos_lit_rpos.c — SEQ (CAT)
 *   test/sprint3/alt_first.c     — ALT
 *   test/sprint4/assign_lit.c    — E_IMM ($ capture)
 *   test/sprint5/arbno_match.c   — ARBNO
 *   src/codegen/emit_c_byrd.py   — Python ground truth
 *   src/ir/lower.py              — lowering ground truth
 */

#include "snoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* -----------------------------------------------------------------------
 * Output helpers
 * ----------------------------------------------------------------------- */

static FILE *byrd_out;

static void B(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(byrd_out, fmt, ap);
    va_end(ap);
}

/* -----------------------------------------------------------------------
 * Label / UID generation
 *
 * Labels: <role>_<uid>_<port>
 * e.g.  lit_7_alpha   cat_12_beta   arbno_3_child_fail
 * ----------------------------------------------------------------------- */

static int byrd_uid_ctr = 0;

static void byrd_uid_reset(void) { byrd_uid_ctr = 0; }
static int  byrd_uid(void)       { return ++byrd_uid_ctr; }

/* Format a label into a static buffer — caller copies if needed */
#define LBUF 128
typedef char Label[LBUF];

static void label_fmt(Label out_l, const char *role, int uid, const char *port) {
    snprintf(out_l, LBUF, "%s_%d_%s", role, uid, port);
}

/* -----------------------------------------------------------------------
 * Forward declaration — byrd_emit is mutually recursive
 * ----------------------------------------------------------------------- */

static void byrd_emit(Expr *pat,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor,
                      int depth);

/* -----------------------------------------------------------------------
 * Static storage declarations
 *
 * The oracle files use static storage for saved cursors, ARBNO stacks, etc.
 * We collect these declarations during the emit pass and print them before
 * the goto chain.
 *
 * Simple approach: emit them inline via a two-pass mechanism.
 * We use a declaration buffer (pre-allocated, written during emit, flushed
 * before the code section).
 * ----------------------------------------------------------------------- */

#define DECL_BUF_MAX  256
#define DECL_LINE_MAX 256

static char decl_buf[DECL_BUF_MAX][DECL_LINE_MAX];
static int  decl_count;

/* Cross-call dedup: tracks statics already emitted in the current C function.
 * Reset by byrd_fn_scope_reset() when emit.c opens a new C function. */
static char fn_seen[DECL_BUF_MAX][DECL_LINE_MAX];
static int  fn_seen_count;

void byrd_fn_scope_reset(void) { fn_seen_count = 0; }

static void decl_reset(void) { decl_count = 0; }

static void decl_add(const char *fmt, ...) {
    if (decl_count >= DECL_BUF_MAX) return;
    char tmp[DECL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, DECL_LINE_MAX, fmt, ap);
    va_end(ap);
    /* Dedup within this pattern's decl buffer */
    for (int i = 0; i < decl_count; i++)
        if (strcmp(decl_buf[i], tmp) == 0) return;
    /* Dedup across patterns in same C function */
    for (int i = 0; i < fn_seen_count; i++)
        if (strcmp(fn_seen[i], tmp) == 0) return;
    memcpy(decl_buf[decl_count++], tmp, DECL_LINE_MAX);
}

static void decl_flush(void) {
    if (decl_count == 0) return;
    B("    /* === static storage === */\n");
    for (int i = 0; i < decl_count; i++) {
        B("static %s;\n", decl_buf[i]);
        /* Record in fn_seen so subsequent patterns skip this decl */
        if (fn_seen_count < DECL_BUF_MAX)
            memcpy(fn_seen[fn_seen_count++], decl_buf[i], DECL_LINE_MAX);
    }
    B("\n");
}

/* -----------------------------------------------------------------------
 * C-safe string literal emission
 * ----------------------------------------------------------------------- */

static void emit_cstr(const char *s) {
    fputc('"', byrd_out);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', byrd_out);
        else if (*s == '\n') { fputs("\\n", byrd_out); continue; }
        else if (*s == '\t') { fputs("\\t", byrd_out); continue; }
        fputc(*s, byrd_out);
    }
    fputc('"', byrd_out);
}

/* -----------------------------------------------------------------------
 * emit_charset_cexpr — build a C expression (written into buf) that evaluates
 * at runtime to a `const char *` charset string, given an Expr* AST node.
 *
 * E_STR  → "literal"          (compile-time constant)
 * E_VAR  → sno_to_str(sno_var_get("name"))
 * E_CONCAT → sno_concat(lhs, rhs)   (both sides recursed)
 * fallback → ""
 * ----------------------------------------------------------------------- */
static void emit_charset_cexpr(Expr *arg, char *buf, int bufsz);

static void emit_charset_cexpr(Expr *arg, char *buf, int bufsz) {
    if (!arg) { snprintf(buf, bufsz, "\"\""); return; }
    switch (arg->kind) {
    case E_STR: {
        /* Build a quoted C literal via emit_cstr logic, inline into buf */
        const char *s = arg->sval ? arg->sval : "";
        int pos = 0;
        buf[pos++] = '"';
        for (; *s && pos < bufsz - 4; s++) {
            if (*s == '"' || *s == '\\') buf[pos++] = '\\';
            else if (*s == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; continue; }
            else if (*s == '\t') { buf[pos++] = '\\'; buf[pos++] = 't'; continue; }
            buf[pos++] = *s;
        }
        buf[pos++] = '"';
        buf[pos]   = '\0';
        return;
    }
    case E_VAR:
        snprintf(buf, bufsz, "sno_to_str(sno_var_get(\"%s\"))",
                 arg->sval ? arg->sval : "");
        return;
    case E_CONCAT: {
        char lbuf[512], rbuf[512];
        emit_charset_cexpr(arg->left,  lbuf, (int)sizeof lbuf);
        emit_charset_cexpr(arg->right, rbuf, (int)sizeof rbuf);
        /* sno_concat is #define'd as sno_concat_sv(SnoVal,SnoVal) in snoc_runtime.h,
         * so we must wrap char* sides in SNO_STR_VAL and extract result with sno_to_str. */
        snprintf(buf, bufsz,
                 "sno_to_str(sno_concat_sv(SNO_STR_VAL(%s), SNO_STR_VAL(%s)))",
                 lbuf, rbuf);
        return;
    }
    case E_KEYWORD:
        /* &UCASE, &LCASE etc — sno_kw(name) = sno_var_get(name) → SnoVal */
        snprintf(buf, bufsz, "sno_to_str(sno_var_get(\"%s\"))",
                 arg->sval ? arg->sval : "");
        return;
    case E_DEREF:
        /* $varname — indirect: left child holds the var name node */
        if (arg->left && arg->left->sval)
            snprintf(buf, bufsz, "sno_to_str(sno_var_get(\"%s\"))", arg->left->sval);
        else
            snprintf(buf, bufsz, "\"\"");
        return;
    default:
        fprintf(stderr, "emit_charset_cexpr: unhandled kind=%d sval=%s\n",
                (int)arg->kind, arg->sval ? arg->sval : "(null)");
        snprintf(buf, bufsz, "\"\"");
        return;
    }
}

/* Forward declarations for leaf emitters */
static void emit_lit(const char *s,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor);
static void emit_pos(long n, const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *cursor);
static void emit_rpos(long n, const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj_len, const char *cursor);
static void emit_len(long n, const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor);
static void emit_tab(long n, const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *cursor);
static void emit_rtab(long n, const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj_len, const char *cursor);
static void emit_any(const char *cs,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor);
static void emit_notany(const char *cs,
                        const char *alpha, const char *beta,
                        const char *gamma, const char *omega,
                        const char *subj, const char *subj_len,
                        const char *cursor);
static void emit_span(const char *cs,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor);
static void emit_break(const char *cs,
                       const char *alpha, const char *beta,
                       const char *gamma, const char *omega,
                       const char *subj, const char *subj_len,
                       const char *cursor);
static void emit_arb(const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor);
static void emit_rem(const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor);
static void emit_fence(const char *alpha, const char *beta,
                       const char *gamma, const char *omega);
static void emit_succeed(const char *alpha, const char *beta,
                         const char *gamma);
static void emit_fail_node(const char *alpha, const char *beta,
                           const char *omega);
static void emit_abort_node(const char *alpha, const char *beta,
                            const char *omega);


static void emit_seq(Expr *left, Expr *right,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth);
static void emit_alt(Expr *left, Expr *right,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth);
static void emit_arbno(Expr *child,
                       const char *alpha, const char *beta,
                       const char *gamma, const char *omega,
                       const char *subj, const char *subj_len,
                       const char *cursor, int depth);
static void emit_imm(Expr *child, const char *varname,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth);
static void emit_cond(Expr *child, const char *varname,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor, int depth);


/* -----------------------------------------------------------------------
 * LIT node
 *
 * Oracle (sprint1/lit_hello.c):
 *   lit1_alpha:
 *       if (cursor + 5 > subject_len) goto lit1_omega;
 *       if (memcmp(subject + cursor, "hello", 5) != 0) goto lit1_omega;
 *       lit1_saved_cursor = cursor;
 *       cursor += 5;
 *       goto lit1_gamma;
 *   lit1_beta:
 *       cursor = lit1_saved_cursor;
 *       goto lit1_omega;
 * ----------------------------------------------------------------------- */

static void emit_lit(const char *s,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor) {
    int n = (int)strlen(s);
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    if (%s + %d > %s) goto %s;\n", cursor, n, subj_len, omega);
    if (n == 1) {
        /* single char: direct comparison */
        char ch = s[0];
        if (ch == '\'' || ch == '\\')
            B("    if (%s[%s] != '\\%c') goto %s;\n", subj, cursor, ch, omega);
        else
            B("    if (%s[%s] != '%c') goto %s;\n", subj, cursor, ch, omega);
    } else {
        B("    if (memcmp(%s + %s, ", subj, cursor);
        emit_cstr(s);
        B(", %d) != 0) goto %s;\n", n, omega);
    }
    B("    %s = %s;\n", saved, cursor);
    B("    %s += %d;\n", cursor, n);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * POS node
 *
 * Oracle (sprint2/cat_pos_lit_rpos.c):
 *   pos1_alpha:
 *       if (cursor != 0) goto pos1_omega;
 *       goto pos1_gamma;
 *   pos1_beta:
 *   pos1_omega:
 *       goto match_fail;
 * ----------------------------------------------------------------------- */

static void emit_pos(long n,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *cursor) {
    B("%s:\n", alpha);
    B("    if (%s != %ld) goto %s;\n", cursor, n, omega);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * RPOS node
 *
 * Oracle:
 *   rpos1_alpha:
 *       if (cursor != subject_len - 0) goto rpos1_omega;
 *       goto rpos1_gamma;
 *   rpos1_beta:
 *       goto rpos1_omega;
 * ----------------------------------------------------------------------- */

static void emit_rpos(long n,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj_len, const char *cursor) {
    B("%s:\n", alpha);
    B("    if (%s != %s - %ld) goto %s;\n", cursor, subj_len, n, omega);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * LEN node
 *
 * Match exactly n characters unconditionally.
 * ----------------------------------------------------------------------- */

static void emit_len(long n,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    if (%s + %ld > %s) goto %s;\n", cursor, n, subj_len, omega);
    B("    %s = %s;\n", saved, cursor);
    B("    %s += %ld;\n", cursor, n);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * TAB node
 *
 * Advance cursor to absolute position n.  Fails if cursor already past n.
 * ----------------------------------------------------------------------- */

static void emit_tab(long n,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    if (%s > %ld) goto %s;\n", cursor, n, omega);
    B("    %s = %s;\n", saved, cursor);
    B("    %s = %ld;\n", cursor, n);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * RTAB node
 *
 * Advance cursor to len - n.  Fails if cursor already past that.
 * ----------------------------------------------------------------------- */

static void emit_rtab(long n,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    if (%s > %s - %ld) goto %s;\n", cursor, subj_len, n, omega);
    B("    %s = %s;\n", saved, cursor);
    B("    %s = %s - %ld;\n", cursor, subj_len, n);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * ANY node — match one char in charset
 * ----------------------------------------------------------------------- */

static void emit_any(const char *cs,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    if (%s >= %s) goto %s;\n", cursor, subj_len, omega);
    B("    if (!strchr(%s, %s[%s])) goto %s;\n", cs, subj, cursor, omega);
    B("    %s = %s;\n", saved, cursor);
    B("    %s++;\n", cursor);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * NOTANY node — match one char NOT in charset
 * ----------------------------------------------------------------------- */

static void emit_notany(const char *cs,
                        const char *alpha, const char *beta,
                        const char *gamma, const char *omega,
                        const char *subj, const char *subj_len,
                        const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    if (%s >= %s) goto %s;\n", cursor, subj_len, omega);
    B("    if (strchr(%s, %s[%s])) goto %s;\n", cs, subj, cursor, omega);
    B("    %s = %s;\n", saved, cursor);
    B("    %s++;\n", cursor);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * SPAN node — match one or more chars in charset
 *
 * On beta: retreat one char at a time (backtrackable).
 * ----------------------------------------------------------------------- */

static void emit_span(const char *cs,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor) {
    char delta[LBUF], start[LBUF];
    snprintf(delta, LBUF, "%s_delta", alpha);
    snprintf(start, LBUF, "%s_start", alpha);
    decl_add("int64_t %s", delta);
    decl_add("int64_t %s", start);

    B("%s:\n", alpha);
    B("    %s = %s;\n", start, cursor);
    B("    while (%s < %s && strchr(%s, %s[%s])) %s++;\n", cursor, subj_len, cs, subj, cursor, cursor);
    B("    %s = %s - %s;\n", delta, cursor, start);
    B("    if (%s == 0) goto %s;\n", delta, omega);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    if (%s <= 1) { %s = %s; goto %s; }\n",
      delta, cursor, start, omega);
    B("    %s--; %s--;\n", delta, cursor);
    B("    goto %s;\n", gamma);
}

/* -----------------------------------------------------------------------
 * BREAK node — match zero or more chars NOT in charset, stopping before hit
 *
 * Deterministic: no backtrack (beta → omega).
 * ----------------------------------------------------------------------- */

static void emit_break(const char *cs,
                       const char *alpha, const char *beta,
                       const char *gamma, const char *omega,
                       const char *subj, const char *subj_len,
                       const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    %s = %s;\n", saved, cursor);
    B("    while (%s < %s && !strchr(%s, %s[%s])) %s++;\n", cursor, subj_len, cs, subj, cursor, cursor);
    B("    if (%s >= %s) { %s = %s; goto %s; }\n",
      cursor, subj_len, cursor, saved, omega);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * ARB node — match any number of characters (greedy, backtrackable)
 *
 * On alpha: succeed at current position (zero chars), offering to extend.
 * On beta:  advance one more char and try again.
 * ----------------------------------------------------------------------- */

static void emit_arb(const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    %s = %s;\n", saved, cursor);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    if (%s >= %s) goto %s;\n", saved, subj_len, omega);
    B("    %s++;\n", saved);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", gamma);
}

/* -----------------------------------------------------------------------
 * REM node — match the rest of the subject
 * ----------------------------------------------------------------------- */

static void emit_rem(const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    B("%s:\n", alpha);
    B("    %s = %s;\n", saved, cursor);
    B("    %s = %s;\n", cursor, subj_len);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    %s = %s;\n", cursor, saved);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * SEQ (CAT) node
 *
 * Wiring (from lower.py _emit Seq):
 *   α     → left_α
 *   β     → right_β   (first try resuming right; if exhausted → left_β)
 *   left succeeds  → right_α
 *   left fails     → ω
 *   right succeeds → γ
 *   right fails    → left_β  (backtrack left)
 *
 * Oracle (sprint2):
 *   root_alpha:  goto cat_l1_alpha;
 *   cat_l1_*     POS
 *   cat_l1_gamma: goto cat_r2_alpha;
 *   cat_l1_omega: goto root_beta;
 *   cat_r2_*     ...
 *   cat_r2_omega: goto cat_l1_beta;
 * ----------------------------------------------------------------------- */

static void emit_seq(Expr *left, Expr *right,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth) {
    int uid = byrd_uid();

    Label left_alpha, left_beta, right_alpha, right_beta;
    label_fmt(left_alpha,  "cat_l", uid, "alpha");
    label_fmt(left_beta,   "cat_l", uid, "beta");
    label_fmt(right_alpha, "cat_r", uid, "alpha");
    label_fmt(right_beta,  "cat_r", uid, "beta");

    /* α → left_α */
    B("%s: /* CAT — enter left */\n", alpha);
    B("    goto %s;\n", left_alpha);

    /* β → right_β (resume right first) */
    B("%s:\n", beta);
    B("    goto %s;\n", right_beta);

    /* lower left: succeed→right_α, concede→ω */
    byrd_emit(left,
              left_alpha, left_beta,
              right_alpha, omega,
              subj, subj_len, cursor, depth + 1);

    /* lower right: succeed→γ, concede→left_β */
    byrd_emit(right,
              right_alpha, right_beta,
              gamma, left_beta,
              subj, subj_len, cursor, depth + 1);
}

/* -----------------------------------------------------------------------
 * ALT node
 *
 * Wiring (from lower.py _emit Alt):
 *   α     → left_α
 *   β     → IndirectGoto(t)  — resume active arm's β
 *   left_γ  → save t=left_β; goto γ
 *   left_ω  → right_α
 *   right_γ → save t=right_β; goto γ
 *   right_ω → ω
 *
 * For C (emit_c_byrd.py): t is an int local; switch on t.
 *
 * Oracle (sprint3/alt_first.c):
 *   cat_l3_alpha:  [ALT — try left]
 *       goto alt_l5_alpha;
 *   alt_l5_alpha:   [left arm]   -> cat_r4_alpha (gamma)
 *   alt_l5_beta:    [backtrack]  -> alt_r6_alpha (right)
 *   alt_r6_alpha:   [right arm]  -> cat_r4_alpha (gamma)
 *   alt_r6_beta:    [backtrack]  -> cat_r2_beta  (omega)
 *   cat_l3_beta:    goto alt_r6_beta;
 * ----------------------------------------------------------------------- */

static void emit_alt(Expr *left, Expr *right,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth) {
    int uid = byrd_uid();

    Label left_alpha, left_beta, right_alpha, right_beta;
    label_fmt(left_alpha,  "alt_l", uid, "alpha");
    label_fmt(left_beta,   "alt_l", uid, "beta");
    label_fmt(right_alpha, "alt_r", uid, "alpha");
    label_fmt(right_beta,  "alt_r", uid, "beta");

    /* α → left_α */
    B("%s: /* ALT — try left */\n", alpha);
    B("    goto %s;\n", left_alpha);

    /* β → left_beta first, then right_beta
     * Per oracle: cat_l3_beta: goto alt_r6_beta;
     * The active arm's beta is wired directly: left_omega→right_alpha,
     * right_omega→omega; β goes to right_beta (second arm backtrack).
     * This matches the oracle pattern exactly. */
    B("%s:\n", beta);
    B("    goto %s;\n", right_beta);

    /* lower left: succeed→γ, concede→right_α */
    byrd_emit(left,
              left_alpha, left_beta,
              gamma, right_alpha,
              subj, subj_len, cursor, depth + 1);

    /* lower right: succeed→γ, concede→ω */
    byrd_emit(right,
              right_alpha, right_beta,
              gamma, omega,
              subj, subj_len, cursor, depth + 1);
}

/* -----------------------------------------------------------------------
 * ARBNO node
 *
 * Wiring (from lower.py _emit Arbno and oracle sprint5/arbno_match.c):
 *
 *   cat_l3_alpha:
 *       cat_l3_depth = -1;
 *       goto cat_r4_alpha;           // ARBNO: zero matches → succeed
 *   cat_l3_beta:                     // ARBNO resume: extend by one
 *       cat_l3_depth++;
 *       if (cat_l3_depth >= 64) goto cat_r2_beta;
 *       cat_l3_cursors[cat_l3_depth] = cursor;
 *       goto arbno_c5_alpha;
 *   arbno_c5_alpha: [child alpha]
 *   arbno_c5_beta:  [child beta]     → cat_l3_child_fail
 *   cat_l3_child_ok:  goto cat_r4_alpha;   // child matched → ARBNO ok
 *   cat_l3_child_fail:
 *       cursor = cat_l3_cursors[cat_l3_depth];
 *       cat_l3_depth--;
 *       goto cat_r2_beta;            // child failed → ARBNO fails
 *
 * The key insight from the oracle:
 *   - ARBNO starts by succeeding with ZERO matches (depth=-1, goto gamma)
 *   - beta extends: save cursor, try child
 *   - child_ok → gamma (ARBNO succeeds again with one more match)
 *   - child_fail → restore cursor, depth--, omega (ARBNO gives up)
 * ----------------------------------------------------------------------- */

static void emit_arbno(Expr *child,
                       const char *alpha, const char *beta,
                       const char *gamma, const char *omega,
                       const char *subj, const char *subj_len,
                       const char *cursor, int depth) {
    int uid = byrd_uid();

    Label child_alpha, child_beta, child_ok, child_fail;
    label_fmt(child_alpha, "arbno_c", uid, "alpha");
    label_fmt(child_beta,  "arbno_c", uid, "beta");
    snprintf(child_ok,   LBUF, "%s_child_ok",   alpha);
    snprintf(child_fail, LBUF, "%s_child_fail", alpha);

    char depth_var[LBUF], stack_var[LBUF];
    snprintf(depth_var, LBUF, "%s_depth", alpha);
    snprintf(stack_var, LBUF, "%s_cursors", alpha);
    decl_add("int %s", depth_var);
    decl_add("int64_t %s[64]", stack_var);

    /* α: zero matches → succeed */
    B("%s:\n", alpha);
    B("    %s = -1;\n", depth_var);
    B("    goto %s;              /* ARBNO: zero matches -> succeed */\n", gamma);

    /* β: extend by one — save cursor, try child */
    B("%s:\n", beta);
    B("    %s++;\n", depth_var);
    B("    if (%s >= 64) goto %s;  /* stack overflow */\n", depth_var, omega);
    B("    %s[%s] = %s;\n", stack_var, depth_var, cursor);
    B("    goto %s;\n", child_alpha);

    /* child_ok: child matched → ARBNO succeeds again */
    B("%s:\n", child_ok);
    B("    goto %s;              /* child matched -> ARBNO succeeds again */\n", gamma);

    /* child_fail: child failed → restore cursor, depth--, ARBNO fails */
    B("%s:\n", child_fail);
    B("    %s = %s[%s];\n", cursor, stack_var, depth_var);
    B("    %s--;\n", depth_var);
    B("    goto %s;              /* child failed -> ARBNO fails */\n", omega);

    /* lower child: succeed→child_ok, concede→child_fail */
    byrd_emit(child,
              child_alpha, child_beta,
              child_ok, child_fail,
              subj, subj_len, cursor, depth + 1);
}

/* -----------------------------------------------------------------------
 * E_IMM ($ capture) node
 *
 * Pattern node:  <child> $ var
 * On child success: record span [start..cursor] into var, goto gamma.
 * On beta: undo the assignment (restore cursor is sufficient — var may linger).
 *
 * Oracle (sprint4/assign_lit.c):
 *   cat_l5_alpha:
 *       cat_l5_start = cursor;
 *       goto assign_c7_alpha;
 *   [child]  →  cat_l5_do_assign
 *   cat_l5_do_assign:
 *       var_OUTPUT.ptr = subject + cat_l5_start;
 *       var_OUTPUT.len = cursor - cat_l5_start;
 *       sno_output(var_OUTPUT);
 *       goto cat_r6_alpha;
 *   cat_l5_beta:
 *       goto assign_c7_beta;
 * ----------------------------------------------------------------------- */

static void emit_imm(Expr *child, const char *varname,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth) {
    int uid = byrd_uid();

    Label child_alpha, child_beta;
    label_fmt(child_alpha, "assign_c", uid, "alpha");
    label_fmt(child_beta,  "assign_c", uid, "beta");

    char start_var[LBUF], do_assign[LBUF];
    snprintf(start_var, LBUF, "%s_start", alpha);
    snprintf(do_assign, LBUF, "%s_do_assign", alpha);

    decl_add("int64_t %s", start_var);

    /* alpha: record start, enter child */
    B("%s:\n", alpha);
    B("    %s = %s;\n", start_var, cursor);
    B("    goto %s;\n", child_alpha);

    /* lower child: succeed → do_assign, concede → omega */
    byrd_emit(child,
              child_alpha, child_beta,
              do_assign, omega,
              subj, subj_len, cursor, depth + 1);

    /* do_assign: write span into variable */
    B("%s:\n", do_assign);
    if (strcasecmp(varname, "OUTPUT") == 0) {
        /* OUTPUT is a special IO stream — use sno_output_str */
        B("    { int64_t _len = %s - %s;\n", cursor, start_var);
        B("      char *_os = malloc(_len + 1); memcpy(_os, %s + %s, _len); _os[_len] = 0;\n",
          subj, start_var);
        B("      sno_output_str(_os); free(_os); }\n");
    } else {
        /* Regular variable: declare str_t and assign */
        char var_decl[LBUF];
        snprintf(var_decl, LBUF, "str_t var_%s", varname);
        decl_add("%s", var_decl);
        B("    var_%s.ptr = %s + %s;\n", varname, subj, start_var);
        B("    var_%s.len = %s - %s;\n", varname, cursor, start_var);
    }
    B("    goto %s;\n", gamma);

    /* beta: backtrack into child */
    B("%s:\n", beta);
    B("    goto %s;\n", child_beta);
}

/* -----------------------------------------------------------------------
 * E_COND (. capture) node
 *
 * Like E_IMM but the assignment is CONDITIONAL — it fires when we reach
 * the gamma of the enclosing match.  For the static compiled path we treat
 * it identically to E_IMM (assign on child success, readable by outer code).
 * ----------------------------------------------------------------------- */

static void emit_cond(Expr *child, const char *varname,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor, int depth) {
    /* Same as IMM for the compiled static path */
    emit_imm(child, varname, alpha, beta, gamma, omega,
             subj, subj_len, cursor, depth);
}

/* -----------------------------------------------------------------------
 * FENCE node (no-argument form)
 *
 * Succeeds once, cannot be resumed (beta → omega immediately).
 * ----------------------------------------------------------------------- */

static void emit_fence(const char *alpha, const char *beta,
                       const char *gamma, const char *omega) {
    B("%s: /* FENCE */\n", alpha);
    B("    goto %s;\n", gamma);

    B("%s:\n", beta);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * SUCCEED node
 *
 * Always succeeds.  Repeats forever on backtrack.
 * ----------------------------------------------------------------------- */

static void emit_succeed(const char *alpha, const char *beta,
                         const char *gamma) {
    B("%s: /* SUCCEED */\n", alpha);
    B("%s:\n", beta);
    B("    goto %s;\n", gamma);
}

/* -----------------------------------------------------------------------
 * FAIL node
 *
 * Always fails.
 * ----------------------------------------------------------------------- */

static void emit_fail_node(const char *alpha, const char *beta,
                           const char *omega) {
    B("%s: /* FAIL */\n", alpha);
    B("%s:\n", beta);
    B("    goto %s;\n", omega);
}

/* -----------------------------------------------------------------------
 * ABORT node
 *
 * Terminates the entire match (not just backtrack).
 * In the static path we jump to omega for now; callers handle abort
 * at the top level.
 * ----------------------------------------------------------------------- */

static void emit_abort_node(const char *alpha, const char *beta,
                            const char *omega) {
    B("%s: /* ABORT */\n", alpha);
    B("%s:\n", beta);
    B("    goto %s;\n", omega);  /* TODO: propagate abort signal */
}

/* -----------------------------------------------------------------------
 * emit_simple_val — emit a SnoVal C expression for a simple Expr node.
 * Used by E_REDUCE to pass left/right as runtime values.
 * Handles: E_STR, E_INT, E_VAR, E_CALL(nTop), E_NULL/NULL.
 * Falls back to SNO_NULL_VAL for anything complex.
 * ----------------------------------------------------------------------- */

static void emit_simple_val(Expr *e) {
    if (!e) { B("SNO_NULL_VAL"); return; }
    switch (e->kind) {
    case E_STR:
        /* strip surrounding quotes if present — sval is already unquoted */
        B("SNO_STR_VAL(\"%s\")", e->sval ? e->sval : "");
        return;
    case E_INT:
        B("SNO_INT_VAL(%ld)", e->ival);
        return;
    case E_VAR:
        B("sno_var_get(\"%s\")", e->sval ? e->sval : "");
        return;
    case E_CALL:
        if (e->sval && strcasecmp(e->sval, "nTop") == 0)
            { B("SNO_INT_VAL(sno_ntop())"); return; }
        if (e->sval && strcasecmp(e->sval, "nInc") == 0)
            { B("SNO_INT_VAL((sno_ninc(), sno_ntop()))"); return; }
        if (e->sval && strcasecmp(e->sval, "nPop") == 0)
            { B("SNO_INT_VAL((sno_npop(), 0))"); return; }
        /* generic: fall through to sno_apply */
        B("sno_apply(\"%s\", NULL, 0)", e->sval ? e->sval : "");
        return;
    default:
        B("SNO_NULL_VAL /* unhandled emit_simple_val kind %d */", (int)e->kind);
        return;
    }
}

/* -----------------------------------------------------------------------
 * Main dispatch — byrd_emit
 *
 * Recursively lowers `pat` with the given four-port labels and emits C.
 * depth is used for recursion guard only.
 * ----------------------------------------------------------------------- */

static void byrd_emit(Expr *pat,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor,
                      int depth) {
    if (!pat) {
        /* epsilon: succeed immediately */
        B("%s: /* epsilon */\n", alpha);
        B("    goto %s;\n", gamma);
        B("%s:\n", beta);
        B("    goto %s;\n", omega);
        return;
    }

    if (depth > 128) {
        B("/* emit_byrd: depth limit hit */\n");
        B("%s: goto %s;\n", alpha, omega);
        B("%s: goto %s;\n", beta,  omega);
        return;
    }

    switch (pat->kind) {

    /* ---------------------------------------------------------------- E_STR */
    case E_STR:
        emit_lit(pat->sval, alpha, beta, gamma, omega,
                 subj, subj_len, cursor);
        return;

    /* ---------------------------------------------------------------- E_CALL — builtins */
    case E_CALL: {
        const char *n = pat->sval;

        /* LEN(n) */
        if (strcasecmp(n, "LEN") == 0 && pat->nargs >= 1) {
            long v = (pat->args[0]->kind == E_INT) ? pat->args[0]->ival : 1;
            emit_len(v, alpha, beta, gamma, omega, subj_len, cursor);
            return;
        }
        /* POS(n) */
        if (strcasecmp(n, "POS") == 0 && pat->nargs >= 1) {
            long v = (pat->args[0]->kind == E_INT) ? pat->args[0]->ival : 0;
            emit_pos(v, alpha, beta, gamma, omega, cursor);
            return;
        }
        /* RPOS(n) */
        if (strcasecmp(n, "RPOS") == 0 && pat->nargs >= 1) {
            long v = (pat->args[0]->kind == E_INT) ? pat->args[0]->ival : 0;
            emit_rpos(v, alpha, beta, gamma, omega, subj_len, cursor);
            return;
        }
        /* TAB(n) */
        if (strcasecmp(n, "TAB") == 0 && pat->nargs >= 1) {
            long v = (pat->args[0]->kind == E_INT) ? pat->args[0]->ival : 0;
            emit_tab(v, alpha, beta, gamma, omega, cursor);
            return;
        }
        /* RTAB(n) */
        if (strcasecmp(n, "RTAB") == 0 && pat->nargs >= 1) {
            long v = (pat->args[0]->kind == E_INT) ? pat->args[0]->ival : 0;
            emit_rtab(v, alpha, beta, gamma, omega, subj_len, cursor);
            return;
        }
        /* ANY(cs) */
        if (strcasecmp(n, "ANY") == 0 && pat->nargs >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->args[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_any(cs, alpha, beta, gamma, omega, subj, subj_len, cursor);
            return;
        }
        /* NOTANY(cs) */
        if (strcasecmp(n, "NOTANY") == 0 && pat->nargs >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->args[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_notany(cs, alpha, beta, gamma, omega, subj, subj_len, cursor);
            return;
        }
        /* SPAN(cs) */
        if (strcasecmp(n, "SPAN") == 0 && pat->nargs >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->args[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_span(cs, alpha, beta, gamma, omega, subj, subj_len, cursor);
            return;
        }
        /* BREAK(cs) */
        if (strcasecmp(n, "BREAK") == 0 && pat->nargs >= 1) {
            char cs_buf[1024]; emit_charset_cexpr(pat->args[0], cs_buf, (int)sizeof cs_buf); const char *cs = cs_buf;
            emit_break(cs, alpha, beta, gamma, omega, subj, subj_len, cursor);
            return;
        }
        /* ARB */
        if (strcasecmp(n, "ARB") == 0) {
            emit_arb(alpha, beta, gamma, omega, subj_len, cursor);
            return;
        }
        /* REM */
        if (strcasecmp(n, "REM") == 0) {
            emit_rem(alpha, beta, gamma, omega, subj_len, cursor);
            return;
        }
        /* ARBNO(child_pat) */
        if (strcasecmp(n, "ARBNO") == 0 && pat->nargs >= 1) {
            emit_arbno(pat->args[0], alpha, beta, gamma, omega,
                       subj, subj_len, cursor, depth);
            return;
        }
        /* FENCE() — no arg form */
        if (strcasecmp(n, "FENCE") == 0 && pat->nargs == 0) {
            emit_fence(alpha, beta, gamma, omega);
            return;
        }
        /* FENCE(pat) — with argument: match pat, then fence */
        if (strcasecmp(n, "FENCE") == 0 && pat->nargs >= 1) {
            /* FENCE(p): match p then prevent backtrack into it */
            int uid = byrd_uid();
            Label ca, cb;
            label_fmt(ca, "fence_p", uid, "alpha");
            label_fmt(cb, "fence_p", uid, "beta");
            /* alpha: enter child */
            B("%s: /* FENCE(p) */\n", alpha);
            B("    goto %s;\n", ca);
            /* lower child: on success → fence_after (can't backtrack) */
            /* use a synthetic gamma for the child that acts as a fence */
            char fence_after[LBUF];
            snprintf(fence_after, LBUF, "fence_after_%d", uid);
            byrd_emit(pat->args[0],
                      ca, cb,
                      fence_after, omega,
                      subj, subj_len, cursor, depth + 1);
            B("%s:\n", fence_after);
            B("    goto %s;\n", gamma);
            /* beta: cut — prevent backtrack */
            B("%s:\n", beta);
            B("    goto %s;\n", omega);
            return;
        }
        /* SUCCEED */
        if (strcasecmp(n, "SUCCEED") == 0) {
            emit_succeed(alpha, beta, gamma);
            return;
        }
        /* FAIL */
        if (strcasecmp(n, "FAIL") == 0) {
            emit_fail_node(alpha, beta, omega);
            return;
        }
        /* ABORT */
        if (strcasecmp(n, "ABORT") == 0) {
            emit_abort_node(alpha, beta, omega);
            return;
        }

        /* nPush() — push counter stack, always succeed (side-effect only) */
        if (strcasecmp(n, "nPush") == 0) {
            B("%s: sno_npush(); goto %s;\n", alpha, gamma);
            B("%s: goto %s;\n", beta, omega);
            return;
        }
        /* nInc() — increment top counter, always succeed */
        if (strcasecmp(n, "nInc") == 0) {
            B("%s: sno_ninc(); goto %s;\n", alpha, gamma);
            B("%s: goto %s;\n", beta, omega);
            return;
        }
        /* nDec() — decrement top counter, always succeed */
        if (strcasecmp(n, "nDec") == 0) {
            B("%s: sno_ndec(); goto %s;\n", alpha, gamma);
            B("%s: goto %s;\n", beta, omega);
            return;
        }
        /* nPop() — pop counter stack, always succeed */
        if (strcasecmp(n, "nPop") == 0) {
            B("%s: sno_npop(); goto %s;\n", alpha, gamma);
            B("%s: goto %s;\n", beta, omega);
            return;
        }
        /* nTop() — read top counter, always succeed (value available via sno_ntop()) */
        if (strcasecmp(n, "nTop") == 0) {
            B("%s: (void)sno_ntop(); goto %s;\n", alpha, gamma);
            B("%s: goto %s;\n", beta, omega);
            return;
        }

        /* Fallback: unknown call — epsilon */
        B("%s: /* unknown call: %s — epsilon */\n", alpha, n);
        B("    goto %s;\n", gamma);
        B("%s:\n", beta);
        B("    goto %s;\n", omega);
        return;
    }

    /* ----------------------------------------------------------- E_CONCAT (CAT / SEQ) */
    case E_CONCAT:
        emit_seq(pat->left, pat->right,
                 alpha, beta, gamma, omega,
                 subj, subj_len, cursor, depth);
        return;

    /* ---------------------------------------------------------------- E_ALT */
    case E_ALT:
        emit_alt(pat->left, pat->right,
                 alpha, beta, gamma, omega,
                 subj, subj_len, cursor, depth);
        return;

    /* ---------------------------------------------------------------- E_IMM ($ assign) */
    case E_IMM: {
        const char *varname = (pat->right && pat->right->kind == E_VAR)
                              ? pat->right->sval : "OUTPUT";
        emit_imm(pat->left, varname,
                 alpha, beta, gamma, omega,
                 subj, subj_len, cursor, depth);
        return;
    }

    /* --------------------------------------------------------------- E_COND (. assign) */
    case E_COND: {
        const char *varname = (pat->right && pat->right->kind == E_VAR)
                              ? pat->right->sval : "OUTPUT";
        emit_cond(pat->left, varname,
                  alpha, beta, gamma, omega,
                  subj, subj_len, cursor, depth);
        return;
    }

    /* ---------------------------------------------------------------- E_VAR (pattern var) */
    case E_VAR:
        /* Pattern variable: acts as epsilon in the static path */
        B("%s: /* pat var %s — epsilon */\n", alpha, pat->sval);
        B("    goto %s;\n", gamma);
        B("%s:\n", beta);
        B("    goto %s;\n", omega);
        return;

    /* ---------------------------------------------------------------- E_DEREF (deferred ref) */
    case E_DEREF: {
        /* *varname — indirect pattern reference.
         * At runtime: get the value of 'varname', treat it as a pattern,
         * match it anchored at the current cursor position.
         * On success: advance cursor to the end of match → gamma.
         * On failure: restore cursor → omega.
         *
         * Byrd ports:
         *   alpha: attempt the match
         *   beta:  resume → restore cursor and fail (no backtracking into the sub-pattern)
         *   gamma: inherited success continuation
         *   omega: inherited failure continuation
         */
        char saved[LBUF];
        snprintf(saved, LBUF, "deref_%d_saved_cursor", byrd_uid());
        decl_add("int64_t %s", saved);

        /* E_DEREF node: operand is in pat->left (created by unop()).
         * For *varname, pat->left->kind == E_VAR, pat->left->sval is the name.
         * For *$expr or other complex deref, fall back to empty. */
        const char *varname = NULL;
        if (pat->left && pat->left->kind == E_VAR)
            varname = pat->left->sval;
        if (!varname) varname = "";

        B("%s: {\n", alpha);
        B("    SnoVal _deref_pat = sno_var_get(%s%s%s);\n", "\"", varname, "\"");
        B("    int _deref_new_cur = sno_match_pattern_at(_deref_pat, %s, (int)%s, (int)%s);\n",
          subj, subj_len, cursor);
        B("    if (_deref_new_cur < 0) goto %s;\n", omega);
        B("    %s = %s;\n", saved, cursor);
        B("    %s = (int64_t)_deref_new_cur;\n", cursor);
        B("    goto %s;\n", gamma);
        B("}\n");

        B("%s:\n", beta);
        B("    %s = %s;\n", cursor, saved);
        B("    goto %s;\n", omega);
        return;
    }

    /* --------------------------------------------------------------- E_REDUCE (& operator) */
    case E_REDUCE: {
        /* "type & count" — calls Reduce(type, count) at match time.
         * Reduce() pops `count` trees from the linked-list stack ($'@S')
         * and pushes one combined tree of `type`.  Always succeeds as a
         * side-effect node (like nPush/nPop).
         * left  = type  (E_STR like 'snoParse', or NULL)
         * right = count (E_CALL(nTop), E_INT, etc.)              */
        B("%s: /* E_REDUCE & */\n", alpha);
        B("    { SnoVal _reduce_args[2] = {");
        emit_simple_val(pat->left);
        B(", ");
        emit_simple_val(pat->right);
        B("};\n");
        B("      sno_apply(\"Reduce\", _reduce_args, 2); }\n");
        B("    goto %s;\n", gamma);
        B("%s: goto %s;\n", beta, omega);
        return;
    }

    /* ---------------------------------------------------------------- default */
    default:
        B("%s: /* unhandled expr kind %d — epsilon */\n", alpha, (int)pat->kind);
        B("    goto %s;\n", gamma);
        B("%s:\n", beta);
        B("    goto %s;\n", omega);
        return;
    }
}

/* =======================================================================
 * Public API
 *
 * byrd_emit_pattern — emit a complete standalone match block:
 *
 *   goto <root>_alpha;
 *   [declarations]
 *   [Byrd box labeled-goto C]
 *   <gamma_label>:   (match succeeded)
 *   <omega_label>:   (match failed)
 *
 * Called from emit.c in the pattern-match statement case.
 *
 * Parameters:
 *   pat          — the pattern Expr* (subject pattern field)
 *   out          — output FILE*
 *   root_name    — prefix for root labels (e.g. "root_1")
 *   subject_var  — C expression for the subject string pointer
 *   subj_len_var — C expression for subject length (int64_t)
 *   cursor_var   — C lvalue for the cursor (int64_t, modified in place)
 *   gamma_label  — C label to jump to on match success
 *   omega_label  — C label to jump to on match failure
 * ======================================================================= */

void byrd_emit_pattern(Expr *pat, FILE *out_file,
                       const char *root_name,
                       const char *subject_var,
                       const char *subj_len_var,
                       const char *cursor_var,
                       const char *gamma_label,
                       const char *omega_label) {
    byrd_out = out_file;
    /* Save counter before first (declaration) pass so second (code) pass
     * uses the same uid sequence — never reset to 0 so multiple patterns
     * in the same compilation unit never collide. */
    int byrd_uid_saved = byrd_uid_ctr;
    decl_reset();

    /* Root labels */
    char root_alpha[LBUF], root_beta[LBUF];
    snprintf(root_alpha, LBUF, "%s_alpha", root_name);
    snprintf(root_beta,  LBUF, "%s_beta",  root_name);

    /* First pass: collect declarations by running emit into /dev/null,
     * then second pass: emit the real code.
     *
     * Simpler approach used by the Python version: emit declarations
     * inline at the start using a two-buffer approach.
     *
     * We use a temporary FILE* for the code body, write declarations
     * first into out_file, then splice code. */

    /* Collect code into a temp buffer */
    char   *code_buf  = NULL;
    size_t  code_size = 0;
    FILE   *code_file = open_memstream(&code_buf, &code_size);
    if (!code_file) {
        fprintf(out_file, "/* emit_byrd: open_memstream failed */\n");
        return;
    }

    byrd_out = code_file;
    byrd_uid_ctr = byrd_uid_saved;  /* rewind to same start for code pass */
    decl_reset();

    /* Emit root entry */
    fprintf(code_file, "\n/* ===== pattern: %s ===== */\n", root_name);

    /* Lower the pattern — byrd_emit emits both alpha and beta labels */
    byrd_emit(pat,
              root_alpha, root_beta,
              gamma_label, omega_label,
              subject_var, subj_len_var, cursor_var,
              0);

    /* Note: root_beta is emitted by byrd_emit above for all node types.
     * Do NOT emit it again here — that causes duplicate label errors. */

    fflush(code_file);
    fclose(code_file);

    /* Now emit to out_file: declarations FIRST, then goto, then code.
     * C99 forbids jumping over variable-length declarations, so all
     * static decls must appear before the first goto in the function. */
    byrd_out = out_file;
    decl_flush();            /* static declarations — before any goto */
    B("    goto %s;\n", root_alpha);

    /* Splice code body */
    if (code_buf && code_size > 0)
        fwrite(code_buf, 1, code_size, out_file);

    free(code_buf);
}

/* =======================================================================
 * byrd_emit_standalone — emit a complete standalone test program
 * matching the sprint oracle style.  Used by tests that compile
 * a .sno file with sno2c in byrd mode and want a standalone executable.
 * ======================================================================= */

void byrd_emit_standalone(Expr *pat, FILE *out_file,
                          const char *subject,
                          const char *root_name) {
    fprintf(out_file,
        "#include <stdint.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n"
        "#include \"../../src/runtime/runtime.h\"\n\n");

    fprintf(out_file, "int main(void) {\n");
    fprintf(out_file, "    const char *subject     = \"%s\";\n", subject);
    fprintf(out_file, "    int64_t     subject_len = %d;\n", (int)strlen(subject));
    fprintf(out_file, "    int64_t     cursor      = 0;\n");
    fprintf(out_file, "    (void)cursor; (void)subject; (void)subject_len;\n\n");

    byrd_emit_pattern(pat, out_file,
                      root_name,
                      "subject", "subject_len", "cursor",
                      "root_MATCH_SUCCESS",
                      "root_MATCH_FAIL");

    fprintf(out_file,
        "    root_MATCH_SUCCESS: return 0; /* matched */\n"
        "    root_MATCH_FAIL:    return 1; /* failed  */\n"
        "}\n");
}
