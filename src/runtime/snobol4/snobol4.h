/* snobol4.h — SNOBOL4-tiny universal value type and runtime API
 *
 * Architecture decisions (recorded PLAN.md 2026-03-10):
 *   D1: Memory model = Boehm GC  (no ref-counting, no free() anywhere)
 *   D2: Tree children = realloc'd dynamic array (unbounded arity)
 *   D3: cstack = thread-local MatchState* (matches SNOBOL4-csharp [ThreadStatic])
 *
 * This header defines the full SNOBOL4 value universe:
 *   SNO_NULL, SNO_STR, SNO_INT, SNO_REAL, SNO_TREE,
 *   SNO_PATTERN, SNO_ARRAY, SNO_TABLE, SNO_CODE
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
    SNO_NULL    = 0,   /* unset / empty string / NULL */
    SNO_STR     = 1,   /* char * (GC-managed) */
    SNO_INT     = 2,   /* int64_t */
    SNO_REAL    = 3,   /* double */
    SNO_TREE    = 4,   /* Tree* (GC-managed) */
    SNO_PATTERN = 5,   /* Pattern* (GC-managed) */
    SNO_ARRAY   = 6,   /* SnoArray* (GC-managed) */
    SNO_TABLE   = 7,   /* SnoTable* (GC-managed) */
    SNO_CODE    = 8,   /* compiled code block */
    SNO_UDEF    = 9,   /* user-defined datatype instance */
} SnoType;

struct _Tree;
struct _Pattern;
struct _SnoArray;
struct _SnoTable;
struct _UDef;

typedef struct SnoVal {
    SnoType type;
    union {
        char             *s;   /* SNO_STR, SNO_NULL (empty string = "") */
        int64_t           i;   /* SNO_INT  */
        double            r;   /* SNO_REAL */
        struct _Tree     *t;   /* SNO_TREE */
        struct _Pattern  *p;   /* SNO_PATTERN */
        struct _SnoArray *a;   /* SNO_ARRAY */
        struct _SnoTable *tbl; /* SNO_TABLE */
        struct _UDef     *u;   /* SNO_UDEF */
        void             *ptr; /* generic GC pointer */
    };
} SnoVal;

#define SNO_NULL_VAL    ((SnoVal){ .type = SNO_NULL, .s = "" })
#define SNO_STR_VAL(s_) ((SnoVal){ .type = SNO_STR,  .s = (s_) })
#define SNO_INT_VAL(i_) ((SnoVal){ .type = SNO_INT,  .i = (i_) })
#define SNO_REAL_VAL(r_)((SnoVal){ .type = SNO_REAL, .r = (r_) })
#define SNO_TREE_VAL(t_)((SnoVal){ .type = SNO_TREE, .t = (t_) })

/* ============================================================
 * String operations
 * ============================================================ */

/* All strings are GC-managed, null-terminated char*.
 * "" is the canonical NULL/empty value.  */

static inline int sno_is_null(SnoVal v)  { return v.type == SNO_NULL || (v.type == SNO_STR && (!v.s || !*v.s)); }
static inline int sno_is_str(SnoVal v)   { return v.type == SNO_STR || v.type == SNO_NULL; }
static inline int sno_is_int(SnoVal v)   { return v.type == SNO_INT; }
static inline int sno_is_real(SnoVal v)  { return v.type == SNO_REAL; }
static inline int sno_is_tree(SnoVal v)  { return v.type == SNO_TREE; }
static inline int sno_is_udef(SnoVal v)  { return v.type == SNO_UDEF; }

/* Convert any value to string (GC-managed) */
char *sno_to_str(SnoVal v);

/* Convert any value to integer (0 on failure) */
int64_t sno_to_int(SnoVal v);

/* Convert any value to real (0.0 on failure) */
double sno_to_real(SnoVal v);

/* String concatenation — GC-managed result */
char *sno_concat(const char *a, const char *b);

/* String duplication */
char *sno_dup(const char *s);

/* String size in characters */
int64_t sno_size(const char *s);

/* DATATYPE function */
const char *sno_datatype(SnoVal v);

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

Tree *sno_tree_new(const char *tag, SnoVal val);
Tree *sno_tree_new0(const char *tag);     /* tag only, null val, no children */
void  sno_tree_append(Tree *x, Tree *y);  /* Append(x,y)  */
void  sno_tree_prepend(Tree *x, Tree *y); /* Prepend(x,y) */
void  sno_tree_insert(Tree *x, Tree *y, int place); /* Insert(x,y,place) 1-based */
Tree *sno_tree_remove(Tree *x, int place);           /* Remove(x,place)   1-based */

/* Accessors (SNOBOL4 field functions for DATA('tree')) */
static inline const char *sno_t(Tree *x) { return x ? x->tag : ""; }
static inline SnoVal      sno_v(Tree *x) { return x ? x->val  : SNO_NULL_VAL; }
static inline int         sno_n(Tree *x) { return x ? x->n    : 0; }
static inline Tree       *sno_c_i(Tree *x, int i) {  /* c(x)[i], 1-based */
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

SnoArray *sno_array_new(int lo, int hi);
SnoArray *sno_array_new2d(int lo1, int hi1, int lo2, int hi2);
SnoVal    sno_array_get(SnoArray *a, int i);       /* 1-based */
void      sno_array_set(SnoArray *a, int i, SnoVal v);
SnoVal    sno_array_get2(SnoArray *a, int i, int j);
void      sno_array_set2(SnoArray *a, int i, int j, SnoVal v);

/* ============================================================
 * Table (hash map)
 * ============================================================ */

typedef struct _SnoTableEntry {
    char   *key;
    SnoVal  val;
    struct _SnoTableEntry *next;
} SnoTableEntry;

#define SNO_TABLE_BUCKETS 256

typedef struct _SnoTable {
    SnoTableEntry *buckets[SNO_TABLE_BUCKETS];
    int            size;
} SnoTable;

SnoTable *sno_table_new(void);
SnoVal    sno_table_get(SnoTable *tbl, const char *key);
void      sno_table_set(SnoTable *tbl, const char *key, SnoVal val);
int       sno_table_has(SnoTable *tbl, const char *key);

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
void sno_data_define(const char *spec);

/* Allocate a new instance of a user-defined type */
SnoVal sno_udef_new(const char *typename, ...);  /* varargs: field values */

/* Get/set field by name */
SnoVal sno_field_get(SnoVal obj, const char *field);
void   sno_field_set(SnoVal obj, const char *field, SnoVal val);

/* ============================================================
 * Variable table (global variables, $name indirect access)
 * ============================================================ */

SnoVal  sno_var_get(const char *name);
void    sno_var_set(const char *name, SnoVal val);
SnoVal  sno_indirect_get(const char *name);  /* $name */
void    sno_indirect_set(const char *name, SnoVal val);

/* ============================================================
 * Counter stack (nPush/nInc/nDec/nTop/nPop)
 * ============================================================ */

void    sno_npush(void);
void    sno_ninc(void);
void    sno_ndec(void);
int64_t sno_ntop(void);
void    sno_npop(void);

/* ============================================================
 * Value stack (Push/Pop/Top for Shift/Reduce)
 * ============================================================ */

void   sno_push(SnoVal v);
SnoVal sno_pop(void);
SnoVal sno_top(void);
int    sno_stack_depth(void);

/* ============================================================
 * Function table (DEFINE/APPLY)
 * ============================================================ */

typedef SnoVal (*SnoFunc)(SnoVal *args, int nargs);

void    sno_define(const char *spec, SnoFunc fn);  /* DEFINE('name(a,b)local1,local2') */
SnoVal  sno_apply(const char *name, SnoVal *args, int nargs);  /* APPLY(name,...) */
int     sno_func_exists(const char *name);

/* ============================================================
 * GOTO / label runtime
 * ============================================================ */

/* Return codes for statement execution */
#define SNO_SUCCESS  0   /* fall through to next statement */
#define SNO_FAILURE  1   /* branch to :F label */
#define SNO_GOTO     2   /* branch to specific label */
#define SNO_RETURN   3   /* return from function */
#define SNO_FRETURN  4   /* failure return from function */
#define SNO_NRETURN  5   /* no-value return */
#define SNO_END      6   /* END of program */

/* ============================================================
 * Builtin functions (string operations)
 * ============================================================ */

SnoVal sno_size_fn(SnoVal s);                        /* SIZE(s) */
SnoVal sno_dupl_fn(SnoVal s, SnoVal n);              /* DUPL(s,n) */
SnoVal sno_replace_fn(SnoVal s, SnoVal from, SnoVal to); /* REPLACE(s,f,t) */
SnoVal sno_substr_fn(SnoVal s, SnoVal i, SnoVal n);  /* SUBSTR(s,i,n) */
SnoVal sno_trim_fn(SnoVal s);                        /* TRIM(s) */
SnoVal sno_lpad_fn(SnoVal s, SnoVal n, SnoVal pad);  /* LPAD(s,n,pad) */
SnoVal sno_rpad_fn(SnoVal s, SnoVal n, SnoVal pad);  /* RPAD(s,n,pad) */
SnoVal sno_reverse_fn(SnoVal s);                     /* REVERSE(s) */
SnoVal sno_char_fn(SnoVal n);                        /* CHAR(n) */
SnoVal sno_integer_fn(SnoVal v);                     /* INTEGER(v) */
SnoVal sno_real_fn(SnoVal v);                        /* REAL(v) */
SnoVal sno_string_fn(SnoVal v);                      /* STRING(v) */

/* ============================================================
 * Arithmetic / comparison predicates
 * ============================================================ */

int sno_eq(SnoVal a, SnoVal b);   /* EQ: numeric equal — succeeds or fails */
int sno_ne(SnoVal a, SnoVal b);
int sno_lt(SnoVal a, SnoVal b);
int sno_le(SnoVal a, SnoVal b);
int sno_gt(SnoVal a, SnoVal b);
int sno_ge(SnoVal a, SnoVal b);

int sno_ident(SnoVal a, SnoVal b); /* IDENT: string/value identical */
int sno_differ(SnoVal a, SnoVal b);/* DIFFER: string/value different */

SnoVal sno_add(SnoVal a, SnoVal b);
SnoVal sno_sub(SnoVal a, SnoVal b);
SnoVal sno_mul(SnoVal a, SnoVal b);
SnoVal sno_div(SnoVal a, SnoVal b);
SnoVal sno_pow(SnoVal a, SnoVal b);
SnoVal sno_neg(SnoVal a);

/* ============================================================
 * I/O
 * ============================================================ */

void   sno_output_val(SnoVal v);        /* OUTPUT = v */
SnoVal sno_input_read(void);            /* v = INPUT */
void   sno_output_str(const char *s);   /* OUTPUT = 'string' */

/* ============================================================
 * SNOBOL4 keywords (&KEYWORD)
 * ============================================================ */

extern int64_t sno_kw_fullscan;
extern int64_t sno_kw_maxlngth;
extern int64_t sno_kw_anchor;
extern int64_t sno_kw_trim;
extern int64_t sno_kw_stlimit;

/* Global character sets */
extern char sno_ucase[27];   /* &UCASE */
extern char sno_lcase[27];   /* &LCASE */
extern char sno_alphabet[257]; /* &ALPHABET */
extern char sno_digits[11];  /* digits constant from global.inc */

/* ============================================================
 * Runtime initialization
 * ============================================================ */

void sno_runtime_init(void);  /* call once at program start */

/* ============================================================
 * Pattern matching interface (matches existing runtime.h)
 * ============================================================ */

/* These match the existing sno_enter/sno_exit arena API */
#include "../runtime.h"

#endif /* SNOBOL4_H */
