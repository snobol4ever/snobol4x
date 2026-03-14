/*
 * emit_byrd.c — Compiled Byrd box emitter for sno2c
 *
 * C port of:
 *   src/ir/lower.py       (four-port lowering: α/β/γ/ω)
 *   src/codegen/emit_c_byrd.py  (labeled-goto C emission)
 *
 * This replaces emit_pat() for the compiled path.  The stopgap
 * pat_* / engine.c interpreter is NOT used here.
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
 * The caller (emit.c, emit_stmt pattern-mtch case) is responsible for:
 *   - emitting subject/cursor/subject_len setup
 *   - jumping to root_α
 *   - handling gamma_label (mtch succeeded) and omega_label (mtch failed)
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

#include "sno2c.h"
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

/* byrd_cs — C static name for a SNOBOL4 variable (mirrors cs() in emit.c).
 * Prepends '_', replaces non-alnum/_ with '_'. Static buffer — use before next call. */
static const char *byrd_cs(const char *s) {
    static char buf[520];
    int i = 0, j = 0;
    buf[j++] = '_';
    for (; s[i] && j < 510; i++) {
        unsigned char c = (unsigned char)s[i];
        buf[j++] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    buf[j] = '\0';
    return buf;
}

/* Three-column pretty printer — shared with emit.c via emit_pretty.h */
#define PRETTY_OUT byrd_out
#include "emit_pretty.h"

/* -----------------------------------------------------------------------
 * Label / UID generation
 *
 * Labels: <role>_<uid>_<port>
 * e.g.  lit_7_α   cat_12_β   arbno_3_child_fail
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
 * Named pattern registry
 *
 * Tracks which pattern variable names have been compiled to C functions
 * by byrd_emit_named_pattern().  E_DEREF (*varname) checks here: if the
 * name is registered, emit a direct function call instead of going through
 * match_pattern_at().
 *
 * Registry entries:
 *   varname  — the SNOBOL4 variable name (e.g. "snoParse")
 *   typename — the C struct typedef name  (e.g. "pat_snoParse_t")
 *   fnname   — the C function name        (e.g. "pat_snoParse")
 * ----------------------------------------------------------------------- */

#define NAMED_PAT_MAX 128
#define NAMED_PAT_NAMELEN 128

typedef struct {
    char varname[NAMED_PAT_NAMELEN];
    char typename[NAMED_PAT_NAMELEN];
    char fnname[NAMED_PAT_NAMELEN];
    int  emitted;   /* 1 after byrd_emit_named_pattern has run for this name */
} NamedPat;

static NamedPat named_pat_registry[NAMED_PAT_MAX];
static int      named_pat_count = 0;

void byrd_named_pat_reset(void) { named_pat_count = 0; }

static void named_pat_register(const char *varname,
                                const char *typename,
                                const char *fnname) {
    /* Deduplicate */
    for (int i = 0; i < named_pat_count; i++)
        if (strcmp(named_pat_registry[i].varname, varname) == 0) return;
    if (named_pat_count >= NAMED_PAT_MAX) return;
    NamedPat *e = &named_pat_registry[named_pat_count++];
    snprintf(e->varname,  NAMED_PAT_NAMELEN, "%s", varname);
    snprintf(e->typename, NAMED_PAT_NAMELEN, "%s", typename);
    snprintf(e->fnname,   NAMED_PAT_NAMELEN, "%s", fnname);
}

/* Pre-register a name without emitting anything — so all names are known
 * before any function body is emitted (handles forward/mutual references). */
void byrd_preregister_named_pattern(const char *varname) {
    char safe[NAMED_PAT_NAMELEN];
    const char *s = varname;
    int i = 0;
    for (; *s && i < (int)(sizeof safe)-1; s++, i++)
        safe[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
    safe[i] = '\0';
    char fnname[NAMED_PAT_NAMELEN], tyname[NAMED_PAT_NAMELEN];
    snprintf(fnname, sizeof fnname, "pat_%s", safe);
    snprintf(tyname, sizeof tyname, "pat_%s_t", safe);
    named_pat_register(varname, tyname, fnname);
}

/* Emit typedef forward-declarations for all registered named pattern structs.
 * Must be called BEFORE byrd_emit_named_fwdecls so the struct pointer types
 * in function prototypes are valid incomplete-type pointers.
 *   typedef struct pat_X_t pat_X_t;   (incomplete — enough for pointer use)
 */
void byrd_emit_named_typedecls(FILE *out_file) {
    for (int i = 0; i < named_pat_count; i++) {
        fprintf(out_file,
            "typedef struct %s %s;\n",
            named_pat_registry[i].typename,
            named_pat_registry[i].typename);
    }
    if (named_pat_count > 0) fprintf(out_file, "\n");
}

/* Emit forward declarations for ALL registered named patterns.
 * Must be called once, after all byrd_preregister_named_pattern calls,
 * and BEFORE any byrd_emit_named_pattern calls.
 * Signature: pat_X(subj, slen, cur_ptr, zz, entry) */
void byrd_emit_named_fwdecls(FILE *out_file) {
    for (int i = 0; i < named_pat_count; i++) {
        fprintf(out_file,
            "static SnoVal %s(const char *, int64_t, int64_t *, %s **, int);\n",
            named_pat_registry[i].fnname,
            named_pat_registry[i].typename);
    }
    if (named_pat_count > 0) fprintf(out_file, "\n");
}

static const NamedPat *named_pat_lookup(const char *varname) {
    for (int i = 0; i < named_pat_count; i++)
        if (strcmp(named_pat_registry[i].varname, varname) == 0)
            return &named_pat_registry[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * Local storage declarations
 *
 * Two modes:
 *   Normal (in_named_pat == 0): emit `static TYPE name;` as before.
 *   Struct  (in_named_pat == 1): collect into a struct typedef, emit
 *     `#define name z->name` aliases so the code body is unchanged.
 *
 * Child frame pointers for E_DEREF calls inside a named pattern are
 * collected in child_decl_buf (separate buffer, same lifetime).
 * ----------------------------------------------------------------------- */

#define DECL_BUF_MAX  256
#define DECL_LINE_MAX 256

static char decl_buf[DECL_BUF_MAX][DECL_LINE_MAX];
static int  decl_count;

/* Child frame pointer declarations: "struct pat_Y_t *Y_z_UID"
 * Collected during byrd_emit of a named pattern body. */
static char child_decl_buf[DECL_BUF_MAX][DECL_LINE_MAX];
static int  child_decl_count;

/* Cross-call dedup: tracks statics already emitted in the current C function.
 * Reset by byrd_fn_scope_reset() when emit.c opens a new C function. */
static char fn_seen[DECL_BUF_MAX][DECL_LINE_MAX];
static int  fn_seen_count;

/* Struct mode — set by byrd_emit_named_pattern */
static int  in_named_pat = 0;

void byrd_fn_scope_reset(void) { fn_seen_count = 0; }

static void decl_reset(void) {
    decl_count = 0;
    child_decl_count = 0;
}

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
    /* In non-struct mode: dedup across patterns in same C function */
    if (!in_named_pat) {
        for (int i = 0; i < fn_seen_count; i++)
            if (strcmp(fn_seen[i], tmp) == 0) return;
    }
    memcpy(decl_buf[decl_count++], tmp, DECL_LINE_MAX);
}

/* Add a child frame pointer field to child_decl_buf.
 * fmt must be a C declaration like "struct pat_Y_t *Y_z_7".
 * Returns the field name (last token) so the caller can reference z->fieldname. */
static void child_decl_add(const char *decl) {
    if (child_decl_count >= DECL_BUF_MAX) return;
    for (int i = 0; i < child_decl_count; i++)
        if (strcmp(child_decl_buf[i], decl) == 0) return;
    strncpy(child_decl_buf[child_decl_count++], decl, DECL_LINE_MAX - 1);
}

/* Extract the field name from a declaration string "TYPE name" or "TYPE *name"
 * or "TYPE name[N]". Returns a pointer into the string at the start of the name. */
/* Walk backwards over one character that is part of a C identifier,
 * including ASCII alnum/underscore AND UTF-8 multibyte sequences
 * (for Greek port suffixes α β γ ω which are 2-byte UTF-8). */
static int is_ident_byte(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 0x80;
}

static const char *decl_field_name(const char *decl) {
    /* If there's an array bracket, the name ends just before '[' */
    const char *bracket = strchr(decl, '[');
    const char *end = bracket ? bracket : decl + strlen(decl);
    /* Walk backwards over the identifier (ASCII + UTF-8 multibyte) */
    while (end > decl && is_ident_byte((unsigned char)end[-1])) end--;
    return end;
}

/* Length of the field name (needed when there's an array suffix). */
static int decl_field_name_len(const char *decl) {
    const char *bracket = strchr(decl, '[');
    const char *end = bracket ? bracket : decl + strlen(decl);
    const char *start = end;
    while (start > decl && is_ident_byte((unsigned char)start[-1])) start--;
    return (int)(end - start);
}

/* Emit normal static storage (non-struct mode). */
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

/* Emit struct typedef + field #defines for named pattern struct mode.
 * tyname = "pat_X_t", safe = "X"
 * Writes to out_file (not byrd_out — called before code body is spliced). */
static void decl_flush_as_struct(FILE *out_file, const char *tyname) {
    /* Struct typedef */
    fprintf(out_file, "typedef struct %s {\n", tyname);
    for (int i = 0; i < decl_count; i++)
        fprintf(out_file, "    %s;\n", decl_buf[i]);
    for (int i = 0; i < child_decl_count; i++)
        fprintf(out_file, "    %s;\n", child_decl_buf[i]);
    fprintf(out_file, "} %s;\n\n", tyname);
}

/* Emit #define aliases so the code body can use bare names via z->name. */
static void decl_emit_defines(FILE *out_file) {
    for (int i = 0; i < decl_count; i++) {
        const char *nm = decl_field_name(decl_buf[i]);
        int len = decl_field_name_len(decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#define %.*s z->%.*s\n", len, nm, len, nm);
    }
    for (int i = 0; i < child_decl_count; i++) {
        const char *nm = decl_field_name(child_decl_buf[i]);
        int len = decl_field_name_len(child_decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#define %.*s z->%.*s\n", len, nm, len, nm);
    }
    if (decl_count + child_decl_count > 0)
        fprintf(out_file, "\n");
}

/* Emit #undef for all defines. */
static void decl_emit_undefs(FILE *out_file) {
    for (int i = 0; i < decl_count; i++) {
        const char *nm = decl_field_name(decl_buf[i]);
        int len = decl_field_name_len(decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#undef %.*s\n", len, nm);
    }
    for (int i = 0; i < child_decl_count; i++) {
        const char *nm = decl_field_name(child_decl_buf[i]);
        int len = decl_field_name_len(child_decl_buf[i]);
        if (len > 0)
            fprintf(out_file, "#undef %.*s\n", len, nm);
    }
    if (decl_count + child_decl_count > 0)
        fprintf(out_file, "\n");
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
 * E_VAR  → to_str(var_get("name"))
 * E_CONCAT → ccat(lhs, rhs)   (both sides recursed)
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
        snprintf(buf, bufsz, "to_str(var_get(\"%s\"))",
                 arg->sval ? arg->sval : "");
        return;
    case E_CONCAT: {
        char lbuf[512], rbuf[512];
        emit_charset_cexpr(arg->left,  lbuf, (int)sizeof lbuf);
        emit_charset_cexpr(arg->right, rbuf, (int)sizeof rbuf);
        /* ccat is #define'd as concat_sv(SnoVal,SnoVal) in snoc_runtime.h,
         * so we must wrap char* sides in STR_VAL and extract result with to_str. */
        snprintf(buf, bufsz,
                 "to_str(concat_sv(STR_VAL(%s), STR_VAL(%s)))",
                 lbuf, rbuf);
        return;
    }
    case E_KEYWORD:
        /* &UCASE, &LCASE etc — kw(name) = var_get(name) → SnoVal */
        snprintf(buf, bufsz, "to_str(var_get(\"%s\"))",
                 arg->sval ? arg->sval : "");
        return;
    case E_DEREF:
        /* $varname — indirect: left child holds the var name node */
        if (arg->left && arg->left->sval)
            snprintf(buf, bufsz, "to_str(var_get(\"%s\"))", arg->left->sval);
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
                     const char *cursor, int depth, int do_shift);
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

    /* α: bounds check, match, advance */
    PLG(alpha, NULL);
    PS(omega,   "if (%s + %d > %s)", cursor, n, subj_len);
    if (n == 1) {
        char ch = s[0];
        if (ch == '\'' || ch == '\\')
            PS(omega, "if (%s[%s] != '\\%c')", subj, cursor, ch);
        else
            PS(omega, "if (%s[%s] != '%c')", subj, cursor, ch);
    } else {
        /* memcmp: build stmt string with embedded string literal */
        char cstr[LBUF*2]; int ci = 0;
        cstr[ci++] = '"';
        for (int i = 0; i < n && ci < (int)sizeof(cstr)-4; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == '"' || c == '\\') { cstr[ci++] = '\\'; cstr[ci++] = c; }
            else if (c < 32)           { ci += snprintf(cstr+ci, 8, "\\x%02x", c); }
            else                       { cstr[ci++] = c; }
        }
        cstr[ci++] = '"'; cstr[ci] = '\0';
        PS(omega, "if (memcmp(%s + %s, %s, %d) != 0)", subj, cursor, cstr, n);
    }
    PS(gamma, "%s = %s; %s += %d;", saved, cursor, cursor, n);

    /* β: restore cursor */
    PL(beta, omega, "%s = %s;", cursor, saved);
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
    PLG(alpha, NULL);
    PS(omega,  "if (%s != %ld)", cursor, n);
    PG(gamma);
    PLG(beta, omega);
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
    PLG(alpha, NULL);
    PS(omega,  "if (%s != %s - %ld)", cursor, subj_len, n);
    PG(gamma);
    PLG(beta, omega);
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

    PLG(alpha, NULL);
    PS(omega,  "if (%s + %ld > %s)", cursor, n, subj_len);
    PS(gamma,  "%s = %s; %s += %ld;", saved, cursor, cursor, n);
    PL(beta, omega, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * TAB node — advance cursor to absolute position n
 * ----------------------------------------------------------------------- */

static void emit_tab(long n,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    PLG(alpha, NULL);
    PS(omega,  "if (%s > %ld)", cursor, n);
    PS(gamma,  "%s = %s; %s = %ld;", saved, cursor, cursor, n);
    PL(beta, omega, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * RTAB node — advance cursor to len - n
 * ----------------------------------------------------------------------- */

static void emit_rtab(long n,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    PLG(alpha, NULL);
    PS(omega,  "if (%s > %s - %ld)", cursor, subj_len, n);
    PS(gamma,  "%s = %s; %s = %s - %ld;", saved, cursor, cursor, subj_len, n);
    PL(beta, omega, "%s = %s;", cursor, saved);
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

    PLG(alpha, NULL);
    PS(omega,  "if (%s >= %s)", cursor, subj_len);
    PS(omega,  "if (!strchr(%s, %s[%s]))", cs, subj, cursor);
    PS(gamma,  "%s = %s; %s++;", saved, cursor, cursor);
    PL(beta, omega, "%s = %s;", cursor, saved);
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

    PLG(alpha, NULL);
    PS(omega,  "if (%s >= %s)", cursor, subj_len);
    PS(omega,  "if (strchr(%s, %s[%s]))", cs, subj, cursor);
    PS(gamma,  "%s = %s; %s++;", saved, cursor, cursor);
    PL(beta, omega, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * SPAN node — mtch one or more chars in charset
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

    PLG(alpha, NULL);
    PS(NULL,   "%s = %s;", start, cursor);
    PS(NULL,   "while (%s < %s && strchr(%s, %s[%s])) %s++;",
               cursor, subj_len, cs, subj, cursor, cursor);
    PS(NULL,   "%s = %s - %s;", delta, cursor, start);
    PS(omega,  "if (%s == 0)", delta);
    PG(gamma);

    PLG(beta, NULL);
    B("                  if (%s <= 1) { %s = %s; goto %s; }\n",
      delta, cursor, start, omega);
    PS(gamma,  "%s--; %s--;", delta, cursor);
}

/* -----------------------------------------------------------------------
 * BREAK node — match zero or more chars NOT in charset, stop before hit
 * Deterministic: beta → omega.
 * ----------------------------------------------------------------------- */

static void emit_break(const char *cs,
                       const char *alpha, const char *beta,
                       const char *gamma, const char *omega,
                       const char *subj, const char *subj_len,
                       const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    PLG(alpha, NULL);
    PS(NULL,  "%s = %s;", saved, cursor);
    PS(NULL,  "while (%s < %s && !strchr(%s, %s[%s])) %s++;",
              cursor, subj_len, cs, subj, cursor, cursor);
    /* Fail-check: if we hit end-of-subject without finding delimiter, restore+fail.
     * Use explicit goto inside braces so wrap never splits the condition from its goto. */
    B("                  if (%s >= %s) { %s = %s; goto %s; }\n",
      cursor, subj_len, cursor, saved, omega);
    PG(gamma);
    PL(beta, omega, "%s = %s;", cursor, saved);
}

/* -----------------------------------------------------------------------
 * ARB node — match any number of chars (greedy, backtrackable)
 * ----------------------------------------------------------------------- */

static void emit_arb(const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj_len, const char *cursor) {
    char saved[LBUF];
    snprintf(saved, LBUF, "%s_saved_cursor", alpha);
    decl_add("int64_t %s", saved);

    PL(alpha, gamma, "%s = %s;", saved, cursor);

    PLG(beta, NULL);
    PS(omega, "if (%s >= %s)", saved, subj_len);
    PS(gamma, "%s++; %s = %s;", saved, cursor, saved);
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

    PL(alpha, gamma, "%s = %s; %s = %s;", saved, cursor, cursor, subj_len);
    PL(beta,  omega, "%s = %s;", cursor, saved);
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
 *   root_α:  goto cat_l1_alpha;
 *   cat_l1_*     POS
 *   cat_l1_gamma: goto cat_r2_alpha;
 *   cat_l1_omega: goto root_β;
 *   cat_r2_*     ...
 *   cat_r2_omega: goto cat_l1_beta;
 * ----------------------------------------------------------------------- */

static void emit_seq(Expr *left, Expr *right,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth) {
    int uid = byrd_uid();

    Label left_α, left_β, right_α, right_β;
    label_fmt(left_α,  "cat_l", uid, "α");
    label_fmt(left_β,  "cat_l", uid, "β");
    label_fmt(right_α, "cat_r", uid, "α");
    label_fmt(right_β, "cat_r", uid, "β");

    PLG(alpha, left_α);   /* α  → left_α  (CAT — entr left) */
    PLG(beta,  right_β);  /* β  → right_β (resume right first) */

    byrd_emit(left,
              left_α, left_β,
              right_α, omega,
              subj, subj_len, cursor, depth + 1);

    byrd_emit(right,
              right_α, right_β,
              gamma, left_β,
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

    Label left_α, left_β, right_α, right_β;
    label_fmt(left_α,  "alt_l", uid, "α");
    label_fmt(left_β,  "alt_l", uid, "β");
    label_fmt(right_α, "alt_r", uid, "α");
    label_fmt(right_β, "alt_r", uid, "β");

    PLG(alpha, left_α);   /* α → left_α  (ALT — try left) */
    PLG(beta,  right_β);  /* β → right_β (backtrack right arm) */

    byrd_emit(left,
              left_α, left_β,
              gamma, right_α,
              subj, subj_len, cursor, depth + 1);

    byrd_emit(right,
              right_α, right_β,
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
 *   - child_ok → gamma (ARBNO succeeds again with one more mtch)
 *   - child_fail → restore cursor, depth--, omega (ARBNO gives up)
 * ----------------------------------------------------------------------- */

static void emit_arbno(Expr *child,
                       const char *alpha, const char *beta,
                       const char *gamma, const char *omega,
                       const char *subj, const char *subj_len,
                       const char *cursor, int depth) {
    int uid = byrd_uid();

    Label child_α, child_β, child_ok, child_fail;
    label_fmt(child_α, "arbno_c", uid, "α");
    label_fmt(child_β, "arbno_c", uid, "β");
    snprintf(child_ok,   LBUF, "%s_child_ok",   alpha);
    snprintf(child_fail, LBUF, "%s_child_fail", alpha);

    char depth_var[LBUF], stack_var[LBUF], stk_save[LBUF];
    snprintf(depth_var, LBUF, "%s_depth",   alpha);
    snprintf(stack_var, LBUF, "%s_cursors", alpha);
    snprintf(stk_save,  LBUF, "%s_saved_stk", alpha);
    decl_add("int %s",         depth_var);
    decl_add("int64_t %s[64]", stack_var);
    /* Save/restore $'@S' (tree stack) across ARBNO iterations.
     * The zero-match alpha path fires Reduce immediately, pushing a tree
     * onto @S.  When ARBNO extends via beta that stray tree must be undone
     * before the next Reduce fires, or Reduce(Stmt,7) consumes it as a child. */
    decl_add("SnoVal %s",      stk_save);

    /* α: snapshot @S, zero matches → succeed immediately */
    PL(alpha, gamma, "%s = var_get(\"@S\"); %s = -1;", stk_save, depth_var);

    /* β: restore @S to pre-alpha snapshot, then extend by one */
    PLG(beta, NULL);
    /* Undo any Reduce side-effects from the previous shorter match */
    PS(NULL, "var_set(\"@S\", %s);", stk_save);
    /* Re-establish counter frame if nPop fired on the alpha-path success */
    PS(NULL, "if (!nhas_frame()) npush();");
    PS(omega,    "if (++%s >= 64)", depth_var);
    PS(child_α,  "%s[%s] = %s;", stack_var, depth_var, cursor);

    /* child_ok: child matched → update @S snapshot, ARBNO offers another match */
    PLG(child_ok, NULL);
    /* Snapshot @S after successful child so next beta restores to this point */
    PS(gamma, "%s = var_get(\"@S\");", stk_save);

    /* child_fail: child failed → restore cursor, pop, ARBNO fails */
    PLG(child_fail, NULL);
    PS(omega,   "%s = %s[%s]; %s--;", cursor, stack_var, depth_var, depth_var);

    byrd_emit(child,
              child_α, child_β,
              child_ok, child_fail,
              subj, subj_len, cursor, depth + 1);
}

/* -----------------------------------------------------------------------
 * is_sideeffect_call — returns 1 if e is an E_CALL to a counter side-effect
 * (nPush/nInc/nPop/nDec/nTop) that has no pattern-matching semantics.
 * ----------------------------------------------------------------------- */

static int is_sideeffect_call(Expr *e) {
    if (!e || e->kind != E_CALL || !e->sval) return 0;
    return (strcasecmp(e->sval, "nPush") == 0 ||
            strcasecmp(e->sval, "nInc")  == 0 ||
            strcasecmp(e->sval, "nPop")  == 0 ||
            strcasecmp(e->sval, "nDec")  == 0 ||
            strcasecmp(e->sval, "nTop")  == 0);
}

/* emit_sideeffect_call_inline — emit a single C statement for a side-effect call */
static void emit_sideeffect_call_inline(Expr *e) {
    if (!e || e->kind != E_CALL || !e->sval) return;
    if (strcasecmp(e->sval, "nPush") == 0) B("npush();");
    else if (strcasecmp(e->sval, "nInc") == 0) B("ninc();");
    else if (strcasecmp(e->sval, "nPop") == 0) B("npop();");
    else if (strcasecmp(e->sval, "nDec") == 0) B("ndec();");
    else if (strcasecmp(e->sval, "nTop") == 0) B("(void)ntop();");
    else B("/* unknown sideeffect %s */", e->sval);
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
 *       output(var_OUTPUT);
 *       goto cat_r6_alpha;
 *   cat_l5_beta:
 *       goto assign_c7_beta;
 *
 * Special case — Gimpel SNOBOL4 side-effect pattern: nPush() $'('
 *   The grammar parses this as E_IMM(left=nPush(), right=E_STR("(")).
 *   left is NOT a pattern child — it's a side-effect call with no cursor advance.
 *   right is NOT a variable name — it's a literal to match and capture to OUTPUT.
 *   Fix: emit the side-effect call inline, then match+capture the literal.
 * ----------------------------------------------------------------------- */

static void emit_imm(Expr *child, const char *varname,
                     const char *alpha, const char *beta,
                     const char *gamma, const char *omega,
                     const char *subj, const char *subj_len,
                     const char *cursor, int depth, int do_shift) {
    int uid = byrd_uid();

    /* -------------------------------------------------------------------
     * Special case: nPush() $'(' — side-effect call + literal capture.
     *
     * Grammar parses `nPush() $'('` as E_IMM(left=nPush(), right=E_STR("(")).
     * child (left) = nPush() — no cursor advance, just a side-effect.
     * varname (from right) = "(" — not a valid variable; it IS the literal.
     *
     * Correct semantics: emit the side-effect inline, then match+capture
     * the literal string (varname) to OUTPUT.  β → ω (no backtrack over
     * a side-effect call — the call already fired).
     * ------------------------------------------------------------------- */
    if (is_sideeffect_call(child) && varname && varname[0] != '\0') {
        /* Check that varname looks like a non-identifier (e.g. punctuation
         * like '(' or ')') — if it IS a valid identifier, it could be a
         * genuine capture variable paired with a side-effect, which we
         * handle the same way (side-effect fires, then match literal). */
        int uid2 = byrd_uid();
        Label lit_α, lit_β;
        label_fmt(lit_α, "imm_se_lit", uid2, "α");
        label_fmt(lit_β, "imm_se_lit", uid2, "β");

        char start_var[LBUF], do_assign[LBUF];
        snprintf(start_var, LBUF, "%s_start",     alpha);
        snprintf(do_assign, LBUF, "%s_do_assign", alpha);
        decl_add("int64_t %s", start_var);

        /* α: emit side-effect inline, record start, enter literal match */
        PLG(alpha, NULL);
        PS(NULL, "%s_start = %s;", alpha, cursor);
        emit_sideeffect_call_inline(child);
        B("\n");
        PS(lit_α, "goto %s;", lit_α);

        /* literal match for varname */
        emit_lit(varname,
                 lit_α, lit_β,
                 do_assign, omega,
                 subj, subj_len, cursor);

        /* do_assign: capture matched span → OUTPUT, then γ */
        PLG(do_assign, NULL);
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        PS(gamma, "  output_str(_os); free(_os); }");

        /* β → ω: side-effect already fired, cannot resume */
        PLG(beta, omega);
        return;
    }

    /* -------------------------------------------------------------------
     * Normal E_IMM path: child is a real pattern, varname is a variable.
     * ------------------------------------------------------------------- */

    /* Sanitize varname: non-alnum/underscore chars → '_' */
    char safe_varname[NAMED_PAT_NAMELEN];
    { int i = 0; const char *s = varname;
      for (; *s && i < (int)(sizeof safe_varname)-1; s++, i++)
          safe_varname[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
      safe_varname[i] = '\0'; }
    varname = safe_varname;

    Label child_α, child_β;
    label_fmt(child_α, "assign_c", uid, "α");
    label_fmt(child_β, "assign_c", uid, "β");

    char start_var[LBUF], do_assign[LBUF];
    snprintf(start_var, LBUF, "%s_start",     alpha);
    snprintf(do_assign, LBUF, "%s_do_assign", alpha);
    decl_add("int64_t %s", start_var);

    /* α: record start, enter child */
    PL(alpha, child_α, "%s = %s;", start_var, cursor);

    byrd_emit(child,
              child_α, child_β,
              do_assign, omega,
              subj, subj_len, cursor, depth + 1);

    /* do_assign: capture span → variable, then γ */
    PLG(do_assign, NULL);
    if (strcasecmp(varname, "OUTPUT") == 0) {
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        PS(gamma, "  output_str(_os); free(_os); }");
    } else {
        /* Sync both hash table and C static (if one exists).
         * Skip C static sync for '_' (discard var → '__' invalid) and
         * any name that maps to a reserved/invalid C identifier.
         * When do_shift=1 (~ operator), varname is a tree tag not a SNOBOL4
         * variable — never emit C static assignment for tags. */
        int skip_cstatic = (strcmp(varname, "_") == 0) || do_shift || (varname[0] == '\0');
        PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
        PS(NULL,  "  char *_os = (char*)GC_malloc(_len + 1);");
        PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
        if (skip_cstatic) {
            PS(NULL,  "  var_set(\"%s\", STR_VAL(_os));", varname);
        } else {
            PS(NULL,  "  var_set(\"%s\", STR_VAL(_os));", varname);
            PS(NULL,  "  %s = STR_VAL(_os);", byrd_cs(varname));
        }
        if (do_shift) {
            /* ~ operator: push tree node onto shift-reduce stack via Shift(type, value) */
            PS(NULL, "  { SnoVal _shift_args[2] = {STR_VAL(\"%s\"), STR_VAL(_os)};", varname);
            PS(NULL, "    aply(\"Shift\", _shift_args, 2); }");
        }
        PS(gamma, "}");
    }

    /* β: backtrack into child */
    PLG(beta, child_β);
}

/* -----------------------------------------------------------------------
 * E_COND (. capture) node
 *
 * Like E_IMM but the assignment is CONDITIONAL — it fires when we reach
 * the gamma of the enclosing mtch.  For the static compiled path we treat
 * it identically to E_IMM (assign on child success, readable by outer code).
 * ----------------------------------------------------------------------- */

static void emit_cond(Expr *child, const char *varname,
                      const char *alpha, const char *beta,
                      const char *gamma, const char *omega,
                      const char *subj, const char *subj_len,
                      const char *cursor, int depth) {
    /* ~ operator: same as IMM but calls Shift() to push tree node */
    emit_imm(child, varname, alpha, beta, gamma, omega,
             subj, subj_len, cursor, depth, 1);
}

/* -----------------------------------------------------------------------
 * FENCE node (no-argument form)
 *
 * Succeeds once, cannot be resumed (beta → omega immediately).
 * ----------------------------------------------------------------------- */

static void emit_fence(const char *alpha, const char *beta,
                       const char *gamma, const char *omega) {
    PLG(alpha, gamma);   /* FENCE: α → γ, no backtrack */
    PLG(beta,  omega);
}

/* -----------------------------------------------------------------------
 * SUCCEED node — always succeeds, repeats forever on backtrack
 * ----------------------------------------------------------------------- */

static void emit_succeed(const char *alpha, const char *beta,
                         const char *gamma) {
    PLG(alpha, gamma);   /* SUCCEED: α and β both → γ */
    PLG(beta,  gamma);
}

/* -----------------------------------------------------------------------
 * FAIL node — always fails
 * ----------------------------------------------------------------------- */

static void emit_fail_node(const char *alpha, const char *beta,
                           const char *omega) {
    PLG(alpha, omega);   /* FAIL: α and β both → ω */
    PLG(beta,  omega);
}

/* -----------------------------------------------------------------------
 * ABORT node — terminates the entire match
 * ----------------------------------------------------------------------- */

static void emit_abort_node(const char *alpha, const char *beta,
                            const char *omega) {
    PLG(alpha, omega);   /* TODO: propagate abort signal */
    PLG(beta,  omega);
}

/* -----------------------------------------------------------------------
 * emit_simple_val — emit a SnoVal C expression for a simple Expr node.
 * Used by E_REDUCE to pass left/right as runtime values.
 * Handles: E_STR, E_INT, E_VAR, E_CALL(nTop), E_NULL/NULL.
 * Falls back to NULL_VAL for anything complex.
 * ----------------------------------------------------------------------- */

static void emit_simple_val(Expr *e) {
    if (!e) { B("NULL_VAL"); return; }
    switch (e->kind) {
    case E_STR: {
        /* 'nTop()' as a quoted string in & context: wire directly to C-level ntop()
         * so compiled byrd box counter (npush/ninc/npop) and Reduce agree. */
        if (e->sval && strcasecmp(e->sval, "nTop()") == 0)
            { B("INT_VAL(ntop())"); return; }
        /* Strip outer single-quote pair: sval "'Stmt'" -> emit STR_VAL("Stmt").
         * The SNOBOL4 source writes "'Stmt'" (double-quoted with inner single quotes).
         * The lexer strips outer double-quotes, storing "'Stmt'" as sval — but the
         * inner single quotes are SNOBOL4 string delimiters that must also be stripped
         * so the C string value is bare: Stmt, not 'Stmt'. */
        const char *sv = e->sval ? e->sval : "";
        size_t sl = strlen(sv);
        if (sl >= 2 && sv[0] == '\'' && sv[sl-1] == '\'')
            B("STR_VAL(\"%.*s\")", (int)(sl-2), sv+1);
        else
            B("STR_VAL(\"%s\")", sv);
        return;
    }
    case E_INT:
        B("INT_VAL(%ld)", e->ival);
        return;
    case E_VAR:
        B("var_get(\"%s\")", e->sval ? e->sval : "");
        return;
    case E_CALL:
        if (e->sval && strcasecmp(e->sval, "nTop") == 0)
            { B("INT_VAL(ntop())"); return; }
        if (e->sval && strcasecmp(e->sval, "nInc") == 0)
            { B("INT_VAL((ninc(), ntop()))"); return; }
        if (e->sval && strcasecmp(e->sval, "nPop") == 0)
            { B("INT_VAL((npop(), 0))"); return; }
        /* generic: fall through to aply */
        B("aply(\"%s\", NULL, 0)", e->sval ? e->sval : "");
        return;
    default:
        B("NULL_VAL /* unhandled emit_simple_val kind %d */", (int)e->kind);
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
        PLG(alpha, gamma);
        PLG(beta,  omega);
        return;
    }

    if (depth > 128) {
        B("/* emit_byrd: depth limit hit */\n");
        PLG(alpha, omega);
        PLG(beta,  omega);
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
        /* FENCE(pat) — with argument: match pat, then cut */
        if (strcasecmp(n, "FENCE") == 0 && pat->nargs >= 1) {
            int uid = byrd_uid();
            Label ca, cb;
            label_fmt(ca, "fence_p", uid, "α");
            label_fmt(cb, "fence_p", uid, "β");
            char fence_after[LBUF];
            snprintf(fence_after, LBUF, "fence_after_%d", uid);

            PLG(alpha, ca);   /* FENCE(p): enter child */
            byrd_emit(pat->args[0],
                      ca, cb,
                      fence_after, omega,
                      subj, subj_len, cursor, depth + 1);
            PLG(fence_after, gamma);   /* child succeeded → γ (no backtrack) */
            PLG(beta, omega);          /* β: cut */
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

        /* nPush() */
        if (strcasecmp(n, "nPush") == 0) {
            PL(alpha, gamma, "npush();");
            /* beta: nPush has no alternatives — fail so the containing CAT
             * routes backtrack to ARBNO's own beta (cat_l_847_β → cat_r_847_β).
             * Re-pushing here was wrong: it reset ARBNO depth=-1 each time,
             * causing infinite zero-iteration ARBNO loop. */
            PLG(beta, omega);
            return;
        }
        /* nInc() */
        if (strcasecmp(n, "nInc") == 0) {
            PL(alpha, gamma, "ninc();");
            PLG(beta, omega);
            return;
        }
        /* nDec() */
        if (strcasecmp(n, "nDec") == 0) {
            PL(alpha, gamma, "ndec();");
            PLG(beta, omega);
            return;
        }
        /* nPop() */
        if (strcasecmp(n, "nPop") == 0) {
            PL(alpha, gamma, "npop();");
            PLG(beta, omega);
            return;
        }
        /* nTop() */
        if (strcasecmp(n, "nTop") == 0) {
            PL(alpha, gamma, "(void)ntop();");
            PLG(beta, omega);
            return;
        }

        /* Fallback: unknown call — epsilon */
        PLG(alpha, gamma);
        PLG(beta,  omega);
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
        const char *varname = "OUTPUT";
        if (pat->right && (pat->right->kind == E_VAR || pat->right->kind == E_STR))
            varname = pat->right->sval;
        emit_imm(pat->left, varname,
                 alpha, beta, gamma, omega,
                 subj, subj_len, cursor, depth, 0);
        return;
    }

    /* --------------------------------------------------------------- E_COND (. assign) */
    case E_COND: {
        const char *varname = "OUTPUT";
        if (pat->right && (pat->right->kind == E_VAR || pat->right->kind == E_STR))
            varname = pat->right->sval;
        emit_cond(pat->left, varname,
                  alpha, beta, gamma, omega,
                  subj, subj_len, cursor, depth);
        return;
    }

    /* ---------------------------------------------------------------- E_VAR (pattern var)
     * A bare variable name in pattern context means implicit deref — same as *X.
     * Fall through to E_DEREF with varname = pat->sval.
     * (Previously emitted epsilon — wrong: `nl` in Command would silently match nothing.) */
    case E_VAR: {
        const char *varname = pat->sval ? pat->sval : "";
        const NamedPat *np_v = named_pat_lookup(varname);
        if (np_v) {
            /* Compiled named pattern — direct call */
            int uid = byrd_uid();
            char saved_cur[LBUF];
            snprintf(saved_cur, LBUF, "deref_%d_saved_cur", uid);
            decl_add("int64_t %s", saved_cur);
            char child_field[LBUF];
            snprintf(child_field, LBUF, "deref_%d_z", uid);
            {
                char child_decl[DECL_LINE_MAX];
                snprintf(child_decl, DECL_LINE_MAX, "%s *%s", np_v->typename, child_field);
                if (in_named_pat)
                    child_decl_add(child_decl);
                else
                    decl_add("%s *%s", np_v->typename, child_field);
            }
            B("%s: {\n", alpha);
            B("    %s = %s;\n", saved_cur, cursor);
            B("    SnoVal _r_%d = %s(%s, %s, &%s, &%s, 0);\n",
              uid, np_v->fnname, subj, subj_len, cursor, child_field);
            B("    if (is_fail(_r_%d)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, omega);
            B("    goto %s;\n", gamma);
            B("}\n");
            B("%s: {\n", beta);
            B("    %s = %s;\n", cursor, saved_cur);
            B("    SnoVal _r_%d_b = %s(%s, %s, &%s, &%s, 1);\n",
              uid, np_v->fnname, subj, subj_len, cursor, child_field);
            B("    if (is_fail(_r_%d_b)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, omega);
            B("    goto %s;\n", gamma);
            B("}\n");
        } else {
            /* String/dynamic pattern — match_pattern_at fallback */
            char saved[LBUF];
            snprintf(saved, LBUF, "deref_%d_saved_cursor", byrd_uid());
            decl_add("int64_t %s", saved);
            B("%s: {\n", alpha);
            B("    SnoVal _deref_pat = var_get(\"%s\");\n", varname);
            B("    int _deref_new_cur = match_pattern_at(_deref_pat, %s, (int)%s, (int)%s);\n",
              subj, subj_len, cursor);
            B("    if (_deref_new_cur < 0) goto %s;\n", omega);
            B("    %s = %s;\n", saved, cursor);
            B("    %s = (int64_t)_deref_new_cur;\n", cursor);
            B("    goto %s;\n", gamma);
            B("}\n");
            B("%s:\n", beta);
            B("    %s = %s;\n", cursor, saved);
            B("    goto %s;\n", omega);
        }
        return;
    }

    /* ---------------------------------------------------------------- E_DEREF (deferred ref) */
    case E_DEREF: {
        const char *varname = NULL;
        /* Grammar: unary *X  → E_DEREF(left=NULL, right=E_VAR("X"))
         * Also seen: left=E_VAR in some paths — check both.
         * Also: left=E_CALL("name") — *name() where name is a named pattern fn.
         * Also: left=NULL, right=E_STR("x") — unary $'x' output capture. */
        if (pat->right && pat->right->kind == E_VAR)
            varname = pat->right->sval;
        else if (pat->left && pat->left->kind == E_VAR)
            varname = pat->left->sval;
        else if (pat->left && pat->left->kind == E_CALL && pat->left->sval)
            varname = pat->left->sval;
        if (!varname) varname = "";

        /* -------------------------------------------------------------------
         * Category: unary $'literal' — E_DEREF(left=NULL, right=E_STR("x"))
         * Semantics: match the literal, capture matched span to OUTPUT.
         * ------------------------------------------------------------------- */
        if (!pat->left && pat->right && pat->right->kind == E_STR && pat->right->sval) {
            const char *lit = pat->right->sval;
            int uid2 = byrd_uid();
            Label lit_α, lit_β;
            label_fmt(lit_α, "dlit", uid2, "α");
            label_fmt(lit_β, "dlit", uid2, "β");
            char start_var[LBUF], do_assign[LBUF];
            snprintf(start_var, LBUF, "dlit_%d_start", uid2);
            snprintf(do_assign, LBUF, "dlit_%d_do_assign", uid2);
            decl_add("int64_t %s", start_var);

            /* α: record start, match literal */
            PLG(alpha, NULL);
            PS(NULL, "%s = %s;", start_var, cursor);
            PS(lit_α, "goto %s;", lit_α);

            emit_lit(lit, lit_α, lit_β, do_assign, omega,
                     subj, subj_len, cursor);

            /* do_assign: capture span → OUTPUT, then γ */
            PLG(do_assign, NULL);
            PS(NULL,  "{ int64_t _len = %s - %s;", cursor, start_var);
            PS(NULL,  "  char *_os = malloc(_len + 1);");
            PS(NULL,  "  memcpy(_os, %s + %s, _len); _os[_len] = 0;", subj, start_var);
            PS(gamma, "  output_str(_os); free(_os); }");

            PLG(beta, lit_β);
            return;
        }

        const NamedPat *np = named_pat_lookup(varname);

        if (np) {
            /* Compiled path: direct function call — no engine.c */
            int uid = byrd_uid();
            char saved_cur[LBUF];
            snprintf(saved_cur, LBUF, "deref_%d_saved_cur", uid);
            decl_add("int64_t %s", saved_cur);

            char child_field[LBUF];
            snprintf(child_field, LBUF, "deref_%d_z", uid);
            {
                char child_decl[DECL_LINE_MAX];
                snprintf(child_decl, DECL_LINE_MAX, "%s *%s", np->typename, child_field);
                if (in_named_pat)
                    child_decl_add(child_decl);
                else
                    decl_add("%s *%s", np->typename, child_field);
            }

            /* α: first call — entry 0 */
            B("%s: {\n", alpha);
            B("    %s = %s;\n", saved_cur, cursor);
            B("    SnoVal _r_%d = %s(%s, %s, &%s, &%s, 0);\n",
              uid, np->fnname, subj, subj_len, cursor, child_field);
            B("    if (is_fail(_r_%d)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, omega);
            B("    goto %s;\n", gamma);
            B("}\n");

            /* β: backtrack — entry 1 */
            B("%s: {\n", beta);
            B("    %s = %s;\n", cursor, saved_cur);
            B("    SnoVal _r_%d_b = %s(%s, %s, &%s, &%s, 1);\n",
              uid, np->fnname, subj, subj_len, cursor, child_field);
            B("    if (is_fail(_r_%d_b)) { %s = %s; goto %s; }\n",
              uid, cursor, saved_cur, omega);
            B("    goto %s;\n", gamma);
            B("}\n");
            return;
        }

        /* Interpreter fallback for dynamic/EVAL patterns */
        char saved[LBUF];
        snprintf(saved, LBUF, "deref_%d_saved_cursor", byrd_uid());
        decl_add("int64_t %s", saved);

        B("%s: {\n", alpha);
        B("    SnoVal _deref_pat = var_get(\"%s\");\n", varname);
        B("    int _deref_new_cur = match_pattern_at(_deref_pat, %s, (int)%s, (int)%s);\n",
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
        B("%s: /* E_REDUCE & */\n", alpha);
        B("    { SnoVal _reduce_args[2] = {");
        emit_simple_val(pat->left);
        B(", ");
        emit_simple_val(pat->right);
        B("};\n");
        B("      aply(\"Reduce\", _reduce_args, 2); }\n");
        B("    goto %s;\n", gamma);
        B("%s: goto %s;\n", beta, omega);
        return;
    }

    /* ---------------------------------------------------------------- default */
    default:
        PLG(alpha, gamma);   /* unhandled — epsilon */
        PLG(beta,  omega);
        return;
    }
}

/* =======================================================================
 * Public API
 *
 * byrd_emit_pattern — emit a complete standalone mtch block:
 *
 *   goto <root>_alpha;
 *   [declarations]
 *   [Byrd box labeled-goto C]
 *   <gamma_label>:   (mtch succeeded)
 *   <omega_label>:   (mtch failed)
 *
 * Called from emit.c in the pattern-mtch statement case.
 *
 * Parameters:
 *   pat          — the pattern Expr* (subject pattern field)
 *   out          — output FILE*
 *   root_name    — prefix for root labels (e.g. "root_1")
 *   subject_var  — C expression for the subject string pointer
 *   subj_len_var — C expression for subject length (int64_t)
 *   cursor_var   — C lvalue for the cursor (int64_t, modified in place)
 *   gamma_label  — C label to jump to on mtch success
 *   omega_label  — C label to jump to on mtch failure
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
    char root_α[LBUF], root_β[LBUF];
    snprintf(root_α, LBUF, "%s_α", root_name);
    snprintf(root_β,  LBUF, "%s_β",  root_name);

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
              root_α, root_β,
              gamma_label, omega_label,
              subject_var, subj_len_var, cursor_var,
              0);

    /* Note: root_β is emitted by byrd_emit above for all node types.
     * Do NOT emit it again here — that causes duplicate label errors. */

    fflush(code_file);
    fclose(code_file);

    /* Now emit to out_file: declarations FIRST, then goto, then code.
     * C99 forbids jumping over variable-length declarations, so all
     * static decls must appear before the first goto in the function. */
    byrd_out = out_file;
    decl_flush();            /* static declarations — before any goto */
    B("    goto %s;\n", root_α);

    /* Splice code body */
    if (code_buf && code_size > 0)
        fwrite(code_buf, 1, code_size, out_file);

    free(code_buf);
}

/* =======================================================================
 * byrd_emit_named_pattern — emit a named pattern as a compiled C function.
 *
 * Emits a forward decl + C function:
 *   static SnoVal pat_X(const char *_subj_np, int64_t _slen_np,
 *                       int64_t *_cur_ptr_np, int _entry_np);
 * entry 0 = alpha (fresh), entry 1 = beta (resume).
 * On success updates *_cur_ptr_np and returns SNO_EMPTY.
 * On failure returns FAIL_VAL.
 *
 * Registers the name so E_DEREF (*varname) emits a direct call.
 * ======================================================================= */

void byrd_emit_named_pattern(const char *varname, Expr *pat, FILE *out_file) {
    /* C-safe name */
    char safe[NAMED_PAT_NAMELEN];
    {
        const char *s = varname;
        int i = 0;
        for (; *s && i < (int)(sizeof safe)-1; s++, i++)
            safe[i] = (isalnum((unsigned char)*s) || *s=='_') ? *s : '_';
        safe[i] = '\0';
    }

    char fnname[NAMED_PAT_NAMELEN];
    char tyname[NAMED_PAT_NAMELEN];
    snprintf(fnname, sizeof fnname, "pat_%s", safe);
    snprintf(tyname, sizeof tyname, "pat_%s_t", safe);

    named_pat_register(varname, tyname, fnname);

    /* Deduplicate: only emit each named pattern once (first definition wins).
     * beauty.sno assigns the same variable in multiple function bodies;
     * we compile the first occurrence and ignore subsequent ones. */
    NamedPat *np_entry = NULL;
    for (int i = 0; i < named_pat_count; i++) {
        if (strcmp(named_pat_registry[i].varname, varname) == 0) {
            np_entry = &named_pat_registry[i];
            break;
        }
    }
    if (np_entry && np_entry->emitted) return;
    if (np_entry) np_entry->emitted = 1;

    /* Reset fn_seen so statics aren't skipped due to a previous pattern's decls */
    byrd_fn_scope_reset();

    /* NOTE: forward declaration is NOT emitted here.
     * Caller must emit all forward decls (via byrd_emit_named_fwdecls) BEFORE
     * calling this function, so mutual/forward refs compile clean. */

    char   *code_buf  = NULL;
    size_t  code_size = 0;
    FILE   *code_file = open_memstream(&code_buf, &code_size);
    if (!code_file) {
        fprintf(out_file, "/* emit_byrd: open_memstream failed for %s */\n", varname);
        return;
    }

    int uid_saved = byrd_uid_ctr;
    decl_reset();

    char γ_lbl[LBUF], ω_lbl[LBUF];
    char root_α[LBUF], root_β[LBUF];
    snprintf(γ_lbl,  LBUF, "_%s_γ",    safe);
    snprintf(ω_lbl,  LBUF, "_%s_ω",  safe);
    snprintf(root_α, LBUF, "_%s_α", safe);
    snprintf(root_β,  LBUF, "_%s_β",  safe);

    /* Struct mode ON — decl_add will collect into struct fields,
     * child_decl_add will collect child frame pointers separately. */
    in_named_pat = 1;

    byrd_out = code_file;
    byrd_uid_ctr = uid_saved;
    decl_reset();

    byrd_emit(pat,
              root_α, root_β,
              γ_lbl, ω_lbl,
              "_subj_np", "_slen_np", "_cur_np",
              0);

    fflush(code_file);
    fclose(code_file);

    in_named_pat = 0;
    byrd_out = out_file;

    /* 1. Emit the struct typedef (uses decl_buf + child_decl_buf collected above) */
    decl_flush_as_struct(out_file, tyname);

    /* 2. Emit the function with new signature: pat_X(subj, slen, cur_ptr, **zz, entry) */
    fprintf(out_file,
        "static SnoVal %s(const char *_subj_np, int64_t _slen_np,\n"
        "                  int64_t *_cur_ptr_np, %s **_zz_np, int _entry_np) {\n"
        "    if (_entry_np == 0) { *_zz_np = calloc(1, sizeof(%s)); }\n"
        "    %s *z = *_zz_np;\n"
        "    int64_t _cur_np = *_cur_ptr_np;\n",
        fnname, tyname, tyname, tyname);

    /* 3. Emit #defines so code body uses bare names via z-> */
    decl_emit_defines(out_file);

    /* 4. Entry dispatch */
    fprintf(out_file,
        "    if (_entry_np == 0) goto %s;\n"
        "    if (_entry_np == 1) goto %s;\n"
        "    goto %s;\n",
        root_α, root_β, ω_lbl);

    /* 5. Splice the code body */
    if (code_buf && code_size > 0)
        fwrite(code_buf, 1, code_size, out_file);
    free(code_buf);

    /* 6. Success/fail exits */
    fprintf(out_file,
        "    %s:;\n"
        "        *_cur_ptr_np = _cur_np;\n"
        "        return STR_VAL(\"\");\n"
        "    %s:;\n"
        "        return FAIL_VAL;\n",
        γ_lbl, ω_lbl);

    /* 7. Emit #undefs and close function */
    decl_emit_undefs(out_file);
    fprintf(out_file, "}\n\n");
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
