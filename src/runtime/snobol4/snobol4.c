/* snobol4.c — scrip-cc Sprint 20 runtime implementation
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
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>

/* ============================================================
 * COMM — monitor telemetry (sync-step 5-way monitor)
 *
 * Wire protocol — RS/US delimiters (ASCII 0x1E / 0x1F):
 *   KIND \x1E name \x1F value \x1E
 *   \x1E (RS) = record terminator; \x1F (US) = name/value separator
 *   Newlines and all bytes in values pass through unescaped.
 * ============================================================ */
#define MON_RS "\x1e"
#define MON_US "\x1f"
#include <sys/uio.h>

int monitor_fd  = -1;
int monitor_ack_fd = -1;
int monitor_ready = 0;  /* set to 1 after pre-init constants are installed */

/* Trace-registration set: only variables registered via TRACE(name,'VALUE')
 * are sent to the monitor.  Simple open-addressed hash set of C strings.
 * Capacity must be a power of two; 64 slots is ample for typical programs. */
#define TRACE_SET_CAP 256
static const char *trace_set[TRACE_SET_CAP];  /* NULL = empty slot */

static void trace_register(const char *name) {
    if (!name || !*name) return;
    unsigned h = 5381;
    for (const char *p = name; *p; p++) h = h * 33 ^ (unsigned char)*p;
    for (int i = 0; i < TRACE_SET_CAP; i++) {
        int slot = (h + i) & (TRACE_SET_CAP - 1);
        if (!trace_set[slot]) { trace_set[slot] = GC_strdup(name); return; }
        if (strcmp(trace_set[slot], name) == 0) return; /* already registered */
    }
}

static void trace_unregister(const char *name) {
    if (!name || !*name) return;
    unsigned h = 5381;
    for (const char *p = name; *p; p++) h = h * 33 ^ (unsigned char)*p;
    for (int i = 0; i < TRACE_SET_CAP; i++) {
        int slot = (h + i) & (TRACE_SET_CAP - 1);
        if (!trace_set[slot]) return;
        if (strcmp(trace_set[slot], name) == 0) { trace_set[slot] = NULL; return; }
    }
}

static int trace_registered(const char *name) {
    if (!name || !*name) return 0;
    unsigned h = 5381;
    for (const char *p = name; *p; p++) h = h * 33 ^ (unsigned char)*p;
    for (int i = 0; i < TRACE_SET_CAP; i++) {
        int slot = (h + i) & (TRACE_SET_CAP - 1);
        if (!trace_set[slot]) return 0;
        if (strcmp(trace_set[slot], name) == 0) return 1;
    }
    return 0;
}

int64_t kw_stcount = 0;

static void mon_send(const char *kind, const char *name, const char *value) {
    if (monitor_fd < 0) return;
    if (!value) value = "";
    struct iovec iov[6];
    iov[0].iov_base = (void*)kind;   iov[0].iov_len = strlen(kind);
    iov[1].iov_base = MON_RS;        iov[1].iov_len = 1;
    iov[2].iov_base = (void*)name;   iov[2].iov_len = strlen(name);
    iov[3].iov_base = MON_US;        iov[3].iov_len = 1;
    iov[4].iov_base = (void*)value;  iov[4].iov_len = strlen(value);
    iov[5].iov_base = MON_RS;        iov[5].iov_len = 1;
    writev(monitor_fd, iov, 6);
    if (monitor_ack_fd >= 0) {
        char ack[1];
        ssize_t r = read(monitor_ack_fd, ack, 1);
        if (r != 1 || ack[0] == 'S') exit(0);
    }
}

void comm_stno(int n) {
    ++kw_stcount;
    if (kw_stlimit >= 0 && kw_stcount > kw_stlimit) {
        fprintf(stderr, "\n** &STLIMIT exceeded at statement %d"
                        " (&STCOUNT=%lld &STLIMIT=%lld)\n",
                n, (long long)kw_stcount, (long long)kw_stlimit);
        exit(1);
    }
}

void comm_var(const char *name, DESCR_t val) {
    if (monitor_fd < 0) return;
    if (!monitor_ready) return;
    if (!name || name[0] == '_') return;
    if (!trace_registered(name)) return;
    const char *s = VARVAL_fn(val);
    mon_send("VALUE", name, s ? s : "(undef)");
}

/* ============================================================
 * Runtime initialization
 * ============================================================ */

/* Global character constants (set by SNO_INIT_fn) */
int64_t kw_fullscan = 0;
int64_t kw_maxlngth = 524288;
int64_t kw_anchor   = 0;
int64_t kw_trim     = 1;
int64_t kw_stlimit  = -1;

char ucase[27]    = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char lcase[27]    = "abcdefghijklmnopqrstuvwxyz";
char digits[11]   = "0123456789";
char alphabet[257];  /* all 256 ASCII chars */

/* ============================================================
 * Numeric comparison builtins: GT LT GE LE EQ NE
 * SNOBOL4 semantics: succeed (return first arg) or fail (FAILDESCR).
 * Also INTEGER, SIZE_fn, REAL type/conversion builtins.
 * ============================================================ */

static DESCR_t _GT_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return gt(a[0], a[1]) ? NULVCL : FAILDESCR;
}
static DESCR_t _LT_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return lt(a[0], a[1]) ? NULVCL : FAILDESCR;
}
static DESCR_t _GE_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return ge(a[0], a[1]) ? NULVCL : FAILDESCR;
}
static DESCR_t _LE_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return le(a[0], a[1]) ? NULVCL : FAILDESCR;
}
static DESCR_t _EQ_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    /* Numeric equality: equal returns first arg, else fail */
    if (a[0].v == DT_I && a[1].v == DT_I)
        return (a[0].i == a[1].i) ? NULVCL : FAILDESCR;
    return (to_real(a[0]) == to_real(a[1])) ? NULVCL : FAILDESCR;
}
static DESCR_t _NE_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    if (a[0].v == DT_I && a[1].v == DT_I)
        return (a[0].i != a[1].i) ? NULVCL : FAILDESCR;
    return (to_real(a[0]) != to_real(a[1])) ? NULVCL : FAILDESCR;
}
/* Arithmetic operator wrappers — registered so APPLY_fn("add",...) works */
static DESCR_t _b_add(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return add(a[0], a[1]);
}
static DESCR_t _b_sub(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return sub(a[0], a[1]);
}
static DESCR_t _b_mul(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return mul(a[0], a[1]);
}
static DESCR_t _b_div(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return DIVIDE_fn(a[0], a[1]);
}
static DESCR_t _b_pow(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    return POWER_fn(a[0], a[1]);
}
static DESCR_t _b_neg(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    return neg(a[0]);
}

static DESCR_t _b_pos(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    return pos(a[0]);
}

static DESCR_t _INTEGER_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    /* Succeed (returning int value) if arg is or converts to integer */
    if (a[0].v == DT_I) return a[0];
    if (a[0].v == DT_S && a[0].s) {
        char *end;
        long long v = strtoll(a[0].s, &end, 10);
        if (end != a[0].s && *end == '\0') return INTVAL(v);
    }
    return FAILDESCR;
}
static DESCR_t _REAL_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    if (a[0].v == DT_R) return a[0];
    if (a[0].v == DT_I)  return (DESCR_t){ .v = DT_R, .r = (double)a[0].i };
    if (a[0].v == DT_S && a[0].s) {
        char *end;
        double v = strtod(a[0].s, &end);
        if (end != a[0].s && *end == '\0') return (DESCR_t){ .v = DT_R, .r = v };
    }
    return FAILDESCR;
}
static DESCR_t _SIZE_(DESCR_t *a, int n) {
    if (n < 1) return INTVAL(0);
    /* Binary string (e.g. &ALPHABET-derived): use slen field directly. */
    if (a[0].v == DT_S && a[0].slen) return INTVAL((int64_t)a[0].slen);
    /* Normal case: convert to string, measure with strlen. */
    const char *s = VARVAL_fn(a[0]);
    return INTVAL((int64_t)(s ? strlen(s) : 0));
}

/* Sprint 23: IDENT, DIFFER, HOST, ENDFILE, APPLY + string builtins as callable */
static DESCR_t _IDENT_(DESCR_t *a, int n) {
    DESCR_t x = (n > 0) ? a[0] : NULVCL;
    DESCR_t y = (n > 1) ? a[1] : NULVCL;
    return ident(x, y) ? NULVCL : FAILDESCR;
}
static DESCR_t _DIFFER_(DESCR_t *a, int n) {
    DESCR_t x = (n > 0) ? a[0] : NULVCL;
    DESCR_t y = (n > 1) ? a[1] : NULVCL;
    return differ(x, y) ? NULVCL : FAILDESCR;
}
/* Lexical string comparators — return first arg on success, FAILDESCR on failure */
static DESCR_t _LGT_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *x = VARVAL_fn(a[0]); const char *y = VARVAL_fn(a[1]);
    if (!x) x = ""; if (!y) y = "";
    return strcmp(x, y) > 0 ? a[0] : FAILDESCR;
}
static DESCR_t _LLT_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *x = VARVAL_fn(a[0]); const char *y = VARVAL_fn(a[1]);
    if (!x) x = ""; if (!y) y = "";
    return strcmp(x, y) < 0 ? a[0] : FAILDESCR;
}
static DESCR_t _LGE_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *x = VARVAL_fn(a[0]); const char *y = VARVAL_fn(a[1]);
    if (!x) x = ""; if (!y) y = "";
    return strcmp(x, y) >= 0 ? a[0] : FAILDESCR;
}
static DESCR_t _LLE_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *x = VARVAL_fn(a[0]); const char *y = VARVAL_fn(a[1]);
    if (!x) x = ""; if (!y) y = "";
    return strcmp(x, y) <= 0 ? a[0] : FAILDESCR;
}
static DESCR_t _LEQ_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *x = VARVAL_fn(a[0]); const char *y = VARVAL_fn(a[1]);
    if (!x) x = ""; if (!y) y = "";
    return strcmp(x, y) == 0 ? a[0] : FAILDESCR;
}
static DESCR_t _LNE_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *x = VARVAL_fn(a[0]); const char *y = VARVAL_fn(a[1]);
    if (!x) x = ""; if (!y) y = "";
    return strcmp(x, y) != 0 ? a[0] : FAILDESCR;
}
static DESCR_t _HOST_(DESCR_t *a, int n) {
    /* HOST(0) = command args string, HOST(1) = PID, HOST(3) = argc */
    /* HOST(4, name) = getenv(name) — used by monitor preamble */
    if (n < 1) return NULVCL;
    int64_t selector = to_int(a[0]);
    if (selector == 0) return STRVAL(GC_strdup(""));
    if (selector == 1) {
        char buf[32]; snprintf(buf, sizeof(buf), "%d", (int)getpid());
        return STRVAL(GC_strdup(buf));
    }
    if (selector == 3) return INTVAL(0);
    if (selector == 4 && n >= 2) {
        const char *envname = VARVAL_fn(a[1]);
        if (!envname || !*envname) return NULVCL;
        const char *val = getenv(envname);
        if (!val) return NULVCL;
        return STRVAL(GC_strdup(val));
    }
    return NULVCL;
}
/* ============================================================
 * Named-channel I/O table
 * Supports INPUT(var,chan,fname) / OUTPUT(var,chan,fname) / ENDFILE(chan)
 * ============================================================ */
#define IO_CHAN_MAX 32
typedef struct {
    FILE  *fp;
    char  *varname;   /* bound variable name, or NULL if unused */
    int    is_output; /* 1=write, 0=read */
    char  *buf;       /* read buffer */
    size_t cap;       /* read buffer capacity */
} io_chan_t;
static io_chan_t _io_chan[IO_CHAN_MAX];
static int _io_chan_init = 0;
static void _io_chan_setup(void) {
    if (_io_chan_init) return;
    memset(_io_chan, 0, sizeof(_io_chan));
    _io_chan_init = 1;
}
static int _io_chan_find_by_var(const char *name) {
    _io_chan_setup();
    for (int i = 0; i < IO_CHAN_MAX; i++)
        if (_io_chan[i].varname && strcmp(_io_chan[i].varname, name) == 0) return i;
    return -1;
}
static void _io_chan_close(int ch) {
    _io_chan_setup();
    if (ch < 0 || ch >= IO_CHAN_MAX) return;
    if (_io_chan[ch].fp) { fclose(_io_chan[ch].fp); _io_chan[ch].fp = NULL; }
    if (_io_chan[ch].varname) { free(_io_chan[ch].varname); _io_chan[ch].varname = NULL; }
    if (_io_chan[ch].buf)  { free(_io_chan[ch].buf); _io_chan[ch].buf = NULL; }
    _io_chan[ch].cap = 0;
    _io_chan[ch].is_output = 0;
}

static DESCR_t _ENDFILE_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    int ch = (int)a[0].i;
    if (ch >= 0 && ch < IO_CHAN_MAX) _io_chan_close(ch);
    return NULVCL;
}
static DESCR_t _APPLY_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    const char *fname = VARVAL_fn(a[0]);
    return APPLY_fn(fname, a + 1, n - 1);
}
static DESCR_t _ARG_(DESCR_t *a, int n);    /* defined after FNCBLK_t */
static DESCR_t _LOCAL_(DESCR_t *a, int n);  /* defined after FNCBLK_t */
static DESCR_t _LPAD_(DESCR_t *a, int n) {
    if (n < 2) return n > 0 ? a[0] : NULVCL;
    return lpad_fn(a[0], a[1], n > 2 ? a[2] : STRVAL(" "));
}
static DESCR_t _RPAD_(DESCR_t *a, int n) {
    if (n < 2) return n > 0 ? a[0] : NULVCL;
    return rpad_fn(a[0], a[1], n > 2 ? a[2] : STRVAL(" "));
}
static DESCR_t _CHAR_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return BCHAR_fn(a[0]);
}
static DESCR_t _DUPL_(DESCR_t *a, int n) {
    if (n < 2) return NULVCL;
    return DUPL_fn(a[0], a[1]);
}
static DESCR_t _REMDR_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    int64_t x = to_int(a[0]), y = to_int(a[1]);
    if (y == 0) return FAILDESCR;
    return INTVAL(x % y);
}
static DESCR_t _REPLACE_(DESCR_t *a, int n) {
    if (n < 3) return NULVCL;
    return REPLACE_fn(a[0], a[1], a[2]);
}
static DESCR_t _TRIM_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return TRIM_fn(a[0]);
}
static DESCR_t _SUBSTR_(DESCR_t *a, int n) {
    if (n < 2) return NULVCL;
    if (n < 3) {
        /* 2-arg SUBSTR(s, i): from position i to end of string */
        DESCR_t big = { .v = DT_I, .slen = 0, .i = 999999999 };
        return SUBSTR_fn(a[0], a[1], big);
    }
    return SUBSTR_fn(a[0], a[1], a[2]);
}
static DESCR_t _REVERSE_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return REVERS_fn(a[0]);
}
static DESCR_t _DATATYPE_(DESCR_t *a, int n) {
    if (n < 1) return STRVAL("STRING");
    return STRVAL((char*)datatype(a[0]));
}
static DESCR_t _LCASE_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    const char *s = VARVAL_fn(a[0]);
    if (!s) return NULVCL;
    char *r = GC_strdup(s);
    for (int i = 0; r[i]; i++) r[i] = (char)tolower((unsigned char)r[i]);
    return STRVAL(r);
}
static DESCR_t _UCASE__fn(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    const char *s = VARVAL_fn(a[0]);
    if (!s) return NULVCL;
    char *r = GC_strdup(s);
    for (int i = 0; r[i]; i++) r[i] = (char)toupper((unsigned char)r[i]);
    return STRVAL(r);
}

/* EVAL / CODE / OPSYN / SORT wrappers — file scope required */
extern DESCR_t EVAL_fn(DESCR_t);
extern DESCR_t code(const char *src);
extern DESCR_t opsyn(DESCR_t, DESCR_t, DESCR_t);
extern DESCR_t sort_fn(DESCR_t);
static DESCR_t _EVAL_(DESCR_t *a, int n)  { return EVAL_fn(n>0?a[0]:NULVCL); }
static DESCR_t _CODE_(DESCR_t *a, int n)  { return code(n>0?VARVAL_fn(a[0]):""); }
static DESCR_t _OPSYN_(DESCR_t *a, int n) {
    return opsyn(n>0?a[0]:NULVCL,n>1?a[1]:NULVCL,n>2?a[2]:NULVCL); }
static DESCR_t _SORT_(DESCR_t *a, int n)  { return sort_fn(n>0?a[0]:NULVCL); }
static DESCR_t _INPUT_(DESCR_t *a, int n);   /* defined near input_read below */
static DESCR_t _OUTPUT_(DESCR_t *a, int n);  /* defined near input_read below */

/* ARRAY(n) or ARRAY('lo:hi') or ARRAY('lo:hi,lo2:hi2') */
static DESCR_t _ARRAY_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    const char *proto = VARVAL_fn(a[0]);
    if (proto && strchr(proto, ':')) {
        /* Parse "lo:hi" or "lo:hi,lo2:hi2" */
        int lo = 1, hi = 1, lo2 = 1, hi2 = 1;
        const char *comma = strchr(proto, ',');
        if (comma) {
            sscanf(proto,  "%d:%d", &lo, &hi);
            sscanf(comma+1, "%d:%d", &lo2, &hi2);
            return ARRAY_VAL(array_new2d(lo, hi, lo2, hi2));
        }
        sscanf(proto, "%d:%d", &lo, &hi);
        return ARRAY_VAL(array_new(lo, hi));
    }
    if (proto && strchr(proto, ',')) {
        /* Parse "R,C" or "R,C,..." — multi-dim without explicit bounds.
         * Only 2D supported in runtime; first two dimensions used. */
        int r = 1, c = 1;
        sscanf(proto, "%d,%d", &r, &c);
        if (r < 1) r = 1;
        if (c < 1) c = 1;
        return ARRAY_VAL(array_new2d(1, r, 1, c));
    }
    int sz = (int)to_int(a[0]);
    if (sz < 1) return FAILDESCR;
    ARBLK_t *arr = array_new(1, sz);
    /* Optional second arg: initial fill value */
    if (n >= 2) {
        for (int i = 0; i < sz; i++) arr->data[i] = a[1];
    }
    return ARRAY_VAL(arr);
}

/* TABLE(initial_size, increment) — both args optional */
static DESCR_t _TABLE_(DESCR_t *a, int n) {
    (void)a; (void)n;
    return TABLE_VAL(table_new());
}

/* CONVERT(val, type) */
static DESCR_t _CONVERT_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    DESCR_t val  = a[0];
    const char *type = VARVAL_fn(a[1]);
    if (!type) return FAILDESCR;
    if (strcasecmp(type, "STRING")  == 0) { const char *s = VARVAL_fn(val); return s ? STRVAL(s) : NULVCL; }
    if (strcasecmp(type, "INTEGER") == 0) return INTVAL((int64_t)to_int(val));
    if (strcasecmp(type, "REAL")    == 0) return (DESCR_t){ .v = DT_R, .r = to_real(val) };
    if (strcasecmp(type, "ARRAY")   == 0) {
        if (val.v == DT_A) return val;
        return FAILDESCR;
    }
    return FAILDESCR;
}

/* COPY(array_or_table) — shallow copy */
static DESCR_t _COPY_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    DESCR_t v = a[0];
    if (v.v == DT_A) {
        int sz = v.arr->hi - v.arr->lo + 1;
        ARBLK_t *copy = array_new(v.arr->lo, v.arr->hi);
        for (int i = 0; i < sz; i++) copy->data[i] = v.arr->data[i];
        return ARRAY_VAL(copy);
    }
    return FAILDESCR;
}

/* Sprint 23: counter stack and tree field accessors as callable DESCR_t functions */
static DESCR_t _b_nPush(DESCR_t *a, int n) {
    (void)a; (void)n;
    NPUSH_fn();
    return NULVCL;
}
static DESCR_t _b_nInc(DESCR_t *a, int n) {
    (void)a; (void)n;
    NINC_fn();
    return INTVAL(ntop());
}
static DESCR_t _b_nDec(DESCR_t *a, int n) {
    (void)a; (void)n;
    NDEC_fn();
    return INTVAL(ntop());
}
static DESCR_t _b_nTop(DESCR_t *a, int n) {
    (void)a; (void)n;
    return INTVAL(ntop());
}
static DESCR_t _b_nPop(DESCR_t *a, int n) {
    (void)a; (void)n;
    int64_t val = ntop();
    NPOP_fn();
    return INTVAL(val);
}
/* TREEBLK_t field accessors: n(x), t(x), v(x), c(x) */
static DESCR_t _b_tree_n(DESCR_t *a, int n) {
    if (n < 1) return INTVAL(0);
    return FIELD_GET_fn(a[0], "n");
}
static DESCR_t _b_tree_t(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return FIELD_GET_fn(a[0], "t");
}
static DESCR_t _b_tree_v(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return FIELD_GET_fn(a[0], "v");
}
static DESCR_t _b_tree_c(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return FIELD_GET_fn(a[0], "c");
}
/* link_counter / link_tag field accessors: value(x), next(x) */
static DESCR_t _b_field_value(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return FIELD_GET_fn(a[0], "value");
}
static DESCR_t _b_field_next(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    return FIELD_GET_fn(a[0], "next");
}

/* DUMP builtin — dump all variables to stderr (implementation after var table) */
static void var_dump(void);
static DESCR_t _DUMP_(DESCR_t *a, int n) {
    (void)a; (void)n;
    var_dump();
    return NULVCL;
}

/* TRACE(varname, type [, label, fn]) — register variable for VALUE tracing.
 * Only 'VALUE' type supported; other types accepted but silently ignored.
 * Returns varname on success (SNOBOL4 spec). */
static DESCR_t _TRACE_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    const char *varname = VARVAL_fn(a[0]);
    if (!varname || !*varname) return FAILDESCR;
    /* arg[1] = type string; default to 'VALUE' if omitted */
    const char *type = (n >= 2) ? VARVAL_fn(a[1]) : "VALUE";
    if (type && (strcmp(type,"VALUE")==0 || strcmp(type,"value")==0))
        trace_register(varname);
    /* return the variable name (SNOBOL4 spec: TRACE returns first arg) */
    return STRVAL(GC_strdup(varname));
}

/* STOPTR(varname [, type]) — remove variable from trace set.
 * Returns varname on success. */
static DESCR_t _STOPTR_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    const char *varname = VARVAL_fn(a[0]);
    if (!varname || !*varname) return FAILDESCR;
    trace_unregister(varname);
    return STRVAL(GC_strdup(varname));
}

/* Forward declarations needed by _DATA_ trampolines */
static DATBLK_t *_udef_lookup(const char *name);

/* ---- DT_DATA() builtin ----
 * DT_DATA('typename(field1,field2,...)') — define a user-defined datatype.
 * Registers: constructor typename(f1,f2,...) and field accessors f1(obj),f2(obj),...
 * Uses GC-allocated closure structs so each registered fn knows its type/field name.
 */
typedef struct { char *typename; int nfields; char **fields; } DataClosure;
typedef struct { char *typename; char *fieldname; } FieldClosure;

/* Dynamic constructor: typename(v1, v2, ...) -> DT_DATA */
static DESCR_t _data_ctor_fn(DESCR_t *args, int nargs) {
    /* Called as a registered FNCPTR_t; the closure is stored in a parallel table.
     * We use apply_closure which is not available, so we look up via type name. */
    /* NOTE: This fn is never called directly — see _DATA_ registration below */
    (void)args; (void)nargs;
    return NULVCL;
}

/* We need closures per-type. Use a static array of up to 64 DT_DATA types. */
#define DATA_MAX_TYPES 64
#define DATA_MAX_FIELDS 16
static struct {
    char *typename;
    int   nfields;
    char *fields[DATA_MAX_FIELDS];
} _data_types[DATA_MAX_TYPES];
static int _data_ntypes = 0;

/* Generic constructor: looks up typename by position in _data_types,
 * builds a DATINST_t with the provided args. */
static DESCR_t _make_ctor(int tidx, DESCR_t *args, int nargs) {
    if (tidx < 0 || tidx >= _data_ntypes) return NULVCL;
    DATBLK_t *t = _udef_lookup(_data_types[tidx].typename);
    if (!t) return NULVCL;
    DATINST_t *u = GC_malloc(sizeof(DATINST_t));
    u->type   = t;
    u->fields = GC_malloc(t->nfields * sizeof(DESCR_t));
    for (int i = 0; i < t->nfields; i++)
        u->fields[i] = (i < nargs) ? args[i] : NULVCL;
    return (DESCR_t){ .v = DT_DATA, .u = u };
}

/* One constructor trampoline per slot (up to DATA_MAX_TYPES) */
#define CTOR_FN(idx) \
static DESCR_t _ctor_##idx(DESCR_t *a, int n) { return _make_ctor(idx, a, n); }

CTOR_FN(0)  CTOR_FN(1)  CTOR_FN(2)  CTOR_FN(3)
CTOR_FN(4)  CTOR_FN(5)  CTOR_FN(6)  CTOR_FN(7)
CTOR_FN(8)  CTOR_FN(9)  CTOR_FN(10) CTOR_FN(11)
CTOR_FN(12) CTOR_FN(13) CTOR_FN(14) CTOR_FN(15)
CTOR_FN(16) CTOR_FN(17) CTOR_FN(18) CTOR_FN(19)
CTOR_FN(20) CTOR_FN(21) CTOR_FN(22) CTOR_FN(23)
CTOR_FN(24) CTOR_FN(25) CTOR_FN(26) CTOR_FN(27)
CTOR_FN(28) CTOR_FN(29) CTOR_FN(30) CTOR_FN(31)
CTOR_FN(32) CTOR_FN(33) CTOR_FN(34) CTOR_FN(35)
CTOR_FN(36) CTOR_FN(37) CTOR_FN(38) CTOR_FN(39)
CTOR_FN(40) CTOR_FN(41) CTOR_FN(42) CTOR_FN(43)
CTOR_FN(44) CTOR_FN(45) CTOR_FN(46) CTOR_FN(47)
CTOR_FN(48) CTOR_FN(49) CTOR_FN(50) CTOR_FN(51)
CTOR_FN(52) CTOR_FN(53) CTOR_FN(54) CTOR_FN(55)
CTOR_FN(56) CTOR_FN(57) CTOR_FN(58) CTOR_FN(59)
CTOR_FN(60) CTOR_FN(61) CTOR_FN(62) CTOR_FN(63)

static DESCR_t (*_ctor_fns[DATA_MAX_TYPES])(DESCR_t *, int) = {
    _ctor_0,  _ctor_1,  _ctor_2,  _ctor_3,
    _ctor_4,  _ctor_5,  _ctor_6,  _ctor_7,
    _ctor_8,  _ctor_9,  _ctor_10, _ctor_11,
    _ctor_12, _ctor_13, _ctor_14, _ctor_15,
    _ctor_16, _ctor_17, _ctor_18, _ctor_19,
    _ctor_20, _ctor_21, _ctor_22, _ctor_23,
    _ctor_24, _ctor_25, _ctor_26, _ctor_27,
    _ctor_28, _ctor_29, _ctor_30, _ctor_31,
    _ctor_32, _ctor_33, _ctor_34, _ctor_35,
    _ctor_36, _ctor_37, _ctor_38, _ctor_39,
    _ctor_40, _ctor_41, _ctor_42, _ctor_43,
    _ctor_44, _ctor_45, _ctor_46, _ctor_47,
    _ctor_48, _ctor_49, _ctor_50, _ctor_51,
    _ctor_52, _ctor_53, _ctor_54, _ctor_55,
    _ctor_56, _ctor_57, _ctor_58, _ctor_59,
    _ctor_60, _ctor_61, _ctor_62, _ctor_63,
};

/* Field accessor trampolines: one per (type,field) slot */
#define FIELD_ACCESSOR_MAX (DATA_MAX_TYPES * DATA_MAX_FIELDS)
static struct { int tidx; int fidx; } _facc_slots[FIELD_ACCESSOR_MAX];
static int _facc_n = 0;

static DESCR_t _make_fget(int slot, DESCR_t obj) {
    if (slot < 0 || slot >= _facc_n) return FAILDESCR;
    int tidx = _facc_slots[slot].tidx;
    int fidx = _facc_slots[slot].fidx;
    if (obj.v != DT_DATA || !obj.u) return FAILDESCR;  /* error 041: wrong datatype */
    if (fidx < 0 || fidx >= obj.u->type->nfields) return FAILDESCR;
    return obj.u->fields[fidx];
}
static void _make_fset(int slot, DESCR_t obj, DESCR_t val) {
    if (slot < 0 || slot >= _facc_n) return;
    int fidx = _facc_slots[slot].fidx;
    if (obj.v != DT_DATA || !obj.u) return;
    if (fidx < 0 || fidx >= obj.u->type->nfields) return;
    obj.u->fields[fidx] = val;
}

#define FACC_FN(idx) \
static DESCR_t _facc_get_##idx(DESCR_t *a, int n) { \
    return n>=1 ? _make_fget(idx, a[0]) : NULVCL; }

FACC_FN(0)   FACC_FN(1)   FACC_FN(2)   FACC_FN(3)
FACC_FN(4)   FACC_FN(5)   FACC_FN(6)   FACC_FN(7)
FACC_FN(8)   FACC_FN(9)   FACC_FN(10)  FACC_FN(11)
FACC_FN(12)  FACC_FN(13)  FACC_FN(14)  FACC_FN(15)
FACC_FN(16)  FACC_FN(17)  FACC_FN(18)  FACC_FN(19)
FACC_FN(20)  FACC_FN(21)  FACC_FN(22)  FACC_FN(23)
FACC_FN(24)  FACC_FN(25)  FACC_FN(26)  FACC_FN(27)
FACC_FN(28)  FACC_FN(29)  FACC_FN(30)  FACC_FN(31)
FACC_FN(32)  FACC_FN(33)  FACC_FN(34)  FACC_FN(35)
FACC_FN(36)  FACC_FN(37)  FACC_FN(38)  FACC_FN(39)
FACC_FN(40)  FACC_FN(41)  FACC_FN(42)  FACC_FN(43)
FACC_FN(44)  FACC_FN(45)  FACC_FN(46)  FACC_FN(47)
FACC_FN(48)  FACC_FN(49)  FACC_FN(50)  FACC_FN(51)
FACC_FN(52)  FACC_FN(53)  FACC_FN(54)  FACC_FN(55)
FACC_FN(56)  FACC_FN(57)  FACC_FN(58)  FACC_FN(59)
FACC_FN(60)  FACC_FN(61)  FACC_FN(62)  FACC_FN(63)
FACC_FN(64)  FACC_FN(65)  FACC_FN(66)  FACC_FN(67)
FACC_FN(68)  FACC_FN(69)  FACC_FN(70)  FACC_FN(71)
FACC_FN(72)  FACC_FN(73)  FACC_FN(74)  FACC_FN(75)
FACC_FN(76)  FACC_FN(77)  FACC_FN(78)  FACC_FN(79)
FACC_FN(80)  FACC_FN(81)  FACC_FN(82)  FACC_FN(83)
FACC_FN(84)  FACC_FN(85)  FACC_FN(86)  FACC_FN(87)
FACC_FN(88)  FACC_FN(89)  FACC_FN(90)  FACC_FN(91)
FACC_FN(92)  FACC_FN(93)  FACC_FN(94)  FACC_FN(95)
FACC_FN(96)  FACC_FN(97)  FACC_FN(98)  FACC_FN(99)
FACC_FN(100) FACC_FN(101) FACC_FN(102) FACC_FN(103)
FACC_FN(104) FACC_FN(105) FACC_FN(106) FACC_FN(107)
FACC_FN(108) FACC_FN(109) FACC_FN(110) FACC_FN(111)
FACC_FN(112) FACC_FN(113) FACC_FN(114) FACC_FN(115)
FACC_FN(116) FACC_FN(117) FACC_FN(118) FACC_FN(119)
FACC_FN(120) FACC_FN(121) FACC_FN(122) FACC_FN(123)
FACC_FN(124) FACC_FN(125) FACC_FN(126) FACC_FN(127)

static DESCR_t (*_facc_fns[FIELD_ACCESSOR_MAX])(DESCR_t *, int) = {
    _facc_get_0,   _facc_get_1,   _facc_get_2,   _facc_get_3,
    _facc_get_4,   _facc_get_5,   _facc_get_6,   _facc_get_7,
    _facc_get_8,   _facc_get_9,   _facc_get_10,  _facc_get_11,
    _facc_get_12,  _facc_get_13,  _facc_get_14,  _facc_get_15,
    _facc_get_16,  _facc_get_17,  _facc_get_18,  _facc_get_19,
    _facc_get_20,  _facc_get_21,  _facc_get_22,  _facc_get_23,
    _facc_get_24,  _facc_get_25,  _facc_get_26,  _facc_get_27,
    _facc_get_28,  _facc_get_29,  _facc_get_30,  _facc_get_31,
    _facc_get_32,  _facc_get_33,  _facc_get_34,  _facc_get_35,
    _facc_get_36,  _facc_get_37,  _facc_get_38,  _facc_get_39,
    _facc_get_40,  _facc_get_41,  _facc_get_42,  _facc_get_43,
    _facc_get_44,  _facc_get_45,  _facc_get_46,  _facc_get_47,
    _facc_get_48,  _facc_get_49,  _facc_get_50,  _facc_get_51,
    _facc_get_52,  _facc_get_53,  _facc_get_54,  _facc_get_55,
    _facc_get_56,  _facc_get_57,  _facc_get_58,  _facc_get_59,
    _facc_get_60,  _facc_get_61,  _facc_get_62,  _facc_get_63,
    _facc_get_64,  _facc_get_65,  _facc_get_66,  _facc_get_67,
    _facc_get_68,  _facc_get_69,  _facc_get_70,  _facc_get_71,
    _facc_get_72,  _facc_get_73,  _facc_get_74,  _facc_get_75,
    _facc_get_76,  _facc_get_77,  _facc_get_78,  _facc_get_79,
    _facc_get_80,  _facc_get_81,  _facc_get_82,  _facc_get_83,
    _facc_get_84,  _facc_get_85,  _facc_get_86,  _facc_get_87,
    _facc_get_88,  _facc_get_89,  _facc_get_90,  _facc_get_91,
    _facc_get_92,  _facc_get_93,  _facc_get_94,  _facc_get_95,
    _facc_get_96,  _facc_get_97,  _facc_get_98,  _facc_get_99,
    _facc_get_100, _facc_get_101, _facc_get_102, _facc_get_103,
    _facc_get_104, _facc_get_105, _facc_get_106, _facc_get_107,
    _facc_get_108, _facc_get_109, _facc_get_110, _facc_get_111,
    _facc_get_112, _facc_get_113, _facc_get_114, _facc_get_115,
    _facc_get_116, _facc_get_117, _facc_get_118, _facc_get_119,
    _facc_get_120, _facc_get_121, _facc_get_122, _facc_get_123,
    _facc_get_124, _facc_get_125, _facc_get_126, _facc_get_127,
};

static int fn_has_builtin(const char *name);  /* forward — defined after FNCBLK_t */
static void _func_init(void);  /* forward */
static DESCR_t _DATA_(DESCR_t *a, int n) {
    if (n < 1) return NULVCL;
    const char *spec = VARVAL_fn(a[0]);
    if (!spec || !*spec) return NULVCL;

    /* Register the type in snobol4's DATBLK_t list */
    DEFDAT_fn(spec);

    /* Parse spec to get typename and fields */
    char *s = GC_strdup(spec);
    char *paren = strchr(s, '(');
    if (!paren) return NULVCL;
    *paren = '\0';
    char *tname = s;
    char *fstr = paren + 1;
    char *close = strchr(fstr, ')');
    if (close) *close = '\0';

    if (_data_ntypes >= DATA_MAX_TYPES) return NULVCL;
    int tidx = _data_ntypes++;
    /* CSNOBOL4 returns DATATYPE as uppercase — uppercase the typename now */
    char *uname = GC_strdup(tname);
    for (char *p = uname; *p; p++) *p = (char)toupper((unsigned char)*p);
    _data_types[tidx].typename = uname;

    /* Parse fields */
    int nf = 0;
    char *tmp = GC_strdup(fstr);
    char *tok = strtok(tmp, ",");
    while (tok && nf < DATA_MAX_FIELDS) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        _data_types[tidx].fields[nf] = GC_strdup(tok);
        nf++;
        tok = strtok(NULL, ",");
    }
    _data_types[tidx].nfields = nf;

    /* Register constructor: typename(f1,f2,...) */
    extern void register_fn(const char *, DESCR_t (*)(DESCR_t*, int), int, int);
    register_fn(tname, _ctor_fns[tidx], 0, nf);

    /* Register field accessors: each field name as a 1-arg function */
    for (int fi = 0; fi < nf; fi++) {
        if (_facc_n >= FIELD_ACCESSOR_MAX) break;
        int slot = _facc_n++;
        _facc_slots[slot].tidx = tidx;
        _facc_slots[slot].fidx = fi;
        /* Register field accessor, but never overwrite a C-backed builtin.
         * fn_has_builtin() checks the hash table before FNCBLK_t is visible here;
         * it is defined later in this file and forward-declared below. */
        const char *fname = _data_types[tidx].fields[fi];
        register_fn(fname, _facc_fns[slot], 1, 1);
    }

    return NULVCL;
}

/* Pattern builtins callable via APPLY_fn() — used when SPAN/BREAK/etc appear
 * inside argument lists and are tokenised as IDENT rather than PAT_BUILTIN. */
extern DESCR_t pat_span(const char *);
extern DESCR_t pat_break_(const char *);
extern DESCR_t pat_breakx(const char *);
extern DESCR_t pat_any_cs(const char *);
extern DESCR_t pat_notany(const char *);
extern DESCR_t pat_len(int64_t);
extern DESCR_t pat_pos(int64_t);
extern DESCR_t pat_rpos(int64_t);
extern DESCR_t pat_tab(int64_t);
extern DESCR_t pat_rtab(int64_t);
extern DESCR_t pat_arb(void);
extern DESCR_t pat_rem(void);
extern DESCR_t pat_fail(void);
extern DESCR_t pat_abort(void);
extern DESCR_t pat_succeed(void);
extern DESCR_t pat_bal(void);
extern DESCR_t pat_arbno(DESCR_t);
extern DESCR_t pat_fence(void);
extern DESCR_t pat_fence_p(DESCR_t);

static DESCR_t _PAT_SPAN_(DESCR_t *a, int n)    { return n>=1 ? pat_span(VARVAL_fn(a[0]))    : FAILDESCR; }
static DESCR_t _PAT_BREAK_(DESCR_t *a, int n)   { return n>=1 ? pat_break_(VARVAL_fn(a[0]))  : FAILDESCR; }
static DESCR_t _PAT_BREAKX_(DESCR_t *a, int n)  { return n>=1 ? pat_breakx(VARVAL_fn(a[0]))  : FAILDESCR; }
static DESCR_t _PAT_ANY_(DESCR_t *a, int n)     { return n>=1 ? pat_any_cs(VARVAL_fn(a[0]))  : FAILDESCR; }
static DESCR_t _PAT_NOTANY_(DESCR_t *a, int n)  { return n>=1 ? pat_notany(VARVAL_fn(a[0]))  : FAILDESCR; }
static DESCR_t _PAT_LEN_(DESCR_t *a, int n)     { return n>=1 ? pat_len(to_int(a[0]))   : FAILDESCR; }
static DESCR_t _PAT_POS_(DESCR_t *a, int n)     { return n>=1 ? pat_pos(to_int(a[0]))   : FAILDESCR; }
static DESCR_t _PAT_RPOS_(DESCR_t *a, int n)    { return n>=1 ? pat_rpos(to_int(a[0]))  : FAILDESCR; }
static DESCR_t _PAT_TAB_(DESCR_t *a, int n)     { return n>=1 ? pat_tab(to_int(a[0]))   : FAILDESCR; }
static DESCR_t _PAT_RTAB_(DESCR_t *a, int n)    { return n>=1 ? pat_rtab(to_int(a[0]))  : FAILDESCR; }
static DESCR_t _PAT_ARB_(DESCR_t *a, int n)     { (void)a;(void)n; return pat_arb();     }
static DESCR_t _PAT_REM_(DESCR_t *a, int n)     { (void)a;(void)n; return pat_rem();     }
static DESCR_t _PAT_FAIL_(DESCR_t *a, int n)    { (void)a;(void)n; return pat_fail();    }
static DESCR_t _PAT_ABORT_(DESCR_t *a, int n)   { (void)a;(void)n; return pat_abort();   }
static DESCR_t _PAT_SUCCEED_(DESCR_t *a, int n) { (void)a;(void)n; return pat_succeed(); }
static DESCR_t _PAT_BAL_(DESCR_t *a, int n)     { (void)a;(void)n; return pat_bal();     }
static DESCR_t _PAT_ARBNO_(DESCR_t *a, int n)   { return n>=1 ? pat_arbno(a[0])  : FAILDESCR; }
static DESCR_t _PAT_FENCE_(DESCR_t *a, int n)   { return n>=1 ? pat_fence_p(a[0]) : pat_fence(); }
static DESCR_t _PAT_ALT_(DESCR_t *a, int n)     { return n>=2 ? pat_alt(a[0], a[1])  : (n>=1 ? a[0] : FAILDESCR); }
static DESCR_t _PAT_CONCAT_(DESCR_t *a, int n)  { return n>=2 ? pat_cat(a[0], a[1])  : (n>=1 ? a[0] : FAILDESCR); }

/* PROTOTYPE(array_or_table) — returns dimension string e.g. "1:3" or "1:3,1:2" */
static DESCR_t _PROTOTYPE_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    DESCR_t v = a[0];
    if (v.v == DT_A && v.arr) {
        ARBLK_t *arr = v.arr;
        char buf[128];
        if (arr->ndim > 1) {
            /* 2D: ndim repurposed as cols count; rows span lo..hi.
             * lo2 is not stored separately — only cols=hi2-lo2+1.
             * SPITBOL returns "R,C" for standard 1-based arrays,
             * "lo:hi,C" only when row lo != 1 (col lo always 1). */
            int rows = arr->hi - arr->lo + 1;
            int cols = arr->ndim;  /* stored as cols count */
            if (arr->lo == 1)
                snprintf(buf, sizeof(buf), "%d,%d", rows, cols);
            else
                snprintf(buf, sizeof(buf), "%d:%d,%d",
                         arr->lo, arr->hi, cols);
        } else {
            /* 1D: SPITBOL returns "N" for standard 1-based arrays,
             * "lo:hi" only when lo != 1 */
            if (arr->lo == 1)
                snprintf(buf, sizeof(buf), "%d", arr->hi);
            else
                snprintf(buf, sizeof(buf), "%d:%d", arr->lo, arr->hi);
        }
        return STRVAL(GC_strdup(buf));
    }
    if (v.v == DT_T) {
        /* TABLE prototype returns empty string per SNOBOL4 */
        return STRVAL("");
    }
    return FAILDESCR;
}

/* ITEM(arr, i1 [, i2, ...]) — programmatic subscript, equivalent to arr<i1,i2,...> */
static DESCR_t _ITEM_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    DESCR_t arr = a[0];
    if (arr.v == DT_T) {
        const char *k = VARVAL_fn(a[1]);
        return table_get(arr.tbl, k ? k : "");
    }
    if (arr.v == DT_A) {
        int i = (int)to_int(a[1]);
        if (n == 2) return array_get(arr.arr, i);
        int j = (int)to_int(a[2]);
        return array_get2(arr.arr, i, j);
    }
    return FAILDESCR;
}

/* VALUE(varname) — returns current value of named variable */
static DESCR_t _VALUE_(DESCR_t *a, int n) {
    if (n < 1) return FAILDESCR;
    const char *name = VARVAL_fn(a[0]);
    if (!name) return FAILDESCR;
    return NV_GET_fn(name);
}

void SNO_INIT_fn(void) {
    GC_INIT();
    /* Build &ALPHABET: all 256 chars in order */
    for (int i = 0; i < 256; i++) alphabet[i] = (char)i;
    alphabet[256] = '\0';
    /* Register as NV keyword — pointer identity used by SIZE for correct length */
    NV_SET_fn("ALPHABET", BSTRVAL(alphabet, 256));
    /* Enable monitor — prefer named FIFO (MONITOR_READY_PIPE env var) over stderr.
     * MONITOR_READY_PIPE=/path/to/fifo  → open FIFO, write trace events there (no stderr pollution)
     * MONITOR=1 (legacy)          → write trace events to stderr (fd 2)
     */
    const char *mon_fifo = getenv("MONITOR_READY_PIPE");
    if (mon_fifo && mon_fifo[0]) {
        monitor_fd = open(mon_fifo, O_WRONLY | O_NONBLOCK);
        if (monitor_fd < 0) monitor_fd = open(mon_fifo, O_WRONLY);
        /* sync-step go pipe */
        const char *go_pipe = getenv("MONITOR_GO_PIPE");
        if (go_pipe && go_pipe[0])
            monitor_ack_fd = open(go_pipe, O_RDONLY);
    } else {
        const char *mon = getenv("MONITOR");
        if (mon && mon[0] == '1') monitor_fd = 2;
    }

    /* Register numeric comparison builtins */
    extern void register_fn(const char *, DESCR_t (*)(DESCR_t*, int), int, int);
    register_fn("GT",       _GT_,       2, 2);
    register_fn("LT",       _LT_,       2, 2);
    register_fn("GE",       _GE_,       2, 2);
    register_fn("LE",       _LE_,       2, 2);
    register_fn("EQ",       _EQ_,       2, 2);
    register_fn("NE",       _NE_,       2, 2);
    /* Arithmetic operators — registered so APPLY_fn("add",...) works */
    register_fn("add",      _b_add,      2, 2);
    register_fn("sub",      _b_sub,      2, 2);
    register_fn("mul",      _b_mul,      2, 2);
    register_fn("DIVIDE_fn",_b_div,      2, 2);
    register_fn("POWER_fn", _b_pow,      2, 2);
    register_fn("neg",      _b_neg,      1, 1);
    register_fn("__num_pos", _b_pos,      1, 1);
    register_fn("INTEGER",  _INTEGER_,  1, 1);
    register_fn("REAL",     _REAL_,     1, 1);
    register_fn("SIZE",        _SIZE_,     1, 1);
    /* Sprint 23: string predicates and host interface */
    register_fn("IDENT",    _IDENT_,    0, 2);
    register_fn("DIFFER",   _DIFFER_,   0, 2);
    register_fn("LGT",      _LGT_,      2, 2);
    register_fn("LLT",      _LLT_,      2, 2);
    register_fn("LGE",      _LGE_,      2, 2);
    register_fn("LLE",      _LLE_,      2, 2);
    register_fn("LEQ",      _LEQ_,      2, 2);
    register_fn("LNE",      _LNE_,      2, 2);
    register_fn("HOST",     _HOST_,     1, 4);
    register_fn("ENDFILE",  _ENDFILE_,  1, 1);
    register_fn("APPLY",    _APPLY_,    1, 9);
    register_fn("LPAD",     _LPAD_,     2, 3);
    register_fn("RPAD",     _RPAD_,     2, 3);
    register_fn("CHAR",     _CHAR_,     1, 1);
    register_fn("DUPL",        _DUPL_,     2, 2);
    register_fn("REPLACE",  _REPLACE_,  3, 3);
    register_fn("REMDR",    _REMDR_,    2, 2);
    register_fn("TRIM",        _TRIM_,     1, 1);
    register_fn("SUBSTR",      _SUBSTR_,   2, 3);
    register_fn("REVERSE",  _REVERSE_,  1, 1);
    register_fn("DATATYPE", _DATATYPE_, 1, 1);
    register_fn("LCASE",    _LCASE_,    1, 1);
    register_fn("UCASE",    _UCASE__fn, 1, 1);
    register_fn("DATA",        _DATA_,     1, 1);
    register_fn("ARRAY",   _ARRAY_,   1, 2);
    register_fn("TABLE",   _TABLE_,   0, 2);
    register_fn("CONVERT", _CONVERT_, 2, 2);
    register_fn("PROTOTYPE", _PROTOTYPE_, 1, 1);
    register_fn("ITEM",    _ITEM_,    2, 9);
    register_fn("VALUE",   _VALUE_,   1, 1);
    register_fn("COPY",    _COPY_,    1, 1);
    register_fn("EVAL",  _EVAL_,  1, 1);
    register_fn("CODE",  _CODE_,  1, 1);
    register_fn("OPSYN", _OPSYN_, 2, 3);
    register_fn("ARG",   _ARG_,   2, 2);
    register_fn("LOCAL", _LOCAL_, 2, 2);
    register_fn("SORT",  _SORT_,  1, 1);
    register_fn("INPUT",  _INPUT_,  1, 4);
    register_fn("OUTPUT", _OUTPUT_, 1, 4);
    register_fn("nPush",    _b_nPush,    0, 0);
    register_fn("nInc",     _b_nInc,     0, 0);
    register_fn("nDec",     _b_nDec,     0, 0);
    register_fn("nTop",     _b_nTop,     0, 0);
    register_fn("nPop",     _b_nPop,     0, 0);
    register_fn("n",        _b_tree_n,      1, 1);
    register_fn("t",        _b_tree_t,      1, 1);
    register_fn("v",        _b_tree_v,      1, 1);
    register_fn("c",        _b_tree_c,      1, 1);


    register_fn("DUMP",     _DUMP_,        0, 1);
    register_fn("TRACE",    _TRACE_,       1, 4);
    register_fn("STOPTR",   _STOPTR_,      1, 2);
    /* Pattern builtins callable via APPLY_fn (when inside arglist parens) */
    register_fn("SPAN",    _PAT_SPAN_,    1, 1);
    register_fn("BREAK",   _PAT_BREAK_,   1, 1);
    register_fn("BREAKX",  _PAT_BREAKX_,  1, 1);
    register_fn("ANY",     _PAT_ANY_,     1, 1);
    register_fn("NOTANY",  _PAT_NOTANY_,  1, 1);
    register_fn("LEN",     _PAT_LEN_,     1, 1);
    register_fn("POS",     _PAT_POS_,     1, 1);
    register_fn("RPOS",    _PAT_RPOS_,    1, 1);
    register_fn("TAB",     _PAT_TAB_,     1, 1);
    register_fn("RTAB",    _PAT_RTAB_,    1, 1);
    register_fn("ARB",     _PAT_ARB_,     0, 0);
    register_fn("REM",     _PAT_REM_,     0, 0);
    register_fn("FAIL",       _PAT_FAIL_,    0, 0);
    register_fn("ABORT",   _PAT_ABORT_,   0, 0);
    register_fn("SUCCEED", _PAT_SUCCEED_, 0, 0);
    register_fn("BAL",     _PAT_BAL_,     0, 0);
    register_fn("ARBNO",   _PAT_ARBNO_,   1, 1);
    register_fn("FENCE",   _PAT_FENCE_,   0, 1);
    register_fn("ALT",     _PAT_ALT_,     2, 2);
    register_fn("CONCAT",  _PAT_CONCAT_,  2, 2);
    /* Sprint 23: pre-INIT_fn &ALPHABET-derived constants from global.sno
     * &ALPHABET is a 256-char binary string; POS(n) LEN(1) . var extracts char(n).
     * Since STRVAL uses strlen, &ALPHABET[0]=NUL causes all matches to fail.
     * We pre-initialize the key character constants directly. */
    {
        char *_ch = GC_malloc_atomic(2);
        _ch[0] = (char)9;  _ch[1] = '\0'; NV_SET_fn("tab", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)9;  _ch[1] = '\0'; NV_SET_fn("ht", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)10; _ch[1] = '\0'; NV_SET_fn("nl", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)10; _ch[1] = '\0'; NV_SET_fn("lf", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)13; _ch[1] = '\0'; NV_SET_fn("cr", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)12; _ch[1] = '\0'; NV_SET_fn("ff", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)11; _ch[1] = '\0'; NV_SET_fn("vt", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)8;  _ch[1] = '\0'; NV_SET_fn("bs", STRVAL(_ch));
        NV_SET_fn("nul", STRVAL(""));  /* char(0) = empty in string context */
        /* epsilon = the always-succeeds zero-MATCH_fn pattern.
         * USER CONTRACT (Lon, Session 47): epsilon is NEVER assigned by user code.
         * It is the pattern equivalent of NULL (empty string).
         * NULL = empty string sentinel; epsilon = always-succeed pattern sentinel.
         * Pre-initialize here exactly like nl/tab/cr. */
        NV_SET_fn("epsilon", pat_epsilon());
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)47; _ch[1] = '\0'; NV_SET_fn("fSlash", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)92; _ch[1] = '\0'; NV_SET_fn("bSlash", STRVAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)59; _ch[1] = '\0'; NV_SET_fn("semicolon", STRVAL(_ch));
        /* Physical constants — ASCII-defined, never change, pre-init for
         * pattern charset expressions that fire before global.sno line 25 */
        NV_SET_fn("UCASE",  STRVAL(ucase));
        NV_SET_fn("LCASE",  STRVAL(lcase));
        NV_SET_fn("digits", STRVAL("0123456789"));
    }
    monitor_ready = 1;  /* pre-init constants installed; monitor may now fire */
    /* Register tree DT_DATA type, then override field accessors.
     * DEFDAT_fn("tree(t,v,n,c)") installs coercing accessors for each
     * field name, which would overwrite the _b_tree_* registered above.
     * By calling DEFDAT_fn HERE and re-registering _b_tree_* AFTER,
     * our raw accessors win — _b_tree_c returns DT_A, not S. */
    DEFDAT_fn("tree(t,v,n,c)");
    register_fn("c", _b_tree_c, 1, 1);
    register_fn("t", _b_tree_t, 1, 1);
    register_fn("v", _b_tree_v, 1, 1);
    register_fn("n", _b_tree_n, 1, 1);
}

/* ============================================================
 * String utilities
 * ============================================================ */

char *STRDUP_fn(const char *s) {
    if (!s) return GC_strdup("");
    return GC_strdup(s);
}

char *STRCONCAT_fn(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char *r = GC_malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb);
    r[la + lb] = '\0';
    return r;
}

/* P003: DESCR_t CONCAT_fn — propagates FAILDESCR if either operand is DT_FAIL.
 * If either operand is a PATTERN, build a pattern concatenation instead of
 * string concatenation (blank-juxtaposition of patterns = pattern cat). */
DESCR_t CONCAT_fn(DESCR_t a, DESCR_t b) {
    if (a.v == DT_FAIL) return FAILDESCR;
    if (b.v == DT_FAIL) return FAILDESCR;
    if (a.v == DT_P || b.v == DT_P)
        return pat_cat(a, b);
    const char *sa = VARVAL_fn(a);
    const char *sb = VARVAL_fn(b);
    return STRVAL(STRCONCAT_fn(sa, sb));
}

int64_t size(const char *s) {
    return s ? (int64_t)strlen(s) : 0;
}

/* ============================================================
 * Type conversions
 * ============================================================ */

char *VARVAL_fn(DESCR_t v) {
    char buf[64];
    switch (v.v) {
        case DT_SNUL:    return GC_strdup("");
        case DT_S:     return v.s ? v.s : GC_strdup("");
        case DT_I:
            snprintf(buf, sizeof(buf), "%" PRId64, v.i);
            return GC_strdup(buf);
        case DT_R: {
            /* SNOBOL4 real format: no trailing zeros, no .0 for whole numbers */
            snprintf(buf, sizeof(buf), "%.15g", v.r);
            /* If no decimal point and no 'e', add trailing dot (SPITBOL style) */
            if (!strchr(buf, '.') && !strchr(buf, 'e'))
                strncat(buf, ".", sizeof(buf) - strlen(buf) - 1);
            return GC_strdup(buf);
        }
        case DT_DATA:
            /* Trees stringify as their tag */
            return v.u ? GC_strdup(v.u->type->name) : GC_strdup("");
        case DT_P:
            return GC_strdup("PATTERN");
        case DT_A:
            return GC_strdup("ARRAY");
        case DT_T:
            return GC_strdup("TABLE");
        default:
            return GC_strdup("");
    }
}

int64_t to_int(DESCR_t v) {
    switch (v.v) {
        case DT_I:  return v.i;
        case DT_R: return (int64_t)v.r;
        case DT_S:
        case DT_SNUL: {
            const char *s = v.s ? v.s : "";
            while (*s == ' ') s++;
            if (!*s) return 0;
            return (int64_t)strtoll(s, NULL, 10);
        }
        default: return 0;
    }
}

double to_real(DESCR_t v) {
    switch (v.v) {
        case DT_R: return v.r;
        case DT_I:  return (double)v.i;
        case DT_S:
        case DT_SNUL: {
            const char *s = v.s ? v.s : "";
            return strtod(s, NULL);
        }
        default: return 0.0;
    }
}

const char *datatype(DESCR_t v) {
    switch (v.v) {
        case DT_SNUL:    return "STRING";  /* NULL = empty string */
        case DT_S:       return "STRING";
        case DT_I:       return "INTEGER";
        case DT_R:       return "REAL";
        case DT_DATA:    return v.u ? v.u->type->name : "DATA";
        case DT_P:       return "PATTERN";
        case DT_A:       return "ARRAY";
        case DT_T:       return "TABLE";
        case DT_C:       return "CODE";
        default:         return "STRING";
    }
}

/* ============================================================
 * TREEBLK_t operations
 * ============================================================ */

TREEBLK_t *tree_new(const char *tag, DESCR_t val) {
    TREEBLK_t *t = GC_malloc(sizeof(TREEBLK_t));
    t->tag = GC_strdup(tag ? tag : "");
    t->val = val;
    t->n   = 0;
    t->cap = 0;
    t->c   = NULL;
    return t;
}

TREEBLK_t *tree_new0(const char *tag) {
    return tree_new(tag, NULVCL);
}

static void _tree_ensure_cap(TREEBLK_t *x, int needed) {
    if (x->cap >= needed) return;
    int newcap = x->cap ? x->cap * 2 : 4;
    while (newcap < needed) newcap *= 2;
    TREEBLK_t **nc = GC_malloc(newcap * sizeof(TREEBLK_t *));
    if (x->c) memcpy(nc, x->c, x->n * sizeof(TREEBLK_t *));
    x->c   = nc;
    x->cap = newcap;
}

void tree_append(TREEBLK_t *x, TREEBLK_t *y) {
    _tree_ensure_cap(x, x->n + 1);
    x->c[x->n++] = y;
}

void tree_prepend(TREEBLK_t *x, TREEBLK_t *y) {
    _tree_ensure_cap(x, x->n + 1);
    memmove(x->c + 1, x->c, x->n * sizeof(TREEBLK_t *));
    x->c[0] = y;
    x->n++;
}

void tree_insert(TREEBLK_t *x, TREEBLK_t *y, int place) {
    /* place is 1-based */
    if (place < 1) place = 1;
    if (place > x->n + 1) place = x->n + 1;
    _tree_ensure_cap(x, x->n + 1);
    int idx = place - 1;
    memmove(x->c + idx + 1, x->c + idx, (x->n - idx) * sizeof(TREEBLK_t *));
    x->c[idx] = y;
    x->n++;
}

TREEBLK_t *tree_remove(TREEBLK_t *x, int place) {
    /* place is 1-based */
    if (!x || place < 1 || place > x->n) return NULL;
    int idx = place - 1;
    TREEBLK_t *removed = x->c[idx];
    memmove(x->c + idx, x->c + idx + 1, (x->n - idx - 1) * sizeof(TREEBLK_t *));
    x->n--;
    return removed;
}

/* ============================================================
 * Array
 * ============================================================ */

ARBLK_t *array_new(int lo, int hi) {
    ARBLK_t *a = GC_malloc(sizeof(ARBLK_t));
    a->lo   = lo;
    a->hi   = hi;
    a->ndim = 1;
    int sz  = hi - lo + 1;
    if (sz < 1) sz = 1;
    a->data = GC_malloc(sz * sizeof(DESCR_t));
    for (int i = 0; i < sz; i++) a->data[i] = NULVCL;
    return a;
}

ARBLK_t *array_new2d(int lo1, int hi1, int lo2, int hi2) {
    /* Stored as flat row-major: INDEX_fn = (i-lo1)*(hi2-lo2+1) + (j-lo2) */
    ARBLK_t *a = GC_malloc(sizeof(ARBLK_t));
    a->lo   = lo1;
    a->hi   = hi1;
    a->ndim = 2;
    int rows = hi1 - lo1 + 1;
    int cols = hi2 - lo2 + 1;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    a->data = GC_malloc(rows * cols * sizeof(DESCR_t));
    for (int i = 0; i < rows * cols; i++) a->data[i] = NULVCL;
    /* Store hi2/lo2 in spare fields — abuse: hi=hi2 in a second slot.
     * For simplicity, encode cols in a separate field. */
    /* Use tag trick: store cols count in a DESCR_t at position -1.
     * Simpler: always allocate +1 and store cols at INDEX_fn 0. */
    /* Actually: store lo2/hi2 by repurposing ndim as cols */
    a->ndim = cols;  /* repurpose: ndim = cols for 2D arrays */
    return a;
}

DESCR_t array_get(ARBLK_t *a, int i) {
    if (!a) return FAILDESCR;
    int idx = i - a->lo;
    if (idx < 0 || idx >= (a->hi - a->lo + 1)) return FAILDESCR;  /* P002 */
    return a->data[idx];
}

void array_set(ARBLK_t *a, int i, DESCR_t v) {
    if (!a) return;
    int idx = i - a->lo;
    if (idx < 0 || idx >= (a->hi - a->lo + 1)) return;
    a->data[idx] = v;
}

DESCR_t array_get2(ARBLK_t *a, int i, int j) {
    if (!a) return FAILDESCR;
    int cols = a->ndim;  /* cols stored in ndim for 2D */
    int row  = i - a->lo;
    /* j-origin: assume lo2 = 1 (SNOBOL4 default) */
    int col  = j - 1;
    int idx  = row * cols + col;
    int total = (a->hi - a->lo + 1) * cols;
    if (row < 0 || row >= (a->hi - a->lo + 1) || col < 0 || col >= cols || idx < 0 || idx >= total)
        return FAILDESCR;
    return a->data[idx];
}

void array_set2(ARBLK_t *a, int i, int j, DESCR_t v) {
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
    return h % TABLE_BUCKETS;
}

TBBLK_t *table_new(void) {
    TBBLK_t *t = GC_malloc(sizeof(TBBLK_t));
    memset(t->buckets, 0, sizeof(t->buckets));
    t->size = 0;
    return t;
}

DESCR_t table_get(TBBLK_t *tbl, const char *key) {
    if (!tbl || !key) return NULVCL;
    unsigned h = _tbl_hash(key);
    for (TBPAIR_t *e = tbl->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->val;
    return NULVCL;
}

void table_set(TBBLK_t *tbl, const char *key, DESCR_t val) {
    if (!tbl || !key) return;
    unsigned h = _tbl_hash(key);
    for (TBPAIR_t *e = tbl->buckets[h]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) { e->val = val; return; }
    }
    TBPAIR_t *e = GC_malloc(sizeof(TBPAIR_t));
    e->key       = GC_strdup(key);
    e->key_descr = STRVAL(e->key);  /* default: string descriptor */
    e->val  = val;
    e->next = tbl->buckets[h];
    tbl->buckets[h] = e;
    tbl->size++;
}

/* table_set_descr: set a table entry preserving the original key descriptor type.
 * Used by _aset_impl so integer keys (e.g. t[1] = 'x') round-trip as integers
 * through SORT() → objArr[i,1] → DATATYPE check in XDump. */
void table_set_descr(TBBLK_t *tbl, const char *key, DESCR_t key_d, DESCR_t val) {
    if (!tbl || !key) return;
    unsigned h = _tbl_hash(key);
    for (TBPAIR_t *e = tbl->buckets[h]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) { e->val = val; e->key_descr = key_d; return; }
    }
    TBPAIR_t *e = GC_malloc(sizeof(TBPAIR_t));
    e->key       = GC_strdup(key);
    e->key_descr = key_d;
    e->val  = val;
    e->next = tbl->buckets[h];
    tbl->buckets[h] = e;
    tbl->size++;
}

int table_has(TBBLK_t *tbl, const char *key) {
    if (!tbl || !key) return 0;
    unsigned h = _tbl_hash(key);
    for (TBPAIR_t *e = tbl->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return 1;
    return 0;
}

/* ============================================================
 * User-defined datatypes (DT_DATA() mechanism)
 * ============================================================ */

static DATBLK_t *_udef_types = NULL;

/* Parse DT_DATA spec: "tree(t,v,n,c)" → name="tree", fields=["t","v","n","c"] */
void DEFDAT_fn(const char *spec) {
    /* Spec format: "typename(field1,field2,...)" */
    char *s = GC_strdup(spec);
    char *paren = strchr(s, '(');
    if (!paren) return;
    *paren = '\0';
    char *name = s;
    char *fields_str = paren + 1;
    char *close = strchr(fields_str, ')');
    if (close) *close = '\0';

    DATBLK_t *t = GC_malloc(sizeof(DATBLK_t));
    /* Uppercase name to match CSNOBOL4 DATATYPE() behavior */
    char *uname = GC_strdup(name);
    for (char *p = uname; *p; p++) *p = (char)toupper((unsigned char)*p);
    t->name = uname;

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

static DATBLK_t *_udef_lookup(const char *name) {
    for (DATBLK_t *t = _udef_types; t; t = t->next)
        if (strcasecmp(t->name, name) == 0) return t;
    return NULL;
}

DESCR_t DATCON_fn(const char *typename, ...) {
    DATBLK_t *t = _udef_lookup(typename);
    if (!t) return NULVCL;

    DATINST_t *u = GC_malloc(sizeof(DATINST_t));
    u->type   = t;
    u->fields = GC_malloc(t->nfields * sizeof(DESCR_t));
    for (int i = 0; i < t->nfields; i++) u->fields[i] = NULVCL;

    /* Assign varargs fields */
    va_list ap;
    va_start(ap, typename);
    for (int i = 0; i < t->nfields; i++) {
        DESCR_t v = va_arg(ap, DESCR_t);
        /* sentinel check: if type == DT_SNUL and s == NULL, stop */
        if (v.v == DT_SNUL && v.s == NULL) break;
        u->fields[i] = v;
    }
    va_end(ap);

    return (DESCR_t){ .v = DT_DATA, .u = u };
}

DESCR_t FIELD_GET_fn(DESCR_t obj, const char *field) {
    if (obj.v != DT_DATA || !obj.u) return NULVCL;
    DATBLK_t *t = obj.u->type;
    for (int i = 0; i < t->nfields; i++)
        if (strcasecmp(t->fields[i], field) == 0)
            return obj.u->fields[i];
    return NULVCL;
}

void FIELD_SET_fn(DESCR_t obj, const char *field, DESCR_t val) {
    if (obj.v != DT_DATA || !obj.u) return;
    DATBLK_t *t = obj.u->type;
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
    DESCR_t  val;
    struct _VarEntry *next;
} NV_t;

static NV_t *_var_buckets[VAR_BUCKETS];
static int _var_init_done = 0;

/* Static-pointer registration: when NV_SET_fn(name,val) fires,
 * also update the C-static pointer if registered. This bridges the
 * two-store gap for vars set via pattern conditional assignment (. var)
 * or pre-INIT_fn in SNO_INIT_fn, whose C statics are never touched
 * by set() because the assignment comes from the pattern engine. */
#define VAR_REG_MAX 1024
typedef struct { const char *name; DESCR_t *ptr; } VarReg;
static VarReg _var_reg[VAR_REG_MAX];
static int    _var_reg_n = 0;

void NV_REG_fn(const char *name, DESCR_t *ptr) {
    if (_var_reg_n < VAR_REG_MAX) {
        _var_reg[_var_reg_n].name = name;
        _var_reg[_var_reg_n].ptr  = ptr;
        _var_reg_n++;
    }
}

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

DESCR_t NV_GET_fn(const char *name) {
    _var_init();
    if (!name) return NULVCL;
    /* Special I/O variables */
    if (strcasecmp(name, "INPUT") == 0) return input_read();
    /* Channel-bound input variable? */
    _io_chan_setup();
    int ch = _io_chan_find_by_var(name);
    if (ch >= 0 && !_io_chan[ch].is_output && _io_chan[ch].fp) {
        ssize_t nread = getline(&_io_chan[ch].buf, &_io_chan[ch].cap, _io_chan[ch].fp);
        if (nread < 0) return FAILDESCR;
        if (nread > 0 && _io_chan[ch].buf[nread-1] == '\n') _io_chan[ch].buf[nread-1] = '\0';
        return STRVAL(GC_strdup(_io_chan[ch].buf));
    }
    /* Protected/unprotected keywords backed by C globals */
    if (strcasecmp(name, "STCOUNT")  == 0) return INTVAL(kw_stcount);
    if (strcasecmp(name, "STNO")     == 0) return INTVAL(kw_stcount);
    if (strcasecmp(name, "STLIMIT")  == 0) return INTVAL(kw_stlimit);
    if (strcasecmp(name, "ANCHOR")   == 0) return INTVAL(kw_anchor);
    if (strcasecmp(name, "TRIM")     == 0) return INTVAL(kw_trim);
    if (strcasecmp(name, "FULLSCAN") == 0) return INTVAL(kw_fullscan);
    if (strcasecmp(name, "ALPHABET") == 0) return BSTRVAL(alphabet, 256);
    unsigned h = _var_hash(name);
    for (NV_t *e = _var_buckets[h]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e->val;
    return NULVCL;
}

void NV_SET_fn(const char *name, DESCR_t val) {
    _var_init();
    if (!name) return;
    comm_var(name, val);
    /* Channel-bound output variable? */
    _io_chan_setup();
    int ch = _io_chan_find_by_var(name);
    if (ch >= 0 && _io_chan[ch].is_output && _io_chan[ch].fp) {
        char *s = VARVAL_fn(val);
        fprintf(_io_chan[ch].fp, "%s\n", s ? s : "");
        return;
    }
    /* Special I/O variables */
    if (strcasecmp(name, "OUTPUT") == 0) { output_val(val); return; }
    if (strcasecmp(name, "TERMINAL") == 0) {
        const char *s = (val.v == DT_S) ? (const char *)val.i : "";
        fprintf(stderr, "%s\n", s);
        return;
    }
    /* Unprotected keywords backed by C globals */
    if (strcasecmp(name, "STLIMIT")  == 0) { kw_stlimit  = (val.v==DT_I)?val.i:(int64_t)to_real(val); return; }
    if (strcasecmp(name, "ANCHOR")   == 0) { kw_anchor   = (val.v==DT_I)?val.i:(int64_t)to_real(val); return; }
    if (strcasecmp(name, "TRIM")     == 0) { kw_trim     = (val.v==DT_I)?val.i:(int64_t)to_real(val); return; }
    if (strcasecmp(name, "FULLSCAN") == 0) { kw_fullscan = (val.v==DT_I)?val.i:(int64_t)to_real(val); return; }
    unsigned h = _var_hash(name);
    for (NV_t *e = _var_buckets[h]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            e->val = val;
            for (int _ri = 0; _ri < _var_reg_n; _ri++)
                if (strcmp(_var_reg[_ri].name, name) == 0) { *_var_reg[_ri].ptr = val; break; }
            return;
        }
    }
    NV_t *e = GC_malloc(sizeof(NV_t));
    e->name = GC_strdup(name);
    e->val  = val;
    e->next = _var_buckets[h];
    _var_buckets[h] = e;
    /* Also update registered C static if present */
    for (int _ri = 0; _ri < _var_reg_n; _ri++)
        if (strcmp(_var_reg[_ri].name, name) == 0) { *_var_reg[_ri].ptr = val; break; }
}

/* Sync all registered C statics FROM the hash table.
 * Call this after all NV_REG_fn() calls (in main) so that
 * vars pre-initialized by SNO_INIT_fn() propagate to their statics. */
void NV_SYNC_fn(void) {
    for (int _ri = 0; _ri < _var_reg_n; _ri++) {
        DESCR_t v = NV_GET_fn(_var_reg[_ri].name);
        if (v.v != DT_SNUL && v.v != 0)
            *_var_reg[_ri].ptr = v;
    }
}

/* $name — indirect variable: the variable whose name is the value of 'name' */
DESCR_t INDR_GET_fn(const char *name) {
    DESCR_t indirect_name = NV_GET_fn(name);
    const char *target = VARVAL_fn(indirect_name);
    return NV_GET_fn(target);
}

void INDR_SET_fn(const char *name, DESCR_t val) {
    DESCR_t indirect_name = NV_GET_fn(name);
    const char *target = VARVAL_fn(indirect_name);
    NV_SET_fn(target, val);
}

/* DUMP implementation — used by _DUMP_ above */
static void var_dump(void) {
    fprintf(stderr, "[DUMP start]\n");
    for (int i = 0; i < VAR_BUCKETS; i++) {
        for (NV_t *e = _var_buckets[i]; e; e = e->next) {
            const char *tname;
            switch(e->val.v) {
                case 0: tname="NULL"; break;
                case 1: tname="STR"; break;
                case 2: tname="INT"; break;
                case 3: tname="REAL"; break;
                case 5: tname="PATTERN"; break;
                case 6: tname="DT_A"; break;
                case 7: tname="TABLE"; break;
                case 8: tname="DT_DATA"; break;
                case 9: tname="DT_FAIL"; break;
                default: tname="OTHER"; break;
            }
            if (e->val.v == DT_S) {
                const char *s = e->val.s ? e->val.s : "(null)";
                int len = (int)strlen(s);
                fprintf(stderr, "  %s = STR(%.*s)\n", e->name, len > 40 ? 40 : len, s);
            } else {
                fprintf(stderr, "  %s = %s\n", e->name, tname);
            }
        }
    }
    fprintf(stderr, "[DUMP end]\n");
}

/* ============================================================
 * Counter stack (nPush/nInc/nDec/nTop/nPop)
 * ============================================================ */

#define NSTACK_MAX 256
static int64_t _nstack[NSTACK_MAX];
static int      _ntop = -1;

/* Home-frame stack: NPUSH records which _nstack level nInc should target.
 * Nested patterns push additional _nstack frames, but nInc should always
 * increment the frame created by the ENCLOSING nPush, not the current top. */
#define NHOME_MAX 256
static int _nhome[NHOME_MAX];
static int _nhome_top = -1;

/* Global sequence counter across all nPush/nInc/nPop/Shift/Reduce events */
int _nseq = 0;

void NPUSH_fn(void) {
    if (_ntop < NSTACK_MAX - 1) {
        ++_ntop;
        _nstack[_ntop] = 0;
        /* record this as the home frame for subsequent nInc calls */
        if (_nhome_top < NHOME_MAX - 1) {
            _nhome[++_nhome_top] = _ntop;
        }
    }
    fprintf(stderr, "SEQ%04d NPUSH depth=%d top=%lld\n",
            ++_nseq, _ntop, (long long)(_ntop >= 0 ? _nstack[_ntop] : 0));
}
int NHAS_FRAME_fn(void) { return _ntop >= 0; }

void NINC_fn(void) {
    if (_ntop >= 0) _nstack[_ntop]++;
    fprintf(stderr, "SEQ%04d NINC  depth=%d top=%lld\n",
            ++_nseq, _ntop, (long long)(_ntop >= 0 ? _nstack[_ntop] : 0));
}
void NINC_AT_fn(int frame) {
    if (frame >= 0 && frame <= _ntop) _nstack[frame]++;
}

void NDEC_fn(void) { if (_ntop >= 0) _nstack[_ntop]--; }

int64_t ntop(void) { return (_ntop >= 0) ? _nstack[_ntop] : 0; }

void NPOP_fn(void) {
    fprintf(stderr, "SEQ%04d NPOP  depth=%d top=%lld\n",
            ++_nseq, _ntop, (long long)(_ntop >= 0 ? _nstack[_ntop] : 0));
    if (_ntop >= 0) _ntop--;
}

/* ============================================================
 * Value stack (Push/Pop/Top for Shift/Reduce)
 * ============================================================ */

#define VSTACK_MAX 1024
static DESCR_t _vstack[VSTACK_MAX];
static int    _vstop = -1;

void PUSH_fn(DESCR_t v) {
    if (_vstop < VSTACK_MAX - 1) _vstack[++_vstop] = v;
}

DESCR_t POP_fn(void) {
    if (_vstop >= 0) return _vstack[_vstop--];
    return NULVCL;
}

DESCR_t TOP_fn(void) {
    if (_vstop >= 0) return _vstack[_vstop];
    return NULVCL;
}

int STACK_DEPTH_fn(void) {
    return _vstop + 1;
}

/* ============================================================
 * Function table (DEFINE_fn/APPLY)
 * ============================================================ */

#define FUNC_BUCKETS 128

typedef struct _FNCBLK_t {
    char   *name;
    char   *spec;       /* full DEFINE_fn spec */
    FNCPTR_t fn;
    /* Parameter names */
    int     nparams;
    char  **params;
    int     nlocals;
    char  **locals;
    struct _FuncEntry *next;
} FNCBLK_t;

static FNCBLK_t *_func_buckets[FUNC_BUCKETS];
static int        _func_init_done = 0;

/* fn_has_builtin — returns 1 if a C-backed (fn!=NULL) entry exists for name.
 * Must use the same hash as _func_hash (h*33^toupper, mod FUNC_BUCKETS). */
static int fn_has_builtin(const char *name) {
    if (!name) return 0;
    _func_init();
    unsigned h = 5381;
    for (const char *p = name; *p; p++)
        h = h * 33 ^ (unsigned char)toupper((unsigned char)*p);
    h %= FUNC_BUCKETS;
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, name) == 0 && e->fn != NULL) return 1;
    return 0;
}

static void _func_init(void) {
    if (_func_init_done) return;
    memset(_func_buckets, 0, sizeof(_func_buckets));
    _func_init_done = 1;
}

static unsigned _func_hash(const char *name) {
    unsigned h = 5381;
    while (*name) h = h * 33 ^ (unsigned char)toupper((unsigned char)*name++);
    return h % FUNC_BUCKETS;
}

/* Parse DEFINE_fn spec: "name(p1,p2)local1,local2"
 * Return allocated FNCBLK_t with name/params/locals filled */
static FNCBLK_t *_parse_define_spec(const char *spec) {
    FNCBLK_t *fe = GC_malloc(sizeof(FNCBLK_t));
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

void DEFINE_fn(const char *spec, FNCPTR_t fn) {
    _func_init();
    FNCBLK_t *fe = _parse_define_spec(spec);
    fe->fn = fn;
    unsigned h = _func_hash(fe->name);
    /* Replace existing if same name */
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next) {
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

/* register_fn_alias — OPSYN support: make newname an alias for oldname.
 * Copies the FNCBLK_t entry for oldname into a new entry for newname,
 * so APPLY_fn(newname,...) dispatches identically to APPLY_fn(oldname,...).
 * If oldname is not found, newname is registered as a no-op (NULVCL). */
void register_fn_alias(const char *newname, const char *oldname) {
    _func_init();
    /* Find the old entry */
    FNCBLK_t *old_entry = NULL;
    unsigned ho = _func_hash(oldname);
    for (FNCBLK_t *e = _func_buckets[ho]; e; e = e->next) {
        if (strcasecmp(e->name, oldname) == 0) { old_entry = e; break; }
    }
    /* Build new entry */
    FNCBLK_t *fe = GC_malloc(sizeof(FNCBLK_t));
    fe->name    = GC_strdup(newname);
    if (old_entry) {
        fe->spec    = old_entry->spec;
        fe->fn      = old_entry->fn;
        fe->nparams = old_entry->nparams;
        fe->params  = old_entry->params;
        fe->nlocals = old_entry->nlocals;
        fe->locals  = old_entry->locals;
    } else {
        fe->spec = GC_strdup(newname); fe->fn = NULL;
        fe->nparams = 0; fe->params = NULL;
        fe->nlocals = 0; fe->locals = NULL;
    }
    fe->next = NULL;
    unsigned hn = _func_hash(newname);
    /* Replace if already present */
    for (FNCBLK_t *e = _func_buckets[hn]; e; e = e->next) {
        if (strcasecmp(e->name, newname) == 0) {
            e->spec = fe->spec; e->fn = fe->fn;
            e->nparams = fe->nparams; e->params = fe->params;
            e->nlocals = fe->nlocals; e->locals = fe->locals;
            return;
        }
    }
    fe->next = _func_buckets[hn];
    _func_buckets[hn] = fe;
}

DESCR_t APPLY_fn(const char *name, DESCR_t *args, int nargs) {
    _func_init();
    if (!name) return NULVCL;
    unsigned h = _func_hash(name);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next) {
        if (strcasecmp(e->name, name) == 0) {
            if (e->fn) return e->fn(args, nargs);
            /* fn==NULL means SNOBOL4-defined function — fall to S sentinel */
            break;
        }
    }
    return NULVCL;
}

/* ARG(fname, n) — return uppercase name of nth parameter (1-based).
 * Fails if fname not found or n out of bounds. */
static DESCR_t _ARG_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *fname = VARVAL_fn(a[0]);
    if (!fname) return FAILDESCR;
    int64_t idx = to_int(a[1]);
    _func_init();
    unsigned h = _func_hash(fname);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next) {
        if (strcasecmp(e->name, fname) == 0) {
            if (idx < 1 || idx > (int64_t)e->nparams) return FAILDESCR;
            const char *pname = e->params[idx - 1];
            size_t len = strlen(pname);
            char *up = GC_malloc(len + 1);
            for (size_t i = 0; i <= len; i++) up[i] = (char)toupper((unsigned char)pname[i]);
            return STRVAL(up);
        }
    }
    return FAILDESCR;
}

/* LOCAL(fname, n) — return uppercase name of nth local variable (1-based).
 * Fails if fname not found or n out of bounds. */
static DESCR_t _LOCAL_(DESCR_t *a, int n) {
    if (n < 2) return FAILDESCR;
    const char *fname = VARVAL_fn(a[0]);
    if (!fname) return FAILDESCR;
    int64_t idx = to_int(a[1]);
    _func_init();
    unsigned h = _func_hash(fname);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next) {
        if (strcasecmp(e->name, fname) == 0) {
            if (idx < 1 || idx > (int64_t)e->nlocals) return FAILDESCR;
            const char *lname = e->locals[idx - 1];
            size_t len = strlen(lname);
            char *up = GC_malloc(len + 1);
            for (size_t i = 0; i <= len; i++) up[i] = (char)toupper((unsigned char)lname[i]);
            return STRVAL(up);
        }
    }
    return FAILDESCR;
}

int FNCEX_fn(const char *name) {
    _func_init();
    if (!name) return 0;
    unsigned h = _func_hash(name);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, name) == 0) return 1;
    return 0;
}

/* Source-case param/local accessors for scrip-interp (avoids uppercase issue in _ARG_) */
int FUNC_NPARAMS_fn(const char *fname) {
    _func_init();
    if (!fname) return 0;
    unsigned h = _func_hash(fname);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, fname) == 0) return e->nparams;
    return 0;
}
int FUNC_NLOCALS_fn(const char *fname) {
    _func_init();
    if (!fname) return 0;
    unsigned h = _func_hash(fname);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, fname) == 0) return e->nlocals;
    return 0;
}
const char *FUNC_PARAM_fn(const char *fname, int i) {
    _func_init();
    if (!fname) return NULL;
    unsigned h = _func_hash(fname);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, fname) == 0)
            return (i >= 0 && i < e->nparams) ? e->params[i] : NULL;
    return NULL;
}
const char *FUNC_LOCAL_fn(const char *fname, int i) {
    _func_init();
    if (!fname) return NULL;
    unsigned h = _func_hash(fname);
    for (FNCBLK_t *e = _func_buckets[h]; e; e = e->next)
        if (strcasecmp(e->name, fname) == 0)
            return (i >= 0 && i < e->nlocals) ? e->locals[i] : NULL;
    return NULL;
}

/* ============================================================
 * Builtin string functions
 * ============================================================ */

DESCR_t SIZE_fn(DESCR_t s) {
    const char *STRVAL_fn = VARVAL_fn(s);
    return INTVAL((int64_t)strlen(STRVAL_fn));
}

DESCR_t DUPL_fn(DESCR_t s, DESCR_t n) {
    const char *STRVAL_fn = VARVAL_fn(s);
    int64_t times   = to_int(n);
    if (times <= 0 || !STRVAL_fn || !*STRVAL_fn) return STRVAL(GC_strdup(""));
    size_t slen = strlen(STRVAL_fn);
    char *r = GC_malloc(slen * (size_t)times + 1);
    r[0] = '\0';
    for (int64_t i = 0; i < times; i++) memcpy(r + i * slen, STRVAL_fn, slen);
    r[slen * times] = '\0';
    return STRVAL(r);
}

DESCR_t REPLACE_fn(DESCR_t s, DESCR_t from, DESCR_t to) {
    /* REPLACE(s, from, to): for each char in from, map to corresponding char in to.
     * Like tr command. Uses descr_slen() to support binary strings (e.g. &ALPHABET). */
    const char *sp   = s.v == DT_S ? s.s : VARVAL_fn(s);
    const char *fp   = from.v == DT_S ? from.s : VARVAL_fn(from);
    const char *tp   = to.v == DT_S ? to.s : VARVAL_fn(to);
    size_t slen_val  = descr_slen(s);
    /* Build translation table: identity by default */
    unsigned char xlat[256];
    for (int i = 0; i < 256; i++) xlat[i] = (unsigned char)i;
    size_t flen = descr_slen(from), tlen = descr_slen(to);
    for (size_t i = 0; i < flen; i++) {
        unsigned char fc = (unsigned char)fp[i];
        unsigned char tc = (i < tlen) ? (unsigned char)tp[i] : 0;
        xlat[fc] = tc;
    }
    /* Apply translation.
     * binary_mode: from/to/subject is a binary string (slen>0) — preserve NULs
     * in result so positional alignment is maintained (&ALPHABET use-case).
     * Normal mode: drop NUL-mapped chars (traditional SNOBOL4 REPLACE). */
    int binary_mode = (from.v == DT_S && from.slen) || (to.v == DT_S && to.slen)
                   || (s.v == DT_S && s.slen);
    char *r = GC_malloc(slen_val + 1);
    size_t rlen = 0;
    for (size_t i = 0; i < slen_val; i++) {
        unsigned char c = xlat[(unsigned char)sp[i]];
        if (binary_mode || c) r[rlen++] = (char)c;
    }
    r[rlen] = '\0';
    return binary_mode ? BSTRVAL(r, rlen) : STRVAL(r);
}

DESCR_t SUBSTR_fn(DESCR_t s, DESCR_t i, DESCR_t n) {
    const char *STRVAL_fn = VARVAL_fn(s);
    int64_t start   = to_int(i);  /* 1-based */
    int64_t len_    = to_int(n);
    int64_t slen    = (int64_t)strlen(STRVAL_fn);
    if (start < 1) start = 1;
    if (start > slen + 1) return STRVAL(GC_strdup(""));
    if (len_ < 0) len_ = 0;
    if (start - 1 + len_ > slen) len_ = slen - start + 1;
    char *r = GC_malloc((size_t)len_ + 1);
    memcpy(r, STRVAL_fn + start - 1, (size_t)len_);
    r[len_] = '\0';
    return STRVAL(r);
}

DESCR_t TRIM_fn(DESCR_t s) {
    const char *STRVAL_fn = VARVAL_fn(s);
    /* TRIM_fn: remove trailing blanks */
    int len = (int)strlen(STRVAL_fn);
    while (len > 0 && STRVAL_fn[len-1] == ' ') len--;
    char *r = GC_malloc((size_t)len + 1);
    memcpy(r, STRVAL_fn, (size_t)len);
    r[len] = '\0';
    return STRVAL(r);
}

DESCR_t lpad_fn(DESCR_t s, DESCR_t n, DESCR_t pad) {
    const char *STRVAL_fn = VARVAL_fn(s);
    int64_t width   = to_int(n);
    const char *p   = VARVAL_fn(pad);
    char padch      = (p && *p) ? p[0] : ' ';
    int64_t slen    = (int64_t)strlen(STRVAL_fn);
    if (width <= slen) return STRVAL(GC_strdup(STRVAL_fn));
    int64_t npad = width - slen;
    char *r = GC_malloc((size_t)width + 1);
    memset(r, padch, (size_t)npad);
    memcpy(r + npad, STRVAL_fn, (size_t)slen);
    r[width] = '\0';
    return STRVAL(r);
}

DESCR_t rpad_fn(DESCR_t s, DESCR_t n, DESCR_t pad) {
    const char *STRVAL_fn = VARVAL_fn(s);
    int64_t width   = to_int(n);
    const char *p   = VARVAL_fn(pad);
    char padch      = (p && *p) ? p[0] : ' ';
    int64_t slen    = (int64_t)strlen(STRVAL_fn);
    if (width <= slen) return STRVAL(GC_strdup(STRVAL_fn));
    char *r = GC_malloc((size_t)width + 1);
    memcpy(r, STRVAL_fn, (size_t)slen);
    memset(r + slen, padch, (size_t)(width - slen));
    r[width] = '\0';
    return STRVAL(r);
}

DESCR_t REVERS_fn(DESCR_t s) {
    const char *STRVAL_fn = VARVAL_fn(s);
    int len = (int)strlen(STRVAL_fn);
    char *r = GC_malloc((size_t)len + 1);
    for (int i = 0; i < len; i++) r[i] = STRVAL_fn[len - 1 - i];
    r[len] = '\0';
    return STRVAL(r);
}

DESCR_t BCHAR_fn(DESCR_t n) {
    int64_t code = to_int(n);
    char *buf = GC_malloc_atomic(2);
    buf[0] = (char)(code & 0xFF);
    buf[1] = '\0';
    return BSTRVAL(buf, 1);
}

DESCR_t INTGER_fn(DESCR_t v) {
    /* INTEGER(v): convert to integer, fail if not possible */
    if (v.v == DT_I) return v;
    if (v.v == DT_R) return INTVAL((int64_t)v.r);
    if (v.v == DT_S || v.v == DT_SNUL) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (!*s) return NULVCL;  /* fail */
        char *end;
        long long iv = strtoll(s, &end, 10);
        while (*end == ' ') end++;
        if (*end) return NULVCL;  /* fail — not a pure integer */
        return INTVAL((int64_t)iv);
    }
    return NULVCL;
}

DESCR_t real_fn(DESCR_t v) {
    if (v.v == DT_R) return v;
    if (v.v == DT_I)  return REALVAL((double)v.i);
    if (v.v == DT_S || v.v == DT_SNUL) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (!*s) return NULVCL;
        char *end;
        double rv = strtod(s, &end);
        while (*end == ' ') end++;
        if (*end) return NULVCL;
        return REALVAL(rv);
    }
    return NULVCL;
}

DESCR_t string_fn(DESCR_t v) {
    return STRVAL(VARVAL_fn(v));
}

/* ============================================================
 * Arithmetic / comparison
 * ============================================================ */

/* Arithmetic — promote int+int=int, otherwise real */
/* Coerce a value to integer if it is a string that contains only digits
 * (possibly with leading/trailing spaces and optional sign).
 * Returns the value unchanged if it does not look like a pure integer. */
static DESCR_t coerce_numeric(DESCR_t v) {
    if (v.v == DT_S) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (*s == '+' || *s == '-') s++;
        if (!*s) return INTVAL(0);  /* empty string coerces to integer 0 */
        const char *p = s;
        while (*p >= '0' && *p <= '9') p++;
        /* skip trailing spaces */
        while (*p == ' ') p++;
        if (*p == '\0' && p > s)  /* pure integer string */
            return INTVAL((int64_t)strtoll(v.s ? v.s : "", NULL, 10));
    }
    return v;
}

DESCR_t add(DESCR_t a, DESCR_t b) {
    if (a.v == DT_FAIL || b.v == DT_FAIL) return FAILDESCR;
    if (a.v == DT_SNUL) a = INTVAL(0);
    if (b.v == DT_SNUL) b = INTVAL(0);
    a = coerce_numeric(a); b = coerce_numeric(b);
    if (a.v == DT_I && b.v == DT_I)
        return INTVAL(a.i + b.i);
    return REALVAL(to_real(a) + to_real(b));
}

DESCR_t sub(DESCR_t a, DESCR_t b) {
    if (a.v == DT_FAIL || b.v == DT_FAIL) return FAILDESCR;
    if (a.v == DT_SNUL) a = INTVAL(0);
    if (b.v == DT_SNUL) b = INTVAL(0);
    a = coerce_numeric(a); b = coerce_numeric(b);
    if (a.v == DT_I && b.v == DT_I)
        return INTVAL(a.i - b.i);
    return REALVAL(to_real(a) - to_real(b));
}

DESCR_t mul(DESCR_t a, DESCR_t b) {
    if (a.v == DT_FAIL || b.v == DT_FAIL) return FAILDESCR;
    a = coerce_numeric(a); b = coerce_numeric(b);
    if (a.v == DT_I && b.v == DT_I)
        return INTVAL(a.i * b.i);
    return REALVAL(to_real(a) * to_real(b));
}

DESCR_t DIVIDE_fn(DESCR_t a, DESCR_t b) {
    if (a.v == DT_FAIL || b.v == DT_FAIL) return FAILDESCR;
    /* SNOBOL4 / is real division; integer / integer = integer in SNOBOL4 */
    if (a.v == DT_I && b.v == DT_I) {
        if (b.i == 0) return NULVCL;  /* division error */
        return INTVAL(a.i / b.i);
    }
    double denom = to_real(b);
    if (denom == 0.0) return NULVCL;
    return REALVAL(to_real(a) / denom);
}

DESCR_t POWER_fn(DESCR_t a, DESCR_t b) {
    if (a.v == DT_FAIL || b.v == DT_FAIL) return FAILDESCR;
    /* Integer ** non-negative integer → integer result */
    if (a.v == DT_I && b.v == DT_I && b.i >= 0) {
        int64_t base = a.i, exp = b.i, result = 1;
        while (exp-- > 0) result *= base;
        return INTVAL(result);
    }
    return REALVAL(pow(to_real(a), to_real(b)));
}

DESCR_t neg(DESCR_t a) {
    if (a.v == DT_FAIL) return FAILDESCR;
    if (a.v == DT_I)  return INTVAL(-a.i);
    if (a.v == DT_R) return REALVAL(-a.r);
    return INTVAL(-to_int(a));
}

/* Unary + — coerce to numeric (identity on int/real, str→int otherwise) */
DESCR_t pos(DESCR_t a) {
    if (a.v == DT_FAIL) return FAILDESCR;
    if (a.v == DT_I)  return a;
    if (a.v == DT_R)  return a;
    return INTVAL(to_int(a));
}

/* Numeric comparisons — return 1=success (true), 0=failure */
int eq(DESCR_t a, DESCR_t b) {
    if (a.v == DT_I && b.v == DT_I) return a.i == b.i;
    return to_real(a) == to_real(b);
}
int ne(DESCR_t a, DESCR_t b) { return !eq(a, b); }
int lt(DESCR_t a, DESCR_t b) {
    if (a.v == DT_I && b.v == DT_I) return a.i < b.i;
    return to_real(a) < to_real(b);
}
int le(DESCR_t a, DESCR_t b) {
    if (a.v == DT_I && b.v == DT_I) return a.i <= b.i;
    return to_real(a) <= to_real(b);
}
int gt(DESCR_t a, DESCR_t b) {
    if (a.v == DT_I && b.v == DT_I) return a.i > b.i;
    return to_real(a) > to_real(b);
}
int ge(DESCR_t a, DESCR_t b) {
    if (a.v == DT_I && b.v == DT_I) return a.i >= b.i;
    return to_real(a) >= to_real(b);
}

/* IDENT: succeed if a and b are identical (same type and value) */
int ident(DESCR_t a, DESCR_t b) {
    if (a.v != b.v) {
        /* "" and NULL are identical */
        int a_null = (a.v == DT_SNUL || (a.v == DT_S && descr_slen(a) == 0));
        int b_null = (b.v == DT_SNUL || (b.v == DT_S && descr_slen(b) == 0));
        if (a_null && b_null) return 1;
        return 0;
    }
    switch (a.v) {
        case DT_SNUL: return 1;
        case DT_S: {
            size_t la = descr_slen(a), lb = descr_slen(b);
            const char *sa = a.s ? a.s : "", *sb = b.s ? b.s : "";
            return la == lb && memcmp(sa, sb, la) == 0;
        }
        case DT_I:  return a.i == b.i;
        case DT_R: return a.r == b.r;
        case DT_DATA: return a.u /* .t->tree gone */ == b.u /* .t->tree gone */;  /* pointer identity */
        default:       return a.ptr == b.ptr;
    }
}

int differ(DESCR_t a, DESCR_t b) { return !ident(a, b); }

/* ============================================================
 * I/O
 * ============================================================ */

void output_val(DESCR_t v) {
    char *s = VARVAL_fn(v);
    printf("%s\n", s ? s : "");
}

void output_str(const char *s) {
    printf("%s\n", s ? s : "");
}

/* Current INPUT source — defaults to stdin, redirected by INPUT(name,channel,'',fileName) */
static FILE *_input_fp = NULL;
static char *_input_buf = NULL;
static size_t _input_cap = 0;

DESCR_t input_read(void) {
    if (!_input_fp) _input_fp = stdin;
    ssize_t nread = getline(&_input_buf, &_input_cap, _input_fp);
    if (nread < 0) return FAILDESCR;  /* EOF = INPUT fails */
    if (nread > 0 && _input_buf[nread-1] == '\n') _input_buf[nread-1] = '\0';
    return STRVAL(GC_strdup(_input_buf));
}

/* Extract filename from "filename[-opts]" or "filename" string */
static const char *_io_extract_fname(const char *opts_str, char *buf, size_t bufsz) {
    if (!opts_str || !opts_str[0]) return NULL;
    const char *bracket = strchr(opts_str, '[');
    if (bracket) {
        size_t len = (size_t)(bracket - opts_str);
        while (len > 0 && opts_str[len-1] == ' ') len--;
        if (len > 0 && len < bufsz) {
            memcpy(buf, opts_str, len);
            buf[len] = '\0';
            return buf;
        }
        return NULL;
    }
    return opts_str;  /* no bracket — whole string is filename */
}

/* Get the variable name from a descriptor (handles NAME/indirect form) */
static const char *_io_varname(DESCR_t d) {
    /* a[0] is passed as .varname — it may be a string holding the var name */
    if (d.v == DT_S) return (const char *)d.i;
    return NULL;
}

/* INPUT(varname, channel, options_or_fname [, fname4])
 * 3-arg: INPUT(.rdInput, 8, "file.txt[-opts]")
 * 4-arg: INPUT(.rdInput, 8, "", "file.txt")  */
static DESCR_t _INPUT_(DESCR_t *a, int n) {
    _io_chan_setup();
    char fname_buf[4096];
    const char *fname = NULL;
    if (n >= 4) {
        fname = VARVAL_fn(a[3]);
    } else if (n >= 3) {
        fname = _io_extract_fname(VARVAL_fn(a[2]), fname_buf, sizeof(fname_buf));
    }
    /* channel number */
    int ch = (n >= 2 && a[1].v == DT_I) ? (int)a[1].i : -1;
    /* fallback: no channel / no fname → reset global stdin */
    if (!fname || !fname[0]) {
        if (_input_fp && _input_fp != stdin) fclose(_input_fp);
        _input_fp = stdin;
        return NULVCL;
    }
    FILE *f = fopen(fname, "r");
    if (!f) return FAILDESCR;
    if (ch >= 0 && ch < IO_CHAN_MAX) {
        _io_chan_close(ch);
        _io_chan[ch].fp = f;
        _io_chan[ch].is_output = 0;
        const char *vn = (n >= 1) ? _io_varname(a[0]) : NULL;
        _io_chan[ch].varname = vn ? strdup(vn) : NULL;
    } else {
        /* no valid channel — fall back to global */
        if (_input_fp && _input_fp != stdin) fclose(_input_fp);
        _input_fp = f;
    }
    return NULVCL;
}

/* OUTPUT(varname, channel, fname)
 * 3-arg: OUTPUT(.wrOutput, 8, "file.txt") */
static DESCR_t _OUTPUT_(DESCR_t *a, int n) {
    _io_chan_setup();
    char fname_buf[4096];
    const char *fname = NULL;
    if (n >= 4) {
        fname = VARVAL_fn(a[3]);
    } else if (n >= 3) {
        fname = _io_extract_fname(VARVAL_fn(a[2]), fname_buf, sizeof(fname_buf));
    } else if (n >= 1) {
        /* 1-arg degenerate: OUTPUT(expr) — not a channel association, ignore */
        return NULVCL;
    }
    int ch = (n >= 2 && a[1].v == DT_I) ? (int)a[1].i : -1;
    if (!fname || !fname[0]) return FAILDESCR;
    FILE *f = fopen(fname, "w");
    if (!f) return FAILDESCR;
    if (ch >= 0 && ch < IO_CHAN_MAX) {
        _io_chan_close(ch);
        _io_chan[ch].fp = f;
        _io_chan[ch].is_output = 1;
        const char *vn = (n >= 1) ? _io_varname(a[0]) : NULL;
        _io_chan[ch].varname = vn ? strdup(vn) : NULL;
    } else {
        fclose(f);
        return FAILDESCR;
    }
    return NULVCL;
}

/* Indirect goto — called when :(var) computed goto is taken.
   Currently a stub: prints a warning and continues.
   Full implementation requires a label dispatch table. */
void indirect_goto(const char *varname) {
    DESCR_t v = NV_GET_fn(varname);
    const char *lbl = (v.v == DT_S) ? v.s : "(nil)";
    fprintf(stderr, "indirect_goto: var=%s label=%s (not implemented)\n",
            varname, lbl);
}

int nhome_info(void) { return (_nhome_top >= 0) ? _nhome[_nhome_top] : -1; }

int NTOP_INDEX_fn(void) { return _ntop; }
int64_t NSTACK_AT_fn(int frame) { return (frame>=0 && frame<NSTACK_MAX) ? _nstack[frame] : 0; }

int _x4_pending_parent_frame = -1;
int _command_pending_parent_frame = -1;
