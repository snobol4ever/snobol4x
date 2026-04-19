/*
 * snobol4_nmd.c — SIL Naming List (§NMD)  SN-21b: flat NAME_t[] stack
 *
 * ARCHITECTURE:
 *
 *   One flat array of NAME_entry_t (a NAME_t + the captured substring),
 *   grown on demand.  A single int `g_top` tracks the high-water slot.
 *
 *     stack: [ 0 | 1 | 2 | ... | top-1 ]    grows right
 *
 *   Two primary operations drive all pattern-time capture bookkeeping:
 *
 *     NAME_push(nm, substr, slen)  — append slot; return handle (index).
 *     NAME_pop(handle)              — drop that slot (LIFO from top).
 *
 *   Two bracket operations frame statement-match and EVAL contexts:
 *
 *     NAME_save()                   — snapshot current top (returns cookie).
 *     NAME_commit(cookie)           — walk stack[cookie..top), fire each
 *                                     through name_commit_value; top = cookie.
 *     NAME_discard(cookie)          — top = cookie (drop, don't fire).
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
 *   two shims were deleted in SN-22c.  SN-22 followups may further
 *   reduce this surface — see GOAL-LANG-SNOBOL4.md SN-22c/d.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Opus 4.7
 * DATE:    2026-04-19
 * SPRINT:  SN-21b
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
/* The stack                                                                  */
/*===========================================================================*/

static NAME_entry_t *g_stack = NULL;
static int          g_cap   = 0;
static int          g_top   = 0;

static void ensure_capacity(int need)
{
    if (need <= g_cap) return;
    int newcap = g_cap ? g_cap * 2 : 64;
    while (newcap < need) newcap *= 2;
    NAME_entry_t *fresh = (NAME_entry_t *)GC_MALLOC((size_t)newcap * sizeof(NAME_entry_t));
    if (g_stack && g_top > 0)
        memcpy(fresh, g_stack, (size_t)g_top * sizeof(NAME_entry_t));
    g_stack = fresh;
    g_cap   = newcap;
}

static const char *dup_substr(const char *s, int len)
{
    if (!s || len <= 0) return "";
    char *copy = (char *)GC_MALLOC((size_t)len + 1);
    memcpy(copy, s, (size_t)len);
    copy[len] = '\0';
    return copy;
}

/* Handle encoding: slot-index + 1, so NULL ≠ slot 0. */
static inline void *idx_to_handle(int i) { return (void *)(intptr_t)(i + 1); }
static inline int   handle_to_idx(void *h) { return h ? (int)(intptr_t)h - 1 : -1; }

/*===========================================================================*/
/* NAME_push — primary push                                                   */
/*===========================================================================*/

void *NAME_push(const NAME_t *nm, const char *substr, int slen)
{
    if (!nm) return NULL;

    ensure_capacity(g_top + 1);

    int idx = g_top;
    NAME_entry_t *e = &g_stack[idx];
    e->live      = 1;
    e->name      = *nm;
    e->substr    = dup_substr(substr, slen);
    e->slen      = (slen > 0) ? slen : 0;

    g_top++;
    return idx_to_handle(idx);
}

/*===========================================================================*/
/* NAME_pop — primary pop                                                     */
/*                                                                            */
/* Drop the slot.  LIFO case (handle is top-1) is O(1).  Non-LIFO pops mark  */
/* a tombstone; trailing tombstones are collapsed on every pop so g_top      */
/* always refers to a live slot.                                              */
/*===========================================================================*/

void NAME_pop(void *handle)
{
    int idx = handle_to_idx(handle);
    if (idx < 0 || idx >= g_top) return;

    NAME_entry_t *e = &g_stack[idx];
    if (!e->live) return;
    e->live = 0;

    while (g_top > 0 && !g_stack[g_top - 1].live) g_top--;
}

/*===========================================================================*/
/* NAME_top — current stack depth                                             */
/*===========================================================================*/

int NAME_top(void)
{
    return g_top;
}

/*===========================================================================*/
/* NAME_pop_above — bulk drop slots [saved_top..top) without firing           */
/*                                                                            */
/* Used by bb_alt's next-arm switch, bb_arbno's body-ω / zero-advance        */
/* escapes, and statement/EVAL failure paths — anywhere a γ-succeeded        */
/* child was abandoned without β-asking it to pop.                           */
/*===========================================================================*/

void NAME_pop_above(int saved_top)
{
    if (saved_top < 0) saved_top = 0;
    if (saved_top > g_top) saved_top = g_top;
    g_top = saved_top;
}

/*===========================================================================*/
/* NOTE — statement/EVAL commit walks live at the call sites.  stmt_exec.c  */
/* on match success walks stack[saved..top) through name_commit_value,       */
/* then NAME_pop_above(saved).  On failure it just NAME_pop_above(saved).   */
/* EVAL always NAME_pop_above(saved) — captures inside an EVAL'd expression */
/* are local to that expression and never propagate out.                     */
/*===========================================================================*/

/*===========================================================================*/
/* ─── LEGACY SHIMS (deleted in SN-21e) ──────────────────────────────────── */
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
 * range via NAME_pop_above. */
void NAME_commit(int cookie)
{
    int mark = cookie;
    if (mark < 0) mark = 0;
    if (mark > g_top) mark = g_top;

    /* Unified commit with last-write-wins dedup.  Walk in push order
     * (oldest → newest) so (.) captures preceding (. *fn()) callcaps
     * fire first, matching SC-26 semantics. */
    for (int i = mark; i < g_top; i++) {
        NAME_entry_t *e = &g_stack[i];
        if (!e->live) continue;

        if (e->name.kind == NM_VAR || e->name.kind == NM_PTR) {
            int superseded = 0;
            for (int j = i + 1; j < g_top; j++) {
                NAME_entry_t *f = &g_stack[j];
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
