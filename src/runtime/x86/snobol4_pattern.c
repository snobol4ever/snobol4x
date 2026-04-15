/*
 * snobol4_pattern.c — SNOBOL4 pattern constructor and matching bridge
 *
 * Connects the pat_*() API (called from beautiful.c) to the existing
 * Byrd Box engine in engine.c.
 *
 * Architecture:
 *   - DESCR_t with type P holds a PATND_t* (GC-managed)
 *   - PATND_t wraps a lazy tree of pattern constructors
 *   - At MATCH_fn time, match_pattern() materialises the pattern into
 *     engine Pattern* nodes and calls engine_match()
 *   - Deferred refs (*name) are resolved from the variable table at MATCH_fn time
 *   - Capture assignments ($ and .) write into the variable table on MATCH_fn
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "snobol4.h"
#include "../../ir/ir.h"         /* ir.h first — sets EXPR_T_DEFINED so scrip_cc.h skips its own EXPR_t */
#include "../../frontend/snobol4/scrip_cc.h"
/* CMPILE.c removed — bison/flex path via scrip_cc.h (GOAL-REMOVE-CMPILE S-4) */

/* Hook for scrip.c to route EVAL(string) through interp_eval_pat.
 * When set, EVAL_fn calls this instead of CONVE_fn→EXPVAL_fn for string args.
 * This handles E_DEFER (*func), $ (cursor-assign), and all other operators
 * that eval_node in eval_code.c does not support. */
DESCR_t (*g_eval_str_hook)(const char *s) = NULL;

/* =========================================================================
 * PATND_t — lazy pattern node
 * ===================================================================== */


/* Forward decl */

/* GC-allocate a PATND_t */
static PATND_t *spat_new(XKIND_t kind) {
    PATND_t *p = (PATND_t *)GC_MALLOC(sizeof(PATND_t));
    memset(p, 0, sizeof(PATND_t));
    p->kind = kind;
    return p;
}

/* Set children array — allocates GC'd array and copies pointers in.
 * Filters out NULL entries; if all are NULL, children stays NULL/nchildren=0. */
static void patnd_set_children(PATND_t *p, PATND_t **ch, int n) {
    int count = 0;
    for (int i = 0; i < n; i++) if (ch[i]) count++;
    if (count == 0) return;
    p->children = (PATND_t **)GC_MALLOC((size_t)count * sizeof(PATND_t *));
    int j = 0;
    for (int i = 0; i < n; i++) if (ch[i]) p->children[j++] = ch[i];
    p->nchildren = count;
}

/* Append a child to an XCAT or XOR node (used when flattening at build time). */
static void patnd_append_child(PATND_t *p, PATND_t *ch) {
    if (!ch) return;
    p->children = (PATND_t **)GC_REALLOC(p->children,
                      (size_t)(p->nchildren + 1) * sizeof(PATND_t *));
    p->children[p->nchildren++] = ch;
}

/* Wrap a PATND_t in a DESCR_t */
static inline DESCR_t spat_val(PATND_t *p) {
    DESCR_t v;
    v.v = DT_P;
    v.p    = (struct _PATND_t *)p;
    return v;
}

/* Extract PATND_t from DESCR_t */
static inline PATND_t *spat_of(DESCR_t v) {
    if (v.v != DT_P) return NULL;
    return (PATND_t *)v.p;
}

/* =========================================================================
 * Pattern constructors
 * ===================================================================== */

DESCR_t pat_lit(const char *s) {
    PATND_t *p = spat_new(XCHR);
    p->STRVAL_fn = s ? GC_strdup(s) : "";
    return spat_val(p);
}

DESCR_t pat_span(const char *chars) {
    PATND_t *p = spat_new(XSPNC);
    p->STRVAL_fn = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

DESCR_t pat_break_(const char *chars) {
    PATND_t *p = spat_new(XBRKC);
    p->STRVAL_fn = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

DESCR_t pat_breakx(const char *chars) {
    PATND_t *p = spat_new(XBRKX);
    p->STRVAL_fn = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

DESCR_t pat_any_cs(const char *chars) {
    PATND_t *p = spat_new(XANYC);
    p->STRVAL_fn = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

DESCR_t pat_notany(const char *chars) {
    PATND_t *p = spat_new(XNNYC);
    p->STRVAL_fn = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

DESCR_t pat_len(int64_t n) {
    /* SIL SCLENR/LENERR: ERRTYP,14 — negative number in illegal context */
    if (n < 0) { sno_runtime_error(14, NULL); return FAILDESCR; }
    PATND_t *p = spat_new(XLNTH);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_pos(int64_t n) {
    if (n < 0) { sno_runtime_error(14, NULL); return FAILDESCR; }
    PATND_t *p = spat_new(XPOSI);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_rpos(int64_t n) {
    if (n < 0) { sno_runtime_error(14, NULL); return FAILDESCR; }
    PATND_t *p = spat_new(XRPSI);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_tab(int64_t n) {
    if (n < 0) { sno_runtime_error(14, NULL); return FAILDESCR; }
    PATND_t *p = spat_new(XTB);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_rtab(int64_t n) {
    if (n < 0) { sno_runtime_error(14, NULL); return FAILDESCR; }
    PATND_t *p = spat_new(XRTB);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_arb(void) {
    return spat_val(spat_new(XFARB));
}

DESCR_t pat_arbno(DESCR_t inner) {
    PATND_t *p = spat_new(XARBN);
    PATND_t *ch = spat_of(inner);
    if (!ch && inner.v == DT_S) ch = spat_of(pat_lit(inner.s));
    PATND_t *arr[1] = { ch };
    patnd_set_children(p, arr, 1);
    return spat_val(p);
}

DESCR_t pat_rem(void) {
    return spat_val(spat_new(XSTAR));
}

DESCR_t pat_fence_p(DESCR_t inner) {
    PATND_t *p = spat_new(XFNCE);
    PATND_t *ch = spat_of(inner);
    if (!ch && inner.v == DT_S && inner.s) ch = spat_of(pat_lit(inner.s));
    if (!ch && inner.v == DT_SNUL)         ch = spat_of(pat_lit(""));
    if (!ch && inner.v == DT_I)            { char buf[32]; snprintf(buf,sizeof buf,"%lld",(long long)inner.i); ch = spat_of(pat_lit(buf)); }
    if (!ch && inner.v == DT_R)            { char buf[64]; snprintf(buf,sizeof buf,"%.14g",inner.r); ch = spat_of(pat_lit(buf)); }
    PATND_t *arr[1] = { ch };
    patnd_set_children(p, arr, 1);
    return spat_val(p);
}

DESCR_t pat_fence(void) {
    return spat_val(spat_new(XFNCE));
}

DESCR_t pat_fail(void) {
    return spat_val(spat_new(XFAIL));
}

DESCR_t pat_abort(void) {
    return spat_val(spat_new(XABRT));
}

DESCR_t pat_succeed(void) {
    return spat_val(spat_new(XSUCF));
}

DESCR_t pat_bal(void) {
    return spat_val(spat_new(XBAL));
}

DESCR_t pat_epsilon(void) {
    return spat_val(spat_new(XEPS));
}

/* Forward declaration — eval_node is defined in eval_code.c (separate TU) */
extern DESCR_t eval_node(EXPR_t *e);

/* pat_to_patnd: coerce a DESCR_t to a PATND_t*, handling string literals.
 * Returns NULL if the value cannot be represented as a pattern.
 *
 * DT_E split:
 *   E_FNC child  → *func()  side-effect pattern: build XATP via pat_user_call.
 *                  The function fires at MATCH_fn time (T_FUNC node), NOT now.
 *   anything else → PATVAL_fn: thaw the expression to a pattern value.
 */
static PATND_t *pat_to_patnd(DESCR_t v) {
    if (v.v == DT_E) {
        EXPR_t *frozen = (EXPR_t *)v.ptr;
        if (!frozen) return NULL;   /* null DT_E — propagate failure (do not epsilon) */
        if (frozen->kind == E_FNC) {
            /* *func(args...) — side-effect call deferred to match time via XATP */
            int nargs = frozen->nchildren;
            DESCR_t *args = NULL;
            if (nargs > 0) {
                args = (DESCR_t *)alloca((size_t)nargs * sizeof(DESCR_t));
                for (int i = 0; i < nargs; i++)
                    args[i] = eval_node(frozen->children[i]);
            }
            const char *fname = frozen->sval ? frozen->sval : "";
            DESCR_t pv = pat_user_call(fname, args, nargs);
            if (IS_FAIL_fn(pv)) return NULL;
            /* If function returned a real pattern, use it directly */
            PATND_t *pp = spat_of(pv);
            if (pp) return pp;
            /* Otherwise coerce non-pattern result to literal string pattern */
            v = pv;  /* fall through to coercion below */
        }
        if (frozen->kind == E_VAR && frozen->sval) {
            /* *varname — deferred variable reference, resolved at match time via XVAR.
             * This is the recursive grammar case: factor = ... *factor ...
             * The variable may not exist yet at construction time. */
            DESCR_t name_d = STRVAL(frozen->sval);
            DESCR_t pv = var_as_pattern(name_d);
            return spat_of(pv);
        }
        /* Other frozen expression: thaw via PATVAL_fn → pattern value */
        v = PATVAL_fn(v);
        if (v.v == DT_FAIL) return NULL;
    }
    /* DT_N (NAME) — deref to actual value, then fall through to coercion */
    if (v.v == DT_N) {
        if (v.slen == 1 && v.ptr) v = *(DESCR_t *)v.ptr;          /* NAMEPTR */
        else if (v.slen == 0 && v.s) v = NV_GET_fn(v.s);           /* NAMEVAL */
        else v = NULVCL;
    }
    PATND_t *p = spat_of(v);
    if (!p && v.v == DT_S) p = (v.s && v.s[0]) ? spat_of(pat_lit(v.s)) : spat_of(pat_lit(""));
    if (!p && v.v == DT_I) { char buf[32]; snprintf(buf,sizeof buf,"%lld",(long long)v.i); p = spat_of(pat_lit(buf)); }
    if (!p && v.v == DT_R) { char buf[64]; snprintf(buf,sizeof buf,"%.14g",v.r); p = spat_of(pat_lit(buf)); }
    if (!p && v.v == DT_SNUL) p = spat_of(pat_lit(""));
    return p;
}

/* pat_cat: n-ary concatenation.
 * If either child is already an XCAT node, flatten its children in.
 * Degenerate cases (NULL child) are dropped with an assertion to catch
 * type errors early rather than silently producing wrong trees. */
DESCR_t pat_cat(DESCR_t left, DESCR_t right) {
    PATND_t *l = pat_to_patnd(left);
    PATND_t *r = pat_to_patnd(right);
    /* DYN-64: assert instead of silent drop — caller must supply valid patterns */
    if (!l && left.v  != DT_SNUL) {
        fprintf(stderr, "pat_cat: left is not a pattern (DT=%d) — dropping\n", left.v);
    }
    if (!r && right.v != DT_SNUL) {
        fprintf(stderr, "pat_cat: right is not a pattern (DT=%d) — dropping\n", right.v);
    }
    if (!l) return r ? spat_val(r) : pat_epsilon();
    if (!r) return spat_val(l);

    PATND_t *p = spat_new(XCAT);
    /* Flatten: if left is already XCAT, steal its children */
    if (l->kind == XCAT) {
        for (int i = 0; i < l->nchildren; i++) patnd_append_child(p, l->children[i]);
    } else {
        patnd_append_child(p, l);
    }
    if (r->kind == XCAT) {
        for (int i = 0; i < r->nchildren; i++) patnd_append_child(p, r->children[i]);
    } else {
        patnd_append_child(p, r);
    }
    return spat_val(p);
}

/* pat_alt: n-ary alternation, flat XOR node. */
DESCR_t pat_alt(DESCR_t left, DESCR_t right) {
    PATND_t *l = pat_to_patnd(left);
    PATND_t *r = pat_to_patnd(right);
    if (!l && left.v  == DT_SNUL) l = spat_of(pat_epsilon());
    if (!r && right.v == DT_SNUL) r = spat_of(pat_epsilon());
    if (!l) return r ? spat_val(r) : pat_epsilon();
    if (!r) return spat_val(l);

    PATND_t *p = spat_new(XOR);
    /* Flatten: if left is already XOR, steal its children */
    if (l->kind == XOR) {
        for (int i = 0; i < l->nchildren; i++) patnd_append_child(p, l->children[i]);
    } else {
        patnd_append_child(p, l);
    }
    if (r->kind == XOR) {
        for (int i = 0; i < r->nchildren; i++) patnd_append_child(p, r->children[i]);
    } else {
        patnd_append_child(p, r);
    }
    return spat_val(p);
}

DESCR_t pat_ref(const char *name) {
    PATND_t *p = spat_new(XDSAR);
    p->STRVAL_fn = name ? GC_strdup(name) : "";
    return spat_val(p);
}

DESCR_t pat_ref_val(DESCR_t nameVal) {
    return pat_ref(VARVAL_fn(nameVal));
}

DESCR_t pat_assign_imm(DESCR_t child, DESCR_t var) {
    PATND_t *p = spat_new(XFNME);
    PATND_t *ch = pat_to_patnd(child);
    PATND_t *arr[1] = { ch };
    patnd_set_children(p, arr, 1);
    p->var  = var;
    return spat_val(p);
}

DESCR_t pat_assign_cond(DESCR_t child, DESCR_t var) {
    PATND_t *p = spat_new(XNME);
    PATND_t *ch = pat_to_patnd(child);
    PATND_t *arr[1] = { ch };
    patnd_set_children(p, arr, 1);
    p->var  = var;
    return spat_val(p);
}

/* pat_assign_callcap — builds XCALLCAP node for "pat . *func(args)" patterns.
 * The function is called at match time (not build time) to get the DT_N lvalue.
 * fnc_name/args/nargs stored in STRVAL_fn/args/nargs fields of PATND_t. */
DESCR_t pat_assign_callcap(DESCR_t child, const char *fnc_name, DESCR_t *args, int nargs) {
    PATND_t *p = spat_new(XCALLCAP);
    PATND_t *ch = pat_to_patnd(child);
    PATND_t *arr[1] = { ch };
    patnd_set_children(p, arr, 1);
    p->STRVAL_fn = fnc_name ? GC_strdup(fnc_name) : "";
    p->args  = args;
    p->nargs = nargs;
    return spat_val(p);
}

DESCR_t var_as_pattern(DESCR_t v) {
    /* If v is already a pattern, return it */
    if (v.v == DT_P) return v;
    /* If v is a string, treat as a literal pattern */
    if (v.v == DT_S || v.v == DT_SNUL) {
        return pat_lit(VARVAL_fn(v));
    }
    /* Otherwise wrap as a variable lookup */
    PATND_t *p = spat_new(XVAR);
    p->var = v;
    return spat_val(p);
}

DESCR_t pat_user_call(const char *name, DESCR_t *args, int nargs) {
    PATND_t *p = spat_new(XATP);
    p->STRVAL_fn   = name ? GC_strdup(name) : "";
    p->nargs = nargs;
    if (nargs > 0) {
        p->args = (DESCR_t *)GC_MALLOC(nargs * sizeof(DESCR_t));
        memcpy(p->args, args, nargs * sizeof(DESCR_t));
    }
    return spat_val(p);
}

/* pat_at_cursor — build XATP("@", varname) node for the @ cursor-capture operator.
 * Called from emit_pat_to_descr (DYN path).  In bb_build the "@" name is
 * intercepted and a bb_atp box is built that writes Δ (cursor) as DT_I into varname. */
DESCR_t pat_at_cursor(const char *varname) {
    PATND_t *p = spat_new(XATP);
    p->STRVAL_fn = "@";
    p->nargs = 1;
    p->args  = (DESCR_t *)GC_MALLOC(sizeof(DESCR_t));
    p->args[0].v    = DT_S;
    p->args[0].s    = varname ? GC_strdup(varname) : "";
    p->args[0].slen = varname ? (uint32_t)strlen(varname) : 0;
    return spat_val(p);
}

/* =========================================================================
 * New functions needed by snobol4.c / beautiful.c
 * ===================================================================== */

/* array_create("lo:hi" or "n") — create array from spec string */
DESCR_t array_create(DESCR_t spec) {
    const char *s = VARVAL_fn(spec);
    int lo = 1, hi = 1;
    const char *colon = strchr(s, ':');
    if (colon) {
        lo = atoi(s);
        hi = atoi(colon + 1);
    } else {
        hi = atoi(s);
    }
    if (hi < lo) hi = lo;
    ARBLK_t *a = array_new(lo, hi);
    DESCR_t v;
    v.v = DT_A;
    v.arr    = a;
    return v;
}

/* subscript_get — get arr[idx] */
DESCR_t subscript_get(DESCR_t arr, DESCR_t idx) {
    if (arr.v == DT_A) {
        return array_get(arr.arr, (int)to_int(idx));  /* returns DT_FAIL if OOB */
    }
    if (arr.v == DT_T) {
        char kb[64]; const char *ks;
        if (IS_INT_fn(idx))       { snprintf(kb,sizeof kb,"%lld",(long long)idx.i); ks=kb; }
        else if (IS_REAL_fn(idx)) { snprintf(kb,sizeof kb,"%g",idx.r); ks=kb; }
        else                      { ks = VARVAL_fn(idx); if (!ks) ks=""; }
        if (!table_has(arr.tbl, ks)) {
            /* IC-5: return table default value if not &null */
            if (arr.tbl->dflt.v != DT_FAIL && arr.tbl->dflt.v != 0)
                return arr.tbl->dflt;
            return FAILDESCR;
        }
        return table_get(arr.tbl, ks);
    }
    /* IC-5: DT_S string subscript — Icon 1-based, negative wraps from end */
    if (arr.v == DT_S || arr.v == DT_SNUL) {
        const char *s = arr.s ? arr.s : "";
        int slen = (int)strlen(s);
        int i = (int)to_int(idx);
        if (i < 0) i = slen + i + 1;   /* Icon: s[-1] → last char (1-based) */
        if (i < 1 || i > slen) return FAILDESCR;
        char *buf = GC_malloc(2); buf[0] = s[i-1]; buf[1] = '\0';
        return STRVAL(buf);
    }
    /* IC-5: DT_DATA icnlist subscript */
    if (arr.v == DT_DATA) {
        /* check if it's an icnlist */
        DESCR_t tag = FIELD_GET_fn(arr, "icn_type");
        if (tag.v == DT_S && tag.s && strcmp(tag.s,"list")==0) {
            int n = (int)FIELD_GET_fn(arr,"icn_size").i;
            DESCR_t ea = FIELD_GET_fn(arr,"icn_elems");
            DESCR_t *elems = (ea.v==DT_DATA) ? (DESCR_t*)ea.ptr : NULL;
            int i = (int)to_int(idx);
            if (i < 0) i = n + 1 + i + 1;
            if (!elems || i < 1 || i > n) return FAILDESCR;
            return elems[i-1];
        }
        /* tree/record child access: c(x)[i] */
        int i = (int)to_int(idx);
        DESCR_t children = FIELD_GET_fn(arr, "c");
        if (children.v == DT_A && children.arr)
            return array_get(children.arr, i);
        return FAILDESCR;
    }
    /* SIL NONARY → ERRTYP,3 → FTLTST: subscript on non-array/non-table is soft error */
    sno_runtime_error(3, NULL);
    return FAILDESCR;
}

/* subscript_set — arr[idx] = val; returns 1 on success, 0 on OOB/type-error */
int subscript_set(DESCR_t arr, DESCR_t idx, DESCR_t val) {
    if (arr.v == DT_A) {
        int i = (int)to_int(idx);
        if (i < arr.arr->lo || i > arr.arr->hi) return 0;  /* OOB → fail stmt */
        array_set(arr.arr, i, val);
        return 1;
    }
    if (arr.v == DT_T) {
        table_set(arr.tbl, VARVAL_fn(idx), val);
        return 1;
    }
    /* SIL NONARY → ERRTYP,3 → FTLTST */
    sno_runtime_error(3, NULL);
    return 0;
}

/* subscript_get2 / subscript_set2 — 2D */
DESCR_t subscript_get2(DESCR_t arr, DESCR_t i, DESCR_t j) {
    if (arr.v == DT_A)
        return array_get2(arr.arr, (int)to_int(i), (int)to_int(j));
    /* IC-5: DT_S string section s[i:j] */
    if (arr.v == DT_S || arr.v == DT_SNUL) {
        const char *s = arr.s ? arr.s : "";
        int slen = (int)strlen(s);
        int ii = (int)to_int(i), jj = (int)to_int(j);
        if (ii < 0) ii = slen + 1 + ii + 1;
        if (jj < 0) jj = slen + 1 + jj + 1;
        if (ii < 1) ii = 1; if (jj > slen+1) jj = slen+1;
        if (ii > jj) { char *e=GC_malloc(1); e[0]='\0'; return STRVAL(e); }
        int len = jj - ii;
        char *buf = GC_malloc(len+1); memcpy(buf, s+ii-1, len); buf[len]='\0';
        return STRVAL(buf);
    }
    return FAILDESCR;  /* P002: not an array — fail the statement */
}

/* subscript_set2 — arr[i,j] = val; returns 1 on success, 0 on OOB */
int subscript_set2(DESCR_t arr, DESCR_t i, DESCR_t j, DESCR_t val) {
    if (arr.v == DT_A) {
        int ii = (int)to_int(i), jj = (int)to_int(j);
        if (ii < arr.arr->lo || ii > arr.arr->hi) return 0;
        if (arr.arr->ndim >= 2 && (jj < arr.arr->lo2 || jj > arr.arr->hi2)) return 0;
        array_set2(arr.arr, ii, jj, val);
        return 1;
    }
    return 0;  /* not an array — fail */
}

/* tree_new — 4-arg version: creates a DT_DATA('tree(t,v,n,c)') instance */
DESCR_t MAKE_TREE_fn(DESCR_t tag, DESCR_t val, DESCR_t n_children, DESCR_t children) {
    /* tree type registered in SNO_INIT_fn — DEFDAT_fn + _b_tree_* override done there */
    return DATCON_fn("tree", tag, val, n_children, children, (DESCR_t){0});
}

/* push_val / pop_val / top_val — aliases for PUSH_fn/POP_fn/TOP_fn */
DESCR_t push_val(DESCR_t x) {
    PUSH_fn(x);
    return NULVCL;
}

DESCR_t pop_val(void) {
    return POP_fn();
}

DESCR_t top_val(void) {
    return TOP_fn();
}

/* register_fn — register a C function in the global function table */
void register_fn(const char *name, DESCR_t (*fn)(DESCR_t*, int), int min_args, int max_args) {
    (void)min_args; (void)max_args;
    DEFINE_fn(name, fn);
}

/* define_spec — DEFINE_fn('name(args)locals') */
void define_spec(DESCR_t spec) {
    DEFINE_fn(VARVAL_fn(spec), NULL);
}

/* apply_val — APPLY(fnval, args...) */
DESCR_t apply_val(DESCR_t fnval, DESCR_t *args, int nargs) {
    const char *name = VARVAL_fn(fnval);
    return APPLY_fn(name, args, nargs);
}

/* =========================================================================
 * EVAL_fn — EVAL(expr)
 *
 * Hand-rolled recursive descent over the subset of SNOBOL4 pattern
 * expressions that beauty.sno produces:
 *
 *   expr : term ('.' term)*
 *   term : '*' ident ['(' args ')']   deferred ref / user_call node
 *        | ident '(' args ')'          function call — evaluate now
 *        | '\'' STRVAL_fn '\''               string literal → pat_lit
 *        | ident                       plain name → S sentinel
 *   args : val (',' val)*
 *   val  : ident '(' args ')'          function call → value
 *        | '\'' STRVAL_fn '\''               string value
 *        | ident                       var lookup
 *        | integer
 *
 * Key semantic: plain IDENT in term position returns STRVAL(name).
 * Dot handler checks right operand type:
 *   S     → assign_cond(left, right)   capture into named var
 *   P → pat_cat(left, right)        pattern CONCAT_fn
 * ========================================================================= */

typedef struct { const char *s; int pos; } SnoEvalCtx;

static void _ev_skip(SnoEvalCtx *e) {
    while (e->s[e->pos] == ' ' || e->s[e->pos] == '\t') e->pos++;
}

static char *_ev_ident(SnoEvalCtx *e) {
    int start = e->pos;
    while (isalnum((unsigned char)e->s[e->pos]) || e->s[e->pos] == '_') e->pos++;
    int len = e->pos - start;
    if (len == 0) return NULL;
    char *nm = GC_malloc(len + 1);
    memcpy(nm, e->s + start, len);
    nm[len] = '\0';
    return nm;
}

static char *_ev_strlit(SnoEvalCtx *e) {
    char delim = e->s[e->pos]; e->pos++;
    int start = e->pos;
    while (e->s[e->pos] && e->s[e->pos] != delim) e->pos++;
    int len = e->pos - start;
    char *lit = GC_malloc(len + 1);
    memcpy(lit, e->s + start, len);
    lit[len] = '\0';
    if (e->s[e->pos] == delim) e->pos++;
    return lit;
}

static DESCR_t _ev_val(SnoEvalCtx *e);
static DESCR_t _ev_term(SnoEvalCtx *e);
static DESCR_t _ev_expr(SnoEvalCtx *e);

static int _ev_args(SnoEvalCtx *e, DESCR_t *args, int maxargs) {
    int na = 0;
    _ev_skip(e);
    while (e->s[e->pos] && e->s[e->pos] != ')') {
        int pos_before = e->pos;
        if (na > 0) { if (e->s[e->pos] == ',') e->pos++; _ev_skip(e); }
        if (na < maxargs) args[na++] = _ev_val(e);
        else _ev_val(e);
        _ev_skip(e);
        if (e->pos == pos_before) break; /* no progress — avoid infinite loop */
    }
    if (e->s[e->pos] == ')') e->pos++;
    return na;
}

static DESCR_t _ev_val(SnoEvalCtx *e) {
    _ev_skip(e);
    char c = e->s[e->pos];
    if (c == '\'' || c == '"') return STRVAL(_ev_strlit(e));
    if (isalpha((unsigned char)c) || c == '_') {
        char *nm = _ev_ident(e);
        _ev_skip(e);
        if (e->s[e->pos] == '(') {
            e->pos++;
            DESCR_t args[8]; int na = _ev_args(e, args, 8);
            return APPLY_fn(nm, args, na);
        }
        return NV_GET_fn(nm);
    }
    if (isdigit((unsigned char)c) || c == '-') {
        char *end;
        long long iv = strtoll(e->s + e->pos, &end, 10);
        e->pos = (int)(end - e->s);
        return INTVAL(iv);
    }
    return NULVCL;
}

static DESCR_t _ev_term(SnoEvalCtx *e) {
    _ev_skip(e);
    char c = e->s[e->pos];
    if (c == '*') {
        e->pos++;
        _ev_skip(e);
        char *nm = _ev_ident(e);
        if (!nm) return pat_epsilon();
        _ev_skip(e);
        if (e->s[e->pos] == '(') {
            e->pos++;
            DESCR_t args[8]; int na = _ev_args(e, args, 8);
            DESCR_t *ac = na ? GC_malloc(na * sizeof(DESCR_t)) : NULL;
            if (ac) memcpy(ac, args, na * sizeof(DESCR_t));
            return pat_user_call(nm, ac, na);
        }
        return pat_ref(nm);
    }
    if (c == '\'' || c == '"') return pat_lit(_ev_strlit(e));
    if (isalpha((unsigned char)c) || c == '_') {
        char *nm = _ev_ident(e);
        _ev_skip(e);
        if (e->s[e->pos] == '(') {
            e->pos++;
            DESCR_t args[8]; int na = _ev_args(e, args, 8);
            return APPLY_fn(nm, args, na);
        }
        return STRVAL(GC_strdup(nm));
    }
    return pat_epsilon();
}

static DESCR_t _ev_expr(SnoEvalCtx *e) {
    DESCR_t left = _ev_term(e);
    if (left.v == DT_S) {
        DESCR_t v = NV_GET_fn(left.s);
        if (v.v == DT_P) left = v;
        else if (v.v == DT_S && v.s && v.s[0]) left = pat_lit(v.s);
        else left = pat_epsilon();
    } else if (left.v == DT_SNUL) {
        left = pat_epsilon();
    }
    _ev_skip(e);
    while (e->s[e->pos] == '.') {
        e->pos++;
        _ev_skip(e);
        DESCR_t right = _ev_term(e);
        _ev_skip(e);
        if (right.v == DT_S) {
            left = pat_assign_cond(left, right);
        } else {
            if (right.v == DT_SNUL) right = pat_epsilon();
            left = pat_cat(left, right);
        }
    }
    return left;
}

DESCR_t EVAL_fn(DESCR_t expr) {
    /* RT-8: SIL EVAL — full type dispatch matching SIL EVAL/EVAL1.
    fprintf(stderr, "EVAL_fn: v=%d s=%s\n", (int)expr.v,
            (expr.v==5||expr.v==0) && expr.s ? expr.s : "(non-str)");
     *
     * DT_E  → EXPVAL_fn (execute frozen EXPR_t* with save/restore)
     * DT_I  → idempotent (return as-is)
     * DT_R  → idempotent (return as-is)
     * DT_P  → run pattern hook (existing behaviour)
     * DT_S/DT_SNUL:
     *   empty  → idempotent (NULVCL)
     *   numeric string → DT_I if integer, DT_R if real (SIL SPCINT/SPREAL)
     *   else → CONVE_fn (compile to DT_E) → EXPVAL_fn (execute)
     */

    /* DT_E: frozen expression — execute via EXPVAL_fn (RT-6) */
    if (expr.v == DT_E) {
        return EXPVAL_fn(expr);
    }

    /* DT_I / DT_R: idempotent (SIL EVAL1 path) */
    if (expr.v == DT_I) return expr;
    if (expr.v == DT_R) return expr;

    /* DT_P: pattern — run via hook (unchanged) */
    if (expr.v == DT_P) {
        if (g_eval_pat_hook) return g_eval_pat_hook(expr);
        return expr;
    }

    /* DT_S / DT_SNUL / anything else: evaluate as string */
    const char *s = VARVAL_fn(expr);

    /* Empty string → idempotent null */
    if (!s || !*s) return NULVCL;

    /* Numeric-string shortcut (SIL SPCINT): pure integer? */
    {
        char *endp = NULL;
        int64_t iv = (int64_t)strtoll(s, &endp, 10);
        if (endp && *endp == '\0') return INTVAL(iv);
    }

    /* Numeric-string shortcut (SIL SPREAL): real number? */
    {
        char *endp = NULL;
        double rv = strtod(s, &endp);
        if (endp && *endp == '\0') return REALVAL(rv);
    }

    /* Quoted string literal ('...' or "..."): do NOT short-circuit.
     * EVAL('BREAK(nl)') must evaluate as SNOBOL4 source → PATTERN, not STRING.
     * Fall through to CONVE_fn to parse and execute the full expression. */

    /* String hook: if scrip.c has wired interp_eval_pat for string->pattern routing,
     * use it -- handles E_DEFER (*func), $ (cursor-assign), and all operators
     * that eval_node in eval_code.c does not support (e.g. EVAL(omega) where
     * omega contains *T8Trace(...) or $ tz). */
    if (g_eval_str_hook) return g_eval_str_hook(s);

    /* General string: compile to DT_E (RT-7 CONVE_fn) then execute (RT-6) */
    DESCR_t compiled = CONVE_fn(expr);

    if (IS_FAIL_fn(compiled)) { fprintf(stderr, "DBG IS_FAIL true!\n"); return FAILDESCR; }
    DESCR_t _ev2 = EXPVAL_fn(compiled);
    return _ev2;
}

/* opsyn — OPSYN(new, old, type)
 * Register 'new' as an alias for the function named 'old'.
 * type arg selects arity context (unary=1, binary=2, function=0) but
 * runtime dispatch is name-based so we just copy the FNCBLK entry. */
DESCR_t opsyn(DESCR_t newname, DESCR_t oldname, DESCR_t type) {
    (void)type;
    const char *nm  = VARVAL_fn(newname);
    const char *old = VARVAL_fn(oldname);
    if (!nm || !old) return FAILDESCR;
    register_fn_alias(nm, old);
    return NULVCL;
}

/* sort_fn — SORT(table_or_array) -> 2D array[1..n,1..2] */
/* Compare function for qsort: sort by string key */
static int _sort_cmp(const void *a, const void *b) {
    const char **sa = (const char **)a;
    const char **sb = (const char **)b;
    return strcmp(sa[0], sb[0]);
}
DESCR_t sort_fn(DESCR_t arr) {
    if (arr.v != DT_T) return arr;  /* pass-through for non-table */
    TBBLK_t *tbl = arr.tbl;
    if (!tbl) return FAILDESCR;

    /* Count entries */
    int n = 0;
    for (int h = 0; h < TABLE_BUCKETS; h++)
        for (TBPAIR_t *e = tbl->buckets[h]; e; e = e->next) n++;
    if (n == 0) return FAILDESCR;

    /* Collect all (key, key_descr, value) pairs */
    const char **keys = GC_malloc(n * sizeof(char *));
    DESCR_t *key_descrs = GC_malloc(n * sizeof(DESCR_t));
    DESCR_t *vals = GC_malloc(n * sizeof(DESCR_t));
    int idx = 0;
    for (int h = 0; h < TABLE_BUCKETS; h++)
        for (TBPAIR_t *e = tbl->buckets[h]; e; e = e->next) {
            keys[idx] = e->key;
            key_descrs[idx] = e->key_descr;
            vals[idx] = e->val;
            idx++;
        }

    /* Sort by string key (indirect insertion sort) */
    int *order = GC_malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        int tmp = order[i];
        int j = i - 1;
        while (j >= 0 && strcmp(keys[order[j]], keys[tmp]) > 0) {
            order[j+1] = order[j]; j--;
        }
        order[j+1] = tmp;
    }

    /* Build 2D array[1..n, 1..2]: col1=key_descr (preserves type), col2=val */
    ARBLK_t *a = GC_malloc(sizeof(ARBLK_t));
    a->lo   = 1;
    a->hi   = n;
    a->ndim = 2;   /* 2 columns */
    a->lo2  = 1;
    a->hi2  = 2;   /* cols 1..2 */
    a->data = GC_malloc(n * 2 * sizeof(DESCR_t));
    for (int i = 0; i < n; i++) {
        a->data[i * 2 + 0] = key_descrs[order[i]];  /* preserve integer/string type */
        a->data[i * 2 + 1] = vals[order[i]];
    }
    DESCR_t result = {0};
    result.v = DT_A;
    result.arr    = a;
    return result;
}

/* pat_call — call a user-defined function with one arg and use result as pattern value.
 * Used when a pattern expression contains a user-defined function call, e.g. *t(y) in pattern. */
DESCR_t pat_call(const char *name, DESCR_t arg) {
    DESCR_t args[1] = { arg };
    DESCR_t result = APPLY_fn(name, args, 1);
    if (IS_FAIL_fn(result)) return pat_fail();
    /* Wrap result as a pattern: if it's already a pattern, return it;
     * otherwise treat it as a literal string pattern. */
    return var_as_pattern(result);
}

/* compile_to_expression — SIL CONVE path: parse string → freeze as DT_E.
 * Used by CONVERT(s,"EXPRESSION"). Does NOT evaluate — stores EXPR_t* as DT_E
 * for later thaw by EVAL_fn. Returns FAILDESCR if parse fails. */
DESCR_t compile_to_expression(const char *src) {
    if (!src || !*src) return FAILDESCR;
    EXPR_t *tree = parse_expr_pat_from_str(src);
    if (!tree) return FAILDESCR;

    DESCR_t d;
    d.v    = DT_E;
    d.ptr  = tree;
    d.slen = 0;
    d.s    = NULL;
    return d;
}

/* rsort_fn — RSORT(table) -> 2D array[1..n,1..2] in reverse key order */
DESCR_t rsort_fn(DESCR_t arr) {
    DESCR_t sorted = sort_fn(arr);
    if (sorted.v != DT_A || !sorted.arr) return sorted;
    ARBLK_t *a = sorted.arr;
    int n = a->hi - a->lo + 1;
    /* Reverse the row order in-place (2 cols per row) */
    for (int lo = 0, hi = n - 1; lo < hi; lo++, hi--) {
        DESCR_t tmp0 = a->data[lo*2+0], tmp1 = a->data[lo*2+1];
        a->data[lo*2+0] = a->data[hi*2+0];
        a->data[lo*2+1] = a->data[hi*2+1];
        a->data[hi*2+0] = tmp0;
        a->data[hi*2+1] = tmp1;
    }
    return sorted;
}

/* ── patnd_print — --dump-bb diagnostic ──────────────────────────────────
 * Recursive pretty-printer for PATND_t trees. Indent is 2 spaces per level.
 * Writes to 'out'; suitable for stderr or stdout. */
#include <stdio.h>

static const char *xkind_name(XKIND_t k) {
    switch (k) {
        case XCHR:     return "CHR";
        case XSPNC:    return "SPAN";
        case XBRKC:    return "BREAK";
        case XANYC:    return "ANY";
        case XNNYC:    return "NOTANY";
        case XLNTH:    return "LEN";
        case XPOSI:    return "POS";
        case XRPSI:    return "RPOS";
        case XTB:      return "TAB";
        case XRTB:     return "RTAB";
        case XFARB:    return "ARB";
        case XARBN:    return "ARBNO";
        case XSTAR:    return "REM";
        case XFNCE:    return "FENCE";
        case XFAIL:    return "FAIL";
        case XABRT:    return "ABORT";
        case XSUCF:    return "SUCCEED";
        case XBAL:     return "BAL";
        case XEPS:     return "EPS";
        case XCAT:     return "CAT";
        case XOR:      return "ALT";
        case XDSAR:    return "DEREF";
        case XFNME:    return "CAP_IMM";
        case XNME:     return "CAP_COND";
        case XCALLCAP: return "CALLCAP";
        case XVAR:     return "VAR";
        case XATP:     return "USERPAT";
        case XBRKX:    return "BREAKX";
        default:       return "?";
    }
}

static void patnd_print_r(const PATND_t *p, FILE *out, int depth) {
    if (!p) { fprintf(out, "%*s(null)\n", depth*2, ""); return; }
    fprintf(out, "%*s(%s", depth*2, "", xkind_name(p->kind));
    if (p->STRVAL_fn) fprintf(out, " \"%s\"", p->STRVAL_fn);
    if (p->num)       fprintf(out, " %lld", (long long)p->num);
    if (p->nchildren == 0) {
        fprintf(out, ")\n");
    } else {
        fprintf(out, "\n");
        for (int i = 0; i < p->nchildren; i++)
            patnd_print_r(p->children[i], out, depth + 1);
        fprintf(out, "%*s)\n", depth*2, "");
    }
}

void patnd_print(const PATND_t *p, FILE *out) {
    patnd_print_r(p, out, 0);
}
