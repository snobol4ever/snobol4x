/* snobol4.c — SNOBOL4-tiny Sprint 20 runtime implementation
 *
 * Implements everything declared in snobol4.h.
 * Memory model: Boehm GC (D1) — no free() anywhere.
 * Build: cc -o prog prog.c snobol4.c -lgc
 */

#include "snobol4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>

/* ============================================================
 * COMM — monitor telemetry (Pick Monitor / double-trace)
 *
 * When sno_monitor_fd >= 0, every statement and every variable
 * assignment emits an event to that file descriptor.
 * Run with:  ./beautiful 2>/tmp/binary_trace.txt
 * (sno_monitor_fd defaults to stderr = fd 2)
 *
 * Format mirrors CSNOBOL4 &TRACE output:
 *   STNO <n>
 *   VAR <name> <quoted-value>
 * ============================================================ */
int sno_monitor_fd = -1;   /* -1 = disabled; set to 2 for stderr */

/* &STCOUNT — incremented every statement; checked against &STLIMIT */
int64_t sno_kw_stcount = 0;

void sno_comm_stno(int n) {
    /* Enforce &STLIMIT (patch P001 — was declared but never checked) */
    if (sno_kw_stlimit >= 0 && ++sno_kw_stcount > sno_kw_stlimit) {
        fprintf(stderr, "\n** &STLIMIT exceeded at statement %d"
                        " (&STCOUNT=%lld &STLIMIT=%lld)\n",
                n, (long long)sno_kw_stcount, (long long)sno_kw_stlimit);
        exit(1);
    }
    if (sno_monitor_fd < 0) return;
    dprintf(sno_monitor_fd, "STNO %d\n", n);
}

void sno_comm_var(const char *name, SnoVal val) {
    if (sno_monitor_fd < 0) return;
    /* skip noisy internal variables */
    if (!name) return;
    if (name[0] == '_') return;          /* internal scratch vars */
    const char *s = sno_to_str(val);
    dprintf(sno_monitor_fd, "VAR %s \"%s\"\n", name, s ? s : "<null>");
}

/* ============================================================
 * Runtime initialization
 * ============================================================ */

/* Global character constants (set by sno_runtime_init) */
int64_t sno_kw_fullscan = 0;
int64_t sno_kw_maxlngth = 524288;
int64_t sno_kw_anchor   = 0;
int64_t sno_kw_trim     = 1;
int64_t sno_kw_stlimit  = -1;

char sno_ucase[27]    = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char sno_lcase[27]    = "abcdefghijklmnopqrstuvwxyz";
char sno_digits[11]   = "0123456789";
char sno_alphabet[257];  /* all 256 ASCII chars */

/* ============================================================
 * Numeric comparison builtins: GT LT GE LE EQ NE
 * SNOBOL4 semantics: succeed (return first arg) or fail (SNO_FAIL_VAL).
 * Also INTEGER, SIZE, REAL type/conversion builtins.
 * ============================================================ */

static SnoVal _b_GT(SnoVal *a, int n) {
    if (n < 2) return SNO_FAIL_VAL;
    return sno_gt(a[0], a[1]) ? a[0] : SNO_FAIL_VAL;
}
static SnoVal _b_LT(SnoVal *a, int n) {
    if (n < 2) return SNO_FAIL_VAL;
    return sno_lt(a[0], a[1]) ? a[0] : SNO_FAIL_VAL;
}
static SnoVal _b_GE(SnoVal *a, int n) {
    if (n < 2) return SNO_FAIL_VAL;
    return sno_ge(a[0], a[1]) ? a[0] : SNO_FAIL_VAL;
}
static SnoVal _b_LE(SnoVal *a, int n) {
    if (n < 2) return SNO_FAIL_VAL;
    return sno_le(a[0], a[1]) ? a[0] : SNO_FAIL_VAL;
}
static SnoVal _b_EQ(SnoVal *a, int n) {
    if (n < 2) return SNO_FAIL_VAL;
    /* Numeric equality: equal returns first arg, else fail */
    if (a[0].type == SNO_INT && a[1].type == SNO_INT)
        return (a[0].i == a[1].i) ? a[0] : SNO_FAIL_VAL;
    return (sno_to_real(a[0]) == sno_to_real(a[1])) ? a[0] : SNO_FAIL_VAL;
}
static SnoVal _b_NE(SnoVal *a, int n) {
    if (n < 2) return SNO_FAIL_VAL;
    if (a[0].type == SNO_INT && a[1].type == SNO_INT)
        return (a[0].i != a[1].i) ? a[0] : SNO_FAIL_VAL;
    return (sno_to_real(a[0]) != sno_to_real(a[1])) ? a[0] : SNO_FAIL_VAL;
}
static SnoVal _b_INTEGER(SnoVal *a, int n) {
    if (n < 1) return SNO_FAIL_VAL;
    /* Succeed (returning int value) if arg is or converts to integer */
    if (a[0].type == SNO_INT) return a[0];
    if (a[0].type == SNO_STR && a[0].s) {
        char *end;
        long long v = strtoll(a[0].s, &end, 10);
        if (end != a[0].s && *end == '\0') return SNO_INT_VAL(v);
    }
    return SNO_FAIL_VAL;
}
static SnoVal _b_REAL(SnoVal *a, int n) {
    if (n < 1) return SNO_FAIL_VAL;
    if (a[0].type == SNO_REAL) return a[0];
    if (a[0].type == SNO_INT)  return (SnoVal){ .type = SNO_REAL, .r = (double)a[0].i };
    if (a[0].type == SNO_STR && a[0].s) {
        char *end;
        double v = strtod(a[0].s, &end);
        if (end != a[0].s && *end == '\0') return (SnoVal){ .type = SNO_REAL, .r = v };
    }
    return SNO_FAIL_VAL;
}
static SnoVal _b_SIZE(SnoVal *a, int n) {
    if (n < 1) return SNO_INT_VAL(0);
    const char *s = sno_to_str(a[0]);
    return SNO_INT_VAL((int64_t)(s ? strlen(s) : 0));
}

/* Sprint 23: IDENT, DIFFER, HOST, ENDFILE, APPLY + string builtins as callable */
static SnoVal _b_IDENT(SnoVal *a, int n) {
    SnoVal x = (n > 0) ? a[0] : SNO_NULL_VAL;
    SnoVal y = (n > 1) ? a[1] : SNO_NULL_VAL;
    return sno_ident(x, y) ? SNO_NULL_VAL : SNO_FAIL_VAL;
}
static SnoVal _b_DIFFER(SnoVal *a, int n) {
    SnoVal x = (n > 0) ? a[0] : SNO_NULL_VAL;
    SnoVal y = (n > 1) ? a[1] : SNO_NULL_VAL;
    return sno_differ(x, y) ? x : SNO_FAIL_VAL;
}
static SnoVal _b_HOST(SnoVal *a, int n) {
    /* HOST(0) = command args string, HOST(1) = PID, HOST(3) = argc */
    if (n < 1) return SNO_NULL_VAL;
    int64_t selector = sno_to_int(a[0]);
    if (selector == 0) return SNO_STR_VAL(GC_strdup(""));
    if (selector == 1) {
        char buf[32]; snprintf(buf, sizeof(buf), "%d", (int)getpid());
        return SNO_STR_VAL(GC_strdup(buf));
    }
    if (selector == 3) return SNO_INT_VAL(0);
    return SNO_NULL_VAL;
}
static SnoVal _b_ENDFILE(SnoVal *a, int n) {
    (void)a; (void)n;
    return SNO_FAIL_VAL;  /* not at EOF on any channel */
}
static SnoVal _b_APPLY(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    const char *fname = sno_to_str(a[0]);
    return sno_apply(fname, a + 1, n - 1);
}
static SnoVal _b_LPAD(SnoVal *a, int n) {
    if (n < 2) return n > 0 ? a[0] : SNO_NULL_VAL;
    return sno_lpad_fn(a[0], a[1], n > 2 ? a[2] : SNO_STR_VAL(" "));
}
static SnoVal _b_RPAD(SnoVal *a, int n) {
    if (n < 2) return n > 0 ? a[0] : SNO_NULL_VAL;
    return sno_rpad_fn(a[0], a[1], n > 2 ? a[2] : SNO_STR_VAL(" "));
}
static SnoVal _b_CHAR(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_char_fn(a[0]);
}
static SnoVal _b_DUPL(SnoVal *a, int n) {
    if (n < 2) return SNO_NULL_VAL;
    return sno_dupl_fn(a[0], a[1]);
}
static SnoVal _b_REPLACE(SnoVal *a, int n) {
    if (n < 3) return SNO_NULL_VAL;
    return sno_replace_fn(a[0], a[1], a[2]);
}
static SnoVal _b_TRIM(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_trim_fn(a[0]);
}
static SnoVal _b_SUBSTR(SnoVal *a, int n) {
    if (n < 3) return SNO_NULL_VAL;
    return sno_substr_fn(a[0], a[1], a[2]);
}
static SnoVal _b_REVERSE(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_reverse_fn(a[0]);
}
static SnoVal _b_DATATYPE(SnoVal *a, int n) {
    if (n < 1) return SNO_STR_VAL("STRING");
    return SNO_STR_VAL((char*)sno_datatype(a[0]));
}

/* EVAL / OPSYN / SORT wrappers — file scope required */
extern SnoVal sno_eval(SnoVal);
extern SnoVal sno_opsyn(SnoVal, SnoVal, SnoVal);
extern SnoVal sno_sort_fn(SnoVal);
static SnoVal _b_EVAL(SnoVal *a, int n)  { return sno_eval(n>0?a[0]:SNO_NULL_VAL); }
static SnoVal _b_OPSYN(SnoVal *a, int n) {
    return sno_opsyn(n>0?a[0]:SNO_NULL_VAL,n>1?a[1]:SNO_NULL_VAL,n>2?a[2]:SNO_NULL_VAL); }
static SnoVal _b_SORT(SnoVal *a, int n)  { return sno_sort_fn(n>0?a[0]:SNO_NULL_VAL); }

/* Sprint 23: counter stack and tree field accessors as callable SnoVal functions */
static SnoVal _b_nPush(SnoVal *a, int n) {
    (void)a; (void)n;
    sno_npush();
    return SNO_NULL_VAL;
}
static SnoVal _b_nInc(SnoVal *a, int n) {
    (void)a; (void)n;
    sno_ninc();
    return SNO_INT_VAL(sno_ntop());
}
static SnoVal _b_nDec(SnoVal *a, int n) {
    (void)a; (void)n;
    sno_ndec();
    return SNO_INT_VAL(sno_ntop());
}
static SnoVal _b_nTop(SnoVal *a, int n) {
    (void)a; (void)n;
    return SNO_INT_VAL(sno_ntop());
}
static SnoVal _b_nPop(SnoVal *a, int n) {
    (void)a; (void)n;
    int64_t val = sno_ntop();
    sno_npop();
    return SNO_INT_VAL(val);
}
/* Tree field accessors: n(x), t(x), v(x), c(x) */
static SnoVal _b_tree_n(SnoVal *a, int n) {
    if (n < 1) return SNO_INT_VAL(0);
    return sno_field_get(a[0], "n");
}
static SnoVal _b_tree_t(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_field_get(a[0], "t");
}
static SnoVal _b_tree_v(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_field_get(a[0], "v");
}
static SnoVal _b_tree_c(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_field_get(a[0], "c");
}
/* link_counter / link_tag field accessors: value(x), next(x) */
static SnoVal _b_field_value(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_field_get(a[0], "value");
}
static SnoVal _b_field_next(SnoVal *a, int n) {
    if (n < 1) return SNO_NULL_VAL;
    return sno_field_get(a[0], "next");
}

void sno_runtime_init(void) {
    GC_INIT();
    /* Build &ALPHABET: all 256 chars in order */
    for (int i = 0; i < 256; i++) sno_alphabet[i] = (char)i;
    sno_alphabet[256] = '\0';
    /* Enable monitor if SNO_MONITOR=1 (writes to stderr) */
    const char *mon = getenv("SNO_MONITOR");
    if (mon && mon[0] == '1') sno_monitor_fd = 2;

    /* Register numeric comparison builtins */
    extern void sno_register_fn(const char *, SnoVal (*)(SnoVal*, int), int, int);
    sno_register_fn("GT",       _b_GT,       2, 2);
    sno_register_fn("LT",       _b_LT,       2, 2);
    sno_register_fn("GE",       _b_GE,       2, 2);
    sno_register_fn("LE",       _b_LE,       2, 2);
    sno_register_fn("EQ",       _b_EQ,       2, 2);
    sno_register_fn("NE",       _b_NE,       2, 2);
    sno_register_fn("INTEGER",  _b_INTEGER,  1, 1);
    sno_register_fn("REAL",     _b_REAL,     1, 1);
    sno_register_fn("SIZE",     _b_SIZE,     1, 1);
    /* Sprint 23: string predicates and host interface */
    sno_register_fn("IDENT",    _b_IDENT,    0, 2);
    sno_register_fn("DIFFER",   _b_DIFFER,   0, 2);
    sno_register_fn("HOST",     _b_HOST,     1, 4);
    sno_register_fn("ENDFILE",  _b_ENDFILE,  1, 1);
    sno_register_fn("APPLY",    _b_APPLY,    1, 9);
    sno_register_fn("LPAD",     _b_LPAD,     2, 3);
    sno_register_fn("RPAD",     _b_RPAD,     2, 3);
    sno_register_fn("CHAR",     _b_CHAR,     1, 1);
    sno_register_fn("DUPL",     _b_DUPL,     2, 2);
    sno_register_fn("REPLACE",  _b_REPLACE,  3, 3);
    sno_register_fn("TRIM",     _b_TRIM,     1, 1);
    sno_register_fn("SUBSTR",   _b_SUBSTR,   3, 3);
    sno_register_fn("REVERSE",  _b_REVERSE,  1, 1);
    sno_register_fn("DATATYPE", _b_DATATYPE, 1, 1);
    sno_register_fn("EVAL",  _b_EVAL,  1, 1);
    sno_register_fn("OPSYN", _b_OPSYN, 2, 3);
    sno_register_fn("SORT",  _b_SORT,  1, 1);
    sno_register_fn("nPush",    _b_nPush,    0, 0);
    sno_register_fn("nInc",     _b_nInc,     0, 0);
    sno_register_fn("nDec",     _b_nDec,     0, 0);
    sno_register_fn("nTop",     _b_nTop,     0, 0);
    sno_register_fn("nPop",     _b_nPop,     0, 0);
    sno_register_fn("n",        _b_tree_n,      1, 1);
    sno_register_fn("t",        _b_tree_t,      1, 1);
    sno_register_fn("v",        _b_tree_v,      1, 1);
    sno_register_fn("c",        _b_tree_c,      1, 1);
    sno_register_fn("value",    _b_field_value, 1, 1);
    sno_register_fn("next",     _b_field_next,  1, 1);
    /* Sprint 23: pre-init &ALPHABET-derived constants from global.sno
     * &ALPHABET is a 256-char binary string; POS(n) LEN(1) . var extracts char(n).
     * Since SNO_STR_VAL uses strlen, &ALPHABET[0]=NUL causes all matches to fail.
     * We pre-initialize the key character constants directly. */
    {
        char *_ch = GC_malloc_atomic(2);
        _ch[0] = (char)9;  _ch[1] = '\0'; sno_var_set("tab", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)9;  _ch[1] = '\0'; sno_var_set("ht", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)10; _ch[1] = '\0'; sno_var_set("nl", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)10; _ch[1] = '\0'; sno_var_set("lf", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)13; _ch[1] = '\0'; sno_var_set("cr", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)12; _ch[1] = '\0'; sno_var_set("ff", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)11; _ch[1] = '\0'; sno_var_set("vt", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)8;  _ch[1] = '\0'; sno_var_set("bs", SNO_STR_VAL(_ch));
        sno_var_set("nul", SNO_STR_VAL(""));  /* char(0) = empty in string context */
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)47; _ch[1] = '\0'; sno_var_set("fSlash", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)92; _ch[1] = '\0'; sno_var_set("bSlash", SNO_STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)59; _ch[1] = '\0'; sno_var_set("semicolon", SNO_STR_VAL(_ch));
    }
}

/* ============================================================
 * String utilities
 * ============================================================ */

char *sno_dup(const char *s) {
    if (!s) return GC_strdup("");
    return GC_strdup(s);
}

char *sno_concat(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char *r = GC_malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb);
    r[la + lb] = '\0';
    return r;
}

/* P003: SnoVal concat — propagates SNO_FAIL_VAL if either operand is FAIL.
 * If either operand is a PATTERN, build a pattern concatenation instead of
 * string concatenation (blank-juxtaposition of patterns = pattern cat). */
SnoVal sno_concat_sv(SnoVal a, SnoVal b) {
    if (a.type == SNO_FAIL) return SNO_FAIL_VAL;
    if (b.type == SNO_FAIL) return SNO_FAIL_VAL;
    if (a.type == SNO_PATTERN || b.type == SNO_PATTERN)
        return sno_pat_cat(a, b);
    const char *sa = sno_to_str(a);
    const char *sb = sno_to_str(b);
    return SNO_STR_VAL(sno_concat(sa, sb));
}

int64_t sno_size(const char *s) {
    return s ? (int64_t)strlen(s) : 0;
}

/* ============================================================
 * Type conversions
 * ============================================================ */

char *sno_to_str(SnoVal v) {
    char buf[64];
    switch (v.type) {
        case SNO_NULL:    return GC_strdup("");
        case SNO_STR:     return v.s ? v.s : GC_strdup("");
        case SNO_INT:
            snprintf(buf, sizeof(buf), "%" PRId64, v.i);
            return GC_strdup(buf);
        case SNO_REAL: {
            /* SNOBOL4 real format: no trailing zeros, no .0 for whole numbers */
            snprintf(buf, sizeof(buf), "%.15g", v.r);
            /* If no decimal point and no 'e', add trailing dot (SPITBOL style) */
            if (!strchr(buf, '.') && !strchr(buf, 'e'))
                strncat(buf, ".", sizeof(buf) - strlen(buf) - 1);
            return GC_strdup(buf);
        }
        case SNO_TREE:
            /* Trees stringify as their tag */
            return v.t ? GC_strdup(v.t->tag) : GC_strdup("");
        default:
            return GC_strdup("");
    }
}

int64_t sno_to_int(SnoVal v) {
    switch (v.type) {
        case SNO_INT:  return v.i;
        case SNO_REAL: return (int64_t)v.r;
        case SNO_STR:
        case SNO_NULL: {
            const char *s = v.s ? v.s : "";
            while (*s == ' ') s++;
            if (!*s) return 0;
            return (int64_t)strtoll(s, NULL, 10);
        }
        default: return 0;
    }
}

double sno_to_real(SnoVal v) {
    switch (v.type) {
        case SNO_REAL: return v.r;
        case SNO_INT:  return (double)v.i;
        case SNO_STR:
        case SNO_NULL: {
            const char *s = v.s ? v.s : "";
            return strtod(s, NULL);
        }
        default: return 0.0;
    }
}

const char *sno_datatype(SnoVal v) {
    switch (v.type) {
        case SNO_NULL:    return "STRING";  /* NULL = empty string */
        case SNO_STR:     return "STRING";
        case SNO_INT:     return "INTEGER";
        case SNO_REAL:    return "REAL";
        case SNO_TREE:    return v.t ? v.t->tag : "TREE";
        case SNO_PATTERN: return "PATTERN";
        case SNO_ARRAY:   return "ARRAY";
        case SNO_TABLE:   return "TABLE";
        case SNO_CODE:    return "CODE";
        case SNO_UDEF:    return v.u ? v.u->type->name : "UDEF";
        default:          return "STRING";
    }
}

/* ============================================================
 * Tree operations
 * ============================================================ */

Tree *sno_tree_new(const char *tag, SnoVal val) {
    Tree *t = GC_malloc(sizeof(Tree));
    t->tag = GC_strdup(tag ? tag : "");
    t->val = val;
    t->n   = 0;
    t->cap = 0;
    t->c   = NULL;
    return t;
}

Tree *sno_tree_new0(const char *tag) {
    return sno_tree_new(tag, SNO_NULL_VAL);
}

static void _tree_ensure_cap(Tree *x, int needed) {
    if (x->cap >= needed) return;
    int newcap = x->cap ? x->cap * 2 : 4;
    while (newcap < needed) newcap *= 2;
    Tree **nc = GC_malloc(newcap * sizeof(Tree *));
    if (x->c) memcpy(nc, x->c, x->n * sizeof(Tree *));
    x->c   = nc;
    x->cap = newcap;
}

void sno_tree_append(Tree *x, Tree *y) {
    _tree_ensure_cap(x, x->n + 1);
    x->c[x->n++] = y;
}

void sno_tree_prepend(Tree *x, Tree *y) {
    _tree_ensure_cap(x, x->n + 1);
    memmove(x->c + 1, x->c, x->n * sizeof(Tree *));
    x->c[0] = y;
    x->n++;
}

void sno_tree_insert(Tree *x, Tree *y, int place) {
    /* place is 1-based */
    if (place < 1) place = 1;
    if (place > x->n + 1) place = x->n + 1;
    _tree_ensure_cap(x, x->n + 1);
    int idx = place - 1;
    memmove(x->c + idx + 1, x->c + idx, (x->n - idx) * sizeof(Tree *));
    x->c[idx] = y;
    x->n++;
}

Tree *sno_tree_remove(Tree *x, int place) {
    /* place is 1-based */
    if (!x || place < 1 || place > x->n) return NULL;
    int idx = place - 1;
    Tree *removed = x->c[idx];
    memmove(x->c + idx, x->c + idx + 1, (x->n - idx - 1) * sizeof(Tree *));
    x->n--;
    return removed;
}

/* ============================================================
 * Array
 * ============================================================ */

SnoArray *sno_array_new(int lo, int hi) {
    SnoArray *a = GC_malloc(sizeof(SnoArray));
    a->lo   = lo;
    a->hi   = hi;
    a->ndim = 1;
    int sz  = hi - lo + 1;
    if (sz < 1) sz = 1;
    a->data = GC_malloc(sz * sizeof(SnoVal));
    for (int i = 0; i < sz; i++) a->data[i] = SNO_NULL_VAL;
    return a;
}

SnoArray *sno_array_new2d(int lo1, int hi1, int lo2, int hi2) {
    /* Stored as flat row-major: index = (i-lo1)*(hi2-lo2+1) + (j-lo2) */
    SnoArray *a = GC_malloc(sizeof(SnoArray));
    a->lo   = lo1;
    a->hi   = hi1;
    a->ndim = 2;
    int rows = hi1 - lo1 + 1;
    int cols = hi2 - lo2 + 1;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    a->data = GC_malloc(rows * cols * sizeof(SnoVal));
    for (int i = 0; i < rows * cols; i++) a->data[i] = SNO_NULL_VAL;
    /* Store hi2/lo2 in spare fields — abuse: hi=hi2 in a second slot.
     * For simplicity, encode cols in a separate field. */
    /* Use tag trick: store cols count in a SnoVal at position -1.
     * Simpler: always allocate +1 and store cols at index 0. */
    /* Actually: store lo2/hi2 by repurposing ndim as cols */
    a->ndim = cols;  /* repurpose: ndim = cols for 2D arrays */
    return a;
}

SnoVal sno_array_get(SnoArray *a, int i) {
    if (!a) return SNO_FAIL_VAL;
    int idx = i - a->lo;
    if (idx < 0 || idx >= (a->hi - a->lo + 1)) return SNO_FAIL_VAL;  /* P002 */
    return a->data[idx];
}

void sno_array_set(SnoArray *a, int i, SnoVal v) {
    if (!a) return;
    int idx = i - a->lo;
    if (idx < 0 || idx >= (a->hi - a->lo + 1)) return;
    a->data[idx] = v;
}

SnoVal sno_array_get2(SnoArray *a, int i, int j) {
    if (!a) return SNO_NULL_VAL;
    int cols = a->ndim;  /* cols stored in ndim for 2D */
    int row  = i - a->lo;
    /* j-origin: assume lo2 = 1 (SNOBOL4 default) */
    int col  = j - 1;
    int idx  = row * cols + col;
    return a->data[idx];
}

void sno_array_set2(SnoArray *a, int i, int j, SnoVal v) {
    if (!a) return;
    int cols = a->ndim;
    int row  = i - a->lo;
    int col  = j - 1;
    int idx  = row * cols + col;
    a->data[idx] = v;
}

/* ============================================================
 * Table (hash map)
 * ============================================================ */

static unsigned _tbl_hash(const char *key) {
    unsigned h = 5381;
    while (*key) h = h * 33 ^ (unsigned char)*key++;
    return h % SNO_TABLE_BUCKETS;
}

SnoTable *sno_table_new(void) {
    SnoTable *t = GC_malloc(sizeof(SnoTable));
    memset(t->buckets, 0, sizeof(t->buckets));
    t->size = 0;
    return t;
}

SnoVal sno_table_get(SnoTable *tbl, const char *key) {
    if (!tbl || !key) return SNO_NULL_VAL;
    unsigned h = _tbl_hash(key);
    for (SnoTableEntry *e = tbl->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->val;
    return SNO_NULL_VAL;
}

void sno_table_set(SnoTable *tbl, const char *key, SnoVal val) {
    if (!tbl || !key) return;
    unsigned h = _tbl_hash(key);
    for (SnoTableEntry *e = tbl->buckets[h]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) { e->val = val; return; }
    }
    SnoTableEntry *e = GC_malloc(sizeof(SnoTableEntry));
    e->key  = GC_strdup(key);
    e->val  = val;
    e->next = tbl->buckets[h];
    tbl->buckets[h] = e;
    tbl->size++;
}

int sno_table_has(SnoTable *tbl, const char *key) {
    if (!tbl || !key) return 0;
    unsigned h = _tbl_hash(key);
    for (SnoTableEntry *e = tbl->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return 1;
    return 0;
}

/* ============================================================
 * User-defined datatypes (DATA() mechanism)
 * ============================================================ */

static UDefType *_udef_types = NULL;

/* Parse DATA spec: "tree(t,v,n,c)" → name="tree", fields=["t","v","n","c"] */
void sno_data_define(const char *spec) {
    /* Spec format: "typename(field1,field2,...)" */
    char *s = GC_strdup(spec);
    char *paren = strchr(s, '(');
    if (!paren) return;
    *paren = '\0';
    char *name = s;
    char *fields_str = paren + 1;
    char *close = strchr(fields_str, ')');
    if (close) *close = '\0';

    UDefType *t = GC_malloc(sizeof(UDefType));
    t->name = GC_strdup(name);

    /* Count and extract fields */
    int nfields = 0;
    char *tmp = GC_strdup(fields_str);
    char *tok = strtok(tmp, ",");
    while (tok) { nfields++; tok = strtok(NULL, ","); }

    t->nfields = nfields;
    t->fields  = GC_malloc(nfields * sizeof(char *));

    tmp = GC_strdup(fields_str);
    tok = strtok(tmp, ",");
    for (int i = 0; i < nfields && tok; i++) {
        /* Trim whitespace */
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        t->fields[i] = GC_strdup(tok);
        tok = strtok(NULL, ",");
    }

    t->next    = _udef_types;
    _udef_types = t;
}

static UDefType *_udef_lookup(const char *name) {
    for (UDefType *t = _udef_types; t; t = t->next)
        if (strcasecmp(t->name, name) == 0) return t;
    return NULL;
}

SnoVal sno_udef_new(const char *typename, ...) {
    UDefType *t = _udef_lookup(typename);
    if (!t) return SNO_NULL_VAL;

    UDef *u = GC_malloc(sizeof(UDef));
    u->type   = t;
    u->fields = GC_malloc(t->nfields * sizeof(SnoVal));
    for (int i = 0; i < t->nfields; i++) u->fields[i] = SNO_NULL_VAL;

    /* Assign varargs fields */
    va_list ap;
    va_start(ap, typename);
    for (int i = 0; i < t->nfields; i++) {
        SnoVal v = va_arg(ap, SnoVal);
        /* sentinel check: if type == SNO_NULL and s == NULL, stop */
        if (v.type == SNO_NULL && v.s == NULL) break;
        u->fields[i] = v;
    }
    va_end(ap);

    return (SnoVal){ .type = SNO_UDEF, .u = u };
}

SnoVal sno_field_get(SnoVal obj, const char *field) {
    if (obj.type != SNO_UDEF || !obj.u) return SNO_NULL_VAL;
    UDefType *t = obj.u->type;
    for (int i = 0; i < t->nfields; i++)
        if (strcasecmp(t->fields[i], field) == 0)
            return obj.u->fields[i];
    return SNO_NULL_VAL;
}

void sno_field_set(SnoVal obj, const char *field, SnoVal val) {
    if (obj.type != SNO_UDEF || !obj.u) return;
    UDefType *t = obj.u->type;
    for (int i = 0; i < t->nfields; i++)
        if (strcasecmp(t->fields[i], field) == 0) {
            obj.u->fields[i] = val;
            return;
        }
}

/* ============================================================
 * Variable table
 * ============================================================ */

#define VAR_BUCKETS 512

typedef struct _VarEntry {
    char   *name;
    SnoVal  val;
    struct _VarEntry *next;
} VarEntry;

static VarEntry *_var_buckets[VAR_BUCKETS];
static int _var_init_done = 0;

static void _var_init(void) {
    if (_var_init_done) return;
    memset(_var_buckets, 0, sizeof(_var_buckets));
    _var_init_done = 1;
}

static unsigned _var_hash(const char *name) {
    unsigned h = 5381;
    while (*name) h = h * 33 ^ (unsigned char)*name++;
    return h % VAR_BUCKETS;
}

SnoVal sno_var_get(const char *name) {
    _var_init();
    if (!name) return SNO_NULL_VAL;
    /* Special I/O variables */
    if (strcmp(name, "INPUT") == 0) return sno_input_read();
    unsigned h = _var_hash(name);
    for (VarEntry *e = _var_buckets[h]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e->val;
    return SNO_NULL_VAL;
}

void sno_var_set(const char *name, SnoVal val) {
    _var_init();
    if (!name) return;
    sno_comm_var(name, val);
    /* Special I/O variables */
    if (strcmp(name, "OUTPUT") == 0) { sno_output_val(val); return; }
    unsigned h = _var_hash(name);
    for (VarEntry *e = _var_buckets[h]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) { e->val = val; return; }
    }
    VarEntry *e = GC_malloc(sizeof(VarEntry));
    e->name = GC_strdup(name);
    e->val  = val;
    e->next = _var_buckets[h];
    _var_buckets[h] = e;
}

/* $name — indirect variable: the variable whose name is the value of 'name' */
SnoVal sno_indirect_get(const char *name) {
    SnoVal indirect_name = sno_var_get(name);
    const char *target = sno_to_str(indirect_name);
    return sno_var_get(target);
}

void sno_indirect_set(const char *name, SnoVal val) {
    SnoVal indirect_name = sno_var_get(name);
    const char *target = sno_to_str(indirect_name);
    sno_var_set(target, val);
}

/* ============================================================
 * Counter stack (nPush/nInc/nDec/nTop/nPop)
 * ============================================================ */

#define NSTACK_MAX 256
static int64_t _nstack[NSTACK_MAX];
static int      _ntop = -1;

void sno_npush(void) {
    if (_ntop < NSTACK_MAX - 1) _nstack[++_ntop] = 0;
}

void sno_ninc(void) {
    if (_ntop >= 0) _nstack[_ntop]++;
}

void sno_ndec(void) {
    if (_ntop >= 0) _nstack[_ntop]--;
}

int64_t sno_ntop(void) {
    return (_ntop >= 0) ? _nstack[_ntop] : 0;
}

void sno_npop(void) {
    if (_ntop >= 0) _ntop--;
}

/* ============================================================
 * Value stack (Push/Pop/Top for Shift/Reduce)
 * ============================================================ */

#define VSTACK_MAX 1024
static SnoVal _vstack[VSTACK_MAX];
static int    _vstop = -1;

void sno_push(SnoVal v) {
    if (_vstop < VSTACK_MAX - 1) _vstack[++_vstop] = v;
}

SnoVal sno_pop(void) {
    if (_vstop >= 0) return _vstack[_vstop--];
    return SNO_NULL_VAL;
}

SnoVal sno_top(void) {
    if (_vstop >= 0) return _vstack[_vstop];
    return SNO_NULL_VAL;
}

int sno_stack_depth(void) {
    return _vstop + 1;
}

/* ============================================================
 * Function table (DEFINE/APPLY)
 * ============================================================ */

#define FUNC_BUCKETS 128

typedef struct _FuncEntry {
    char   *name;
    char   *spec;       /* full DEFINE spec */
    SnoFunc fn;
    /* Parameter names */
    int     nparams;
    char  **params;
    int     nlocals;
    char  **locals;
    struct _FuncEntry *next;
} FuncEntry;

static FuncEntry *_func_buckets[FUNC_BUCKETS];
static int        _func_init_done = 0;

static void _func_init(void) {
    if (_func_init_done) return;
    memset(_func_buckets, 0, sizeof(_func_buckets));
    _func_init_done = 1;
}

static unsigned _func_hash(const char *name) {
    unsigned h = 5381;
    while (*name) h = h * 33 ^ (unsigned char)*name++;
    return h % FUNC_BUCKETS;
}

/* Parse DEFINE spec: "name(p1,p2)local1,local2"
 * Return allocated FuncEntry with name/params/locals filled */
static FuncEntry *_parse_define_spec(const char *spec) {
    FuncEntry *fe = GC_malloc(sizeof(FuncEntry));
    char *s = GC_strdup(spec);
    fe->spec = GC_strdup(spec);

    /* Split on '(' */
    char *paren = strchr(s, '(');
    if (!paren) {
        /* No params: "name" or "name,local1,local2" */
        char *comma = strchr(s, ',');
        if (comma) {
            *comma = '\0';
            fe->name = GC_strdup(s);
            /* Locals follow the comma */
            char *lstr = GC_strdup(comma + 1);
            int nl = 0;
            char *tok = strtok(lstr, ",");
            while (tok) { nl++; tok = strtok(NULL, ","); }
            fe->nlocals = nl;
            fe->locals  = GC_malloc(nl * sizeof(char *));
            lstr = GC_strdup(comma + 1);
            tok  = strtok(lstr, ",");
            for (int i = 0; i < nl && tok; i++) {
                while (*tok == ' ') tok++;
                fe->locals[i] = GC_strdup(tok);
                tok = strtok(NULL, ",");
            }
        } else {
            fe->name = GC_strdup(s);
        }
        fe->nparams = 0;
        fe->params  = NULL;
        return fe;
    }

    *paren = '\0';
    fe->name = GC_strdup(s);

    char *close = strchr(paren + 1, ')');
    char *locals_str = NULL;
    if (close) {
        locals_str = close + 1;
        if (*locals_str == ',') locals_str++;
        *close = '\0';
    }

    /* Parse params */
    char *pstr = GC_strdup(paren + 1);
    int np = 0;
    if (*pstr) {
        char *tok = strtok(pstr, ",");
        while (tok) { np++; tok = strtok(NULL, ","); }
    }
    fe->nparams = np;
    fe->params  = np ? GC_malloc(np * sizeof(char *)) : NULL;
    if (np) {
        pstr = GC_strdup(paren + 1);
        char *tok = strtok(pstr, ",");
        for (int i = 0; i < np && tok; i++) {
            while (*tok == ' ') tok++;
            fe->params[i] = GC_strdup(tok);
            tok = strtok(NULL, ",");
        }
    }

    /* Parse locals */
    int nl = 0;
    fe->nlocals = 0;
    fe->locals  = NULL;
    if (locals_str && *locals_str) {
        char *lstr = GC_strdup(locals_str);
        char *tok  = strtok(lstr, ",");
        while (tok) { nl++; tok = strtok(NULL, ","); }
        fe->nlocals = nl;
        fe->locals  = GC_malloc(nl * sizeof(char *));
        lstr = GC_strdup(locals_str);
        tok  = strtok(lstr, ",");
        for (int i = 0; i < nl && tok; i++) {
            while (*tok == ' ') tok++;
            fe->locals[i] = GC_strdup(tok);
            tok = strtok(NULL, ",");
        }
    }

    return fe;
}

void sno_define(const char *spec, SnoFunc fn) {
    _func_init();
    FuncEntry *fe = _parse_define_spec(spec);
    fe->fn = fn;
    unsigned h = _func_hash(fe->name);
    /* Replace existing if same name */
    for (FuncEntry *e = _func_buckets[h]; e; e = e->next) {
        if (strcasecmp(e->name, fe->name) == 0) {
            e->spec    = fe->spec;
            e->fn      = fe->fn;
            e->nparams = fe->nparams;
            e->params  = fe->params;
            e->nlocals = fe->nlocals;
            e->locals  = fe->locals;
            return;
        }
    }
    fe->next = _func_buckets[h];
    _func_buckets[h] = fe;
}

SnoVal sno_apply(const char *name, SnoVal *args, int nargs) {
    _func_init();
    if (!name) return SNO_NULL_VAL;
    unsigned h = _func_hash(name);
    for (FuncEntry *e = _func_buckets[h]; e; e = e->next) {
        if (strcasecmp(e->name, name) == 0) {
            if (e->fn) return e->fn(args, nargs);
            break;
        }
    }
    return SNO_NULL_VAL;
}

int sno_func_exists(const char *name) {
    _func_init();
    if (!name) return 0;
    unsigned h = _func_hash(name);
    for (FuncEntry *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, name) == 0) return 1;
    return 0;
}

/* ============================================================
 * Builtin string functions
 * ============================================================ */

SnoVal sno_size_fn(SnoVal s) {
    const char *str = sno_to_str(s);
    return SNO_INT_VAL((int64_t)strlen(str));
}

SnoVal sno_dupl_fn(SnoVal s, SnoVal n) {
    const char *str = sno_to_str(s);
    int64_t times   = sno_to_int(n);
    if (times <= 0 || !str || !*str) return SNO_STR_VAL(GC_strdup(""));
    size_t slen = strlen(str);
    char *r = GC_malloc(slen * (size_t)times + 1);
    r[0] = '\0';
    for (int64_t i = 0; i < times; i++) memcpy(r + i * slen, str, slen);
    r[slen * times] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_replace_fn(SnoVal s, SnoVal from, SnoVal to) {
    /* REPLACE(s, from, to): for each char in from, replace with corresponding
     * char in to. Like tr command. */
    const char *str  = sno_to_str(s);
    const char *f    = sno_to_str(from);
    const char *t    = sno_to_str(to);
    size_t slen = strlen(str);
    char *r = GC_malloc(slen + 1);
    /* Build translation table */
    unsigned char xlat[256];
    for (int i = 0; i < 256; i++) xlat[i] = (unsigned char)i;
    size_t flen = strlen(f), tlen = strlen(t);
    for (size_t i = 0; i < flen; i++) {
        unsigned char fc = (unsigned char)f[i];
        unsigned char tc = (i < tlen) ? (unsigned char)t[i] : 0;
        xlat[fc] = tc;
    }
    size_t rlen = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = xlat[(unsigned char)str[i]];
        if (c) r[rlen++] = (char)c;
    }
    r[rlen] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_substr_fn(SnoVal s, SnoVal i, SnoVal n) {
    const char *str = sno_to_str(s);
    int64_t start   = sno_to_int(i);  /* 1-based */
    int64_t len_    = sno_to_int(n);
    int64_t slen    = (int64_t)strlen(str);
    if (start < 1) start = 1;
    if (start > slen + 1) return SNO_STR_VAL(GC_strdup(""));
    if (len_ < 0) len_ = 0;
    if (start - 1 + len_ > slen) len_ = slen - start + 1;
    char *r = GC_malloc((size_t)len_ + 1);
    memcpy(r, str + start - 1, (size_t)len_);
    r[len_] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_trim_fn(SnoVal s) {
    const char *str = sno_to_str(s);
    /* TRIM: remove trailing blanks */
    int len = (int)strlen(str);
    while (len > 0 && str[len-1] == ' ') len--;
    char *r = GC_malloc((size_t)len + 1);
    memcpy(r, str, (size_t)len);
    r[len] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_lpad_fn(SnoVal s, SnoVal n, SnoVal pad) {
    const char *str = sno_to_str(s);
    int64_t width   = sno_to_int(n);
    const char *p   = sno_to_str(pad);
    char padch      = (p && *p) ? p[0] : ' ';
    int64_t slen    = (int64_t)strlen(str);
    if (width <= slen) return SNO_STR_VAL(GC_strdup(str));
    int64_t npad = width - slen;
    char *r = GC_malloc((size_t)width + 1);
    memset(r, padch, (size_t)npad);
    memcpy(r + npad, str, (size_t)slen);
    r[width] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_rpad_fn(SnoVal s, SnoVal n, SnoVal pad) {
    const char *str = sno_to_str(s);
    int64_t width   = sno_to_int(n);
    const char *p   = sno_to_str(pad);
    char padch      = (p && *p) ? p[0] : ' ';
    int64_t slen    = (int64_t)strlen(str);
    if (width <= slen) return SNO_STR_VAL(GC_strdup(str));
    char *r = GC_malloc((size_t)width + 1);
    memcpy(r, str, (size_t)slen);
    memset(r + slen, padch, (size_t)(width - slen));
    r[width] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_reverse_fn(SnoVal s) {
    const char *str = sno_to_str(s);
    int len = (int)strlen(str);
    char *r = GC_malloc((size_t)len + 1);
    for (int i = 0; i < len; i++) r[i] = str[len - 1 - i];
    r[len] = '\0';
    return SNO_STR_VAL(r);
}

SnoVal sno_char_fn(SnoVal n) {
    int64_t code = sno_to_int(n);
    char buf[2];
    buf[0] = (char)(code & 0xFF);
    buf[1] = '\0';
    return SNO_STR_VAL(GC_strdup(buf));
}

SnoVal sno_integer_fn(SnoVal v) {
    /* INTEGER(v): convert to integer, fail if not possible */
    if (v.type == SNO_INT) return v;
    if (v.type == SNO_REAL) return SNO_INT_VAL((int64_t)v.r);
    if (v.type == SNO_STR || v.type == SNO_NULL) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (!*s) return SNO_NULL_VAL;  /* fail */
        char *end;
        long long iv = strtoll(s, &end, 10);
        while (*end == ' ') end++;
        if (*end) return SNO_NULL_VAL;  /* fail — not a pure integer */
        return SNO_INT_VAL((int64_t)iv);
    }
    return SNO_NULL_VAL;
}

SnoVal sno_real_fn(SnoVal v) {
    if (v.type == SNO_REAL) return v;
    if (v.type == SNO_INT)  return SNO_REAL_VAL((double)v.i);
    if (v.type == SNO_STR || v.type == SNO_NULL) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (!*s) return SNO_NULL_VAL;
        char *end;
        double rv = strtod(s, &end);
        while (*end == ' ') end++;
        if (*end) return SNO_NULL_VAL;
        return SNO_REAL_VAL(rv);
    }
    return SNO_NULL_VAL;
}

SnoVal sno_string_fn(SnoVal v) {
    return SNO_STR_VAL(sno_to_str(v));
}

/* ============================================================
 * Arithmetic / comparison
 * ============================================================ */

/* Arithmetic — promote int+int=int, otherwise real */
SnoVal sno_add(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT)
        return SNO_INT_VAL(a.i + b.i);
    return SNO_REAL_VAL(sno_to_real(a) + sno_to_real(b));
}

SnoVal sno_sub(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT)
        return SNO_INT_VAL(a.i - b.i);
    return SNO_REAL_VAL(sno_to_real(a) - sno_to_real(b));
}

SnoVal sno_mul(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT)
        return SNO_INT_VAL(a.i * b.i);
    return SNO_REAL_VAL(sno_to_real(a) * sno_to_real(b));
}

SnoVal sno_div(SnoVal a, SnoVal b) {
    /* SNOBOL4 / is real division; integer / integer = integer in SNOBOL4 */
    if (a.type == SNO_INT && b.type == SNO_INT) {
        if (b.i == 0) return SNO_NULL_VAL;  /* division error */
        return SNO_INT_VAL(a.i / b.i);
    }
    double denom = sno_to_real(b);
    if (denom == 0.0) return SNO_NULL_VAL;
    return SNO_REAL_VAL(sno_to_real(a) / denom);
}

SnoVal sno_pow(SnoVal a, SnoVal b) {
    return SNO_REAL_VAL(pow(sno_to_real(a), sno_to_real(b)));
}

SnoVal sno_neg(SnoVal a) {
    if (a.type == SNO_INT)  return SNO_INT_VAL(-a.i);
    if (a.type == SNO_REAL) return SNO_REAL_VAL(-a.r);
    return SNO_INT_VAL(-sno_to_int(a));
}

/* Numeric comparisons — return 1=success (true), 0=failure */
int sno_eq(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT) return a.i == b.i;
    return sno_to_real(a) == sno_to_real(b);
}
int sno_ne(SnoVal a, SnoVal b) { return !sno_eq(a, b); }
int sno_lt(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT) return a.i < b.i;
    return sno_to_real(a) < sno_to_real(b);
}
int sno_le(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT) return a.i <= b.i;
    return sno_to_real(a) <= sno_to_real(b);
}
int sno_gt(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT) return a.i > b.i;
    return sno_to_real(a) > sno_to_real(b);
}
int sno_ge(SnoVal a, SnoVal b) {
    if (a.type == SNO_INT && b.type == SNO_INT) return a.i >= b.i;
    return sno_to_real(a) >= sno_to_real(b);
}

/* IDENT: succeed if a and b are identical (same type and value) */
int sno_ident(SnoVal a, SnoVal b) {
    if (a.type != b.type) {
        /* "" and NULL are identical */
        int a_null = (a.type == SNO_NULL || (a.type == SNO_STR && (!a.s || !*a.s)));
        int b_null = (b.type == SNO_NULL || (b.type == SNO_STR && (!b.s || !*b.s)));
        if (a_null && b_null) return 1;
        return 0;
    }
    switch (a.type) {
        case SNO_NULL: return 1;
        case SNO_STR:  return strcmp(a.s ? a.s : "", b.s ? b.s : "") == 0;
        case SNO_INT:  return a.i == b.i;
        case SNO_REAL: return a.r == b.r;
        case SNO_TREE: return a.t == b.t;  /* pointer identity */
        default:       return a.ptr == b.ptr;
    }
}

int sno_differ(SnoVal a, SnoVal b) { return !sno_ident(a, b); }

/* ============================================================
 * I/O
 * ============================================================ */

void sno_output_val(SnoVal v) {
    char *s = sno_to_str(v);
    printf("%s\n", s ? s : "");
}

void sno_output_str(const char *s) {
    printf("%s\n", s ? s : "");
}

SnoVal sno_input_read(void) {
    static char *linebuf = NULL;
    static size_t linecap = 0;
    ssize_t nread = getline(&linebuf, &linecap, stdin);
    if (nread < 0) return SNO_NULL_VAL;  /* EOF = INPUT fails */
    /* Strip trailing newline */
    if (nread > 0 && linebuf[nread-1] == '\n') linebuf[nread-1] = '\0';
    return SNO_STR_VAL(GC_strdup(linebuf));
}

/* Indirect goto — called when :(var) computed goto is taken.
   Currently a stub: prints a warning and continues.
   Full implementation requires a label dispatch table. */
void sno_indirect_goto(const char *varname) {
    SnoVal v = sno_var_get(varname);
    const char *lbl = (v.type == SNO_STR) ? v.s : "(nil)";
    fprintf(stderr, "sno_indirect_goto: var=%s label=%s (not implemented)\n",
            varname, lbl);
}
