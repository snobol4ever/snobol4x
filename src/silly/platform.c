/*
 * platform.c — platform layer for Silly SNOBOL4
 *
 * Provides:
 *   1. arena_init()          — 128 MB mmap arena
 *   2. struct syntab + STREAM_fn / clertb_fn / plugtb_fn
 *   3. All static scan-table definitions (ported from CSNOBOL4 syn.c)
 *   4. Runtime scan-table init for operator-fn put values (init_syntab)
 *   5. All XCALL_* stubs
 *   6. Operator-fn DESCR_t globals (ADDFN, SUBFN, …)
 *   7. Other missing data globals
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: SS-19 (platform)
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include "types.h"
#include "data.h"
#include "arena.h"

/* ── I/O read EOF/ERR flag (set by STREAD_fn, read by callers) ───────── */
/* 1 = EOF (oracle IO_EOF → XLATIN/FILCHK), 0 = I/O error (oracle IO_ERR → COMP1) */
int sread_last_eof = 0;

/* ── 1. ARENA ─────────────────────────────────────────────────────────── */


/* arena_base declared in data.c; we assign it here */
extern char *arena_base;

/* stack sizes (in DESCRs) */
#define PDL_DESCS   4096
#define SIL_DESCS   8192
#define OBJ_DESCS   (OCASIZ)   /* object code area */

/* arena_init is in arena.c */

/* ── 2. SCAN TABLE INFRASTRUCTURE ────────────────────────────────────── */

enum action { AC_CONTIN=0, AC_STOP, AC_STOPSH, AC_ERROR, AC_GOTO };

struct acts {
    int_t           put;    /* STYPE value on break (type code or fn offset) */
    enum action act;
    struct syntab *go;  /* for AC_GOTO */
};

#define CHARSET 256

struct syntab {
    const char          *name;
    unsigned char        chrs[CHARSET];   /* 0=CONTIN, else 1-based index */
    const struct acts *actions;
};

/*
 * STREAM_fn — core scanner.
 * Sets STYPE.a.i to the put value on break/stop.
 * Returns OK on AC_STOP/AC_STOPSH, FAIL on run-out, ERROR (error) on AC_ERROR.
 */
/* STREAM_fn defined below after lookup_tbl */

/* clertb_fn / plugtb_fn — used by SPAN/BREAK at runtime */
/* clertb_fn defined below */

/* plugtb_fn defined below */

/* ── 3. STATIC SCAN TABLES ───────────────────────────────────────────── */
/*
 * chrs[] arrays and action tables copied verbatim from CSNOBOL4 syn.c.
 * put values that are type-code integers match equ.h exactly.
 * put values that are arena offsets (ADDFN etc.) are filled by init_syntab().
 */

/* ── FRWDTB ── skip whitespace, stop on syntax chars ── */
static struct acts FRWDTB_actions[7];
struct syntab FRWDTB_st = { "FRWDTB", {
     7,  7,  7,  7,  7,  7,  7,  7,  7,  0,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     0,  7,  7,  7,  7,  7,  7,  7,  7,  2,  7,  7,  4,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  5,  6,  7,  1,  3,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  3,  7,  1,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7
}, FRWDTB_actions };

/* ── CARDTB ── card type classification ── */
static struct acts CARDTB_actions[4];
struct syntab CARDTB_st = { "CARDTB", {
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  1,  4,  1,  4,  4,  4,  4,  4,  4,  1,  3,  4,  2,  3,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4
}, CARDTB_actions };

/* ── IBLKTB ── inter-statement blank ── */
static struct acts IBLKTB_actions[3];
struct syntab IBLKTB_st = { "IBLKTB", {
      3,  3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      1,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, IBLKTB_actions };

/* ── EOSTB ── end of statement ── */
static struct acts EOSTB_actions[1];
struct syntab EOSTB_st = { "EOSTB", {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
}, EOSTB_actions };

/* ── GOTSTB ── success/fail goto ── */
static struct acts GOTSTB_actions[3];
struct syntab GOTSTB_st = { "GOTSTB", {
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, GOTSTB_actions };

/* ── GOTOTB ── goto dispatcher ── */
static struct acts GOTOTB_actions[5];
struct syntab GOTOTB_st = { "GOTOTB", {
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  3,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  4,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  2,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  1,  5,  5,  5,  5,  5,  5,  5,  4,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  2,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  1,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
      5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
}, GOTOTB_actions };

/* ── GOTFTB ── failure/nofail goto ── */
static struct acts GOTFTB_actions[3];
struct syntab GOTFTB_st = { "GOTFTB", {
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, GOTFTB_actions };

/* ── LBLTB / LBLXTB ── label scanning ── */
static struct acts LBLTB_actions[3];
struct syntab LBLTB_st = { "LBLTB", {
      3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,  3,  3,
      3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  2,  3,  3,  3,  3,
      3,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  3,  3,  3,  3,
      3,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  3,  3,  3,  3,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
}, LBLTB_actions };

static struct acts LBLXTB_actions[1];
struct syntab LBLXTB_st = { "LBLXTB", {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
}, LBLXTB_actions };

/* ── NUMBTB / NUMCTB ── prototype dimension parsing ── */
static struct acts NUMBTB_actions[4];
static struct acts NUMCTB_actions[3];
static struct syntab NUMCTB_st;
struct syntab NUMBTB_st = { "NUMBTB", {
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  2,  1,  4,  4,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
      4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
}, NUMBTB_actions };

/* ── ELEMTB ── element dispatcher ── */
static struct acts ELEMTB_actions[6];
static struct syntab INTGTB_st;
static struct syntab VARTB_st;
static struct syntab SQLITB_st;
static struct syntab DQLITB_st;
struct syntab ELEMTB_st = { "ELEMTB", {
      6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
      6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
      6,  6,  4,  6,  6,  6,  6,  3,  5,  6,  6,  6,  6,  6,  6,  6,
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  6,  6,  6,  6,  6,  6,
      6,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  6,  6,  6,  6,  6,
      6,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  6,  6,  6,  6,  6,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
}, ELEMTB_actions };

/* ── VARTB / VARATB / VARBTB ── variable scanning ── */
static struct acts VARTB_actions[4];
static struct acts VARATB_actions[4];
static struct acts VARBTB_actions[4];
static struct syntab VARBTB_st;
static struct syntab VARATB_st;

/* ── INTGTB / FLITB / EXPTB / EXPBTB ── number parsing ── */
static struct acts INTGTB_actions[4];
static struct acts FLITB_actions[3];
static struct acts EXPTB_actions[2];
static struct acts EXPBTB_actions[2];
static struct syntab FLITB_st;
static struct syntab EXPTB_st;
static struct syntab EXPBTB_st;

/* ── SQLITB / DQLITB ── string literals ── */
static struct acts SQLITB_actions[1];
static struct acts DQLITB_actions[1];

/* ── SPANTB / BRKTB ── SPAN/BREAK (runtime-filled, placeholders) ── */
static struct acts SPANTB_actions[3];
static struct acts BRKTB_actions[3];
struct syntab SPANTB_st = { "SPANTB", { 0 }, SPANTB_actions };
struct syntab BRKTB_st  = { "BRKTB",  { 0 }, BRKTB_actions  };

/* ── NBLKTB ── non-blank block ── */
static struct acts NBLKTB_actions[2];
static struct syntab NBLKTB_st;

/* ── STARTB / TBLKTB ── star/token block ── */
static struct acts STARTB_actions[3];
static struct acts TBLKTB_actions[2];
static struct syntab STARTB_st;
static struct syntab TBLKTB_st;

/* ── BIOPTB and friends ── binary operators (put filled at runtime) ── */
static struct acts BIOPTB_actions[15];
struct syntab BIOPTB_st = { "BIOPTB", {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 11, 15,  8,  4,  9, 12, 15, 15, 15,  5,  1, 15,  2,  3,  6,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 14,
     7, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 10, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 15, 13, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
}, BIOPTB_actions };

/* UNOPTB — unary operators */
static struct acts UNOPTB_actions[15];
struct syntab UNOPTB_st = { "UNOPTB", {
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 12, 15,  9,  4,  7, 10, 15, 15, 15,  5,  1, 15,  2,  3,  6,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13,
      8, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 15, 14, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 12, 15, 11, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
}, UNOPTB_actions };

/* SBIPTB */
static struct acts SBIPTB_actions[16];
struct syntab SBIPTB_st = { "SBIPTB", {
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 11, 16,  8,  4,  9, 12, 16, 16, 16,  5,  1, 16,  2,  3,  6,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 14,
      7, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 13, 16, 10, 15,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 11, 16, 13, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
}, SBIPTB_actions };

/* BBIOPTB */
static struct acts BBIOPTB_actions[15];
struct syntab BBIOPTB_st = { "BBIOPTB", {
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 11, 15,  8,  4,  9, 12, 15, 15, 15,  5,  1, 15,  2,  3,  6,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 14,
      7, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 10, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 15, 13, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
}, BBIOPTB_actions };

/* BSBIPTB */
static struct acts BSBIPTB_actions[16];
struct syntab BSBIPTB_st = { "BSBIPTB", {
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 11, 16,  8,  4,  9, 12, 16, 16, 16,  5,  1, 16,  2,  3,  6,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 16, 14,
      7, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 13, 16, 10, 15,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 11, 16, 13, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
}, BSBIPTB_actions };

/* ── 4. OPERATOR-FN DESCR GLOBALS (v311.sil §24 lines 11629–11720) ──── */
/* FNC-flagged function descriptors; V = arity; A = filled by data_init */

DESCR_t ADDFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=30},0,29}
};
DESCR_t BIAMFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=5},0,4}
};
DESCR_t BIATFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=25},0,24}
};
DESCR_t BINGFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=70},0,70}
};
DESCR_t BIPDFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=35},0,34}
};
DESCR_t BIPRFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=45},0,44}
};
DESCR_t BIBDFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=16},0,15}
};
DESCR_t BIBRFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=18},0,17}
};
DESCR_t BIQSFN[3] = {
    {{.i=0},1,0},
    {{.i=0},0,0},
    {{.i=70},0,69}
};
/* v311.sil §24 lines 11655–11671: 3-slot binary op-fns (LHERE aliases point here) */
/* BISNFN is the primary name; SCANFN is a macro alias (LHERE BISNFN, then SCANFN DESCR) */
DESCR_t BISNFN[3] = { {{.i=0},0,2}, {{.i=0},0,0}, {{.i=3},0,2} };   /* SCAN,   prec 3/2  */
DESCR_t BISRFN[3] = { {{.i=0},0,3}, {{.i=0},0,0}, {{.i=3},0,2} };   /* SJSR,   prec 3/2  */
DESCR_t BIEQFN[3] = { {{.i=0},0,2}, {{.i=0},0,0}, {{.i=1},0,1} };   /* ASGN,   prec 1/1  */
DESCR_t CONFN[3]  = { {{.i=0},0,2}, {{.i=0},0,0}, {{.i=20},0,19} }; /* CONCAT, prec 20/19 */
/* LHERE aliases resolved: BISNFN=SCANFN[0], BISRFN=SJSRFN[0], BIEQFN=ASGNFN[0] */
/* SCANFN/SJSRFN/ASGNFN macro aliases defined in data.h */
DESCR_t DIVFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=40},0,39}
};
DESCR_t DOLFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=60},0,59}
};
DESCR_t EXPFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=50},0,50}
};
DESCR_t MPYFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=42},0,41}
};
DESCR_t NAMFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=60},0,59}
};
DESCR_t ORFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=10},0,9}
};
DESCR_t SUBFN[3] = {
    {{.i=0},0,2},
    {{.i=0},0,0},
    {{.i=30},0,29}
};
DESCR_t AROWFN = {{.i=0}, 1, 0};
DESCR_t ATFN   = {{.i=0}, 0, 1};
DESCR_t BARFN  = {{.i=0}, 1, 0};
DESCR_t DOTFN  = {{.i=0}, 0, 1};
DESCR_t INDFN  = {{.i=0}, 0, 1};
DESCR_t KEYFN  = {{.i=0}, 0, 1};
DESCR_t MNSFN  = {{.i=0}, 0, 1};
DESCR_t NEGFN  = {{.i=0}, 0, 1};
DESCR_t PDFN   = {{.i=0}, 1, 0};
DESCR_t PLSFN  = {{.i=0}, 0, 1};
DESCR_t PRFN   = {{.i=0}, 1, 0};
DESCR_t QUESFN = {{.i=0}, 0, 1};
DESCR_t SLHFN  = {{.i=0}, 1, 0};
DESCR_t STRFN  = {{.i=0}, 0, 1};

/* ── 5. OTHER MISSING DATA GLOBALS ───────────────────────────────────── */

/* Scratch DESCRs referenced by data.h but not in data.c */
DESCR_t DTCL   = {.a={.i=0},.f=0,.v=0};
DESCR_t TAILSP_d = {.a={.i=0},.f=0,.v=0};   /* TAILSP is a SPEC_t below */
SPEC_t  TAILSP = {0};
SPEC_t  REALSP = {0};
SPEC_t  STARSP = {0};

/* Compiler working globals */
DESCR_t EXOPND = {.a={.i=0},.f=0,.v=0};
DESCR_t EXPRND = {.a={.i=0},.f=0,.v=0};
DESCR_t EXELND = {.a={.i=0},.f=0,.v=0};
DESCR_t EXEXND = {.a={.i=0},.f=0,.v=0};
DESCR_t FORMND = {.a={.i=0},.f=0,.v=0};
DESCR_t SUBJND = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTOND = {.a={.i=0},.f=0,.v=0};
DESCR_t SGOND  = {.a={.i=0},.f=0,.v=0};  /* success goto node  v311.sil 11078 */
DESCR_t FGOND  = {.a={.i=0},.f=0,.v=0};  /* failure goto node  v311.sil 11072 */
DESCR_t SRNCL  = {.a={.i=0},.f=0,.v=0};  /* success return     v311.sil 11058 */
DESCR_t ELEMND = {.a={.i=0},.f=0,.v=0};
DESCR_t ELEIND = {.a={.i=0},.f=0,.v=0};    /* ELIEXND/ELEXND */
DESCR_t ELEYND = {.a={.i=0},.f=0,.v=0};
DESCR_t ELEXND = {.a={.i=0},.f=0,.v=0};

/* Keyword value list */
DESCR_t KVLIST = {.a={.i=0},.f=0,.v=0};
DESCR_t KNLIST = {.a={.i=0},.f=0,.v=0};

/* I/O block pointers */
DESCR_t IO1PTR = {.a={.i=0},.f=0,.v=0};
DESCR_t IO2PTR = {.a={.i=0},.f=0,.v=0};
DESCR_t IO3PTR = {.a={.i=0},.f=0,.v=0};
DESCR_t IO4PTR = {.a={.i=0},.f=0,.v=0};
/* IOSP is SPEC_t in data.h */

/* Misc missing */
DESCR_t A2PTR  = {.a={.i=0},.f=0,.v=0};
DESCR_t A3PTR  = {.a={.i=0},.f=0,.v=0};
DESCR_t ATPTR  = {.a={.i=0},.f=0,.v=0};
DESCR_t LPTR   = {.a={.i=0},.f=0,.v=0};
DESCR_t WPTRX  = {.a={.i=0},.f=0,.v=0};
DESCR_t XIPTR  = {.a={.i=0},.f=0,.v=0};
DESCR_t STOPSH_d = {.a={.i=0},.f=0,.v=0};  /* STOPSH used in clertb calls */
DESCR_t CONTIN_d = {.a={.i=0},.f=0,.v=0};
int32_t NMOVER = 0;   /* name list end (byte count) */
#define NMOVRSZ  32   /* name buffer size in DESCRs */

/* Trace working */
DESCR_t TVAL   = {.a={.i=0},.f=0,.v=0};
DESCR_t TVALL  = {.a={.i=0},.f=0,.v=0};
DESCR_t NVAL   = {.a={.i=0},.f=0,.v=0};
DESCR_t VVAL   = {.a={.i=0},.f=0,.v=0};
DESCR_t VVALL  = {.a={.i=0},.f=0,.v=0};
DESCR_t TKEYLIST = {.a={.i=0},.f=0,.v=0};
DESCR_t TKLIST = {.a={.i=0},.f=0,.v=0};
DESCR_t TRATL  = {.a={.i=0},.f=0,.v=0};
DESCR_t TRATLEND = {.a={.i=0},.f=0,.v=0};
DESCR_t VALTRLIST = {.a={.i=0},.f=0,.v=0};
DESCR_t VALTRS = {.a={.i=0},.f=0,.v=0};

/* Compiler fixed-up globals (aliases/extras) */
DESCR_t OCSVCLX = {.a={.i=0},.f=0,.v=0};
DESCR_t OCLIMAX = {.a={.i=0},.f=0,.v=0};
DESCR_t OCLIM   = {.a={.i=0},.f=0,.v=0};
char INBUF[4096] = {0};   /* compiler input buffer */
DESCR_t CERRBUF = {.a={.i=0},.f=0,.v=0};
char ERRBUF[4096] = {0};  /* error pointer buffer */
SPEC_t  ERRSP   = {0};
SPEC_t  CERSP   = {0};
SPEC_t  QTSP    = {0};
SPEC_t  DPSP_sp = {0};
SPEC_t  DTARSP  = {0};
SPEC_t  LNOSP   = {0};
SPEC_t  AMPSP   = {0};
SPEC_t  COLSP   = {0};
SPEC_t  BLNSP   = {0};
SPEC_t  BLEQSP  = {0};
SPEC_t  BLSP    = {0};
SPEC_t  EQLSP   = {0};
SPEC_t  LPRNSP  = {0};
SPEC_t  RPRNSP  = {0};
SPEC_t  OFSP_sp = {0};
SPEC_t  PROTSP  = {0};
SPEC_t  ETIMSP  = {0};
SPEC_t  XFERSP  = {0};
SPEC_t  CMSP    = {0};  /* comma spec */
SPEC_t  CMASP   = {0};
SPEC_t  XFILENM = {0};

/* More desc globals */
DESCR_t XCALL_XECOMP_d = {.a={.i=0},.f=0,.v=0};
DESCR_t SIGNCL = {.a={.i=0},.f=0,.v=0};
DESCR_t ETMCL  = {.a={.i=0},.f=0,.v=0};
/* EOSCL in data.c */
DESCR_t UNSCL  = {.a={.i=0},.f=0,.v=0};
DESCR_t SCFLCL = {.a={.i=0},.f=0,.v=0};
DESCR_t DFLFST = {.a={.i=0},.f=0,.v=0};
DESCR_t PRMTBL2[8];   /* second copy of PRMTBL for GENVAR */
DESCR_t ODPSIZ = {.a={.i=0},.f=0,.v=0};
DESCR_t INTSIZ = {.a={.i=0},.f=0,.v=0};
DESCR_t DTARSZ = {.a={.i=0},.f=0,.v=0};
DESCR_t MAXLEN = {.a={.i=0x7fffffff},.f=0,.v=I};
DESCR_t DMPPTR = {.a={.i=0},.f=0,.v=0};
DESCR_t XPTR_b = {.a={.i=0},.f=0,.v=0};

/* Keyword-list terminal nodes */
DESCR_t ABRTKY = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t ARBKY  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t BALKY  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t ERRTKY = {.a={.i=0},.f=TTL|MARK,.v=0}; /* error-type keyword key (ERRTSP) */
DESCR_t FAILKY = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t FNCEKY = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t REMKY  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t SUCCKY = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t STCTKY = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t STNOKY = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t FALKY  = {.a={.i=0},.f=TTL|MARK,.v=0};

/* Pattern-valued globals */
DESCR_t PATND  = {.a={.i=0},.f=0,.v=P};
DESCR_t FNCPL  = {.a={.i=0},.f=FNC,.v=0};

/* Trace data */
DESCR_t TRCSP_d = {.a={.i=0},.f=0,.v=0};
SPEC_t  TRACSP  = {0};
SPEC_t  TRSTSP  = {0};
SPEC_t  TRLVSP  = {0};
SPEC_t  TRCLSP  = {0};   /* ' call of ' — oracle v311.sil line 10926 */
DESCR_t TRCBLK2[6];   /* working copy */

/* DEFCL / FUNTCL */
DESCR_t DEFCL  = {.a={.i=0},.f=FNC,.v=0};
DESCR_t FUNTCL = {.a={.i=0},.f=FNC,.v=0};
DESCR_t FRNCL  = {.a={.i=0},.f=0,.v=0};
/* GOBRCL in data.c */
DESCR_t GOGOCL = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTLCL = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTGCL = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTOCL = {.a={.i=0},.f=0,.v=0};  /* GOTO fn DESCR FNC v311.sil 10784 */

/* Sizes */
DESCR_t TBSIZ  = {.a={.i=0},.f=0,.v=0};
DESCR_t TBLBCS = {.a={.i=0},.f=0,.v=0};
DESCR_t TBLSCS = {.a={.i=0},.f=0,.v=0};
DESCR_t SNODSZ = {.a={.i=NODESZ},.f=0,.v=P};
DESCR_t XSIZ   = {.a={.i=0},.f=0,.v=0};
DESCR_t YSIZ   = {.a={.i=0},.f=0,.v=0};
DESCR_t ZSIZ   = {.a={.i=0},.f=0,.v=0};
DESCR_t TSIZ   = {.a={.i=0},.f=0,.v=0};
DESCR_t INCSTK = {.a={.i=0},.f=0,.v=0};
DESCR_t BOSCLX = {.a={.i=0},.f=0,.v=0};
DESCR_t BOSCL  = {.a={.i=0},.f=0,.v=0};
DESCR_t UNDEF  = {.a={.i=0},.f=0,.v=S};   /* UNDFC */
DESCR_t UNDFCL = {.a={.i=0},.f=0,.v=S};
DESCR_t UNSCCL = {.a={.i=0},.f=0,.v=0};
DESCR_t SCANCL = {.a={.i=0},.f=FNC,.v=2};
DESCR_t SJSRCL = {.a={.i=0},.f=FNC,.v=0};
DESCR_t NNOCL  = {.a={.i=0},.f=0,.v=0};
DESCR_t NNYCCL = {.a={.i=0},.f=FNC,.v=3};
DESCR_t ANYCL2 = {.a={.i=0},.f=FNC,.v=3};
DESCR_t SPNCCL = {.a={.i=0},.f=FNC,.v=3};
DESCR_t BRKCCL2= {.a={.i=0},.f=FNC,.v=3};
DESCR_t RPSICLX= {.a={.i=0},.f=0,.v=0};
DESCR_t RTBCLX = {.a={.i=0},.f=FNC,.v=0};
DESCR_t POSICL2= {.a={.i=0},.f=FNC,.v=0};
DESCR_t LENCLX = {.a={.i=0},.f=FNC,.v=0};
DESCR_t TBCLX  = {.a={.i=0},.f=FNC,.v=0};
DESCR_t ERRTYX = {.a={.i=0},.f=0,.v=0};
DESCR_t XOCSVC = {.a={.i=0},.f=0,.v=0};
DESCR_t XOCICL = {.a={.i=0},.f=0,.v=0};
DESCR_t XOCBSC = {.a={.i=0},.f=0,.v=0};
DESCR_t XLNNOC = {.a={.i=0},.f=0,.v=0};
DESCR_t XLSFLN = {.a={.i=0},.f=0,.v=0};
DESCR_t XLSLNC = {.a={.i=0},.f=0,.v=0};
DESCR_t XITNDT = {.a={.i=0},.f=0,.v=0};
DESCR_t XITPTR = {.a={.i=0},.f=0,.v=0};
DESCR_t XFRTNC = {.a={.i=0},.f=0,.v=0};
DESCR_t XERRTY = {.a={.i=0},.f=0,.v=0};

/* INATLM / OTSAT etc. (I/O block lists) */
DESCR_t INATL  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t INSATL = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t OTSATL = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t OUTATL = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t KNATL  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t KVATL  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t DTATL  = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t DTLIST = {.a={.i=0},.f=TTL|MARK,.v=0};
DESCR_t TRLVLIST = {.a={.i=0},.f=0,.v=0};
DESCR_t TKRL   = {.a={.i=0},.f=0,.v=0};
DESCR_t TKEYL  = {.a={.i=0},.f=0,.v=0};
DESCR_t TLABL  = {.a={.i=0},.f=0,.v=0};
DESCR_t TFNCLP = {.a={.i=0},.f=0,.v=0};
DESCR_t TFNRLP = {.a={.i=0},.f=0,.v=0};
DESCR_t ERRMSG = {.a={.i=0},.f=0,.v=0};
DESCR_t EMSGCL = {.a={.i=0},.f=0,.v=0};
DESCR_t LNTHCL = {.a={.i=0},.f=0,.v=0};
DESCR_t NNYCL2 = {.a={.i=0},.f=FNC,.v=3};
DESCR_t DT1CL  = {.a={.i=0},.f=0,.v=0};

/* FNLIST / ICLBLK (fn-list for compiler) */
/* FNLIST in data.c */
/* ICLBLK in data.c */
DESCR_t FNCEKYX= {.a={.i=0},.f=FNC,.v=2};
DESCR_t FNMEBLK= {.a={.i=0},.f=FNC,.v=0};
DESCR_t FNMECL = {.a={.i=0},.f=FNC,.v=0};
DESCR_t PSTACK = {.a={.i=0},.f=0,.v=0};

/* ── 6. DESCR_t wrappers for scan tables (used via P2A) ──────────────── */
/*
 * Each syntab is referenced in code as a DESCR_t whose A field = P2A
 * of the syntab struct.  We expose them here as DESCR_t globals.
 * init_syntab() fills the A fields after arena_base is set.
 */
DESCR_t FRWDTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t CARDTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t IBLKTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t ELEMTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t EOSTB   = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTSTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTOTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t GOTFTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t LBLTB   = {.a={.i=0},.f=0,.v=0};
DESCR_t LBLXTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t NUMBTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t SPANTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t BRKTB   = {.a={.i=0},.f=0,.v=0};
DESCR_t BIOPTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t SBIPTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t UNOPTB  = {.a={.i=0},.f=0,.v=0};
DESCR_t BBIOPTB = {.a={.i=0},.f=0,.v=0};
DESCR_t BSBIPTB = {.a={.i=0},.f=0,.v=0};
DESCR_t INTGTB  = {.a={.i=0},.f=0,.v=0};  /* integer scan table */

/* ── 7. INIT_SYNTAB — fills syntab action arrays + DESCR A fields ── */

static void init_actions(void)
{
    /* FRWDTB: [0]=EQTYP/STOP [1]=RPTYP/STOP [2]=RBTYP/STOP
               [3]=CMATYP/STOP [4]=CLNTYP/STOP [5]=EOSTYP/STOP [6]=NBTYP/STOPSH */
    FRWDTB_actions[0] = (struct acts){EQTYP, AC_STOP, NULL};
    FRWDTB_actions[1] = (struct acts){RPTYP, AC_STOP, NULL};
    FRWDTB_actions[2] = (struct acts){RBTYP, AC_STOP, NULL};
    FRWDTB_actions[3] = (struct acts){CMATYP, AC_STOP, NULL};
    FRWDTB_actions[4] = (struct acts){CLNTYP, AC_STOP, NULL};
    FRWDTB_actions[5] = (struct acts){EOSTYP, AC_STOP, NULL};
    FRWDTB_actions[6] = (struct acts){NBTYP, AC_STOPSH, NULL};
    CARDTB_actions[0] = (struct acts){CMTTYP, AC_STOPSH, NULL}; /* CARDTB: [0]=CMTTYP/STOPSH [1]=CTLTYP/STOPSH [2]=CNTTYP/STOPSH [3]=NEWTYP/STOPSH */
    CARDTB_actions[1] = (struct acts){CTLTYP, AC_STOPSH, NULL};
    CARDTB_actions[2] = (struct acts){CNTTYP, AC_STOPSH, NULL};
    CARDTB_actions[3] = (struct acts){NEWTYP, AC_STOPSH, NULL};
    IBLKTB_actions[0] = (struct acts){0, AC_GOTO, &FRWDTB_st}; /* IBLKTB: [0]=goto FRWDTB [1]=EOSTYP/STOP [2]=ERROR */
    IBLKTB_actions[1] = (struct acts){EOSTYP, AC_STOP, NULL};
    IBLKTB_actions[2] = (struct acts){0, AC_STOPSH, NULL}; /* HW-8: non-blank→ST_STOP, not ST_ERROR */
    EOSTB_actions[0] = (struct acts){0, AC_STOP, NULL}; /* EOSTB: [0]=STOP */
    GOTSTB_actions[0] = (struct acts){SGOTYP, AC_STOP, NULL}; /* GOTSTB: [0]=SGOTYP/STOP [1]=STOTYP/STOP [2]=ERROR */
    GOTSTB_actions[1] = (struct acts){STOTYP, AC_STOP, NULL};
    GOTSTB_actions[2] = (struct acts){0, AC_CONTIN, NULL}; /* HW-9: arg3=arg4 RTZPTR */
    GOTOTB_actions[0] = (struct acts){0, AC_GOTO, &GOTSTB_st}; /* GOTOTB: [0]=goto GOTSTB [1]=goto GOTFTB [2]=UGOTYP/STOP [3]=UTOTYP/STOP [4]=ERROR */
    GOTOTB_actions[1] = (struct acts){0, AC_GOTO, &GOTFTB_st};
    GOTOTB_actions[2] = (struct acts){UGOTYP, AC_STOP, NULL};
    GOTOTB_actions[3] = (struct acts){UTOTYP, AC_STOP, NULL};
    GOTOTB_actions[4] = (struct acts){0, AC_ERROR, NULL};
    GOTFTB_actions[0] = (struct acts){FGOTYP, AC_STOP, NULL}; /* GOTFTB: [0]=FGOTYP/STOP [1]=FTOTYP/STOP [2]=ERROR */
    GOTFTB_actions[1] = (struct acts){FTOTYP, AC_STOP, NULL};
    GOTFTB_actions[2] = (struct acts){0, AC_ERROR, NULL};
    LBLTB_actions[0] = (struct acts){0, AC_GOTO, &LBLXTB_st}; /* LBLTB: [0]=goto LBLXTB [1]=STOPSH [2]=ERROR */
    LBLTB_actions[1] = (struct acts){0, AC_STOPSH, NULL};
    LBLTB_actions[2] = (struct acts){0, AC_CONTIN, NULL}; /* HW-10: COMP7/COMP7 same exit */
    LBLXTB_actions[0] = (struct acts){0, AC_STOPSH, NULL}; /* LBLXTB: [0]=STOPSH */
    NUMBTB_actions[0] = (struct acts){0, AC_GOTO, &NUMCTB_st}; /* NUMBTB: [0]=goto NUMCTB [1]=CMATYP/STOPSH [2]=DIMTYP/STOPSH [3]=ERROR */
    NUMBTB_actions[1] = (struct acts){CMATYP, AC_STOPSH, NULL};
    NUMBTB_actions[2] = (struct acts){DIMTYP, AC_STOPSH, NULL};
    NUMBTB_actions[3] = (struct acts){0, AC_ERROR, NULL};
    NUMCTB_actions[0] = (struct acts){CMATYP, AC_STOPSH, NULL};
    NUMCTB_actions[1] = (struct acts){DIMTYP, AC_STOPSH, NULL};
    NUMCTB_actions[2] = (struct acts){0, AC_ERROR, NULL};
    NUMCTB_st = (struct syntab){ "NUMCTB", {
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 3, 3, 3,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
    }, NUMCTB_actions };
    ELEMTB_actions[0] = (struct acts){ILITYP, AC_GOTO, &INTGTB_st}; /* ELEMTB actions */
    ELEMTB_actions[1] = (struct acts){VARTYP, AC_GOTO, &VARTB_st};
    ELEMTB_actions[2] = (struct acts){QLITYP, AC_GOTO, &SQLITB_st};
    ELEMTB_actions[3] = (struct acts){QLITYP, AC_GOTO, &DQLITB_st};
    ELEMTB_actions[4] = (struct acts){NSTTYP, AC_STOP, NULL};
    ELEMTB_actions[5] = (struct acts){0, AC_ERROR, NULL};
    SQLITB_actions[0] = (struct acts){0, AC_STOP, NULL}; /* SQLITB / DQLITB */
    DQLITB_actions[0] = (struct acts){0, AC_STOP, NULL};
    SQLITB_st = (struct syntab){ "SQLITB", {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    }, SQLITB_actions };
    DQLITB_st = (struct syntab){ "DQLITB", {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    }, DQLITB_actions };
    VARTB_actions[0] = (struct acts){VARTYP, AC_STOPSH, NULL}; /* VARTB */
    VARTB_actions[1] = (struct acts){FNCTYP, AC_STOP, NULL};
    VARTB_actions[2] = (struct acts){ARYTYP, AC_STOP, NULL};
    VARTB_actions[3] = (struct acts){0, AC_ERROR, NULL};
    VARTB_st = (struct syntab){ "VARTB", {
        4,4,4,4,4,4,4,4,4,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        1,4,4,4,4,4,4,4,2,1,4,4,1,4,0,4,0,0,0,0,0,0,0,0,0,0,4,1,3,4,1,4,
        4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,4,1,4,0,
        4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,4,4,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    }, VARTB_actions };
    VARATB_actions[0] = (struct acts){0, AC_GOTO, &VARBTB_st}; /* VARATB / VARBTB */
    VARATB_actions[1] = (struct acts){CMATYP, AC_STOPSH, NULL};
    VARATB_actions[2] = (struct acts){RPTYP, AC_STOPSH, NULL};
    VARATB_actions[3] = (struct acts){0, AC_CONTIN, NULL}; /* HW-11: arg3=arg4 PROTER */
    VARBTB_actions[0] = (struct acts){LPTYP, AC_STOPSH, NULL};
    VARBTB_actions[1] = (struct acts){CMATYP, AC_STOPSH, NULL};
    VARBTB_actions[2] = (struct acts){RPTYP, AC_STOPSH, NULL};
    VARBTB_actions[3] = (struct acts){0, AC_ERROR, NULL};
    VARATB_st = (struct syntab){ "VARATB", {
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,3,4,4,2,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,4,4,4,4,4,
        4,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,4,4,4,4,4,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    }, VARATB_actions };
    VARBTB_st = (struct syntab){ "VARBTB", {
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,1,3,4,4,2,4,0,4,0,0,0,0,0,0,0,0,0,0,4,4,4,4,4,4,
        4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,4,0,
        4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,4,4,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    }, VARBTB_actions };
    INTGTB_actions[0] = (struct acts){ILITYP, AC_STOPSH, NULL}; /* INTGTB */
    INTGTB_actions[1] = (struct acts){FLITYP, AC_GOTO, &FLITB_st};
    INTGTB_actions[2] = (struct acts){FLITYP, AC_GOTO, &EXPTB_st};
    INTGTB_actions[3] = (struct acts){0, AC_ERROR, NULL};
    INTGTB_st = (struct syntab){ "INTGTB", {
        4,4,4,4,4,4,4,4,4,1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        1,4,4,4,4,4,4,4,4,1,4,4,1,4,2,4,0,0,0,0,0,0,0,0,0,0,4,1,4,4,1,4,
        4,4,4,4,4,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,4,4,
        4,4,4,4,4,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    }, INTGTB_actions };
    FLITB_actions[0] = (struct acts){0, AC_STOPSH, NULL}; /* FLITB / EXPTB / EXPBTB */
    FLITB_actions[1] = (struct acts){0, AC_GOTO, &EXPTB_st};
    FLITB_actions[2] = (struct acts){0, AC_ERROR, NULL};
    EXPTB_actions[0] = (struct acts){0, AC_GOTO, &EXPBTB_st};
    EXPTB_actions[1] = (struct acts){0, AC_ERROR, NULL};
    EXPBTB_actions[0] = (struct acts){0, AC_STOPSH, NULL};
    EXPBTB_actions[1] = (struct acts){0, AC_ERROR, NULL};
    FLITB_st = (struct syntab){ "FLITB", {
        3,3,3,3,3,3,3,3,3,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        1,3,3,3,3,3,3,3,3,1,3,3,1,3,3,3,0,0,0,0,0,0,0,0,0,0,3,1,3,3,1,3,
        3,3,3,3,3,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,3,3,
        3,3,3,3,3,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    }, FLITB_actions };
    EXPTB_st = (struct syntab){ "EXPTB", {
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,1,2,1,2,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    }, EXPTB_actions };
    EXPBTB_st = (struct syntab){ "EXPBTB", {
        2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        1,2,2,2,2,2,2,2,2,1,2,2,1,2,2,2,0,0,0,0,0,0,0,0,0,0,2,1,2,2,1,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    }, EXPBTB_actions };
    NBLKTB_actions[0] = (struct acts){0, AC_ERROR, NULL}; /* NBLKTB */
    NBLKTB_actions[1] = (struct acts){0, AC_STOPSH, NULL};
    NBLKTB_st = (struct syntab){ "NBLKTB", {
        2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        1,2,2,2,2,2,2,2,2,1,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,1,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    }, NBLKTB_actions };
    TBLKTB_actions[0] = (struct acts){0, AC_STOP, NULL}; /* TBLKTB / STARTB */
    TBLKTB_actions[1] = (struct acts){0, AC_ERROR, NULL};
    TBLKTB_st = (struct syntab){ "TBLKTB", {
        2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    }, TBLKTB_actions };
    STARTB_actions[0] = (struct acts){0, AC_STOP, NULL}; /* STARTB: [0]=STOP [1]=EXPFN/goto TBLKTB [2]=ERROR */
    STARTB_actions[1] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* put filled below */
    STARTB_actions[2] = (struct acts){0, AC_ERROR, NULL};
    STARTB_st = (struct syntab){ "STARTB", {
        3,3,3,3,3,3,3,3,3,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        1,3,3,3,3,3,3,3,3,3,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    }, STARTB_actions };
    SPANTB_actions[0] = (struct acts){0, AC_STOP, NULL}; /* SPANTB / BRKTB actions (chrs[] are runtime-filled by clertb/plugtb) */
    SPANTB_actions[1] = (struct acts){0, AC_STOPSH, NULL};
    SPANTB_actions[2] = (struct acts){0, AC_ERROR, NULL};
    BRKTB_actions[0] = (struct acts){0, AC_STOP, NULL};
    BRKTB_actions[1] = (struct acts){0, AC_STOPSH, NULL};
    BRKTB_actions[2] = (struct acts){0, AC_ERROR, NULL};
    BIOPTB_actions[ 0] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* ADDFN  + */  /* BIOPTB actions — put filled below with arena offsets */
    BIOPTB_actions[ 1] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* SUBFN  - */
    BIOPTB_actions[ 2] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* NAMFN  . */
    BIOPTB_actions[ 3] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* DOLFN  $ */
    BIOPTB_actions[ 4] = (struct acts){0, AC_GOTO, &STARTB_st}; /* MPYFN  * */
    BIOPTB_actions[ 5] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* DIVFN  / */
    BIOPTB_actions[ 6] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* BIATFN @ */
    BIOPTB_actions[ 7] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* BIPDFN # */
    BIOPTB_actions[ 8] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* BIPRFN % */
    BIOPTB_actions[ 9] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* EXPFN  ^ */
    BIOPTB_actions[10] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* ORFN   | */
    BIOPTB_actions[11] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* BIAMFN & */
    BIOPTB_actions[12] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* BINGFN \ */
    BIOPTB_actions[13] = (struct acts){0, AC_GOTO, &TBLKTB_st}; /* BIQSFN ? */
    BIOPTB_actions[14] = (struct acts){0, AC_CONTIN, NULL}; /* HW-4: arg3=arg4 in all STREAM calls */
    UNOPTB_actions[ 0] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* PLSFN  + */                  /* UNOPTB actions */
    UNOPTB_actions[ 1] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* MNSFN  - */
    UNOPTB_actions[ 2] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* DOTFN  . */
    UNOPTB_actions[ 3] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* INDFN  $ */
    UNOPTB_actions[ 4] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* STRFN  * */
    UNOPTB_actions[ 5] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* SLHFN  / */
    UNOPTB_actions[ 6] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* PRFN   % */
    UNOPTB_actions[ 7] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* ATFN   @ */
    UNOPTB_actions[ 8] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* PDFN   # */
    UNOPTB_actions[ 9] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* KEYFN  & */
    UNOPTB_actions[10] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* NEGFN  \ */
    UNOPTB_actions[11] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* BARFN  | */
    UNOPTB_actions[12] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* QUESFN ? */
    UNOPTB_actions[13] = (struct acts){0, AC_GOTO, &NBLKTB_st}; /* AROWFN ! */
    UNOPTB_actions[14] = (struct acts){0, AC_CONTIN, NULL}; /* HW-3: non-op chars → ST_EOS, not error */

    /* SBIPTB */
    SBIPTB_actions[0].put = P2A(ADDFN);
    SBIPTB_actions[0].act = AC_GOTO;
    SBIPTB_actions[0].go = &TBLKTB_st;
    SBIPTB_actions[1].put = P2A(SUBFN);
    SBIPTB_actions[1].act = AC_GOTO;
    SBIPTB_actions[1].go = &TBLKTB_st;
    SBIPTB_actions[2].put = P2A(NAMFN);
    SBIPTB_actions[2].act = AC_GOTO;
    SBIPTB_actions[2].go = &TBLKTB_st;
    SBIPTB_actions[3].put = P2A(DOLFN);
    SBIPTB_actions[3].act = AC_GOTO;
    SBIPTB_actions[3].go = &TBLKTB_st;
    SBIPTB_actions[4].put = P2A(MPYFN);
    SBIPTB_actions[4].act = AC_GOTO;
    SBIPTB_actions[4].go = &STARTB_st;
    SBIPTB_actions[5].put = P2A(DIVFN);
    SBIPTB_actions[5].act = AC_GOTO;
    SBIPTB_actions[5].go = &TBLKTB_st;
    SBIPTB_actions[6].put = P2A(BIATFN);
    SBIPTB_actions[6].act = AC_GOTO;
    SBIPTB_actions[6].go = &TBLKTB_st;
    SBIPTB_actions[7].put = P2A(BIPDFN);
    SBIPTB_actions[7].act = AC_GOTO;
    SBIPTB_actions[7].go = &TBLKTB_st;
    SBIPTB_actions[8].put = P2A(BIPRFN);
    SBIPTB_actions[8].act = AC_GOTO;
    SBIPTB_actions[8].go = &TBLKTB_st;
    SBIPTB_actions[9].put = P2A(EXPFN);
    SBIPTB_actions[9].act = AC_GOTO;
    SBIPTB_actions[9].go = &TBLKTB_st;
    SBIPTB_actions[10].put = P2A(ORFN);
    SBIPTB_actions[10].act = AC_GOTO;
    SBIPTB_actions[10].go = &TBLKTB_st;
    SBIPTB_actions[11].put = P2A(BIAMFN);
    SBIPTB_actions[11].act = AC_GOTO;
    SBIPTB_actions[11].go = &TBLKTB_st;
    SBIPTB_actions[12].put = P2A(BINGFN);
    SBIPTB_actions[12].act = AC_GOTO;
    SBIPTB_actions[12].go = &TBLKTB_st;
    SBIPTB_actions[13].put = P2A(SCANFN);
    SBIPTB_actions[13].act = AC_GOTO;
    SBIPTB_actions[13].go = &TBLKTB_st;
    SBIPTB_actions[14].put = P2A(ASGNFN);
    SBIPTB_actions[14].act = AC_GOTO;
    SBIPTB_actions[14].go = &TBLKTB_st;
    SBIPTB_actions[15].act = AC_CONTIN; /* HW-5: arg3=arg4 in all STREAM calls */
    BSBIPTB_actions[0].put = P2A(ADDFN);
    BSBIPTB_actions[0].act = AC_GOTO;
    BSBIPTB_actions[0].go = &TBLKTB_st;
    BSBIPTB_actions[1].put = P2A(SUBFN);
    BSBIPTB_actions[1].act = AC_GOTO;
    BSBIPTB_actions[1].go = &TBLKTB_st;
    BSBIPTB_actions[2].put = P2A(NAMFN);
    BSBIPTB_actions[2].act = AC_GOTO;
    BSBIPTB_actions[2].go = &TBLKTB_st;
    BSBIPTB_actions[3].put = P2A(DOLFN);
    BSBIPTB_actions[3].act = AC_GOTO;
    BSBIPTB_actions[3].go = &TBLKTB_st;
    BSBIPTB_actions[4].put = P2A(MPYFN);
    BSBIPTB_actions[4].act = AC_GOTO;
    BSBIPTB_actions[4].go = &STARTB_st;
    BSBIPTB_actions[5].put = P2A(DIVFN);
    BSBIPTB_actions[5].act = AC_GOTO;
    BSBIPTB_actions[5].go = &TBLKTB_st;
    BSBIPTB_actions[6].put = P2A(BIATFN);
    BSBIPTB_actions[6].act = AC_GOTO;
    BSBIPTB_actions[6].go = &TBLKTB_st;
    BSBIPTB_actions[7].put = P2A(BIBDFN);
    BSBIPTB_actions[7].act = AC_GOTO;
    BSBIPTB_actions[7].go = &TBLKTB_st;
    BSBIPTB_actions[8].put = P2A(BIBRFN);
    BSBIPTB_actions[8].act = AC_GOTO;
    BSBIPTB_actions[8].go = &TBLKTB_st;
    BSBIPTB_actions[9].put = P2A(EXPFN);
    BSBIPTB_actions[9].act = AC_GOTO;
    BSBIPTB_actions[9].go = &TBLKTB_st;
    BSBIPTB_actions[10].put = P2A(ORFN);
    BSBIPTB_actions[10].act = AC_GOTO;
    BSBIPTB_actions[10].go = &TBLKTB_st;
    BSBIPTB_actions[11].put = P2A(BIAMFN);
    BSBIPTB_actions[11].act = AC_GOTO;
    BSBIPTB_actions[11].go = &TBLKTB_st;
    BSBIPTB_actions[12].put = P2A(BINGFN);
    BSBIPTB_actions[12].act = AC_GOTO;
    BSBIPTB_actions[12].go = &TBLKTB_st;
    BSBIPTB_actions[13].put = P2A(SCANFN);
    BSBIPTB_actions[13].act = AC_GOTO;
    BSBIPTB_actions[13].go = &TBLKTB_st;
    BSBIPTB_actions[14].put = P2A(ASGNFN);
    BSBIPTB_actions[14].act = AC_GOTO;
    BSBIPTB_actions[14].go = &TBLKTB_st;
    BSBIPTB_actions[15].act = AC_CONTIN; /* HW-7: arg3=arg4 */
    /* BBIOPTB */
    BBIOPTB_actions[0].put = P2A(ADDFN);
    BBIOPTB_actions[0].act = AC_GOTO;
    BBIOPTB_actions[0].go = &TBLKTB_st;
    BBIOPTB_actions[1].put = P2A(SUBFN);
    BBIOPTB_actions[1].act = AC_GOTO;
    BBIOPTB_actions[1].go = &TBLKTB_st;
    BBIOPTB_actions[2].put = P2A(NAMFN);
    BBIOPTB_actions[2].act = AC_GOTO;
    BBIOPTB_actions[2].go = &TBLKTB_st;
    BBIOPTB_actions[3].put = P2A(DOLFN);
    BBIOPTB_actions[3].act = AC_GOTO;
    BBIOPTB_actions[3].go = &TBLKTB_st;
    BBIOPTB_actions[4].put = P2A(MPYFN);
    BBIOPTB_actions[4].act = AC_GOTO;
    BBIOPTB_actions[4].go = &STARTB_st;
    BBIOPTB_actions[5].put = P2A(DIVFN);
    BBIOPTB_actions[5].act = AC_GOTO;
    BBIOPTB_actions[5].go = &TBLKTB_st;
    BBIOPTB_actions[6].put = P2A(BIATFN);
    BBIOPTB_actions[6].act = AC_GOTO;
    BBIOPTB_actions[6].go = &TBLKTB_st;
    BBIOPTB_actions[7].put = P2A(BIBDFN);
    BBIOPTB_actions[7].act = AC_GOTO;
    BBIOPTB_actions[7].go = &TBLKTB_st;
    BBIOPTB_actions[8].put = P2A(BIBRFN);
    BBIOPTB_actions[8].act = AC_GOTO;
    BBIOPTB_actions[8].go = &TBLKTB_st;
    BBIOPTB_actions[9].put = P2A(EXPFN);
    BBIOPTB_actions[9].act = AC_GOTO;
    BBIOPTB_actions[9].go = &TBLKTB_st;
    BBIOPTB_actions[10].put = P2A(ORFN);
    BBIOPTB_actions[10].act = AC_GOTO;
    BBIOPTB_actions[10].go = &TBLKTB_st;
    BBIOPTB_actions[11].put = P2A(BIAMFN);
    BBIOPTB_actions[11].act = AC_GOTO;
    BBIOPTB_actions[11].go = &TBLKTB_st;
    BBIOPTB_actions[12].put = P2A(BINGFN);
    BBIOPTB_actions[12].act = AC_GOTO;
    BBIOPTB_actions[12].go = &TBLKTB_st;
    BBIOPTB_actions[13].put = P2A(BIQSFN);
    BBIOPTB_actions[13].act = AC_GOTO;
    BBIOPTB_actions[13].go = &TBLKTB_st;
    BBIOPTB_actions[14].act = AC_CONTIN; /* HW-6: arg3=arg4 */
    /* BSBIPTB */
    BSBIPTB_actions[0].put = P2A(ADDFN);
    BSBIPTB_actions[0].act = AC_GOTO;
    BSBIPTB_actions[0].go = &TBLKTB_st;
    BSBIPTB_actions[1].put = P2A(SUBFN);
    BSBIPTB_actions[1].act = AC_GOTO;
    BSBIPTB_actions[1].go = &TBLKTB_st;
    BSBIPTB_actions[2].put = P2A(NAMFN);
    BSBIPTB_actions[2].act = AC_GOTO;
    BSBIPTB_actions[2].go = &TBLKTB_st;
    BSBIPTB_actions[3].put = P2A(DOLFN);
    BSBIPTB_actions[3].act = AC_GOTO;
    BSBIPTB_actions[3].go = &TBLKTB_st;
    BSBIPTB_actions[4].put = P2A(MPYFN);
    BSBIPTB_actions[4].act = AC_GOTO;
    BSBIPTB_actions[4].go = &STARTB_st;
    BSBIPTB_actions[5].put = P2A(DIVFN);
    BSBIPTB_actions[5].act = AC_GOTO;
    BSBIPTB_actions[5].go = &TBLKTB_st;
    BSBIPTB_actions[6].put = P2A(BIATFN);
    BSBIPTB_actions[6].act = AC_GOTO;
    BSBIPTB_actions[6].go = &TBLKTB_st;
    BSBIPTB_actions[7].put = P2A(BIBDFN);
    BSBIPTB_actions[7].act = AC_GOTO;
    BSBIPTB_actions[7].go = &TBLKTB_st;
    BSBIPTB_actions[8].put = P2A(BIBRFN);
    BSBIPTB_actions[8].act = AC_GOTO;
    BSBIPTB_actions[8].go = &TBLKTB_st;
    BSBIPTB_actions[9].put = P2A(EXPFN);
    BSBIPTB_actions[9].act = AC_GOTO;
    BSBIPTB_actions[9].go = &TBLKTB_st;
    BSBIPTB_actions[10].put = P2A(ORFN);
    BSBIPTB_actions[10].act = AC_GOTO;
    BSBIPTB_actions[10].go = &TBLKTB_st;
    BSBIPTB_actions[11].put = P2A(BIAMFN);
    BSBIPTB_actions[11].act = AC_GOTO;
    BSBIPTB_actions[11].go = &TBLKTB_st;
    BSBIPTB_actions[12].put = P2A(BINGFN);
    BSBIPTB_actions[12].act = AC_GOTO;
    BSBIPTB_actions[12].go = &TBLKTB_st;
    BSBIPTB_actions[13].put = P2A(SCANFN);
    BSBIPTB_actions[13].act = AC_GOTO;
    BSBIPTB_actions[13].go = &TBLKTB_st;
    BSBIPTB_actions[14].put = P2A(ASGNFN);
    BSBIPTB_actions[14].act = AC_GOTO;
    BSBIPTB_actions[14].go = &TBLKTB_st;
    BSBIPTB_actions[15].act = AC_CONTIN; /* HW-7: arg3=arg4 */
}

/*====================================================================================================================*/
/* Bind DESCR_t wrappers to their syntab structs */

/* Registry: map DESCR_t* → syntab* for platform tables */
#define MAX_TBLS 32
static struct { DESCR_t *d; struct syntab *st; } tbl_reg[MAX_TBLS];
static int tbl_reg_n = 0;

static void reg_tbl(DESCR_t *d, struct syntab *st)
{
    tbl_reg[tbl_reg_n].d = d;
    tbl_reg[tbl_reg_n].st = st;
    tbl_reg_n++;
    d->a.i = -(int32_t)tbl_reg_n; /* negative = registry index */  /* Store index+1 in A field as negative sentinel so A2P is not called */
}

/*====================================================================================================================*/
/* Override STREAM_fn to handle registry tables */
/* (redefine STREAM_fn to dispatch via registry for negative A) */
/* We patch the function by using a dispatch wrapper — rename the
 * raw scanner and expose the wrapper as STREAM_fn proper.         */

void init_syntab(void)
{
    init_actions();
    BIOPTB_actions[ 0].put = P2A(ADDFN); /* Bind operator fn arena offsets into BIOPTB/UNOPTB put fields */
    BIOPTB_actions[ 1].put = P2A(SUBFN);
    BIOPTB_actions[ 2].put = P2A(NAMFN);
    BIOPTB_actions[ 3].put = P2A(DOLFN);
    BIOPTB_actions[ 4].put = P2A(MPYFN);
    BIOPTB_actions[ 5].put = P2A(DIVFN);
    BIOPTB_actions[ 6].put = P2A(BIATFN);
    BIOPTB_actions[ 7].put = P2A(BIPDFN);
    BIOPTB_actions[ 8].put = P2A(BIPRFN);
    BIOPTB_actions[ 9].put = P2A(EXPFN);
    BIOPTB_actions[10].put = P2A(ORFN);
    BIOPTB_actions[11].put = P2A(BIAMFN);
    BIOPTB_actions[12].put = P2A(BINGFN);
    BIOPTB_actions[13].put = P2A(BIQSFN);
    STARTB_actions[1].put = P2A(EXPFN);
    UNOPTB_actions[ 0].put = P2A(&PLSFN);
    UNOPTB_actions[ 1].put = P2A(&MNSFN);
    UNOPTB_actions[ 2].put = P2A(&DOTFN);
    UNOPTB_actions[ 3].put = P2A(&INDFN);
    UNOPTB_actions[ 4].put = P2A(&STRFN);
    UNOPTB_actions[ 5].put = P2A(&SLHFN);
    UNOPTB_actions[ 6].put = P2A(&PRFN);
    UNOPTB_actions[ 7].put = P2A(&ATFN);
    UNOPTB_actions[ 8].put = P2A(&PDFN);
    UNOPTB_actions[ 9].put = P2A(&KEYFN);
    UNOPTB_actions[10].put = P2A(&NEGFN);
    UNOPTB_actions[11].put = P2A(&BARFN);
    UNOPTB_actions[12].put = P2A(&QUESFN);
    UNOPTB_actions[13].put = P2A(&AROWFN);
    reg_tbl(&FRWDTB, &FRWDTB_st); /* Register all tables */
    reg_tbl(&CARDTB, &CARDTB_st);
    reg_tbl(&IBLKTB, &IBLKTB_st);
    reg_tbl(&ELEMTB, &ELEMTB_st);
    reg_tbl(&EOSTB, &EOSTB_st);
    reg_tbl(&GOTSTB, &GOTSTB_st);
    reg_tbl(&GOTOTB, &GOTOTB_st);
    reg_tbl(&GOTFTB, &GOTFTB_st);
    reg_tbl(&LBLTB, &LBLTB_st);
    reg_tbl(&LBLXTB, &LBLXTB_st);
    reg_tbl(&NUMBTB, &NUMBTB_st);
    reg_tbl(&SPANTB, &SPANTB_st);
    reg_tbl(&BRKTB, &BRKTB_st);
    reg_tbl(&BIOPTB, &BIOPTB_st);
    reg_tbl(&SBIPTB, &SBIPTB_st);
    reg_tbl(&UNOPTB, &UNOPTB_st);
    reg_tbl(&INTGTB, &INTGTB_st);

    /* BUG-CERRSP: CERRSP needs an arena-backed buffer (oracle: ERRBUF CARDSZ+STNOSZ+1).
     * Without this, sp_ptr(&CERRSP) = A2P(0) = arena_base and APDSP corrupts it. */
    {
        int32_t errbuf_sz = CARDSZ + STNOSZ + 1;
        int32_t errbuf = BLOCK_fn(errbuf_sz, B);
        if (errbuf) {
            CERRSP.a = errbuf;
            CERRSP.o = 0;
            CERRSP.l = 0;
            CERRSP.v = S;
            CERRSP.f = 0;
        }
    }

    /* PRMTBL: header + 18 GC roots [v311.sil §24 lines 11997+12005] */
    PRMTBL[0].a.i = P2A(PRMTBL); PRMTBL[0].f = TTL|MARK; PRMTBL[0].v = 18*DESCR;
    /* PRMPTR [v311.sil 11143]: DESCR PRMTBL,0,0 — A must point to PRMTBL for GC root walk */
    PRMPTR.a.i = P2A(PRMTBL);
    /* DTEND: single DESCR, A=EFFCL ptr [v311.sil line 11990] */
    DTEND.a.i = P2A(&EFFCL);
    /* GCXTTL / GCBLK [v311.sil 11132+11155]: GCBLK.A must point to a 2-DESCR TTL+MARK buffer.
     * GCM uses GCBLK to mark individual string structures (GCBA4 path in GC_fn).
     * Oracle: res.gcblk.A = GCXTTL; res.gcxttl = {GCXTTL, TTL+MARK, DESCR}.
     * Allocate a 2-DESCR arena block (title + 1 body slot) and wire GCBLK to it. */
    {
        int32_t gcxttl_off = BLOCK_fn(DESCR, 0); /* 1-body-DESCR block; BLOCK_fn prepends TTL */
        if (gcxttl_off) {
            DESCR_t *gcxttl = (DESCR_t *)A2P(gcxttl_off);
            gcxttl->a.i = gcxttl_off;  /* self-ptr (oracle: D_A(gcxttl)=GCXTTL) */
            gcxttl->f   = TTL | MARK;
            gcxttl->v   = DESCR;        /* 1 body slot */
            GCBLK.a.i   = gcxttl_off;
        }
    }
    /* Trace/literal fn DESCRs: fn-ptr fills (LABTR_fn etc are defined in trace.c/asgn.c) */
    extern RESULT_t LABTR_fn(void), VALTR_fn(void), LIT_fn(void), KEYTR_fn(void), INIT_fn(void), GOTO_fn(void);
    extern RESULT_t GOTG_fn(void), GOTL_fn(void), FENTR_fn(void), FNEXTR_fn(void);
    extern RESULT_t ITEM_fn(void), BASE_fn(void), CMA_fn(void), ARGNER_fn(void), END_fn(void), EROR_fn(void);
    AREFN.a.i   = P2A(&ITEM_fn);   BASEFN.a.i  = P2A(&BASE_fn);
    CMAFN.a.i   = P2A(&CMA_fn);    ENDAFN.a.i  = P2A(&ARGNER_fn);
    ENDFN.a.i   = P2A(&END_fn);    ERORFN.a.i  = P2A(&EROR_fn);
    FNTRFN.a.i  = P2A(&FENTR_fn);  FXTRFN.a.i = P2A(&FNEXTR_fn);
    GOTGFN.a.i  = P2A(&GOTG_fn);   GOTLFN.a.i = P2A(&GOTL_fn);
    GOTOFN.a.i  = P2A(&GOTO_fn);
    INITFN.a.i  = P2A(&INIT_fn);
    KEYTFN.a.i = P2A(&KEYTR_fn); LABTFN.a.i = P2A(&LABTR_fn); LITFN.a.i = P2A(&LIT_fn); VLTRFN.a.i = P2A(&VALTR_fn);
    PRMTBL[1].a.i = P2A(&DTLIST);
    PRMTBL[2].a.i = P2A(&FNLIST);
    PRMTBL[3].a.i = P2A(&FTABLE);
    PRMTBL[4].a.i = P2A(&ICLBLK);
    PRMTBL[5].a.i = P2A(&KNLIST);
    PRMTBL[6].a.i = P2A(&KVLIST);
    PRMTBL[7].a.i = P2A(&OPTBL);
    /* STKHED block [v311.sil 12005]: 11 more GC roots appended after OPTBL */
    PRMTBL[8].a.i  = P2A(&STKHED);  /* interpreter stack                  */
    PRMTBL[9].a.i  = P2A(&INLIST);  /* input association pair list        */
    PRMTBL[10].a.i = P2A(&OTLIST);  /* output association pair list       */
    PRMTBL[11].a.i = P2A(&INSATL);  /* input block list                   */
    PRMTBL[12].a.i = P2A(&OTSATL);  /* output block list                  */
    PRMTBL[13].a.i = P2A(TFENPL);   /* call trace pair list               */
    PRMTBL[14].a.i = P2A(TFEXPL);   /* return trace pair list             */
    PRMTBL[15].a.i = P2A(TKEYPL);   /* keyword trace pair list            */
    PRMTBL[16].a.i = P2A(TLABPL);   /* label trace pair list              */
    PRMTBL[17].a.i = P2A(&TRLIST);  /* trace pair list                    */
    PRMTBL[18].a.i = P2A(TVALPL);   /* value trace pair list              */
    /* PRMDX = PRMSIZ = 18*DESCR (slots 1..18 of PRMTBL) [oracle data_init.h line 725] */
    D_A(PRMDX) = 18 * DESCR;
    /* Trace pair-list self-ptrs [v311.sil §24: each DESCR X,TTL+MARK,2*DESCR] */
    TVALPL[0].a.i = P2A(TVALPL);
    TLABPL[0].a.i = P2A(TLABPL);
    TFENPL[0].a.i = P2A(TFENPL);
    TFEXPL[0].a.i = P2A(TFEXPL);
    TKEYPL[0].a.i = P2A(TKEYPL);
    VALBLK[0].a.i = P2A(VALBLK);

    /* INITLS [v311.sil line 11399]: DESCR INITLS,TTL+MARK,8*DESCR + 8 sublist ptrs.
     * Allocate 8-slot arena block; fill with pointers to the 8 runtime lists.
     * Order: DTLIST FNLIST INLIST KNLIST KVLIST OTLIST OTSATL TRLIST */
    {
        int32_t ilblk = BLOCK_fn(8 * DESCR, 0);
        DESCR_t *il = (DESCR_t *)A2P(ilblk + DESCR); /* body starts after title DESCR */
        il[0].a.i = P2A(&DTLIST);
        il[1].a.i = P2A(&FNLIST);
        il[2].a.i = P2A(&INLIST);
        il[3].a.i = P2A(&KNLIST);
        il[4].a.i = P2A(&KVLIST);
        il[5].a.i = P2A(&OTLIST);
        il[6].a.i = P2A(&OTSATL);
        il[7].a.i = P2A(&TRLIST);
        INITLS.a.i = ilblk;
        INITLS.f   = TTL | MARK;
        INITLS.v   = 8 * DESCR;
    }

    /* Primitive pattern node self-ptrs + fn-code slot ptrs [v311.sil §24] */
    /* FAILPT: [0]=self-ptr, [1].a=P2A(&SALFFN) */
    FAILPT[0].a.i = P2A(FAILPT);
    FAILPT[1].a.i = P2A(&SALFFN);
    /* FNCEPT: [0]=self-ptr, [1].a=P2A(&FNCEFN) */
    FNCEPT[0].a.i = P2A(FNCEPT);
    FNCEPT[1].a.i = P2A(&FNCEFN);
    /* REMPT: [0]=self-ptr, [1].a=P2A(&RTBFN) */
    REMPT[0].a.i = P2A(REMPT);
    REMPT[1].a.i = P2A(&RTBFN);
    /* STARPT: [0]=self-ptr, [1].a=P2A(&STARFN), [5].a=P2A(&SCOKFN), [8].a=P2A(&DSARFN) */
    STARPT[0].a.i = P2A(STARPT);
    STARPT[1].a.i = P2A(&STARFN);
    STARPT[5].a.i = P2A(&SCOKFN);
    STARPT[8].a.i = P2A(&DSARFN);
    /* SUCCPT: [0]=self-ptr, [1].a=P2A(&SUCFFN) */
    SUCCPT[0].a.i = P2A(SUCCPT);
    SUCCPT[1].a.i = P2A(&SUCFFN);
    /* STRPAT: arena offset of STARPT template (used by SJSR/ASGN for *X wrapping) */
    STRPAT.a.i = P2A(STARPT);
    /* BALPT: [0]=self-ptr, [1].a=P2A(&SCOKFN), [4].a=P2A(&BALFN), [7].a=P2A(&BALFFN) */
    BALPT[0].a.i = P2A(BALPT);
    BALPT[1].a.i = P2A(&SCOKFN);
    BALPT[4].a.i = P2A(&BALFN);
    BALPT[7].a.i = P2A(&BALFFN);
    /* ARTAL: [0]=self-ptr, [1]=EARBFN, [4]=SCOKFN */
    ARTAL[0].a.i = P2A(ARTAL);
    ARTAL[1].a.i = P2A(&EARBFN);
    ARTAL[4].a.i = P2A(&SCOKFN);
    /* ARHED: [0]=self-ptr, [1]=SCOKFN, [4]=SCOKFN, [7]=ARBNFN, [10]=ARBFFN */
    ARHED[0].a.i = P2A(ARHED);
    ARHED[1].a.i = P2A(&SCOKFN);
    ARHED[4].a.i = P2A(&SCOKFN);
    ARHED[7].a.i = P2A(&ARBNFN);
    ARHED[10].a.i = P2A(&ARBFFN);
    /* ARBPT: [0]=self-ptr, [1]=SCOKFN, [4]=SCOKFN, [7]=FARBFN */
    ARBPT[0].a.i = P2A(ARBPT);
    ARBPT[1].a.i = P2A(&SCOKFN);
    ARBPT[4].a.i = P2A(&SCOKFN);
    ARBPT[7].a.i = P2A(&FARBFN);
    /* ARBAK: [0]=self-ptr, [1]=ONARFN, [4]=ONRFFN */
    ARBAK[0].a.i = P2A(ARBAK);
    ARBAK[1].a.i = P2A(&ONARFN);
    ARBAK[4].a.i = P2A(&ONRFFN);
    /* ABORPT: [0]=self-ptr, [1]=ABORFN */
    ABORPT[0].a.i = P2A(ABORPT);
    ABORPT[1].a.i = P2A(&ABORFN);
}

/*====================================================================================================================*/
/* Lookup syntab from a DESCR_t (handles registry-indexed tables) */
static struct syntab *lookup_tbl(const DESCR_t *d)
{
    int32_t a = D_A(*d);
    if (a < 0) {
        int idx = (int)(-a) - 1;
        if (idx < tbl_reg_n) return tbl_reg[idx].st;
    }
    return (struct syntab *)A2P(a); /* fallback: A is a direct arena offset */
}

/*====================================================================================================================*/
/* Rewrite STREAM_fn to use lookup_tbl instead of A2P directly */
RESULT_t STREAM_fn(SPEC_t *sp1, SPEC_t *sp2, DESCR_t *tbl_descr, int *stype_out)
{
    struct syntab *tp = lookup_tbl(tbl_descr);
    const unsigned char *cp = (const unsigned char *)A2P(sp2->a) + sp2->o;
    int len = sp2->l;
    int_t put = 0;
    for (; len > 0; cp++, len--) {
        unsigned ai = tp->chrs[*cp];
        if (ai == 0) continue;
        const struct acts *ap = tp->actions + (ai - 1);
        if (ap->put) put = ap->put;
        switch (ap->act) {
        case AC_CONTIN: break;
        case AC_STOP: cp++; len--; /* FALLTHROUGH */
        case AC_STOPSH: goto break_loop;
        case AC_ERROR:
            D_A(STYPE) = 0;
            { extern void error(int); error(17); }
            return FAIL;
        case AC_GOTO: tp = ap->go; break;
        }
    }
    {
        /* ST_EOS: no STOP/STOPSH found — reached end of subject.
         * Oracle stream.c always executes S_L(sp2) -= len after break_loop,
         * even for ST_EOS. len==0 here (loop exhausted), so match = sp2->l.
         * sp2->l becomes 0 (all consumed). sp2->o NOT bumped (no STOP accepted char).
         * sp1 gets the full prefix (everything scanned). */
        int match = sp2->l - len; /* = sp2->l when len==0 (fully exhausted) */
        *sp1 = *sp2; sp1->l = match;
        /* sp2->o NOT bumped (ST_EOS = no accepted char), but sp2->l IS decremented */
        sp2->l = len;  /* = 0: subject fully consumed */
        D_A(STYPE) = put;
        if (stype_out) *stype_out = (int)put;
        return FAIL;
    }
break_loop:
    {
        int match = sp2->l - len;
        *sp1 = *sp2; sp1->l = match; sp2->o += match; sp2->l = len;
        D_A(STYPE) = put;
        if (stype_out) *stype_out = (int)put;
        return OK;
    }
}

/*====================================================================================================================*/
/* clertb_fn/plugtb_fn defined below with DESCR_t interface */

/* ── 8. XCALL STUBS ───────────────────────────────────────────────────── */

void XCALL_ISTACK(void)      { /* C has native stack */ }
void XCALL_XECOMP(void)      { /* signal compile done — no-op initially */ }
void XCALL_io_flushall(void)  { fflush(NULL); }

void XCALL_MSTIME(DESCR_t *res)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    res->f = (uint8_t)R; /* return milliseconds as real_t */
    *(real_t *)&res->a.i = (real_t)(tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0);
}

/*====================================================================================================================*/
void XCALL_ZERBLK(DESCR_t *dst, DESCR_t size)
{
    void *p = A2P(D_A(*dst));
    memset(p, 0, (size_t)D_A(size));
}

/*====================================================================================================================*/
RESULT_t XCALL_GETPARM(SPEC_t *sp)
{
    sp->a = 0; sp->o = 0; sp->l = 0; /* No &PARM support yet */
    return FAIL;
}
/*====================================================================================================================*/
void XCALL_FREEPARM(SPEC_t *sp) { (void)sp; }

RESULT_t XCALL_GETPMPROTO(SPEC_t *sp, int32_t n) { (void)sp; (void)n; return FAIL; }

void XCALL_OUTPUT(int32_t unit, DESCR_t msg)
{
    (void)unit;
    const char *s = (const char *)A2P(D_A(msg));
    int32_t l = (int32_t)msg.v;
    fwrite(s, 1, (size_t)l, stdout);
    fputc('\n', stdout);
}

/*====================================================================================================================*/
void XCALL_OUTPUT_fmt(DESCR_t unit, const char *fmt, ...)
{
    (void)unit;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

/*====================================================================================================================*/
int  chk_break(int x)        { (void)x; return 1; } /* PLB113: always "hit" → proceed to STNOKY locapt */
void XCALL_chk_break(int x)  { (void)x; }

/* I/O stubs */
void XCALL_IO_OPENI(DESCR_t u, SPEC_t *sp)  { (void)u; (void)sp; }
void XCALL_IO_OPENO(DESCR_t u, SPEC_t *sp)  { (void)u; (void)sp; }
void XCALL_IO_SEEK(DESCR_t u, DESCR_t n)    { (void)u; (void)n; }
void XCALL_IO_PAD(SPEC_t *sp, int32_t w)    { (void)sp; (void)w; }
RESULT_t XCALL_IO_FILE(DESCR_t u, SPEC_t *sp) { (void)u; sp->l=0; return FAIL; }
void XCALL_BKSPCE(DESCR_t u)  { (void)u; }
int  XCALL_ENFILE(DESCR_t u)  { (void)u; return 1; } /* stub: always succeeds */
void XCALL_REWIND(DESCR_t u)  { (void)u; }
void XCALL_LINK(DESCR_t u, SPEC_t *sp)      { (void)u; (void)sp; }
void XCALL_UNLOAD(DESCR_t u)  { (void)u; }
void XCALL_RELSTRING(DESCR_t d) { (void)d; }
/* XCALL_XINCLD: open an include file — stub returns FAIL (not yet implemented) */
RESULT_t XCALL_XINCLD(DESCR_t unit, SPEC_t *fname) { (void)unit; (void)fname; return FAIL; }
void XCALL_XRAISP(SPEC_t *sp)
{
    unsigned char *p = (unsigned char *)A2P(sp->a) + sp->o;
    int i;
    for (i = 0; i < sp->l; i++) p[i] = (unsigned char)toupper(p[i]);
}
/*====================================================================================================================*/
void XCALL_REVERSE(SPEC_t *dst, SPEC_t *src)
{
    int32_t off = D_A(FRSGPT); /* copy src reversed into dst — naive arena alloc */
    D_A(FRSGPT) += src->l;
    char *d = A2P(off);
    const char *s = (const char *)A2P(src->a) + src->o + src->l - 1;
    int i;
    for (i = 0; i < src->l; i++) d[i] = *s--;
    dst->a = off; dst->o = 0; dst->l = src->l;
}
/*====================================================================================================================*/
void XCALL_SBREAL(DESCR_t *res, DESCR_t a, DESCR_t b)
{
    real_t ra = *(real_t*)&a.a.i;
    real_t rb = *(real_t*)&b.a.i;
    real_t rc = ra - rb;
    res->f = (uint8_t)R;
    *(real_t*)&res->a.i = rc;
}
/*====================================================================================================================*/
void XCALL_RPLACE(SPEC_t *dst, SPEC_t *src, SPEC_t *rep)
{
    (void)dst; (void)src; (void)rep; /* stub */
}
/*====================================================================================================================*/
void XCALL_XSUBSTR(SPEC_t *res, SPEC_t *src, int32_t start, int32_t len)
{
    *res = *src;
    res->o += start;
    res->l = len;
}
/*====================================================================================================================*/
void XCALL_DATE(SPEC_t *sp)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    int32_t off = D_A(FRSGPT); /* Arena-allocate 9-char date string "MM/DD/YY " */
    D_A(FRSGPT) += 12;
    char *buf = A2P(off);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(buf, 12, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, tm->tm_year%100);
#pragma GCC diagnostic pop
    sp->a = off; sp->o = 0; sp->l = 8;
}

/*====================================================================================================================*/
/* STREAD_fn / STPRNT_fn — I/O primitives */
/* io_eof_out: set to 1 on EOF (→ XLATIN/FILCHK), 0 on I/O error (→ COMP1). NULL = don't care. */
RESULT_t STREAD_fn(SPEC_t *dst, DESCR_t unit)
{
    (void)unit;
    static char linebuf[4096]; /* Read a line from stdin into arena */
    if (!fgets(linebuf, (int)sizeof linebuf, stdin)) {
        /* Oracle: IO_EOF → XLATIN (FILCHK path); IO_ERR → COMP1 */
        /* Callers check sread_is_eof after FAIL to dispatch correctly. */
        sread_last_eof = feof(stdin) ? 1 : 0;
        return FAIL;
    }
    sread_last_eof = 0;
    int n = (int)strlen(linebuf);
    if (n > 0 && linebuf[n-1] == '\n') n--;
    int32_t off = D_A(FRSGPT);
    D_A(FRSGPT) += n;
    memcpy(A2P(off), linebuf, (size_t)n);
    dst->a = off; dst->o = 0; dst->l = n;
    return OK;
}

/*====================================================================================================================*/
void STPRNT_fn(int32_t unit, DESCR_t rec, SPEC_t *sp)
{
    (void)unit; (void)rec;
    FILE *f = (unit == 1) ? stderr : stdout;
    if (sp->l > 0) fwrite(A2P(sp->a) + sp->o, 1, (size_t)sp->l, f);
    fputc('\n', f);
}

/*====================================================================================================================*/
/* STREAM_fn wrapper for clertb/plugtb — expose enum for callers */
void stream_fn_wrap(SPEC_t *sp1, SPEC_t *sp2, DESCR_t *tbl, int *st)
{
    STREAM_fn(sp1, sp2, tbl, st);
}

/*====================================================================================================================*/
/* removed duplicate */

/* error — fatal error handler */
void error(int n)
{
    if (n > 0 && n < 40 && MSGNO[n])
        fprintf(stderr, "SNOBOL4 fatal error %d: %s\n", n, MSGNO[n]);
    else
        fprintf(stderr, "SNOBOL4 fatal error %d\n", n);
    exit(1);
}

/*====================================================================================================================*/
/* ── 9. REMAINING MISSING SYMBOLS ────────────────────────────────────── */

/* INCL — include-file descriptor (DESCR_t, used as block pointer) */
DESCR_t INCL    = {{.i=0}, 0, 0};
DESCR_t INCSTK2 = {{.i=0}, 0, 0};  /* include stack */

/* Compiler object-code node descriptors */
DESCR_t LITCL   = {{.i=0}, 0x40, 1};   /* literal fn code node */
DESCR_t ITEMCL  = {{.i=0}, 0x40, 0};   /* item code node */
DESCR_t EXOPCL  = {{.i=0}, 0, 0};      /* expression operand code */
DESCR_t INITCL  = {{.i=0}, 0, 0};      /* INIT code cell */

/* Pattern primitives */
DESCR_t ANYCCL  = {{.i=0}, 0x40, 3};   /* ANY pattern class */
DESCR_t POSICL  = {{.i=0}, 0x40, 0};   /* POS pattern class */
DESCR_t RPSICL  = {{.i=0}, 0x40, 0};   /* RPOS pattern class */
DESCR_t RTBCL   = {{.i=0}, 0x40, 0};   /* RTAB pattern class */
DESCR_t TBCL    = {{.i=0}, 0x40, 0};   /* TAB pattern class */
DESCR_t ARBACK  = {{.i=0}, 0, P};      /* ARB backtrack node */

/* SPECs missing from data.c */
SPEC_t RNOSP  = {0};   /* run-out spec (empty) */
SPEC_t CERRSP = {0};   /* compiler error spec */
SPEC_t SPCSP  = {0};   /* space spec — 1 space char */
SPEC_t OFSP   = {0};   /* output format spec */
SPEC_t VSP    = {0};   /* value spec (scratch) */

/* XFILEN — current source filename spec */
SPEC_t XFILENSP = {0};
DESCR_t XFILEN  = {{.i=0}, 0, S};

/* XSTNOC — statement number copy (writable) */
DESCR_t XSTNOC  = {{.i=0}, 0, I};

/* stream_fn — lower-case alias used in scan.c */
/* CONTIN / STOPSH — action enum values as int_t globals for STREAM calls */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* removed duplicate */

/* IOSP — already SPEC_t in data.h, defined in data.c as SPEC_t IOSP */
/* If it shows as undefined, it's because data.c didn't define it.
   Check and add if needed: */
/* IOSP / ISPPTR — not defined in data.c */
SPEC_t  IOSP   = {0};
DESCR_t ISPPTR = {{.i=0}, 0, 0};

/* ── 10. CORRECT-SIGNATURE STUBS (override wrong ones above) ─────────── */
/* These match the exact extern declarations in the callers.              */

/* maknod_fn — allocate an IR node */
RESULT_t maknod_fn(DESCR_t *out, int32_t blk_off, int32_t tag, int32_t extra)
{
    (void)extra;
    int32_t off = D_A(FRSGPT);
    D_A(FRSGPT) += (int32_t)NODESZ;
    memset(A2P(off), 0, NODESZ);
    DESCR_t *hdr = (DESCR_t *)A2P(off);
    hdr->a.i = blk_off;
    hdr->f = (uint8_t)(TTL | MARK);
    hdr->v = tag;
    out->a.i = off; out->f = 0; out->v = tag;
    return OK;
}

/*====================================================================================================================*/
/* lvalue_fn — resolve pattern offset to its l-value offset */
int32_t lvalue_fn(int32_t pat_off) { return pat_off; }

/* cpypat_fn — copy NODESZ bytes from src_off to dst_off */
void cpypat_fn(int32_t dst_off, int32_t src_off, int32_t n)
{
    memcpy(A2P(dst_off), A2P(src_off), (size_t)(n > 0 ? n : NODESZ));
}

/*====================================================================================================================*/
/* getbal_fn — balanced-string scanner (stub: return full spec) */
int32_t getbal_fn(SPEC_t *sp, int32_t maxlen)
{
    if (sp->l > maxlen) sp->l = maxlen;
    return sp->l;
}

/*====================================================================================================================*/
/* intspc_fn — format DESCR integer value into spec */
void intspc_fn(SPEC_t *sp, DESCR_t dp)
{
    int32_t off = D_A(FRSGPT);
    char *buf = (char *)A2P(off);
    int len = snprintf(buf, 32, "%d", (int)D_A(dp));
    D_A(FRSGPT) += (int32_t)(len + 1);
    sp->a = off; sp->o = 0; sp->l = len;
}

/*====================================================================================================================*/
/* realst_fn — format DESCR real value into spec */
void realst_fn(SPEC_t *sp, DESCR_t dp)
{
    int32_t off = D_A(FRSGPT);
    char *buf = (char *)A2P(off);
    real_t r = *(real_t *)&dp.a.i;
    int len = snprintf(buf, 32, "%g", (double)r);
    D_A(FRSGPT) += (int32_t)(len + 1);
    sp->a = off; sp->o = 0; sp->l = len;
}

/*====================================================================================================================*/
/* stream_fn — takes (res, src, table*) — wrapper for STREAM_fn */
int32_t stream_fn(SPEC_t *res, const SPEC_t *src, const DESCR_t *table)
{
    SPEC_t s = *src;
    int stype = 0;
    RESULT_t rc = STREAM_fn(res, &s, (DESCR_t *)table, &stype);
    return (rc == OK) ? stype : 0;
}

/*====================================================================================================================*/
/* clertb_fn — takes (table*, fill_DESCR) where fill identifies action */
void clertb_fn(DESCR_t *tbl_descr, DESCR_t fill)
{
    struct syntab *tp = lookup_tbl(tbl_descr); /* fill.a.i == 0 → CONTIN; fill.a.i == AC_STOPSH+1 → STOPSH */
    enum action act = (enum action)fill.a.i;
    int j = 0;
    if (act != AC_CONTIN) {
        int i; for (i = 0; ; i++) if (tp->actions[i].act == act) { j=i+1; break; }
    }
    memset(tp->chrs, j, CHARSET);
}

/*====================================================================================================================*/
/* plugtb_fn — takes (table*, sentinel_DESCR, chars_spec) */
void plugtb_fn(DESCR_t *tbl_descr, DESCR_t sentinel, const SPEC_t *chars)
{
    struct syntab *tp = lookup_tbl(tbl_descr);
    enum action act = (enum action)sentinel.a.i;
    int j = 0;
    if (act != AC_CONTIN) {
        int i; for (i = 0; ; i++) if (tp->actions[i].act == act) { j=i+1; break; }
    }
    const unsigned char *p = (const unsigned char *)A2P(chars->a) + chars->o;
    int rem = chars->l;
    while (rem-- > 0) tp->chrs[*p++] = (unsigned char)j;
}

/*====================================================================================================================*/
/* xany_fn — any() for pattern matching */
int xany_fn(const SPEC_t *sp, const DESCR_t *dp)
{
    if (sp->l == 0) return 0;
    unsigned char c = *((const unsigned char *)A2P(sp->a) + sp->o);
    DESCR_t *vp = (DESCR_t *)A2P(D_A(*dp));
    const unsigned char *cp = (const unsigned char *)A2P(D_A(*dp)) + BCDFLD;
    int i = (int)D_V(*vp);
    while (i-- > 0) if (*cp++ == c) return 1;
    return 0;
}

/*====================================================================================================================*/
/* CONTIN / STOPSH as DESCR_t globals (action code in .a.i) */
DESCR_t CONTIN = {{.i=(int_t)AC_CONTIN}, 0, 0};
DESCR_t STOPSH = {{.i=(int_t)AC_STOPSH}, 0, 0};

/* DTREP_fn2/3 — format a DESCR as type-name string (stub) */
RESULT_t DTREP_fn2(DESCR_t *out, DESCR_t obj)
{
    (void)obj;
    static char buf[] = "?";
    out->a.i = P2A(buf); out->f = 0; out->v = S;
    return OK;
}

/*====================================================================================================================*/
RESULT_t DTREP_fn3(DESCR_t *out, DESCR_t obj)
{
    (void)obj;
    static char buf[] = "?";
    out->a.i = P2A(buf); out->f = 0; out->v = S;
    return OK;
}

/*====================================================================================================================*/
/* LOAD2_fn — dynamic load stub */
RESULT_t LOAD2_fn(void) { return FAIL; }

/* PSTACK_fn */
void PSTACK_fn(DESCR_t *pos) { (void)pos; }

/* VPXPTR_fn2 */
void VPXPTR_fn2(void) { }

/* PAD_fn — pad string */
void PAD_fn(int32_t dir, SPEC_t *out, SPEC_t *subj, SPEC_t *pad)
{
    (void)dir; (void)pad;
    *out = *subj; /* stub: return subject unchanged */
}

/*====================================================================================================================*/
/* KEYT_fn */
RESULT_t KEYT_fn(void) { return FAIL; }

/* ARGINT_fn */
RESULT_t ARGINT_fn(DESCR_t fn, DESCR_t n) { (void)fn; (void)n; return FAIL; }

/* SHORTN_fn */
void SHORTN_fn(SPEC_t *sp, int32_t n) { if (sp->l > n) sp->l = n; }

/* deql_fn — descriptor equality (removed from earlier stub pass, re-added) */
int deql_fn(DESCR_t a, DESCR_t b)
{
    return (D_A(a) == D_A(b) && a.f == b.f && a.v == b.v);
}

/* ── Scan function-code descriptors [v311.sil §24: DESCR Xfoo,0,narg]       */
/* A-field = integer opcode (XSUCF etc); flag=0; v=arg count.               */
DESCR_t SUCFFN = {.a={.i=XSUCF}, .f=0, .v=2}; /* SUCCEED              */
DESCR_t SALFFN = {.a={.i=XSALF}, .f=0, .v=2}; /* SALF link            */
DESCR_t SCOKFN = {.a={.i=XSCOK}, .f=0, .v=2}; /* scan-ok continuation */
DESCR_t STARFN = {.a={.i=XSTAR}, .f=0, .v=3}; /* *X expression        */
DESCR_t FNCEFN = {.a={.i=XFNCE}, .f=0, .v=2}; /* FENCE                */
DESCR_t RTBFN  = {.a={.i=XRTB},  .f=0, .v=3}; /* RTAB(n)              */
DESCR_t DSARFN = {.a={.i=XDSAR}, .f=0, .v=3}; /* deferred expression  */
/* Scan fn DESCRs with X-opcodes (flag=0, a=opcode, v=arity) */
DESCR_t ANYCFN = {.a={.i=XANYC},  .f=0, .v=3}; /* ANY(cset)            */
DESCR_t ATOPFN = {.a={.i=XATP},   .f=0, .v=3}; /* @X cursor capture    */
DESCR_t CHRFN  = {.a={.i=XCHR},   .f=0, .v=3}; /* single char match    */
DESCR_t BRKCFN = {.a={.i=XBRKC},  .f=0, .v=3}; /* BREAK(cset)          */
DESCR_t BRXCFN = {.a={.i=XBRKX},  .f=0, .v=3}; /* BREAKX(cset)         */
DESCR_t BRFCFN = {.a={.i=XBRKXF}, .f=0, .v=2}; /* BREAKX rematching    */
DESCR_t DNMEFN = {.a={.i=XDNME},  .f=0, .v=2}; /* $ immediate assign   */
DESCR_t DNMIFN = {.a={.i=XDNME1}, .f=0, .v=2}; /* $ imm alternate      */
DESCR_t ENMEFN = {.a={.i=XENME},  .f=0, .v=3}; /* . cond assign expr   */
DESCR_t ENMIFN = {.a={.i=XENMI},  .f=0, .v=3}; /* . imm cond assign    */
DESCR_t FNMEFN = {.a={.i=XFNME},  .f=0, .v=2}; /* fn call in pattern   */
DESCR_t LNTHFN = {.a={.i=XLNTH},  .f=0, .v=3}; /* LEN(n)               */
DESCR_t NMEFN  = {.a={.i=XNME},   .f=0, .v=2}; /* . cond value assign  */
DESCR_t NNYCFN = {.a={.i=XNNYC},  .f=0, .v=3}; /* NOTANY(cset)         */
DESCR_t POSIFN = {.a={.i=XPOSI},  .f=0, .v=3}; /* POS(n)               */
DESCR_t RPSIFN = {.a={.i=XRPSI},  .f=0, .v=3}; /* RPOS(n)              */
DESCR_t SCFLFN = {.a={.i=XFAIL},  .f=0, .v=2}; /* scan fail            */
DESCR_t SCONFN = {.a={.i=XSCON},  .f=0, .v=2}; /* scan continue        */

/* SCANFN/SJSRFN/ASGNFN/CONFN: declared in operator-fn block above (before init_syntab) */
DESCR_t SPNCFN = {.a={.i=XSPNC},  .f=0, .v=3}; /* SPAN(cset)           */
DESCR_t TBFN   = {.a={.i=XTB},    .f=0, .v=3}; /* TAB(n)               */
DESCR_t FNCFFN = {.a={.i=XRTNL3}, .f=0, .v=2}; /* FENCE failure        */
/* Trace/literal fn DESCRs (a=fn-ptr filled at init, flag=0) */
DESCR_t AREFN   = {.a={.i=0}, .f=FNC, .v=1}; /* array/table ref (ITEM_fn)  */
DESCR_t BASEFN  = {.a={.i=0}, .f=0,   .v=0}; /* base obj code   (BASE_fn)  */
DESCR_t CMAFN   = {.a={.i=0}, .f=FNC, .v=1}; /* CMA             (CMA_fn)   */
DESCR_t ENDAFN  = {.a={.i=0}, .f=0,   .v=0}; /* trace safety    (ARGNER_fn)*/
DESCR_t ENDFN   = {.a={.i=0}, .f=0,   .v=0}; /* end of program  (END_fn)   */
DESCR_t ERORFN  = {.a={.i=0}, .f=0,   .v=1}; /* erroneous stmt  (EROR_fn)  */
DESCR_t FNTRFN  = {.a={.i=0}, .f=0, .v=2}; /* call trace  (FENTR_fn)   */
DESCR_t FXTRFN  = {.a={.i=0}, .f=0, .v=2}; /* return trace(FNEXTR_fn)  */
DESCR_t GOTGFN  = {.a={.i=0}, .f=0, .v=1}; /* :<X> goto   (GOTG_fn)    */
DESCR_t GOTLFN  = {.a={.i=0}, .f=0, .v=1}; /* :(L) goto   (GOTL_fn)    */
DESCR_t GOTOFN  = {.a={.i=0}, .f=0, .v=1}; /* goto handler (GOTO_fn)   */
DESCR_t INITFN  = {.a={.i=0}, .f=0, .v=1}; /* stmt init   (INIT_fn)    */
DESCR_t KEYTFN = {.a={.i=0}, .f=0, .v=2}; /* keyword trace (KEYTR_fn) */
DESCR_t LABTFN = {.a={.i=0}, .f=0, .v=2}; /* label trace (LABTR_fn)   */

DESCR_t VLTRFN = {.a={.i=0}, .f=0, .v=2}; /* value trace (VALTR_fn)   */
DESCR_t BALFN  = {.a={.i=XBAL},  .f=0, .v=2}; /* BAL match            */
DESCR_t BALFFN = {.a={.i=XBALF}, .f=0, .v=2}; /* BAL failure          */
DESCR_t EARBFN = {.a={.i=XEARB},  .f=0, .v=2}; /* ARB extended         */
DESCR_t ARBNFN = {.a={.i=XARBN},  .f=0, .v=2}; /* ARBNO                */
DESCR_t ARBFFN = {.a={.i=XARBF},  .f=0, .v=2}; /* ARB failure          */
DESCR_t FARBFN = {.a={.i=XFARB},  .f=0, .v=2}; /* ARB first try        */
DESCR_t ONARFN = {.a={.i=XONAR},  .f=0, .v=2}; /* ARBNO predecessor    */
DESCR_t ONRFFN = {.a={.i=XONRF},  .f=0, .v=2}; /* ARBNO predecessor rf */
DESCR_t ABORFN = {.a={.i=XRTNL3}, .f=0, .v=3}; /* ABORT (rtnl3)        */

/* ── Primitive pattern nodes [v311.sil §24] ──────────────────────────────── */
/* Slot[0]: self-ptr (A filled by init_syntab) / TTL|MARK / size-in-bytes    */
/* Slot[1]: fn-code ptr (A = P2A(&XxxFN) filled by init_syntab) / FNC / 2-3 */
/* FAILPT — FAIL: 3*DESCR body (4 slots total)                               */
DESCR_t FAILPT[4] = {
    {.a={.i=0}, .f=TTL|MARK, .v=3*DESCR},  /* [0] hdr: self-ptr filled at init */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] SALFFN ptr filled at init    */
    {0}, {0}
};
/* FNCEPT — FENCE: 3*DESCR body (4 slots total)                              */
DESCR_t FNCEPT[4] = {
    {.a={.i=0}, .f=TTL|MARK, .v=3*DESCR},  /* [0] hdr: self-ptr filled at init */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] FNCEFN ptr filled at init    */
    {0}, {0}
};
/* REMPT — RTAB: 4*DESCR body (5 slots total); slot[4].v=I (integer type)    */
DESCR_t REMPT[5] = {
    {.a={.i=0}, .f=TTL|MARK, .v=4*DESCR},  /* [0] hdr: self-ptr filled at init */
    {.a={.i=0}, .f=FNC,      .v=3},         /* [1] RTBFN ptr filled at init     */
    {0}, {0},
    {.a={.i=0}, .f=0,        .v=I}          /* [4] integer-type slot            */
};
/* STARPT — * expression: 11*DESCR body (12 slots total)                     */
/* Internal offsets are relative to STARPT base (oracle uses ptr arithmetic)  */
DESCR_t STARPT[12] = {
    {.a={.i=0}, .f=TTL|MARK, .v=11*DESCR}, /* [0]  hdr: self-ptr at init       */
    {.a={.i=0}, .f=FNC,      .v=3},         /* [1]  STARFN ptr at init          */
    {.a={.i=0}, .f=0,        .v=4*DESCR},   /* [2]  nval offset = 4*DESCR       */
    {.a={.i=1}, .f=0,        .v=0},          /* [3]  counter = 1                 */
    {0},                                     /* [4]  (zero)                      */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [5]  SCOKFN ptr at init          */
    {.a={.i=7*DESCR}, .f=0,  .v=0},          /* [6]  success-link: A=7*DESCR     */
    {0},                                     /* [7]  (zero)                      */
    {.a={.i=0}, .f=FNC,      .v=3},         /* [8]  DSARFN ptr at init          */
    {.a={.i=0}, .f=0,        .v=4*DESCR},   /* [9]  nval offset = 4*DESCR       */
    {0}, {0}                                 /* [10][11] (zero)                  */
};
/* SUCCPT — SUCCEED: 3*DESCR body (4 slots total)                            */
DESCR_t SUCCPT[4] = {
    {.a={.i=0}, .f=TTL|MARK, .v=3*DESCR},  /* [0] hdr: self-ptr filled at init */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] SUCFFN ptr filled at init    */
    {0}, {0}
};

/* ── Trace pair lists — 3-slot each (header + 2 zero slots) [v311.sil §24] */
/* A-field (self-ptr) filled by init_syntab(); F=TTL|MARK; V=2*DESCR        */
DESCR_t TVALPL[3] = { {.a={.i=0},.f=TTL|MARK,.v=2*DESCR}, {0}, {0} };
DESCR_t TLABPL[3] = { {.a={.i=0},.f=TTL|MARK,.v=2*DESCR}, {0}, {0} };
DESCR_t TFENPL[3] = { {.a={.i=0},.f=TTL|MARK,.v=2*DESCR}, {0}, {0} };
DESCR_t TFEXPL[3] = { {.a={.i=0},.f=TTL|MARK,.v=2*DESCR}, {0}, {0} };
DESCR_t TKEYPL[3] = { {.a={.i=0},.f=TTL|MARK,.v=2*DESCR}, {0}, {0} };

/* VALBLK — value-type discriminator: 6*DESCR body (7 slots) [v311.sil §24 line 12141] */
/* [1].v=S (STRING=1), [3].v=N (NAME=9), [5].v=K (KEYWORD=10); evens are 0-offsets   */
DESCR_t VALBLK[7] = {
    {.a={.i=0}, .f=TTL|MARK, .v=6*DESCR},  /* [0] hdr: self-ptr filled at init */
    {.a={.i=0}, .f=0,        .v=S},         /* [1] STRING type code             */
    {.a={.i=0}, .f=0,        .v=0},         /* [2] 0 offset                     */
    {.a={.i=0}, .f=0,        .v=N},         /* [3] NAME type code               */
    {.a={.i=0}, .f=0,        .v=0},         /* [4] 0 offset                     */
    {.a={.i=0}, .f=0,        .v=K},         /* [5] KEYWORD (NAME) type code     */
    {.a={.i=0}, .f=0,        .v=0},         /* [6] 0 offset                     */
};

/* BALPT — BAL: 9*DESCR body (10 slots total) [v311.sil line 12080]         */
/* Slots [5],[8]: A=6*DESCR (relative link offset within node)               */
DESCR_t BALPT[10] = {
    {.a={.i=0}, .f=TTL|MARK, .v=9*DESCR},  /* [0] hdr: self-ptr at init        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] SCOKFN ptr at init           */
    {.a={.i=0}, .f=0,        .v=3*DESCR},   /* [2] nval offset V=3*DESCR        */
    {0},                                     /* [3] zero                         */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [4] BALFN ptr at init            */
    {.a={.i=6*DESCR}, .f=0,  .v=0},         /* [5] link A=6*DESCR               */
    {0},                                     /* [6] zero                         */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [7] BALFFN ptr at init           */
    {.a={.i=6*DESCR}, .f=0,  .v=0},         /* [8] link A=6*DESCR               */
    {0}                                      /* [9] zero                         */
};
/* ARTAL — ARB tail: 6*DESCR body (7 slots) [v311.sil line 12072]           */
DESCR_t ARTAL[7] = {
    {.a={.i=0}, .f=TTL|MARK, .v=6*DESCR},  /* [0] hdr: self-ptr at init        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] EARBFN ptr at init           */
    {.a={.i=0}, .f=0,        .v=3*DESCR},   /* [2] nval V=3*DESCR               */
    {0},                                     /* [3] zero                         */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [4] SCOKFN ptr at init           */
    {.a={.i=6*DESCR}, .f=0,  .v=0},         /* [5] link A=6*DESCR               */
    {0}                                      /* [6] zero                         */
};
/* ARHED — ARB head: 12*DESCR body (13 slots) [v311.sil line 12058]         */
/* slot[8]: A=9*DESCR, V=12*DESCR (both set per oracle)                     */
DESCR_t ARHED[13] = {
    {.a={.i=0}, .f=TTL|MARK, .v=12*DESCR}, /* [0]  hdr: self-ptr at init       */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1]  SCOKFN ptr at init          */
    {.a={.i=0}, .f=0,        .v=3*DESCR},   /* [2]  V=3*DESCR                   */
    {0},                                     /* [3]  zero                        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [4]  SCOKFN ptr at init          */
    {.a={.i=6*DESCR}, .f=0,  .v=0},         /* [5]  A=6*DESCR                   */
    {0},                                     /* [6]  zero                        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [7]  ARBNFN ptr at init          */
    {.a={.i=9*DESCR}, .f=0,  .v=12*DESCR},  /* [8]  A=9*DESCR, V=12*DESCR       */
    {0},                                     /* [9]  zero                        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [10] ARBFFN ptr at init          */
    {0}, {0}                                 /* [11][12] zero                    */
};
/* ARBPT — ARB: 9*DESCR body (10 slots) [v311.sil line 12047]               */
DESCR_t ARBPT[10] = {
    {.a={.i=0}, .f=TTL|MARK, .v=9*DESCR},  /* [0] hdr: self-ptr at init        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] SCOKFN ptr at init           */
    {.a={.i=0}, .f=0,        .v=3*DESCR},   /* [2] V=3*DESCR                    */
    {0},                                     /* [3] zero                         */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [4] SCOKFN ptr at init           */
    {.a={.i=6*DESCR}, .f=0,  .v=0},         /* [5] A=6*DESCR                    */
    {0},                                     /* [6] zero                         */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [7] FARBFN ptr at init           */
    {.a={.i=6*DESCR}, .f=0,  .v=0},         /* [8] A=6*DESCR                    */
    {0}                                      /* [9] zero                         */
};
/* ARBAK — ARBNO back: 6*DESCR body (7 slots) [v311.sil line 12039]         */
/* slot[2]: A=3*DESCR (link), not V */
DESCR_t ARBAK[7] = {
    {.a={.i=0}, .f=TTL|MARK, .v=6*DESCR},  /* [0] hdr: self-ptr at init        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] ONARFN ptr at init           */
    {.a={.i=3*DESCR}, .f=0,  .v=0},         /* [2] A=3*DESCR                    */
    {0},                                     /* [3] zero                         */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [4] ONRFFN ptr at init           */
    {0}, {0}                                 /* [5][6] zero                      */
};
/* ABORPT — ABORT: 3*DESCR body (4 slots) [v311.sil line 12034]             */
DESCR_t ABORPT[4] = {
    {.a={.i=0}, .f=TTL|MARK, .v=3*DESCR},  /* [0] hdr: self-ptr at init        */
    {.a={.i=0}, .f=FNC,      .v=2},         /* [1] ABORFN ptr at init           */
    {0}, {0}
};
