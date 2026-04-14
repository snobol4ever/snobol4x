/*===================================================== spitbol_shim.c (IM-14)
 * spitbol_shim.c — replaces osint/syspl.c when linking SPITBOL in-process.
 *
 * Two responsibilities:
 *   1. zyspl()         — per-statement hook (fires before every statement).
 *                        With -DENGINE=1 the production syspl.c already strips
 *                        all interactive polling; we replace the whole function
 *                        so our hook fires unconditionally.
 *   2. spl_nv_snapshot() — walk SPITBOL's vrblk hash table and return an
 *                        NvPair[] in the same format as one4all's nv_snapshot().
 *                        This is what IM-15 will plug into ExecSnapshot.
 *
 * Build: compiled as part of scrip via -I$X64/osint (for port.h / osint.h /
 *        spitblks.h). syspl.c must be EXCLUDED from libspitbol.a when linking
 *        with this shim (or link this object before the archive so it wins).
 *
 * Gate (IM-14): compiles cleanly against x64/osint headers;
 *               nm shows zyspl and spl_nv_snapshot.
 *==========================================================================*/

#include "port.h"   /* pulls in osint.h, spitblks.h, globals.h, sproto.h     */
                    /* provides: NORMAL_RETURN, SET_WA, word, TYPE_ICL/SCL/RCL */
                    /* provides: struct vrblk, scblk, icblk, rcblk, union block */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * hshte — end-of-hash-table pointer.
 * Exported from sbl.asm ("global hshte") but absent from the C extern list
 * in osint.h. We declare it ourselves — it is a word-sized global that holds
 * the address one-past the last hash bucket, set during SPITBOL initialisation.
 *-------------------------------------------------------------------------*/
extern word hshte;

/*===========================================================================
 * 1. Per-statement hook — replaces zyspl() in osint/syspl.c
 *=========================================================================*/

typedef void (*spl_step_fn)(int stno, void *arg);

spl_step_fn  g_spl_step_hook = NULL;   /* NULL in production — zero overhead */
void        *g_spl_step_arg  = NULL;
static int   g_spl_stno      = 0;      /* statement counter for this run     */

/* Called by SPITBOL before every statement (WA=0 polling, WA=2 step done).
 * With ENGINE=1 all brkpnd/pollevent guards are compiled away upstream, so
 * we just fire the hook (if set) and re-arm with SET_WA(1) so we get called
 * again after exactly one statement. */
int
zyspl(void)
{
    if (g_spl_step_hook)
        g_spl_step_hook(++g_spl_stno, g_spl_step_arg);
    SET_WA(1);          /* re-arm: call again after 1 statement */
    return NORMAL_RETURN;
}

/* Reset statement counter between runs. */
void
spl_step_reset(void)
{
    g_spl_stno = 0;
}

/*===========================================================================
 * 2. spl_nv_snapshot() — walk SPITBOL's vrblk hash table
 *
 * Returns an NvPair[] (heap-allocated) with all named, non-empty program
 * variables and their stringified values.  Caller must free() *out.
 *
 * NvPair is defined in one4all's snobol4.h as:
 *   typedef struct { const char *name; DESCR_t val; } NvPair;
 * but we don't want to pull in all of scrip's headers here.  We use a
 * compatible local struct and cast at the call site in IM-15.
 *
 * For now we emit a simpler SplNvPair (name + stringified value) which
 * IM-15 will convert to ExecSnapshot format.
 *=========================================================================*/

typedef struct {
    char *name;     /* heap-allocated copy of variable name */
    char *val_str;  /* heap-allocated stringified value      */
} SplNvPair;

/*--- helpers ---------------------------------------------------------------*/

/* cfp_b = sizeof(word) on this platform. */
#define CFP_B ((int)sizeof(word))

/* Access a field in a vrblk by word-offset (matching SIL equ values). */
#define VR_FIELD(vp, word_off) (((word *)(vp))[(word_off)])

/* vrblk field word offsets (from sbl.asm equ definitions):
 *   vrget=0  vrsto=1  vrval=2  vrtra=3  vrlbl=4  vrfnc=5  vrnxt=6
 *   vrlen=7  vrchs=8  (vrchs is where the name characters start)        */
#define VR_VRVAL(vp)  ((union block *) VR_FIELD(vp, 2))
#define VR_VRNXT(vp)  ((struct vrblk *) VR_FIELD(vp, 6))
#define VR_VRLEN(vp)  ((int) VR_FIELD(vp, 7))
#define VR_VRCHS(vp)  ((char *)(&((word *)(vp))[8]))

/* Stringify a SPITBOL value block into a fixed buffer.
 * Returns buf, always NUL-terminated. */
static char *
spl_val_to_str(union block *blk, char *buf, int bufsz)
{
    if (!blk) { snprintf(buf, bufsz, "<null>"); return buf; }

    word typ = ((word *)blk)[0];        /* first word is always the type tag */

    if (typ == TYPE_ICL) {
        /* integer */
        snprintf(buf, bufsz, "%ld", (long)blk->icb.val);
    } else if (typ == TYPE_SCL) {
        /* string — may contain NUL bytes; we truncate at first NUL or len */
        int len = (int)blk->scb.len;
        if (len <= 0) {
            snprintf(buf, bufsz, "\"\"");
        } else {
            int copy = (len < bufsz - 3) ? len : (bufsz - 3);
            buf[0] = '"';
            memcpy(buf + 1, blk->scb.str, copy);
            buf[1 + copy] = '"';
            buf[2 + copy] = '\0';
        }
    } else if (typ == TYPE_RCL) {
        /* real */
        snprintf(buf, bufsz, "%g", blk->rcb.rcval);
    } else {
        /* pattern, array, table, code, etc. — just show the type word */
        snprintf(buf, bufsz, "<blk:0x%lx>", (unsigned long)typ);
    }
    return buf;
}

/* Walk the hash table and collect all named, non-empty variables.
 * Returns count; sets *out to a heap-allocated SplNvPair[].
 * Returns -1 on allocation failure.                                       */
int
spl_nv_snapshot(SplNvPair **out)
{
    /* hshtb / hshte hold addresses of the first and one-past-last bucket.
     * Each bucket is one word — a pointer to the head vrblk of the chain
     * (or 0 if the bucket is empty).                                      */
    word  *bucket     = GET_MIN_VALUE(hshtb, word *);
    word  *bucket_end = &hshte;         /* hshte IS the end pointer value  */

    /* Conservative upper bound: count vrblks first so we allocate once.   */
    int cap  = 64;
    int n    = 0;
    SplNvPair *pairs = malloc(cap * sizeof(SplNvPair));
    if (!pairs) { *out = NULL; return -1; }

    char vbuf[256];

    for (; bucket < bucket_end; bucket++) {
        struct vrblk *vr = (struct vrblk *)(*bucket);
        while (vr) {
            int vrlen = VR_VRLEN(vr);

            /* Skip system variables (vrlen == 0 means the name is stored
             * in the associated svblk, not inline — these are SPITBOL
             * internals like &STCOUNT, not user variables).               */
            if (vrlen > 0) {
                union block *val = VR_VRVAL(vr);

                /* Skip unset (null-string) and trapped values.
                 * TYPE_SCL + len==0 is the null string (nulls).
                 * A trapped block's first word won't be ICL/SCL/RCL.      */
                int is_null = (val &&
                               ((word *)val)[0] == TYPE_SCL &&
                               val->scb.len == 0);

                if (val && !is_null) {
                    /* grow array if needed */
                    if (n >= cap) {
                        cap *= 2;
                        SplNvPair *p2 = realloc(pairs, cap * sizeof(SplNvPair));
                        if (!p2) {
                            /* free what we have and bail */
                            for (int i = 0; i < n; i++) {
                                free(pairs[i].name);
                                free(pairs[i].val_str);
                            }
                            free(pairs);
                            *out = NULL;
                            return -1;
                        }
                        pairs = p2;
                    }

                    /* copy name (not NUL-terminated in vrblk) */
                    char *nm = malloc(vrlen + 1);
                    if (!nm) goto oom;
                    memcpy(nm, VR_VRCHS(vr), vrlen);
                    nm[vrlen] = '\0';

                    /* stringify value */
                    spl_val_to_str(val, vbuf, sizeof(vbuf));
                    char *vs = strdup(vbuf);
                    if (!vs) { free(nm); goto oom; }

                    pairs[n].name    = nm;
                    pairs[n].val_str = vs;
                    n++;
                }
            }

            vr = VR_VRNXT(vr);
        }
    }

    *out = pairs;
    return n;

oom:
    for (int i = 0; i < n; i++) { free(pairs[i].name); free(pairs[i].val_str); }
    free(pairs);
    *out = NULL;
    return -1;
}

/* Free a SplNvPair array returned by spl_nv_snapshot(). */
void
spl_nv_snapshot_free(SplNvPair *pairs, int n)
{
    if (!pairs) return;
    for (int i = 0; i < n; i++) {
        free(pairs[i].name);
        free(pairs[i].val_str);
    }
    free(pairs);
}
