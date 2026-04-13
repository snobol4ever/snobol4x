/*============================================================================================================================
 * descr.h — Minimal DESCR_t definition for the universal Byrd box value type
 *
 * Extracted from snobol4.h to break the circular include dependency:
 *   bb_box.h needs DESCR_t (for bb_box_fn return type)
 *   snobol4.h needs bb_box.h (for bb_node_t in PATND_t)
 *
 * This header has NO GC, NO snobol4 runtime dependencies — just stdint + DESCR_t.
 * snobol4.h includes this header and adds all the rest (GC, inline helpers, macros).
 * bb_box.h includes this header to get DESCR_t for bb_box_fn.
 *
 * U-5 (GOAL-UNIFIED-BROKER): added to resolve circular include when bb_box_fn
 * return type changed from spec_t to DESCR_t.
 *============================================================================================================================*/

#ifndef DESCR_H
#define DESCR_H

#include <stdint.h>
#include <stddef.h>

/*------------------------------------------------------------------------------------------------------------------------------
 * DTYPE_t — value type tag
 *----------------------------------------------------------------------------------------------------------------------------*/
typedef enum {
    DT_SNUL =  0,   /* null sentinel — empty/unset              */
    DT_S    =  1,   /* STRING  — char*                          */
    DT_P    =  3,   /* PATTERN — PATND_t*                       */
    DT_A    =  4,   /* ARRAY   — ARBLK_t*                       */
    DT_T    =  5,   /* TABLE   — TBBLK_t*                       */
    DT_I    =  6,   /* INTEGER — int64_t                        */
    DT_R    =  7,   /* REAL    — double                         */
    DT_C    =  8,   /* CODE    — compiled code block            */
    DT_N    =  9,   /* NAME    — l-value reference              */
    DT_K    = 10,   /* KEYWORD — protected variable             */
    DT_E    = 11,   /* EXPRESSION — unevaluated                 */
    DT_FAIL = 99,   /* failure sentinel (our invention)         */
    DT_DATA = 100,  /* first user-defined DT_DATA type          */
} DTYPE_t;

/*------------------------------------------------------------------------------------------------------------------------------
 * DESCR_t — the universal 16-byte value descriptor (same ABI as spec_t)
 * Forward-declares pointer types; full definitions in snobol4.h / snobol4_patnd.h.
 *----------------------------------------------------------------------------------------------------------------------------*/
struct _PATND_t;
struct _ARBLK_t;
struct _TBBLK_t;
struct _DATINST_t;

typedef struct DESCR_t {
    DTYPE_t  v;      /* type tag                             */
    uint32_t slen;   /* string byte length (0 = use strlen)  */
    union {
        char              *s;   /* DT_S   — string pointer          */
        int64_t            i;   /* DT_I   — integer                 */
        double             r;   /* DT_R   — real                    */
        struct _PATND_t   *p;   /* DT_P   — pattern node            */
        struct _ARBLK_t   *arr; /* DT_A   — array block             */
        struct _TBBLK_t   *tbl; /* DT_T   — table block             */
        struct _DATINST_t *u;   /* DT_DATA — user data instance     */
        void              *ptr; /* generic pointer                  */
    };
} DESCR_t;

/*------------------------------------------------------------------------------------------------------------------------------
 * FAILDESCR / IS_FAIL_fn — failure sentinel (mirrors spec_empty)
 *----------------------------------------------------------------------------------------------------------------------------*/
#define FAILDESCR    ((DESCR_t){ .v = DT_FAIL, .i = 0 })
static inline int IS_FAIL_fn(DESCR_t v) { return v.v == DT_FAIL; }

#endif /* DESCR_H */
