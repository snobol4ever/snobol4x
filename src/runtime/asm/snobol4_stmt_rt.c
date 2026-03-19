/*
 * snobol4_stmt_rt.c — Statement-level runtime shim for the x64 ASM backend.
 *
 * The SNOBOL4 runtime (snobol4.c) exposes SNO_INIT_fn(), NV_SET_fn(), etc.
 * as real linkable symbols.  Some operations in runtime_shim.h are static
 * inlines or macros and cannot be extern'd from NASM.  This file wraps them
 * as proper exported functions so the generated .s can call them.
 *
 * Exported (all use C calling convention — SysV AMD64):
 *
 *   void   stmt_init(void)
 *       Call once at program start.  Calls SNO_INIT_fn + NV_SYNC_fn.
 *
 *   DESCR_t stmt_strval(const char *s)          → rax:rdx
 *       GC-duplicate s and return a DT_S DESCR_t.
 *
 *   void   stmt_output(DESCR_t val)              ← rdi:rsi (first two int regs)
 *       Set SNOBOL4 OUTPUT variable (triggers write to stdout).
 *
 *   DESCR_t stmt_get(const char *name)           → rax:rdx
 *       Get named SNOBOL4 variable value.
 *
 *   void   stmt_set(const char *name, DESCR_t v) ← rdi / rsi:rdx
 *       Set named SNOBOL4 variable.
 *
 *   int    stmt_is_fail(DESCR_t v)               → eax (0=ok, 1=fail)
 *       Returns 1 if descriptor is FAIL, 0 otherwise.
 *
 *   int    stmt_pos_var(const char *varname, int64_t cursor)  → eax
 *       POS(variable): fetch var, coerce to int, return 1 if cursor==n.
 *
 *   int    stmt_rpos_var(const char *varname, int64_t cursor, int64_t subj_len) → eax
 *       RPOS(variable): fetch var, coerce to int, return 1 if cursor==subj_len-n.
 *
 *   DESCR_t stmt_input(void)                     → rax:rdx
 *       Read one line from INPUT (stdin).  Returns FAILDESCR on EOF.
 *
 *   DESCR_t stmt_concat(DESCR_t a, DESCR_t b)   → rax:rdx
 *       String concatenation.
 *
 *   DESCR_t stmt_intval(int64_t i)               → rax:rdx
 *       Return integer DESCR_t.
 *
 * ABI note: DESCR_t is 16 bytes → SysV returns in rax (bytes 0-7) + rdx (bytes 8-15).
 *   bytes 0-3: DTYPE_t v  (int)
 *   bytes 4-7: padding
 *   bytes 8-15: union (ptr / int64 / double)
 * When passed as argument: first DESCR_t in rdi (low) + rsi (high),
 *                          second DESCR_t in rdx (low) + rcx (high).
 * (Each DESCR_t occupies two consecutive integer registers.)
 */

#include <string.h>
#include <gc.h>
#include "snobol4.h"
#include "runtime_shim.h"

/* ---- init ---- */

void stmt_init(void) {
    SNO_INIT_fn();
    extern void inc_init(void);
    inc_init();
    NV_SYNC_fn();
}

/* ---- string value ---- */

DESCR_t stmt_strval(const char *s) {
    return _str_impl(s);
}

/* ---- integer value ---- */

DESCR_t stmt_intval(int64_t i) {
    return INTVAL(i);
}

/* ---- real (double) value ---- */

DESCR_t stmt_realval(const double *p) {
    return REALVAL(*p);
}

/* ---- null assign (X =) ---- */

void stmt_set_null(const char *name) {
    if (name && name[0] == '&') name++;
    NV_SET_fn(name, NULVCL);
}

/* ---- indirect assign ($name = val) ---- */

void stmt_set_indirect(DESCR_t name_val, DESCR_t val) {
    const char *s = VARVAL_fn(name_val);
    if (!s || !*s) return;
    NV_SET_fn(s, val);
}



/* ---- POS(variable) — fetch var, coerce to int, compare with cursor ---- */

int stmt_pos_var(const char *varname, int64_t cursor) {
    DESCR_t v = NV_GET_fn(varname);
    int64_t n = to_int(v);
    return (cursor == n) ? 1 : 0;
}

/* ---- RPOS(variable) — fetch var, coerce to int, check from right ---- */

int stmt_rpos_var(const char *varname, int64_t cursor, int64_t subj_len) {
    DESCR_t v = NV_GET_fn(varname);
    int64_t n = to_int(v);
    return (cursor == subj_len - n) ? 1 : 0;
}

/* ---- SPAN(variable) — scan chars IN charset, return new cursor or -1 ---- */
/* Returns new cursor (≥1 advance) on match, -1 on fail.                    */
int64_t stmt_span_var(const char *varname, int64_t cursor,
                      const char *subj, int64_t subj_len) {
    DESCR_t v = NV_GET_fn(varname);
    const char *cs = VARVAL_fn(v);
    if (!cs || cursor >= subj_len) return -1;
    int64_t pos = cursor;
    while (pos < subj_len) {
        char c = subj[pos];
        if (!strchr(cs, c)) break;
        pos++;
    }
    if (pos == cursor) return -1;   /* no chars matched */
    return pos;
}

/* ---- BREAK(variable) — scan chars NOT IN charset, return new cursor or -1 */
int64_t stmt_break_var(const char *varname, int64_t cursor,
                       const char *subj, int64_t subj_len) {
    DESCR_t v = NV_GET_fn(varname);
    const char *cs = VARVAL_fn(v);
    if (!cs) cs = "";
    int64_t pos = cursor;
    while (pos < subj_len) {
        char c = subj[pos];
        if (strchr(cs, c)) break;
        pos++;
    }
    if (pos >= subj_len) return -1;  /* never hit delimiter — fail */
    return pos;                       /* zero advance OK: cursor already at delimiter */
}

/* ---- BREAKX(variable) — like BREAK but fails on zero advance ---- */
int64_t stmt_breakx_var(const char *varname, int64_t cursor,
                        const char *subj, int64_t subj_len) {
    DESCR_t v = NV_GET_fn(varname);
    const char *cs = VARVAL_fn(v);
    if (!cs) cs = "";
    int64_t pos = cursor;
    while (pos < subj_len) {
        char c = subj[pos];
        if (strchr(cs, c)) break;
        pos++;
    }
    if (pos >= subj_len) return -1;
    if (pos == cursor)   return -1;  /* BREAKX: zero advance fails */
    return pos;
}

/* ---- BREAKX(literal) — same, literal charset ---- */
int64_t stmt_breakx_lit(const char *cs, int64_t cursor,
                        const char *subj, int64_t subj_len) {
    if (!cs) cs = "";
    int64_t pos = cursor;
    while (pos < subj_len) {
        char c = subj[pos];
        if (strchr(cs, c)) break;
        pos++;
    }
    if (pos >= subj_len) return -1;
    if (pos == cursor)   return -1;
    return pos;
}

/* ---- ANY(variable) — match one char IN charset, return 1/0 ---- */
int stmt_any_var(const char *varname, int64_t cursor,
                 const char *subj, int64_t subj_len) {
    DESCR_t v = NV_GET_fn(varname);
    const char *cs = VARVAL_fn(v);
    if (!cs || cursor >= subj_len) return 0;
    return strchr(cs, subj[cursor]) ? 1 : 0;
}

/* ---- NOTANY(variable) — match one char NOT IN charset, return 1/0 ---- */
int stmt_notany_var(const char *varname, int64_t cursor,
                    const char *subj, int64_t subj_len) {
    DESCR_t v = NV_GET_fn(varname);
    const char *cs = VARVAL_fn(v);
    if (!cs || cursor >= subj_len) return 0;
    return strchr(cs, subj[cursor]) ? 0 : 1;
}

/* ---- @VAR — cursor position capture: store cursor as integer into VAR ---- */
void stmt_at_capture(const char *varname, int64_t cursor) {
    NV_SET_fn(varname, INTVAL(cursor));
}

/* ---- fail test ---- */

int stmt_is_fail(DESCR_t v) {
    return IS_FAIL_fn(v);
}

/* ---- variable get / set ---- */

DESCR_t stmt_get(const char *name) {
    /* Strip leading & so &KEYWORD routes to NV_GET_fn("KEYWORD") */
    if (name && name[0] == '&') name++;
    return NV_GET_fn(name);
}

void stmt_set(const char *name, DESCR_t v) {
    /* Strip leading & so &KEYWORD routes to NV_SET_fn("KEYWORD") */
    if (name && name[0] == '&') name++;
    NV_SET_fn(name, v);
}

/* ---- OUTPUT ---- */

void stmt_output(DESCR_t val) {
    NV_SET_fn("OUTPUT", val);
}

/* ---- INPUT ---- */

DESCR_t stmt_input(void) {
    return NV_GET_fn("INPUT");
}

/* ---- string concatenation ---- */

DESCR_t stmt_concat(DESCR_t a, DESCR_t b) {
    /* Propagate failure: if either argument is FAILDESCR, the concat fails.
     * This ensures DIFFER(X,Y) CONCAT rest fails when X==Y. */
    if (IS_FAIL_fn(a)) return FAILDESCR;
    if (IS_FAIL_fn(b)) return FAILDESCR;
    /* Both must be string-ish; convert integers to string first */
    const char *sa = VARVAL_fn(a);
    const char *sb = VARVAL_fn(b);
    if (!sa) sa = "";
    if (!sb) sb = "";
    size_t la = strlen(sa), lb = strlen(sb);
    char *buf = GC_MALLOC_ATOMIC(la + lb + 1);
    memcpy(buf, sa, la);
    memcpy(buf + la, sb, lb);
    buf[la + lb] = '\0';
    return STRVAL(buf);
}

/* ---- program end ---- */

void stmt_finish(void) {
    /* nothing — placeholder matching C backend's finish() */
}

/* ---- computed goto dispatch ---- */

/*
 * stmt_goto_dispatch: resolve a DESCR_t label value to an integer index
 * into a caller-provided name table.  Returns the 0-based index of the
 * matching label name, or -1 if not found.
 *
 * The ASM emitter generates a name table in .data and calls this function;
 * the returned index drives an indirect jmp via a jump table in .text.
 *
 * Usage in emitted ASM (pseudocode):
 *   lea  rdi, [rel computed_expr_result]   ; DESCR_t low
 *   mov  rsi, [computed_expr_result+8]     ; DESCR_t high
 *   lea  rdx, [rel label_name_table]       ; char** table
 *   mov  rcx, N                            ; number of entries
 *   call stmt_goto_dispatch
 *   ; rax = index (0..N-1) or -1
 *   cmp  rax, -1
 *   je   .no_match
 *   jmp  [rel jump_table + rax*8]
 */
int stmt_goto_dispatch(DESCR_t label_val, const char **names, int n) {
    const char *s = VARVAL_fn(label_val);
    if (!s || !*s) return -1;
    /* strip quotes if present */
    char buf[512]; size_t j = 0;
    for (size_t i = 0; s[i] && j < sizeof(buf)-1; i++) {
        if (s[i] == '\'' || s[i] == '"') continue;
        buf[j++] = s[i];
    }
    buf[j] = '\0';
    for (int i = 0; i < n; i++) {
        if (strcasecmp(buf, names[i]) == 0) return i;
    }
    return -1;
}

/* ---- arithmetic helpers ---- */

/* stmt_add_int: integer addition on DESCR_t values.
 * Both operands must be numeric (DT_I or DT_S convertible). */
DESCR_t stmt_add_int(DESCR_t a, DESCR_t b) {
    int64_t ia = (a.v == DT_I) ? a.i : (a.s ? atoll(a.s) : 0);
    int64_t ib = (b.v == DT_I) ? b.i : (b.s ? atoll(b.s) : 0);
    return INTVAL(ia + ib);
}

/* stmt_gt: returns 1 (success) if a > b, 0 (fail) otherwise */
int stmt_gt(DESCR_t a, DESCR_t b) {
    int64_t ia = (a.v == DT_I) ? a.i : (a.s ? atoll(a.s) : 0);
    int64_t ib = (b.v == DT_I) ? b.i : (b.s ? atoll(b.s) : 0);
    return ia > ib;
}

/* stmt_apply: call a named SNOBOL4 builtin function with an array of DESCR_t args.
 * Returns FAILDESCR on failure. */
DESCR_t stmt_apply(const char *name, DESCR_t *args, int nargs) {
    return APPLY_fn(name, args, nargs);
}

/* ---- Pattern match subject setup ---- */

/* Byrd box globals — defined in the .s file, extern here so C can write them */
extern uint64_t cursor;
extern uint64_t subject_len_val;
extern char     subject_data[65536];

/* stmt_setup_subject: copy DESCR_t string value into subject_data flat buffer,
 * set subject_len_val, reset cursor to 0.
 * Called from ASM before jmp root_alpha for each pattern-match statement.
 * Returns 0 on success, 1 if subject is FAIL/null (skip the match). */
int stmt_setup_subject(DESCR_t subj) {
    const char *s = VARVAL_fn(subj);
    if (!s) s = "";
    size_t len = strlen(s);
    if (len >= 65536) len = 65535;
    memcpy(subject_data, s, len);
    subject_data[len] = '\0';
    subject_len_val = (uint64_t)len;
    cursor = 0;
    return 0;
}

/* stmt_apply_replacement: after a successful match, write the replacement
 * value back into the subject variable (name given as string).
 * For now: stores the replacement DESCR_t into the named variable. */
void stmt_apply_replacement(const char *varname, DESCR_t repl) {
    if (varname && *varname)
        NV_SET_fn(varname, repl);
}

/* stmt_apply_replacement_splice: SNOBOL4 subject replacement semantics.
 * Reconstructs the full subject as: subject[0..match_start] + repl + subject[match_end..end]
 * and writes the result back into the subject variable.
 * match_start = position where the scan attempt began (scan_start_N bss slot).
 * match_end   = cursor (position after successful match).
 * Called from APPLY_REPL_SPLICE macro in the pattern γ path. */
void stmt_apply_replacement_splice(const char *varname, DESCR_t repl,
                                   uint64_t match_start, uint64_t match_end) {
    if (!varname || !*varname) return;
    /* Collect replacement string */
    const char *rstr = "";
    size_t rlen = 0;
    if (repl.v == DT_S && repl.s) { rstr = repl.s; rlen = strlen(rstr); }
    else if (repl.v == DT_I) {
        static char ibuf[64];
        snprintf(ibuf, sizeof ibuf, "%ld", (long)repl.i);
        rstr = ibuf; rlen = strlen(rstr);
    } else if (repl.v == DT_R) {
        static char rbuf[64];
        snprintf(rbuf, sizeof rbuf, "%g", repl.r);
        rstr = rbuf; rlen = strlen(rstr);
    } else if (repl.v == DT_SNUL) {
        rstr = ""; rlen = 0;
    }
    /* Bounds-check */
    if (match_start > subject_len_val) match_start = subject_len_val;
    if (match_end   > subject_len_val) match_end   = subject_len_val;
    if (match_end   < match_start)     match_end   = match_start;
    size_t prefix_len = (size_t)match_start;
    size_t suffix_len = (size_t)(subject_len_val - match_end);
    size_t total = prefix_len + rlen + suffix_len;
    char *out = GC_MALLOC_ATOMIC(total + 1);
    if (!out) return;
    memcpy(out, subject_data, prefix_len);
    memcpy(out + prefix_len, rstr, rlen);
    memcpy(out + prefix_len + rlen, subject_data + match_end, suffix_len);
    out[total] = '\0';
    NV_SET_fn(varname, STRVAL(out));
}

/* stmt_match_var: dynamic indirect pattern match (*VAR).

 * Fetches the string value of SNOBOL4 variable 'varname', then tries to match
 * it as a literal at the current cursor position in subject_data.
 * Returns 1 on success (cursor advanced), 0 on failure.
 * Used by E_INDR nodes where the variable holds a plain string, not a named pattern. */
int stmt_match_var(const char *varname) {
    DESCR_t val = NV_GET_fn(varname);
    const char *s = VARVAL_fn(val);
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0) return 1; /* empty string always matches — advance 0 */
    if (cursor + len > subject_len_val) return 0;
    if (memcmp(subject_data + cursor, s, len) != 0) return 0;
    cursor += (uint64_t)len;
    return 1;
}

/* ---- capture variable materialisation ---- */

/* stmt_set_capture: after a DOL/NAM pattern match, copy the captured
 * byte span from cap_VAR_buf (length cap_VAR_len) into the SNOBOL4
 * variable named varname.
 * Called from ASM in the pattern γ path before goto S-target. */
void stmt_set_capture(const char *varname, const char *buf, uint64_t len) {
    if (!varname || !buf) return;
    if (len == (uint64_t)-1) return; /* sentinel: no capture */
    char *s = GC_MALLOC_ATOMIC(len + 1);
    if (!s) return;
    memcpy(s, buf, len);
    s[len] = '\0';
    NV_SET_fn(varname, STRVAL(s));
}

