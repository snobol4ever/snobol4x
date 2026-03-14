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
 * When monitor_fd >= 0, every statement and every variable
 * assignment emits an event to that file descriptor.
 * Run with:  ./beautiful 2>/tmp/binary_trace.txt
 * (monitor_fd defaults to stderr = fd 2)
 *
 * Format mirrors CSNOBOL4 &TRACE output:
 *   STNO <n>
 *   VAR <name> <quoted-value>
 * ============================================================ */
int monitor_fd = -1;   /* -1 = disabled; set to 2 for stderr */

/* &STCOUNT — incremented every statement; checked against &STLIMIT */
int64_t kw_stcount = 0;

void comm_stno(int n) {
    /* Enforce &STLIMIT (patch P001 — was declared but never checked) */
    if (kw_stlimit >= 0 && ++kw_stcount > kw_stlimit) {
        fprintf(stderr, "\n** &STLIMIT exceeded at statement %d"
                        " (&STCOUNT=%lld &STLIMIT=%lld)\n",
                n, (long long)kw_stcount, (long long)kw_stlimit);
        exit(1);
    }
    if (monitor_fd < 0) return;
    dprintf(monitor_fd, "STNO %d\n", n);
}

void comm_var(const char *name, SnoVal val) {
    if (monitor_fd < 0) return;
    /* skip noisy internal variables */
    if (!name) return;
    if (name[0] == '_') return;          /* internal scratch vars */
    const char *s = to_str(val);
    dprintf(monitor_fd, "VAR %s \"%s\"\n", name, s ? s : "<null>");
}

/* ============================================================
 * Runtime initialization
 * ============================================================ */

/* Global character constants (set by runtime_init) */
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
 * SNOBOL4 semantics: succeed (return first arg) or fail (FAIL_VAL).
 * Also INTEGER, SIZE, REAL type/conversion builtins.
 * ============================================================ */

static SnoVal _b_GT(SnoVal *a, int n) {
    if (n < 2) return FAIL_VAL;
    return gt(a[0], a[1]) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_LT(SnoVal *a, int n) {
    if (n < 2) return FAIL_VAL;
    return lt(a[0], a[1]) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_GE(SnoVal *a, int n) {
    if (n < 2) return FAIL_VAL;
    return ge(a[0], a[1]) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_LE(SnoVal *a, int n) {
    if (n < 2) return FAIL_VAL;
    return le(a[0], a[1]) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_EQ(SnoVal *a, int n) {
    if (n < 2) return FAIL_VAL;
    /* Numeric equality: equal returns first arg, else fail */
    if (a[0].type == SINT && a[1].type == SINT)
        return (a[0].i == a[1].i) ? NULL_VAL : FAIL_VAL;
    return (to_real(a[0]) == to_real(a[1])) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_NE(SnoVal *a, int n) {
    if (n < 2) return FAIL_VAL;
    if (a[0].type == SINT && a[1].type == SINT)
        return (a[0].i != a[1].i) ? NULL_VAL : FAIL_VAL;
    return (to_real(a[0]) != to_real(a[1])) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_INTEGER(SnoVal *a, int n) {
    if (n < 1) return FAIL_VAL;
    /* Succeed (returning int value) if arg is or converts to integer */
    if (a[0].type == SINT) return a[0];
    if (a[0].type == SSTR && a[0].s) {
        char *end;
        long long v = strtoll(a[0].s, &end, 10);
        if (end != a[0].s && *end == '\0') return INT_VAL(v);
    }
    return FAIL_VAL;
}
static SnoVal _b_REAL(SnoVal *a, int n) {
    if (n < 1) return FAIL_VAL;
    if (a[0].type == SREAL) return a[0];
    if (a[0].type == SINT)  return (SnoVal){ .type = SREAL, .r = (double)a[0].i };
    if (a[0].type == SSTR && a[0].s) {
        char *end;
        double v = strtod(a[0].s, &end);
        if (end != a[0].s && *end == '\0') return (SnoVal){ .type = SREAL, .r = v };
    }
    return FAIL_VAL;
}
static SnoVal _b_SIZE(SnoVal *a, int n) {
    if (n < 1) return INT_VAL(0);
    const char *s = to_str(a[0]);
    return INT_VAL((int64_t)(s ? strlen(s) : 0));
}

/* Sprint 23: IDENT, DIFFER, HOST, ENDFILE, APPLY + string builtins as callable */
static SnoVal _b_IDENT(SnoVal *a, int n) {
    SnoVal x = (n > 0) ? a[0] : NULL_VAL;
    SnoVal y = (n > 1) ? a[1] : NULL_VAL;
    return ident(x, y) ? NULL_VAL : FAIL_VAL;
}
static SnoVal _b_DIFFER(SnoVal *a, int n) {
    SnoVal x = (n > 0) ? a[0] : NULL_VAL;
    SnoVal y = (n > 1) ? a[1] : NULL_VAL;
    return differ(x, y) ? x : FAIL_VAL;
}
static SnoVal _b_HOST(SnoVal *a, int n) {
    /* HOST(0) = command args string, HOST(1) = PID, HOST(3) = argc */
    if (n < 1) return NULL_VAL;
    int64_t selector = to_int(a[0]);
    if (selector == 0) return STR_VAL(GC_strdup(""));
    if (selector == 1) {
        char buf[32]; snprintf(buf, sizeof(buf), "%d", (int)getpid());
        return STR_VAL(GC_strdup(buf));
    }
    if (selector == 3) return INT_VAL(0);
    return NULL_VAL;
}
static SnoVal _b_ENDFILE(SnoVal *a, int n) {
    (void)a; (void)n;
    return FAIL_VAL;  /* not at EOF on any channel */
}
static SnoVal _b_APPLY(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    const char *fname = to_str(a[0]);
    return aply(fname, a + 1, n - 1);
}
static SnoVal _b_LPAD(SnoVal *a, int n) {
    if (n < 2) return n > 0 ? a[0] : NULL_VAL;
    return lpad_fn(a[0], a[1], n > 2 ? a[2] : STR_VAL(" "));
}
static SnoVal _b_RPAD(SnoVal *a, int n) {
    if (n < 2) return n > 0 ? a[0] : NULL_VAL;
    return rpad_fn(a[0], a[1], n > 2 ? a[2] : STR_VAL(" "));
}
static SnoVal _b_CHAR(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return char_fn(a[0]);
}
static SnoVal _b_DUPL(SnoVal *a, int n) {
    if (n < 2) return NULL_VAL;
    return dupl_fn(a[0], a[1]);
}
static SnoVal _b_REPLACE(SnoVal *a, int n) {
    if (n < 3) return NULL_VAL;
    return replace_fn(a[0], a[1], a[2]);
}
static SnoVal _b_TRIM(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return trim_fn(a[0]);
}
static SnoVal _b_SUBSTR(SnoVal *a, int n) {
    if (n < 3) return NULL_VAL;
    return substr_fn(a[0], a[1], a[2]);
}
static SnoVal _b_REVERSE(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return reverse_fn(a[0]);
}
static SnoVal _b_DATATYPE(SnoVal *a, int n) {
    if (n < 1) return STR_VAL("STRING");
    return STR_VAL((char*)datatype(a[0]));
}

/* EVAL / OPSYN / SORT wrappers — file scope required */
extern SnoVal evl(SnoVal);
extern SnoVal opsyn(SnoVal, SnoVal, SnoVal);
extern SnoVal sort_fn(SnoVal);
static SnoVal _b_EVAL(SnoVal *a, int n)  { return evl(n>0?a[0]:NULL_VAL); }
static SnoVal _b_OPSYN(SnoVal *a, int n) {
    return opsyn(n>0?a[0]:NULL_VAL,n>1?a[1]:NULL_VAL,n>2?a[2]:NULL_VAL); }
static SnoVal _b_SORT(SnoVal *a, int n)  { return sort_fn(n>0?a[0]:NULL_VAL); }
static SnoVal _b_INPUT(SnoVal *a, int n);  /* defined near input_read below */

/* Sprint 23: counter stack and tree field accessors as callable SnoVal functions */
static SnoVal _b_nPush(SnoVal *a, int n) {
    (void)a; (void)n;
    npush();
    return NULL_VAL;
}
static SnoVal _b_nInc(SnoVal *a, int n) {
    (void)a; (void)n;
    ninc();
    return INT_VAL(ntop());
}
static SnoVal _b_nDec(SnoVal *a, int n) {
    (void)a; (void)n;
    ndec();
    return INT_VAL(ntop());
}
static SnoVal _b_nTop(SnoVal *a, int n) {
    (void)a; (void)n;
    return INT_VAL(ntop());
}
static SnoVal _b_nPop(SnoVal *a, int n) {
    (void)a; (void)n;
    int64_t val = ntop();
    npop();
    return INT_VAL(val);
}
/* Tree field accessors: n(x), t(x), v(x), c(x) */
static SnoVal _b_tree_n(SnoVal *a, int n) {
    if (n < 1) return INT_VAL(0);
    return field_get(a[0], "n");
}
static SnoVal _b_tree_t(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return field_get(a[0], "t");
}
static SnoVal _b_tree_v(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return field_get(a[0], "v");
}
static SnoVal _b_tree_c(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return field_get(a[0], "c");
}
/* link_counter / link_tag field accessors: value(x), next(x) */
static SnoVal _b_field_value(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return field_get(a[0], "value");
}
static SnoVal _b_field_next(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    return field_get(a[0], "next");
}

/* DUMP builtin — dump all variables to stderr (implementation after var table) */
static void var_dump(void);
static SnoVal _b_DUMP(SnoVal *a, int n) {
    (void)a; (void)n;
    var_dump();
    return NULL_VAL;
}

/* Forward declarations needed by _b_DATA trampolines */
static UDefType *_udef_lookup(const char *name);

/* ---- DATA() builtin ----
 * DATA('typename(field1,field2,...)') — define a user-defined datatype.
 * Registers: constructor typename(f1,f2,...) and field accessors f1(obj),f2(obj),...
 * Uses GC-allocated closure structs so each registered fn knows its type/field name.
 */
typedef struct { char *typename; int nfields; char **fields; } DataClosure;
typedef struct { char *typename; char *fieldname; } FieldClosure;

/* Dynamic constructor: typename(v1, v2, ...) -> UDEF */
static SnoVal _data_ctor_fn(SnoVal *args, int nargs) {
    /* Called as a registered SnoFunc; the closure is stored in a parallel table.
     * We use apply_closure which is not available, so we look up via type name. */
    /* NOTE: This fn is never called directly — see _b_DATA registration below */
    (void)args; (void)nargs;
    return NULL_VAL;
}

/* We need closures per-type. Use a static array of up to 64 DATA types. */
#define DATA_MAX_TYPES 64
#define DATA_MAX_FIELDS 16
static struct {
    char *typename;
    int   nfields;
    char *fields[DATA_MAX_FIELDS];
} _data_types[DATA_MAX_TYPES];
static int _data_ntypes = 0;

/* Generic constructor: looks up typename by position in _data_types,
 * builds a UDef with the provided args. */
static SnoVal _make_ctor(int tidx, SnoVal *args, int nargs) {
    if (tidx < 0 || tidx >= _data_ntypes) return NULL_VAL;
    UDefType *t = _udef_lookup(_data_types[tidx].typename);
    if (!t) return NULL_VAL;
    UDef *u = GC_malloc(sizeof(UDef));
    u->type   = t;
    u->fields = GC_malloc(t->nfields * sizeof(SnoVal));
    for (int i = 0; i < t->nfields; i++)
        u->fields[i] = (i < nargs) ? args[i] : NULL_VAL;
    return (SnoVal){ .type = UDEF, .u = u };
}

/* One constructor trampoline per slot (up to DATA_MAX_TYPES) */
#define CTOR_FN(idx) \
static SnoVal _ctor_##idx(SnoVal *a, int n) { return _make_ctor(idx, a, n); }

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

static SnoVal (*_ctor_fns[DATA_MAX_TYPES])(SnoVal *, int) = {
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

static SnoVal _make_fget(int slot, SnoVal obj) {
    if (slot < 0 || slot >= _facc_n) return NULL_VAL;
    int tidx = _facc_slots[slot].tidx;
    int fidx = _facc_slots[slot].fidx;
    if (obj.type != UDEF || !obj.u) return NULL_VAL;
    if (fidx < 0 || fidx >= obj.u->type->nfields) return NULL_VAL;
    return obj.u->fields[fidx];
}
static void _make_fset(int slot, SnoVal obj, SnoVal val) {
    if (slot < 0 || slot >= _facc_n) return;
    int fidx = _facc_slots[slot].fidx;
    if (obj.type != UDEF || !obj.u) return;
    if (fidx < 0 || fidx >= obj.u->type->nfields) return;
    obj.u->fields[fidx] = val;
}

#define FACC_FN(idx) \
static SnoVal _facc_get_##idx(SnoVal *a, int n) { \
    return n>=1 ? _make_fget(idx, a[0]) : NULL_VAL; }

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

static SnoVal (*_facc_fns[FIELD_ACCESSOR_MAX])(SnoVal *, int) = {
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

static SnoVal _b_DATA(SnoVal *a, int n) {
    if (n < 1) return NULL_VAL;
    const char *spec = to_str(a[0]);
    if (!spec || !*spec) return NULL_VAL;

    /* Register the type in snobol4's UDefType list */
    data_define(spec);

    /* Parse spec to get typename and fields */
    char *s = GC_strdup(spec);
    char *paren = strchr(s, '(');
    if (!paren) return NULL_VAL;
    *paren = '\0';
    char *tname = s;
    char *fstr = paren + 1;
    char *close = strchr(fstr, ')');
    if (close) *close = '\0';

    if (_data_ntypes >= DATA_MAX_TYPES) return NULL_VAL;
    int tidx = _data_ntypes++;
    _data_types[tidx].typename = GC_strdup(tname);

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
    extern void register_fn(const char *, SnoVal (*)(SnoVal*, int), int, int);
    register_fn(tname, _ctor_fns[tidx], 0, nf);

    /* Register field accessors: each field name as a 1-arg function */
    for (int fi = 0; fi < nf; fi++) {
        if (_facc_n >= FIELD_ACCESSOR_MAX) break;
        int slot = _facc_n++;
        _facc_slots[slot].tidx = tidx;
        _facc_slots[slot].fidx = fi;
        /* Only register if not already registered (don't override next/value builtins) */
        const char *fname = _data_types[tidx].fields[fi];
        register_fn(fname, _facc_fns[slot], 1, 1);
    }

    return NULL_VAL;
}

/* Pattern builtins callable via aply() — used when SPAN/BREAK/etc appear
 * inside argument lists and are tokenised as IDENT rather than PAT_BUILTIN. */
extern SnoVal pat_span(const char *);
extern SnoVal pat_break_(const char *);
extern SnoVal pat_any_cs(const char *);
extern SnoVal pat_notany(const char *);
extern SnoVal pat_len(int64_t);
extern SnoVal pat_pos(int64_t);
extern SnoVal pat_rpos(int64_t);
extern SnoVal pat_tab(int64_t);
extern SnoVal pat_rtab(int64_t);
extern SnoVal pat_arb(void);
extern SnoVal pat_rem(void);
extern SnoVal pat_fail(void);
extern SnoVal pat_abort(void);
extern SnoVal pat_succeed(void);
extern SnoVal pat_bal(void);
extern SnoVal pat_arbno(SnoVal);
extern SnoVal pat_fence(void);
extern SnoVal pat_fence_p(SnoVal);

static SnoVal _b_PAT_SPAN(SnoVal *a, int n)    { return n>=1 ? pat_span(to_str(a[0]))    : FAIL_VAL; }
static SnoVal _b_PAT_BREAK(SnoVal *a, int n)   { return n>=1 ? pat_break_(to_str(a[0]))  : FAIL_VAL; }
static SnoVal _b_PAT_ANY(SnoVal *a, int n)     { return n>=1 ? pat_any_cs(to_str(a[0]))  : FAIL_VAL; }
static SnoVal _b_PAT_NOTANY(SnoVal *a, int n)  { return n>=1 ? pat_notany(to_str(a[0]))  : FAIL_VAL; }
static SnoVal _b_PAT_LEN(SnoVal *a, int n)     { return n>=1 ? pat_len(to_int(a[0]))   : FAIL_VAL; }
static SnoVal _b_PAT_POS(SnoVal *a, int n)     { return n>=1 ? pat_pos(to_int(a[0]))   : FAIL_VAL; }
static SnoVal _b_PAT_RPOS(SnoVal *a, int n)    { return n>=1 ? pat_rpos(to_int(a[0]))  : FAIL_VAL; }
static SnoVal _b_PAT_TAB(SnoVal *a, int n)     { return n>=1 ? pat_tab(to_int(a[0]))   : FAIL_VAL; }
static SnoVal _b_PAT_RTAB(SnoVal *a, int n)    { return n>=1 ? pat_rtab(to_int(a[0]))  : FAIL_VAL; }
static SnoVal _b_PAT_ARB(SnoVal *a, int n)     { (void)a;(void)n; return pat_arb();     }
static SnoVal _b_PAT_REM(SnoVal *a, int n)     { (void)a;(void)n; return pat_rem();     }
static SnoVal _b_PAT_FAIL(SnoVal *a, int n)    { (void)a;(void)n; return pat_fail();    }
static SnoVal _b_PAT_ABORT(SnoVal *a, int n)   { (void)a;(void)n; return pat_abort();   }
static SnoVal _b_PAT_SUCCEED(SnoVal *a, int n) { (void)a;(void)n; return pat_succeed(); }
static SnoVal _b_PAT_BAL(SnoVal *a, int n)     { (void)a;(void)n; return pat_bal();     }
static SnoVal _b_PAT_ARBNO(SnoVal *a, int n)   { return n>=1 ? pat_arbno(a[0])  : FAIL_VAL; }
static SnoVal _b_PAT_FENCE(SnoVal *a, int n)   { return n>=1 ? pat_fence_p(a[0]) : pat_fence(); }

void runtime_init(void) {
    GC_INIT();
    /* Build &ALPHABET: all 256 chars in order */
    for (int i = 0; i < 256; i++) alphabet[i] = (char)i;
    alphabet[256] = '\0';
    /* Enable monitor if MONITOR=1 (writes to stderr) */
    const char *mon = getenv("MONITOR");
    if (mon && mon[0] == '1') monitor_fd = 2;

    /* Register numeric comparison builtins */
    extern void register_fn(const char *, SnoVal (*)(SnoVal*, int), int, int);
    register_fn("GT",       _b_GT,       2, 2);
    register_fn("LT",       _b_LT,       2, 2);
    register_fn("GE",       _b_GE,       2, 2);
    register_fn("LE",       _b_LE,       2, 2);
    register_fn("EQ",       _b_EQ,       2, 2);
    register_fn("NE",       _b_NE,       2, 2);
    register_fn("INTEGER",  _b_INTEGER,  1, 1);
    register_fn("REAL",     _b_REAL,     1, 1);
    register_fn("SIZE",     _b_SIZE,     1, 1);
    /* Sprint 23: string predicates and host interface */
    register_fn("IDENT",    _b_IDENT,    0, 2);
    register_fn("DIFFER",   _b_DIFFER,   0, 2);
    register_fn("HOST",     _b_HOST,     1, 4);
    register_fn("ENDFILE",  _b_ENDFILE,  1, 1);
    register_fn("APPLY",    _b_APPLY,    1, 9);
    register_fn("LPAD",     _b_LPAD,     2, 3);
    register_fn("RPAD",     _b_RPAD,     2, 3);
    register_fn("CHAR",     _b_CHAR,     1, 1);
    register_fn("DUPL",     _b_DUPL,     2, 2);
    register_fn("REPLACE",  _b_REPLACE,  3, 3);
    register_fn("TRIM",     _b_TRIM,     1, 1);
    register_fn("SUBSTR",   _b_SUBSTR,   3, 3);
    register_fn("REVERSE",  _b_REVERSE,  1, 1);
    register_fn("DATATYPE", _b_DATATYPE, 1, 1);
    register_fn("DATA",     _b_DATA,     1, 1);
    register_fn("EVAL",  _b_EVAL,  1, 1);
    register_fn("OPSYN", _b_OPSYN, 2, 3);
    register_fn("SORT",  _b_SORT,  1, 1);
    register_fn("INPUT", _b_INPUT, 1, 4);
    register_fn("nPush",    _b_nPush,    0, 0);
    register_fn("nInc",     _b_nInc,     0, 0);
    register_fn("nDec",     _b_nDec,     0, 0);
    register_fn("nTop",     _b_nTop,     0, 0);
    register_fn("nPop",     _b_nPop,     0, 0);
    register_fn("n",        _b_tree_n,      1, 1);
    register_fn("t",        _b_tree_t,      1, 1);
    register_fn("v",        _b_tree_v,      1, 1);
    register_fn("c",        _b_tree_c,      1, 1);
    register_fn("value",    _b_field_value, 1, 1);
    register_fn("next",     _b_field_next,  1, 1);
    register_fn("DUMP",     _b_DUMP,        0, 1);
    /* Pattern builtins callable via aply (when inside arglist parens) */
    register_fn("SPAN",    _b_PAT_SPAN,    1, 1);
    register_fn("BREAK",   _b_PAT_BREAK,   1, 1);
    register_fn("ANY",     _b_PAT_ANY,     1, 1);
    register_fn("NOTANY",  _b_PAT_NOTANY,  1, 1);
    register_fn("LEN",     _b_PAT_LEN,     1, 1);
    register_fn("POS",     _b_PAT_POS,     1, 1);
    register_fn("RPOS",    _b_PAT_RPOS,    1, 1);
    register_fn("TAB",     _b_PAT_TAB,     1, 1);
    register_fn("RTAB",    _b_PAT_RTAB,    1, 1);
    register_fn("ARB",     _b_PAT_ARB,     0, 0);
    register_fn("REM",     _b_PAT_REM,     0, 0);
    register_fn("FAIL",    _b_PAT_FAIL,    0, 0);
    register_fn("ABORT",   _b_PAT_ABORT,   0, 0);
    register_fn("SUCCEED", _b_PAT_SUCCEED, 0, 0);
    register_fn("BAL",     _b_PAT_BAL,     0, 0);
    register_fn("ARBNO",   _b_PAT_ARBNO,   1, 1);
    register_fn("FENCE",   _b_PAT_FENCE,   0, 1);
    /* Sprint 23: pre-ini &ALPHABET-derived constants from global.sno
     * &ALPHABET is a 256-char binary string; POS(n) LEN(1) . var extracts char(n).
     * Since STR_VAL uses strlen, &ALPHABET[0]=NUL causes all matches to fail.
     * We pre-initialize the key character constants directly. */
    {
        char *_ch = GC_malloc_atomic(2);
        _ch[0] = (char)9;  _ch[1] = '\0'; var_set("tab", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)9;  _ch[1] = '\0'; var_set("ht", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)10; _ch[1] = '\0'; var_set("nl", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)10; _ch[1] = '\0'; var_set("lf", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)13; _ch[1] = '\0'; var_set("cr", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)12; _ch[1] = '\0'; var_set("ff", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)11; _ch[1] = '\0'; var_set("vt", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)8;  _ch[1] = '\0'; var_set("bs", STR_VAL(_ch));
        var_set("nul", STR_VAL(""));  /* char(0) = empty in string context */
        /* epsilon = the always-succeeds zero-mtch pattern.
         * USER CONTRACT (Lon, Session 47): epsilon is NEVER assigned by user code.
         * It is the pattern equivalent of NULL (empty string).
         * NULL = empty string sentinel; epsilon = always-succeed pattern sentinel.
         * Pre-initialize here exactly like nl/tab/cr. */
        var_set("epsilon", pat_epsilon());
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)47; _ch[1] = '\0'; var_set("fSlash", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)92; _ch[1] = '\0'; var_set("bSlash", STR_VAL(_ch));
        _ch = GC_malloc_atomic(2);
        _ch[0] = (char)59; _ch[1] = '\0'; var_set("semicolon", STR_VAL(_ch));
    }
}

/* ============================================================
 * String utilities
 * ============================================================ */

char *dupl(const char *s) {
    if (!s) return GC_strdup("");
    return GC_strdup(s);
}

char *ccat(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char *r = GC_malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb);
    r[la + lb] = '\0';
    return r;
}

/* P003: SnoVal ccat — propagates FAIL_VAL if either operand is FAIL.
 * If either operand is a PATTERN, build a pattern concatenation instead of
 * string concatenation (blank-juxtaposition of patterns = pattern cat). */
SnoVal concat_sv(SnoVal a, SnoVal b) {
    if (a.type == SFAIL) return FAIL_VAL;
    if (b.type == SFAIL) return FAIL_VAL;
    if (a.type == SPATTERN || b.type == SPATTERN)
        return pat_cat(a, b);
    const char *sa = to_str(a);
    const char *sb = to_str(b);
    return STR_VAL(ccat(sa, sb));
}

int64_t size(const char *s) {
    return s ? (int64_t)strlen(s) : 0;
}

/* ============================================================
 * Type conversions
 * ============================================================ */

char *to_str(SnoVal v) {
    char buf[64];
    switch (v.type) {
        case SNULL:    return GC_strdup("");
        case SSTR:     return v.s ? v.s : GC_strdup("");
        case SINT:
            snprintf(buf, sizeof(buf), "%" PRId64, v.i);
            return GC_strdup(buf);
        case SREAL: {
            /* SNOBOL4 real format: no trailing zeros, no .0 for whole numbers */
            snprintf(buf, sizeof(buf), "%.15g", v.r);
            /* If no decimal point and no 'e', add trailing dot (SPITBOL style) */
            if (!strchr(buf, '.') && !strchr(buf, 'e'))
                strncat(buf, ".", sizeof(buf) - strlen(buf) - 1);
            return GC_strdup(buf);
        }
        case STREE:
            /* Trees stringify as their tag */
            return v.t ? GC_strdup(v.t->tag) : GC_strdup("");
        default:
            return GC_strdup("");
    }
}

int64_t to_int(SnoVal v) {
    switch (v.type) {
        case SINT:  return v.i;
        case SREAL: return (int64_t)v.r;
        case SSTR:
        case SNULL: {
            const char *s = v.s ? v.s : "";
            while (*s == ' ') s++;
            if (!*s) return 0;
            return (int64_t)strtoll(s, NULL, 10);
        }
        default: return 0;
    }
}

double to_real(SnoVal v) {
    switch (v.type) {
        case SREAL: return v.r;
        case SINT:  return (double)v.i;
        case SSTR:
        case SNULL: {
            const char *s = v.s ? v.s : "";
            return strtod(s, NULL);
        }
        default: return 0.0;
    }
}

const char *datatype(SnoVal v) {
    switch (v.type) {
        case SNULL:    return "STRING";  /* NULL = empty string */
        case SSTR:     return "STRING";
        case SINT:     return "INTEGER";
        case SREAL:    return "REAL";
        case STREE:    return v.t ? v.t->tag : "TREE";
        case SPATTERN: return "PATTERN";
        case ARRAY:   return "ARRAY";
        case STABLE:   return "TABLE";
        case CODE:    return "CODE";
        case UDEF:    return v.u ? v.u->type->name : "UDEF";
        default:          return "STRING";
    }
}

/* ============================================================
 * Tree operations
 * ============================================================ */

Tree *tree_new(const char *tag, SnoVal val) {
    Tree *t = GC_malloc(sizeof(Tree));
    t->tag = GC_strdup(tag ? tag : "");
    t->val = val;
    t->n   = 0;
    t->cap = 0;
    t->c   = NULL;
    return t;
}

Tree *tree_new0(const char *tag) {
    return tree_new(tag, NULL_VAL);
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

void tree_append(Tree *x, Tree *y) {
    _tree_ensure_cap(x, x->n + 1);
    x->c[x->n++] = y;
}

void tree_prepend(Tree *x, Tree *y) {
    _tree_ensure_cap(x, x->n + 1);
    memmove(x->c + 1, x->c, x->n * sizeof(Tree *));
    x->c[0] = y;
    x->n++;
}

void tree_insert(Tree *x, Tree *y, int place) {
    /* place is 1-based */
    if (place < 1) place = 1;
    if (place > x->n + 1) place = x->n + 1;
    _tree_ensure_cap(x, x->n + 1);
    int idx = place - 1;
    memmove(x->c + idx + 1, x->c + idx, (x->n - idx) * sizeof(Tree *));
    x->c[idx] = y;
    x->n++;
}

Tree *tree_remove(Tree *x, int place) {
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

SnoArray *array_new(int lo, int hi) {
    SnoArray *a = GC_malloc(sizeof(SnoArray));
    a->lo   = lo;
    a->hi   = hi;
    a->ndim = 1;
    int sz  = hi - lo + 1;
    if (sz < 1) sz = 1;
    a->data = GC_malloc(sz * sizeof(SnoVal));
    for (int i = 0; i < sz; i++) a->data[i] = NULL_VAL;
    return a;
}

SnoArray *array_new2d(int lo1, int hi1, int lo2, int hi2) {
    /* Stored as flat row-major: indx = (i-lo1)*(hi2-lo2+1) + (j-lo2) */
    SnoArray *a = GC_malloc(sizeof(SnoArray));
    a->lo   = lo1;
    a->hi   = hi1;
    a->ndim = 2;
    int rows = hi1 - lo1 + 1;
    int cols = hi2 - lo2 + 1;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    a->data = GC_malloc(rows * cols * sizeof(SnoVal));
    for (int i = 0; i < rows * cols; i++) a->data[i] = NULL_VAL;
    /* Store hi2/lo2 in spare fields — abuse: hi=hi2 in a second slot.
     * For simplicity, encode cols in a separate field. */
    /* Use tag trick: store cols count in a SnoVal at position -1.
     * Simpler: always allocate +1 and store cols at indx 0. */
    /* Actually: store lo2/hi2 by repurposing ndim as cols */
    a->ndim = cols;  /* repurpose: ndim = cols for 2D arrays */
    return a;
}

SnoVal array_get(SnoArray *a, int i) {
    if (!a) return FAIL_VAL;
    int idx = i - a->lo;
    if (idx < 0 || idx >= (a->hi - a->lo + 1)) return FAIL_VAL;  /* P002 */
    return a->data[idx];
}

void array_set(SnoArray *a, int i, SnoVal v) {
    if (!a) return;
    int idx = i - a->lo;
    if (idx < 0 || idx >= (a->hi - a->lo + 1)) return;
    a->data[idx] = v;
}

SnoVal array_get2(SnoArray *a, int i, int j) {
    if (!a) return FAIL_VAL;
    int cols = a->ndim;  /* cols stored in ndim for 2D */
    int row  = i - a->lo;
    /* j-origin: assume lo2 = 1 (SNOBOL4 default) */
    int col  = j - 1;
    int idx  = row * cols + col;
    int total = (a->hi - a->lo + 1) * cols;
    if (row < 0 || row >= (a->hi - a->lo + 1) || col < 0 || col >= cols || idx < 0 || idx >= total)
        return FAIL_VAL;
    return a->data[idx];
}

void array_set2(SnoArray *a, int i, int j, SnoVal v) {
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

SnoTable *table_new(void) {
    SnoTable *t = GC_malloc(sizeof(SnoTable));
    memset(t->buckets, 0, sizeof(t->buckets));
    t->size = 0;
    return t;
}

SnoVal table_get(SnoTable *tbl, const char *key) {
    if (!tbl || !key) return NULL_VAL;
    unsigned h = _tbl_hash(key);
    for (SnoTableEntry *e = tbl->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->val;
    return NULL_VAL;
}

void table_set(SnoTable *tbl, const char *key, SnoVal val) {
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

int table_has(SnoTable *tbl, const char *key) {
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
void data_define(const char *spec) {
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

SnoVal udef_new(const char *typename, ...) {
    UDefType *t = _udef_lookup(typename);
    if (!t) return NULL_VAL;

    UDef *u = GC_malloc(sizeof(UDef));
    u->type   = t;
    u->fields = GC_malloc(t->nfields * sizeof(SnoVal));
    for (int i = 0; i < t->nfields; i++) u->fields[i] = NULL_VAL;

    /* Assign varargs fields */
    va_list ap;
    va_start(ap, typename);
    for (int i = 0; i < t->nfields; i++) {
        SnoVal v = va_arg(ap, SnoVal);
        /* sentinel check: if type == SNULL and s == NULL, stop */
        if (v.type == SNULL && v.s == NULL) break;
        u->fields[i] = v;
    }
    va_end(ap);

    return (SnoVal){ .type = UDEF, .u = u };
}

SnoVal field_get(SnoVal obj, const char *field) {
    if (obj.type != UDEF || !obj.u) return NULL_VAL;
    UDefType *t = obj.u->type;
    for (int i = 0; i < t->nfields; i++)
        if (strcasecmp(t->fields[i], field) == 0)
            return obj.u->fields[i];
    return NULL_VAL;
}

void field_set(SnoVal obj, const char *field, SnoVal val) {
    if (obj.type != UDEF || !obj.u) return;
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

/* Static-pointer registration: when var_set(name,val) fires,
 * also update the C-static pointer if registered. This bridges the
 * two-store gap for vars set via pattern conditional assignment (. var)
 * or pre-ini in runtime_init, whose C statics are never touched
 * by set() because the assignment comes from the pattern engine. */
#define VAR_REG_MAX 1024
typedef struct { const char *name; SnoVal *ptr; } VarReg;
static VarReg _var_reg[VAR_REG_MAX];
static int    _var_reg_n = 0;

void var_register(const char *name, SnoVal *ptr) {
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

SnoVal var_get(const char *name) {
    _var_init();
    if (!name) return NULL_VAL;
    /* Special I/O variables */
    if (strcmp(name, "INPUT") == 0) return input_read();
    unsigned h = _var_hash(name);
    for (VarEntry *e = _var_buckets[h]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e->val;
    return NULL_VAL;
}

void var_set(const char *name, SnoVal val) {
    _var_init();
    if (!name) return;
    comm_var(name, val);
    /* Special I/O variables */
    if (strcmp(name, "OUTPUT") == 0) { output_val(val); return; }
    unsigned h = _var_hash(name);
    for (VarEntry *e = _var_buckets[h]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            e->val = val;
            for (int _ri = 0; _ri < _var_reg_n; _ri++)
                if (strcmp(_var_reg[_ri].name, name) == 0) { *_var_reg[_ri].ptr = val; break; }
            return;
        }
    }
    VarEntry *e = GC_malloc(sizeof(VarEntry));
    e->name = GC_strdup(name);
    e->val  = val;
    e->next = _var_buckets[h];
    _var_buckets[h] = e;
    /* Also update registered C static if present */
    for (int _ri = 0; _ri < _var_reg_n; _ri++)
        if (strcmp(_var_reg[_ri].name, name) == 0) { *_var_reg[_ri].ptr = val; break; }
}

/* Sync all registered C statics FROM the hash table.
 * Call this after all var_register() calls (in main) so that
 * vars pre-initialized by runtime_init() propagate to their statics. */
void var_sync_registered(void) {
    for (int _ri = 0; _ri < _var_reg_n; _ri++) {
        SnoVal v = var_get(_var_reg[_ri].name);
        if (v.type != SNULL && v.type != 0)
            *_var_reg[_ri].ptr = v;
    }
}

/* $name — indirect variable: the variable whose name is the value of 'name' */
SnoVal indirect_get(const char *name) {
    SnoVal indirect_name = var_get(name);
    const char *target = to_str(indirect_name);
    return var_get(target);
}

void indirect_set(const char *name, SnoVal val) {
    SnoVal indirect_name = var_get(name);
    const char *target = to_str(indirect_name);
    var_set(target, val);
}

/* DUMP implementation — used by _b_DUMP above */
static void var_dump(void) {
    fprintf(stderr, "[DUMP start]\n");
    for (int i = 0; i < VAR_BUCKETS; i++) {
        for (VarEntry *e = _var_buckets[i]; e; e = e->next) {
            const char *tname;
            switch(e->val.type) {
                case 0: tname="NULL"; break;
                case 1: tname="STR"; break;
                case 2: tname="INT"; break;
                case 3: tname="REAL"; break;
                case 5: tname="PATTERN"; break;
                case 6: tname="ARRAY"; break;
                case 7: tname="TABLE"; break;
                case 8: tname="UDEF"; break;
                case 9: tname="FAIL"; break;
                default: tname="OTHER"; break;
            }
            if (e->val.type == SSTR) {
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

void npush(void) {
    if (_ntop < NSTACK_MAX - 1) _nstack[++_ntop] = 0;
}

void ninc(void) {
    if (_ntop >= 0) _nstack[_ntop]++;
}

void ndec(void) {
    if (_ntop >= 0) _nstack[_ntop]--;
}

int64_t ntop(void) {
    return (_ntop >= 0) ? _nstack[_ntop] : 0;
}

void npop(void) {
    if (_ntop >= 0) _ntop--;
}

/* ============================================================
 * Value stack (Push/Pop/Top for Shift/Reduce)
 * ============================================================ */

#define VSTACK_MAX 1024
static SnoVal _vstack[VSTACK_MAX];
static int    _vstop = -1;

void push(SnoVal v) {
    if (_vstop < VSTACK_MAX - 1) _vstack[++_vstop] = v;
}

SnoVal pop(void) {
    if (_vstop >= 0) return _vstack[_vstop--];
    return NULL_VAL;
}

SnoVal top(void) {
    if (_vstop >= 0) return _vstack[_vstop];
    return NULL_VAL;
}

int stack_depth(void) {
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

void define(const char *spec, SnoFunc fn) {
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

SnoVal aply(const char *name, SnoVal *args, int nargs) {
    _func_init();
    if (!name) return NULL_VAL;
    unsigned h = _func_hash(name);
    for (FuncEntry *e = _func_buckets[h]; e; e = e->next) {
        if (strcasecmp(e->name, name) == 0) {
            if (e->fn) return e->fn(args, nargs);
            break;
        }
    }
    return NULL_VAL;
}

int func_exists(const char *name) {
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

SnoVal size_fn(SnoVal s) {
    const char *strv = to_str(s);
    return INT_VAL((int64_t)strlen(strv));
}

SnoVal dupl_fn(SnoVal s, SnoVal n) {
    const char *strv = to_str(s);
    int64_t times   = to_int(n);
    if (times <= 0 || !strv || !*strv) return STR_VAL(GC_strdup(""));
    size_t slen = strlen(strv);
    char *r = GC_malloc(slen * (size_t)times + 1);
    r[0] = '\0';
    for (int64_t i = 0; i < times; i++) memcpy(r + i * slen, strv, slen);
    r[slen * times] = '\0';
    return STR_VAL(r);
}

SnoVal replace_fn(SnoVal s, SnoVal from, SnoVal to) {
    /* REPLACE(s, from, to): for each char in from, replc with corresponding
     * char in to. Like tr command. */
    const char *strv  = to_str(s);
    const char *f    = to_str(from);
    const char *t    = to_str(to);
    size_t slen = strlen(strv);
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
        unsigned char c = xlat[(unsigned char)strv[i]];
        if (c) r[rlen++] = (char)c;
    }
    r[rlen] = '\0';
    return STR_VAL(r);
}

SnoVal substr_fn(SnoVal s, SnoVal i, SnoVal n) {
    const char *strv = to_str(s);
    int64_t start   = to_int(i);  /* 1-based */
    int64_t len_    = to_int(n);
    int64_t slen    = (int64_t)strlen(strv);
    if (start < 1) start = 1;
    if (start > slen + 1) return STR_VAL(GC_strdup(""));
    if (len_ < 0) len_ = 0;
    if (start - 1 + len_ > slen) len_ = slen - start + 1;
    char *r = GC_malloc((size_t)len_ + 1);
    memcpy(r, strv + start - 1, (size_t)len_);
    r[len_] = '\0';
    return STR_VAL(r);
}

SnoVal trim_fn(SnoVal s) {
    const char *strv = to_str(s);
    /* TRIM: remove trailing blanks */
    int len = (int)strlen(strv);
    while (len > 0 && strv[len-1] == ' ') len--;
    char *r = GC_malloc((size_t)len + 1);
    memcpy(r, strv, (size_t)len);
    r[len] = '\0';
    return STR_VAL(r);
}

SnoVal lpad_fn(SnoVal s, SnoVal n, SnoVal pad) {
    const char *strv = to_str(s);
    int64_t width   = to_int(n);
    const char *p   = to_str(pad);
    char padch      = (p && *p) ? p[0] : ' ';
    int64_t slen    = (int64_t)strlen(strv);
    if (width <= slen) return STR_VAL(GC_strdup(strv));
    int64_t npad = width - slen;
    char *r = GC_malloc((size_t)width + 1);
    memset(r, padch, (size_t)npad);
    memcpy(r + npad, strv, (size_t)slen);
    r[width] = '\0';
    return STR_VAL(r);
}

SnoVal rpad_fn(SnoVal s, SnoVal n, SnoVal pad) {
    const char *strv = to_str(s);
    int64_t width   = to_int(n);
    const char *p   = to_str(pad);
    char padch      = (p && *p) ? p[0] : ' ';
    int64_t slen    = (int64_t)strlen(strv);
    if (width <= slen) return STR_VAL(GC_strdup(strv));
    char *r = GC_malloc((size_t)width + 1);
    memcpy(r, strv, (size_t)slen);
    memset(r + slen, padch, (size_t)(width - slen));
    r[width] = '\0';
    return STR_VAL(r);
}

SnoVal reverse_fn(SnoVal s) {
    const char *strv = to_str(s);
    int len = (int)strlen(strv);
    char *r = GC_malloc((size_t)len + 1);
    for (int i = 0; i < len; i++) r[i] = strv[len - 1 - i];
    r[len] = '\0';
    return STR_VAL(r);
}

SnoVal char_fn(SnoVal n) {
    int64_t code = to_int(n);
    char buf[2];
    buf[0] = (char)(code & 0xFF);
    buf[1] = '\0';
    return STR_VAL(GC_strdup(buf));
}

SnoVal integer_fn(SnoVal v) {
    /* INTEGER(v): convert to integer, fail if not possible */
    if (v.type == SINT) return v;
    if (v.type == SREAL) return INT_VAL((int64_t)v.r);
    if (v.type == SSTR || v.type == SNULL) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (!*s) return NULL_VAL;  /* fail */
        char *end;
        long long iv = strtoll(s, &end, 10);
        while (*end == ' ') end++;
        if (*end) return NULL_VAL;  /* fail — not a pure integer */
        return INT_VAL((int64_t)iv);
    }
    return NULL_VAL;
}

SnoVal real_fn(SnoVal v) {
    if (v.type == SREAL) return v;
    if (v.type == SINT)  return REAL_VAL((double)v.i);
    if (v.type == SSTR || v.type == SNULL) {
        const char *s = v.s ? v.s : "";
        while (*s == ' ') s++;
        if (!*s) return NULL_VAL;
        char *end;
        double rv = strtod(s, &end);
        while (*end == ' ') end++;
        if (*end) return NULL_VAL;
        return REAL_VAL(rv);
    }
    return NULL_VAL;
}

SnoVal string_fn(SnoVal v) {
    return STR_VAL(to_str(v));
}

/* ============================================================
 * Arithmetic / comparison
 * ============================================================ */

/* Arithmetic — promote int+int=int, otherwise real */
SnoVal add(SnoVal a, SnoVal b) {
    if (a.type == SFAIL || b.type == SFAIL) return FAIL_VAL;
    if (a.type == SINT && b.type == SINT)
        return INT_VAL(a.i + b.i);
    return REAL_VAL(to_real(a) + to_real(b));
}

SnoVal sub(SnoVal a, SnoVal b) {
    if (a.type == SFAIL || b.type == SFAIL) return FAIL_VAL;
    if (a.type == SINT && b.type == SINT)
        return INT_VAL(a.i - b.i);
    return REAL_VAL(to_real(a) - to_real(b));
}

SnoVal mul(SnoVal a, SnoVal b) {
    if (a.type == SFAIL || b.type == SFAIL) return FAIL_VAL;
    if (a.type == SINT && b.type == SINT)
        return INT_VAL(a.i * b.i);
    return REAL_VAL(to_real(a) * to_real(b));
}

SnoVal dyvide(SnoVal a, SnoVal b) {
    if (a.type == SFAIL || b.type == SFAIL) return FAIL_VAL;
    /* SNOBOL4 / is real division; integer / integer = integer in SNOBOL4 */
    if (a.type == SINT && b.type == SINT) {
        if (b.i == 0) return NULL_VAL;  /* division error */
        return INT_VAL(a.i / b.i);
    }
    double denom = to_real(b);
    if (denom == 0.0) return NULL_VAL;
    return REAL_VAL(to_real(a) / denom);
}

SnoVal powr(SnoVal a, SnoVal b) {
    if (a.type == SFAIL || b.type == SFAIL) return FAIL_VAL;
    return REAL_VAL(pow(to_real(a), to_real(b)));
}

SnoVal neg(SnoVal a) {
    if (a.type == SFAIL) return FAIL_VAL;
    if (a.type == SINT)  return INT_VAL(-a.i);
    if (a.type == SREAL) return REAL_VAL(-a.r);
    return INT_VAL(-to_int(a));
}

/* Numeric comparisons — return 1=success (true), 0=failure */
int eq(SnoVal a, SnoVal b) {
    if (a.type == SINT && b.type == SINT) return a.i == b.i;
    return to_real(a) == to_real(b);
}
int ne(SnoVal a, SnoVal b) { return !eq(a, b); }
int lt(SnoVal a, SnoVal b) {
    if (a.type == SINT && b.type == SINT) return a.i < b.i;
    return to_real(a) < to_real(b);
}
int le(SnoVal a, SnoVal b) {
    if (a.type == SINT && b.type == SINT) return a.i <= b.i;
    return to_real(a) <= to_real(b);
}
int gt(SnoVal a, SnoVal b) {
    if (a.type == SINT && b.type == SINT) return a.i > b.i;
    return to_real(a) > to_real(b);
}
int ge(SnoVal a, SnoVal b) {
    if (a.type == SINT && b.type == SINT) return a.i >= b.i;
    return to_real(a) >= to_real(b);
}

/* IDENT: succeed if a and b are identical (same type and value) */
int ident(SnoVal a, SnoVal b) {
    if (a.type != b.type) {
        /* "" and NULL are identical */
        int a_null = (a.type == SNULL || (a.type == SSTR && (!a.s || !*a.s)));
        int b_null = (b.type == SNULL || (b.type == SSTR && (!b.s || !*b.s)));
        if (a_null && b_null) return 1;
        return 0;
    }
    switch (a.type) {
        case SNULL: return 1;
        case SSTR:  return strcmp(a.s ? a.s : "", b.s ? b.s : "") == 0;
        case SINT:  return a.i == b.i;
        case SREAL: return a.r == b.r;
        case STREE: return a.t == b.t;  /* pointer identity */
        default:       return a.ptr == b.ptr;
    }
}

int differ(SnoVal a, SnoVal b) { return !ident(a, b); }

/* ============================================================
 * I/O
 * ============================================================ */

void output_val(SnoVal v) {
    char *s = to_str(v);
    printf("%s\n", s ? s : "");
}

void output_str(const char *s) {
    printf("%s\n", s ? s : "");
}

/* Current INPUT source — defaults to stdin, redirected by INPUT(name,channel,'',fileName) */
static FILE *_input_fp = NULL;
static char *_input_buf = NULL;
static size_t _input_cap = 0;

SnoVal input_read(void) {
    if (!_input_fp) _input_fp = stdin;
    ssize_t nread = getline(&_input_buf, &_input_cap, _input_fp);
    if (nread < 0) return FAIL_VAL;  /* EOF = INPUT fails */
    if (nread > 0 && _input_buf[nread-1] == '\n') _input_buf[nread-1] = '\0';
    return STR_VAL(GC_strdup(_input_buf));
}

/* INPUT(name, channel, options, fileName) — I/O association (SPITBOL-style).
 * io.sno OPSYNs the original INPUT builtin to input__ then calls it with 4 args.
 * We support the essential case: reassign INPUT source to a named file. */
static SnoVal _b_INPUT(SnoVal *a, int n) {
    const char *fname = (n >= 4) ? to_str(a[3]) : NULL;
    if (!fname || !fname[0]) {
        if (_input_fp && _input_fp != stdin) fclose(_input_fp);
        _input_fp = stdin;
        return NULL_VAL;
    }
    FILE *f = fopen(fname, "r");
    if (!f) return FAIL_VAL;
    if (_input_fp && _input_fp != stdin) fclose(_input_fp);
    _input_fp = f;
    return NULL_VAL;
}

/* Indirect goto — called when :(var) computed goto is taken.
   Currently a stub: prints a warning and continues.
   Full implementation requires a label dispatch table. */
void indirect_goto(const char *varname) {
    SnoVal v = var_get(varname);
    const char *lbl = (v.type == SSTR) ? v.s : "(nil)";
    fprintf(stderr, "indirect_goto: var=%s label=%s (not implemented)\n",
            varname, lbl);
}
