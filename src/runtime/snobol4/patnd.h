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
    PATND_t     *left;           /* XCAT/XOR/XARBN/XFNCE/XFNME/XNME */
    PATND_t     *right;          /* XCAT/XOR */
    DESCR_t      var;            /* XFNME/XNME capture target; XVAR value */
    DESCR_t     *args;           /* XATP args */
    int          nargs;          /* XATP nargs */
};

#endif /* PATND_H */
