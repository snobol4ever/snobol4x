/*
 * snobol4_pattern.c — SNOBOL4 pattern constructor and matching bridge
 *
 * Connects the sno_pat_*() API (called from beautiful.c) to the existing
 * Byrd Box engine in engine.c.
 *
 * Architecture:
 *   - SnoVal with type SNO_PATTERN holds a SnoPattern* (GC-managed)
 *   - SnoPattern wraps a lazy tree of pattern constructors
 *   - At match time, sno_match_pattern() materialises the pattern into
 *     engine Pattern* nodes and calls engine_match()
 *   - Deferred refs (*name) are resolved from the variable table at match time
 *   - Capture assignments ($ and .) write into the variable table on match
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
    const char *str;       /* SPAT_LIT / SPAT_SPAN / SPAT_BREAK / SPAT_ANY / SPAT_NOTANY / SPAT_REF / SPAT_USER_CALL */
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
    v.type = SNO_PATTERN;
    v.p    = (struct _Pattern *)p;
    return v;
}

/* Extract SnoPattern from SnoVal */
static inline SnoPattern *spat_of(SnoVal v) {
    if (v.type != SNO_PATTERN) return NULL;
    return (SnoPattern *)v.p;
}

/* =========================================================================
 * Pattern constructors
 * ===================================================================== */

SnoVal sno_pat_lit(const char *s) {
    SnoPattern *p = spat_new(SPAT_LIT);
    p->str = s ? GC_strdup(s) : "";
    return spat_val(p);
}

SnoVal sno_pat_span(const char *chars) {
    SnoPattern *p = spat_new(SPAT_SPAN);
    p->str = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal sno_pat_break_(const char *chars) {
    SnoPattern *p = spat_new(SPAT_BREAK);
    p->str = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal sno_pat_any_cs(const char *chars) {
    SnoPattern *p = spat_new(SPAT_ANY);
    p->str = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal sno_pat_notany(const char *chars) {
    SnoPattern *p = spat_new(SPAT_NOTANY);
    p->str = chars ? GC_strdup(chars) : "";
    return spat_val(p);
}

SnoVal sno_pat_len(int64_t n) {
    SnoPattern *p = spat_new(SPAT_LEN);
    p->num = n;
    return spat_val(p);
}

SnoVal sno_pat_pos(int64_t n) {
    SnoPattern *p = spat_new(SPAT_POS);
    p->num = n;
    return spat_val(p);
}

SnoVal sno_pat_rpos(int64_t n) {
    SnoPattern *p = spat_new(SPAT_RPOS);
    p->num = n;
    return spat_val(p);
}

SnoVal sno_pat_tab(int64_t n) {
    SnoPattern *p = spat_new(SPAT_TAB);
    p->num = n;
    return spat_val(p);
}

SnoVal sno_pat_rtab(int64_t n) {
    SnoPattern *p = spat_new(SPAT_RTAB);
    p->num = n;
    return spat_val(p);
}

SnoVal sno_pat_arb(void) {
    return spat_val(spat_new(SPAT_ARB));
}

SnoVal sno_pat_arbno(SnoVal inner) {
    SnoPattern *p = spat_new(SPAT_ARBNO);
    p->left = spat_of(inner);
    /* If inner is not a pattern (e.g. a string), wrap it */
    if (!p->left && inner.type == SNO_STR) {
        p->left = spat_of(sno_pat_lit(inner.s));
    }
    return spat_val(p);
}

SnoVal sno_pat_rem(void) {
    return spat_val(spat_new(SPAT_REM));
}

SnoVal sno_pat_fence_p(SnoVal inner) {
    SnoPattern *p = spat_new(SPAT_FENCE);
    p->left = spat_of(inner);
    return spat_val(p);
}

SnoVal sno_pat_fence(void) {
    return spat_val(spat_new(SPAT_FENCE));
}

SnoVal sno_pat_fail(void) {
    return spat_val(spat_new(SPAT_FAIL));
}

SnoVal sno_pat_abort(void) {
    return spat_val(spat_new(SPAT_ABORT));
}

SnoVal sno_pat_succeed(void) {
    return spat_val(spat_new(SPAT_SUCCEED));
}

SnoVal sno_pat_bal(void) {
    return spat_val(spat_new(SPAT_BAL));
}

SnoVal sno_pat_epsilon(void) {
    return spat_val(spat_new(SPAT_EPSILON));
}

SnoVal sno_pat_cat(SnoVal left, SnoVal right) {
    SnoPattern *p = spat_new(SPAT_CAT);
    p->left  = spat_of(left);
    p->right = spat_of(right);
    /* Handle string literals on either side */
    if (!p->left  && left.type  == SNO_STR) p->left  = spat_of(sno_pat_lit(left.s));
    if (!p->right && right.type == SNO_STR) p->right = spat_of(sno_pat_lit(right.s));
    if (!p->left)  return right;   /* degenerate */
    if (!p->right) return left;
    return spat_val(p);
}

SnoVal sno_pat_alt(SnoVal left, SnoVal right) {
    SnoPattern *p = spat_new(SPAT_ALT);
    p->left  = spat_of(left);
    p->right = spat_of(right);
    if (!p->left  && left.type  == SNO_STR) p->left  = spat_of(sno_pat_lit(left.s));
    if (!p->right && right.type == SNO_STR) p->right = spat_of(sno_pat_lit(right.s));
    if (!p->left)  return right;
    if (!p->right) return left;
    return spat_val(p);
}

SnoVal sno_pat_ref(const char *name) {
    SnoPattern *p = spat_new(SPAT_REF);
    p->str = name ? GC_strdup(name) : "";
    return spat_val(p);
}

SnoVal sno_pat_ref_val(SnoVal nameVal) {
    return sno_pat_ref(sno_to_str(nameVal));
}

SnoVal sno_pat_assign_imm(SnoVal child, SnoVal var) {
    SnoPattern *p = spat_new(SPAT_ASSIGN_IMM);
    p->left = spat_of(child);
    if (!p->left && child.type == SNO_STR) p->left = spat_of(sno_pat_lit(child.s));
    p->var  = var;
    return spat_val(p);
}

SnoVal sno_pat_assign_cond(SnoVal child, SnoVal var) {
    SnoPattern *p = spat_new(SPAT_ASSIGN_COND);
    p->left = spat_of(child);
    if (!p->left && child.type == SNO_STR) p->left = spat_of(sno_pat_lit(child.s));
    p->var  = var;
    return spat_val(p);
}

SnoVal sno_var_as_pattern(SnoVal v) {
    /* If v is already a pattern, return it */
    if (v.type == SNO_PATTERN) return v;
    /* If v is a string, treat as a literal pattern */
    if (v.type == SNO_STR || v.type == SNO_NULL) {
        return sno_pat_lit(sno_to_str(v));
    }
    /* Otherwise wrap as a variable lookup */
    SnoPattern *p = spat_new(SPAT_VAR);
    p->var = v;
    return spat_val(p);
}

SnoVal sno_pat_user_call(const char *name, SnoVal *args, int nargs) {
    SnoPattern *p = spat_new(SPAT_USER_CALL);
    p->str   = name ? GC_strdup(name) : "";
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
 * The engine uses malloc'd Pattern nodes that are freed after each match.
 * We materialise lazily: deferred refs are resolved from the var table NOW.
 * Capture variables are recorded in a capture list, applied after match.
 * ===================================================================== */

typedef struct {
    char   *var_name;   /* variable name to assign to */
    int     start;      /* match start cursor */
    int     end;        /* match end cursor */
    int     is_imm;     /* 1 = immediate ($), 0 = conditional (.) */
} Capture;

#define MAX_CAPTURES 64

typedef struct {
    PatternList pl;
    Capture     captures[MAX_CAPTURES];
    int         ncaptures;
    const char *subject;     /* the subject string being matched */
    int         scan_start;  /* current scan start offset for POS/RPOS adjustment */
} MatchCtx;

/* Forward decl */
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

static Pattern *materialise(SnoPattern *sp, MatchCtx *ctx) {
    if (!sp) return make_epsilon(&ctx->pl);

    Pattern *p = pattern_alloc(&ctx->pl);

    switch (sp->kind) {

    case SPAT_LIT:
        p->type  = T_LITERAL;
        p->s     = sp->str ? sp->str : "";
        p->s_len = (int)strlen(p->s);
        return p;

    case SPAT_SPAN:
        p->type  = T_SPAN;
        p->chars = sp->str ? sp->str : "";
        return p;

    case SPAT_BREAK:
        p->type  = T_BREAK;
        p->chars = sp->str ? sp->str : "";
        return p;

    case SPAT_ANY:
        p->type  = T_ANY;
        p->chars = sp->str ? sp->str : "";
        return p;

    case SPAT_NOTANY:
        p->type  = T_NOTANY;
        p->chars = sp->str ? sp->str : "";
        return p;

    case SPAT_LEN:
        p->type = T_LEN;
        p->n    = (int)sp->num;
        return p;

    case SPAT_POS:
        p->type = T_POS;
        /* Adjust for scan offset: POS(n) in original = POS(n - scan_start) in sub-string.
         * If n < scan_start, this can never match from this offset. */
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
        if (getenv("SNO_PAT_DEBUG") && sp->str)
            fprintf(stderr, "  SPAT_REF '%s' -> type=%d\n", sp->str,
                sno_var_get(sp->str).type);
        /* Resolve *name from variable table NOW */
        SnoVal v = sno_var_get(sp->str);
        if (v.type == SNO_PATTERN) {
            /* Cycle detection: track variable names being materialised.
             * Recursive patterns (e.g. snoExpr14 = '|' *snoExpr14 rest)
             * must not expand infinitely. Return epsilon on a cycle. */
            #define MAT_MAX_DEPTH 64
            static __thread const char *_mat_stack[MAT_MAX_DEPTH];
            static __thread int _mat_top = 0;
            for (int _i = 0; _i < _mat_top; _i++) {
                if (_mat_stack[_i] == sp->str || strcmp(_mat_stack[_i], sp->str) == 0) {
                    return make_epsilon(&ctx->pl); /* cycle: return epsilon */
                }
            }
            if (_mat_top < MAT_MAX_DEPTH) _mat_stack[_mat_top++] = sp->str;
            SnoPattern *sp2 = spat_of(v);
            Pattern *result = materialise(sp2, ctx);
            if (_mat_top > 0) _mat_top--;
            return result;
        }
        /* Variable holds a string — treat as literal */
        if (v.type == SNO_STR || v.type == SNO_NULL) {
            p->type  = T_LITERAL;
            p->s     = sno_to_str(v);
            p->s_len = (int)strlen(p->s);
            return p;
        }
        /* Pattern not set yet — epsilon (fail gracefully) */
        return make_epsilon(&ctx->pl);
    }

    case SPAT_VAR: {
        /* Variable holding a pattern value */
        SnoVal v = sp->var;
        if (v.type == SNO_PATTERN) {
            return materialise(spat_of(v), ctx);
        }
        if (v.type == SNO_STR || v.type == SNO_NULL) {
            p->type  = T_LITERAL;
            p->s     = sno_to_str(v);
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
         * is the index into ctx->captures[] where we stored the variable name.
         *
         * The callback (capture_callback below) is called by engine_match_ex, reads
         * ctx->captures[cap_slot].var_name, and calls sno_var_set with the matched span.
         */
        if (ctx->ncaptures >= MAX_CAPTURES) {
            /* Too many captures — degrade to no-capture (child only) */
            return materialise(sp->left, ctx);
        }

        /* Record capture metadata */
        const char *vname = NULL;
        SnoVal vv = sp->var;
        if (vv.type == SNO_STR) vname = vv.s;
        int slot = ctx->ncaptures;
        ctx->captures[slot].var_name = vname ? GC_strdup(vname) : NULL;
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
        if (getenv("SNO_PAT_DEBUG"))
            fprintf(stderr, "SPAT_USER_CALL %s\n", sp->str);
        /* Call a user function that returns a pattern, then materialise */
        SnoVal result = sno_apply(sp->str, sp->args, sp->nargs);
        if (result.type == SNO_PATTERN) {
            return materialise(spat_of(result), ctx);
        }
        if (result.type == SNO_STR || result.type == SNO_NULL) {
            p->type  = T_LITERAL;
            p->s     = sno_to_str(result);
            p->s_len = (int)strlen(p->s);
            return p;
        }
        return make_epsilon(&ctx->pl);
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
    if (getenv("SNO_PAT_DEBUG"))
        fprintf(stderr, "  CAPTURE_CB: slot=%d var=%s start=%d end=%d\n",
            cap_slot, cap->var_name ? cap->var_name : "(null)", cap->start, cap->end);
}

/* Apply captures: call sno_var_set for each fired capture */
static void apply_captures(MatchCtx *ctx) {
    for (int i = 0; i < ctx->ncaptures; i++) {
        Capture *cap = &ctx->captures[i];
        if (cap->start < 0 || !cap->var_name || !cap->var_name[0]) continue;
        int len = cap->end - cap->start;
        if (len < 0) len = 0;
        char *text = (char *)GC_MALLOC(len + 1);
        if (ctx->subject)
            memcpy(text, ctx->subject + cap->start, len);
        text[len] = '\0';
        sno_var_set(cap->var_name, SNO_STR_VAL(text));
        if (getenv("SNO_PAT_DEBUG"))
            fprintf(stderr, "CAPTURE: %s = \"%.*s\"\n", cap->var_name, len, text);
    }
}

/* =========================================================================
 * sno_match_pattern — top-level match entry point
 * ===================================================================== */

/* Internal: try match at a single starting offset. Returns match end (>=start) or -1. */
static int try_match_at(SnoPattern *sp, const char *subject, int slen, int start, MatchCtx *ctx) {
    ctx->scan_start = start;
    Pattern *root = materialise(sp, ctx);
    EngineOpts opts;
    opts.cap_fn   = (ctx->ncaptures > 0) ? capture_callback : NULL;
    opts.cap_data = ctx;
    MatchResult mr = engine_match_ex(root, subject + start, slen - start, &opts);
    if (getenv("SNO_PAT_DEBUG") && slen < 20)
        fprintf(stderr, "  try_match_at: start=%d slen=%d -> matched=%d end=%d\n",
            start, slen, mr.matched, mr.end);
    if (mr.matched) {
        apply_captures(ctx);
        return start + mr.end;
    }
    return -1;
}

int sno_match_pattern(SnoVal pat, const char *subject) {
    int _dbg = getenv("SNO_PAT_DEBUG") && subject && strlen(subject) < 20;
    if (_dbg) fprintf(stderr, "sno_match_pattern: subj=%s(%zu) pat.type=%d\n",
        subject, strlen(subject), pat.type);
    if (!subject) subject = "";

    SnoPattern *sp = spat_of(pat);
    if (!sp) {
        if (pat.type == SNO_STR) {
            const char *lit = pat.s ? pat.s : "";
            return strstr(subject, lit) != NULL;
        }
        return 0;
    }

    int slen = (int)strlen(subject);

    /* SNOBOL4 unanchored match: try each starting position */
    if (sno_kw_anchor) {
        /* &ANCHOR=1: anchored at position 0 only */
        MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = subject;
        int r = try_match_at(sp, subject, slen, 0, &ctx);
        pattern_free_all(&ctx.pl);
        return r >= 0;
    }

    for (int start = 0; start <= slen; start++) {
        MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = subject;
        int r = try_match_at(sp, subject, slen, start, &ctx);
        pattern_free_all(&ctx.pl);
        if (r >= 0) return 1;
        /* Don't scan past end for patterns that consume nothing */
        if (start == slen) break;
    }
    return 0;
}

/* =========================================================================
 * sno_match_and_replace — pattern match with replacement
 *
 * Matches pat against *subject. On success, replaces the matched portion
 * with replacement and updates *subject. Returns 1 on success, 0 on fail.
 * ===================================================================== */

int sno_match_and_replace(SnoVal *subject, SnoVal pat, SnoVal replacement) {
    if (!subject) return 0;
    /* P002: if replacement value signals failure, propagate it as F-branch */
    if (sno_is_fail(replacement)) return 0;

    /* P002: out-of-bounds subscript returns SNO_FAIL_VAL — fail the statement */
    if (sno_is_fail(*subject) || sno_is_fail(replacement)) return 0;

    const char *s = sno_to_str(*subject);
    if (!s) s = "";
    int slen = (int)strlen(s);

    SnoPattern *sp = spat_of(pat);
    if (!sp) return 0;

    /* Try each starting position */
    int match_start = -1, match_end = -1;
    for (int start = 0; start <= slen; start++) {
        MatchCtx ctx; memset(&ctx, 0, sizeof(ctx)); ctx.subject = s;
        ctx.scan_start = start;
        Pattern *root = materialise(sp, &ctx);
        EngineOpts opts;
        opts.cap_fn   = (ctx.ncaptures > 0) ? capture_callback : NULL;
        opts.cap_data = &ctx;
        MatchResult mr = engine_match_ex(root, s + start, slen - start, &opts);
        if (mr.matched) {
            apply_captures(&ctx);
            match_start = start;
            match_end   = start + mr.end;
            pattern_free_all(&ctx.pl);
            break;
        }
        pattern_free_all(&ctx.pl);
        if (start == slen) break;
    }
    if (match_start < 0) return 0;

    const char *repl = sno_to_str(replacement);
    if (!repl) repl = "";
    size_t rlen  = strlen(repl);
    size_t total = (size_t)match_start + rlen + (size_t)(slen - match_end);

    char *result = (char *)GC_MALLOC(total + 1);
    memcpy(result,                          s,    (size_t)match_start);
    memcpy(result + match_start,            repl, rlen);
    memcpy(result + match_start + rlen,     s + match_end, (size_t)(slen - match_end));
    result[total] = '\0';

    *subject = SNO_STR_VAL(result);
    return 1;
}

/* =========================================================================
 * New functions needed by snobol4.c / beautiful.c
 * ===================================================================== */

/* sno_array_create("lo:hi" or "n") — create array from spec string */
SnoVal sno_array_create(SnoVal spec) {
    const char *s = sno_to_str(spec);
    int lo = 1, hi = 1;
    const char *colon = strchr(s, ':');
    if (colon) {
        lo = atoi(s);
        hi = atoi(colon + 1);
    } else {
        hi = atoi(s);
    }
    if (hi < lo) hi = lo;
    SnoArray *a = sno_array_new(lo, hi);
    SnoVal v;
    v.type = SNO_ARRAY;
    v.a    = a;
    return v;
}

/* sno_subscript_get — get arr[idx] */
SnoVal sno_subscript_get(SnoVal arr, SnoVal idx) {
    if (arr.type == SNO_ARRAY) {
        return sno_array_get(arr.a, (int)sno_to_int(idx));  /* returns FAIL if OOB */
    }
    if (arr.type == SNO_TABLE) {
        return sno_table_get(arr.tbl, sno_to_str(idx));
    }
    /* Tree child access: c(x)[i] */
    if (arr.type == SNO_TREE) {
        int i = (int)sno_to_int(idx);
        Tree *child = sno_c_i(arr.t, i);
        return child ? SNO_TREE_VAL(child) : SNO_FAIL_VAL;  /* P002: no child = fail */
    }
    return SNO_FAIL_VAL;  /* P002: unknown container type = fail */
}

/* sno_subscript_set — arr[idx] = val */
void sno_subscript_set(SnoVal arr, SnoVal idx, SnoVal val) {
    if (arr.type == SNO_ARRAY) {
        sno_array_set(arr.a, (int)sno_to_int(idx), val);
        return;
    }
    if (arr.type == SNO_TABLE) {
        sno_table_set(arr.tbl, sno_to_str(idx), val);
        return;
    }
}

/* sno_subscript_get2 / sno_subscript_set2 — 2D */
SnoVal sno_subscript_get2(SnoVal arr, SnoVal i, SnoVal j) {
    if (arr.type == SNO_ARRAY)
        return sno_array_get2(arr.a, (int)sno_to_int(i), (int)sno_to_int(j));
    return SNO_FAIL_VAL;  /* P002: not an array — fail the statement */
}

void sno_subscript_set2(SnoVal arr, SnoVal i, SnoVal j, SnoVal val) {
    if (arr.type == SNO_ARRAY)
        sno_array_set2(arr.a, (int)sno_to_int(i), (int)sno_to_int(j), val);
}

/* sno_tree_new — 4-arg version: creates a DATA('tree(t,v,n,c)') instance */
SnoVal sno_make_tree(SnoVal tag, SnoVal val, SnoVal n_children, SnoVal children) {
    /* Ensure the tree type is registered */
    if (!sno_func_exists("t")) {
        sno_data_define("tree(t,v,n,c)");
    }
    return sno_udef_new("tree", tag, val, n_children, children, (SnoVal){0});
}

/* sno_push_val / sno_pop_val / sno_top_val — aliases for sno_push/pop/top */
SnoVal sno_push_val(SnoVal x) {
    sno_push(x);
    return SNO_NULL_VAL;
}

SnoVal sno_pop_val(void) {
    return sno_pop();
}

SnoVal sno_top_val(void) {
    return sno_top();
}

/* sno_register_fn — register a C function in the global function table */
void sno_register_fn(const char *name, SnoVal (*fn)(SnoVal*, int), int min_args, int max_args) {
    (void)min_args; (void)max_args;
    sno_define(name, fn);
}

/* sno_define_spec — DEFINE('name(args)locals') */
void sno_define_spec(SnoVal spec) {
    sno_define(sno_to_str(spec), NULL);
}

/* sno_apply_val — APPLY(fnval, args...) */
SnoVal sno_apply_val(SnoVal fnval, SnoVal *args, int nargs) {
    const char *name = sno_to_str(fnval);
    return sno_apply(name, args, nargs);
}

/* sno_eval — EVAL(expr) — evaluate a string as a SNOBOL4 expression */
SnoVal sno_eval(SnoVal expr) {
    /* Full EVAL requires a compiler/interpreter loop — for now,
     * if the expression is already a value, return it.
     * If it's a string that names a variable, return that variable's value.
     * TODO: full expression evaluator */
    const char *s = sno_to_str(expr);
    if (!s || !*s) return SNO_NULL_VAL;
    /* Try as a variable name first */
    SnoVal v = sno_var_get(s);
    if (v.type != SNO_NULL) return v;
    /* Try as an integer */
    if (isdigit((unsigned char)s[0]) || s[0] == '-') {
        char *end;
        long long i = strtoll(s, &end, 10);
        if (*end == '\0') return SNO_INT_VAL(i);
    }
    /* Return as string */
    return SNO_STR_VAL(GC_strdup(s));
}

/* sno_opsyn — OPSYN(new, old, type) */
SnoVal sno_opsyn(SnoVal newname, SnoVal oldname, SnoVal type) {
    (void)type;
    /* Register new as an alias for old */
    const char *nm = sno_to_str(newname);
    const char *old = sno_to_str(oldname);
    /* For now just a no-op — full OPSYN is complex */
    (void)nm; (void)old;
    return SNO_NULL_VAL;
}

/* sno_sort_fn — SORT(array) */
SnoVal sno_sort_fn(SnoVal arr) {
    /* Stub — return input unchanged */
    return arr;
}
