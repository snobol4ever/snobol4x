/*============================================================= csnobol4_shim.c
 * csnobol4_shim.c — CSNOBOL4 in-process executor shim (IM-15b).
 *
 * Compiled only when WITH_CSNOBOL4 is defined (scrip-monitor build).
 * Links against csnobol4/libcsnobol4.a.
 *
 * CSNOBOL4 macro convention: S_L(x), S_A(x) etc. expect x to be a POINTER
 * (they cast via _SPEC(x) = *(struct spec *)(x)). Use &sp not sp.
 *
 * Variable table walk: OBLIST..OBEND bucket array, LNKFLD chains.
 * Each node: name at BCDFLD via X_LOCSP, value descriptor at node+DESCR.
 *===========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "/home/claude/csnobol4/include/h.h"
#include "/home/claude/csnobol4/include/snotypes.h"
#include "/home/claude/csnobol4/include/macros.h"
#include "/home/claude/csnobol4/include/lib.h"
#include "/home/claude/csnobol4/equ.h"
#include "/home/claude/csnobol4/res.h"
#include "/home/claude/csnobol4/data.h"

typedef struct { char *name; char *val_str; } CsnNvPair;

/*--- Hook globals (extern in csnobol4/isnobol4.c via IM-15b patch) ---------*/
typedef void (*csn_step_fn)(int stno, void *arg);
csn_step_fn  g_csn_step_hook = NULL;
void        *g_csn_step_arg  = NULL;
int          g_csn_stno      = 0;
void csn_step_reset(void) { g_csn_stno = 0; }

/*--- Step-limit longjmp ----------------------------------------------------*/
static jmp_buf _csn_step_jmp;
static int     _csn_step_target = 0;
static void _csn_step_cb(int stno, void *arg) {
    (void)arg;
    if (stno >= _csn_step_target) longjmp(_csn_step_jmp, 1);
}

/*--- Value descriptor → string ---------------------------------------------*/
static char *csn_descr_to_str(ptr_t vp) {
    int vtype = (int)D_V(vp);
    char buf[256];
    if (vtype == S) {
        struct spec sp; struct spec *spp = &sp;
        X_LOCSP(spp, vp);
        int len = (int)S_L(spp);
        if (len <= 0) return strdup("");
        if (len > 4096) len = 4096;
        char *s = malloc((size_t)(len + 1));
        if (!s) return strdup("?");
        memcpy(s, S_SP(spp), (size_t)len);
        s[len] = '\0';
        return s;
    } else if (vtype == I) {
        snprintf(buf, sizeof buf, "%ld", (long)D_A(vp));
        return strdup(buf);
    } else if (vtype == R) {
        real_t rv; int_t ia = D_A(vp);
        memcpy(&rv, &ia, sizeof rv);
        snprintf(buf, sizeof buf, "%g", (double)rv);
        return strdup(buf);
    } else if (vtype == 0) {
        return strdup("");
    } else {
        snprintf(buf, sizeof buf, "<type%d>", vtype);
        return strdup(buf);
    }
}

/*--- Walk OBSTRT hash table -------------------------------------------------*/
static int csn_nv_snapshot(CsnNvPair **out_pairs, int *out_count) {
    int cap = 64, n = 0;
    CsnNvPair *pairs = malloc((size_t)cap * sizeof(CsnNvPair));
    if (!pairs) { *out_pairs = NULL; *out_count = 0; return -1; }
    ptr_t bucket = OBLIST;
    ptr_t obend  = OBEND;
    while (1) {
        bucket += DESCR;
        if (D_PTR(bucket) > D_PTR(obend)) break;
        ptr_t node = bucket;
        while (1) {
            node = D_A(node + LNKFLD);
            if (node == 0) break;
            ptr_t vp = node + DESCR;
            if (D_V(vp) == 0 && D_A(vp) == 0) continue;
            struct spec nsp; struct spec *nspp = &nsp;
            X_LOCSP(nspp, node);
            int nlen = (int)S_L(nspp);
            if (nlen <= 0) continue;
            char *name = malloc((size_t)(nlen + 1));
            if (!name) continue;
            memcpy(name, S_SP(nspp), (size_t)nlen);
            name[nlen] = '\0';
            char *val = csn_descr_to_str(vp);
            if (n >= cap) {
                cap *= 2;
                CsnNvPair *tmp = realloc(pairs, (size_t)cap * sizeof(CsnNvPair));
                if (!tmp) { free(name); free(val); break; }
                pairs = tmp;
            }
            pairs[n].name = name; pairs[n].val_str = val; n++;
        }
    }
    *out_pairs = pairs; *out_count = n; return n;
}

/*--- Public interface -------------------------------------------------------*/
int csnobol4_main(int argc, char *argv[]);

int csnobol4_run_steps(const char *sno_path, int step_limit,
                       CsnNvPair **out_pairs, int *out_count) {
    *out_pairs = NULL; *out_count = 0;
    if (!sno_path || step_limit <= 0) return -1;
    _csn_step_target = step_limit;
    g_csn_stno = 0;
    g_csn_step_hook = _csn_step_cb;
    g_csn_step_arg  = NULL;
    char *argv_csn[] = { (char *)"csnobol4", (char *)sno_path, NULL };
    int hit = setjmp(_csn_step_jmp);
    if (hit == 0) csnobol4_main(2, argv_csn);
    g_csn_step_hook = NULL;
    csn_nv_snapshot(out_pairs, out_count);
    return 0;
}

void csn_nv_snapshot_free(CsnNvPair *pairs, int n) {
    if (!pairs) return;
    for (int i = 0; i < n; i++) { free(pairs[i].name); free(pairs[i].val_str); }
    free(pairs);
}
