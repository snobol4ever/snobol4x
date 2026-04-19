/*
 * snobol4_nmd.c — SIL Naming List (§NMD)
 *
 * ARCHITECTURE (SN-23e — collapsed API: push + pop + commit + ctx-brackets):
 *
 *   The NAM stack is owned by a NAME_ctx_t, not by a file-scope global.
 *   A static root ctx (g_root_ctx) wraps the original entries array and
 *   top counter; g_ctx_current points at the active ctx.  All ops route
 *   through g_ctx_current.  Nested matches (EVAL-inside-match, recursive
 *   patterns with their own scan frame) each create a child ctx via
 *   NAME_ctx_enter, so inner captures can never leak into the outer.
 *
 *     ctx.entries: [ 0 | 1 | 2 | ... | top-1 ]    grows right
 *
 *   Complete public API — five ops, period:
 *
 *     NAME_push(nm, substr, slen)   — append slot; return handle (index).
 *                                     Every box γ calls this on match.
 *     NAME_pop(handle)              — drop that specific slot (LIFO by index).
 *     NAME_pop_top()                — drop whatever's on top (handle-free; used
 *                                     by bb_cap where the push handle is not
 *                                     threaded through to β/ω).
 *     NAME_commit()                 — walk the active ctx oldest→newest,
 *                                     fire each live slot via
 *                                     name_commit_value, then reset top=0.
 *                                     Called once, on outer match success.
 *     NAME_ctx_enter(ctx) / NAME_ctx_leave()
 *                                   — bracket a nested scan / EVAL frame.
 *
 * EVERY box γ push has a matching β/ω pop — boxes own and self-unwind
 * their own slots.  There is never "leftover" pushed state once a box
 * has backtracked or failed out.  NAME_commit walks entries in push
 * order (oldest → newest) to preserve the capture-before-callcap
 * semantics fixed in SC-26.
 *
 * HISTORY:
 *
 *   SN-20   — box-owned self-unwind (NAME_pop on β/ω).
 *   SN-21   — unified NAME_t + flat NAM stack, one push/pop API.
 *   SN-22a+b — removed NAME_mark / NAME_rollback_to call sites from
 *              bb_alt and bb_arbno; their definitions deleted in SN-22c.
 *   SN-23a   — introduced NAME_ctx_t and ctx-enter/leave.
 *   SN-23b+c — rewired stmt_exec Phase 3 and EVAL frame to ctx brackets;
 *              NAME_save/NAME_discard no longer live-used.
 *   SN-23d   — bb_cap dropped nam_handle; NAME_pop_top() introduced.
 *   SN-23e   — deleted NAME_save, NAME_discard, NAME_top, NAME_pop_above.
 *              NAME_commit dropped its cookie argument (always 0 under
 *              ctx nesting).  API surface: 9 entries → 5.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Opus 4.7
 * DATE:    2026-04-19
 * SPRINT:  SN-23e
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <gc/gc.h>

#include "snobol4.h"
#include "sil_macros.h"
#include "name_t.h"

/*===========================================================================*/
/* NAME_entry_t — one slot in the flat stack                                  */
/*===========================================================================*/

typedef struct {
    int         live;        /* 1 = occupied, 0 = tombstone                     */
    NAME_t      name;        /* full unified lvalue descriptor                  */
    const char *substr;      /* GC_malloc'd captured text (epsilon → "")        */
    int         slen;
} NAME_entry_t;

/*===========================================================================*/
/* NAME_ctx_t — per-context NAM stack (SN-23a)                                */
/*                                                                            */
/* The struct is defined in snobol4.h (as an exposed POD with an opaque      */
/* `void *entries` field) so callers can stack-allocate without pulling      */
/* in NAME_entry_t.  Internally we just cast `entries` to NAME_entry_t*     */
/* at use.  The root ctx (g_root_ctx) is statically allocated and starts    */
/* active; NAME_ctx_enter/leave nest child ctxs on top.                     */
/*===========================================================================*/

/* The root ctx owns the legacy global stack.  It has no parent and is     */
/* never popped — NAME_ctx_leave on the root is a safe no-op.              */
static NAME_ctx_t  g_root_ctx  = { NULL, 0, 0, NULL };
static NAME_ctx_t *g_ctx_current = &g_root_ctx;

/* Internal accessor: cast the ctx's opaque entries to the slot type. */
static inline NAME_entry_t *ctx_entries(NAME_ctx_t *ctx)
{
    return (NAME_entry_t *)ctx->entries;
}

/*===========================================================================*/
/* Internal helpers — all ops route through the active ctx                    */
/*===========================================================================*/

static void ctx_ensure_capacity(NAME_ctx_t *ctx, int need)
{
    if (need <= ctx->cap) return;
    int newcap = ctx->cap ? ctx->cap * 2 : 64;
    while (newcap < need) newcap *= 2;
    NAME_entry_t *fresh = (NAME_entry_t *)GC_MALLOC((size_t)newcap * sizeof(NAME_entry_t));
    NAME_entry_t *old   = ctx_entries(ctx);
    if (old && ctx->top > 0)
        memcpy(fresh, old, (size_t)ctx->top * sizeof(NAME_entry_t));
    ctx->entries = fresh;
    ctx->cap     = newcap;
}

static const char *dup_substr(const char *s, int len)
{
    if (!s || len <= 0) return "";
    char *copy = (char *)GC_MALLOC((size_t)len + 1);
    memcpy(copy, s, (size_t)len);
    copy[len] = '\0';
    return copy;
}

/* Handle encoding: slot-index + 1, so NULL ≠ slot 0.  Handles are scoped to
 * the ctx that was active at push time; popping a handle from a different
 * ctx is undefined — but in practice every box γ pushes and its own β/ω
 * pops within the same dynamic extent, so the active ctx matches. */
static inline void *idx_to_handle(int i) { return (void *)(intptr_t)(i + 1); }
static inline int   handle_to_idx(void *h) { return h ? (int)(intptr_t)h - 1 : -1; }

/*===========================================================================*/
/* NAME_ctx_enter / NAME_ctx_leave (SN-23a)                                   */
/*                                                                            */
/* Push ctx onto the ctx chain, making it the active stack.  Callers pass   */
/* a zero-initialized NAME_ctx_t (stack-allocated or static); the struct    */
/* is populated in place.  Leaving drops whatever entries the ctx           */
/* accumulated — it's the caller's responsibility to have already committed */
/* or discarded anything meaningful before leaving.                          */
/*===========================================================================*/

void NAME_ctx_enter(NAME_ctx_t *ctx)
{
    if (!ctx) return;
    ctx->entries = NULL;
    ctx->cap     = 0;
    ctx->top     = 0;
    ctx->parent  = g_ctx_current;
    g_ctx_current = ctx;
}

void NAME_ctx_leave(void)
{
    NAME_ctx_t *ctx = g_ctx_current;
    if (!ctx || ctx == &g_root_ctx) return;   /* never pop the root */
    g_ctx_current = ctx->parent;
    /* Caller owns the ctx storage; we just unlink.  GC reclaims entries. */
    ctx->entries = NULL;
    ctx->cap     = 0;
    ctx->top     = 0;
    ctx->parent  = NULL;
}

/*===========================================================================*/
/* NAME_push — primary push                                                   */
/*===========================================================================*/

void *NAME_push(const NAME_t *nm, const char *substr, int slen)
{
    if (!nm) return NULL;

    NAME_ctx_t *ctx = g_ctx_current;
    ctx_ensure_capacity(ctx, ctx->top + 1);

    int idx = ctx->top;
    NAME_entry_t *e = &ctx_entries(ctx)[idx];
    e->live      = 1;
    e->name      = *nm;
    e->substr    = dup_substr(substr, slen);
    e->slen      = (slen > 0) ? slen : 0;

    ctx->top++;
    return idx_to_handle(idx);
}

/*===========================================================================*/
/* NAME_pop — primary pop                                                     */
/*                                                                            */
/* Drop the slot.  LIFO case (handle is top-1) is O(1).  Non-LIFO pops mark  */
/* a tombstone; trailing tombstones are collapsed on every pop so top       */
/* always refers to a live slot.                                              */
/*===========================================================================*/

void NAME_pop(void *handle)
{
    NAME_ctx_t *ctx = g_ctx_current;
    int idx = handle_to_idx(handle);
    if (idx < 0 || idx >= ctx->top) return;

    NAME_entry_t *es = ctx_entries(ctx);
    NAME_entry_t *e  = &es[idx];
    if (!e->live) return;
    e->live = 0;

    while (ctx->top > 0 && !es[ctx->top - 1].live) ctx->top--;
}

/*===========================================================================*/
/* NAME_pop_top — SN-23d: drop topmost live slot of the active ctx            */
/*                                                                            */
/* Pure LIFO pop.  Every bb_cap γ pushes; its own β/ω pops the top.  The      */
/* box self-unwind invariant (SN-22d) guarantees that when a box reaches     */
/* β/ω, every peer/child γ-push made after its own γ has already been popped */
/* by its owning box — so the top IS this box's own push.  No handle needed. */
/*===========================================================================*/

void NAME_pop_top(void)
{
    NAME_ctx_t *ctx = g_ctx_current;
    if (ctx->top <= 0) return;
    NAME_entry_t *es = ctx_entries(ctx);
    /* Skip any tombstones at the top (defensive — post-SN-22d there should */
    /* be none, but keep the invariant consistent with NAME_pop's trailer). */
    while (ctx->top > 0 && !es[ctx->top - 1].live) ctx->top--;
    if (ctx->top <= 0) return;
    es[ctx->top - 1].live = 0;
    ctx->top--;
}

/*===========================================================================*/
/* NAME_push_callcap / NAME_push_callcap_named — NM_CALL entry.               */
/*                                                                            */
/* Convenience wrappers around NAME_push for the pat . *fn() XCALLCAP case.  */
/* Slated for deletion in SN-23f — zero live callers remain (bb_cap builds    */
/* the NAME_t and calls NAME_push directly).                                  */
/*===========================================================================*/

void *NAME_push_callcap(const char *fnc_name, DESCR_t *fnc_args, int fnc_nargs,
                       const char *matched_text, int matched_len)
{
    return NAME_push_callcap_named(fnc_name, fnc_args, fnc_nargs,
                                   NULL, 0, matched_text, matched_len);
}

void *NAME_push_callcap_named(const char *fnc_name,
                              DESCR_t *fnc_args, int fnc_nargs,
                              char **fnc_arg_names, int fnc_n_arg_names,
                              const char *matched_text, int matched_len)
{
    NAME_t nm;
    name_init_as_call(&nm, fnc_name, fnc_args, fnc_nargs,
                      fnc_arg_names, fnc_n_arg_names);
    return NAME_push(&nm, matched_text, matched_len);
}

/*===========================================================================*/
/* NAME_commit — walk the active ctx, fire each live slot, clear the stack.   */
/*                                                                            */
/* DT_S entries are fired with last-write-wins dedup (scan forward until an   */
/* NM_CALL barrier; if a later slot targets the same variable, skip this one).*/
/* Walk is oldest → newest to preserve capture-before-callcap ordering        */
/* (SC-26 invariant: (.) captures like tag/wrd must be assigned before the    */
/* (. *fn()) callcaps like push_list/push_item that read them).               */
/*===========================================================================*/

/* same_var_target — last-write-wins dedup helper used inline below. */
static int same_var_target(const NAME_entry_t *a, const NAME_entry_t *b)
{
    if (a->name.kind != b->name.kind) return 0;
    if (a->name.kind == NM_PTR)
        return a->name.var_ptr && a->name.var_ptr == b->name.var_ptr;
    if (a->name.kind == NM_VAR)
        return a->name.var_name && b->name.var_name
               && strcmp(a->name.var_name, b->name.var_name) == 0;
    return 0;
}

void NAME_commit(void)
{
    NAME_ctx_t *ctx = g_ctx_current;
    NAME_entry_t *es = ctx_entries(ctx);

    for (int i = 0; i < ctx->top; i++) {
        NAME_entry_t *e = &es[i];
        if (!e->live) continue;

        if (e->name.kind == NM_VAR || e->name.kind == NM_PTR) {
            int superseded = 0;
            for (int j = i + 1; j < ctx->top; j++) {
                NAME_entry_t *f = &es[j];
                if (!f->live) continue;
                if (f->name.kind == NM_CALL) break;
                if (same_var_target(e, f)) { superseded = 1; break; }
            }
            if (superseded) continue;
        }

        DESCR_t val = { .v = DT_S, .slen = (uint32_t)e->slen,
                        .s = (char *)e->substr };
        name_commit_value(&e->name, val);
    }

    ctx->top = 0;
}
