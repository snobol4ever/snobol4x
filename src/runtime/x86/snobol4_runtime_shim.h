/*
 * runtime_shim.h — shim between scrip-cc-generated C and snobol4.h runtime
 *
 * scrip-cc emits: STRVAL_fn(), INTVAL_fn(), kw(), CONCAT_fn(), alt(),
 * aref(), aset(), iset(), deref(), cursor_get(),
 * assign_expr(), get(v), set(v,x), IS_FAIL_fn(v), DT_SNUL.
 * Also: MATCH_fn(&s,pat) → SnoMatch; REPLACE_fn(&s,&m,repl).
 */

#ifndef RUNTIME_SHIM_H
#define RUNTIME_SHIM_H

#include "snobol4.h"
#include <string.h>
#include <gc/gc.h>

/* ---- Avoid circular macro expansion: save and clear DT_SNUL enum member ---- */
/* DT_SNUL is both an enum value (DTYPE_t) and needs to be a DESCR_t macro.
 * We use NULVCL for the DESCR_t form. */

/* ---- Failure macro ---- */
#define IS_FAIL_fn(v)      IS_FAIL_fn(v)

/* ---- Variable get/set: scrip-cc emits static locals, identity macros ---- */
#define get(v)          (v)
#define set(v, x)       ((v) = (x))

/* ---- Null DESCR_t ---- */
/* We cannot redefine DT_SNUL (it's a DTYPE_t enum member).
 * scrip-cc emits DT_SNUL as a DESCR_t — we handle this by emit.c
 * emitting NULVCL instead. See emit.c emit_expr E_NUL case. */

/* ---- Scalar constructors ---- */
static inline DESCR_t _vint_impl(int64_t i)    { return INTVAL(i); }
static inline DESCR_t _real_impl(double d)    { return REALVAL(d); }
static inline DESCR_t _str_impl(const char *s) {
    if (!s) return NULVCL;
    char *p = GC_STRDUP(s);
    return STRVAL(p);
}
#define INTVAL_fn(i)   _vint_impl((int64_t)(i))
#define real(d)  _real_impl((double)(d))
#define STRVAL_fn(s)   _str_impl(s)

/* ---- Keyword access ---- */
static inline DESCR_t _kw_impl(const char *name) { return NV_GET_fn(name); }
static inline void   _kw_set_impl(const char *name, DESCR_t v) { NV_SET_fn(name, v); }
#define kw(name)         _kw_impl(name)
#define kw_set(name, v)  _kw_set_impl(name, v)

/* ---- Concatenation: override char* version with DESCR_t version ---- */
/* CONCAT_fn in snobol4.h is char*,char*→char*; we need DESCR_t,DESCR_t→DESCR_t */
#ifdef CONCAT_fn
#undef CONCAT_fn
#endif
#define CONCAT_fn(a, b)    CONCAT_fn((a), (b))

/* ---- Pattern alternation ---- */
static inline DESCR_t _alt_impl(DESCR_t a, DESCR_t b) { return pat_alt(a, b); }
#define alt(a, b)   _alt_impl(a, b)

/* ---- Indirect access ---- */
static inline DESCR_t _deref_impl(DESCR_t nameVal) {
    const char *name = VARVAL_fn(nameVal);
    if (!name || !*name) return NULVCL;
    return NV_GET_fn(name);
}
static inline void _iset_impl(DESCR_t nameVal, DESCR_t v) {
    const char *name = VARVAL_fn(nameVal);
    if (name && *name) NV_SET_fn(name, v);
}
#define deref(nv)       _deref_impl(nv)
#define iset(nv, v)     _iset_impl(nv, v)
#define assign_expr(lvar, x)  ((lvar) = (x))

/* ---- Array / table access ---- */
static inline DESCR_t _aref_impl(DESCR_t arr, DESCR_t *keys, int n) {
    if (n <= 0) return FAILDESCR;
    if (arr.v == DT_T) {
        const char *k = VARVAL_fn(keys[0]);
        return table_get(arr.tbl, k ? k : "");
    }
    if (arr.v == DT_A) {
        int i = (int)to_int(keys[0]);
        if (n == 1) return array_get(arr.arr, i);
        int j = (int)to_int(keys[1]);
        return array_get2(arr.arr, i, j);
    }
    return FAILDESCR;
}
static inline void _aset_impl(DESCR_t arr, DESCR_t *keys, int n, DESCR_t v) {
    if (n <= 0) return;
    if (arr.v == DT_T) {
        const char *k = VARVAL_fn(keys[0]);
        table_set_descr(arr.tbl, k ? k : "", keys[0], v);
        return;
    }
    if (arr.v == DT_A) {
        int i = (int)to_int(keys[0]);
        if (n == 1) { array_set(arr.arr, i, v); return; }
        int j = (int)to_int(keys[1]);
        array_set2(arr.arr, i, j, v);
    }
}
static inline DESCR_t _index_impl(DESCR_t base, DESCR_t *keys, int n) {
    return _aref_impl(base, keys, n);
}
/* Use functions directly (not macros) to avoid preprocessor compound-literal arg-count issues */
#undef aref
#undef aset
#undef INDEX_fn
/* These are now just the underlying functions */
#define aref   _aref_impl
#define aset   _aset_impl
#define INDEX_fn  _index_impl

/* ---- Cursor capture stub ---- */
static inline DESCR_t _snoc_cursor_get(const char *varname) {
    (void)varname; return NULVCL;
}
#define cursor_get(n)   _snoc_cursor_get(n)

/* ---- Pattern aliases ---- */
#define pat_break(chars)    pat_break_((chars))
#define pat_any(chars)      pat_any_cs((chars))

static inline DESCR_t _snoc_pat_var(const char *name) { return pat_ref(name); }
static inline DESCR_t _snoc_pat_val(DESCR_t v)          { return var_as_pattern(v); }
static inline DESCR_t _snoc_pat_deref(DESCR_t v)        { return var_as_pattern(v); }
static inline DESCR_t _snoc_pat_cond(DESCR_t child, const char *var) {
    return pat_assign_cond(child, STRVAL((char *)var));
}
static inline DESCR_t _snoc_pat_imm(DESCR_t child, const char *var) {
    return pat_assign_imm(child, STRVAL((char *)var));
}
#define pat_var(name)           _snoc_pat_var(name)
#define pat_val(v)              _snoc_pat_val(v)
#define pat_deref(v)            _snoc_pat_deref(v)
#define pat_cond(child, var)    _snoc_pat_cond(child, var)
#define pat_imm(child, var)     _snoc_pat_imm(child, var)

/* ---- Match / REPLACE_fn ---- */
typedef struct {
    int    failed;
    DESCR_t subject_saved;  /* saved subject before MATCH_fn, for replacement */
    DESCR_t pattern;
} SnoMatch;

static inline SnoMatch _snoc_match(DESCR_t *subj, DESCR_t pat) {
    SnoMatch m; m.failed = 1;
    if (!subj) return m;
    m.subject_saved = *subj;
    m.pattern = pat;
    const char *s = VARVAL_fn(*subj);
    m.failed = !match_pattern(pat, s ? s : "");
    return m;
}
static inline void _snoc_replace(DESCR_t *subj, SnoMatch *m, DESCR_t repl) {
    if (!subj || !m || m->failed) return;
    *subj = m->subject_saved;  /* restore to pre-MATCH_fn state */
    match_and_replace(subj, m->pattern, repl);
}
#define MATCH_fn(subj, pat)           _snoc_match(subj, pat)
#define REPLACE_fn(subj, m, repl)     _snoc_replace(subj, m, repl)

/* ---- Runtime INIT_fn / finish ---- */
static inline void INIT_fn(void)    { SNO_INIT_fn(); extern void inc_init(void); inc_init(); }
static inline void finish(void)  { }

/* ---- ABORT / exception handler stack ----
 *
 * Architecture (Session 27 eureka):
 *   Normal pattern failure: pure Byrd Box gotos, zero overhead.
 *   ABORT and genuinely bad things: longjmp to nearest handler.
 *   Each statement is a setjmp catch boundary.
 *   Each DEFINE_fn'd function is also a catch boundary.
 *   Line number is implicit in which boundary catches — free diagnostics.
 */
#include <setjmp.h>

#define ABRT_STACK_INIT 16
static jmp_buf **_sno_abort_stack = NULL;
static int       _sno_abort_depth = 0;
static int       _sno_abort_cap   = 0;
static int       _sno_abort_lineno = 0;

static inline void push_abort_handler(jmp_buf *jb) {
    if (_sno_abort_depth >= _sno_abort_cap) {
        _sno_abort_cap = _sno_abort_cap ? _sno_abort_cap * 2 : ABRT_STACK_INIT;
        _sno_abort_stack = realloc(_sno_abort_stack, _sno_abort_cap * sizeof(jmp_buf *));
        if (!_sno_abort_stack) { fprintf(stderr, "abort stack OOM\n"); abort(); }
    }
    _sno_abort_stack[_sno_abort_depth++] = jb;
}
static inline void pop_abort_handler(void) {
    if (_sno_abort_depth > 0) _sno_abort_depth--;
}
static inline void ABORT_fn(int lineno) {
    _sno_abort_lineno = lineno;
    if (_sno_abort_depth > 0)
        longjmp(*_sno_abort_stack[_sno_abort_depth-1], 1);
    /* No handler — fatal */
    fprintf(stderr, "ABORT at line %d\n", lineno);
    exit(1);
}

/* Macro for generated per-statement abort guard.
 * Usage: STMT_BEGIN(lineno) ... STMT_END(lineno, fn, onfail_label)
 * Normal path: falls through.
 * ABORT path: jumps to onfail_label (which should be :F() target or FRETURN).
 */
#define ABRT_GUARD_DECL   jmp_buf _stmt_jmp;
#define ABRT_GUARD_SET    (push_abort_handler(&_stmt_jmp), setjmp(_stmt_jmp))
#define ABRT_GUARD_POP    pop_abort_handler()

#endif /* RUNTIME_SHIM_H */
