/*
 * nmd.c — SIL Naming List (§NMD)  RT-4  *** REFACTOR: unified ordered event queue ***
 *
 * ARCHITECTURE:
 *
 *   The naming list is a stack of NamFrame objects.  Each call to
 *   NAM_save() pushes a new frame; NAM_commit/NAM_discard pops it.
 *   Within a frame, entries are a singly-linked list appended in
 *   left-to-right match order (oldest → newest).
 *
 *   Two entry kinds share the same list:
 *     NAM_KIND_CAPTURE — XNME (.) conditional capture (assign substr to var)
 *     NAM_KIND_CALLCAP — XCALLCAP (pat . *fn()) indirect-call side effect
 *
 *   Unified ordering is the fix for SC-26:
 *     (word . tag) && (epsilon . *push_list())
 *   queues [CAPTURE tag="NP", CALLCAP push_list] in that order.
 *   NAM_commit walks oldest→newest, so tag is assigned before push_list()
 *   fires and reads it.  Last-write-wins dedup applies only to CAPTURE
 *   entries (same target, later value wins); CALLCAP entries always fire.
 *
 *   Byrd box mapping:
 *     XNME γ      →  NAM_push()          (capture entry, append)
 *     XCALLCAP γ  →  NAM_push_callcap()  (callcap entry, append)
 *     discard     →  NAM_discard()       (clear frame list)
 *     outer γ     →  NAM_commit()        (walk oldest→newest, dispatch)
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-17
 * SPRINT:  SC-26
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc/gc.h>

#include "snobol4.h"
#include "sil_macros.h"

/*---------------------------------------------------------------------------*/
/* Entry kind                                                                 */
/*---------------------------------------------------------------------------*/

typedef enum {
    NAM_KIND_CAPTURE = 0,  /* XNME (.) conditional capture                  */
    NAM_KIND_CALLCAP = 1   /* XCALLCAP (pat . *fn()) deferred call           */
} NamKind_t;

/*---------------------------------------------------------------------------*/
/* NamEntry_t — one node in the ordered event list                           */
/*---------------------------------------------------------------------------*/

typedef struct NamEntry {
    NamKind_t       kind;

    /* ── CAPTURE fields ── */
    const char     *varname;   /* GC_strdup'd target name (or NULL if var_ptr) */
    DESCR_t        *var_ptr;   /* interior pointer target (or NULL)             */
    int             dt;        /* DT_S / DT_K / DT_E                           */
    const char     *substr;    /* GC_malloc'd matched substring                 */
    int             slen;

    /* ── CALLCAP fields ── */
    const char     *fnc_name;  /* function name                                 */
    DESCR_t        *fnc_args;  /* static arg array (may be NULL)                */
    int             fnc_nargs;
    /* TL-2: arg *names* for flush-time resolution.  When fnc_arg_names is
     * non-NULL NAM_commit calls NV_GET_fn(name) per entry at commit time,
     * after all in-order earlier . captures have already written their vars. */
    char          **fnc_arg_names;
    int             fnc_n_arg_names;
    const char     *cc_substr; /* GC_malloc'd matched text (epsilon → "")       */
    int             cc_slen;

    struct NamEntry *next;     /* list link — oldest → newest                   */
} NamEntry_t;

/*---------------------------------------------------------------------------*/
/* NamFrame_t — one save/commit-or-discard scope                             */
/*---------------------------------------------------------------------------*/

typedef struct NamFrame {
    NamEntry_t     *head;       /* oldest entry                                 */
    NamEntry_t     *tail;       /* newest entry — O(1) append                   */
    struct NamFrame *prev;
} NamFrame_t;

/*---------------------------------------------------------------------------*/
/* Stack state                                                                */
/*---------------------------------------------------------------------------*/

static NamFrame_t *nam_stack = NULL;
static int         nam_depth = 0;

/*---------------------------------------------------------------------------*/
/* g_user_call_hook — supplied by stmt_exec.c; needed for callcap dispatch   */
/*---------------------------------------------------------------------------*/

extern DESCR_t (*g_user_call_hook)(const char *name, DESCR_t *args, int nargs);

/*---------------------------------------------------------------------------*/
/* NAM_save                                                                   */
/*---------------------------------------------------------------------------*/

int NAM_save(void)
{
    int cookie = nam_depth;
    NamFrame_t *f = GC_MALLOC(sizeof(NamFrame_t));
    f->head = NULL;
    f->tail = NULL;
    f->prev = nam_stack;
    nam_stack = f;
    nam_depth++;
    return cookie;
}

/*---------------------------------------------------------------------------*/
/* nam_append — internal O(1) append to current frame                        */
/*---------------------------------------------------------------------------*/

static void nam_append(NamEntry_t *e)
{
    e->next = NULL;
    if (nam_stack->tail) {
        nam_stack->tail->next = e;
        nam_stack->tail       = e;
    } else {
        nam_stack->head = e;
        nam_stack->tail = e;
    }
}

/*---------------------------------------------------------------------------*/
/* NAM_push — record one XNME (.) conditional capture                        */
/*---------------------------------------------------------------------------*/

void NAM_push(const char *var, DESCR_t *ptr, int dt,
              const char *s, int len)
{
    if (!nam_stack) {
        fprintf(stderr, "nmd: NAM_push with no active frame — ignored\n");
        return;
    }

    NamEntry_t *e = GC_MALLOC(sizeof(NamEntry_t));
    e->kind    = NAM_KIND_CAPTURE;
    e->varname = var ? GC_strdup(var) : NULL;
    e->var_ptr = ptr;
    e->dt      = dt;

    if (s && len > 0) {
        char *copy = GC_MALLOC((size_t)len + 1);
        memcpy(copy, s, (size_t)len);
        copy[len] = '\0';
        e->substr = copy;
        e->slen   = len;
    } else {
        e->substr = "";
        e->slen   = 0;
    }

    /* callcap fields unused */
    e->fnc_name  = NULL;
    e->fnc_args  = NULL;
    e->fnc_nargs = 0;
    e->cc_substr = NULL;
    e->cc_slen   = 0;

    nam_append(e);
}

/*---------------------------------------------------------------------------*/
/* NAM_push_callcap — record one XCALLCAP (pat . *fn()) deferred call        */
/* Called from bb_callcap CC_γ_core instead of g_cc_events push.             */
/*---------------------------------------------------------------------------*/

void NAM_push_callcap(const char *fnc_name, DESCR_t *fnc_args, int fnc_nargs,
                      const char *matched_text, int matched_len)
{
    NAM_push_callcap_named(fnc_name, fnc_args, fnc_nargs, NULL, 0,
                            matched_text, matched_len);
}

void NAM_push_callcap_named(const char *fnc_name,
                             DESCR_t *fnc_args, int fnc_nargs,
                             char **fnc_arg_names, int fnc_n_arg_names,
                             const char *matched_text, int matched_len)
{
    if (!nam_stack) {
        fprintf(stderr, "nmd: NAM_push_callcap with no active frame — ignored\n");
        return;
    }

    NamEntry_t *e = GC_MALLOC(sizeof(NamEntry_t));
    e->kind      = NAM_KIND_CALLCAP;
    e->fnc_name  = fnc_name;   /* points into PATND_t — stable lifetime        */
    e->fnc_args  = fnc_args;
    e->fnc_nargs = fnc_nargs;
    e->fnc_arg_names   = fnc_arg_names;
    e->fnc_n_arg_names = fnc_n_arg_names;

    if (matched_text && matched_len > 0) {
        char *copy = GC_MALLOC((size_t)matched_len + 1);
        memcpy(copy, matched_text, (size_t)matched_len);
        copy[matched_len] = '\0';
        e->cc_substr = copy;
        e->cc_slen   = matched_len;
    } else {
        e->cc_substr = "";
        e->cc_slen   = 0;
    }

    /* capture fields unused */
    e->varname = NULL;
    e->var_ptr = NULL;
    e->dt      = 0;
    e->substr  = NULL;
    e->slen    = 0;

    nam_append(e);
}

/*---------------------------------------------------------------------------*/
/* NAM_commit — walk oldest→newest, dispatch captures then callcaps in order */
/*---------------------------------------------------------------------------*/

void NAM_commit(int cookie)
{
    if (!nam_stack || nam_depth != cookie + 1) {
        fprintf(stderr, "nmd: NAM_commit cookie mismatch (depth=%d cookie=%d)\n",
                nam_depth, cookie);
        return;
    }

    NamFrame_t *f = nam_stack;

    /* Walk oldest→newest; assign each capture entry, fire each callcap.
     * Last-write-wins for captures: skip if a later entry targets the same var
     * AND no callcap intervenes.  When a callcap lies between two writes to the
     * same variable (ARBNO with per-iteration side-effects), we must write the
     * early value so the callcap reads it correctly. */
    for (NamEntry_t *e = f->head; e; e = e->next) {

        if (e->kind == NAM_KIND_CAPTURE) {
            /* Check if a later entry in the list has the same target        */
            int has_later = 0;
            for (NamEntry_t *f2 = e->next; f2; f2 = f2->next) {
                if (f2->kind == NAM_KIND_CALLCAP) break;   /* callcap intervenes — must write now */
                if (f2->kind != NAM_KIND_CAPTURE) continue;
                if (e->var_ptr && f2->var_ptr == e->var_ptr) { has_later=1; break; }
                if (!e->var_ptr && e->varname && f2->varname &&
                    strcmp(e->varname, f2->varname) == 0)    { has_later=1; break; }
            }
            if (has_later) continue;   /* skip — later write will win        */

            DESCR_t val = { .v = DT_S, .slen = (uint32_t)e->slen,
                            .s = (char *)e->substr };
            if (e->dt == DT_K) {
                if (e->varname) ASGNIC_fn(e->varname, val);
            } else if (e->dt == DT_E) {
                extern DESCR_t EVAL_fn(DESCR_t);
                DESCR_t expr_d = { .v = DT_E, .ptr = e->var_ptr,
                                   .slen = 0, .s = NULL };
                DESCR_t evval = EVAL_fn(expr_d);
                if (!IS_FAIL_fn(evval)) {
                    if (e->varname && e->varname[0])
                        NV_SET_fn(e->varname, evval);
                }
            } else {
                if (e->var_ptr)
                    *e->var_ptr = val;
                else if (e->varname && e->varname[0])
                    NV_SET_fn(e->varname, val);
            }

        } else {
            /* NAM_KIND_CALLCAP — fire the function now */
            if (!g_user_call_hook || !e->fnc_name) continue;

            /* Pass only the explicit static args — do NOT prepend matched text.
             * SNOBOL4 spec: *fn(a,b) in pattern receives exactly the listed args.
             * Prepending the captured substring as args[0] was wrong: it shifted
             * all explicit args by 1, so param[0] got "" (epsilon match) instead
             * of the intended first argument.  CSNOBOL4 confirms no prepend. */
            /* Bug #1c fix: the NAME returned by *fn() is the target cell for
             * the . conditional assignment.  We must write the MATCHED TEXT
             * (e->cc_substr / e->cc_slen — captured in bb_callcap) into that
             * cell, not the DT_N descriptor itself.  Writing name_d was
             * dropping a stale 1-byte fragment (cursor/offset, often \t) into
             * the cell, breaking expr_eval.sno and any (PAT . *fn()) idiom. */
            /* TL-2: if arg *names* are attached, resolve them now via
             * NV_GET_fn.  This is the flush-time lookup — every earlier
             * . capture in the NAM list has already been processed above
             * (oldest→newest walk), so the variable written by e.g.
             * (word . tag) holds the matched text by the time *cb(tag)
             * resolves tag here. */
            DESCR_t *call_args = e->fnc_args;
            int      call_n    = e->fnc_nargs;
            DESCR_t  resolved_buf[8];
            DESCR_t *resolved  = NULL;
            if (e->fnc_arg_names && e->fnc_n_arg_names > 0) {
                extern DESCR_t NV_GET_fn(const char *);
                call_n = e->fnc_n_arg_names;
                resolved = (call_n <= 8) ? resolved_buf
                                         : (DESCR_t *)GC_MALLOC((size_t)call_n * sizeof(DESCR_t));
                for (int k = 0; k < call_n; k++) {
                    resolved[k] = NV_GET_fn(e->fnc_arg_names[k] ? e->fnc_arg_names[k] : "");
                }
                call_args = resolved;
            }
            DESCR_t name_d = g_user_call_hook(e->fnc_name, call_args, call_n);
            DESCR_t *cell = (name_d.v == DT_N && name_d.ptr)
                            ? (DESCR_t *)name_d.ptr : NULL;
            if (cell) {
                DESCR_t val;
                val.v    = DT_S;
                val.slen = (uint32_t)e->cc_slen;
                val.s    = (char *)(e->cc_substr ? e->cc_substr : "");
                *cell = val;
            }
        }
    }

    /* Pop frame */
    nam_stack = f->prev;
    nam_depth--;
}

/*---------------------------------------------------------------------------*/
/* NAM_discard — clear current frame (backtrack / scan-position reset)       */
/*---------------------------------------------------------------------------*/

void NAM_discard(int cookie)
{
    if (!nam_stack) return;
    nam_stack->head = NULL;
    nam_stack->tail = NULL;
}

/*---------------------------------------------------------------------------*/
/* NAM_mark / NAM_rollback_to — intra-frame high-water checkpoint             */
/*                                                                            */
/* Used by pattern combinators that backtrack internally (bb_alt, bb_arbno,   */
/* etc.). NAM_mark() returns an opaque pointer (really the current tail) so   */
/* that NAM_rollback_to(mark) can trim any entries appended since.            */
/* A NULL mark means "frame was empty at the time of marking" — rollback in   */
/* that case empties the frame entirely.                                      */
/*---------------------------------------------------------------------------*/

void *NAM_mark(void)
{
    if (!nam_stack) return NULL;
    return (void *)nam_stack->tail;   /* may be NULL when frame is empty     */
}

void NAM_rollback_to(void *mark)
{
    if (!nam_stack) return;
    NamEntry_t *new_tail = (NamEntry_t *)mark;
    if (new_tail == NULL) {
        /* frame was empty at mark time — clear everything appended since    */
        nam_stack->head = NULL;
        nam_stack->tail = NULL;
        return;
    }
    /* Trim the list after new_tail. GC will reclaim the dangling nodes.     */
    new_tail->next  = NULL;
    nam_stack->tail = new_tail;
}

/*---------------------------------------------------------------------------*/
/* NAM_pop — pop frame after final failure                                    */
/*---------------------------------------------------------------------------*/

void NAM_pop(int cookie)
{
    if (!nam_stack || nam_depth != cookie + 1) return;
    nam_stack = nam_stack->prev;
    nam_depth--;
}
