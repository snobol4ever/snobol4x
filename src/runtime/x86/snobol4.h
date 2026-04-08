/* snobol4.h — scrip-cc universal value type and runtime API
 *
 * Architecture decisions (recorded PLAN.md 2026-03-10):
 *   D1: Memory model = Boehm GC  (no ref-counting, no free() anywhere)
 *   D2: TREEBLK_t children = realloc'd dynamic array (unbounded arity)
 *   D3: cstack = thread-local MatchState* (matches snobol4csharp [ThreadStatic])
 *
 * This header defines the full SNOBOL4 value universe:
 *   DT_SNUL, S, I, R, DT_DATA,
 *   P, DT_A, T, DT_C
 */

#ifndef SNOBOL4_H
#define SNOBOL4_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gc/gc.h>

/* ============================================================
 * Value type
 * ============================================================ */

typedef enum {
    DT_SNUL =  0,   /* our null sentinel — empty/unset                    */
    DT_S    =  1,   /* STRING  — char* (GC-managed)                       */
    DT_P    =  3,   /* PATTERN — PATND_t* (GC-managed)                      */
    DT_A    =  4,   /* DT_A   — ARBLK_t* (GC-managed)                      */
    DT_T    =  5,   /* TABLE   — TBBLK_t* (GC-managed)                      */
    DT_I    =  6,   /* INTEGER — int64_t                                   */
    DT_R    =  7,   /* REAL    — double                                    */
    DT_C    =  8,   /* CODE    — compiled code block                       */
    DT_N    =  9,   /* NAME    — l-value reference                         */
    DT_K    = 10,   /* KEYWORD — protected variable                        */
    DT_E    = 11,   /* EXPRESSION — unevaluated                            */
    DT_FAIL = 99,   /* failure sentinel — drives :F branch (our invention) */
    DT_DATA = 100,  /* first user-defined DT_DATA type — v >= DT_DATA            */
} DTYPE_t;

struct _TREEBLK_t;
struct _ARBLK_t;
struct _TBBLK_t;
struct _DATINST_t;

struct _PATND_t;  /* defined in snobol4_patnd.h — included below */
typedef struct DESCR_t {
    DTYPE_t v;             /* type tag — SIL v field (DTYPE_t enum) */
    uint32_t slen;         /* binary string byte length; 0 = use strlen (fits in padding) */
    union {              /* value   — SIL a field               */
        char             *s;   /* S    — string pointer (GC)    */
        int64_t           i;   /* I    — integer                */
        double            r;   /* R    — real                   */
        struct _PATND_t    *p;   /* P    — pattern node           */
        struct _ARBLK_t    *arr; /* A    — array block            */
        struct _TBBLK_t    *tbl; /* T    — table block            */
        struct _DATINST_t  *u;   /* DT_DATA — user DT_DATA instance     */
        void             *ptr; /* generic GC pointer            */
    };
} DESCR_t;

#include "snobol4_patnd.h"  /* XKIND_t + PATND_t — requires DESCR_t */

/* descr_slen: byte length of a string descriptor.
 * If slen field is nonzero, use it (binary string with possible embedded NULs).
 * Otherwise fall back to strlen — correct for all normal NUL-terminated strings. */
static inline size_t descr_slen(DESCR_t d) {
    /* Only honour slen for string types; for all others use strlen/0 */
    if (d.v == DT_S) {
        if (d.slen) return (size_t)d.slen;
        return d.s ? strlen(d.s) : 0;
    }
    /* Non-string: convert to string representation then measure */
    return 0;
}

#define NULVCL    ((DESCR_t){ .v = DT_SNUL, .slen = 0, .s = "" })
#define STRVAL(s_) ((DESCR_t){ .v = DT_S,  .slen = 0, .s = (s_) })
/* BSTRVAL: binary string with explicit byte length (may contain embedded NULs) */
#define BSTRVAL(s_, len_) ((DESCR_t){ .v = DT_S, .slen = (uint32_t)(len_), .s = (s_) })
#define INTVAL(i_) ((DESCR_t){ .v = DT_I,  .i = (i_) })
#define REALVAL(r_)((DESCR_t){ .v = DT_R, .r = (r_) })
#define FAILDESCR    ((DESCR_t){ .v = DT_FAIL, .i = 0 })   /* P002/P003 */
/* NAME descriptor — SIL semantics: value field is DESCR_t* pointing to the live cell.
 * Read:  if (d.v==DT_N) return *d.ptr;
 * Write: if (d.v==DT_N) *d.ptr = val;
 * Mirrors SIL ARYA10/ASSCR/FIELD: SETVC XPTR,N keeps interior pointer as value. */
#define NAMEPTR(dp_) ((DESCR_t){ .v = DT_N, .slen = 1, .ptr = (void*)(dp_) })  /* interior ptr: slen=1 */
/* Legacy string-name compat — do not use for new code */
#define NAMEVAL(s_)  ((DESCR_t){ .v = DT_N, .slen = 0, .s = (char *)(s_) })  /* name string: slen=0 */
#define STYPE(v_)    ((v_).v)

static inline int IS_FAIL_fn(DESCR_t v) { return v.v == DT_FAIL; }

/* ============================================================
 * String operations
 * ============================================================ */

/* All strings are GC-managed, null-terminated char*.
 * "" is the canonical NULL/empty value.  */

static inline int IS_NULL_fn(DESCR_t v)  { return v.v == DT_SNUL || (v.v == DT_S && (!v.s || !*v.s)); }
static inline int IS_STR_fn(DESCR_t v)   { return v.v == DT_S || v.v == DT_SNUL; }
static inline int IS_INT_fn(DESCR_t v)   { return v.v == DT_I; }
static inline int IS_REAL_fn(DESCR_t v)  { return v.v == DT_R; }
static inline int IS_DATA_fn(DESCR_t v)  { return v.v == DT_DATA; }

/* Convert any value to string (GC-managed) */
char *VARVAL_fn(DESCR_t v);

/* ── RT-1: SIL-faithful INVOKE / ARGVAL (invoke.c) ── */
DESCR_t INVOKE_fn(const char *name, DESCR_t *args, int nargs); /* SIL INVOKE */
DESCR_t ARGVAL_fn(DESCR_t d);       /* SIL ARGVAL — evaluate one argument    */
DESCR_t VARVAL_d_fn(DESCR_t d);     /* SIL VARVAL — coerce to STRING         */
DESCR_t INTVAL_fn(DESCR_t d);       /* SIL INTVAL — coerce to INTEGER        */
DESCR_t PATVAL_fn(DESCR_t d);       /* SIL PATVAL — coerce to PATTERN        */
DESCR_t VARVUP_fn(DESCR_t d);       /* SIL VARVUP — coerce to uppercase STR  */
extern int64_t kw_case;             /* &CASE keyword (0=fold, non-0=sensitive)*/

/* Convert any value to integer (0 on failure) */
int64_t to_int(DESCR_t v);

/* Convert any value to real (0.0 on failure) */
double to_real(DESCR_t v);

/* String concatenation — GC-managed result */
char *STRCONCAT_fn(const char *a, const char *b);
DESCR_t CONCAT_fn(DESCR_t a, DESCR_t b);  /* P003: DT_FAIL-propagating CONCAT_fn */

/* String duplication */
char *STRDUP_fn(const char *s);

/* String size in characters */
int64_t size(const char *s);

/* DATATYPE function */
const char *datatype(DESCR_t v);

/* ============================================================
 * TREEBLK_t (Beautiful.sno's core data structure)
 * ============================================================
 *
 * DT_DATA('tree(t,v,n,c)') — tag, value, child count, children array
 * Children: realloc'd dynamic array (D2 — unbounded arity)
 */

typedef struct _TREEBLK_t {
    char   *tag;        /* type tag string   — t(x) */
    DESCR_t  val;        /* leaf value        — v(x) */
    int     n;          /* child count       — n(x) */
    int     cap;        /* children capacity */
    struct _TREEBLK_t **c;   /* children array    — c(x) */
} TREEBLK_t;

TREEBLK_t *tree_new(const char *tag, DESCR_t val);
TREEBLK_t *tree_new0(const char *tag);     /* tag only, null val, no children */
void  tree_append(TREEBLK_t *x, TREEBLK_t *y);  /* Append(x,y)  */
void  tree_prepend(TREEBLK_t *x, TREEBLK_t *y); /* Prepend(x,y) */
void  tree_insert(TREEBLK_t *x, TREEBLK_t *y, int place); /* Insert(x,y,place) 1-based */
TREEBLK_t *tree_remove(TREEBLK_t *x, int place);           /* Remove(x,place)   1-based */

/* Accessors (SNOBOL4 field functions for DT_DATA('tree')) */
static inline const char *t(TREEBLK_t *x) { return x ? x->tag : ""; }
static inline DESCR_t      v(TREEBLK_t *x) { return x ? x->val  : NULVCL; }
static inline int         n(TREEBLK_t *x) { return x ? x->n    : 0; }
static inline TREEBLK_t       *c_i(TREEBLK_t *x, int i) {  /* c(x)[i], 1-based */
    if (!x || i < 1 || i > x->n) return NULL;
    return x->c[i-1];
}

/* ============================================================
 * Array
 * ============================================================ */

typedef struct _ARBLK_t {
    int     lo, hi;      /* ARRAY('lo:hi') bounds, dim 1 */
    int     ndim;        /* number of dimensions (1 or 2) */
    int     lo2, hi2;   /* dim 2 bounds (ndim==2 only); cols = hi2-lo2+1 */
    DESCR_t *data;        /* lo..hi, 0-based offset by lo */
} ARBLK_t;

ARBLK_t *array_new(int lo, int hi);
ARBLK_t *array_new2d(int lo1, int hi1, int lo2, int hi2);
DESCR_t    array_get(ARBLK_t *a, int i);       /* 1-based */
void      array_set(ARBLK_t *a, int i, DESCR_t v);
DESCR_t    array_get2(ARBLK_t *a, int i, int j);
void      array_set2(ARBLK_t *a, int i, int j, DESCR_t v);

/* ============================================================
 * Table (hash map)
 * ============================================================ */

typedef struct _TBBLK_tEntry {
    char   *key;
    DESCR_t  key_descr;  /* original key descriptor — preserves integer/string type */
    DESCR_t  val;
    struct _TBBLK_tEntry *next;
} TBPAIR_t;

#define TABLE_BUCKETS 256

typedef struct _TBBLK_t {
    TBPAIR_t *buckets[TABLE_BUCKETS];
    int            size;
    int            init, inc;   /* constructor args for stringify: TABLE(init,inc) */
} TBBLK_t;

TBBLK_t *table_new(void);
TBBLK_t *table_new_args(int init, int inc);
DESCR_t    table_get(TBBLK_t *tbl, const char *key);
void      table_set(TBBLK_t *tbl, const char *key, DESCR_t val);
void      table_set_descr(TBBLK_t *tbl, const char *key, DESCR_t key_d, DESCR_t val);
int       table_has(TBBLK_t *tbl, const char *key);

/* ============================================================
 * User-defined datatypes (DT_DATA() mechanism)
 * ============================================================ */

typedef struct _DATINST_tType {
    char   *name;
    int     nfields;
    char  **fields;
    struct _DATINST_tType *next;
} DATBLK_t;

typedef struct _DATINST_t {
    DATBLK_t *type;
    DESCR_t   *fields;  /* GC-managed array of nfields values */
} DATINST_t;

/* Register a DT_DATA() definition: DT_DATA('tree(t,v,n,c)') */
void DEFDAT_fn(const char *spec);

/* Allocate a new instance of a user-defined type */
DESCR_t DATCON_fn(const char *typename, ...);  /* varargs: field values */

/* Get/set field by name */
DESCR_t FIELD_GET_fn(DESCR_t obj, const char *field);
void    FIELD_SET_fn(DESCR_t obj, const char *field, DESCR_t val);
void   FIELD_SET_fn(DESCR_t obj, const char *field, DESCR_t val);

/* ============================================================
 * Variable table (global variables, $name indirect access)
 * ============================================================ */

DESCR_t  NV_GET_fn(const char *name);
DESCR_t  NV_SET_fn(const char *name, DESCR_t val);  /* RT-5: returns val for embedded assignment */
void    NV_REG_fn(const char *name, DESCR_t *ptr);
void    NV_SYNC_fn(void);
void    NV_CLEAR_fn(void);      /* reset all non-keyword NV vars to null */
DESCR_t  INDR_GET_fn(const char *name);  /* $name */
void    INDR_SET_fn(const char *name, DESCR_t val);

/* SIL NAME proc: .X — return DT_N descriptor for variable/keyword name.
 * Returns NAMEVAL (slen=0, GC-safe name string) for ordinary variables and
 * keywords; NAMEPTR (slen=1, interior ptr) for addressable NV cells.
 * Keywords are not addressable via interior pointer — returns NAMEVAL. */
DESCR_t  NAME_fn(const char *varname);

/* SIL ASGNIC: keyword assignment — coerce val to INTEGER then store.
 * Returns 1 on success, 0 if varname is not a known keyword (caller
 * should fall back to NV_SET_fn for ordinary variables). */
int      ASGNIC_fn(const char *kw_name, DESCR_t val);

/* ── RT-4: SIL Naming List (§NMD) — nmd.c ──────────────────────────────── */
/* NAM_push: record a conditional (.) capture during pattern match.
 *   var    — NV variable name (DT_S target); NULL if ptr is used
 *   ptr    — DT_N interior pointer target; NULL if var is used
 *   dt     — DT_S / DT_K / DT_E (dispatch selector for commit)
 *   s      — matched substring start (in subject buffer)
 *   len    — matched substring length                                       */
void    NAM_push(const char *var, DESCR_t *ptr, int dt,
                 const char *s, int len);

/* NAM_save: snapshot current naming-list top; returns opaque cookie.        */
int     NAM_save(void);

/* NAM_commit: on pattern success — assign all entries since cookie.
 *   DT_K  → ASGNIC_fn (coerce to INTEGER per SIL NMDIC)
 *   DT_E  → stub (NAMEXN / EXPEVL — implemented in RT-6)
 *   else  → NV_SET_fn or interior-pointer write                             */
void    NAM_commit(int cookie);

/* NAM_discard: mid-scan reset — clear current frame entries, keep frame.    */
void    NAM_discard(int cookie);
/* NAM_pop: pop frame after final failure (call once after NAM_discard).     */
void    NAM_pop(int cookie);

/* ============================================================
 * Counter stack (nPush/nInc/nDec/nTop/nPop)
 * ============================================================ */

void    NPUSH_fn(void);
int     NHAS_FRAME_fn(void);  /* 1 if counter stack is non-empty */
int     NTOP_INDEX_fn(void);  /* return current _ntop index (-1 if empty) */
int64_t NSTACK_AT_fn(int frame); /* read _nstack[frame] safely */
void    NINC_fn(void);
void    NINC_AT_fn(int frame);
void    NDEC_fn(void);
int64_t ntop(void);
void    NPOP_fn(void);

/* ============================================================
 * Value stack (Push/Pop/Top for Shift/Reduce)
 * ============================================================ */

void   PUSH_fn(DESCR_t v);
DESCR_t POP_fn(void);
DESCR_t TOP_fn(void);
int    STACK_DEPTH_fn(void);

/* ============================================================
 * Function table (DEFINE_fn/APPLY)
 * ============================================================ */

typedef DESCR_t (*FNCPTR_t)(DESCR_t *args, int nargs);

void    DEFINE_fn(const char *spec, FNCPTR_t fn);  /* DEFINE_fn('name(a,b)local1,local2') */
void    DEFINE_fn_entry(const char *spec, FNCPTR_t fn, const char *entry_label);
void    register_fn_alias(const char *newname, const char *oldname); /* OPSYN alias */
DESCR_t  APPLY_fn(const char *name, DESCR_t *args, int nargs);  /* APPLY(name,...) */
int     FNCEX_fn(const char *name);
// Source-case param/local accessors (scrip-interp; NV store is case-sensitive)
int         FUNC_NPARAMS_fn(const char *fname);
int         FUNC_NLOCALS_fn(const char *fname);
const char *FUNC_PARAM_fn(const char *fname, int i);
const char *FUNC_LOCAL_fn(const char *fname, int i);
const char *FUNC_ENTRY_fn(const char *fname);

/* ============================================================
 * GOTO / label runtime
 * ============================================================ */

/* Return codes for statement execution */
#define RETCODE_SUCCEED  0
#define RETCODE_CONCEDE  1
#define SGOTO     2   /* branch to specific label */
#define SRETURN   3   /* return from function */
#define FRETURN  4   /* failure return from function */
#define NRETURN  5   /* no-value return */
#define RETCODE_END  6   /* END of program */

/* ============================================================
 * Builtin functions (string operations)
 * ============================================================ */

DESCR_t SIZE_fn(DESCR_t s);                        /* SIZE_fn(s) */
DESCR_t DUPL_fn(DESCR_t s, DESCR_t n);              /* DUPL_fn(s,n) */
DESCR_t REPLACE_fn(DESCR_t s, DESCR_t from, DESCR_t to); /* REPLACE(s,f,t) */
DESCR_t SUBSTR_fn(DESCR_t s, DESCR_t i, DESCR_t n);  /* SUBSTR_fn(s,i,n) */
DESCR_t TRIM_fn(DESCR_t s);                        /* TRIM_fn(s) */
DESCR_t lpad_fn(DESCR_t s, DESCR_t n, DESCR_t pad);  /* LPAD(s,n,pad) */
DESCR_t rpad_fn(DESCR_t s, DESCR_t n, DESCR_t pad);  /* RPAD(s,n,pad) */
DESCR_t REVERS_fn(DESCR_t s);                     /* REVERSE(s) */
DESCR_t BCHAR_fn(DESCR_t n);                        /* CHAR(n) */
DESCR_t INTGER_fn(DESCR_t v);                     /* INTEGER(v) */
DESCR_t real_fn(DESCR_t v);                        /* REAL(v) */
DESCR_t string_fn(DESCR_t v);                      /* STRING(v) */

/* ============================================================
 * Arithmetic / comparison predicates
 * ============================================================ */

int eq(DESCR_t a, DESCR_t b);   /* EQ: numeric equal — succeeds or fails */
int ne(DESCR_t a, DESCR_t b);
int lt(DESCR_t a, DESCR_t b);
int le(DESCR_t a, DESCR_t b);
int gt(DESCR_t a, DESCR_t b);
int ge(DESCR_t a, DESCR_t b);

int ident(DESCR_t a, DESCR_t b); /* IDENT: string/value identical */
int differ(DESCR_t a, DESCR_t b);/* DIFFER: string/value different */

DESCR_t add(DESCR_t a, DESCR_t b);
DESCR_t sub(DESCR_t a, DESCR_t b);
DESCR_t mul(DESCR_t a, DESCR_t b);
DESCR_t DIVIDE_fn(DESCR_t a, DESCR_t b);
DESCR_t POWER_fn(DESCR_t a, DESCR_t b);
DESCR_t neg(DESCR_t a);
DESCR_t pos(DESCR_t a);

/* ============================================================
 * I/O
 * ============================================================ */

void   output_val(DESCR_t v);        /* OUTPUT = v */
DESCR_t input_read(void);            /* v = INPUT */
void   output_str(const char *s);   /* OUTPUT = 'string' */

/* COMM — monitor telemetry */
extern int monitor_fd;
void comm_stno(int n);
void comm_var(const char *name, DESCR_t val);

/* ============================================================
 * SNOBOL4 keywords (&KEYWORD)
 * ============================================================ */

extern int64_t kw_fullscan;
extern int64_t kw_maxlngth;
extern int64_t kw_anchor;
extern int64_t kw_trim;
extern int64_t kw_stlimit;
extern int64_t kw_stcount;   /* &STCOUNT — incremented every statement */
extern int64_t kw_ftrace;    /* &FTRACE   - function trace counter */
extern int64_t kw_errlimit;  /* &ERRLIMIT - max compile errors */
extern int64_t kw_code;      /* &CODE     - program exit code */
extern int64_t kw_fnclevel;  /* &FNCLEVEL - function nesting depth */
extern char    kw_rtntype[16]; /* &RTNTYPE  - RETURN/FRETURN/NRETURN */

/* GAP 4 - runtime error infrastructure
 * Mirrors v311.sil error routing:
 *   SNO_ERR_TERMINAL: codes 20,21,22,23,26,27,29,30,31,39 → FTLEND (exit immediately)
 *   SNO_ERR_FATAL:    codes 19,24,25,35                   → FTLERR (exit, respects &FATALLIMIT)
 *   SNO_ERR_SOFT:     everything else                     → FTLTST (longjmp, :F catchable)
 */
#include <setjmp.h>
void sno_runtime_error(int code, const char *msg);
extern int g_kw_ctx;  /* set to 1 when NV_SET_fn called from &KW = context */
extern jmp_buf g_sno_err_jmp;
extern int     g_sno_err_active;
extern int     g_sno_err_stmt;

static inline int sno_err_is_terminal(int code) {
    switch (code) {
        case 20: case 21: case 22: case 23:   /* storage/stack/stlimit/size */
        case 26: case 27: case 29: case 30:   /* compile-limit/end/include  */
        case 31: case 39:                     /* line-stmt / cant-continue  */
            return 1;
        default: return 0;
    }
}
static inline int sno_err_is_fatal(int code) {
    switch (code) {
        case 19: case 24: case 25: case 35:   /* fail-goto/bad-goto/args/no-setexit */
            return 1;
        default: return 0;
    }
}

/* Global character sets */
extern char ucase[27];   /* &UCASE */
extern char lcase[27];   /* &LCASE */
extern char alphabet[257]; /* &ALPHABET */
extern char digits[11];  /* digits constant from global.inc */

/* ============================================================
 * Runtime initialization
 * ============================================================ */

void SNO_INIT_fn(void);  /* call once at program start */

/* ============================================================
 * Pattern constructors (snobol4_pattern.c)
 * ============================================================ */

DESCR_t pat_lit(const char *s);
DESCR_t pat_span(const char *chars);
DESCR_t pat_break_(const char *chars);
DESCR_t pat_any_cs(const char *chars);
DESCR_t pat_notany(const char *chars);
DESCR_t pat_len(int64_t n);
DESCR_t pat_pos(int64_t n);
DESCR_t pat_rpos(int64_t n);
DESCR_t pat_tab(int64_t n);
DESCR_t pat_rtab(int64_t n);
DESCR_t pat_arb(void);
DESCR_t pat_arbno(DESCR_t inner);
DESCR_t pat_rem(void);
DESCR_t pat_fence(void);
DESCR_t pat_fence_p(DESCR_t inner);
DESCR_t pat_fail(void);
DESCR_t pat_abort(void);
DESCR_t pat_succeed(void);
DESCR_t pat_bal(void);
DESCR_t pat_epsilon(void);
DESCR_t pat_cat(DESCR_t left, DESCR_t right);
DESCR_t pat_alt(DESCR_t left, DESCR_t right);
DESCR_t pat_ref(const char *name);
DESCR_t pat_ref_val(DESCR_t nameVal);
DESCR_t pat_assign_imm(DESCR_t child, DESCR_t var);
DESCR_t pat_assign_cond(DESCR_t child, DESCR_t var);
DESCR_t pat_assign_callcap(DESCR_t child, const char *fnc_name, DESCR_t *args, int nargs);
DESCR_t var_as_pattern(DESCR_t v);
DESCR_t pat_user_call(const char *name, DESCR_t *args, int nargs);

/* Pattern matching */
int  match_pattern(DESCR_t pat, const char *subject);
int  match_pattern_at(DESCR_t pat, const char *subject, int subj_len, int cursor);
int  match_and_replace(DESCR_t *subject, DESCR_t pat, DESCR_t replacement);

/* ============================================================
 * Array/Table/TREEBLK_t DESCR_t-level API (snobol4_pattern.c)
 * ============================================================ */

DESCR_t array_create(DESCR_t spec);            /* ARRAY('lo:hi') */
DESCR_t subscript_get(DESCR_t arr, DESCR_t idx);
DESCR_t *NV_PTR_fn(const char *name);       /* find-or-create NV cell, return &val */
const char *NV_name_from_ptr(const DESCR_t *ptr); /* reverse-lookup &val -> name */
extern DESCR_t (*g_eval_pat_hook)(DESCR_t pat);   /* EVAL_fn hook for DT_P patterns */
DESCR_t *array_ptr(ARBLK_t *a, int i);      /* interior pointer to array cell (NULL=OOB) */
DESCR_t *table_ptr(TBBLK_t *tbl, DESCR_t key_d); /* find-or-create table cell ptr */

/* Hook for interpreter to supply user-function dispatch to the pattern engine.
 * Set by scrip-interp.c main() before running any program.
 * Signature: fn(name, args, nargs) → DESCR_t result. */
extern DESCR_t (*g_user_call_hook)(const char *name, DESCR_t *args, int nargs);
void   subscript_set(DESCR_t arr, DESCR_t idx, DESCR_t val);
DESCR_t subscript_get2(DESCR_t arr, DESCR_t i, DESCR_t j);
void   subscript_set2(DESCR_t arr, DESCR_t i, DESCR_t j, DESCR_t val);
DESCR_t MAKE_TREE_fn(DESCR_t tag, DESCR_t val, DESCR_t n, DESCR_t children);

/* Value stack aliases */
DESCR_t push_val(DESCR_t x);
DESCR_t pop_val(void);
DESCR_t top_val(void);
int    val_stack_depth(void);

/* Function registration */
void   register_fn(const char *name, DESCR_t (*fn)(DESCR_t*, int), int min_args, int max_args);
void   define_spec(DESCR_t spec);
DESCR_t apply_val(DESCR_t fnval, DESCR_t *args, int nargs);
DESCR_t EVAL_fn(DESCR_t expr);
DESCR_t compile_to_expression(const char *src); /* SIL CONVE: parse→DT_E, no eval */
DESCR_t opsyn(DESCR_t newname, DESCR_t oldname, DESCR_t type);
/* 2-arg convenience — type defaults to NULVCL */
static inline DESCR_t opsyn2(DESCR_t a, DESCR_t b) { return opsyn(a, b, NULVCL); }
DESCR_t sort_fn(DESCR_t arr);
DESCR_t rsort_fn(DESCR_t arr);
void    sno_set_label_exists_hook(int (*fn)(const char *));
const char *setexit_label_get(void);

/* TABLE_VAL macro */
#define TABLE_VAL(tbl_) ((DESCR_t){ .v = DT_T, .tbl = (tbl_) })
#define ARRAY_VAL(a_)   ((DESCR_t){ .v = DT_A, .arr = (a_)   })

/* ============================================================
 * Pattern matching interface (matches engine_runtime.h)
 * ============================================================ */

#include "engine_runtime.h"

#endif /* SNOBOL4_H */
void indirect_goto(const char *varname);
DESCR_t pat_call(const char *name, DESCR_t arg);

extern int _x4_pending_parent_frame;
extern int _command_pending_parent_frame;
