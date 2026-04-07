/*======================================================================================
 * engine.c — scrip-cc Byrd Box MATCH_fn engine (pure C, no Python)
 *
 * Derived from SNOBOL4cython/snobol4c_module.c (Lon Cherryholmes, 2026).
 * Extracted: Python→C converter and CPython module removed entirely.
 *
 * Implements:
 *   Psi  — continuation stack
 *   Omega — backtrack stack with owned Psi snapshots
 *   z_*  — state navigation helpers
 *   scan_* — leaf primitive scanners
 *   engine_match() — the main dispatch loop
 *======================================================================================*/
#include "engine.h"
#include <stdio.h>
#include <stdint.h>

/*======================================================================================
 * Psi — realloc'd array.  Plain PUSH_fn/POP_fn stack.  Deep-copied into omega snapshots.
 *======================================================================================*/
typedef struct {
    Pattern *   PI;
    int         ctx;
} PsiEntry;

typedef struct {
    PsiEntry *  entries;    /* realloc'd with doubling */
    int         count;
    int         capacity;
} Psi;

static inline void psi_push(Psi *psi, Pattern *PI, int ctx) {
    if (psi->count >= psi->capacity) {
        psi->capacity = psi->capacity ? psi->capacity * 2 : 8;
        psi->entries = realloc(psi->entries, psi->capacity * sizeof(PsiEntry));
    }
    psi->entries[psi->count++] = (PsiEntry){PI, ctx};
}

static inline bool psi_pop(Psi *psi, Pattern **PI, int *ctx) {
    if (psi->count > 0) {
        *PI  = psi->entries[--psi->count].PI;
        *ctx = psi->entries[  psi->count].ctx;
        return true;
    }
    return false;
}

static inline Psi psi_snapshot(Psi *psi) {
    Psi snap = {NULL, psi->count, psi->count};
    if (psi->count > 0) {
        snap.entries = malloc(psi->count * sizeof(PsiEntry));
        memcpy(snap.entries, psi->entries, psi->count * sizeof(PsiEntry));
    }
    return snap;
}

static inline void psi_restore(Psi *psi, Psi *snap) {
    if (snap->count > psi->capacity) {
        psi->capacity = snap->count;
        psi->entries = realloc(psi->entries, psi->capacity * sizeof(PsiEntry));
    }
    psi->count = snap->count;
    if (snap->count > 0)
        memcpy(psi->entries, snap->entries, snap->count * sizeof(PsiEntry));
}

/*======================================================================================
 * State — engine working state (psi is separate)
 *======================================================================================*/
typedef struct {
    const char *    SIGMA;
    int             DELTA;
    int             OMEGA;
    const char *    sigma;
    int             delta;
    Pattern *       PI;
    int             fenced;
    int             yielded;
    int             ctx;
    /* Capture support — non-NULL when engine_match_ex is used */
    CaptureFn       cap_fn;
    void           *cap_data;
    int             cap_start;  /* DELTA at the point T_CAPTURE entered its child */
    /* Variable-resolve support for T_VARREF (deferred pattern refs inside ARBNO) */
    VarResolveFn    var_fn;
    void           *var_data;
    /* Absolute start offset of this sub-string in the original subject.
     * Used to make T_POS/T_TAB work correctly when materialise is done once
     * at scan_start=0 and re-used across multiple position attempts. */
    int             scan_start;
} State;

/*======================================================================================
 * Omega — realloc'd backtrack stack.  Each entry owns a psi snapshot.
 *======================================================================================*/
typedef struct {
    State   state;
    Psi     psi_snap;   /* owned copy */
} OmegaEntry;

typedef struct {
    OmegaEntry *    entries;    /* realloc'd with doubling */
    int             count;
    int             capacity;
} Omega;

static void omega_push(Omega *omega, State *z, Psi *psi) {
    if (omega->count >= omega->capacity) {
        omega->capacity = omega->capacity ? omega->capacity * 2 : 8;
        omega->entries = realloc(omega->entries, omega->capacity * sizeof(OmegaEntry));
    }
    omega->entries[omega->count].state    = *z;
    omega->entries[omega->count].psi_snap = psi_snapshot(psi);
    omega->count++;
}

static void omega_pop(Omega *omega, State *z, Psi *psi) {
    if (omega->count > 0) {
        OmegaEntry *e = &omega->entries[--omega->count];
        *z = e->state;
        psi_restore(psi, &e->psi_snap);
        free(e->psi_snap.entries);
        e->psi_snap.entries = NULL;
    } else {
        memset(z, 0, sizeof(State));
        z->PI = NULL;
        psi->count = 0;
    }
}

static OmegaEntry *omega_tip(Omega *omega) {
    return omega->count > 0 ? &omega->entries[omega->count - 1] : NULL;
}

static void omega_free(Omega *omega) {
    for (int i = 0; i < omega->count; i++)
        free(omega->entries[i].psi_snap.entries);
    free(omega->entries);
    omega->entries  = NULL;
    omega->count    = 0;
    omega->capacity = 0;
}

/*======================================================================================
 * State navigation
 *======================================================================================*/
static inline void z_down(State *z, Psi *psi) {
    psi_push(psi, z->PI, z->ctx);
    z->sigma = z->SIGMA;
    z->delta = z->DELTA;
    z->PI    = z->PI->children[z->ctx];
    z->ctx   = 0;
}

static inline void z_down_single(State *z, Psi *psi) {
    psi_push(psi, z->PI, z->ctx);
    z->sigma = z->SIGMA;
    z->delta = z->DELTA;
    z->PI    = z->PI->children[0];
    z->ctx   = 0;
}

static inline void z_up(State *z, Psi *psi) {
    if (!psi_pop(psi, &z->PI, &z->ctx))
        z->PI = NULL;
}

static inline void z_up_track(State *z, Psi *psi, Omega *omega) {
    OmegaEntry *track = omega_tip(omega);
    if (track) {
        track->state.SIGMA   = z->SIGMA;
        track->state.DELTA   = z->DELTA;
        track->state.sigma   = z->sigma;
        track->state.delta   = z->delta;
        track->state.yielded = 1;
    }
    z_up(z, psi);
}

static inline void z_up_fail(State *z, Psi *psi) {
    z_up(z, psi);
}

static inline void z_stay_next(State *z) {
    z->sigma   = z->SIGMA;
    z->delta   = z->DELTA;
    z->yielded = 0;
    z->ctx++;
}

static inline void z_move_next(State *z) {
    z->SIGMA   = z->sigma;
    z->DELTA   = z->delta;
    z->yielded = 0;
    z->ctx++;
}

static inline void z_next(State *z) {
    z->sigma = z->SIGMA;
    z->delta = z->DELTA;
}

/*======================================================================================
 * Pattern scanners
 *======================================================================================*/
static inline bool scan_move(State *z, int delta) {
    if (delta >= 0 && z->DELTA + delta <= z->OMEGA) {
        z->sigma += delta;
        z->delta += delta;
        return true;
    }
    return false;
}

static bool scan_LITERAL(State *z) {
    if (z->delta + z->PI->s_len > z->OMEGA) return false;
    if (memcmp(z->sigma, z->PI->s, z->PI->s_len) != 0) return false;
    z->sigma += z->PI->s_len;
    z->delta += z->PI->s_len;
    return true;
}

static bool scan_ANY(State *z) {
    if (z->delta >= z->OMEGA) return false;
    for (const char *c = z->PI->chars; *c; c++)
        if (*z->sigma == *c) { z->sigma++; z->delta++; return true; }
    return false;
}

static bool scan_NOTANY(State *z) {
    if (z->delta >= z->OMEGA) return false;
    for (const char *c = z->PI->chars; *c; c++)
        if (*z->sigma == *c) return false;
    z->sigma++; z->delta++; return true;
}

static bool scan_SPAN(State *z) {
    int start = z->delta;
    while (z->delta < z->OMEGA) {
        const char *c;
        for (c = z->PI->chars; *c; c++)
            if (*z->sigma == *c) break;
        if (!*c) break;
        z->sigma++; z->delta++;
    }
    return z->delta > start;
}

static bool scan_BREAK(State *z) {
    while (z->delta < z->OMEGA) {
        for (const char *c = z->PI->chars; *c; c++)
            if (*z->sigma == *c) return true;
        z->sigma++; z->delta++;
    }
    return false;
}

static bool scan_ARB(State *z) {
    if (z->DELTA + z->ctx <= z->OMEGA) {
        z->sigma += z->ctx;
        z->delta += z->ctx;
        return true;
    }
    return false;
}

static bool scan_BAL(State *z) {
    int nest = 0;
    z->sigma += z->ctx + 1;
    z->delta += z->ctx + 1;
    while (z->delta <= z->OMEGA) {
        char ch = z->sigma[-1];
        if      (ch == '(') nest++;
        else if (ch == ')') nest--;
        if (nest < 0) break;
        else if (nest > 0 && z->delta >= z->OMEGA) break;
        else if (nest == 0) { z->ctx = z->delta; return true; }
        z->sigma++; z->delta++;
    }
    return false;
}

static inline bool scan_POS(State *z)   { return z->PI->n == z->DELTA + z->scan_start; }
static inline bool scan_RPOS(State *z)  { return z->PI->n == z->OMEGA - z->DELTA; }
static inline bool scan_LEN(State *z)   { return scan_move(z, z->PI->n); }
static inline bool scan_TAB(State *z)   { return scan_move(z, z->PI->n - z->DELTA - z->scan_start); }
static inline bool scan_REM(State *z)   { return scan_move(z, z->OMEGA - z->DELTA); }
static inline bool scan_RTAB(State *z)  { return scan_move(z, z->OMEGA - z->DELTA - z->PI->n); }
static inline bool scan_ALPHA(State *z) { return z->DELTA == 0 || (z->DELTA > 0 && z->SIGMA[-1] == '\n'); }
static inline bool scan_OMEGA(State *z) { return z->DELTA == z->OMEGA || (z->DELTA < z->OMEGA && z->SIGMA[0] == '\n'); }

/*======================================================================================
 * THE MATCH ENGINE
 *======================================================================================*/
MatchResult engine_match(Pattern *root, const char *subject, int subject_len) {
    return engine_match_ex(root, subject, subject_len, NULL);
}

MatchResult engine_match_ex(Pattern *root, const char *subject, int subject_len,
                             const EngineOpts *opts) {
    MatchResult result = {0, 0, 0};

    if (getenv("PAT_DEBUG") && subject_len >= 4 && subject_len <= 8) {
        fprintf(stderr, "  ENGINE_ENTRY slen=%d subj=%.10s\n", subject_len, subject ? subject : "(null)");
    }

    Psi   psi   = {NULL, 0, 0};
    Omega omega = {NULL, 0, 0};

    int a = PROCEED;
    State Z;
    memset(&Z, 0, sizeof(Z));
    Z.SIGMA    = subject;
    Z.OMEGA    = subject_len;
    Z.sigma    = subject;
    Z.PI       = root;
    Z.cap_fn   = opts ? opts->cap_fn   : NULL;
    Z.cap_data = opts ? opts->cap_data : NULL;
    Z.var_fn   = opts ? opts->var_fn   : NULL;
    Z.var_data = opts ? opts->var_data : NULL;
    Z.scan_start = opts ? opts->scan_start : 0;

    int _iter = 0;
    while (Z.PI) {
        if (++_iter > 10000000) {
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  ENGINE LOOP LIMIT slen=%d a=%d\n", subject_len, a);
            break;
        }
        int t = Z.PI->type;
        switch (t << 2 | a) {
/*--- Π (alternation) ---------------------------------------------------------------*/
        case T_PI<<2|PROCEED:
            if (Z.ctx < Z.PI->n) { a = PROCEED; omega_push(&omega, &Z, &psi); z_down(&Z, &psi);              break; }
            else                 { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
        case T_PI<<2|SUCCEED:    { a = SUCCEED;                                 z_up(&Z, &psi);                break; }
        case T_PI<<2|CONCEDE:    { a = PROCEED;                                 z_stay_next(&Z);               break; }
        case T_PI<<2|RECEDE:
            if (Z.ctx < Z.PI->n) { a = PROCEED;                                z_stay_next(&Z);               break; }
            else                 { a = CONCEDE;                                 z_up_fail(&Z, &psi);           break; }
/*--- Σ (sequence) ------------------------------------------------------------------*/
        case T_SIGMA<<2|PROCEED:
            if (Z.ctx < Z.PI->n) { a = PROCEED;                                z_down(&Z, &psi);              break; }
            else                 {
                if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                    fprintf(stderr, "  SIGMA SUCCEED ctx=%d/%d delta=%d\n", Z.ctx, Z.PI->n, Z.delta);
                a = SUCCEED;                                 z_up(&Z, &psi);                break; }
        case T_SIGMA<<2|SUCCEED:
            if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                fprintf(stderr, "  SIGMA<<SUCCEED ctx=%d/%d delta=%d\n", Z.ctx, Z.PI->n, Z.delta);
            { a = PROCEED;                                 z_move_next(&Z);               break; }
        case T_SIGMA<<2|CONCEDE:
            if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                fprintf(stderr, "  SIGMA CONCEDE ctx=%d/%d delta=%d\n", Z.ctx, Z.PI->n, Z.delta);
            { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
/*--- ρ (conjunction) ---------------------------------------------------------------*/
        case T_RHO<<2|PROCEED:
            if (Z.ctx < Z.PI->n) { a = PROCEED;                                z_down(&Z, &psi);              break; }
            else                 { a = SUCCEED;                                 z_up(&Z, &psi);                break; }
        case T_RHO<<2|SUCCEED:   { a = PROCEED;                                 z_stay_next(&Z);               break; }
        case T_RHO<<2|CONCEDE:   { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
/*--- π (optional) ------------------------------------------------------------------*/
        case T_pi<<2|PROCEED:
            if (Z.ctx == 0)      { a = SUCCEED;  omega_push(&omega, &Z, &psi); z_up(&Z, &psi);                break; }
            else if (Z.ctx == 1) { a = PROCEED;  omega_push(&omega, &Z, &psi); z_down_single(&Z, &psi);       break; }
            else                 { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
        case T_pi<<2|SUCCEED:    { a = SUCCEED;                                 z_up(&Z, &psi);                break; }
        case T_pi<<2|CONCEDE:    { a = CONCEDE;                                 z_up_fail(&Z, &psi);           break; }
        case T_pi<<2|RECEDE:
            if (!Z.fenced)       { a = PROCEED;                                 z_stay_next(&Z);               break; }
            else                 { a = CONCEDE;                                 z_up_fail(&Z, &psi);           break; }
/*--- ARBNO -------------------------------------------------------------------------*/
        case T_ARBNO<<2|PROCEED:
            if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                fprintf(stderr, "  ARBNO PROCEED ctx=%d DELTA=%d OMEGA=%d\n", Z.ctx, Z.DELTA, Z.OMEGA);
            if (Z.ctx == 0)      { a = SUCCEED;  omega_push(&omega, &Z, &psi); z_up_track(&Z, &psi, &omega);  break; }
            else                 { a = PROCEED;  omega_push(&omega, &Z, &psi); z_down_single(&Z, &psi);       break; }
        case T_ARBNO<<2|SUCCEED:
            if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                fprintf(stderr, "  ARBNO SUCCEED delta=%d OMEGA=%d\n", Z.delta, Z.OMEGA);
            { a = SUCCEED;                                 z_up_track(&Z, &psi, &omega);  break; }
        case T_ARBNO<<2|CONCEDE: { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
        case T_ARBNO<<2|RECEDE:
            if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                fprintf(stderr, "  ARBNO RECEDE ctx=%d yielded=%d fenced=%d DELTA=%d delta=%d\n", Z.ctx, Z.yielded, Z.fenced, Z.DELTA, Z.delta);
            if (Z.fenced)                         { a = CONCEDE; z_up_fail(&Z, &psi); break; }
            else if (Z.yielded && Z.delta > Z.DELTA) { a = PROCEED; z_move_next(&Z);     break; } /* made progress */
            else if (Z.yielded)                   { a = CONCEDE; z_up_fail(&Z, &psi); break; } /* zero-progress: stop */
            else                                  { a = CONCEDE; z_up_fail(&Z, &psi); break; }
/*--- ARB ---------------------------------------------------------------------------*/
        case T_ARB<<2|PROCEED:
            if (scan_ARB(&Z))    { a = SUCCEED;  omega_push(&omega, &Z, &psi); z_up(&Z, &psi);                break; }
            else                 { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
        case T_ARB<<2|RECEDE:
            if (!Z.fenced)       { a = PROCEED;                                 z_stay_next(&Z);               break; }
            else                 { a = CONCEDE;                                 z_up_fail(&Z, &psi);           break; }
/*--- BAL ---------------------------------------------------------------------------*/
        case T_BAL<<2|PROCEED:
            if (scan_BAL(&Z))    { a = SUCCEED;  omega_push(&omega, &Z, &psi); z_up(&Z, &psi);                break; }
            else                 { a = RECEDE;   omega_pop(&omega, &Z, &psi);                                  break; }
        case T_BAL<<2|RECEDE:
            if (!Z.fenced)       { a = PROCEED;                                 z_next(&Z);                    break; }
            else                 { a = CONCEDE;                                 z_up_fail(&Z, &psi);           break; }
/*--- FENCE -------------------------------------------------------------------------*/
        case T_FENCE<<2|PROCEED:
            if (Z.PI->n == 0)    { a = SUCCEED;  omega_push(&omega, &Z, &psi); z_up(&Z, &psi);                break; }
            else                 { a = PROCEED;  Z.fenced = 1;                  z_down_single(&Z, &psi);       break; }
        case T_FENCE<<2|RECEDE:
            if (Z.PI->n == 0)    { a = RECEDE;   Z.PI = NULL;                                                  break; }
            else                 { a = CONCEDE;   Z.PI = NULL;                                                  break; }
        case T_FENCE<<2|SUCCEED:
            if (Z.PI->n == 1)    { a = SUCCEED;  Z.fenced = 0;                  z_up(&Z, &psi);               break; }
            else                 { a = CONCEDE;   Z.PI = NULL;                                                  break; }
        case T_FENCE<<2|CONCEDE:
            if (Z.PI->n == 1)    { a = CONCEDE;  Z.fenced = 0;                  z_up_fail(&Z, &psi);          break; }
            else                 { a = CONCEDE;   Z.PI = NULL;                                                  break; }
/*--- Control -----------------------------------------------------------------------*/
        case T_ABORT<<2|PROCEED:   { a = CONCEDE;  Z.PI = NULL;                                                break; }
        case T_SUCCEED<<2|PROCEED: { a = SUCCEED;  omega_push(&omega, &Z, &psi); z_up(&Z, &psi);              break; }
        case T_SUCCEED<<2|RECEDE:
            if (!Z.fenced)         { a = PROCEED;                                z_stay_next(&Z);              break; }
            else                   { a = CONCEDE;                                z_up_fail(&Z, &psi);          break; }
        case T_FAIL<<2|PROCEED:    { a = CONCEDE;                                z_up_fail(&Z, &psi);          break; }
        case T_EPSILON<<2|PROCEED: { a = SUCCEED;                                z_up(&Z, &psi);               break; }
/*--- T_VARREF: deferred variable pattern ref (used inside ARBNO for recursive grammars) ---*/
        case T_VARREF<<2|PROCEED: {
            /* Resolve the variable name to a Pattern* at MATCH_fn time */
            Pattern *resolved = Z.var_fn ? Z.var_fn(Z.PI->s, Z.var_data) : NULL;
            if (!resolved || resolved->type == T_EPSILON) {
                /* var not set or empty — treat as epsilon (succeed without consuming) */
                a = SUCCEED; z_up(&Z, &psi);
            } else {
                /* Push this T_VARREF node as the continuation (ctx=1 = succeed side),
                 * then descend into the resolved pattern. */
                psi_push(&psi, Z.PI, 1);
                Z.PI    = resolved;
                Z.ctx   = 0;
                /* Reset cursor to current start position so child pattern starts fresh */
                Z.sigma = Z.SIGMA;
                Z.delta = Z.DELTA;
                a       = PROCEED;
            }
            break;
        }
        case T_VARREF<<2|SUCCEED:  { a = SUCCEED;  z_up(&Z, &psi); break; }
        case T_VARREF<<2|CONCEDE:  { a = CONCEDE;  z_up_fail(&Z, &psi); break; }
        case T_VARREF<<2|RECEDE:   { a = CONCEDE;  z_up_fail(&Z, &psi); break; }
/*--- Leaf scanners -----------------------------------------------------------------*/
        case T_LITERAL<<2|PROCEED:
            if (scan_LITERAL(&Z)) {
                if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                    fprintf(stderr, "  LIT '%.*s' MATCH delta=%d\n", Z.PI->s_len, Z.PI->s, Z.delta);
                a = SUCCEED; z_up(&Z, &psi);
            } else {
                if (getenv("PAT_DEBUG") && Z.OMEGA <= 6 && Z.PI->s_len <= 4)
                    fprintf(stderr, "  LIT '%.*s' DT_FAIL at delta=%d\n", Z.PI->s_len, Z.PI->s, Z.delta);
                a = CONCEDE; z_up_fail(&Z, &psi);
            }
            break;

        case T_ANY<<2|PROCEED:
            if (scan_ANY(&Z))      { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_NOTANY<<2|PROCEED:
            if (scan_NOTANY(&Z))   { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_SPAN<<2|PROCEED:
            if (scan_SPAN(&Z))     { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_BREAK<<2|PROCEED:
            if (scan_BREAK(&Z)) {
                if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                    fprintf(stderr, "  BREAK MATCH delta=%d chars='%.8s'\n", Z.delta, Z.PI->chars);
                a = SUCCEED; z_up(&Z, &psi);
            } else {
                if (getenv("PAT_DEBUG") && Z.OMEGA <= 6)
                    fprintf(stderr, "  BREAK DT_FAIL at delta=%d chars='%.8s'\n", Z.delta, Z.PI->chars);
                a = CONCEDE; z_up_fail(&Z, &psi);
            }
            break;
        case T_POS<<2|PROCEED:
            if (scan_POS(&Z))      { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_RPOS<<2|PROCEED:
            if (scan_RPOS(&Z))     { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_LEN<<2|PROCEED:
            if (scan_LEN(&Z))      { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_TAB<<2|PROCEED:
            if (scan_TAB(&Z))      { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_RTAB<<2|PROCEED:
            if (scan_RTAB(&Z))     { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_REM<<2|PROCEED:
            if (scan_REM(&Z))      { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_ALPHA<<2|PROCEED:
            if (scan_ALPHA(&Z))    { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
        case T_OMEGA<<2|PROCEED:
            if (scan_OMEGA(&Z))    { a = SUCCEED; z_up(&Z, &psi); } else { a = CONCEDE; z_up_fail(&Z, &psi); } break;
/*--- MARB --------------------------------------------------------------------------*/
        case T_MARB<<2|PROCEED:
            if (scan_ARB(&Z))      { a = SUCCEED; omega_push(&omega, &Z, &psi); z_up(&Z, &psi); break; }
            else                   { a = RECEDE;  omega_pop(&omega, &Z, &psi);                   break; }
        case T_MARB<<2|RECEDE:
            if (!Z.fenced)         { a = PROCEED;                                z_stay_next(&Z); break; }
            else                   { a = CONCEDE;                                z_up_fail(&Z, &psi); break; }
/*--- T_CAPTURE (capture the span matched by child) ---------------------------------*/
        case T_CAPTURE<<2|PROCEED: {
            /* Record start, PUSH_fn omega for potential backtrack, descend into child */
            int _cap_slot = Z.PI->n;
            Z.cap_start = Z.DELTA;
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  T_CAPTURE PROCEED slot=%d DELTA=%d\n", _cap_slot, Z.cap_start);
            omega_push(&omega, &Z, &psi);
            z_down_single(&Z, &psi);
            a = PROCEED;
            break;
        }
        case T_CAPTURE<<2|SUCCEED:
            /* Child matched: fire callback with (cap_slot, start, delta_end).
             * Z.delta = uncommitted cursor after child's MATCH_fn. */
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  T_CAPTURE SUCCEED slot=%d delta=%d (cap_start=%d) psi_depth=%d\n",
                    Z.PI->n, Z.delta, Z.cap_start, psi.count);
            if (Z.cap_fn)
                Z.cap_fn(Z.PI->n, Z.cap_start, Z.delta, Z.cap_data);
            a = SUCCEED;
            z_up(&Z, &psi);
            if (getenv("PAT_DEBUG") && Z.PI)
                fprintf(stderr, "  T_CAPTURE SUCCEED: after z_up, Z.PI->type=%d Z.ctx=%d\n", Z.PI->type, Z.ctx);
            break;
        case T_CAPTURE<<2|CONCEDE:
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  T_CAPTURE CONCEDE slot=%d\n", Z.PI->n);
            a = RECEDE;
            omega_pop(&omega, &Z, &psi);
            break;
        case T_CAPTURE<<2|RECEDE:
            if (getenv("PAT_DEBUG"))
                fprintf(stderr, "  T_CAPTURE RECEDE slot=%d\n", Z.PI->n);
            a = CONCEDE;
            z_up_fail(&Z, &psi);
            break;
/*--- T_FUNC (zero-width side-effect call at MATCH_fn time) ----------------------------*/
        case T_FUNC<<2|PROCEED:
            if (Z.PI->func) {
                void *r = Z.PI->func(Z.PI->func_data);
                if (r == (void *)(intptr_t)-1) { a = CONCEDE; z_up_fail(&Z, &psi); break; }
            }
            a = SUCCEED; z_up(&Z, &psi); break;
        case T_FUNC<<2|SUCCEED:  { a = SUCCEED; z_up(&Z, &psi);      break; }
        case T_FUNC<<2|CONCEDE:  { a = CONCEDE; z_up_fail(&Z, &psi); break; }
        case T_FUNC<<2|RECEDE:   { a = CONCEDE; z_up_fail(&Z, &psi); break; }
/*-----------------------------------------------------------------------------------*/
        default:
            a = CONCEDE;
            Z.PI = NULL;
            break;
        }
    }

    if (a == SUCCEED) {
        result.matched = 1;
        result.start   = 0;
        result.end     = Z.delta;
    }

    if (getenv("PAT_DEBUG") && subject_len <= 6 && subject_len > 0) {
        fprintf(stderr, "  engine_match_ex: slen=%d -> a=%d matched=%d end=%d\n",
            subject_len, a, result.matched, result.end);
    }

    /* Cleanup */
    free(psi.entries);
    omega_free(&omega);

    return result;
}

