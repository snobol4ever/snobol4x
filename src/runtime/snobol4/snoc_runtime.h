/*
 * snoc_runtime.h — shim between snoc-generated C and snobol4.h runtime
 *
 * snoc emits: sno_str(), sno_int(), sno_kw(), sno_concat(), sno_alt(),
 * sno_aref(), sno_aset(), sno_iset(), sno_deref(), sno_cursor_get(),
 * sno_assign_expr(), sno_get(v), sno_set(v,x), SNO_IS_FAIL(v), SNO_NULL.
 * Also: sno_match(&s,pat) → SnoMatch; sno_replace(&s,&m,repl).
 */

#ifndef SNOC_RUNTIME_H
#define SNOC_RUNTIME_H

#include "snobol4.h"
#include <string.h>
#include <gc/gc.h>

/* ---- Avoid circular macro expansion: save and clear SNO_NULL enum member ---- */
/* SNO_NULL is both an enum value (SnoType) and needs to be a SnoVal macro.
 * We use SNO_NULL_VAL for the SnoVal form. */

/* ---- Failure macro ---- */
#define SNO_IS_FAIL(v)      sno_is_fail(v)

/* ---- Variable get/set: snoc emits static locals, identity macros ---- */
#define sno_get(v)          (v)
#define sno_set(v, x)       ((v) = (x))

/* ---- Null SnoVal ---- */
/* We cannot redefine SNO_NULL (it's a SnoType enum member).
 * snoc emits SNO_NULL as a SnoVal — we handle this by emit.c
 * emitting SNO_NULL_VAL instead. See emit.c emit_expr E_NULL case. */

/* ---- Scalar constructors ---- */
static inline SnoVal _snoc_int(int64_t i)    { return SNO_INT_VAL(i); }
static inline SnoVal _snoc_real(double d)    { return SNO_REAL_VAL(d); }
static inline SnoVal _snoc_str(const char *s) {
    if (!s) return SNO_NULL_VAL;
    char *p = GC_STRDUP(s);
    return SNO_STR_VAL(p);
}
#define sno_int(i)   _snoc_int((int64_t)(i))
#define sno_real(d)  _snoc_real((double)(d))
#define sno_str(s)   _snoc_str(s)

/* ---- Keyword access ---- */
static inline SnoVal _snoc_kw(const char *name) { return sno_var_get(name); }
static inline void   _snoc_kw_set(const char *name, SnoVal v) { sno_var_set(name, v); }
#define sno_kw(name)         _snoc_kw(name)
#define sno_kw_set(name, v)  _snoc_kw_set(name, v)

/* ---- Concatenation: override char* version with SnoVal version ---- */
/* sno_concat in snobol4.h is char*,char*→char*; we need SnoVal,SnoVal→SnoVal */
#ifdef sno_concat
#undef sno_concat
#endif
#define sno_concat(a, b)    sno_concat_sv((a), (b))

/* ---- Pattern alternation ---- */
static inline SnoVal _snoc_alt(SnoVal a, SnoVal b) { return sno_pat_alt(a, b); }
#define sno_alt(a, b)   _snoc_alt(a, b)

/* ---- Indirect access ---- */
static inline SnoVal _snoc_deref(SnoVal nameVal) {
    const char *name = sno_to_str(nameVal);
    if (!name || !*name) return SNO_NULL_VAL;
    return sno_var_get(name);
}
static inline void _snoc_iset(SnoVal nameVal, SnoVal v) {
    const char *name = sno_to_str(nameVal);
    if (name && *name) sno_var_set(name, v);
}
#define sno_deref(nv)       _snoc_deref(nv)
#define sno_iset(nv, v)     _snoc_iset(nv, v)
#define sno_assign_expr(lvar, x)  ((lvar) = (x))

/* ---- Array / table access ---- */
static inline SnoVal _snoc_aref(SnoVal arr, SnoVal *keys, int n) {
    if (n <= 0) return SNO_FAIL_VAL;
    if (arr.type == SNO_TABLE) {
        const char *k = sno_to_str(keys[0]);
        return sno_table_get(arr.tbl, k ? k : "");
    }
    if (arr.type == SNO_ARRAY) {
        int i = (int)sno_to_int(keys[0]);
        if (n == 1) return sno_array_get(arr.a, i);
        int j = (int)sno_to_int(keys[1]);
        return sno_array_get2(arr.a, i, j);
    }
    return SNO_FAIL_VAL;
}
static inline void _snoc_aset(SnoVal arr, SnoVal *keys, int n, SnoVal v) {
    if (n <= 0) return;
    if (arr.type == SNO_TABLE) {
        const char *k = sno_to_str(keys[0]);
        sno_table_set(arr.tbl, k ? k : "", v);
        return;
    }
    if (arr.type == SNO_ARRAY) {
        int i = (int)sno_to_int(keys[0]);
        if (n == 1) { sno_array_set(arr.a, i, v); return; }
        int j = (int)sno_to_int(keys[1]);
        sno_array_set2(arr.a, i, j, v);
    }
}
static inline SnoVal _snoc_index(SnoVal base, SnoVal *keys, int n) {
    return _snoc_aref(base, keys, n);
}
/* Use functions directly (not macros) to avoid preprocessor compound-literal arg-count issues */
#undef sno_aref
#undef sno_aset
#undef sno_index
/* These are now just the underlying functions */
#define sno_aref   _snoc_aref
#define sno_aset   _snoc_aset
#define sno_index  _snoc_index

/* ---- Cursor capture stub ---- */
static inline SnoVal _snoc_cursor_get(const char *varname) {
    (void)varname; return SNO_NULL_VAL;
}
#define sno_cursor_get(n)   _snoc_cursor_get(n)

/* ---- Pattern aliases ---- */
#define sno_pat_break(chars)    sno_pat_break_((chars))
#define sno_pat_any(chars)      sno_pat_any_cs((chars))

static inline SnoVal _snoc_pat_var(const char *name) { return sno_pat_ref(name); }
static inline SnoVal _snoc_pat_val(SnoVal v)          { return sno_var_as_pattern(v); }
static inline SnoVal _snoc_pat_deref(SnoVal v)        { return sno_var_as_pattern(v); }
static inline SnoVal _snoc_pat_cond(SnoVal child, const char *var) {
    return sno_pat_assign_cond(child, SNO_STR_VAL((char *)var));
}
static inline SnoVal _snoc_pat_imm(SnoVal child, const char *var) {
    return sno_pat_assign_imm(child, SNO_STR_VAL((char *)var));
}
#define sno_pat_var(name)           _snoc_pat_var(name)
#define sno_pat_val(v)              _snoc_pat_val(v)
#define sno_pat_deref(v)            _snoc_pat_deref(v)
#define sno_pat_cond(child, var)    _snoc_pat_cond(child, var)
#define sno_pat_imm(child, var)     _snoc_pat_imm(child, var)

/* ---- Match / replace ---- */
typedef struct {
    int    failed;
    SnoVal subject_saved;  /* saved subject before match, for replacement */
    SnoVal pattern;
} SnoMatch;

static inline SnoMatch _snoc_match(SnoVal *subj, SnoVal pat) {
    SnoMatch m; m.failed = 1;
    if (!subj) return m;
    m.subject_saved = *subj;
    m.pattern = pat;
    const char *s = sno_to_str(*subj);
    m.failed = !sno_match_pattern(pat, s ? s : "");
    return m;
}
static inline void _snoc_replace(SnoVal *subj, SnoMatch *m, SnoVal repl) {
    if (!subj || !m || m->failed) return;
    *subj = m->subject_saved;  /* restore to pre-match state */
    sno_match_and_replace(subj, m->pattern, repl);
}
#define sno_match(subj, pat)           _snoc_match(subj, pat)
#define sno_replace(subj, m, repl)     _snoc_replace(subj, m, repl)

/* ---- Runtime init / finish ---- */
static inline void sno_init(void)    { sno_runtime_init(); }
static inline void sno_finish(void)  { }

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

#define SNO_ABORT_STACK_MAX 256
static jmp_buf *_sno_abort_stack[SNO_ABORT_STACK_MAX];
static int      _sno_abort_depth = 0;
static int      _sno_abort_lineno = 0;

static inline void sno_push_abort_handler(jmp_buf *jb) {
    if (_sno_abort_depth < SNO_ABORT_STACK_MAX)
        _sno_abort_stack[_sno_abort_depth++] = jb;
}
static inline void sno_pop_abort_handler(void) {
    if (_sno_abort_depth > 0) _sno_abort_depth--;
}
static inline void sno_abort(int lineno) {
    _sno_abort_lineno = lineno;
    if (_sno_abort_depth > 0)
        longjmp(*_sno_abort_stack[_sno_abort_depth-1], 1);
    /* No handler — fatal */
    fprintf(stderr, "ABORT at line %d\n", lineno);
    exit(1);
}

/* Macro for generated per-statement abort guard.
 * Usage: SNO_STMT_BEGIN(lineno) ... SNO_STMT_END(lineno, fn, onfail_label)
 * Normal path: falls through.
 * ABORT path: jumps to onfail_label (which should be :F() target or FRETURN).
 */
#define SNO_ABORT_GUARD_DECL   jmp_buf _stmt_jmp;
#define SNO_ABORT_GUARD_SET    (sno_push_abort_handler(&_stmt_jmp), setjmp(_stmt_jmp))
#define SNO_ABORT_GUARD_POP    sno_pop_abort_handler()

#endif /* SNOC_RUNTIME_H */
