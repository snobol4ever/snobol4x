/*
 * snobol4_pattern.c — SNOBOL4 pattern constructor and matching bridge
 *
 * Connects the pat_*() API (called from beautiful.c) to the existing
 * Byrd Box engine in engine.c.
 *
 * Architecture:
 *   - SnoVal with type SPATTERN holds a SnoPattern* (GC-managed)
 *   - SnoPattern wraps a lazy tree of pattern constructors
 *   - At mtch time, match_pattern() materialises the pattern into
 *     engine Pattern* nodes and calls engine_match()
 *   - Deferred refs (*name) are resolved from the variable table at mtch time
 *   - Capture assignments ($ and .) write into the variable table on mtch
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "snobol4.h"
#include "../engine.h"

/* =========================================================================
 * SnoPattern — lazy pattern node
 * ===================================================================== */

typedef enum {
    SPAT_LIT,          /* literal string */
    SPAT_SPAN,         /* SPAN(chars) */
    SPAT_BREAK,        /* BREAK(chars) */
    SPAT_ANY,          /* ANY(chars) */
    SPAT_NOTANY,       /* NOTANY(chars) */
    SPAT_LEN,          /* LEN(n) */
    SPAT_POS,          /* POS(n) */
    SPAT_RPOS,         /* RPOS(n) */
    SPAT_TAB,          /* TAB(n) */
    SPAT_RTAB,         /* RTAB(n) */
    SPAT_ARB,          /* ARB */
    SPAT_ARBNO,        /* ARBNO(p) */
    SPAT_REM,          /* REM */
    SPAT_FENCE,        /* FENCE or FENCE(p) */
    SPAT_FAIL,         /* FAIL */
    SPAT_ABORT,        /* ABORT */
    SPAT_SUCCEED,      /* SUCCEED */
    SPAT_BAL,          /* BAL */
    SPAT_EPSILON,      /* epsilon (always succeeds, no chars consumed) */
    SPAT_CAT,          /* concatenation: left right */
    SPAT_ALT,          /* alternation:   left | right */
    SPAT_REF,          /* deferred var ref: *name */
    SPAT_ASSIGN_IMM,   /* immediate capture: pat $ var */
    SPAT_ASSIGN_COND,  /* conditional capture: pat . var */
    SPAT_VAR,          /* variable holding a pattern */
    SPAT_USER_CALL,    /* user-defined pattern function call */
} SnoPatKind;

/* Forward decl */
struct _SnoPattern;
typedef struct _SnoPattern SnoPattern;

struct _SnoPattern {
    SnoPatKind  kind;
    int         materialising; /* cycle detection flag */
    const char *strv;       /* SPAT_LIT / SPAT_SPAN / SPAT_BREAK / SPAT_ANY / SPAT_NOTANY / SPAT_REF / SPAT_USER_CALL */
    int64_t     num;       /* SPAT_LEN / SPAT_POS / SPAT_RPOS / SPAT_TAB / SPAT_RTAB */
    SnoPattern *left;      /* SPAT_CAT / SPAT_ALT / SPAT_ARBNO / SPAT_FENCE / SPAT_ASSIGN_IMM / SPAT_ASSIGN_COND */
    SnoPattern *right;     /* SPAT_CAT / SPAT_ALT */
    SnoVal      var;       /* SPAT_ASSIGN_IMM / SPAT_ASSIGN_COND / SPAT_VAR capture target or value */
    SnoVal     *args;      /* SPAT_USER_CALL args */
    int         nargs;     /* SPAT_USER_CALL nargs */
};

/* GC-allocate a SnoPattern */
static SnoPattern *spat_new(SnoPatKind kind) {
    SnoPattern *p = (SnoPattern *)GC_MALLOC(sizeof(SnoPattern));
    memset(p, 0, sizeof(SnoPattern));
    p->kind = kind;
    return p;
}

/* Wrap a SnoPattern in a SnoVal */
static inline SnoVal spat_val(SnoPattern *p) {
    SnoVal v;
    v.type = SPATTERN;
    v.p    = (struct _Pattern *)p;
    return v;
}

/* Extract SnoPattern from SnoVal */
static inline SnoPattern *spat_of(SnoVal v) {
    if (v.type != SPATTERN) return NULL;
    return (SnoPattern *)v.p;
}

/* =========================================================================
 * Pattern constructors
 * ===================================================================== */

SnoVal pat_lit(const char *s) {
    SnoPattern *p = spat_new(SPAT_LIT);
    p->strv = s ? GC_strdup(s) : "";
    return spat_val(p);
}

SnoVal pat_span(const char *chars) {
    SnoPattern *p = spat_new(SPAT_SPAN);
    p->strv = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal pat_break_(const char *chars) {
    SnoPattern *p = spat_new(SPAT_BREAK);
    p->strv = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal pat_any_cs(const char *chars) {
    SnoPattern *p = spat_new(SPAT_ANY);
    p->strv = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal pat_notany(const char *chars) {
    SnoPattern *p = spat_new(SPAT_NOTANY);
    p->strv = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal pat_len(int64_t n) {
    SnoPattern *p = spat_new(SPAT_LEN);
    p->num = n;
    return spat_val(p);
}

SnoVal pat_pos(int64_t n) {
    SnoPattern *p = spat_new(SPAT_POS);
    p->num = n;
    return spat_val(p);
}

SnoVal pat_rpos(int64_t n) {
    SnoPattern *p = spat_new(SPAT_RPOS);
    p->num = n;
    return spat_val(p);
}

SnoVal pat_tab(int64_t n) {
    SnoPattern *p = spat_new(SPAT_TAB);
    p->num = n;
    return spat_val(p);
}

SnoVal pat_rtab(int64_t n) {
    SnoPattern *p = spat_new(SPAT_RTAB);
    p->num = n;
    return spat_val(p);
}

SnoVal pat_arb(void) {
    return spat_val(spat_new(SPAT_ARB));
}

SnoVal pat_arbno(SnoVal inner) {
    SnoPattern *p = spat_new(SPAT_ARBNO);
    p->left = spat_of(inner);
    /* If inner is not a pattern (e.g. a string), wrap it */
    if (!p->left && inner.type == SSTR) {
        p->left = spat_of(pat_lit(inner.s));
    }
    return spat_val(p);
}

SnoVal pat_rem(void) {
    return spat_val(spat_new(SPAT_REM));
}

SnoVal pat_fence_p(SnoVal inner) {
    SnoPattern *p = spat_new(SPAT_FENCE);
    p->left = spat_of(inner);
    return spat_val(p);
}

SnoVal pat_fence(void) {
    return spat_val(spat_new(SPAT_FENCE));
}

SnoVal pat_fail(void) {
    return spat_val(spat_new(SPAT_FAIL));
}

SnoVal pat_abort(void) {
    return spat_val(spat_new(SPAT_ABORT));
}

SnoVal pat_succeed(void) {
    return spat_val(spat_new(SPAT_SUCCEED));
}

SnoVal pat_bal(void) {
    return spat_val(spat_new(SPAT_BAL));
}

SnoVal pat_epsilon(void) {
    return spat_val(spat_new(SPAT_EPSILON));
}

SnoVal pat_cat(SnoVal left, SnoVal right) {
    SnoPattern *p = spat_new(SPAT_CAT);
    p->left  = spat_of(left);
    p->right = spat_of(right);
    /* Handle string literals on either side */
    if (!p->left  && left.type  == SSTR) p->left  = spat_of(pat_lit(left.s));
    if (!p->right && right.type == SSTR) p->right = spat_of(pat_lit(right.s));
    if (!p->left)  return right;   /* degenerate */
    if (!p->right) return left;
    return spat_val(p);
}

SnoVal pat_alt(SnoVal left, SnoVal right) {
    SnoPattern *p = spat_new(SPAT_ALT);
    p->left  = spat_of(left);
    p->right = spat_of(right);
    if (!p->left  && left.type  == SSTR)  p->left  = spat_of(pat_lit(left.s));
    if (!p->right && right.type == SSTR)  p->right = spat_of(pat_lit(right.s));
    /* SNULL (uninitialized var) in ALT = epsilon: always succeeds.
     * e.g. (nl | ';') where nl is uninitialized => ("" | ';') => epsilon. */
    if (!p->left  && left.type  == SNULL) p->left  = spat_of(pat_epsilon());
    if (!p->right && right.type == SNULL) p->right = spat_of(pat_epsilon());
    if (!p->left)  return right;
    if (!p->right) return left;
    return spat_val(p);
}

SnoVal pat_ref(const char *name) {
    SnoPattern *p = spat_new(SPAT_REF);
    p->strv = name ? GC_strdup(name) : "";
    return spat_val(p);
}

SnoVal pat_ref_val(SnoVal nameVal) {
    return pat_ref(to_str(nameVal));
}

SnoVal pat_assign_imm(SnoVal child, SnoVal var) {
    SnoPattern *p = spat_new(SPAT_ASSIGN_IMM);
    p->left = spat_of(child);
    if (!p->left && child.type == SSTR) p->left = spat_of(pat_lit(child.s));
    p->var  = var;
    return spat_val(p);
}

SnoVal pat_assign_cond(SnoVal child, SnoVal var) {
    SnoPattern *p = spat_new(SPAT_ASSIGN_COND);
    p->left = spat_of(child);
    if (!p->left && child.type == SSTR) p->left = spat_of(pat_lit(child.s));
    p->var  = var;
    return spat_val(p);
}

SnoVal var_as_pattern(SnoVal v) {
    /* If v is already a pattern, return it */
    if (v.type == SPATTERN) return v;
    /* If v is a string, treat as a literal pattern */
    if (v.type == SSTR || v.type == SNULL) {
        return pat_lit(to_str(v));
    }
    /* Otherwise wrap as a variable lookup */
    SnoPattern *p = spat_new(SPAT_VAR);
    p->var = v;
    return spat_val(p);
}

SnoVal pat_user_call(const char *name, SnoVal *args, int nargs) {
    SnoPattern *p = spat_new(SPAT_USER_CALL);
    p->strv   = name ? GC_strdup(name) : "";
    p->nargs = nargs;
    if (nargs > 0) {
        p->args = (SnoVal *)GC_MALLOC(nargs * sizeof(SnoVal));
        memcpy(p->args, args, nargs * sizeof(SnoVal));
    }
    return spat_val(p);
}

/* =========================================================================
 * Pattern materialisation — convert SnoPattern tree to engine Pattern* tree
 *
 * The engine uses malloc'd Pattern nodes that are freed after each mtch.
 * We materialise lazily: deferred refs are resolved from the var table NOW.
 * Capture variables are recorded in a capture list, applied after mtch.
 * ===================================================================== */

typedef struct {
    char   *var_name;   /* variable name to assign to (static, or NULL if deferred) */
    /* Deferred var name: when var is *FuncCall(), evaluate at aply time */
    char *(*var_fn)(void *data); /* if set, call this at aply time to get var name */
    void   *var_data;   /* userdata for var_fn */
    int     start;      /* mtch start cursor */
    int     end;        /* mtch end cursor */
    int     is_imm;     /* 1 = immediate ($), 0 = conditional (.) */
} Capture;

#define MAX_CAPTURES 64

/* Cache of already-materialised patterns keyed by SnoPattern* identity.
 * Prevents var_resolve_callback from re-materialising the same variable
 * (e.g. snoCommand) on every ARBNO iteration.  nInc/nPush/nPop fire ONCE
 * at materialise time (correct — they set up capture wrappers), not N times. */
#define VAR_CACHE_MAX 32
typedef struct {
    SnoPattern *sp;    /* original SnoPattern* (identity key) */
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

/* T_FUNC callback for SPAT_USER_CALL deferred functions.
 *
 * Used for functions that must fire at MATCH TIME not materialise time:
 *   nInc, nPush, nPop — return SPATTERN (a capture-wrapper/epsilon); also have
 *                        stack side-effects (increment counter, push/pop frame).
 *   Reduce            — returns SNULL; pops parse stack and builds tree node.
 *
 * If the function returns SPATTERN, we run it as a zero-width sub-mtch
 * against the current subject string so its captures fire correctly.
 * SFAIL (FRETURN) => -1 => engine CONCEDE.
 * All other returns => succeed zero-width. */
typedef struct {
    const char  *name;
    SnoVal      *args;
    int          nargs;
    /* Set at mtch time by the engine dispatcher in match_pattern.
     * Points to the current subject being matched — used for SPATTERN sub-mtch. */
    const char  *subject;
} UCData;

static void *user_call_fn(void *userdata) {
    UCData *d = (UCData *)userdata;
    if (getenv("PAT_DEBUG")) fprintf(stderr, "  user_call_fn: %s\n", d->name);
    SnoVal r = aply(d->name, d->args, d->nargs);
    if (r.type == SFAIL) return (void *)(intptr_t)-1;
    if (r.type == SPATTERN && r.p) {
        /* Run the returned pattern as a zero-width sub-mtch.
         * nInc/nPush/nPop return epsilon . *Fn() wrappers — matching against ""
         * fires the capture which records the counter/frame. */
        const char *subj = d->subject ? d->subject : "";
        match_pattern(r, subj);
    }
    return (void *)1;
}

/* Return 1 if this function must be deferred to mtch time.
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
    SnoVal r = aply(d->name, d->args, d->nargs);
    if (r.type == SSTR && r.s && r.s[0]) return (char *)r.s;
    if (r.type == SNULL) return NULL; /* NRETURN — no assignment */
    return NULL;
}

static Pattern *materialise(SnoPattern *sp, MatchCtx *ctx);

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

/* Deferred USER_CALL: data passed to T_FUNC callback at mtch time */
typedef struct {
    const char *name;
    SnoVal     *args;
    int         nargs;
} DeferredCall;

/* T_FUNC callback: called by engine at mtch time (zero-width, side-effect) */
static void *deferred_call_fn(void *userdata) {
    DeferredCall *d = (DeferredCall *)userdata;
    /* Special handling for reduce/Reduce: evaluate string args at mtch time */
    if (d->nargs >= 2 && strcasecmp(d->name, "reduce") == 0) {
        SnoVal t_arg = d->args[0];
        SnoVal n_arg = d->args[1];
        /* Strip outer quotes from type string */
        if (t_arg.type == SSTR && t_arg.s) {
            const char *ts = t_arg.s;
            int tlen = (int)strlen(ts);
            if (tlen >= 2 &&
                ((ts[0]=='\'' && ts[tlen-1]=='\'') ||
                 (ts[0]=='"'  && ts[tlen-1]=='"'))) {
                char *stripped = GC_malloc(tlen - 1);
                memcpy(stripped, ts + 1, tlen - 2);
                stripped[tlen-2] = '\0';
                t_arg = STR_VAL(stripped);
            }
        }
        /* Evaluate count expression at mtch time */
        if (n_arg.type == SSTR)
            n_arg = evl(n_arg);
        SnoVal reduce_args[2] = { t_arg, n_arg };
        aply("Reduce", reduce_args, 2);
        return (void *)1; /* succeed */
    }
    /* All other calls: fire and succeed (side-effect only) */
    aply(d->name, d->args, d->nargs);
    return (void *)1;
}

static Pattern *make_func(PatternList *pl, const char *name, SnoVal *args, int nargs) {
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

static Pattern *materialise(SnoPattern *sp, MatchCtx *ctx) {
    if (!sp) return make_epsilon(&ctx->pl);

    Pattern *p = pattern_alloc(&ctx->pl);

    switch (sp->kind) {

    case SPAT_LIT:
        p->type  = T_LITERAL;
        p->s     = sp->strv ? sp->strv : "";
        p->s_len = (int)strlen(p->s);
        return p;

    case SPAT_SPAN:
        p->type  = T_SPAN;
        p->chars = sp->strv ? sp->strv : "";
        return p;

    case SPAT_BREAK:
        p->type  = T_BREAK;
        p->chars = sp->strv ? sp->strv : "";
        return p;

    case SPAT_ANY:
        p->type  = T_ANY;
        p->chars = sp->strv ? sp->strv : "";
        return p;

    case SPAT_NOTANY:
        p->type  = T_NOTANY;
        p->chars = sp->strv ? sp->strv : "";
        return p;

    case SPAT_LEN:
        p->type = T_LEN;
        p->n    = (int)sp->num;
        return p;

    case SPAT_POS:
        p->type = T_POS;
        /* Adjust for scan offset: POS(n) in original = POS(n - scan_start) in sub-string.
         * If n < scan_start, this can never mtch from this offset. */
        p->n = (int)(sp->num - ctx->scan_start);
        if (p->n < 0) { p->type = T_FAIL; p->n = 0; }
        return p;

    case SPAT_RPOS:
        /* RPOS(n) is from end — not affected by scan offset, but we need full subject len.
         * Engine uses OMEGA - DELTA, so RPOS works correctly relative to sub-string end.
         * However the sub-string end = original end, so this is correct as-is. */
        p->type = T_RPOS;
        p->n    = (int)sp->num;
        return p;

    case SPAT_TAB:
        p->type = T_TAB;
        p->n    = (int)sp->num;
        return p;

    case SPAT_RTAB:
        p->type = T_RTAB;
        p->n    = (int)sp->num;
        return p;

    case SPAT_ARB:
        p->type = T_ARB;
        return p;

    case SPAT_REM:
        p->type = T_REM;
        return p;

    case SPAT_EPSILON:
        p->type = T_EPSILON;
        return p;

    case SPAT_FAIL:
        p->type = T_FAIL;
        return p;

    case SPAT_ABORT:
        p->type = T_ABORT;
        return p;

    case SPAT_SUCCEED:
        p->type = T_SUCCEED;
        return p;

    case SPAT_BAL:
        p->type = T_BAL;
        return p;

    case SPAT_FENCE:
        p->type = T_FENCE;
        p->n    = 0;
        if (sp->left) {
            /* FENCE(p) — fence with sub-pattern */
            p->n = 1;
            p->children[0] = materialise(sp->left, ctx);
        }
        return p;

    case SPAT_ARBNO:
        p->type = T_ARBNO;
        p->n    = 1;
        p->children[0] = sp->left ? materialise(sp->left, ctx) : make_epsilon(&ctx->pl);
        return p;

    case SPAT_CAT: {
        Pattern *l = materialise(sp->left,  ctx);
        Pattern *r = materialise(sp->right, ctx);
        return make_seq(&ctx->pl, l, r);
    }

    case SPAT_ALT: {
        Pattern *l = materialise(sp->left,  ctx);
        Pattern *r = materialise(sp->right, ctx);
        return make_alt(&ctx->pl, l, r);
    }

    case SPAT_REF: {
        if (getenv("PAT_DEBUG") && sp->strv)
            fprintf(stderr, "  SPAT_REF '%s' -> type=%d\n", sp->strv,
                var_get(sp->strv).type);
        /* Resolve *name from variable table NOW */
        SnoVal v = var_get(sp->strv);
        if (v.type == SPATTERN) {
            /* Cycle detection: track variable names being materialised.
             * Recursive patterns (e.g. snoExpr14 = '|' *snoExpr14 rest)
             * must not expand infinitely. Return epsilon on a cycle. */
            #define MAT_MAX_DEPTH 64
            static __thread const char *_mat_stack[MAT_MAX_DEPTH];
            static __thread int _mat_top = 0;
            for (int _i = 0; _i < _mat_top; _i++) {
                if (_mat_stack[_i] == sp->strv || strcmp(_mat_stack[_i], sp->strv) == 0) {
                    return make_epsilon(&ctx->pl); /* cycle: return epsilon */
                }
            }
            if (_mat_top < MAT_MAX_DEPTH) _mat_stack[_mat_top++] = sp->strv;
            SnoPattern *sp2 = spat_of(v);
            Pattern *result = materialise(sp2, ctx);
            if (_mat_top > 0) _mat_top--;
            return result;
        }
        /* Variable holds a string — treat as literal */
        if (v.type == SSTR || v.type == SNULL) {
            p->type  = T_LITERAL;
            p->s     = to_str(v);
            p->s_len = (int)strlen(p->s);
            return p;
        }
        /* Pattern not set yet — epsilon (fail gracefully) */
        return make_epsilon(&ctx->pl);
    }

    case SPAT_VAR: {
        /* Variable holding a pattern value */
        SnoVal v = sp->var;
        if (v.type == SPATTERN) {
            return materialise(spat_of(v), ctx);
        }
        if (v.type == SSTR || v.type == SNULL) {
            p->type  = T_LITERAL;
            p->s     = to_str(v);
            p->s_len = (int)strlen(p->s);
            return p;
        }
        return make_epsilon(&ctx->pl);
    }

    case SPAT_ASSIGN_IMM:
    case SPAT_ASSIGN_COND: {
        /*
         * Capture assignment: PAT . var  (conditional) or  PAT $ var  (immediate).
         *
         * We materialise this as a T_CAPTURE node wrapping the child. T_CAPTURE fires
         * a callback with (cap_slot, start, end) when the child succeeds. The cap_slot
         * is the indx into ctx->captures[] where we stored the variable name.
         *
         * The callback (capture_callback below) is called by engine_match_ex, reads
         * ctx->captures[cap_slot].var_name, and calls var_set with the matched span.
         */
        if (ctx->ncaptures >= MAX_CAPTURES) {
            /* Too many captures — degrade to no-capture (child only) */
            return materialise(sp->left, ctx);
        }

        /* Record capture metadata */
        const char *vname = NULL;
        SnoVal vv = sp->var;
        int slot = ctx->ncaptures;
        ctx->captures[slot].var_name = NULL;
        ctx->captures[slot].var_fn   = NULL;
        ctx->captures[slot].var_data = NULL;
        if (vv.type == SSTR) {
            vname = vv.s;
            ctx->captures[slot].var_name = vname ? GC_strdup(vname) : NULL;
        } else if (vv.type == SPATTERN) {
            /* Deferred: var is *FuncCall() — evaluate at apply_captures time */
            SnoPattern *vsp = spat_of(vv);
            if (vsp && vsp->kind == SPAT_USER_CALL) {
                UCData *d = (UCData *)GC_MALLOC(sizeof(UCData));
                d->name  = vsp->strv;
                d->nargs = vsp->nargs;
                d->args  = NULL;
                if (vsp->nargs > 0) {
                    d->args = (SnoVal *)GC_MALLOC(vsp->nargs * sizeof(SnoVal));
                    memcpy(d->args, vsp->args, vsp->nargs * sizeof(SnoVal));
                }
                ctx->captures[slot].var_fn   = deferred_var_fn;
                ctx->captures[slot].var_data = d;
            }
        }
        ctx->captures[slot].is_imm   = (sp->kind == SPAT_ASSIGN_IMM);
        ctx->captures[slot].start    = -1;
        ctx->captures[slot].end      = -1;
        ctx->ncaptures++;

        /* Materialise child */
        Pattern *child = materialise(sp->left, ctx);

        /* Wrap in T_CAPTURE node */
        Pattern *cap = pattern_alloc(&ctx->pl);
        cap->type        = T_CAPTURE;
        cap->n           = slot;      /* cap_slot: which capture[] entry to fill */
        cap->children[0] = child;
        return cap;
    }

    case SPAT_USER_CALL: {
        if (getenv("PAT_DEBUG"))
            fprintf(stderr, "SPAT_USER_CALL %s (deferred→T_FUNC)\n", sp->strv);
        /* Defer the call to mtch time via T_FUNC — side-effect calls like
         * nPush, nInc, nPop, Reduce must fire when the engine reaches this
         * node during matching, NOT during materialise(). */
        return make_func(&ctx->pl, sp->strv, sp->args, sp->nargs);
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

/* Apply captures: call var_set for each fired capture */
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

        int len = cap->end - cap->start;
        if (len < 0) len = 0;
        char *text = (char *)GC_MALLOC(len + 1);
        if (ctx->subject)
            memcpy(text, ctx->subject + cap->start, len);
        text[len] = '\0';
        var_set(vname, STR_VAL(text));
        if (getenv("PAT_DEBUG"))
            fprintf(stderr, "CAPTURE: %s = \"%.*s\"\n", vname, len, text);
    }
}

/* =========================================================================
 * match_pattern — top-level mtch entry point
 * ===================================================================== */

/* Callback for T_VARREF resolution: look up variable by name and materialise.
 * userdata is MatchCtx*. Returns epsilon if variable not set or not a pattern. */
static Pattern *var_resolve_callback(const char *name, void *userdata) {
    MatchCtx *ctx = (MatchCtx *)userdata;
    SnoVal v = var_get(name);
    if (v.type != SPATTERN) {
        /* not a pattern — return epsilon */
        Pattern *ep = pattern_alloc(&ctx->pl);
        ep->type = T_EPSILON;
        return ep;
    }
    SnoPattern *sp = spat_of(v);
    if (!sp) {
        Pattern *ep = pattern_alloc(&ctx->pl);
        ep->type = T_EPSILON;
        return ep;
    }
    /* Cache lookup: if we've already materialised this SnoPattern* during this
     * mtch, return the cached Pattern* tree.  This prevents nInc/nPush/nPop
     * (and any other SPAT_USER_CALL nodes) from firing on every ARBNO iteration
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

/* Internal: try mtch using a pre-materialised root at a single starting offset.
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

/* Internal: materialise then try mtch at a single starting offset.
 * Used for match_and_replace and other single-shot callers. */
static int try_match_at(SnoPattern *sp, const char *subject, int slen, int start, MatchCtx *ctx) {
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

int match_pattern(SnoVal pat, const char *subject) {
    if (!subject) subject = "";
    int _dbg = getenv("PAT_DEBUG") && strlen(subject) < 20;
    if (_dbg) fprintf(stderr, "match_pattern: subj=(%zu) pat.type=%d\n",
        strlen(subject), pat.type);

    SnoPattern *sp = spat_of(pat);
    if (!sp) {
        if (pat.type == SSTR) {
            const char *lit = pat.s ? pat.s : "";
            return strstr(subject, lit) != NULL;
        }
        return 0;
    }

    int slen = (int)strlen(subject);

    /* Materialise the pattern ONCE (scan_start=0 for absolute POS/TAB values).
     * This prevents side-effect functions (Reduce, nInc, etc.) inside SPAT_USER_CALL
     * from being called N times (once per scan position). The engine receives
     * opts.scan_start per position so T_POS/T_TAB compute correctly. */
    MatchCtx mat_ctx; memset(&mat_ctx, 0, sizeof(mat_ctx)); mat_ctx.subject = subject;
    Pattern *root = materialise(sp, &mat_ctx);

    /* SNOBOL4 unanchored mtch: try each starting position */
    if (kw_anchor) {
        /* &ANCHOR=1: anchored at position 0 only */
        MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = subject;
        int r = try_match_at_root(root, subject, slen, 0, &ctx);
        pattern_free_all(&mat_ctx.pl);
        return r >= 0;
    }

    for (int start = 0; start <= slen; start++) {
        MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = subject;
        int r = try_match_at_root(root, subject, slen, start, &ctx);
        if (r >= 0) {
            pattern_free_all(&mat_ctx.pl);
            return 1;
        }
        /* Don't scan past end for patterns that consume nothing */
        if (start == slen) break;
    }
    pattern_free_all(&mat_ctx.pl);
    return 0;
}

/* =========================================================================
 * match_and_replace — pattern mtch with replacement
 *
 * Matches pat against *subject. On success, replaces the matched portion
 * with replacement and updates *subject. Returns 1 on success, 0 on fail.
 * ===================================================================== */

/* =========================================================================
 * match_pattern_at — cursor-aware anchored mtch for E_DEREF / *varname
 *
 * Matches pat against subject starting at position cursor (anchored — no
 * scan loop).  Returns the new cursor position (>= cursor) on success, or
 * -1 on failure.  Used by compiled Byrd boxes for indirect pattern refs.
 * ========================================================================= */

int match_pattern_at(SnoVal pat, const char *subject, int subj_len, int cursor) {
    if (!subject) subject = "";
    if (cursor < 0 || cursor > subj_len) return -1;

    SnoPattern *sp = spat_of(pat);
    if (!sp) {
        /* Plain string literal pattern: anchored memcmp */
        if (pat.type == SSTR) {
            const char *lit = pat.s ? pat.s : "";
            int n = (int)strlen(lit);
            if (cursor + n > subj_len) return -1;
            if (memcmp(subject + cursor, lit, (size_t)n) != 0) return -1;
            return cursor + n;
        }
        /* NULL/integer/other — treat as empty string: anchored mtch of "" always succeeds */
        return cursor;
    }

    MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = subject;
    int r = try_match_at(sp, subject, subj_len, cursor, &ctx);
    /* try_match_at returns end-position on success, -1 on failure */
    return r;
}

int match_and_replace(SnoVal *subject, SnoVal pat, SnoVal replacement) {
    if (!subject) return 0;
    /* P002: if replacement value signals failure, propagate it as F-branch */
    if (is_fail(replacement)) return 0;

    /* P002: out-of-bounds subscript returns FAIL_VAL — fail the statement */
    if (is_fail(*subject) || is_fail(replacement)) return 0;

    const char *s = to_str(*subject);
    if (!s) s = "";
    int slen = (int)strlen(s);

    SnoPattern *sp = spat_of(pat);
    if (!sp) return 0;

    /* Materialise once to prevent side-effect functions firing per scan position */
    MatchCtx mat_ctx; memset(&mat_ctx, 0, sizeof(mat_ctx)); mat_ctx.subject = s;
    Pattern *mat_root = materialise(sp, &mat_ctx);

    /* Try each starting position */
    int match_start = -1, match_end = -1;
    for (int start = 0; start <= slen; start++) {
        MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = s;
        ctx.scan_start = start;
        EngineOpts opts;
        opts.cap_fn    = (ctx.ncaptures > 0) ? capture_callback : NULL;
        opts.cap_data  = &ctx;
        opts.var_fn    = var_resolve_callback;
        opts.var_data  = &ctx;
        opts.scan_start = start;
        MatchResult mr = engine_match_ex(mat_root, s + start, slen - start, &opts);
        if (mr.matched) {
            apply_captures(&ctx);
            match_start = start;
            match_end   = start + mr.end;
            pattern_free_all(&mat_ctx.pl);
            break;
        }
        if (start == slen) break;
    }
    if (match_start < 0) pattern_free_all(&mat_ctx.pl);
    if (match_start < 0) return 0;

    const char *repl = to_str(replacement);
    if (!repl) repl = "";
    size_t rlen  = strlen(repl);
    size_t total = (size_t)match_start + rlen + (size_t)(slen - match_end);

    char *result = (char *)GC_MALLOC(total + 1);
    memcpy(result,                          s,    (size_t)match_start);
    memcpy(result + match_start,            repl, rlen);
    memcpy(result + match_start + rlen,     s + match_end, (size_t)(slen - match_end));
    result[total] = '\0';

    *subject = STR_VAL(result);
    return 1;
}

/* =========================================================================
 * New functions needed by snobol4.c / beautiful.c
 * ===================================================================== */

/* array_create("lo:hi" or "n") — create array from spec string */
SnoVal array_create(SnoVal spec) {
    const char *s = to_str(spec);
    int lo = 1, hi = 1;
    const char *colon = strchr(s, ':');
    if (colon) {
        lo = atoi(s);
        hi = atoi(colon + 1);
    } else {
        hi = atoi(s);
    }
    if (hi < lo) hi = lo;
    SnoArray *a = array_new(lo, hi);
    SnoVal v;
    v.type = ARRAY;
    v.a    = a;
    return v;
}

/* subscript_get — get arr[idx] */
SnoVal subscript_get(SnoVal arr, SnoVal idx) {
    if (arr.type == ARRAY) {
        return array_get(arr.a, (int)to_int(idx));  /* returns FAIL if OOB */
    }
    if (arr.type == STABLE) {
        return table_get(arr.tbl, to_str(idx));
    }
    /* Tree child access: c(x)[i] */
    if (arr.type == STREE) {
        int i = (int)to_int(idx);
        Tree *child = c_i(arr.t, i);
        return child ? TREE_VAL(child) : FAIL_VAL;  /* P002: no child = fail */
    }
    return FAIL_VAL;  /* P002: unknown container type = fail */
}

/* subscript_set — arr[idx] = val */
void subscript_set(SnoVal arr, SnoVal idx, SnoVal val) {
    if (arr.type == ARRAY) {
        array_set(arr.a, (int)to_int(idx), val);
        return;
    }
    if (arr.type == STABLE) {
        table_set(arr.tbl, to_str(idx), val);
        return;
    }
}

/* subscript_get2 / subscript_set2 — 2D */
SnoVal subscript_get2(SnoVal arr, SnoVal i, SnoVal j) {
    if (arr.type == ARRAY)
        return array_get2(arr.a, (int)to_int(i), (int)to_int(j));
    return FAIL_VAL;  /* P002: not an array — fail the statement */
}

void subscript_set2(SnoVal arr, SnoVal i, SnoVal j, SnoVal val) {
    if (arr.type == ARRAY)
        array_set2(arr.a, (int)to_int(i), (int)to_int(j), val);
}

/* tree_new — 4-arg version: creates a DATA('tree(t,v,n,c)') instance */
SnoVal make_tree(SnoVal tag, SnoVal val, SnoVal n_children, SnoVal children) {
    /* Ensure the tree type is registered */
    if (!func_exists("t")) {
        data_define("tree(t,v,n,c)");
    }
    return udef_new("tree", tag, val, n_children, children, (SnoVal){0});
}

/* push_val / pop_val / top_val — aliases for push/pop/top */
SnoVal push_val(SnoVal x) {
    push(x);
    return NULL_VAL;
}

SnoVal pop_val(void) {
    return pop();
}

SnoVal top_val(void) {
    return top();
}

/* register_fn — register a C function in the global function table */
void register_fn(const char *name, SnoVal (*fn)(SnoVal*, int), int min_args, int max_args) {
    (void)min_args; (void)max_args;
    define(name, fn);
}

/* define_spec — DEFINE('name(args)locals') */
void define_spec(SnoVal spec) {
    define(to_str(spec), NULL);
}

/* apply_val — APPLY(fnval, args...) */
SnoVal apply_val(SnoVal fnval, SnoVal *args, int nargs) {
    const char *name = to_str(fnval);
    return aply(name, args, nargs);
}

/* =========================================================================
 * evl — EVAL(expr)
 *
 * Hand-rolled recursive descent over the subset of SNOBOL4 pattern
 * expressions that beauty.sno produces:
 *
 *   expr : term ('.' term)*
 *   term : '*' ident ['(' args ')']   deferred ref / user_call node
 *        | ident '(' args ')'          function call — evaluate now
 *        | '\'' strv '\''               string literal → pat_lit
 *        | ident                       plain name → SSTR sentinel
 *   args : val (',' val)*
 *   val  : ident '(' args ')'          function call → value
 *        | '\'' strv '\''               string value
 *        | ident                       var lookup
 *        | integer
 *
 * Key semantic: plain IDENT in term position returns STR_VAL(name).
 * Dot handler checks right operand type:
 *   SSTR     → assign_cond(left, right)   capture into named var
 *   SPATTERN → pat_cat(left, right)        pattern ccat
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

static SnoVal _ev_val(SnoEvalCtx *e);
static SnoVal _ev_term(SnoEvalCtx *e);
static SnoVal _ev_expr(SnoEvalCtx *e);

static int _ev_args(SnoEvalCtx *e, SnoVal *args, int maxargs) {
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

static SnoVal _ev_val(SnoEvalCtx *e) {
    _ev_skip(e);
    char c = e->s[e->pos];
    if (c == '\'' || c == '"') return STR_VAL(_ev_strlit(e));
    if (isalpha((unsigned char)c) || c == '_') {
        char *nm = _ev_ident(e);
        _ev_skip(e);
        if (e->s[e->pos] == '(') {
            e->pos++;
            SnoVal args[8]; int na = _ev_args(e, args, 8);
            return aply(nm, args, na);
        }
        return var_get(nm);
    }
    if (isdigit((unsigned char)c) || c == '-') {
        char *end;
        long long iv = strtoll(e->s + e->pos, &end, 10);
        e->pos = (int)(end - e->s);
        return INT_VAL(iv);
    }
    return NULL_VAL;
}

static SnoVal _ev_term(SnoEvalCtx *e) {
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
            SnoVal args[8]; int na = _ev_args(e, args, 8);
            SnoVal *ac = na ? GC_malloc(na * sizeof(SnoVal)) : NULL;
            if (ac) memcpy(ac, args, na * sizeof(SnoVal));
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
            SnoVal args[8]; int na = _ev_args(e, args, 8);
            return aply(nm, args, na);
        }
        return STR_VAL(GC_strdup(nm));
    }
    return pat_epsilon();
}

static SnoVal _ev_expr(SnoEvalCtx *e) {
    SnoVal left = _ev_term(e);
    if (left.type == SSTR) {
        SnoVal v = var_get(left.s);
        if (v.type == SPATTERN) left = v;
        else if (v.type == SSTR && v.s && v.s[0]) left = pat_lit(v.s);
        else left = pat_epsilon();
    } else if (left.type == SNULL) {
        left = pat_epsilon();
    }
    _ev_skip(e);
    while (e->s[e->pos] == '.') {
        e->pos++;
        _ev_skip(e);
        SnoVal right = _ev_term(e);
        _ev_skip(e);
        if (right.type == SSTR) {
            left = pat_assign_cond(left, right);
        } else {
            if (right.type == SNULL) right = pat_epsilon();
            left = pat_cat(left, right);
        }
    }
    return left;
}

SnoVal evl(SnoVal expr) {
    if (expr.type != SSTR && expr.type != SNULL) return expr;
    const char *s = to_str(expr);
    if (!s || !*s) return pat_epsilon();
    SnoEvalCtx ctx = { s, 0 };
    return _ev_expr(&ctx);
}

/* opsyn — OPSYN(new, old, type) */
SnoVal opsyn(SnoVal newname, SnoVal oldname, SnoVal type) {
    (void)type;
    /* Register new as an alias for old */
    const char *nm = to_str(newname);
    const char *old = to_str(oldname);
    /* For now just a no-op — full OPSYN is complex */
    (void)nm; (void)old;
    return NULL_VAL;
}

/* sort_fn — SORT(table_or_array) -> 2D array[1..n,1..2] */
/* Compare function for qsort: sort by string key */
static int _sort_cmp(const void *a, const void *b) {
    const char **sa = (const char **)a;
    const char **sb = (const char **)b;
    return strcmp(sa[0], sb[0]);
}
SnoVal sort_fn(SnoVal arr) {
    if (arr.type != STABLE) return arr;  /* pass-through for non-table */
    SnoTable *tbl = arr.tbl;
    if (!tbl) return FAIL_VAL;

    /* Count entries */
    int n = 0;
    for (int h = 0; h < TABLE_BUCKETS; h++)
        for (SnoTableEntry *e = tbl->buckets[h]; e; e = e->next) n++;
    if (n == 0) return FAIL_VAL;

    /* Collect all (key, value) pairs */
    const char **keys = GC_malloc(n * sizeof(char *));
    SnoVal *vals = GC_malloc(n * sizeof(SnoVal));
    int idx = 0;
    for (int h = 0; h < TABLE_BUCKETS; h++)
        for (SnoTableEntry *e = tbl->buckets[h]; e; e = e->next) {
            keys[idx] = e->key;
            vals[idx] = e->val;
            idx++;
        }

    /* Sort keys (indirect sort via indx array) */
    /* Build indx array and sort */
    int *order = GC_malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) order[i] = i;
    /* Simple insertion sort to avoid qsort complexity with indirect */
    for (int i = 1; i < n; i++) {
        int tmp = order[i];
        int j = i - 1;
        while (j >= 0 && strcmp(keys[order[j]], keys[tmp]) > 0) {
            order[j+1] = order[j]; j--;
        }
        order[j+1] = tmp;
    }

    /* Build 2D array[1..n, 1..2] */
    SnoArray *a = GC_malloc(sizeof(SnoArray));
    a->lo   = 1;
    a->hi   = n;
    a->ndim = 2;   /* 2 columns */
    a->data = GC_malloc(n * 2 * sizeof(SnoVal));
    for (int i = 0; i < n; i++) {
        a->data[i * 2 + 0] = STR_VAL(GC_strdup(keys[order[i]]));
        a->data[i * 2 + 1] = vals[order[i]];
    }
    SnoVal result = {0};
    result.type = ARRAY;
    result.a    = a;
    return result;
}

/* pat_call — call a user-defined function with one arg and use result as pattern value.
 * Used when a pattern expression contains a user-defined function call, e.g. *t(y) in pattern. */
SnoVal pat_call(const char *name, SnoVal arg) {
    SnoVal args[1] = { arg };
    SnoVal result = aply(name, args, 1);
    if (is_fail(result)) return pat_fail();
    /* Wrap result as a pattern: if it's already a pattern, return it;
     * otherwise treat it as a literal string pattern. */
    return var_as_pattern(result);
}
