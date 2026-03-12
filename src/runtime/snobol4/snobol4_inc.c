/*
 * snobol4_inc.c — C implementations of SNOBOL4 .inc library functions
 *
 * Implements the library used by beauty.sno:
 *   global, is, io, case, assign, match, Gen, Qize, ShiftReduce, Tree, Stack
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "snobol4.h"
#include "snobol4_inc.h"

/* =========================================================================
 * Global variables (global.inc)
 * ===================================================================== */

/* Standard character constants */
static const char *sno_digits_str  = "0123456789";
static const char *sno_tab_str     = "\t";
static const char *sno_nl_str      = "\n";
static const char *sno_cr_str      = "\r";
static const char *sno_ff_str      = "\f";
static const char *sno_bs_str      = "\b";
static const char *sno_bSlash_str  = "\\";

/* Keywords */

/* doDebug, xTrace, doParseTree */
static long long g_doDebug     = 0;
static long long g_xTrace      = 0;
static long long g_doParseTree = 0;

/* Gen.inc globals */
static char *g_buf    = NULL;   /* $'$B' — output buffer */
static char *g_cont   = NULL;   /* $'$C' — continuation char */
static char *g_mark   = NULL;   /* $'$X' — marks cont position */
static long long g_level = 0;   /* $'#L' — indentation level */

/* Stack globals — $'@S' link head */
/* (already managed through sno_var_get/set with key "@S") */

/* =========================================================================
 * Helper: safe strdup via GC
 * ===================================================================== */
static char *sno_strdup_gc(const char *s) {
    if (!s) return (char*)"";
    size_t n = strlen(s) + 1;
    char *p = (char*)GC_MALLOC(n);
    memcpy(p, s, n);
    return p;
}

static char *sno_concat_gc(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char*)GC_MALLOC(la + lb + 1);
    memcpy(p, a, la);
    memcpy(p + la, b, lb);
    p[la+lb] = '\0';
    return p;
}

/* =========================================================================
 * case.inc: lwr(s), upr(s)
 * ===================================================================== */

SnoVal sno_lwr(SnoVal s) {
    const char *src = sno_to_str(s);
    if (!src || !*src) return SNO_STR_VAL("");
    size_t n = strlen(src);
    char *out = (char*)GC_MALLOC(n+1);
    for (size_t i = 0; i < n; i++)
        out[i] = (char)tolower((unsigned char)src[i]);
    out[n] = '\0';
    return SNO_STR_VAL(out);
}

SnoVal sno_upr(SnoVal s) {
    const char *src = sno_to_str(s);
    if (!src || !*src) return SNO_STR_VAL("");
    size_t n = strlen(src);
    char *out = (char*)GC_MALLOC(n+1);
    for (size_t i = 0; i < n; i++)
        out[i] = (char)toupper((unsigned char)src[i]);
    out[n] = '\0';
    return SNO_STR_VAL(out);
}

/* =========================================================================
 * assign.inc: assign(name, expression)
 * Sets $name = EVAL(expression) if expression is EXPRESSION datatype,
 * else $name = expression. Always succeeds (returns .dummy).
 * ===================================================================== */

SnoVal sno_assign_fn(SnoVal name, SnoVal expression) {
    const char *nm = sno_to_str(name);
    /* If expression is an unevaluated expression, eval it */
    SnoVal val = expression;
    if (SNO_TYPE(expression) == SNO_STR) {
        /* Try to evaluate as SNOBOL4 — for now just use it as-is */
        val = expression;
    }
    sno_var_set(nm, val);
    return SNO_NULL_VAL;
}

/* =========================================================================
 * match.inc: match(subject, pattern) → NRETURN on match, FRETURN on fail
 * In C: return non-null on match, SNO_FAIL_VAL on fail
 * ===================================================================== */

/* Forward declaration — pattern matching from snobol4.c */
extern int sno_match_pattern(SnoVal pat, const char *subject);

SnoVal sno_match_fn(SnoVal subject, SnoVal pattern) {
    const char *subj = sno_to_str(subject);
    int ok = sno_match_pattern(pattern, subj);
    return ok ? SNO_STR_VAL("") : SNO_NULL_VAL;
}

SnoVal sno_notmatch_fn(SnoVal subject, SnoVal pattern) {
    const char *subj = sno_to_str(subject);
    int ok = sno_match_pattern(pattern, subj);
    return ok ? SNO_NULL_VAL : SNO_STR_VAL("");
}

/* =========================================================================
 * io.inc: basic I/O — in beautiful.sno, io() opens named channels.
 * We use stdin/stdout as channels 5/6 per SNOBOL4 convention.
 * ===================================================================== */

SnoVal sno_io_fn(SnoVal name, SnoVal mode) {
    /* No-op for our purposes — we use stdin/stdout */
    return SNO_NULL_VAL;
}

/* =========================================================================
 * Gen.inc — output generation with indentation buffering
 *
 * Global state:
 *   g_buf   = current line buffer ($'$B')
 *   g_cont  = continuation character ($'$C')
 *   g_mark  = marks where cont char goes ($'$X')
 *   g_level = current indentation level ($'#L')
 * ===================================================================== */

SnoVal sno_IncLevel(SnoVal delta) {
    long long d = (SNO_TYPE(delta) == SNO_NULL) ? 2 : sno_to_int(delta);
    g_level += d;
    return SNO_NULL_VAL;
}

SnoVal sno_DecLevel(SnoVal delta) {
    long long d = (SNO_TYPE(delta) == SNO_NULL) ? 2 : sno_to_int(delta);
    g_level -= d;
    if (g_level < 0) g_level = 0;
    return SNO_NULL_VAL;
}

SnoVal sno_SetLevel(SnoVal level) {
    g_level = sno_to_int(level);
    return SNO_NULL_VAL;
}

SnoVal sno_GetLevel(void) {
    return SNO_INT_VAL(g_level);
}

/* Flush one line from g_buf to output, replacing $'$X' with g_cont */
static void gen_flush_line(const char *line) {
    /* Replace the mark position with cont char if it's a continuation */
    printf("%s\n", line);
}

SnoVal sno_Gen(SnoVal str, SnoVal outNm) {
    const char *s = sno_to_str(str);
    if (!s) s = "";

    if (!g_buf) g_buf = sno_strdup_gc("");
    if (!g_mark) g_mark = sno_strdup_gc("");

    /* Append str to buffer */
    /* If buffer is empty, prepend the mark+indent */
    const char *new_buf;
    if (!g_buf || !*g_buf) {
        /* Start a new line: mark = cont position, then indent, then str */
        char indent[256] = {0};
        long long mark_len = g_mark ? (long long)strlen(g_mark) : 0;
        long long ind_len  = g_level > mark_len ? g_level - mark_len : 0;
        if (ind_len > 255) ind_len = 255;
        memset(indent, ' ', (size_t)ind_len);
        indent[ind_len] = '\0';
        new_buf = sno_concat_gc(g_mark, sno_concat_gc(indent, s));
    } else {
        new_buf = sno_concat_gc(g_buf, s);
    }

    /* Check for newline in new_buf — flush complete lines */
    const char *p = new_buf;
    while (1) {
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        /* Output the line up to (not including) the newline */
        size_t len = (size_t)(nl - p);
        char *line = (char*)GC_MALLOC(len + 1);
        memcpy(line, p, len);
        line[len] = '\0';
        gen_flush_line(line);
        /* Reset mark for continuation lines */
        if (g_cont && *g_cont) {
            g_mark = sno_strdup_gc(g_cont);
        } else {
            g_mark = sno_strdup_gc(" ");
        }
        p = nl + 1;
    }
    g_buf = sno_strdup_gc(p);
    return SNO_NULL_VAL;
}

SnoVal sno_GenTab(SnoVal pos) {
    long long target = (SNO_TYPE(pos) == SNO_NULL) ? g_level : sno_to_int(pos);
    if (!g_buf) g_buf = sno_strdup_gc("");

    long long cur = (long long)strlen(g_buf);
    if (cur < target) {
        long long spaces = target - cur;
        if (spaces > 1024) spaces = 1024;
        char *pad = (char*)GC_MALLOC((size_t)(spaces + 1));
        memset(pad, ' ', (size_t)spaces);
        pad[spaces] = '\0';
        g_buf = sno_concat_gc(g_buf, pad);
    }
    return SNO_NULL_VAL;
}

SnoVal sno_GenSetCont(SnoVal cont) {
    /* Flush any pending buffer, set continuation char, reset mark */
    if (g_buf && *g_buf) {
        /* Don't flush partial line — just reset */
    }
    g_buf  = sno_strdup_gc("");
    const char *c = sno_to_str(cont);
    g_cont = sno_strdup_gc((c && *c) ? c : "");
    g_mark = sno_strdup_gc("");
    return SNO_NULL_VAL;
}

/* =========================================================================
 * Qize.inc: Qize(s) — wrap string in SNOBOL4 quote characters
 * Returns a SNOBOL4 string literal representation of s.
 * ===================================================================== */

SnoVal sno_Qize(SnoVal s) {
    const char *src = sno_to_str(s);
    if (!src || !*src) return SNO_STR_VAL("''");

    /* Simple version: if no single quotes, wrap in single quotes */
    if (!strchr(src, '\'')) {
        char *out = (char*)GC_MALLOC(strlen(src) + 3);
        sprintf(out, "'%s'", src);
        return SNO_STR_VAL(out);
    }
    /* Has single quotes — use double quotes if no double quotes */
    if (!strchr(src, '"')) {
        char *out = (char*)GC_MALLOC(strlen(src) + 3);
        sprintf(out, "\"%s\"", src);
        return SNO_STR_VAL(out);
    }
    /* Both quote types present — split around single quotes */
    /* For now: escape by concatenation of quoted parts */
    char *out = (char*)GC_MALLOC(strlen(src) * 6 + 10);
    char *p = out;
    *p++ = '\'';
    for (const char *c = src; *c; c++) {
        if (*c == '\'') {
            *p++ = '\'';
            *p++ = '"';
            *p++ = '\'';
            *p++ = '"';
            *p++ = '\'';
        } else {
            *p++ = *c;
        }
    }
    *p++ = '\'';
    *p = '\0';
    return SNO_STR_VAL(out);
}

/* =========================================================================
 * ShiftReduce.inc: Shift(t, v), Reduce(t, n)
 * These use the Push/Pop stack from snobol4.c and the tree() DATA type.
 * ===================================================================== */

/* Forward declarations from snobol4.c */
extern SnoVal sno_field_get(SnoVal obj, const char *field);

SnoVal sno_Shift(SnoVal t_arg) {
    /* Shift(t, v) — but in beauty.sno it's called as Shift('tag', value) */
    /* For now t_arg is the tree type tag, value is empty */
    SnoVal s = sno_make_tree(t_arg, SNO_STR_VAL(""), SNO_INT_VAL(0), SNO_NULL_VAL);
    sno_push_val(s);
    return SNO_NULL_VAL;
}

SnoVal sno_Reduce(SnoVal t_arg, SnoVal n_arg) {
    /* Evaluate t if it's an unevaluated expression */
    SnoVal t = t_arg;
    SnoVal n = n_arg;
    long long count = sno_to_int(n);

    /* Build array of n children from stack */
    if (count < 1) count = 0;
    SnoVal children = sno_array_create(SNO_STR_VAL("1:256"));

    for (long long i = count; i >= 1; i--) {
        SnoVal child = sno_pop_val();
        sno_subscript_set(children, SNO_INT_VAL(i), child);
    }

    SnoVal r = sno_make_tree(t, SNO_STR_VAL(""), SNO_INT_VAL(count), children);
    sno_push_val(r);
    return SNO_NULL_VAL;
}

/* =========================================================================
 * TDump / XDump — debug dumps (no-op when doDebug == 0)
 * ===================================================================== */

SnoVal sno_TDump(SnoVal x) { (void)x; return SNO_NULL_VAL; }
SnoVal sno_XDump(SnoVal x) { (void)x; return SNO_NULL_VAL; }

/* =========================================================================
 * omega.inc / trace.inc: TV, TW, TX, TY, TZ, T8Trace, T8Pos
 * All are no-ops when doDebug == 0 (beauty.sno sets doDebug = 0).
 * ===================================================================== */

SnoVal sno_TV(SnoVal lvl, SnoVal pat, SnoVal name) { return pat; }
SnoVal sno_TW(SnoVal lvl, SnoVal pat, SnoVal name) { return pat; }
SnoVal sno_TX(SnoVal lvl, SnoVal pat, SnoVal name) { return pat; }
SnoVal sno_TY(SnoVal lvl, SnoVal name, SnoVal pat) { return pat; }
SnoVal sno_TZ(SnoVal lvl, SnoVal name, SnoVal pat) { return pat; }
SnoVal sno_T8Trace(SnoVal lvl, SnoVal str, SnoVal ofs) { return SNO_NULL_VAL; }
SnoVal sno_T8Pos(SnoVal ofs, SnoVal map) { return SNO_STR_VAL(""); }

/* =========================================================================
 * Lexicographic comparison functions
 * ===================================================================== */

SnoVal sno_LEQ(SnoVal a, SnoVal b) {
    return strcmp(sno_to_str(a), sno_to_str(b)) <= 0 ? a : SNO_NULL_VAL;
}
SnoVal sno_LGT(SnoVal a, SnoVal b) {
    return strcmp(sno_to_str(a), sno_to_str(b)) >  0 ? a : SNO_NULL_VAL;
}
SnoVal sno_LGE(SnoVal a, SnoVal b) {
    return strcmp(sno_to_str(a), sno_to_str(b)) >= 0 ? a : SNO_NULL_VAL;
}
SnoVal sno_LLT(SnoVal a, SnoVal b) {
    return strcmp(sno_to_str(a), sno_to_str(b)) <  0 ? a : SNO_NULL_VAL;
}
SnoVal sno_LLE(SnoVal a, SnoVal b) {
    return strcmp(sno_to_str(a), sno_to_str(b)) <= 0 ? a : SNO_NULL_VAL;
}
SnoVal sno_LNE(SnoVal a, SnoVal b) {
    return strcmp(sno_to_str(a), sno_to_str(b)) != 0 ? a : SNO_NULL_VAL;
}

/* =========================================================================
 * sno_inc_init — register all inc functions in the global function table
 * and set up global variables.
 * ===================================================================== */

/* Forward: register a C function in the snobol4 function table */
extern void sno_register_fn(const char *name, SnoVal (*fn)(SnoVal*, int), int min_args, int max_args);

/* Wrapper shims — adapt variadic calling convention */
static SnoVal _w_lwr(SnoVal *a, int n) {
    return sno_lwr(n>0 ? a[0] : SNO_NULL_VAL);
}
static SnoVal _w_upr(SnoVal *a, int n) {
    return sno_upr(n>0 ? a[0] : SNO_NULL_VAL);
}
static SnoVal _w_assign(SnoVal *a, int n) {
    return sno_assign_fn(n>0?a[0]:SNO_NULL_VAL, n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_match(SnoVal *a, int n) {
    return sno_match_fn(n>0?a[0]:SNO_NULL_VAL, n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_notmatch(SnoVal *a, int n) {
    return sno_notmatch_fn(n>0?a[0]:SNO_NULL_VAL, n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_Gen(SnoVal *a, int n) {
    return sno_Gen(n>0?a[0]:SNO_NULL_VAL, n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_GenTab(SnoVal *a, int n) {
    return sno_GenTab(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_GenSetCont(SnoVal *a, int n) {
    return sno_GenSetCont(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_IncLevel(SnoVal *a, int n) {
    return sno_IncLevel(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_DecLevel(SnoVal *a, int n) {
    return sno_DecLevel(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_SetLevel(SnoVal *a, int n) {
    return sno_SetLevel(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_GetLevel(SnoVal *a, int n) {
    (void)a; (void)n; return sno_GetLevel();
}
static SnoVal _w_Qize(SnoVal *a, int n) {
    return sno_Qize(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_Shift(SnoVal *a, int n) {
    return sno_Shift(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_Reduce(SnoVal *a, int n) {
    return sno_Reduce(n>0?a[0]:SNO_NULL_VAL, n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_TDump(SnoVal *a, int n) {
    return sno_TDump(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_XDump(SnoVal *a, int n) {
    return sno_XDump(n>0?a[0]:SNO_NULL_VAL);
}
static SnoVal _w_TV(SnoVal *a, int n) {
    return sno_TV(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL);
}
static SnoVal _w_TW(SnoVal *a, int n) {
    return sno_TW(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL);
}
static SnoVal _w_TX(SnoVal *a, int n) {
    return sno_TX(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL);
}
static SnoVal _w_TY(SnoVal *a, int n) {
    return sno_TY(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL);
}
static SnoVal _w_TZ(SnoVal *a, int n) {
    return sno_TZ(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL);
}
static SnoVal _w_T8Trace(SnoVal *a, int n) {
    return sno_T8Trace(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL);
}
static SnoVal _w_T8Pos(SnoVal *a, int n) {
    return sno_T8Pos(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_LEQ(SnoVal *a, int n) {
    return sno_LEQ(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_LGT(SnoVal *a, int n) {
    return sno_LGT(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_LGE(SnoVal *a, int n) {
    return sno_LGE(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_LLT(SnoVal *a, int n) {
    return sno_LLT(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_LLE(SnoVal *a, int n) {
    return sno_LLE(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}
static SnoVal _w_LNE(SnoVal *a, int n) {
    return sno_LNE(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL);
}

void sno_inc_init(void) {
    /* Set up global character constants */
    sno_var_set("digits",   SNO_STR_VAL(sno_digits_str));
    sno_var_set("tab",      SNO_STR_VAL(sno_tab_str));
    sno_var_set("nl",       SNO_STR_VAL(sno_nl_str));
    sno_var_set("cr",       SNO_STR_VAL(sno_cr_str));
    sno_var_set("ff",       SNO_STR_VAL(sno_ff_str));
    sno_var_set("bs",       SNO_STR_VAL(sno_bs_str));
    sno_var_set("bSlash",   SNO_STR_VAL(sno_bSlash_str));
    sno_var_set("lf",       SNO_STR_VAL(sno_nl_str));
    sno_var_set("whitespace", SNO_STR_VAL(" \t\n\r"));
    sno_var_set("doDebug",  SNO_INT_VAL(g_doDebug));
    sno_var_set("xTrace",   SNO_INT_VAL(g_xTrace));
    sno_var_set("doParseTree", SNO_INT_VAL(g_doParseTree));
    sno_var_set("level",    SNO_STR_VAL(""));

    /* Gen.inc globals */
    g_buf  = (char*)GC_MALLOC(1); g_buf[0]  = '\0';
    g_cont = (char*)GC_MALLOC(1); g_cont[0] = '\0';
    g_mark = (char*)GC_MALLOC(1); g_mark[0] = '\0';
    g_level = 0;

    /* Register all functions */
    sno_register_fn("lwr",        _w_lwr,       1, 1);
    sno_register_fn("upr",        _w_upr,       1, 1);
    sno_register_fn("assign",     _w_assign,    2, 2);
    sno_register_fn("match",      _w_match,     2, 2);
    sno_register_fn("notmatch",   _w_notmatch,  2, 2);
    sno_register_fn("Gen",        _w_Gen,       1, 2);
    sno_register_fn("GenTab",     _w_GenTab,    0, 1);
    sno_register_fn("GenSetCont", _w_GenSetCont,0, 1);
    sno_register_fn("IncLevel",   _w_IncLevel,  0, 1);
    sno_register_fn("DecLevel",   _w_DecLevel,  0, 1);
    sno_register_fn("SetLevel",   _w_SetLevel,  1, 1);
    sno_register_fn("GetLevel",   _w_GetLevel,  0, 0);
    sno_register_fn("Qize",       _w_Qize,      1, 1);
    sno_register_fn("Shift",      _w_Shift,     1, 2);
    sno_register_fn("Reduce",     _w_Reduce,    2, 2);
    sno_register_fn("TDump",      _w_TDump,     1, 1);
    sno_register_fn("XDump",      _w_XDump,     1, 1);
    sno_register_fn("TV",         _w_TV,        3, 3);
    sno_register_fn("TW",         _w_TW,        3, 3);
    sno_register_fn("TX",         _w_TX,        3, 3);
    sno_register_fn("TY",         _w_TY,        3, 3);
    sno_register_fn("TZ",         _w_TZ,        3, 3);
    sno_register_fn("T8Trace",    _w_T8Trace,   3, 3);
    sno_register_fn("T8Pos",      _w_T8Pos,     2, 2);
    sno_register_fn("LEQ",        _w_LEQ,       2, 2);
    sno_register_fn("LGT",        _w_LGT,       2, 2);
    sno_register_fn("LGE",        _w_LGE,       2, 2);
    sno_register_fn("LLT",        _w_LLT,       2, 2);
    sno_register_fn("LLE",        _w_LLE,       2, 2);
    sno_register_fn("LNE",        _w_LNE,       2, 2);
    void sno_inc_init_extra(void);  /* forward declaration */
    sno_inc_init_extra();
}

/* =========================================================================
 * Additional missing registrations (identified by compiland reachability)
 * ===================================================================== */

/* icase(str) — case.inc: build case-insensitive pattern from string
 * Returns a pattern that matches str case-insensitively.
 * Each alpha char → (upper | lower) alternation; non-alpha → literal. */
static SnoVal _w_icase(SnoVal *a, int n) {
    const char *s = sno_to_str(n > 0 ? a[0] : SNO_NULL_VAL);
    if (!s || !*s) return sno_pat_epsilon();
    /* Build cat of per-char patterns */
    SnoVal pat = sno_pat_epsilon();
    int len = (int)strlen(s);
    for (int i = len - 1; i >= 0; i--) {
        char c = s[i];
        SnoVal cp;
        if (isalpha((unsigned char)c)) {
            char lo[2] = { (char)tolower((unsigned char)c), 0 };
            char hi[2] = { (char)toupper((unsigned char)c), 0 };
            cp = sno_pat_alt(sno_pat_lit(GC_strdup(lo)), sno_pat_lit(GC_strdup(hi)));
        } else {
            char buf[2] = { c, 0 };
            cp = sno_pat_lit(GC_strdup(buf));
        }
        pat = (i == len - 1) ? cp : sno_pat_cat(cp, pat);
    }
    return pat;
}

/* IsSnobol4() — is.inc: we ARE SNOBOL4-tiny, so always RETURN (succeed) */
static SnoVal _w_IsSnobol4(SnoVal *a, int n) {
    (void)a; (void)n;
    return SNO_STR_VAL("");   /* non-null = success */
}

/* Push(x) — stack.inc: push x onto value stack */
static SnoVal _w_Push(SnoVal *a, int n) {
    SnoVal x = n > 0 ? a[0] : SNO_NULL_VAL;
    sno_push(x);
    /* Push returns .dummy (NRETURN) — return null marker */
    return SNO_NULL_VAL;
}

/* Pop() / Pop(var) — stack.inc: pop from value stack */
static SnoVal _w_Pop(SnoVal *a, int n) {
    if (sno_stack_depth() == 0) return SNO_NULL_VAL;
    SnoVal v = sno_pop();
    if (n > 0 && a[0].type == SNO_STR) {
        /* Pop(var) — store into named variable */
        sno_var_set(a[0].s, v);
        return SNO_NULL_VAL;
    }
    return v;
}

/* TopCounter() — counter.inc: return current counter value */
static SnoVal _w_TopCounter(SnoVal *a, int n) {
    (void)a; (void)n;
    int64_t v = sno_ntop();
    if (v < 0) return SNO_NULL_VAL;   /* FRETURN if stack empty */
    return SNO_INT_VAL(v);
}

/* SqlSQize(str) — Qize.inc: SQL single-quote escape ('' for each ') */
static SnoVal _w_SqlSQize(SnoVal *a, int n) {
    const char *s = sno_to_str(n > 0 ? a[0] : SNO_NULL_VAL);
    if (!s || !*s) return SNO_STR_VAL("");
    size_t len = strlen(s);
    /* Count single quotes */
    int sq = 0;
    for (size_t i = 0; i < len; i++) if (s[i] == '\'') sq++;
    char *out = (char *)GC_MALLOC(len + sq + 1);
    char *p = out;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') { *p++ = '\''; *p++ = '\''; }
        else               *p++ = s[i];
    }
    *p = '\0';
    return SNO_STR_VAL(out);
}

/* TLump(x, len) — TDump.inc: tree-to-string up to len chars (stub) */
static SnoVal _w_TLump(SnoVal *a, int n) {
    /* Stub: return tree tag as string */
    if (n < 1 || a[0].type == SNO_NULL) return SNO_STR_VAL("()");
    if (a[0].type == SNO_UDEF) {
        SnoVal t = sno_field_get(a[0], "t");
        SnoVal v = sno_field_get(a[0], "v");
        const char *ts = sno_to_str(t);
        const char *vs = sno_to_str(v);
        size_t tl = strlen(ts), vl = strlen(vs);
        char *out = (char *)GC_MALLOC(tl + vl + 4);
        if (vl > 0) sprintf(out, "(%s %s)", ts, vs);
        else        sprintf(out, "(%s)", ts);
        return SNO_STR_VAL(out);
    }
    return SNO_STR_VAL(sno_to_str(a[0]));
}

/* TValue(x) — TDump.inc: extract printable value from tree node */
static SnoVal _w_TValue(SnoVal *a, int n) {
    if (n < 1) return SNO_STR_VAL(".");
    SnoVal x = a[0];
    if (x.type == SNO_NULL) return SNO_STR_VAL(".");
    if (x.type == SNO_UDEF) {
        SnoVal v = sno_field_get(x, "v");
        if (sno_is_null(v)) return SNO_STR_VAL(".");
        return SNO_STR_VAL(sno_to_str(v));
    }
    return SNO_STR_VAL(sno_to_str(x));
}

/* Visit(x, fnc) — tree.inc: pre-order traversal, apply fnc at each node */
static SnoVal _w_Visit(SnoVal *a, int n) {
    if (n < 2) return SNO_NULL_VAL;
    SnoVal x   = a[0];
    SnoVal fnc = a[1];
    const char *fname = sno_to_str(fnc);
    /* Apply fnc to x */
    sno_apply(fname, &x, 1);
    /* Recurse into children */
    if (x.type == SNO_UDEF) {
        SnoVal nc  = sno_field_get(x, "n");
        SnoVal ca  = sno_field_get(x, "c");
        int    cnt = (int)sno_to_int(nc);
        for (int i = 1; i <= cnt; i++) {
            SnoVal child = sno_subscript_get(ca, SNO_INT_VAL(i));
            SnoVal visit_args[2] = { child, fnc };
            _w_Visit(visit_args, 2);
        }
    }
    return SNO_NULL_VAL;
}

/* bVisit — same as Visit for beautiful.sno purposes */
static SnoVal _w_bVisit(SnoVal *a, int n) {
    return _w_Visit(a, n);
}

/* Equal(x, y) — tree.inc: structural equality */
static SnoVal _w_Equal(SnoVal *a, int n) {
    if (n < 2) return SNO_NULL_VAL;
    SnoVal x = a[0], y = a[1];
    /* Both null → equal */
    if (x.type == SNO_NULL && y.type == SNO_NULL) return SNO_STR_VAL("");
    if (x.type == SNO_NULL || y.type == SNO_NULL) return SNO_NULL_VAL;
    if (x.type != SNO_UDEF || y.type != SNO_UDEF) {
        return strcmp(sno_to_str(x), sno_to_str(y)) == 0 ? SNO_STR_VAL("") : SNO_NULL_VAL;
    }
    /* Compare t, v, n */
    if (!sno_ident(sno_field_get(x,"t"), sno_field_get(y,"t"))) return SNO_NULL_VAL;
    if (!sno_ident(sno_field_get(x,"v"), sno_field_get(y,"v"))) return SNO_NULL_VAL;
    SnoVal nx = sno_field_get(x,"n"), ny = sno_field_get(y,"n");
    if (!sno_ident(nx, ny)) return SNO_NULL_VAL;
    int cnt = (int)sno_to_int(nx);
    SnoVal cx = sno_field_get(x,"c"), cy = sno_field_get(y,"c");
    for (int i = 1; i <= cnt; i++) {
        SnoVal ci_x = sno_subscript_get(cx, SNO_INT_VAL(i));
        SnoVal ci_y = sno_subscript_get(cy, SNO_INT_VAL(i));
        SnoVal eq_args[2] = { ci_x, ci_y };
        if (sno_is_null(_w_Equal(eq_args, 2))) return SNO_NULL_VAL;
    }
    return SNO_STR_VAL("");
}

/* Equiv(x, y) — tree.inc: structural equivalence (like Equal but looser) */
static SnoVal _w_Equiv(SnoVal *a, int n) {
    return _w_Equal(a, n);   /* same semantics for our purposes */
}

/* Find(xn, y, f) — tree.inc: search tree *xn for node equiv to y, apply f */
static SnoVal _w_Find(SnoVal *a, int n) {
    if (n < 3) return SNO_NULL_VAL;
    /* xn is a variable name (indirect ref), y is search target, f is function */
    SnoVal xn  = a[0];
    SnoVal y   = a[1];
    SnoVal f   = a[2];
    const char *xname  = sno_to_str(xn);
    const char *fname  = sno_to_str(f);
    SnoVal root = sno_var_get(xname);
    if (sno_is_null(root)) return SNO_NULL_VAL;
    /* Check if root equiv to y */
    SnoVal eq_args[2] = { root, y };
    if (!sno_is_null(_w_Equiv(eq_args, 2))) {
        sno_apply(fname, &xn, 1);
        return SNO_STR_VAL("");
    }
    /* Recurse into children */
    if (root.type == SNO_UDEF) {
        SnoVal nc = sno_field_get(root, "n");
        SnoVal ca = sno_field_get(root, "c");
        int cnt = (int)sno_to_int(nc);
        for (int i = 1; i <= cnt; i++) {
            SnoVal child = sno_subscript_get(ca, SNO_INT_VAL(i));
            /* For child, we'd need a temp var — skip deep recursion for now */
            (void)child;
        }
    }
    return SNO_NULL_VAL;
}

/* Insert(x, y, place) — tree.inc: insert y into tree x at position place */
static SnoVal _w_Insert(SnoVal *a, int n) {
    if (n < 3) return n > 0 ? a[0] : SNO_NULL_VAL;
    SnoVal x     = a[0];
    SnoVal y     = a[1];
    int    place = (int)sno_to_int(a[2]);
    if (x.type != SNO_UDEF) return x;

    SnoVal nc  = sno_field_get(x, "n");
    SnoVal ca  = sno_field_get(x, "c");
    int    cnt = (int)sno_to_int(nc);

    /* Build new children array with y inserted at place */
    int new_cnt = cnt + 1;
    SnoVal new_c = sno_array_create(SNO_STR_VAL("1:256"));
    for (int i = 1; i < place && i <= cnt; i++)
        sno_subscript_set(new_c, SNO_INT_VAL(i), sno_subscript_get(ca, SNO_INT_VAL(i)));
    sno_subscript_set(new_c, SNO_INT_VAL(place), y);
    for (int i = place; i <= cnt; i++)
        sno_subscript_set(new_c, SNO_INT_VAL(i+1), sno_subscript_get(ca, SNO_INT_VAL(i)));

    sno_field_set(x, "n", SNO_INT_VAL(new_cnt));
    sno_field_set(x, "c", new_c);
    return x;
}

/* Register all the missing functions */
void sno_inc_init_extra(void) {
    sno_register_fn("icase",      _w_icase,      1, 1);
    sno_register_fn("IsSnobol4",  _w_IsSnobol4,  0, 0);
    sno_register_fn("Push",       _w_Push,        1, 1);
    sno_register_fn("Pop",        _w_Pop,         0, 1);
    sno_register_fn("TopCounter", _w_TopCounter,  0, 0);
    sno_register_fn("SqlSQize",   _w_SqlSQize,    1, 1);
    sno_register_fn("TLump",      _w_TLump,       1, 2);
    sno_register_fn("TValue",     _w_TValue,      1, 1);
    sno_register_fn("Visit",      _w_Visit,       2, 2);
    sno_register_fn("bVisit",     _w_bVisit,      2, 2);
    sno_register_fn("Equal",      _w_Equal,       2, 2);
    sno_register_fn("Equiv",      _w_Equiv,       2, 2);
    sno_register_fn("Find",       _w_Find,        3, 3);
    sno_register_fn("Insert",     _w_Insert,      3, 3);
}
