/*
 * snobol4_nmd.c — SIL Naming List (§NMD)
 *
 * ARCHITECTURE (SN-23a — per-context NAM stack layer):
 *
 *   The NAM stack is now owned by a NAME_ctx_t, not by a file-scope global.
 *   A static root ctx (g_root_ctx) wraps the legacy g_stack/g_cap/g_top
 *   globals; g_ctx_current points at the active ctx.  All ops route through
 *   g_ctx_current, so behavior is identical to the flat-stack era as long
 *   as no caller enters a child ctx.
 *
 *     ctx.entries: [ 0 | 1 | 2 | ... | top-1 ]    grows right
 *
 *   Two primary operations drive all pattern-time capture bookkeeping:
 *
 *     NAME_push(nm, substr, slen)  — append slot; return handle (index).
 *     NAME_pop(handle)              — drop that slot (LIFO from top).
 *
 *   Two bracket operations frame statement-match and EVAL contexts (legacy
 *   API, unchanged in SN-23a):
 *
 *     NAME_save()                   — snapshot current top (returns cookie).
 *     NAME_commit(cookie)           — walk stack[cookie..top), fire each
 *                                     through name_commit_value; top = cookie.
 *     NAME_discard(cookie)          — top = cookie (drop, don't fire).
 *
 *   SN-23a adds two ctx brackets (dormant until callers adopt them, landing
 *   in SN-23b..c):
 *
 *     NAME_ctx_enter(ctx)           — make ctx the active stack; parent saved.
 *     NAME_ctx_leave()              — restore parent; ctx's entries dropped.
 *
 * EVERY box γ push has a matching β/ω pop — boxes own and self-unwind
 * their own slots.  There is never "leftover" pushed state once a box
 * has backtracked or failed out.  Commit at the statement-match bracket
 * walks entries in push order (oldest → newest) to preserve the
 * capture-before-callcap semantics fixed in SC-26.
 *
 * LEGACY SHIMS:
 *
 *   NAME_save / NAME_commit / NAME_discard / NAME_push_callcap* /
 *   NAME_top / NAME_pop_above remain as thin wrappers around the core
 *   NAME_push / NAME_pop protocol.  SN-22a+b removed the last in-box
 *   callers of NAME_mark / NAME_rollback_to (bb_alt, bb_arbno); those
 *   two shims were deleted in SN-22c.  SN-23 will collapse the bracket
 *   API onto NAME_ctx_enter/leave across SN-23b..e.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Opus 4.7
 * DATE:    2026-04-19
 * SPRINT:  SN-23a
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
/* NAME_top — current stack depth                                             */
/*===========================================================================*/

int NAME_top(void)
{
    return g_ctx_current->top;
}

/*===========================================================================*/
/* NAME_pop_above — bulk drop slots [saved_top..top) without firing           */
/*                                                                            */
/* Used internally by NAME_commit / NAME_discard.  The in-box callers       */
/* (bb_alt's next-arm switch, bb_arbno's body-ω / zero-advance escapes)     */
/* were removed in SN-22a+b once per-box self-unwind was complete.          */
/*===========================================================================*/

void NAME_pop_above(int saved_top)
{
    NAME_ctx_t *ctx = g_ctx_current;
    if (saved_top < 0) saved_top = 0;
    if (saved_top > ctx->top) saved_top = ctx->top;
    ctx->top = saved_top;
}

/*===========================================================================*/
/* NOTE — statement/EVAL commit walks live at the call sites.  stmt_exec.c  */
/* on match success walks stack[saved..top) through name_commit_value,       */
/* then NAME_pop_above(saved).  On failure it just NAME_pop_above(saved).   */
/* EVAL always NAME_pop_above(saved) — captures inside an EVAL'd expression */
/* are local to that expression and never propagate out.                     */
/*===========================================================================*/

/*===========================================================================*/
/* ─── LEGACY SHIMS (reduced in SN-22c; to be collapsed in SN-23b..e) ────── */
/*===========================================================================*/

/* NAME_save — "push frame" becomes "snapshot current top". */
int NAME_save(void)
{
    return NAME_top();
}

/* NAME_push_callcap / NAME_push_callcap_named — NM_CALL entry. */
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

/* NAME_commit — walk slots [cookie..top), fire DT_S entries with
 * last-write-wins dedup (stop at intervening NM_CALL), then drop the
 * range via NAME_pop_above.  Operates on the active ctx. */
void NAME_commit(int cookie)
{
    NAME_ctx_t *ctx = g_ctx_current;
    int mark = cookie;
    if (mark < 0) mark = 0;
    if (mark > ctx->top) mark = ctx->top;

    NAME_entry_t *es = ctx_entries(ctx);

    /* Unified commit with last-write-wins dedup.  Walk in push order
     * (oldest → newest) so (.) captures preceding (. *fn()) callcaps
     * fire first, matching SC-26 semantics. */
    for (int i = mark; i < ctx->top; i++) {
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

    NAME_pop_above(mark);
}

/* NAME_discard — drop entries [cookie..top). */
void NAME_discard(int cookie)
{
    NAME_pop_above(cookie);
}
