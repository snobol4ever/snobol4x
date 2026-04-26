/*
 * name_t.h — SN-21: Unified NAME descriptor.
 *
 * SNOBOL4 has exactly one concept for the right-hand side of `.` (conditional
 * assignment) and `$` (immediate assignment): a NAME — a writable storage
 * location.  The unary `.` operator applied to any addressable expression
 * produces DT_N.  `*fn()` via NRETURN produces DT_N.  `A[i,j]` produces DT_N.
 * They are all the same concept.
 *
 * Before SN-21, the runtime fractured this concept across three places:
 *   - `capture_t` in bb_box.h carried { varname, var_ptr } for (.) / ($) plain
 *     variable and DT_N pointer captures.
 *   - `callcap_t` in stmt_exec.c carried { fnc_name, fnc_args, ... } for the
 *     `pat . *fn()` indirect-call case.
 *   - Array-indexed captures `A[i,j]` flowed through ad-hoc code paths.
 *
 * Two separate state machines (`bb_capture` / `bb_callcap`), two separate NAM
 * push kinds (NAM_KIND_CAPTURE / NAM_KIND_CALLCAP), and separate ownership
 * rules.  This was the root of the NAM rollback bugs across sessions 15–18,
 * and the surface at which the DT_E-thaw and Porter-stemmer gaps live.
 *
 * SN-21 replaces all three with ONE descriptor — NAME_t — used everywhere a
 * match location is named.  `name_commit_value(&name, value)` dispatches on
 * `kind` and does the single correct thing for that kind.
 *
 * Migration complete at SN-21e (this commit): bb_capture / bb_callcap /
 * capture_t / callcap_t / NAM_KIND_* are deleted; every caller goes through
 * bb_cap + NAME_t.  The DT_E thaw is folded into name_commit_value as a
 * one-line idempotent prelude, closing the SN-20 *var-holds-DT_E gap.
 */

#ifndef NAME_T_H
#define NAME_T_H

#include "descr.h"

/*---------------------------------------------------------------------------*/
/* NameKind_t — what kind of writable location this NAME_t names             */
/*---------------------------------------------------------------------------*/

typedef enum {
    NM_VAR  = 0,   /* plain named variable — write via NV_SET_fn               */
    NM_PTR  = 1,   /* DT_N interior pointer — *var_ptr = value                 */
    NM_IDX  = 2,   /* A[i,j] — array element; idx_expr evaluated at write      */
    NM_CALL = 3    /* *fn() NRETURN — call function; returned DT_N is target   */
} NameKind_t;

/*---------------------------------------------------------------------------*/
/* NAME_t — a single unified lvalue descriptor                                */
/*                                                                            */
/* Field interpretation by kind:                                              */
/*                                                                            */
/*   NM_VAR   — var_name holds the target identifier.  All other fields       */
/*              ignored.  write → NV_SET_fn(var_name, value).                 */
/*                                                                            */
/*   NM_PTR   — var_ptr holds a direct DESCR_t* (raw cell, e.g. table slot,   */
/*              array slot, outer stack frame NAME).  All other fields        */
/*              ignored.  write → *var_ptr = value.                           */
/*                                                                            */
/*   NM_IDX   — idx_expr holds an expression tree for `A[i,j]`; at write      */
/*              time we evaluate the expression (resolving a DT_N cell) and   */
/*              store value there.  NM_IDX is not used in SN-21a/b; reserved  */
/*              so migration in later steps does not need a header change.    */
/*                                                                            */
/*   NM_CALL  — fnc_name + fnc_args[fnc_nargs] (+ optional fnc_arg_names for  */
/*              delayed lookup) describe an indirect call.  write →           */
/*              invoke fn, obtain DT_N result, *cell_of(result) = value.      */
/*              Captures the substring text so the write source is correct    */
/*              when firing at commit time.                                   */
/*---------------------------------------------------------------------------*/

typedef struct NAME_s {
    NameKind_t   kind;

    /* NM_VAR */
    const char  *var_name;

    /* NM_PTR */
    DESCR_t     *var_ptr;

    /* NM_IDX — reserved, not used before SN-21c+ */
    void        *idx_expr;

    /* NM_CALL */
    const char  *fnc_name;
    DESCR_t     *fnc_args;
    int          fnc_nargs;
    char       **fnc_arg_names;
    int          fnc_n_arg_names;
} NAME_t;

/*---------------------------------------------------------------------------*/
/* name_commit_value — commit a value into the location described by *nm.    */
/*                                                                            */
/* The single dispatch point used by bb_cap γ (immediate, $) and by           */
/* NAME_commit (deferred, .).  Every kind has exactly one correct              */
/* implementation, and it lives here.                                         */
/*                                                                            */
/* value must already be the resolved DESCR_t to store — e.g. a DT_S slice    */
/* of the subject; for DT_E thaw, callers should evaluate before calling.     */
/*---------------------------------------------------------------------------*/

int  name_commit_value(const NAME_t *nm, DESCR_t value); /* 0=ok -1=fail (NM_CALL FRETURN) */

/*---------------------------------------------------------------------------*/
/* name_init_as_var / name_init_as_ptr / name_init_as_call                   */
/*                                                                            */
/* Zero the struct then set the kind-specific fields.  Callers can also      */
/* build NAME_t literals directly; these helpers just make call sites short. */
/*---------------------------------------------------------------------------*/

void name_init_as_var (NAME_t *nm, const char *var_name);
void name_init_as_ptr (NAME_t *nm, DESCR_t *var_ptr);
void name_init_as_call(NAME_t *nm,
                       const char *fnc_name,
                       DESCR_t *fnc_args, int fnc_nargs,
                       char **fnc_arg_names, int fnc_n_arg_names);

/*---------------------------------------------------------------------------*/
/* Flat NAME stack — primary API (SN-21b).                                    */
/*                                                                            */
/* Two primary ops match the Python generator `push; yield; pop` idiom:      */
/* every capture box's γ calls NAME_push before γ-return; every β and ω     */
/* call NAME_pop.  The stack rolls and unrolls itself through the γ/β/ω     */
/* cascade — no separate commit/discard bureaucracy.                         */
/*                                                                            */
/* SN-23h collapsed pop to a single handle-free form matching the SIL's     */
/* DNME ("decrement NAMICL").  NAME_push returns an opaque handle for       */
/* backward source compatibility but every caller discards it — the box    */
/* self-unwind invariant (SN-22d) guarantees pops are always LIFO.          */
/*                                                                            */
/* SN-23e removed NAME_top / NAME_pop_above / NAME_save / NAME_discard.     */
/*---------------------------------------------------------------------------*/

void *NAME_push      (const NAME_t *nm, const char *substr, int slen);
void  NAME_pop       (void);             /* SN-23h: drop topmost live slot  */

#endif /* NAME_T_H */
