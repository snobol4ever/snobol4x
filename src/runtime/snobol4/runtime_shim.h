/*
 * runtime_shim.h — shim between sno2c-generated C and snobol4.h runtime
 *
 * sno2c emits: strv(), vint(), kw(), ccat(), alt(),
 * aref(), aset(), iset(), deref(), cursor_get(),
 * assign_expr(), get(v), set(v,x), IS_FAIL(v), SNULL.
 * Also: mtch(&s,pat) → SnoMatch; replc(&s,&m,repl).
 */

#ifndef RUNTIME_SHIM_H
#define RUNTIME_SHIM_H

#include "snobol4.h"
#include <string.h>
#include <gc/gc.h>

/* ---- Avoid circular macro expansion: save and clear SNULL enum member ---- */
/* SNULL is both an enum value (SnoType) and needs to be a SnoVal macro.
 * We use NULL_VAL for the SnoVal form. */

/* ---- Failure macro ---- */
#define IS_FAIL(v)      is_fail(v)

/* ---- Variable get/set: sno2c emits static locals, identity macros ---- */
#define get(v)          (v)
#define set(v, x)       ((v) = (x))

/* ---- Null SnoVal ---- */
/* We cannot redefine SNULL (it's a SnoType enum member).
 * sno2c emits SNULL as a SnoVal — we handle this by emit.c
 * emitting NULL_VAL instead. See emit.c emit_expr E_NULL case. */

/* ---- Scalar constructors ---- */
static inline SnoVal _vint_impl(int64_t i)    { return INT_VAL(i); }
static inline SnoVal _real_impl(double d)    { return REAL_VAL(d); }
static inline SnoVal _str_impl(const char *s) {
    if (!s) return NULL_VAL;
    char *p = GC_STRDUP(s);
    return STR_VAL(p);
}
#define vint(i)   _vint_impl((int64_t)(i))
#define real(d)  _real_impl((double)(d))
#define strv(s)   _str_impl(s)

/* ---- Keyword access ---- */
static inline SnoVal _kw_impl(const char *name) { return var_get(name); }
static inline void   _kw_set_impl(const char *name, SnoVal v) { var_set(name, v); }
#define kw(name)         _kw_impl(name)
#define kw_set(name, v)  _kw_set_impl(name, v)

/* ---- Concatenation: override char* version with SnoVal version ---- */
/* ccat in snobol4.h is char*,char*→char*; we need SnoVal,SnoVal→SnoVal */
#ifdef ccat
#undef ccat
#endif
#define ccat(a, b)    concat_sv((a), (b))

/* ---- Pattern alternation ---- */
static inline SnoVal _alt_impl(SnoVal a, SnoVal b) { return pat_alt(a, b); }
#define alt(a, b)   _alt_impl(a, b)

/* ---- Indirect access ---- */
static inline SnoVal _deref_impl(SnoVal nameVal) {
    const char *name = to_str(nameVal);
    if (!name || !*name) return NULL_VAL;
    return var_get(name);
}
static inline void _iset_impl(SnoVal nameVal, SnoVal v) {
    const char *name = to_str(nameVal);
    if (name && *name) var_set(name, v);
}
#define deref(nv)       _deref_impl(nv)
#define iset(nv, v)     _iset_impl(nv, v)
#define assign_expr(lvar, x)  ((lvar) = (x))

/* ---- Array / table access ---- */
static inline SnoVal _aref_impl(SnoVal arr, SnoVal *keys, int n) {
    if (n <= 0) return FAIL_VAL;
    if (arr.type == STABLE) {
        const char *k = to_str(keys[0]);
        return table_get(arr.tbl, k ? k : "");
    }
    if (arr.type == ARRAY) {
        int i = (int)to_int(keys[0]);
        if (n == 1) return array_get(arr.a, i);
        int j = (int)to_int(keys[1]);
        return array_get2(arr.a, i, j);
    }
    return FAIL_VAL;
}
static inline void _aset_impl(SnoVal arr, SnoVal *keys, int n, SnoVal v) {
    if (n <= 0) return;
    if (arr.type == STABLE) {
        const char *k = to_str(keys[0]);
        table_set(arr.tbl, k ? k : "", v);
        return;
    }
    if (arr.type == ARRAY) {
        int i = (int)to_int(keys[0]);
        if (n == 1) { array_set(arr.a, i, v); return; }
        int j = (int)to_int(keys[1]);
        array_set2(arr.a, i, j, v);
    }
}
static inline SnoVal _index_impl(SnoVal base, SnoVal *keys, int n) {
    return _aref_impl(base, keys, n);
}
/* Use functions directly (not macros) to avoid preprocessor compound-literal arg-count issues */
#undef aref
#undef aset
#undef indx
/* These are now just the underlying functions */
#define aref   _aref_impl
#define aset   _aset_impl
#define indx  _index_impl

/* ---- Cursor capture stub ---- */
static inline SnoVal _snoc_cursor_get(const char *varname) {
    (void)varname; return NULL_VAL;
}
#define cursor_get(n)   _snoc_cursor_get(n)

/* ---- Pattern aliases ---- */
#define pat_break(chars)    pat_break_((chars))
#define pat_any(chars)      pat_any_cs((chars))

static inline SnoVal _snoc_pat_var(const char *name) { return pat_ref(name); }
static inline SnoVal _snoc_pat_val(SnoVal v)          { return var_as_pattern(v); }
static inline SnoVal _snoc_pat_deref(SnoVal v)        { return var_as_pattern(v); }
static inline SnoVal _snoc_pat_cond(SnoVal child, const char *var) {
    return pat_assign_cond(child, STR_VAL((char *)var));
}
static inline SnoVal _snoc_pat_imm(SnoVal child, const char *var) {
    return pat_assign_imm(child, STR_VAL((char *)var));
}
#define pat_var(name)           _snoc_pat_var(name)
#define pat_val(v)              _snoc_pat_val(v)
#define pat_deref(v)            _snoc_pat_deref(v)
#define pat_cond(child, var)    _snoc_pat_cond(child, var)
#define pat_imm(child, var)     _snoc_pat_imm(child, var)

/* ---- Match / replc ---- */
typedef struct {
    int    failed;
    SnoVal subject_saved;  /* saved subject before mtch, for replacement */
    SnoVal pattern;
} SnoMatch;

static inline SnoMatch _snoc_match(SnoVal *subj, SnoVal pat) {
    SnoMatch m; m.failed = 1;
    if (!subj) return m;
    m.subject_saved = *subj;
    m.pattern = pat;
    const char *s = to_str(*subj);
    m.failed = !match_pattern(pat, s ? s : "");
    return m;
}
static inline void _snoc_replace(SnoVal *subj, SnoMatch *m, SnoVal repl) {
    if (!subj || !m || m->failed) return;
    *subj = m->subject_saved;  /* restore to pre-mtch state */
    match_and_replace(subj, m->pattern, repl);
}
#define mtch(subj, pat)           _snoc_match(subj, pat)
#define replc(subj, m, repl)     _snoc_replace(subj, m, repl)

/* ---- Runtime ini / finish ---- */
static inline void ini(void)    { runtime_init(); extern void inc_init(void); inc_init(); }
static inline void finish(void)  { }

/* ---- ABORT / exception handler stack ----
 *
 * Architecture (Session 27 eureka):
 *   Normal pattern failure: pure Byrd Box gotos, zero overhead.
 *   ABORT and genuinely bad things: longjmp to nearest handler.
 *   Each statement is a setjmp catch boundary.
 *   Each DEFINE'd function is also a catch boundary.
 *   Line number is implicit in which boundary catches — free diagnostics.
 */
#include <setjmp.h>

#define ABRT_STACK_MAX 256
static jmp_buf *_sno_abort_stack[ABRT_STACK_MAX];
static int      _sno_abort_depth = 0;
static int      _sno_abort_lineno = 0;

static inline void push_abort_handler(jmp_buf *jb) {
    if (_sno_abort_depth < ABRT_STACK_MAX)
        _sno_abort_stack[_sno_abort_depth++] = jb;
}
static inline void pop_abort_handler(void) {
    if (_sno_abort_depth > 0) _sno_abort_depth--;
}
static inline void abrt(int lineno) {
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
