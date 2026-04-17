#ifndef PATND_H
#define PATND_H
/* patnd.h — canonical PATND_t / XKIND_t definitions.
 * Single source of truth for the pattern node tree.
 * Included by snobol4.h after DESCR_t is defined. Do NOT include directly —
 * always include snobol4.h which pulls this in at the right point.
 */

#include <stdint.h>

typedef enum {
    XCHR,          /* literal string                          */
    XSPNC,         /* SPAN(chars)                             */
    XBRKC,         /* BREAK(chars)                            */
    XANYC,         /* ANY(chars)                              */
    XNNYC,         /* NOTANY(chars)                           */
    XLNTH,         /* LEN(n)                                  */
    XPOSI,         /* POS(n)                                  */
    XRPSI,         /* RPOS(n)                                 */
    XTB,           /* TAB(n)                                  */
    XRTB,          /* RTAB(n)                                 */
    XFARB,         /* ARB                                     */
    XARBN,         /* ARBNO(p)                                */
    XSTAR,         /* REM                                     */
    XFNCE,         /* FENCE or FENCE(p)                       */
    XFAIL,         /* FAIL                                    */
    XABRT,         /* ABORT                                   */
    XSUCF,         /* SUCCEED                                 */
    XBAL,          /* BAL                                     */
    XEPS,          /* epsilon — always succeeds, no advance   */
    XCAT,          /* concatenation: left right               */
    XOR,           /* alternation:   left | right             */
    XDSAR,         /* deferred var ref: *name                 */
    XFNME,         /* immediate capture: pat $ var            */
    XNME,          /* conditional capture: pat . var          */
    XCALLCAP,      /* conditional capture: pat . *func()      */
    XVAR,          /* variable holding a pattern              */
    XATP,          /* user-defined pattern function call      */
    XBRKX,         /* BREAKX(chars)                           */
} XKIND_t;

struct _PATND_t;
typedef struct _PATND_t PATND_t;

/* NOTE: DESCR_t must be defined before this point (via snobol4.h). */
struct _PATND_t {
    XKIND_t      kind;
    int          materialising;  /* cycle-detection flag */
    const char  *STRVAL_fn;      /* XCHR/XSPNC/XBRKC/XANYC/XNNYC/XDSAR/XATP */
    int64_t      num;            /* XLNTH/XPOSI/XRPSI/XTB/XRTB */
    /* DYN-64: n-ary children replace binary left/right.
     * XCAT/XOR: 2+ children (flat, no spine-chaining needed).
     * XARBN/XFNCE/XFNME/XNME: exactly 1 child (children[0]).
     * Leaf nodes (XCHR, XLNTH, XPOSI, ...): nchildren=0. */
    PATND_t    **children;       /* heap-allocated array, NULL if nchildren==0 */
    int          nchildren;
    DESCR_t      var;            /* XFNME/XNME capture target; XVAR value */
    DESCR_t     *args;           /* XATP args */
    int          nargs;          /* XATP nargs */
    /* TL-2: for XCALLCAP *fn(var) captures, arg *names* captured at pattern-build
     * time, to be resolved at flush time (NAM_commit / CC_γ_core immediate branch)
     * after in-order prior . captures have written the variable.  Only populated
     * when every arg of the *fn() call is a plain E_VAR.  If arg_names==NULL the
     * args/nargs fields above carry pre-evaluated DESCR_t snapshots (legacy path).
     */
    char       **arg_names;      /* XCALLCAP: GC-allocated array of arg var names */
    int          n_arg_names;    /* XCALLCAP: count of arg_names; 0 means unused  */
};

/* Convenience: single-child access for XARBN/XFNCE/XFNME/XNME */
#define PATND_CHILD0(p)  ((p)->children ? (p)->children[0] : NULL)

#include <stdio.h>
void patnd_print(const PATND_t *p, FILE *out);  /* diagnostic: --dump-bb */

#endif /* PATND_H */
