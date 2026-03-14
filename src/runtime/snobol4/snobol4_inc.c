/*
 * snobol4_inc.c — C implementations of SNOBOL4 .inc library functions
 *
 * Implements the library used by beauty.sno:
 *   global, is, io, case, assign, mtch, Gen, Qize, ShiftReduce, Tree, Stack
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
/* (already managed through var_get/set with key "@S") */

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

SnoVal lwr(SnoVal s) {
    const char *src = to_str(s);
    if (!src || !*src) return STR_VAL("");
    size_t n = strlen(src);
    char *out = (char*)GC_MALLOC(n+1);
    for (size_t i = 0; i < n; i++)
        out[i] = (char)tolower((unsigned char)src[i]);
    out[n] = '\0';
    return STR_VAL(out);
}

SnoVal upr(SnoVal s) {
    const char *src = to_str(s);
    if (!src || !*src) return STR_VAL("");
    size_t n = strlen(src);
    char *out = (char*)GC_MALLOC(n+1);
    for (size_t i = 0; i < n; i++)
        out[i] = (char)toupper((unsigned char)src[i]);
    out[n] = '\0';
    return STR_VAL(out);
}

/* =========================================================================
 * assign.inc: assign(name, expression)
 * Sets $name = EVAL(expression) if expression is EXPRESSION datatype,
 * else $name = expression. Always succeeds (returns .dummy).
 * ===================================================================== */

SnoVal assign_fn(SnoVal name, SnoVal expression) {
    const char *nm = to_str(name);
    /* If expression is an unevaluated expression, evl it */
    SnoVal val = expression;
    if (STYPE(expression) == SSTR) {
        /* Try to evaluate as SNOBOL4 — for now just use it as-is */
        val = expression;
    }
    var_set(nm, val);
    return NULL_VAL;
}

/* =========================================================================
 * mtch.inc: mtch(subject, pattern) → NRETURN on mtch, FRETURN on fail
 * In C: return non-null on mtch, FAIL_VAL on fail
 * ===================================================================== */

/* Forward declaration — pattern matching from snobol4.c */
extern int match_pattern(SnoVal pat, const char *subject);

SnoVal match_fn(SnoVal subject, SnoVal pattern) {
    const char *subj = to_str(subject);
    int ok = match_pattern(pattern, subj);
    return ok ? STR_VAL("") : NULL_VAL;
}

SnoVal notmatch_fn(SnoVal subject, SnoVal pattern) {
    const char *subj = to_str(subject);
    int ok = match_pattern(pattern, subj);
    return ok ? NULL_VAL : STR_VAL("");
}

/* =========================================================================
 * io.inc: basic I/O — in beautiful.sno, io() opens named channels.
 * We use stdin/stdout as channels 5/6 per SNOBOL4 convention.
 * ===================================================================== */

SnoVal io_fn(SnoVal name, SnoVal mode) {
    /* No-op for our purposes — we use stdin/stdout */
    return NULL_VAL;
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

SnoVal IncLevel(SnoVal delta) {
    long long d = (STYPE(delta) == SNULL) ? 2 : to_int(delta);
    g_level += d;
    return NULL_VAL;
}

SnoVal DecLevel(SnoVal delta) {
    long long d = (STYPE(delta) == SNULL) ? 2 : to_int(delta);
    g_level -= d;
    if (g_level < 0) g_level = 0;
    return NULL_VAL;
}

SnoVal SetLevel(SnoVal level) {
    g_level = to_int(level);
    return NULL_VAL;
}

SnoVal GetLevel(void) {
    return INT_VAL(g_level);
}

/* Flush one line from g_buf to output, replacing $'$X' with g_cont */
static void gen_flush_line(const char *line) {
    /* Replace the mark position with cont char if it's a continuation */
    printf("%s\n", line);
}

SnoVal Gen(SnoVal strv, SnoVal outNm) {
    const char *s = to_str(strv);
    if (!s) s = "";

    if (!g_buf) g_buf = strdup_gc("");
    if (!g_mark) g_mark = strdup_gc("");

    /* Append strv to buffer */
    /* If buffer is empty, prepend the mark+indent */
    const char *new_buf;
    if (!g_buf || !*g_buf) {
        /* Start a new line: mark = cont position, then indent, then strv */
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
    return NULL_VAL;
}

SnoVal GenTab(SnoVal pos) {
    long long target = (STYPE(pos) == SNULL) ? g_level : to_int(pos);
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
    return NULL_VAL;
}

SnoVal GenSetCont(SnoVal cont) {
    /* Flush any pending buffer, set continuation char, reset mark */
    if (g_buf && *g_buf) {
        /* Don't flush partial line — just reset */
    }
    g_buf  = strdup_gc("");
    const char *c = to_str(cont);
    g_cont = strdup_gc((c && *c) ? c : "");
    g_mark = strdup_gc("");
    return NULL_VAL;
}

/* =========================================================================
 * Qize.inc: Qize(s) — wrap string in SNOBOL4 quote characters
 * Returns a SNOBOL4 string literal representation of s.
 * ===================================================================== */

SnoVal Qize(SnoVal s) {
    const char *src = to_str(s);
    if (!src || !*src) return STR_VAL("''");

    /* Simple version: if no single quotes, wrap in single quotes */
    if (!strchr(src, '\'')) {
        char *out = (char*)GC_MALLOC(strlen(src) + 3);
        sprintf(out, "'%s'", src);
        return STR_VAL(out);
    }
    /* Has single quotes — use double quotes if no double quotes */
    if (!strchr(src, '"')) {
        char *out = (char*)GC_MALLOC(strlen(src) + 3);
        sprintf(out, "\"%s\"", src);
        return STR_VAL(out);
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
    return STR_VAL(out);
}

/* =========================================================================
 * ShiftReduce.inc: Shift(t, v), Reduce(t, n)
 * These use the Push/Pop stack from snobol4.c and the tree() DATA type.
 * ===================================================================== */

/* Forward declarations from snobol4.c */
extern SnoVal field_get(SnoVal obj, const char *field);

SnoVal Shift(SnoVal t_arg) {
    /* Shift(t, v) — but in beauty.sno it's called as Shift('tag', value) */
    /* For now t_arg is the tree type tag, value is empty */
    SnoVal s = make_tree(t_arg, STR_VAL(""), INT_VAL(0), NULL_VAL);
    push_val(s);
    return NULL_VAL;
}

SnoVal Reduce(SnoVal t_arg, SnoVal n_arg) {
    /* Evaluate t if it's an unevaluated expression */
    SnoVal t = t_arg;
    SnoVal n = n_arg;
    long long count = to_int(n);

    /* Build array of n children from stack */
    if (count < 1) count = 0;
    SnoVal children = array_create(STR_VAL("1:256"));

    for (long long i = count; i >= 1; i--) {
        SnoVal child = pop_val();
        subscript_set(children, INT_VAL(i), child);
    }

    SnoVal r = make_tree(t, STR_VAL(""), INT_VAL(count), children);
    push_val(r);
    return NULL_VAL;
}

/* =========================================================================
 * TDump / XDump — debug dumps (no-op when doDebug == 0)
 * ===================================================================== */

SnoVal TDump(SnoVal x) { (void)x; return NULL_VAL; }
SnoVal XDump(SnoVal x) { (void)x; return NULL_VAL; }

/* =========================================================================
 * omega.inc / trace.inc: TV, TW, TX, TY, TZ, T8Trace, T8Pos
 * All are no-ops when doDebug == 0 (beauty.sno sets doDebug = 0).
 * ===================================================================== */

SnoVal TV(SnoVal lvl, SnoVal pat, SnoVal name) { return pat; }
SnoVal TW(SnoVal lvl, SnoVal pat, SnoVal name) { return pat; }
SnoVal TX(SnoVal lvl, SnoVal pat, SnoVal name) { return pat; }
SnoVal TY(SnoVal lvl, SnoVal name, SnoVal pat) { return pat; }
SnoVal TZ(SnoVal lvl, SnoVal name, SnoVal pat) { return pat; }
SnoVal T8Trace(SnoVal lvl, SnoVal strv, SnoVal ofs) { return NULL_VAL; }
SnoVal T8Pos(SnoVal ofs, SnoVal map) { return STR_VAL(""); }

/* =========================================================================
 * Lexicographic comparison functions
 * ===================================================================== */

SnoVal LEQ(SnoVal a, SnoVal b) {
    return strcmp(to_str(a), to_str(b)) <= 0 ? a : NULL_VAL;
}
SnoVal LGT(SnoVal a, SnoVal b) {
    return strcmp(to_str(a), to_str(b)) >  0 ? a : NULL_VAL;
}
SnoVal LGE(SnoVal a, SnoVal b) {
    return strcmp(to_str(a), to_str(b)) >= 0 ? a : NULL_VAL;
}
SnoVal LLT(SnoVal a, SnoVal b) {
    return strcmp(to_str(a), to_str(b)) <  0 ? a : NULL_VAL;
}
SnoVal LLE(SnoVal a, SnoVal b) {
    return strcmp(to_str(a), to_str(b)) <= 0 ? a : NULL_VAL;
}
SnoVal LNE(SnoVal a, SnoVal b) {
    return strcmp(to_str(a), to_str(b)) != 0 ? a : NULL_VAL;
}

/* =========================================================================
 * inc_init — register all inc functions in the global function table
 * and set up global variables.
 * ===================================================================== */

/* Forward: register a C function in the snobol4 function table */
extern void register_fn(const char *name, SnoVal (*fn)(SnoVal*, int), int min_args, int max_args);

/* Wrapper shims — adapt variadic calling convention */
static SnoVal _w_lwr(SnoVal *a, int n) {
    return lwr(n>0 ? a[0] : NULL_VAL);
}
static SnoVal _w_upr(SnoVal *a, int n) {
    return upr(n>0 ? a[0] : NULL_VAL);
}
static SnoVal _w_assign(SnoVal *a, int n) {
    return assign_fn(n>0?a[0]:NULL_VAL, n>1?a[1]:NULL_VAL);
}
static SnoVal _w_match(SnoVal *a, int n) {
    return match_fn(n>0?a[0]:NULL_VAL, n>1?a[1]:NULL_VAL);
}
static SnoVal _w_notmatch(SnoVal *a, int n) {
    return notmatch_fn(n>0?a[0]:NULL_VAL, n>1?a[1]:NULL_VAL);
}
static SnoVal _w_Gen(SnoVal *a, int n) {
    return Gen(n>0?a[0]:NULL_VAL, n>1?a[1]:NULL_VAL);
}
static SnoVal _w_GenTab(SnoVal *a, int n) {
    return GenTab(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_GenSetCont(SnoVal *a, int n) {
    return GenSetCont(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_IncLevel(SnoVal *a, int n) {
    return IncLevel(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_DecLevel(SnoVal *a, int n) {
    return DecLevel(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_SetLevel(SnoVal *a, int n) {
    return SetLevel(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_GetLevel(SnoVal *a, int n) {
    (void)a; (void)n; return GetLevel();
}
static SnoVal _w_Qize(SnoVal *a, int n) {
    return Qize(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_Shift(SnoVal *a, int n) {
    return Shift(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_Reduce(SnoVal *a, int n) {
    return Reduce(n>0?a[0]:NULL_VAL, n>1?a[1]:NULL_VAL);
}
static SnoVal _w_TDump(SnoVal *a, int n) {
    return TDump(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_XDump(SnoVal *a, int n) {
    return XDump(n>0?a[0]:NULL_VAL);
}
static SnoVal _w_TV(SnoVal *a, int n) {
    return TV(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL);
}
static SnoVal _w_TW(SnoVal *a, int n) {
    return TW(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL);
}
static SnoVal _w_TX(SnoVal *a, int n) {
    return TX(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL);
}
static SnoVal _w_TY(SnoVal *a, int n) {
    return TY(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL);
}
static SnoVal _w_TZ(SnoVal *a, int n) {
    return TZ(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL);
}
static SnoVal _w_T8Trace(SnoVal *a, int n) {
    return T8Trace(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL);
}
static SnoVal _w_T8Pos(SnoVal *a, int n) {
    return T8Pos(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}
static SnoVal _w_LEQ(SnoVal *a, int n) {
    return LEQ(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}
static SnoVal _w_LGT(SnoVal *a, int n) {
    return LGT(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}
static SnoVal _w_LGE(SnoVal *a, int n) {
    return LGE(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}
static SnoVal _w_LLT(SnoVal *a, int n) {
    return LLT(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}
static SnoVal _w_LLE(SnoVal *a, int n) {
    return LLE(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}
static SnoVal _w_LNE(SnoVal *a, int n) {
    return LNE(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL);
}

void inc_init(void) {
    /* Set up global character constants */
    var_set("digits",   STR_VAL(digits_str));
    var_set("tab",      STR_VAL(tab_str));
    var_set("nl",       STR_VAL(nl_str));
    var_set("cr",       STR_VAL(cr_str));
    var_set("ff",       STR_VAL(ff_str));
    var_set("bs",       STR_VAL(bs_str));
    var_set("bSlash",   STR_VAL(bSlash_str));
    var_set("lf",       STR_VAL(nl_str));
    var_set("whitespace", STR_VAL(" \t\n\r"));
    var_set("doDebug",  INT_VAL(g_doDebug));
    var_set("xTrace",   INT_VAL(g_xTrace));
    var_set("doParseTree", INT_VAL(g_doParseTree));
    var_set("level",    STR_VAL(""));

    /* Gen.inc globals */
    g_buf  = (char*)GC_MALLOC(1); g_buf[0]  = '\0';
    g_cont = (char*)GC_MALLOC(1); g_cont[0] = '\0';
    g_mark = (char*)GC_MALLOC(1); g_mark[0] = '\0';
    g_level = 0;

    /* Register all functions */
    register_fn("lwr",        _w_lwr,       1, 1);
    register_fn("upr",        _w_upr,       1, 1);
    register_fn("assign",     _w_assign,    2, 2);
    register_fn("mtch",      _w_match,     2, 2);
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
    register_fn("LEQ",        _w_LEQ,       2, 2);
    register_fn("LGT",        _w_LGT,       2, 2);
    register_fn("LGE",        _w_LGE,       2, 2);
    register_fn("LLT",        _w_LLT,       2, 2);
    register_fn("LLE",        _w_LLE,       2, 2);
    register_fn("LNE",        _w_LNE,       2, 2);
    void inc_init_extra(void);  /* forward declaration */
    inc_init_extra();
}

/* =========================================================================
 * Additional missing registrations (identified by compiland reachability)
 * ===================================================================== */

/* icase(strv) — case.inc: build case-insensitive pattern from string
 * Returns a pattern that matches strv case-insensitively.
 * Each alpha char → (upper | lower) alternation; non-alpha → literal. */
static SnoVal _w_icase(SnoVal *a, int n) {
    const char *s = to_str(n > 0 ? a[0] : NULL_VAL);
    if (!s || !*s) return pat_epsilon();
    /* Build cat of per-char patterns */
    SnoVal pat = pat_epsilon();
    int len = (int)strlen(s);
    for (int i = len - 1; i >= 0; i--) {
        char c = s[i];
        SnoVal cp;
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

/* IsSnobol4() — is.inc: we ARE SNOBOL4-tiny, so always RETURN (succeed) */
static SnoVal _w_IsSnobol4(SnoVal *a, int n) {
    (void)a; (void)n;
    return STR_VAL("");   /* non-null = success */
}

/* Push(x) — stack.inc: push x onto value stack */
static SnoVal _w_Push(SnoVal *a, int n) {
    SnoVal x = n > 0 ? a[0] : NULL_VAL;
    push(x);
    /* Push returns .dummy (NRETURN) — return null marker */
    return NULL_VAL;
}

/* Pop() / Pop(var) — stack.inc: pop from value stack */
static SnoVal _w_Pop(SnoVal *a, int n) {
    if (stack_depth() == 0) return NULL_VAL;
    SnoVal v = pop();
    if (n > 0 && a[0].type == SSTR) {
        /* Pop(var) — store into named variable */
        var_set(a[0].s, v);
        return NULL_VAL;
    }
    return v;
}

/* TopCounter() — counter.inc: return current counter value */
static SnoVal _w_TopCounter(SnoVal *a, int n) {
    (void)a; (void)n;
    int64_t v = ntop();
    if (v < 0) return NULL_VAL;   /* FRETURN if stack empty */
    return INT_VAL(v);
}

/* SqlSQize(strv) — Qize.inc: SQL single-quote escape ('' for each ') */
static SnoVal _w_SqlSQize(SnoVal *a, int n) {
    const char *s = to_str(n > 0 ? a[0] : NULL_VAL);
    if (!s || !*s) return STR_VAL("");
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
    return STR_VAL(out);
}

/* TLump(x, len) — TDump.inc: tree-to-string up to len chars (stub) */
static SnoVal _w_TLump(SnoVal *a, int n) {
    /* Stub: return tree tag as string */
    if (n < 1 || a[0].type == SNULL) return STR_VAL("()");
    if (a[0].type == UDEF) {
        SnoVal t = field_get(a[0], "t");
        SnoVal v = field_get(a[0], "v");
        const char *ts = to_str(t);
        const char *vs = to_str(v);
        size_t tl = strlen(ts), vl = strlen(vs);
        char *out = (char *)GC_MALLOC(tl + vl + 4);
        if (vl > 0) sprintf(out, "(%s %s)", ts, vs);
        else        sprintf(out, "(%s)", ts);
        return STR_VAL(out);
    }
    return STR_VAL(to_str(a[0]));
}

/* TValue(x) — TDump.inc: extract printable value from tree node */
static SnoVal _w_TValue(SnoVal *a, int n) {
    if (n < 1) return STR_VAL(".");
    SnoVal x = a[0];
    if (x.type == SNULL) return STR_VAL(".");
    if (x.type == UDEF) {
        SnoVal v = field_get(x, "v");
        if (is_null(v)) return STR_VAL(".");
        return STR_VAL(to_str(v));
    }
    return STR_VAL(to_str(x));
}

/* Visit(x, fnc) — tree.inc: pre-order traversal, aply fnc at each node */
static SnoVal _w_Visit(SnoVal *a, int n) {
    if (n < 2) return NULL_VAL;
    SnoVal x   = a[0];
    SnoVal fnc = a[1];
    const char *fname = to_str(fnc);
    /* Apply fnc to x */
    aply(fname, &x, 1);
    /* Recurse into children */
    if (x.type == UDEF) {
        SnoVal nc  = field_get(x, "n");
        SnoVal ca  = field_get(x, "c");
        int    cnt = (int)to_int(nc);
        for (int i = 1; i <= cnt; i++) {
            SnoVal child = subscript_get(ca, INT_VAL(i));
            SnoVal visit_args[2] = { child, fnc };
            _w_Visit(visit_args, 2);
        }
    }
    return NULL_VAL;
}

/* bVisit — same as Visit for beautiful.sno purposes */
static SnoVal _w_bVisit(SnoVal *a, int n) {
    return _w_Visit(a, n);
}

/* Equal(x, y) — tree.inc: structural equality */
static SnoVal _w_Equal(SnoVal *a, int n) {
    if (n < 2) return NULL_VAL;
    SnoVal x = a[0], y = a[1];
    /* Both null → equal */
    if (x.type == SNULL && y.type == SNULL) return STR_VAL("");
    if (x.type == SNULL || y.type == SNULL) return NULL_VAL;
    if (x.type != UDEF || y.type != UDEF) {
        return strcmp(to_str(x), to_str(y)) == 0 ? STR_VAL("") : NULL_VAL;
    }
    /* Compare t, v, n */
    if (!ident(field_get(x,"t"), field_get(y,"t"))) return NULL_VAL;
    if (!ident(field_get(x,"v"), field_get(y,"v"))) return NULL_VAL;
    SnoVal nx = field_get(x,"n"), ny = field_get(y,"n");
    if (!ident(nx, ny)) return NULL_VAL;
    int cnt = (int)to_int(nx);
    SnoVal cx = field_get(x,"c"), cy = field_get(y,"c");
    for (int i = 1; i <= cnt; i++) {
        SnoVal ci_x = subscript_get(cx, INT_VAL(i));
        SnoVal ci_y = subscript_get(cy, INT_VAL(i));
        SnoVal eq_args[2] = { ci_x, ci_y };
        if (is_null(_w_Equal(eq_args, 2))) return NULL_VAL;
    }
    return STR_VAL("");
}

/* Equiv(x, y) — tree.inc: structural equivalence (like Equal but looser) */
static SnoVal _w_Equiv(SnoVal *a, int n) {
    return _w_Equal(a, n);   /* same semantics for our purposes */
}

/* Find(xn, y, f) — tree.inc: search tree *xn for node equiv to y, aply f */
static SnoVal _w_Find(SnoVal *a, int n) {
    if (n < 3) return NULL_VAL;
    /* xn is a variable name (indirect ref), y is search target, f is function */
    SnoVal xn  = a[0];
    SnoVal y   = a[1];
    SnoVal f   = a[2];
    const char *xname  = to_str(xn);
    const char *fname  = to_str(f);
    SnoVal root = var_get(xname);
    if (is_null(root)) return NULL_VAL;
    /* Check if root equiv to y */
    SnoVal eq_args[2] = { root, y };
    if (!is_null(_w_Equiv(eq_args, 2))) {
        aply(fname, &xn, 1);
        return STR_VAL("");
    }
    /* Recurse into children */
    if (root.type == UDEF) {
        SnoVal nc = field_get(root, "n");
        SnoVal ca = field_get(root, "c");
        int cnt = (int)to_int(nc);
        for (int i = 1; i <= cnt; i++) {
            SnoVal child = subscript_get(ca, INT_VAL(i));
            /* For child, we'd need a temp var — skip deep recursion for now */
            (void)child;
        }
    }
    return NULL_VAL;
}

/* Insert(x, y, place) — tree.inc: insert y into tree x at position place */
static SnoVal _w_Insert(SnoVal *a, int n) {
    if (n < 3) return n > 0 ? a[0] : NULL_VAL;
    SnoVal x     = a[0];
    SnoVal y     = a[1];
    int    place = (int)to_int(a[2]);
    if (x.type != UDEF) return x;

    SnoVal nc  = field_get(x, "n");
    SnoVal ca  = field_get(x, "c");
    int    cnt = (int)to_int(nc);

    /* Build new children array with y inserted at place */
    int new_cnt = cnt + 1;
    SnoVal new_c = array_create(STR_VAL("1:256"));
    for (int i = 1; i < place && i <= cnt; i++)
        subscript_set(new_c, INT_VAL(i), subscript_get(ca, INT_VAL(i)));
    subscript_set(new_c, INT_VAL(place), y);
    for (int i = place; i <= cnt; i++)
        subscript_set(new_c, INT_VAL(i+1), subscript_get(ca, INT_VAL(i)));

    field_set(x, "n", INT_VAL(new_cnt));
    field_set(x, "c", new_c);
    return x;
}

/* Register all the missing functions */
void inc_init_extra(void) {
    register_fn("icase",      _w_icase,      1, 1);
    register_fn("IsSnobol4",  _w_IsSnobol4,  0, 0);
    register_fn("Push",       _w_Push,        1, 1);
    register_fn("Pop",        _w_Pop,         0, 1);
    register_fn("TopCounter", _w_TopCounter,  0, 0);
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
