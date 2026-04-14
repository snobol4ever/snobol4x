/*
 * icn_runtime.h — Icon interpreter runtime API
 *
 * FI-4: declarations for all symbols moved from scrip.c to icn_runtime.c.
 * Include this in scrip.c (and anywhere else that needs Icon runtime access).
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6 (FI-4, 2026-04-14)
 */
#ifndef DRIVER_ICN_RUNTIME_H
#define DRIVER_ICN_RUNTIME_H

#include "../../ir/ir.h"
#include "../../frontend/snobol4/scrip_cc.h"
#include "../../runtime/x86/bb_broker.h"
#include "../../frontend/icon/icon_gen.h"

/*------------------------------------------------------------------------
 * Constants
 *------------------------------------------------------------------------*/
#define ICN_SLOT_MAX        64
#define ICN_PROC_MAX       256
#define ICN_GEN_MAX         16
#define ICN_FRAME_MAX      256
#define ICN_SCAN_STACK_MAX  16
#define ICN_GLOBAL_MAX      64

/*------------------------------------------------------------------------
 * Types
 *------------------------------------------------------------------------*/
typedef struct { const char *name; EXPR_t *proc; } IcnProcEntry;

typedef struct { EXPR_t *node; long cur; const char *sval; } IcnGenEntry_d;

/* IM-10: moved above IcnFrame so IcnFrame.sc can embed IcnScope by value */
typedef struct { const char *name; int slot; } IcnScopeEnt;
typedef struct { IcnScopeEnt e[ICN_SLOT_MAX]; int n; } IcnScope;

typedef struct {
    DESCR_t       env[ICN_SLOT_MAX];
    int           env_n;
    int           returning;
    DESCR_t       return_val;
    IcnGenEntry_d gen[ICN_GEN_MAX];
    int           gen_depth;
    int           loop_break;
    EXPR_t       *body_root;
    /* IM-10: slot→name map, copied from the scope built in icn_call_proc.
     * Allows sync_monitor to name local variables in snapshots. */
    IcnScope      sc;
    /* suspend coroutine state: set by E_SUSPEND, cleared by icn_drive */
    int           suspending;   /* 1 = procedure is suspending a value     */
    DESCR_t       suspend_val;  /* the value being suspended               */
    EXPR_t       *suspend_do;   /* do-clause to run on resumption, or NULL */
} IcnFrame;

/*------------------------------------------------------------------------
 * Globals (defined in icn_runtime.c)
 *------------------------------------------------------------------------*/
extern IcnProcEntry icn_proc_table[ICN_PROC_MAX];
extern int          icn_proc_count;
extern int          g_lang;        /* 0=SNOBOL4 1=Icon */
extern EXPR_t      *g_icn_root;

extern IcnFrame     icn_frame_stack[ICN_FRAME_MAX];
extern int          icn_frame_depth;
#define ICN_CUR (icn_frame_stack[icn_frame_depth - 1])

extern const char  *icn_scan_subj;
extern icn_suspend_state_t *icn_active_ss;
extern int          icn_scan_pos;
typedef struct { const char *subj; int pos; } IcnScanEntry;
extern IcnScanEntry icn_scan_stack[ICN_SCAN_STACK_MAX];
extern int          icn_scan_depth;

extern const char  *icn_global_names[ICN_GLOBAL_MAX];
extern int          icn_global_count;

/*------------------------------------------------------------------------
 * Functions (defined in icn_runtime.c)
 *------------------------------------------------------------------------*/
void    icn_gen_push(EXPR_t *n, long v, const char *sv);
void    icn_gen_pop(void);
int     icn_gen_lookup(EXPR_t *n, long *out);
int     icn_gen_lookup_sv(EXPR_t *n, long *out, const char **sv);
int     icn_gen_active(EXPR_t *n);

int     icn_is_global(const char *name);
void    icn_global_register(const char *name);

int     icn_drive(EXPR_t *e);
int     icn_drive_fnc(EXPR_t *e);   /* suspend-aware driver for user proc generators */

/* Set by icn_drive_fnc while running the every-body with a suspended value.
 * interp_eval(E_FNC) checks this: if the E_FNC node matches icn_drive_node,
 * return icn_drive_val directly instead of calling the procedure again. */
extern EXPR_t  *icn_drive_node;   /* the E_FNC node currently being driven */
extern DESCR_t  icn_drive_val;    /* the suspended value to return          */

int     icn_scope_add(IcnScope *sc, const char *name);
int     icn_scope_get(IcnScope *sc, const char *name);
void    icn_scope_patch(IcnScope *sc, EXPR_t *e);

DESCR_t icn_call_proc(EXPR_t *proc, DESCR_t *args, int nargs);
bb_node_t icn_eval_gen(EXPR_t *e);

#endif /* DRIVER_ICN_RUNTIME_H */
