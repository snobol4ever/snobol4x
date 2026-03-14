/* snobol4.h — SNOBOL4-tiny universal value type and runtime API
 *
 * Architecture decisions (recorded PLAN.md 2026-03-10):
 *   D1: Memory model = Boehm GC  (no ref-counting, no free() anywhere)
 *   D2: Tree children = realloc'd dynamic array (unbounded arity)
 *   D3: cstack = thread-local MatchState* (matches SNOBOL4-csharp [ThreadStatic])
 *
 * This header defines the full SNOBOL4 value universe:
 *   SNULL, SSTR, SINT, SREAL, STREE,
 *   SPATTERN, ARRAY, STABLE, CODE
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
    SNULL    = 0,   /* unset / empty string / NULL */
    SSTR     = 1,   /* char * (GC-managed) */
    SINT     = 2,   /* int64_t */
    SREAL    = 3,   /* double */
    STREE    = 4,   /* Tree* (GC-managed) */
    SPATTERN = 5,   /* Pattern* (GC-managed) */
    ARRAY   = 6,   /* SnoArray* (GC-managed) */
    STABLE   = 7,   /* SnoTable* (GC-managed) */
    CODE    = 8,   /* compiled code block */
    UDEF    = 9,   /* user-defined datatype instance */
    SFAIL    = 10,  /* P002/P003: failure sentinel — propagates :F branch */
} SnoType;

struct _Tree;
struct _Pattern;
struct _SnoArray;
struct _SnoTable;
struct _UDef;

typedef struct SnoVal {
    SnoType type;
    union {
        char             *s;   /* SSTR, SNULL (empty string = "") */
        int64_t           i;   /* SINT  */
        double            r;   /* SREAL */
        struct _Tree     *t;   /* STREE */
        struct _Pattern  *p;   /* SPATTERN */
        struct _SnoArray *a;   /* ARRAY */
        struct _SnoTable *tbl; /* STABLE */
        struct _UDef     *u;   /* UDEF */
        void             *ptr; /* generic GC pointer */
    };
} SnoVal;

#define NULL_VAL    ((SnoVal){ .type = SNULL, .s = "" })
#define STR_VAL(s_) ((SnoVal){ .type = SSTR,  .s = (s_) })
#define INT_VAL(i_) ((SnoVal){ .type = SINT,  .i = (i_) })
#define REAL_VAL(r_)((SnoVal){ .type = SREAL, .r = (r_) })
#define TREE_VAL(t_)((SnoVal){ .type = STREE, .t = (t_) })
#define FAIL_VAL    ((SnoVal){ .type = SFAIL, .i = 0 })   /* P002/P003 */
#define STYPE(v_)    ((v_).type)

static inline int is_fail(SnoVal v) { return v.type == SFAIL; }

/* ============================================================
 * String operations
 * ============================================================ */

/* All strings are GC-managed, null-terminated char*.
 * "" is the canonical NULL/empty value.  */

static inline int is_null(SnoVal v)  { return v.type == SNULL || (v.type == SSTR && (!v.s || !*v.s)); }
static inline int is_str(SnoVal v)   { return v.type == SSTR || v.type == SNULL; }
static inline int is_int(SnoVal v)   { return v.type == SINT; }
static inline int is_real(SnoVal v)  { return v.type == SREAL; }
static inline int is_tree(SnoVal v)  { return v.type == STREE; }
static inline int is_udef(SnoVal v)  { return v.type == UDEF; }

/* Convert any value to string (GC-managed) */
char *to_str(SnoVal v);

/* Convert any value to integer (0 on failure) */
int64_t to_int(SnoVal v);

/* Convert any value to real (0.0 on failure) */
double to_real(SnoVal v);

/* String concatenation — GC-managed result */
char *ccat(const char *a, const char *b);
SnoVal concat_sv(SnoVal a, SnoVal b);  /* P003: FAIL-propagating ccat */

/* String duplication */
char *dupl(const char *s);

/* String size in characters */
int64_t size(const char *s);

/* DATATYPE function */
const char *datatype(SnoVal v);

/* ============================================================
 * Tree (Beautiful.sno's core data structure)
 * ============================================================
 *
 * DATA('tree(t,v,n,c)') — tag, value, child count, children array
 * Children: realloc'd dynamic array (D2 — unbounded arity)
 */

typedef struct _Tree {
    char   *tag;        /* type tag string   — t(x) */
    SnoVal  val;        /* leaf value        — v(x) */
    int     n;          /* child count       — n(x) */
    int     cap;        /* children capacity */
    struct _Tree **c;   /* children array    — c(x) */
} Tree;

Tree *tree_new(const char *tag, SnoVal val);
Tree *tree_new0(const char *tag);     /* tag only, null val, no children */
void  tree_append(Tree *x, Tree *y);  /* Append(x,y)  */
void  tree_prepend(Tree *x, Tree *y); /* Prepend(x,y) */
void  tree_insert(Tree *x, Tree *y, int place); /* Insert(x,y,place) 1-based */
Tree *tree_remove(Tree *x, int place);           /* Remove(x,place)   1-based */

/* Accessors (SNOBOL4 field functions for DATA('tree')) */
static inline const char *t(Tree *x) { return x ? x->tag : ""; }
static inline SnoVal      v(Tree *x) { return x ? x->val  : NULL_VAL; }
static inline int         n(Tree *x) { return x ? x->n    : 0; }
static inline Tree       *c_i(Tree *x, int i) {  /* c(x)[i], 1-based */
    if (!x || i < 1 || i > x->n) return NULL;
    return x->c[i-1];
}

/* ============================================================
 * Array
 * ============================================================ */

typedef struct _SnoArray {
    int     lo, hi;      /* ARRAY('lo:hi') bounds */
    int     ndim;        /* number of dimensions */
    SnoVal *data;        /* lo..hi, 0-based offset by lo */
} SnoArray;

SnoArray *array_new(int lo, int hi);
SnoArray *array_new2d(int lo1, int hi1, int lo2, int hi2);
SnoVal    array_get(SnoArray *a, int i);       /* 1-based */
void      array_set(SnoArray *a, int i, SnoVal v);
SnoVal    array_get2(SnoArray *a, int i, int j);
void      array_set2(SnoArray *a, int i, int j, SnoVal v);

/* ============================================================
 * Table (hash map)
 * ============================================================ */

typedef struct _SnoTableEntry {
    char   *key;
    SnoVal  val;
    struct _SnoTableEntry *next;
} SnoTableEntry;

#define TABLE_BUCKETS 256

typedef struct _SnoTable {
    SnoTableEntry *buckets[TABLE_BUCKETS];
    int            size;
} SnoTable;

SnoTable *table_new(void);
SnoVal    table_get(SnoTable *tbl, const char *key);
void      table_set(SnoTable *tbl, const char *key, SnoVal val);
int       table_has(SnoTable *tbl, const char *key);

/* ============================================================
 * User-defined datatypes (DATA() mechanism)
 * ============================================================ */

typedef struct _UDefType {
    char   *name;
    int     nfields;
    char  **fields;
    struct _UDefType *next;
} UDefType;

typedef struct _UDef {
    UDefType *type;
    SnoVal   *fields;  /* GC-managed array of nfields values */
} UDef;

/* Register a DATA() definition: DATA('tree(t,v,n,c)') */
void data_define(const char *spec);

/* Allocate a new instance of a user-defined type */
SnoVal udef_new(const char *typename, ...);  /* varargs: field values */

/* Get/set field by name */
SnoVal field_get(SnoVal obj, const char *field);
void   field_set(SnoVal obj, const char *field, SnoVal val);

/* ============================================================
 * Variable table (global variables, $name indirect access)
 * ============================================================ */

SnoVal  var_get(const char *name);
void    var_set(const char *name, SnoVal val);
void    var_register(const char *name, SnoVal *ptr);
void    var_sync_registered(void);
SnoVal  indirect_get(const char *name);  /* $name */
void    indirect_set(const char *name, SnoVal val);

/* ============================================================
 * Counter stack (nPush/nInc/nDec/nTop/nPop)
 * ============================================================ */

void    npush(void);
void    ninc(void);
void    ndec(void);
int64_t ntop(void);
void    npop(void);

/* ============================================================
 * Value stack (Push/Pop/Top for Shift/Reduce)
 * ============================================================ */

void   push(SnoVal v);
SnoVal pop(void);
SnoVal top(void);
int    stack_depth(void);

/* ============================================================
 * Function table (DEFINE/APPLY)
 * ============================================================ */

typedef SnoVal (*SnoFunc)(SnoVal *args, int nargs);

void    define(const char *spec, SnoFunc fn);  /* DEFINE('name(a,b)local1,local2') */
SnoVal  aply(const char *name, SnoVal *args, int nargs);  /* APPLY(name,...) */
int     func_exists(const char *name);

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

SnoVal size_fn(SnoVal s);                        /* SIZE(s) */
SnoVal dupl_fn(SnoVal s, SnoVal n);              /* DUPL(s,n) */
SnoVal replace_fn(SnoVal s, SnoVal from, SnoVal to); /* REPLACE(s,f,t) */
SnoVal substr_fn(SnoVal s, SnoVal i, SnoVal n);  /* SUBSTR(s,i,n) */
SnoVal trim_fn(SnoVal s);                        /* TRIM(s) */
SnoVal lpad_fn(SnoVal s, SnoVal n, SnoVal pad);  /* LPAD(s,n,pad) */
SnoVal rpad_fn(SnoVal s, SnoVal n, SnoVal pad);  /* RPAD(s,n,pad) */
SnoVal reverse_fn(SnoVal s);                     /* REVERSE(s) */
SnoVal char_fn(SnoVal n);                        /* CHAR(n) */
SnoVal integer_fn(SnoVal v);                     /* INTEGER(v) */
SnoVal real_fn(SnoVal v);                        /* REAL(v) */
SnoVal string_fn(SnoVal v);                      /* STRING(v) */

/* ============================================================
 * Arithmetic / comparison predicates
 * ============================================================ */

int eq(SnoVal a, SnoVal b);   /* EQ: numeric equal — succeeds or fails */
int ne(SnoVal a, SnoVal b);
int lt(SnoVal a, SnoVal b);
int le(SnoVal a, SnoVal b);
int gt(SnoVal a, SnoVal b);
int ge(SnoVal a, SnoVal b);

int ident(SnoVal a, SnoVal b); /* IDENT: string/value identical */
int differ(SnoVal a, SnoVal b);/* DIFFER: string/value different */

SnoVal add(SnoVal a, SnoVal b);
SnoVal sub(SnoVal a, SnoVal b);
SnoVal mul(SnoVal a, SnoVal b);
SnoVal dyvide(SnoVal a, SnoVal b);
SnoVal powr(SnoVal a, SnoVal b);
SnoVal neg(SnoVal a);

/* ============================================================
 * I/O
 * ============================================================ */

void   output_val(SnoVal v);        /* OUTPUT = v */
SnoVal input_read(void);            /* v = INPUT */
void   output_str(const char *s);   /* OUTPUT = 'string' */

/* COMM — monitor telemetry */
extern int monitor_fd;
void comm_stno(int n);
void comm_var(const char *name, SnoVal val);

/* ============================================================
 * SNOBOL4 keywords (&KEYWORD)
 * ============================================================ */

extern int64_t kw_fullscan;
extern int64_t kw_maxlngth;
extern int64_t kw_anchor;
extern int64_t kw_trim;
extern int64_t kw_stlimit;
extern int64_t kw_stcount;   /* &STCOUNT — incremented every statement */

/* Global character sets */
extern char ucase[27];   /* &UCASE */
extern char lcase[27];   /* &LCASE */
extern char alphabet[257]; /* &ALPHABET */
extern char digits[11];  /* digits constant from global.inc */

/* ============================================================
 * Runtime initialization
 * ============================================================ */

void runtime_init(void);  /* call once at program start */

/* ============================================================
 * Pattern constructors (snobol4_pattern.c)
 * ============================================================ */

SnoVal pat_lit(const char *s);
SnoVal pat_span(const char *chars);
SnoVal pat_break_(const char *chars);
SnoVal pat_any_cs(const char *chars);
SnoVal pat_notany(const char *chars);
SnoVal pat_len(int64_t n);
SnoVal pat_pos(int64_t n);
SnoVal pat_rpos(int64_t n);
SnoVal pat_tab(int64_t n);
SnoVal pat_rtab(int64_t n);
SnoVal pat_arb(void);
SnoVal pat_arbno(SnoVal inner);
SnoVal pat_rem(void);
SnoVal pat_fence(void);
SnoVal pat_fence_p(SnoVal inner);
SnoVal pat_fail(void);
SnoVal pat_abort(void);
SnoVal pat_succeed(void);
SnoVal pat_bal(void);
SnoVal pat_epsilon(void);
SnoVal pat_cat(SnoVal left, SnoVal right);
SnoVal pat_alt(SnoVal left, SnoVal right);
SnoVal pat_ref(const char *name);
SnoVal pat_ref_val(SnoVal nameVal);
SnoVal pat_assign_imm(SnoVal child, SnoVal var);
SnoVal pat_assign_cond(SnoVal child, SnoVal var);
SnoVal var_as_pattern(SnoVal v);
SnoVal pat_user_call(const char *name, SnoVal *args, int nargs);

/* Pattern matching */
int  match_pattern(SnoVal pat, const char *subject);
int  match_pattern_at(SnoVal pat, const char *subject, int subj_len, int cursor);
int  match_and_replace(SnoVal *subject, SnoVal pat, SnoVal replacement);

/* ============================================================
 * Array/Table/Tree SnoVal-level API (snobol4_pattern.c)
 * ============================================================ */

SnoVal array_create(SnoVal spec);            /* ARRAY('lo:hi') */
SnoVal subscript_get(SnoVal arr, SnoVal idx);
void   subscript_set(SnoVal arr, SnoVal idx, SnoVal val);
SnoVal subscript_get2(SnoVal arr, SnoVal i, SnoVal j);
void   subscript_set2(SnoVal arr, SnoVal i, SnoVal j, SnoVal val);
SnoVal make_tree(SnoVal tag, SnoVal val, SnoVal n, SnoVal children);

/* Value stack aliases */
SnoVal push_val(SnoVal x);
SnoVal pop_val(void);
SnoVal top_val(void);

/* Function registration */
void   register_fn(const char *name, SnoVal (*fn)(SnoVal*, int), int min_args, int max_args);
void   define_spec(SnoVal spec);
SnoVal apply_val(SnoVal fnval, SnoVal *args, int nargs);
SnoVal evl(SnoVal expr);
SnoVal opsyn(SnoVal newname, SnoVal oldname, SnoVal type);
/* 2-arg convenience — type defaults to NULL_VAL */
static inline SnoVal opsyn2(SnoVal a, SnoVal b) { return opsyn(a, b, NULL_VAL); }
SnoVal sort_fn(SnoVal arr);

/* TABLE_VAL macro */
#define TABLE_VAL(tbl_) ((SnoVal){ .type = STABLE, .tbl = (tbl_) })
#define ARRAY_VAL(a_)   ((SnoVal){ .type = ARRAY, .a   = (a_)   })

/* ============================================================
 * Pattern matching interface (matches existing runtime.h)
 * ============================================================ */

#include "../runtime.h"

#endif /* SNOBOL4_H */
void indirect_goto(const char *varname);
SnoVal pat_call(const char *name, SnoVal arg);
