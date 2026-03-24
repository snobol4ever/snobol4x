/*
 * mock_includes.c — C implementations of SNOBOL4 .inc library functions
 *
 * Implements the library used by beauty.sno:
 *   global, is, io, case, assign, MATCH_fn, Gen, Qize, ShiftReduce, TREEBLK_t, Stack
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "snobol4.h"
#include "mock_includes.h"

/* Shared sequence counter — defined in snobol4.c, used here for Shift/Reduce */
extern int _nseq;

/* =========================================================================
 * Global variables (global.inc)
 * ===================================================================== */

/* Standard character constants */
static const char *digits_str  = "0123456789";
static const char *tab_str     = "\t";
static const char *nl_str      = "\n";
static const char *cr_str      = "\r";
static const char *ff_str      = "\f";
static const char *bs_str      = "\b";
static const char *bSlash_str  = "\\";

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
/* (already managed through NV_GET_fn/set with key "@S") */

/* =========================================================================
 * Helper: safe strdup via GC
 * ===================================================================== */
static char *strdup_gc(const char *s) {
    if (!s) return (char*)"";
    size_t n = strlen(s) + 1;
    char *p = (char*)GC_MALLOC(n);
    memcpy(p, s, n);
    return p;
}

static char *concat_gc(const char *a, const char *b) {
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

DESCR_t lwr(DESCR_t s) {
    const char *src = VARVAL_fn(s);
    if (!src || !*src) return STRVAL("");
    size_t n = strlen(src);
    char *out = (char*)GC_MALLOC(n+1);
    for (size_t i = 0; i < n; i++)
        out[i] = (char)tolower((unsigned char)src[i]);
    out[n] = '\0';
    return STRVAL(out);
}

DESCR_t upr(DESCR_t s) {
    const char *src = VARVAL_fn(s);
    if (!src || !*src) return STRVAL("");
    size_t n = strlen(src);
    char *out = (char*)GC_MALLOC(n+1);
    for (size_t i = 0; i < n; i++)
        out[i] = (char)toupper((unsigned char)src[i]);
    out[n] = '\0';
    return STRVAL(out);
}

/* =========================================================================
 * assign.inc: assign(name, expression)
 * Sets $name = EVAL(expression) if expression is EXPRESSION datatype,
 * else $name = expression. Always succeeds (returns .dummy).
 * ===================================================================== */

DESCR_t assign_fn(DESCR_t name, DESCR_t expression) {
    const char *nm = VARVAL_fn(name);
    /* If expression is an unevaluated expression, EVAL_fn it */
    DESCR_t val = expression;
    if (STYPE(expression) == DT_S) {
        /* Try to evaluate as SNOBOL4 — for now just use it as-is */
        val = expression;
    }
    NV_SET_fn(nm, val);
    return NULVCL;
}

/* =========================================================================
 * MATCH_fn.inc: MATCH_fn(subject, pattern) → NRETURN on MATCH_fn, FRETURN on fail
 * In C: return non-null on MATCH_fn, FAILDESCR on fail
 * ===================================================================== */

/* Forward declaration — pattern matching from snobol4.c */
extern int match_pattern(DESCR_t pat, const char *subject);

DESCR_t match_fn(DESCR_t subject, DESCR_t pattern) {
    const char *subj = VARVAL_fn(subject);
    int ok = match_pattern(pattern, subj);
    return ok ? STRVAL("") : NULVCL;
}

DESCR_t notmatch_fn(DESCR_t subject, DESCR_t pattern) {
    const char *subj = VARVAL_fn(subject);
    int ok = match_pattern(pattern, subj);
    return ok ? NULVCL : STRVAL("");
}

/* =========================================================================
 * io.inc: basic I/O — in beautiful.sno, io() opens named channels.
 * We use stdin/stdout as channels 5/6 per SNOBOL4 convention.
 * ===================================================================== */

DESCR_t io_fn(DESCR_t name, DESCR_t mode) {
    /* No-op for our purposes — we use stdin/stdout */
    return NULVCL;
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

DESCR_t IncLevel(DESCR_t delta) {
    long long d = (STYPE(delta) == DT_SNUL) ? 2 : to_int(delta);
    g_level += d;
    return NULVCL;
}

DESCR_t DecLevel(DESCR_t delta) {
    long long d = (STYPE(delta) == DT_SNUL) ? 2 : to_int(delta);
    g_level -= d;
    if (g_level < 0) g_level = 0;
    return NULVCL;
}

DESCR_t SetLevel(DESCR_t level) {
    g_level = to_int(level);
    return NULVCL;
}

DESCR_t GetLevel(void) {
    return INTVAL(g_level);
}

/* Flush one line from g_buf to output, replacing $'$X' with g_cont */
static void gen_flush_line(const char *line) {
    /* Replace the mark position with cont char if it's a continuation */
    printf("%s\n", line);
}

DESCR_t Gen(DESCR_t STRVAL_fn, DESCR_t outNm) {
    const char *s = VARVAL_fn(STRVAL_fn);
    if (!s) s = "";

    if (!g_buf) g_buf = strdup_gc("");
    if (!g_mark) g_mark = strdup_gc("");

    /* Append STRVAL_fn to buffer */
    /* If buffer is empty, prepend the mark+indent */
    const char *new_buf;
    if (!g_buf || !*g_buf) {
        /* Start a new line: mark = cont position, then indent, then STRVAL_fn */
        char indent[256] = {0};
        long long mark_len = g_mark ? (long long)strlen(g_mark) : 0;
        long long ind_len  = g_level > mark_len ? g_level - mark_len : 0;
        if (ind_len > 255) ind_len = 255;
        memset(indent, ' ', (size_t)ind_len);
        indent[ind_len] = '\0';
        new_buf = concat_gc(g_mark, concat_gc(indent, s));
    } else {
        new_buf = concat_gc(g_buf, s);
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
            g_mark = strdup_gc(g_cont);
        } else {
            g_mark = strdup_gc(" ");
        }
        p = nl + 1;
    }
    g_buf = strdup_gc(p);
    return NULVCL;
}

DESCR_t GenTab(DESCR_t pos) {
    long long target = (STYPE(pos) == DT_SNUL) ? g_level : to_int(pos);
    if (!g_buf) g_buf = strdup_gc("");

    long long cur = (long long)strlen(g_buf);
    if (cur < target) {
        long long spaces = target - cur;
        if (spaces > 1024) spaces = 1024;
        char *pad = (char*)GC_MALLOC((size_t)(spaces + 1));
        memset(pad, ' ', (size_t)spaces);
        pad[spaces] = '\0';
        g_buf = concat_gc(g_buf, pad);
    }
    return NULVCL;
}

DESCR_t GenSetCont(DESCR_t cont) {
    /* Flush any pending buffer, set continuation char, reset mark */
    if (g_buf && *g_buf) {
        /* Don't flush partial line — just reset */
    }
    g_buf  = strdup_gc("");
    const char *c = VARVAL_fn(cont);
    g_cont = strdup_gc((c && *c) ? c : "");
    g_mark = strdup_gc("");
    return NULVCL;
}

/* =========================================================================
 * Qize.inc: Qize(s) — wrap string in SNOBOL4 quote characters
 * Returns a SNOBOL4 string literal representation of s.
 * ===================================================================== */

DESCR_t Qize(DESCR_t s) {
    const char *src = VARVAL_fn(s);
    if (!src || !*src) return STRVAL("''");

    /* Simple version: if no single quotes, wrap in single quotes */
    if (!strchr(src, '\'')) {
        char *out = (char*)GC_MALLOC(strlen(src) + 3);
        sprintf(out, "'%s'", src);
        return STRVAL(out);
    }
    /* Has single quotes — use double quotes if no double quotes */
    if (!strchr(src, '"')) {
        char *out = (char*)GC_MALLOC(strlen(src) + 3);
        sprintf(out, "\"%s\"", src);
        return STRVAL(out);
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
    return STRVAL(out);
}

/* =========================================================================
 * ShiftReduce.inc: Shift(t, v), Reduce(t, n)
 * These use the Push/Pop stack from snobol4.c and the tree() DT_DATA type.
 * ===================================================================== */

/* Forward declarations from snobol4.c */
extern DESCR_t FIELD_GET_fn(DESCR_t obj, const char *field);

DESCR_t Shift(DESCR_t t_arg, DESCR_t v_arg) {
    /* Shift(t, v) — create tree node with type t and value v, push onto stack */
    const char *ts = IS_STR_fn(t_arg) ? t_arg.s : "?";
    const char *vs = IS_STR_fn(v_arg) ? v_arg.s : (IS_INT_fn(v_arg) ? "(int)" : "(null)");
    fprintf(stderr, "SEQ%04d SHIFT type=%s val='%s'\n", ++_nseq, ts, vs);
    DESCR_t s = MAKE_TREE_fn(t_arg, v_arg, INTVAL(0), NULVCL);
    push_val(s);
    return NULVCL;
}

DESCR_t Reduce(DESCR_t t_arg, DESCR_t n_arg) {
    /* Evaluate t if it's an unevaluated expression */
    DESCR_t t = t_arg;
    DESCR_t n = n_arg;
    long long count = to_int(n);
    const char *ts = IS_STR_fn(t_arg) ? t_arg.s : "?";
    fprintf(stderr, "SEQ%04d REDUCE type=%s n=%lld\n", ++_nseq, ts, count);

    /* Build array of n children from stack */
    if (count < 1) count = 0;
    DESCR_t children = array_create(STRVAL("1:256"));

    for (long long i = count; i >= 1; i--) {
        DESCR_t child = pop_val();
        subscript_set(children, INTVAL(i), child);
    }

    DESCR_t r = MAKE_TREE_fn(t, STRVAL(""), INTVAL(count), children);
    push_val(r);
    return NULVCL;
}

/* =========================================================================
 * TDump / XDump — debug dumps (no-op when doDebug == 0)
 * ===================================================================== */

DESCR_t TDump(DESCR_t x) { (void)x; return NULVCL; }
DESCR_t XDump(DESCR_t x) { (void)x; return NULVCL; }

/* =========================================================================
 * omega.inc / trace.inc: TV, TW, TX, TY, TZ, T8Trace, T8Pos
 * All are no-ops when doDebug == 0 (beauty.sno sets doDebug = 0).
 * ===================================================================== */

DESCR_t TV(DESCR_t lvl, DESCR_t pat, DESCR_t name) { return pat; }
DESCR_t TW(DESCR_t lvl, DESCR_t pat, DESCR_t name) { return pat; }
DESCR_t TX(DESCR_t lvl, DESCR_t pat, DESCR_t name) { return pat; }
DESCR_t TY(DESCR_t lvl, DESCR_t name, DESCR_t pat) { return pat; }
DESCR_t TZ(DESCR_t lvl, DESCR_t name, DESCR_t pat) { return pat; }
DESCR_t T8Trace(DESCR_t lvl, DESCR_t STRVAL_fn, DESCR_t ofs) { return NULVCL; }
DESCR_t T8Pos(DESCR_t ofs, DESCR_t map) { return STRVAL(""); }

/* =========================================================================
 * Lexicographic comparison functions
 * ===================================================================== */

DESCR_t LEQ(DESCR_t a, DESCR_t b) {
    return strcmp(VARVAL_fn(a), VARVAL_fn(b)) <= 0 ? a : NULVCL;
}
DESCR_t LGT(DESCR_t a, DESCR_t b) {
    return strcmp(VARVAL_fn(a), VARVAL_fn(b)) >  0 ? a : NULVCL;
}
DESCR_t LGE(DESCR_t a, DESCR_t b) {
    return strcmp(VARVAL_fn(a), VARVAL_fn(b)) >= 0 ? a : NULVCL;
}
DESCR_t LLT(DESCR_t a, DESCR_t b) {
    return strcmp(VARVAL_fn(a), VARVAL_fn(b)) <  0 ? a : NULVCL;
}
DESCR_t LLE(DESCR_t a, DESCR_t b) {
    return strcmp(VARVAL_fn(a), VARVAL_fn(b)) <= 0 ? a : NULVCL;
}
DESCR_t LNE(DESCR_t a, DESCR_t b) {
    return strcmp(VARVAL_fn(a), VARVAL_fn(b)) != 0 ? a : NULVCL;
}

/* =========================================================================
 * inc_init — register all inc functions in the global function table
 * and set up global variables.
 * ===================================================================== */

/* Forward: register a C function in the snobol4 function table */
extern void register_fn(const char *name, DESCR_t (*fn)(DESCR_t*, int), int min_args, int max_args);

/* Wrapper shims — adapt variadic calling convention */
static DESCR_t _w_lwr(DESCR_t *a, int n) {
    return lwr(n>0 ? a[0] : NULVCL);
}
static DESCR_t _w_upr(DESCR_t *a, int n) {
    return upr(n>0 ? a[0] : NULVCL);
}
static DESCR_t _w_assign(DESCR_t *a, int n) {
    return assign_fn(n>0?a[0]:NULVCL, n>1?a[1]:NULVCL);
}
static DESCR_t _w_match(DESCR_t *a, int n) {
    return match_fn(n>0?a[0]:NULVCL, n>1?a[1]:NULVCL);
}
static DESCR_t _w_notmatch(DESCR_t *a, int n) {
    return notmatch_fn(n>0?a[0]:NULVCL, n>1?a[1]:NULVCL);
}
static DESCR_t _w_Gen(DESCR_t *a, int n) {
    return Gen(n>0?a[0]:NULVCL, n>1?a[1]:NULVCL);
}
static DESCR_t _w_GenTab(DESCR_t *a, int n) {
    return GenTab(n>0?a[0]:NULVCL);
}
static DESCR_t _w_GenSetCont(DESCR_t *a, int n) {
    return GenSetCont(n>0?a[0]:NULVCL);
}
static DESCR_t _w_IncLevel(DESCR_t *a, int n) {
    return IncLevel(n>0?a[0]:NULVCL);
}
static DESCR_t _w_DecLevel(DESCR_t *a, int n) {
    return DecLevel(n>0?a[0]:NULVCL);
}
static DESCR_t _w_SetLevel(DESCR_t *a, int n) {
    return SetLevel(n>0?a[0]:NULVCL);
}
static DESCR_t _w_GetLevel(DESCR_t *a, int n) {
    (void)a; (void)n; return GetLevel();
}
static DESCR_t _w_Qize(DESCR_t *a, int n) {
    return Qize(n>0?a[0]:NULVCL);
}
static DESCR_t _w_Shift(DESCR_t *a, int n) {
    return Shift(n>0?a[0]:NULVCL, n>1?a[1]:NULVCL);
}
static DESCR_t _w_Reduce(DESCR_t *a, int n) {
    return Reduce(n>0?a[0]:NULVCL, n>1?a[1]:NULVCL);
}
static DESCR_t _w_TDump(DESCR_t *a, int n) {
    return TDump(n>0?a[0]:NULVCL);
}
static DESCR_t _w_XDump(DESCR_t *a, int n) {
    return XDump(n>0?a[0]:NULVCL);
}
static DESCR_t _w_TV(DESCR_t *a, int n) {
    return TV(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL);
}
static DESCR_t _w_TW(DESCR_t *a, int n) {
    return TW(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL);
}
static DESCR_t _w_TX(DESCR_t *a, int n) {
    return TX(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL);
}
static DESCR_t _w_TY(DESCR_t *a, int n) {
    return TY(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL);
}
static DESCR_t _w_TZ(DESCR_t *a, int n) {
    return TZ(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL);
}
static DESCR_t _w_T8Trace(DESCR_t *a, int n) {
    return T8Trace(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL);
}
static DESCR_t _w_T8Pos(DESCR_t *a, int n) {
    return T8Pos(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}
static DESCR_t _w_LEQ(DESCR_t *a, int n) {
    return LEQ(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}
static DESCR_t _w_LGT(DESCR_t *a, int n) {
    return LGT(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}
static DESCR_t _w_LGE(DESCR_t *a, int n) {
    return LGE(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}
static DESCR_t _w_LLT(DESCR_t *a, int n) {
    return LLT(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}
static DESCR_t _w_LLE(DESCR_t *a, int n) {
    return LLE(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}
static DESCR_t _w_LNE(DESCR_t *a, int n) {
    return LNE(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL);
}

void inc_init(void) {
    /* Set up global character constants */
    NV_SET_fn("digits",   STRVAL(digits_str));
    NV_SET_fn("tab",      STRVAL(tab_str));
    NV_SET_fn("nl",       STRVAL(nl_str));
    NV_SET_fn("cr",       STRVAL(cr_str));
    NV_SET_fn("ff",       STRVAL(ff_str));
    NV_SET_fn("bs",       STRVAL(bs_str));
    NV_SET_fn("bSlash",   STRVAL(bSlash_str));
    NV_SET_fn("lf",       STRVAL(nl_str));
    NV_SET_fn("whitespace", STRVAL(" \t\n\r"));
    NV_SET_fn("doDebug",  INTVAL(g_doDebug));
    NV_SET_fn("xTrace",   INTVAL(g_xTrace));
    NV_SET_fn("doParseTree", INTVAL(g_doParseTree));
    NV_SET_fn("level",    STRVAL(""));

    /* Gen.inc globals */
    g_buf  = (char*)GC_MALLOC(1); g_buf[0]  = '\0';
    g_cont = (char*)GC_MALLOC(1); g_cont[0] = '\0';
    g_mark = (char*)GC_MALLOC(1); g_mark[0] = '\0';
    g_level = 0;

    /* Register all functions */
    register_fn("lwr",        _w_lwr,       1, 1);
    register_fn("upr",        _w_upr,       1, 1);
    register_fn("assign",     _w_assign,    2, 2);
    register_fn("MATCH_fn",      _w_match,     2, 2);
    register_fn("notmatch",   _w_notmatch,  2, 2);
    register_fn("Gen",        _w_Gen,       1, 2);
    register_fn("GenTab",     _w_GenTab,    0, 1);
    register_fn("GenSetCont", _w_GenSetCont,0, 1);
    register_fn("IncLevel",   _w_IncLevel,  0, 1);
    register_fn("DecLevel",   _w_DecLevel,  0, 1);
    register_fn("SetLevel",   _w_SetLevel,  1, 1);
    register_fn("GetLevel",   _w_GetLevel,  0, 0);
    register_fn("Qize",       _w_Qize,      1, 1);
    register_fn("Shift",      _w_Shift,     1, 2);
    register_fn("Reduce",     _w_Reduce,    2, 2);
    register_fn("TDump",      _w_TDump,     1, 1);
    register_fn("XDump",      _w_XDump,     1, 1);
    register_fn("TV",         _w_TV,        3, 3);
    register_fn("TW",         _w_TW,        3, 3);
    register_fn("TX",         _w_TX,        3, 3);
    register_fn("TY",         _w_TY,        3, 3);
    register_fn("TZ",         _w_TZ,        3, 3);
    register_fn("T8Trace",    _w_T8Trace,   3, 3);
    register_fn("T8Pos",      _w_T8Pos,     2, 2);
    /* LGT/LLT/LGE/LLE/LEQ/LNE registered by SNO_INIT_fn (snobol4.c) with
     * correct FAILDESCR semantics. Do NOT re-register here — it overwrites
     * the correct versions with _w_* wrappers that return NULVCL on failure. */
    void inc_init_extra(void);  /* forward declaration */
    inc_init_extra();
}

/* =========================================================================
 * Additional missing registrations (identified by compiland reachability)
 * ===================================================================== */

/* icase(STRVAL_fn) — case.inc: build case-insensitive pattern from string
 * Returns a pattern that matches STRVAL_fn case-insensitively.
 * Each alpha char → (upper | lower) alternation; non-alpha → literal. */
static DESCR_t _w_icase(DESCR_t *a, int n) {
    const char *s = VARVAL_fn(n > 0 ? a[0] : NULVCL);
    if (!s || !*s) return pat_epsilon();
    /* Build cat of per-char patterns */
    DESCR_t pat = pat_epsilon();
    int len = (int)strlen(s);
    for (int i = len - 1; i >= 0; i--) {
        char c = s[i];
        DESCR_t cp;
        if (isalpha((unsigned char)c)) {
            char lo[2] = { (char)tolower((unsigned char)c), 0 };
            char hi[2] = { (char)toupper((unsigned char)c), 0 };
            cp = pat_alt(pat_lit(GC_strdup(lo)), pat_lit(GC_strdup(hi)));
        } else {
            char buf[2] = { c, 0 };
            cp = pat_lit(GC_strdup(buf));
        }
        pat = (i == len - 1) ? cp : pat_cat(cp, pat);
    }
    return pat;
}

/* IsSnobol4() — is.inc: we ARE snobol4x, so always RETURN (succeed) */
static DESCR_t _w_IsSnobol4(DESCR_t *a, int n) {
    (void)a; (void)n;
    return STRVAL("");   /* non-null = success */
}

/* Push(x) — stack.inc: PUSH_fn x onto value stack */
static DESCR_t _w_Push(DESCR_t *a, int n) {
    DESCR_t x = n > 0 ? a[0] : NULVCL;
    PUSH_fn(x);
    /* Push returns .dummy (NRETURN) — return null marker */
    return NULVCL;
}

/* Pop() / Pop(var) — stack.inc: POP_fn from value stack */
static DESCR_t _w_Pop(DESCR_t *a, int n) {
    if (STACK_DEPTH_fn() == 0) return NULVCL;
    DESCR_t v = POP_fn();
    if (n > 0 && a[0].v == DT_S) {
        /* Pop(var) — store into named variable */
        NV_SET_fn(a[0].s, v);
        return NULVCL;
    }
    return v;
}

/* TopCounter() — counter.inc: return current counter value */
static DESCR_t _w_TopCounter(DESCR_t *a, int n) {
    (void)a; (void)n;
    int64_t v = ntop();
    if (v < 0) return NULVCL;   /* FRETURN if stack empty */
    return INTVAL(v);
}

/* nPush() / nInc() / nPop() / nTop() — semantic.inc wrappers.
 * These are SNOBOL4-defined NRETURN functions that return epsilon patterns
 * with counter side-effects.  When called via the pattern engine's T_FUNC
 * path (deferred_call_fn → APPLY_fn), APPLY_fn would find fn==NULL and
 * silently return NULVCL, dropping the side-effect.  Register C wrappers
 * so APPLY_fn dispatches to the actual C-level counter operations.
 * The T_FUNC engine node treats any non-(-1) return as zero-width succeed,
 * which matches the NRETURN epsilon behaviour. */
static DESCR_t _w_nPush(DESCR_t *a, int n) {
    (void)a; (void)n;
    NPUSH_fn();
    return NULVCL;   /* epsilon / NRETURN */
}
static DESCR_t _w_nInc(DESCR_t *a, int n) {
    (void)a; (void)n;
    NINC_fn();
    return NULVCL;   /* epsilon / NRETURN */
}
static DESCR_t _w_nPop(DESCR_t *a, int n) {
    (void)a; (void)n;
    NPOP_fn();
    return NULVCL;   /* epsilon / NRETURN */
}
static DESCR_t _w_nTop(DESCR_t *a, int n) {
    (void)a; (void)n;
    int64_t v = ntop();
    return INTVAL(v);   /* returns counter value (integer) */
}

/* SqlSQize(STRVAL_fn) — Qize.inc: SQL single-quote escape ('' for each ') */
static DESCR_t _w_SqlSQize(DESCR_t *a, int n) {
    const char *s = VARVAL_fn(n > 0 ? a[0] : NULVCL);
    if (!s || !*s) return STRVAL("");
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
    return STRVAL(out);
}

/* TLump(x, len) — TDump.inc: tree-to-string up to len chars (stub) */
static DESCR_t _w_TLump(DESCR_t *a, int n) {
    /* Stub: return tree tag as string */
    if (n < 1 || a[0].v == DT_SNUL) return STRVAL("()");
    if (a[0].v == DT_DATA) {
        DESCR_t t = FIELD_GET_fn(a[0], "t");
        DESCR_t v = FIELD_GET_fn(a[0], "v");
        const char *ts = VARVAL_fn(t);
        const char *vs = VARVAL_fn(v);
        size_t tl = strlen(ts), vl = strlen(vs);
        char *out = (char *)GC_MALLOC(tl + vl + 4);
        if (vl > 0) sprintf(out, "(%s %s)", ts, vs);
        else        sprintf(out, "(%s)", ts);
        return STRVAL(out);
    }
    return STRVAL(VARVAL_fn(a[0]));
}

/* TValue(x) — TDump.inc: extract printable value from tree node */
static DESCR_t _w_TValue(DESCR_t *a, int n) {
    if (n < 1) return STRVAL(".");
    DESCR_t x = a[0];
    if (x.v == DT_SNUL) return STRVAL(".");
    if (x.v == DT_DATA) {
        DESCR_t v = FIELD_GET_fn(x, "v");
        if (IS_NULL_fn(v)) return STRVAL(".");
        return STRVAL(VARVAL_fn(v));
    }
    return STRVAL(VARVAL_fn(x));
}

/* Visit(x, fnc) — tree.inc: pre-order traversal, APPLY_fn fnc at each node */
static DESCR_t _w_Visit(DESCR_t *a, int n) {
    if (n < 2) return NULVCL;
    DESCR_t x   = a[0];
    DESCR_t fnc = a[1];
    const char *fname = VARVAL_fn(fnc);
    /* Apply fnc to x */
    APPLY_fn(fname, &x, 1);
    /* Recurse into children */
    if (x.v == DT_DATA) {
        DESCR_t nc  = FIELD_GET_fn(x, "n");
        DESCR_t ca  = FIELD_GET_fn(x, "c");
        int    cnt = (int)to_int(nc);
        for (int i = 1; i <= cnt; i++) {
            DESCR_t child = subscript_get(ca, INTVAL(i));
            DESCR_t visit_args[2] = { child, fnc };
            _w_Visit(visit_args, 2);
        }
    }
    return NULVCL;
}

/* bVisit — same as Visit for beautiful.sno purposes */
static DESCR_t _w_bVisit(DESCR_t *a, int n) {
    return _w_Visit(a, n);
}

/* Equal(x, y) — tree.inc: structural equality */
static DESCR_t _w_Equal(DESCR_t *a, int n) {
    if (n < 2) return NULVCL;
    DESCR_t x = a[0], y = a[1];
    /* Both null → equal */
    if (x.v == DT_SNUL && y.v == DT_SNUL) return STRVAL("");
    if (x.v == DT_SNUL || y.v == DT_SNUL) return NULVCL;
    if (x.v != DT_DATA || y.v != DT_DATA) {
        return strcmp(VARVAL_fn(x), VARVAL_fn(y)) == 0 ? STRVAL("") : NULVCL;
    }
    /* Compare t, v, n */
    if (!ident(FIELD_GET_fn(x,"t"), FIELD_GET_fn(y,"t"))) return NULVCL;
    if (!ident(FIELD_GET_fn(x,"v"), FIELD_GET_fn(y,"v"))) return NULVCL;
    DESCR_t nx = FIELD_GET_fn(x,"n"), ny = FIELD_GET_fn(y,"n");
    if (!ident(nx, ny)) return NULVCL;
    int cnt = (int)to_int(nx);
    DESCR_t cx = FIELD_GET_fn(x,"c"), cy = FIELD_GET_fn(y,"c");
    for (int i = 1; i <= cnt; i++) {
        DESCR_t ci_x = subscript_get(cx, INTVAL(i));
        DESCR_t ci_y = subscript_get(cy, INTVAL(i));
        DESCR_t eq_args[2] = { ci_x, ci_y };
        if (IS_NULL_fn(_w_Equal(eq_args, 2))) return NULVCL;
    }
    return STRVAL("");
}

/* Equiv(x, y) — tree.inc: structural equivalence (like Equal but looser) */
static DESCR_t _w_Equiv(DESCR_t *a, int n) {
    return _w_Equal(a, n);   /* same semantics for our purposes */
}

/* Find(xn, y, f) — tree.inc: search tree *xn for node equiv to y, APPLY_fn f */
static DESCR_t _w_Find(DESCR_t *a, int n) {
    if (n < 3) return NULVCL;
    /* xn is a variable name (indirect ref), y is search target, f is function */
    DESCR_t xn  = a[0];
    DESCR_t y   = a[1];
    DESCR_t f   = a[2];
    const char *xname  = VARVAL_fn(xn);
    const char *fname  = VARVAL_fn(f);
    DESCR_t root = NV_GET_fn(xname);
    if (IS_NULL_fn(root)) return NULVCL;
    /* Check if root equiv to y */
    DESCR_t eq_args[2] = { root, y };
    if (!IS_NULL_fn(_w_Equiv(eq_args, 2))) {
        APPLY_fn(fname, &xn, 1);
        return STRVAL("");
    }
    /* Recurse into children */
    if (root.v == DT_DATA) {
        DESCR_t nc = FIELD_GET_fn(root, "n");
        DESCR_t ca = FIELD_GET_fn(root, "c");
        int cnt = (int)to_int(nc);
        for (int i = 1; i <= cnt; i++) {
            DESCR_t child = subscript_get(ca, INTVAL(i));
            /* For child, we'd need a temp var — skip deep recursion for now */
            (void)child;
        }
    }
    return NULVCL;
}

/* Insert(x, y, place) — tree.inc: insert y into tree x at position place */
static DESCR_t _w_Insert(DESCR_t *a, int n) {
    if (n < 3) return n > 0 ? a[0] : NULVCL;
    DESCR_t x     = a[0];
    DESCR_t y     = a[1];
    int    place = (int)to_int(a[2]);
    if (x.v != DT_DATA) return x;

    DESCR_t nc  = FIELD_GET_fn(x, "n");
    DESCR_t ca  = FIELD_GET_fn(x, "c");
    int    cnt = (int)to_int(nc);

    /* Build new children array with y inserted at place */
    int new_cnt = cnt + 1;
    DESCR_t new_c = array_create(STRVAL("1:256"));
    for (int i = 1; i < place && i <= cnt; i++)
        subscript_set(new_c, INTVAL(i), subscript_get(ca, INTVAL(i)));
    subscript_set(new_c, INTVAL(place), y);
    for (int i = place; i <= cnt; i++)
        subscript_set(new_c, INTVAL(i+1), subscript_get(ca, INTVAL(i)));

    FIELD_SET_fn(x, "n", INTVAL(new_cnt));
    FIELD_SET_fn(x, "c", new_c);
    return x;
}

/* Register all the missing functions */
void inc_init_extra(void) {
    register_fn("icase",      _w_icase,      1, 1);
    register_fn("IsSnobol4",  _w_IsSnobol4,  0, 0);
    register_fn("Push",       _w_Push,        1, 1);
    register_fn("Pop",        _w_Pop,         0, 1);
    register_fn("TopCounter", _w_TopCounter,  0, 0);
    register_fn("nPush",      _w_nPush,       0, 0);
    register_fn("nInc",       _w_nInc,        0, 0);
    register_fn("nPop",       _w_nPop,        0, 0);
    register_fn("nTop",       _w_nTop,        0, 0);
    register_fn("SqlSQize",   _w_SqlSQize,    1, 1);
    register_fn("TLump",      _w_TLump,       1, 2);
    register_fn("TValue",     _w_TValue,      1, 1);
    register_fn("Visit",      _w_Visit,       2, 2);
    register_fn("bVisit",     _w_bVisit,      2, 2);
    register_fn("Equal",      _w_Equal,       2, 2);
    register_fn("Equiv",      _w_Equiv,       2, 2);
    register_fn("Find",       _w_Find,        3, 3);
    register_fn("Insert",     _w_Insert,      3, 3);
}
