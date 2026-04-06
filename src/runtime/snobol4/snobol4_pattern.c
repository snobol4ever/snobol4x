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
#include "../engine.h"
#include "../../ir/ir.h"         /* ir.h first — sets EXPR_T_DEFINED so scrip_cc.h skips its own EXPR_t */
#include "../../frontend/snobol4/scrip_cc.h"
#include "../../frontend/snobol4/CMPILE.c"

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
    PATND_t *p = spat_new(XLNTH);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_pos(int64_t n) {
    PATND_t *p = spat_new(XPOSI);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_rpos(int64_t n) {
    PATND_t *p = spat_new(XRPSI);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_tab(int64_t n) {
    PATND_t *p = spat_new(XTB);
    p->num = n;
    return spat_val(p);
}

DESCR_t pat_rtab(int64_t n) {
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
        if (frozen && frozen->kind == E_FNC) {
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
            return spat_of(pv);
        }
        if (frozen && frozen->kind == E_VAR && frozen->sval) {
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
    PATND_t *p = spat_of(v);
    if (!p && v.v == DT_S && v.s && v.s[0]) p = spat_of(pat_lit(v.s));
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
 * Pattern materialisation — convert PATND_t tree to engine Pattern* tree
 *
 * The engine uses malloc'd Pattern nodes that are freed after each MATCH_fn.
 * We materialise lazily: deferred refs are resolved from the var table NOW.
 * Capture variables are recorded in a capture list, applied after MATCH_fn.
 * ===================================================================== */

typedef struct {
    char   *var_name;   /* variable name to assign to (static, or NULL if deferred) */
    /* Deferred var name: when var is *FuncCall(), evaluate at APPLY_fn time */
    char *(*var_fn)(void *data); /* if set, call this at APPLY_fn time to get var name */
    void   *var_data;   /* userdata for var_fn */
    int     start;      /* MATCH_fn start cursor */
    int     end;        /* MATCH_fn end cursor */
    int     is_imm;        /* 1 = immediate ($), 0 = conditional (.) */
    int     is_cursor_cap; /* 1 = @var — write cursor pos as integer, not substring */
} Capture;

#define MAX_CAPTURES 64

/* Cache of already-materialised patterns keyed by PATND_t* identity.
 * Prevents var_resolve_callback from re-materialising the same variable
 * (e.g. snoCommand) on every ARBNO iteration.  nInc/nPush/nPop fire ONCE
 * at materialise time (correct — they set up capture wrappers), not N times. */
#define VAR_CACHE_MAX 32
typedef struct {
    PATND_t *sp;    /* original PATND_t* (identity key) */
    Pattern    *root;  /* materialised Pattern* tree (owned by pl) */
} VarCacheEntry;

typedef struct {
    PatternList    pl;
    Capture        captures[MAX_CAPTURES];
    int            ncaptures;
    const char    *subject;       /* the subject string being matched */
    int            scan_start;    /* current scan start offset for POS/RPOS adjustment */
    VarCacheEntry  var_cache[VAR_CACHE_MAX];
    int            var_cache_n;
} MatchCtx;

/* Forward decl */

/* T_FUNC callback for XATP deferred functions.
 *
 * Used for functions that must fire at MATCH TIME not materialise time:
 *   nInc, nPush, nPop — return P (a capture-wrapper/epsilon); also have
 *                        stack side-effects (increment counter, PUSH_fn/POP_fn frame).
 *   Reduce            — returns DT_SNUL; pops parse stack and builds tree node.
 *
 * If the function returns P, we run it as a zero-width sub-MATCH_fn
 * against the current subject string so its captures fire correctly.
 * DT_FAIL (FRETURN) => -1 => engine CONCEDE.
 * All other returns => succeed zero-width. */
typedef struct {
    const char  *name;
    DESCR_t      *args;
    int          nargs;
    /* Set at MATCH_fn time by the engine dispatcher in match_pattern.
     * Points to the current subject being matched — used for P sub-MATCH_fn. */
    const char  *subject;
} UCData;

static void *user_call_fn(void *userdata) {
    UCData *d = (UCData *)userdata;
    if (getenv("PAT_DEBUG")) fprintf(stderr, "  user_call_fn: %s\n", d->name);
    DESCR_t r = APPLY_fn(d->name, d->args, d->nargs);
    if (r.v == DT_FAIL) return (void *)(intptr_t)-1;
    if (r.v == DT_P && r.p) {
        /* Run the returned pattern as a zero-width sub-MATCH_fn.
         * nInc/nPush/nPop return epsilon . *Fn() wrappers — matching against ""
         * fires the capture which records the counter/frame. */
        const char *subj = d->subject ? d->subject : "";
        match_pattern(r, subj);
    }
    return (void *)1;
}

/* Return 1 if this function must be deferred to MATCH_fn time.
 * All others are safe to call eagerly at materialise time. */
static int is_sideeffect_fn(const char *name) {
    if (!name) return 0;
    static const char *deferred[] = {
        "nInc", "nPop", "nPush", "Reduce", NULL
    };
    for (int i = 0; deferred[i]; i++)
        if (strcmp(name, deferred[i]) == 0) return 1;
    return 0;
}

/* Deferred var name evaluation: call SNOBOL4 function at apply_captures time
 * to get the variable name for a capture.  Used for  pat . *FuncCall()  where
 * the function's return value names the target variable. */
static char *deferred_var_fn(void *data) {
    UCData *d = (UCData *)data;
    DESCR_t r = APPLY_fn(d->name, d->args, d->nargs);
    if (r.v == DT_S && r.s && r.s[0]) return (char *)r.s;
    if (r.v == DT_SNUL) return NULL; /* NRETURN — no assignment */
    return NULL;
}

static Pattern *materialise(PATND_t *sp, MatchCtx *ctx);

/* Build a Σ (sequence/concatenation) node */
static Pattern *make_seq(PatternList *pl, Pattern *a, Pattern *b) {
    if (!a) return b;
    if (!b) return a;
    Pattern *p = pattern_alloc(pl);
    p->type = T_SIGMA;
    p->n    = 2;
    p->children[0] = a;
    p->children[1] = b;
    return p;
}

/* Build a Π (alternation) node */
static Pattern *make_alt(PatternList *pl, Pattern *a, Pattern *b) {
    if (!a) return b;
    if (!b) return a;
    Pattern *p = pattern_alloc(pl);
    p->type = T_PI;
    p->n    = 2;
    p->children[0] = a;
    p->children[1] = b;
    return p;
}

/* Epsilon node */
static Pattern *make_epsilon(PatternList *pl) {
    Pattern *p = pattern_alloc(pl);
    p->type = T_EPSILON;
    return p;
}

/* Deferred USER_CALL: data passed to T_FUNC callback at MATCH_fn time */
typedef struct {
    const char *name;
    DESCR_t     *args;
    int         nargs;
} DeferredCall;

/* T_FUNC callback: called by engine at MATCH_fn time (zero-width, side-effect) */
static void *deferred_call_fn(void *userdata) {
    DeferredCall *d = (DeferredCall *)userdata;
    /* Special handling for reduce/Reduce: evaluate string args at MATCH_fn time */
    if (d->nargs >= 2 && strcasecmp(d->name, "reduce") == 0) {
        DESCR_t t_arg = d->args[0];
        DESCR_t n_arg = d->args[1];
        /* Strip outer quotes from type string */
        if (t_arg.v == DT_S && t_arg.s) {
            const char *ts = t_arg.s;
            int tlen = (int)strlen(ts);
            if (tlen >= 2 &&
                ((ts[0]=='\'' && ts[tlen-1]=='\'') ||
                 (ts[0]=='"'  && ts[tlen-1]=='"'))) {
                char *stripped = GC_malloc(tlen - 1);
                memcpy(stripped, ts + 1, tlen - 2);
                stripped[tlen-2] = '\0';
                t_arg = STRVAL(stripped);
            }
        }
        /* Evaluate count expression at MATCH_fn time */
        if (n_arg.v == DT_S)
            n_arg = EVAL_fn(n_arg);
        DESCR_t reduce_args[2] = { t_arg, n_arg };
        APPLY_fn("Reduce", reduce_args, 2);
        return (void *)1; /* succeed */
    }
    /* All other calls: fire and propagate failure if function fails.
     * Engine sentinel: NULL or (void*)-1 = fail, anything else = succeed.
     * engine.c T_FUNC checks r==(void*)(intptr_t)-1 for failure. */
    {
        DESCR_t _r = APPLY_fn(d->name, d->args, d->nargs);
        return IS_FAIL_fn(_r) ? (void *)(intptr_t)-1 : (void *)1;
    }
}

static Pattern *make_func(PatternList *pl, const char *name, DESCR_t *args, int nargs) {
    Pattern *p = pattern_alloc(pl);
    p->type = T_FUNC;
    DeferredCall *d = GC_malloc(sizeof(DeferredCall));
    d->name  = name;
    d->args  = args;
    d->nargs = nargs;
    p->func      = deferred_call_fn;
    p->func_data = d;
    return p;
}

static Pattern *materialise(PATND_t *sp, MatchCtx *ctx) {
    if (!sp) return make_epsilon(&ctx->pl);

    Pattern *p = pattern_alloc(&ctx->pl);

    switch (sp->kind) {

    case XCHR:
        p->type  = T_LITERAL;
        p->s     = sp->STRVAL_fn ? sp->STRVAL_fn : "";
        p->s_len = (int)strlen(p->s);
        return p;

    case XSPNC:
        p->type  = T_SPAN;
        p->chars = sp->STRVAL_fn ? sp->STRVAL_fn : "";
        return p;

    case XBRKC:
        p->type  = T_BREAK;
        p->chars = sp->STRVAL_fn ? sp->STRVAL_fn : "";
        return p;

    case XBRKX:
        p->type  = T_BREAKX;
        p->chars = sp->STRVAL_fn ? sp->STRVAL_fn : "";
        return p;

    case XANYC:
        p->type  = T_ANY;
        p->chars = sp->STRVAL_fn ? sp->STRVAL_fn : "";
        return p;

    case XNNYC:
        p->type  = T_NOTANY;
        p->chars = sp->STRVAL_fn ? sp->STRVAL_fn : "";
        return p;

    case XLNTH:
        p->type = T_LEN;
        p->n    = (int)sp->num;
        return p;

    case XPOSI:
        p->type = T_POS;
        /* Adjust for scan offset: POS(n) in original = POS(n - scan_start) in sub-string.
         * If n < scan_start, this can never MATCH_fn from this offset. */
        p->n = (int)(sp->num - ctx->scan_start);
        if (p->n < 0) { p->type = T_FAIL; p->n = 0; }
        return p;

    case XRPSI:
        /* RPOS(n) is from end — not affected by scan offset, but we need full subject len.
         * Engine uses OMEGA - DELTA, so RPOS works correctly relative to sub-string end.
         * However the sub-string end = original end, so this is correct as-is. */
        p->type = T_RPOS;
        p->n    = (int)sp->num;
        return p;

    case XTB:
        p->type = T_TAB;
        p->n    = (int)sp->num;
        return p;

    case XRTB:
        p->type = T_RTAB;
        p->n    = (int)sp->num;
        return p;

    case XFARB:
        p->type = T_ARB;
        return p;

    case XSTAR:
        p->type = T_REM;
        return p;

    case XEPS:
        p->type = T_EPSILON;
        return p;

    case XFAIL:
        p->type = T_FAIL;
        return p;

    case XABRT:
        p->type = T_ABORT;
        return p;

    case XSUCF:
        p->type = T_SUCCEED;
        return p;

    case XBAL:
        p->type = T_BAL;
        return p;

    case XFNCE:
        p->type = T_FENCE;
        p->n    = 0;
        if (sp->nchildren > 0 && sp->children[0]) {
            /* FENCE(p) — fence with sub-pattern */
            p->n = 1;
            p->children[0] = materialise(sp->children[0], ctx);
        }
        return p;

    case XARBN:
        p->type = T_ARBNO;
        p->n    = 1;
        p->children[0] = (sp->nchildren > 0 && sp->children[0])
                         ? materialise(sp->children[0], ctx)
                         : make_epsilon(&ctx->pl);
        return p;

    case XCAT: {
        /* Fold right: seq(children[0], seq(children[1], ... children[n-1])) */
        if (sp->nchildren == 0) return make_epsilon(&ctx->pl);
        Pattern *acc = materialise(sp->children[sp->nchildren - 1], ctx);
        for (int i = sp->nchildren - 2; i >= 0; i--)
            acc = make_seq(&ctx->pl, materialise(sp->children[i], ctx), acc);
        return acc;
    }

    case XOR: {
        /* Fold right: alt(children[0], alt(children[1], ... children[n-1])) */
        if (sp->nchildren == 0) return make_epsilon(&ctx->pl);
        Pattern *acc = materialise(sp->children[sp->nchildren - 1], ctx);
        for (int i = sp->nchildren - 2; i >= 0; i--)
            acc = make_alt(&ctx->pl, materialise(sp->children[i], ctx), acc);
        return acc;
    }

    case XDSAR: {
        if (getenv("PAT_DEBUG") && sp->STRVAL_fn)
            fprintf(stderr, "  XDSAR '%s' -> type=%d\n", sp->STRVAL_fn,
                NV_GET_fn(sp->STRVAL_fn).v);
        /* Resolve *name from variable table NOW */
        DESCR_t v = NV_GET_fn(sp->STRVAL_fn);
        if (v.v == DT_P) {
            /* Cycle detection: track variable names being materialised.
             * Recursive patterns (e.g. term = *factor addop *term . *Binary())
             * must not expand infinitely at materialise time.
             * On a cycle, emit T_VARREF instead of epsilon — the engine
             * re-resolves the variable lazily at each PROCEED, so right-recursive
             * grammars work correctly: predecessor nodes consume input first,
             * then T_VARREF fires when the recursive alternative is actually tried. */
            #define MAT_MAX_DEPTH 64
            static __thread const char *_mat_stack[MAT_MAX_DEPTH];
            static __thread int _mat_top = 0;
            for (int _i = 0; _i < _mat_top; _i++) {
                if (_mat_stack[_i] == sp->STRVAL_fn ||
                        strcmp(_mat_stack[_i], sp->STRVAL_fn) == 0) {
                    /* cycle: emit T_VARREF for lazy engine-level resolution */
                    p->type = T_VARREF;
                    p->s    = sp->STRVAL_fn;
                    return p;
                }
            }
            if (_mat_top < MAT_MAX_DEPTH) _mat_stack[_mat_top++] = sp->STRVAL_fn;
            PATND_t *sp2 = spat_of(v);
            Pattern *result = materialise(sp2, ctx);
            if (_mat_top > 0) _mat_top--;
            return result;
        }
        /* Variable holds a string — treat as literal */
        if (v.v == DT_S || v.v == DT_SNUL) {
            p->type  = T_LITERAL;
            p->s     = VARVAL_fn(v);
            p->s_len = (int)strlen(p->s);
            return p;
        }
        /* Pattern not set yet — epsilon (fail gracefully) */
        return make_epsilon(&ctx->pl);
    }

    case XVAR: {
        /* Variable holding a pattern value */
        DESCR_t v = sp->var;
        if (v.v == DT_P) {
            return materialise(spat_of(v), ctx);
        }
        if (v.v == DT_S || v.v == DT_SNUL) {
            p->type  = T_LITERAL;
            p->s     = VARVAL_fn(v);
            p->s_len = (int)strlen(p->s);
            return p;
        }
        return make_epsilon(&ctx->pl);
    }

    case XFNME:
    case XNME: {
        /*
         * Capture assignment: PAT . var  (conditional) or  PAT $ var  (immediate).
         *
         * We materialise this as a T_CAPTURE node wrapping the child. T_CAPTURE fires
         * a callback with (cap_slot, start, end) when the child succeeds. The cap_slot
         * is the INDEX_fn into ctx->captures[] where we stored the variable name.
         *
         * The callback (capture_callback below) is called by engine_match_ex, reads
         * ctx->captures[cap_slot].var_name, and calls NV_SET_fn with the matched span.
         */
        if (ctx->ncaptures >= MAX_CAPTURES) {
            /* Too many captures — degrade to no-capture (child only) */
            return (sp->nchildren > 0 && sp->children[0])
                   ? materialise(sp->children[0], ctx)
                   : make_epsilon(&ctx->pl);
        }

        /* Record capture metadata */
        const char *vname = NULL;
        DESCR_t vv = sp->var;
        int slot = ctx->ncaptures;
        ctx->captures[slot].var_name = NULL;
        ctx->captures[slot].var_fn   = NULL;
        ctx->captures[slot].var_data = NULL;
        if (vv.v == DT_S) {
            vname = vv.s;
            ctx->captures[slot].var_name = vname ? GC_strdup(vname) : NULL;
        } else if (vv.v == DT_P) {
            /* Deferred: var is *FuncCall() — evaluate at apply_captures time */
            PATND_t *vsp = spat_of(vv);
            if (vsp && vsp->kind == XATP) {
                UCData *d = (UCData *)GC_MALLOC(sizeof(UCData));
                d->name  = vsp->STRVAL_fn;
                d->nargs = vsp->nargs;
                d->args  = NULL;
                if (vsp->nargs > 0) {
                    d->args = (DESCR_t *)GC_MALLOC(vsp->nargs * sizeof(DESCR_t));
                    memcpy(d->args, vsp->args, vsp->nargs * sizeof(DESCR_t));
                }
                ctx->captures[slot].var_fn   = deferred_var_fn;
                ctx->captures[slot].var_data = d;
            }
        }
        ctx->captures[slot].is_imm        = (sp->kind == XFNME);
        ctx->captures[slot].is_cursor_cap  = 0;
        ctx->captures[slot].start          = -1;
        ctx->captures[slot].end            = -1;
        ctx->ncaptures++;

        /* Materialise child */
        Pattern *child = (sp->nchildren > 0 && sp->children[0])
                         ? materialise(sp->children[0], ctx)
                         : make_epsilon(&ctx->pl);

        /* Wrap in T_CAPTURE node */
        Pattern *cap = pattern_alloc(&ctx->pl);
        cap->type        = T_CAPTURE;
        cap->n           = slot;      /* cap_slot: which capture[] entry to fill */
        cap->children[0] = child;
        return cap;
    }

    case XATP: {
        if (getenv("PAT_DEBUG"))
            fprintf(stderr, "XATP %s\n", sp->STRVAL_fn);

        /* Primitive pattern builtins: evaluate args now and build proper T_* node.
         * These are NOT side-effect functions — they return a pattern value.
         * ANY(chars), SPAN(chars), BREAK(chars), NOTANY(chars): arg[0] = charset string.
         * LEN(n): arg[0] = integer.
         * POS(n), RPOS(n), TAB(n), RTAB(n): arg[0] = integer. */
        const char *nm = sp->STRVAL_fn ? sp->STRVAL_fn : "";

        if ((strcasecmp(nm, "ANY")    == 0 ||
             strcasecmp(nm, "SPAN")   == 0 ||
             strcasecmp(nm, "BREAK")  == 0 ||
             strcasecmp(nm, "NOTANY") == 0) && sp->nargs >= 1) {
            /* Evaluate charset arg at materialise time */
            DESCR_t cv = sp->args[0];
            const char *chars = (cv.v == DT_S && cv.s) ? cv.s : "";
            p->chars = GC_strdup(chars);
            if      (strcasecmp(nm, "ANY")    == 0) p->type = T_ANY;
            else if (strcasecmp(nm, "SPAN")   == 0) p->type = T_SPAN;
            else if (strcasecmp(nm, "BREAK")  == 0) p->type = T_BREAK;
            else                                     p->type = T_NOTANY;
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  → %s chars='%.20s'\n", nm, p->chars);
            return p;
        }

        if ((strcasecmp(nm, "LEN")   == 0 ||
             strcasecmp(nm, "POS")   == 0 ||
             strcasecmp(nm, "RPOS")  == 0 ||
             strcasecmp(nm, "TAB")   == 0 ||
             strcasecmp(nm, "RTAB")  == 0) && sp->nargs >= 1) {
            DESCR_t nv = sp->args[0];
            int n = (nv.v == DT_I) ? (int)nv.i : (int)to_int(nv);
            p->n = n;
            if      (strcasecmp(nm, "LEN")  == 0) { p->type = T_LEN;  }
            else if (strcasecmp(nm, "POS")  == 0) {
                /* Adjust for scan offset */
                p->n = n - ctx->scan_start;
                if (p->n < 0) { p->type = T_FAIL; p->n = 0; }
                else p->type = T_POS;
            }
            else if (strcasecmp(nm, "RPOS") == 0) { p->type = T_RPOS; }
            else if (strcasecmp(nm, "TAB")  == 0) { p->type = T_TAB;  }
            else                                   { p->type = T_RTAB; }
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  → %s n=%d\n", nm, p->n);
            return p;
        }

        /* Special: "@" = cursor-position capture — write cursor as integer to varname */
        if (strcmp(nm, "@") == 0 && sp->nargs >= 1) {
            const char *vname = (sp->args[0].v == DT_S && sp->args[0].s)
                                ? GC_strdup(sp->args[0].s) : NULL;
            if (vname && vname[0]) {
                int slot = ctx->ncaptures;
                if (slot < MAX_CAPTURES) {
                    ctx->captures[slot].var_name      = (char *)vname;
                    ctx->captures[slot].var_fn        = NULL;
                    ctx->captures[slot].var_data      = NULL;
                    ctx->captures[slot].start         = -1;
                    ctx->captures[slot].end           = -1;
                    ctx->captures[slot].is_imm        = 0;
                    ctx->captures[slot].is_cursor_cap = 1;
                    ctx->ncaptures++;
                    Pattern *ep  = make_epsilon(&ctx->pl);
                    Pattern *cap = pattern_alloc(&ctx->pl);
                    cap->type        = T_CAPTURE;
                    cap->n           = slot;
                    cap->children[0] = ep;
                    return cap;
                }
            }
        }

        /* All other calls (nInc, nPush, nPop, Reduce, match, etc.):
         * Defer to match time via T_FUNC — side-effect calls must fire
         * when the engine reaches this node, NOT during materialise(). */
        return make_func(&ctx->pl, sp->STRVAL_fn, sp->args, sp->nargs);
    }

    default:
        return make_epsilon(&ctx->pl);
    }
}

/* =========================================================================
 * capture_callback — fired by engine_match_ex when a T_CAPTURE node succeeds
 * ===================================================================== */
static void capture_callback(int cap_slot, int start, int end, void *userdata) {
    MatchCtx *ctx = (MatchCtx *)userdata;
    if (cap_slot < 0 || cap_slot >= ctx->ncaptures) return;
    Capture *cap = &ctx->captures[cap_slot];
    cap->start = start + ctx->scan_start;
    cap->end   = end   + ctx->scan_start;
    if (getenv("PAT_DEBUG"))
        fprintf(stderr, "  CAPTURE_CB: slot=%d var=%s start=%d end=%d\n",
            cap_slot, cap->var_name ? cap->var_name : "(null)", cap->start, cap->end);
}

/* Apply captures: call NV_SET_fn for each fired capture */
static void apply_captures(MatchCtx *ctx) {
    for (int i = 0; i < ctx->ncaptures; i++) {
        Capture *cap = &ctx->captures[i];
        if (cap->start < 0) continue;

        /* Resolve variable name: static or deferred */
        const char *vname = cap->var_name;
        if (!vname && cap->var_fn) {
            vname = cap->var_fn(cap->var_data);
        }
        if (!vname || !vname[0]) continue;

        /* @var cursor capture: write position as integer */
        if (cap->is_cursor_cap) {
            NV_SET_fn(vname, INTVAL((int64_t)cap->start));
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "CURSOR_CAP: %s = %d\n", vname, cap->start);
            continue;
        }

        int len = cap->end - cap->start;
        if (len < 0) len = 0;
        char *text = (char *)GC_MALLOC(len + 1);
        if (ctx->subject)
            memcpy(text, ctx->subject + cap->start, len);
        text[len] = '\0';
        NV_SET_fn(vname, BSTRVAL(text, len));
        if (getenv("PAT_DEBUG"))
            fprintf(stderr, "CAPTURE: %s = \"%.*s\"\n", vname, len, text);
    }
}

/* =========================================================================
 * match_pattern — TOP_fn-level MATCH_fn entry point
 * ===================================================================== */

/* Callback for T_VARREF resolution: look up variable by name and materialise.
 * userdata is MatchCtx*. Returns epsilon if variable not set or not a pattern. */
static Pattern *var_resolve_callback(const char *name, void *userdata) {
    MatchCtx *ctx = (MatchCtx *)userdata;
    DESCR_t v = NV_GET_fn(name);
    /* DT_E: variable holds a frozen EXPRESSION — thaw to pattern value.
     * This is the *factor / *term / *expr deferred-variable case in
     * recursive-descent parsers built from patterns (SIL EXPVAL path). */
    if (v.v == DT_E)
        v = PATVAL_fn(v);
    if (v.v != DT_P) {
        /* not a pattern — return epsilon */
        Pattern *ep = pattern_alloc(&ctx->pl);
        ep->type = T_EPSILON;
        return ep;
    }
    PATND_t *sp = spat_of(v);
    if (!sp) {
        Pattern *ep = pattern_alloc(&ctx->pl);
        ep->type = T_EPSILON;
        return ep;
    }
    /* Cache lookup: if we've already materialised this PATND_t* during this
     * MATCH_fn, return the cached Pattern* tree.  This prevents nInc/nPush/nPop
     * (and any other XATP nodes) from firing on every ARBNO iteration
     * that re-resolves the same variable (e.g. snoCommand).
     * The tree is identical every time — only the first materialise call is needed. */
    for (int i = 0; i < ctx->var_cache_n; i++)
        if (ctx->var_cache[i].sp == sp)
            return ctx->var_cache[i].root;
    Pattern *root = materialise(sp, ctx);
    if (ctx->var_cache_n < VAR_CACHE_MAX) {
        ctx->var_cache[ctx->var_cache_n].sp   = sp;
        ctx->var_cache[ctx->var_cache_n].root = root;
        ctx->var_cache_n++;
    }
    return root;
}

/* Internal: try MATCH_fn using a pre-materialised root at a single starting offset.
 * Does NOT call materialise — caller must do that once. */
static int try_match_at_root(Pattern *root, const char *subject, int slen, int start, MatchCtx *ctx) {
    ctx->scan_start = start;
    EngineOpts opts;
    opts.cap_fn    = (ctx->ncaptures > 0) ? capture_callback : NULL;
    opts.cap_data  = ctx;
    opts.var_fn    = var_resolve_callback;
    opts.var_data  = ctx;
    opts.scan_start = start;
    MatchResult mr = engine_match_ex(root, subject + start, slen - start, &opts);
    if (getenv("PAT_DEBUG") && slen < 20)
        fprintf(stderr, "  try_match_at: start=%d slen=%d -> matched=%d end=%d\n",
            start, slen, mr.matched, mr.end);
    if (mr.matched) {
        apply_captures(ctx);
        return start + mr.end;
    }
    return -1;
}

/* Internal: materialise then try MATCH_fn at a single starting offset.
 * Used for match_and_replace and other single-shot callers. */
static int try_match_at(PATND_t *sp, const char *subject, int slen, int start, MatchCtx *ctx) {
    ctx->scan_start = start;
    Pattern *root = materialise(sp, ctx);
    EngineOpts opts;
    opts.cap_fn   = (ctx->ncaptures > 0) ? capture_callback : NULL;
    opts.cap_data = ctx;
    opts.var_fn   = var_resolve_callback;
    opts.var_data = ctx;
    opts.scan_start = start;
    MatchResult mr = engine_match_ex(root, subject + start, slen - start, &opts);
    if (getenv("PAT_DEBUG") && slen < 20)
        fprintf(stderr, "  try_match_at: start=%d slen=%d -> matched=%d end=%d\n",
            start, slen, mr.matched, mr.end);
    if (mr.matched) {
        apply_captures(ctx);
        return start + mr.end;
    }
    return -1;
}

int match_pattern(DESCR_t pat, const char *subject) {
    if (!subject) subject = "";
    int _dbg = getenv("PAT_DEBUG") && strlen(subject) < 20;
    if (_dbg) fprintf(stderr, "match_pattern: subj=(%zu) pat.v=%d\n",
        strlen(subject), pat.v);

    PATND_t *sp = spat_of(pat);
    if (!sp) {
        if (pat.v == DT_S) {
            const char *lit = pat.s ? pat.s : "";
            return strstr(subject, lit) != NULL;
        }
        return 0;
    }

    int slen = (int)strlen(subject);

    /* Materialise the pattern ONCE (scan_start=0 for absolute POS/TAB values).
     * This prevents side-effect functions (Reduce, nInc, etc.) inside XATP
     * from being called N times (once per scan position). The engine receives
     * opts.scan_start per position so T_POS/T_TAB compute correctly. */
    MatchCtx mat_ctx; memset(&mat_ctx, 0, sizeof(mat_ctx)); mat_ctx.subject = subject;
    Pattern *root = materialise(sp, &mat_ctx);

    /* SNOBOL4 unanchored MATCH_fn: try each starting position.
     * Reuse mat_ctx (not a fresh ctx) so captures[] registered during
     * materialise() survive into apply_captures() on success.
     * Reset only captures[i].start/end between attempts. */
    if (kw_anchor) {
        for (int i = 0; i < mat_ctx.ncaptures; i++)
            mat_ctx.captures[i].start = mat_ctx.captures[i].end = -1;
        int r = try_match_at_root(root, subject, slen, 0, &mat_ctx);
        pattern_free_all(&mat_ctx.pl);
        return r >= 0;
    }

    for (int start = 0; start <= slen; start++) {
        for (int i = 0; i < mat_ctx.ncaptures; i++)
            mat_ctx.captures[i].start = mat_ctx.captures[i].end = -1;
        int r = try_match_at_root(root, subject, slen, start, &mat_ctx);
        if (r >= 0) {
            pattern_free_all(&mat_ctx.pl);
            return 1;
        }
        if (start == slen) break;
    }
    pattern_free_all(&mat_ctx.pl);
    return 0;
}

/* =========================================================================
 * match_and_replace — pattern MATCH_fn with replacement
 *
 * Matches pat against *subject. On success, replaces the matched portion
 * with replacement and updates *subject. Returns 1 on success, 0 on fail.
 * ===================================================================== */

/* =========================================================================
 * match_pattern_at — cursor-aware anchored MATCH_fn for E_INDIRECT / *varname
 *
 * Matches pat against subject starting at position cursor (anchored — no
 * scan loop).  Returns the new cursor position (>= cursor) on success, or
 * -1 on failure.  Used by compiled Byrd boxes for indirect pattern refs.
 * ========================================================================= */

int match_pattern_at(DESCR_t pat, const char *subject, int subj_len, int cursor) {
    if (!subject) subject = "";
    if (cursor < 0 || cursor > subj_len) return -1;

    PATND_t *sp = spat_of(pat);
    if (!sp) {
        /* Plain string literal pattern: anchored memcmp */
        if (pat.v == DT_S) {
            const char *lit = pat.s ? pat.s : "";
            int n = (int)strlen(lit);
            if (cursor + n > subj_len) return -1;
            if (memcmp(subject + cursor, lit, (size_t)n) != 0) return -1;
            return cursor + n;
        }
        /* NULL/integer/other — treat as empty string: anchored MATCH_fn of "" always succeeds */
        return cursor;
    }

    MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = subject;
    int r = try_match_at(sp, subject, subj_len, cursor, &ctx);
    /* try_match_at returns end-position on success, -1 on failure */
    return r;
}

int match_and_replace(DESCR_t *subject, DESCR_t pat, DESCR_t replacement) {
    if (!subject) return 0;
    /* P002: if replacement value signals failure, propagate it as F-branch */
    if (IS_FAIL_fn(replacement)) return 0;

    /* P002: out-of-bounds subscript returns FAILDESCR — fail the statement */
    if (IS_FAIL_fn(*subject) || IS_FAIL_fn(replacement)) return 0;

    const char *s = VARVAL_fn(*subject);
    if (!s) s = "";
    int slen = (int)strlen(s);

    PATND_t *sp = spat_of(pat);
    if (!sp) return 0;

    /* Materialise once to prevent side-effect functions firing per scan position */
    MatchCtx mat_ctx; memset(&mat_ctx, 0, sizeof(mat_ctx)); mat_ctx.subject = s;
    Pattern *mat_root = materialise(sp, &mat_ctx);

    /* Try each starting position — reuse mat_ctx so captures[] are visible */
    int match_start = -1, match_end = -1;
    for (int start = 0; start <= slen; start++) {
        for (int i = 0; i < mat_ctx.ncaptures; i++)
            mat_ctx.captures[i].start = mat_ctx.captures[i].end = -1;
        mat_ctx.scan_start = start;
        EngineOpts opts;
        opts.cap_fn    = (mat_ctx.ncaptures > 0) ? capture_callback : NULL;
        opts.cap_data  = &mat_ctx;
        opts.var_fn    = var_resolve_callback;
        opts.var_data  = &mat_ctx;
        opts.scan_start = start;
        MatchResult mr = engine_match_ex(mat_root, s + start, slen - start, &opts);
        if (mr.matched) {
            apply_captures(&mat_ctx);
            match_start = start;
            match_end   = start + mr.end;
            pattern_free_all(&mat_ctx.pl);
            break;
        }
        if (start == slen) break;
    }
    if (match_start < 0) pattern_free_all(&mat_ctx.pl);
    if (match_start < 0) return 0;

    const char *repl = VARVAL_fn(replacement);
    if (!repl) repl = "";
    size_t rlen  = strlen(repl);
    size_t total = (size_t)match_start + rlen + (size_t)(slen - match_end);

    char *result = (char *)GC_MALLOC(total + 1);
    memcpy(result,                          s,    (size_t)match_start);
    memcpy(result + match_start,            repl, rlen);
    memcpy(result + match_start + rlen,     s + match_end, (size_t)(slen - match_end));
    result[total] = '\0';

    *subject = STRVAL(result);
    return 1;
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
        return table_get(arr.tbl, VARVAL_fn(idx));
    }
    /* DATA instance child access: c(x)[i] — tree children via DATINST_t */
    if (arr.v == DT_DATA) {
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

/* subscript_set — arr[idx] = val */
void subscript_set(DESCR_t arr, DESCR_t idx, DESCR_t val) {
    if (arr.v == DT_A) {
        array_set(arr.arr, (int)to_int(idx), val);
        return;
    }
    if (arr.v == DT_T) {
        table_set(arr.tbl, VARVAL_fn(idx), val);
        return;
    }
    /* SIL NONARY → ERRTYP,3 → FTLTST */
    sno_runtime_error(3, NULL);
}

/* subscript_get2 / subscript_set2 — 2D */
DESCR_t subscript_get2(DESCR_t arr, DESCR_t i, DESCR_t j) {
    if (arr.v == DT_A)
        return array_get2(arr.arr, (int)to_int(i), (int)to_int(j));
    return FAILDESCR;  /* P002: not an array — fail the statement */
}

void subscript_set2(DESCR_t arr, DESCR_t i, DESCR_t j, DESCR_t val) {
    if (arr.v == DT_A)
        array_set2(arr.arr, (int)to_int(i), (int)to_int(j), val);
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

/* cmpnd_to_expr — lower CMPND_t (CMPILE parse node) to EXPR_t (shared IR).
 * Uses SIL stype constants directly — no integer guessing.
 * CMPILE.c is #included above so CMPND_t, stype defines are in this TU. */

EXPR_t *cmpnd_to_expr(CMPND_t *n) {
    if (!n || n->stype == 0) return NULL;

    /* Transparent wrapper: parenthesised sub-expression — unwrap */
    if (n->stype == NSTTYP)
        return n->nchildren == 1 ? cmpnd_to_expr(n->children[0]) : NULL;

    /* Implicit concatenation: VARTYP root with >1 child */
    if (n->stype == VARTYP && n->nchildren > 0) {
        EXPR_t *e = GC_MALLOC(sizeof *e);
        e->kind = E_CAT;
        e->children = GC_MALLOC(n->nchildren * sizeof(EXPR_t*));
        e->nchildren = n->nchildren;
        for (int i = 0; i < n->nchildren; i++)
            e->children[i] = cmpnd_to_expr(n->children[i]);
        return e;
    }

    EXPR_t *e = GC_MALLOC(sizeof *e);

    /* Map SIL stype → EKind using named constants */
    switch (n->stype) {
        /* Literals */
        case QLITYP: e->kind = E_QLIT;    e->sval = n->text ? strdup(n->text) : NULL; break;
        case ILITYP: e->kind = E_ILIT;    e->ival = (long)n->ival; break;
        case FLITYP: e->kind = E_FLIT;    e->dval = n->fval; break;
        /* References */
        case VARTYP: e->kind = E_VAR;     e->sval = n->text ? strdup(n->text) : NULL; break;
        case FNCTYP: e->kind = E_FNC;     e->sval = n->text ? strdup(n->text) : NULL; break;
        case ARYTYP:
            /* CMPILE ARYTYP: text = array/table name, children = subscript exprs.
             * E_IDX expects children[0]=base, children[1..]=subscripts.
             * Prepend a synthetic E_VAR node for the base name. */
            e->kind = E_IDX;
            e->sval = n->text ? strdup(n->text) : NULL;
            {
                int nc = n->nchildren;
                e->children = GC_MALLOC((size_t)(nc + 1) * sizeof(EXPR_t *));
                e->nchildren = nc + 1;
                EXPR_t *base_var = GC_MALLOC(sizeof *base_var);
                base_var->kind = E_VAR;
                base_var->sval = n->text ? strdup(n->text) : NULL;
                e->children[0] = base_var;
                for (int i = 0; i < nc; i++)
                    e->children[i + 1] = cmpnd_to_expr(n->children[i]);
            }
            return e;
        case KEYFN:
            /* CMPILE emits KEYFN node as: stype=KEYFN, text="UOP_KEY",
             * child[0] = VARTYP node with the keyword name (e.g. "STNO").
             * E_KEYWORD needs the keyword name, not the operator label. */
            e->kind = E_KEYWORD;
            if (n->nchildren > 0 && n->children[0] && n->children[0]->text)
                e->sval = strdup(n->children[0]->text);
            else
                e->sval = n->text ? strdup(n->text) : NULL;
            /* child already consumed — don't re-recurse it below */
            return e;
        /* Concatenation */
        case CATFN:  e->kind = E_CAT;     break;
        /* Binary arithmetic */
        case ADDFN:  e->kind = E_ADD;     break;
        case SUBFN:  e->kind = E_SUB;     break;
        case MPYFN:  e->kind = E_MUL;     break;
        case DIVFN:  e->kind = E_DIV;     break;
        case EXPFN:  e->kind = E_POW;     break;
        /* Pattern / capture binary */
        case ORFN:   e->kind = E_ALT;     break;
        case NAMFN:  e->kind = E_CAPT_COND_ASGN;  break;
        case DOLFN:  e->kind = E_CAPT_IMMED_ASGN; break;
        case BIQSFN: e->kind = E_SCAN;    break;
        case BIEQFN: e->kind = E_ASSIGN;  break;  /* P2D: chained assignment */
        /* Unary */
        case PLSFN:  e->kind = E_PLS;     break;
        case MNSFN:  e->kind = E_MNS;     break;
        case DOTFN:  e->kind = E_NAME;    break;
        case INDFN:  e->kind = E_INDIRECT; break;
        case STRFN:  e->kind = E_DEFER;   break;
        case ATFN:   e->kind = E_CAPT_CURSOR; break;
        case QUESFN: e->kind = E_INTERROGATE; break;
        case NEGFN:  e->kind = E_MNS;     break; /* ~X negation → unary minus */
        /* Alternative eval list */
        case SELTYP: e->kind = E_ALT;     break;
        /* User-definable — treat as opaque function call */
        case BIATFN: case BIPDFN: case BIPRFN:
        case BIAMFN: case BINGFN:
            e->kind = E_OPSYN; e->sval = n->text ? strdup(n->text) : NULL; break;
        default:
            e->kind = E_VAR; e->sval = n->text ? strdup(n->text) : NULL; break;
    }

    /* Recurse into children */
    if (n->nchildren > 0) {
        e->children = GC_MALLOC(n->nchildren * sizeof(EXPR_t*));
        e->nchildren = n->nchildren;
        for (int i = 0; i < n->nchildren; i++)
            e->children[i] = cmpnd_to_expr(n->children[i]);
    }
    return e;
}

/* eval_via_cmpile — parse expression string via CMPILE, lower to EXPR_t,
 * evaluate via eval_node().  Used by EVAL() for string-valued arguments.
 * CMPILE globals (TEXTSP, BRTYPE, STYPE, g_error, FORWRD) are in this TU
 * via #include "../../frontend/snobol4/CMPILE.c". */
static DESCR_t eval_via_cmpile(const char *s) {
    init_tables();
    g_error = 0;
    TEXTSP.ptr = s; TEXTSP.len = (int)strlen(s);
    XSP.ptr = s; XSP.len = 0;
    BRTYPE = 0; STYPE = 0;
    FORWRD();
    if (g_error) return FAILDESCR;
    CMPND_t *n = EXPR();
    if (!n || g_error) return FAILDESCR;
    EXPR_t *e = cmpnd_to_expr(n);
    if (!e) return FAILDESCR;
    return eval_node(e);
}

DESCR_t EVAL_fn(DESCR_t expr) {
    /* DT_E: frozen EXPR_t* — thaw by calling eval_node directly */
    if (expr.v == DT_E) {
        if (!expr.ptr) return FAILDESCR;
        DESCR_t _res = eval_node(expr.ptr);
        if (IS_FAIL_fn(_res)) return FAILDESCR;
        return _res;
    }
    if (expr.v == DT_P) {
        /* DT_P: pattern from *func(). Run against empty subject via hook.
         * If function fails at match time, EVAL fails. */
        if (g_eval_pat_hook) return g_eval_pat_hook(expr);
        return expr;  /* no hook — treat as success (fallback) */
    }
    if (expr.v != DT_S && expr.v != DT_SNUL) return expr;
    const char *s = VARVAL_fn(expr);
    if (!s || !*s) return pat_epsilon();
    /* If the expression is a quoted string literal ('...' or "..."),
     * EVAL returns the string value — not a pattern.
     * SNOBOL4: EVAL("'STMT_t'") => "STMT_t" */
    size_t sl = strlen(s);
    if (sl >= 2 && (s[0] == '\'' || s[0] == '"') && s[sl-1] == s[0]) {
        char *inner = GC_malloc(sl - 1);
        memcpy(inner, s + 1, sl - 2);
        inner[sl - 2] = '\0';
        return STRVAL(inner);
    }
    /* Use CMPILE EXPR() — direct SIL CONVEX path, handles "x + 4" correctly */
    DESCR_t full = eval_via_cmpile(s);
    if (!IS_FAIL_fn(full)) return full;
    /* Fallback: old _ev_expr for pattern-context strings */
    SnoEvalCtx ctx = { s, 0 };
    return _ev_expr(&ctx);
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
    CMPND_t *cmpnd = cmpile_eval_expr(src);
    if (!cmpnd) return FAILDESCR;
    EXPR_t *tree = cmpnd_to_expr(cmpnd);
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
