/*
 * sil_types.h — SIL types, structs, flags, and equates
 *
 * Faithful C translation of v311.sil §1 "Linkage and Equivalences"
 * (Phil Budne, CSNOBOL4 2.3.3, lines 829–953).
 *
 * Platform: 32-bit clean on 64-bit host (-m32).
 *   int_t  = int32_t   (SIL A field: integer value or arena offset)
 *   real_t = float     (SIL real: 32-bit, matching original)
 *
 * Arena model: one flat 128 MB mmap slab. All pointer-valued A fields
 * hold 32-bit byte offsets from arena_base, not raw pointers.
 *   A2P(off) — arena offset -> raw pointer
 *   P2A(ptr) — raw pointer  -> arena offset
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M0
 */

#ifndef SIL_TYPES_H
#define SIL_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ── Arena ────────────────────────────────────────────────────────────── */

#define ARENA_SIZE  (128 * 1024 * 1024)   /* 128 MB flat arena */

extern char *arena_base;                   /* set once in arena_init() */

#define A2P(off)   ((void *)((char *)arena_base + (int32_t)(off)))
#define P2A(ptr)   ((int32_t)((char *)(ptr) - arena_base))

/* ── Primitive types (machine-dependent parameters) ──────────────────── */
/*
 * v311.sil COPY PARMS selects these per platform.
 * We fix them: 32-bit integers, 32-bit float, matching original SIL.
 */

typedef int32_t  int_t;    /* A field: integer value or arena offset      */
typedef float    real_t;   /* real value (32-bit)                         */

/* ── DESCR_t — the universal SIL cell ────────────────────────────────── */
/*
 * v311.sil (snotypes.h): struct descr { union addr a; FFLD(f); VFLD(v); }
 *
 * A field: integer value, arena offset of a block, or real bits.
 *          In SIL: "address" field. We keep a union so real values
 *          can be stored without type-punning through int32_t.
 * F field: flag byte. Bits: FNC TTL STTL MARK PTR FRZN.
 * V field: data type code (S=1..DATSTA=100+) or block size in bytes.
 *
 * Packed to match SIL layout: a(4) f(1) v(4) = 9 bytes natural,
 * but we use __attribute__((packed)) to match the SIL 9-byte cell.
 * On 32-bit -m32 the compiler aligns to 4 anyway; we force pack
 * so DESCR arithmetic (INCRA OCICL,DESCR etc.) stays exact.
 */

typedef struct __attribute__((packed)) {
    union {
        int_t   i;   /* integer value or arena offset */
        real_t  f;   /* real value                    */
    } a;
    uint8_t     f;   /* flags — FNC TTL STTL MARK PTR FRZN */
    int_t       v;   /* type code or size             */
} DESCR_t;

#define DESCR  ((int)sizeof(DESCR_t))      /* = 9 bytes packed */
#define CPD    DESCR                        /* chars per descriptor (v311.sil CPD = DESCR) */
#define CPA    1                            /* chars per address unit */

/* ── SPEC_t — string specifier (two adjacent DESCRs in SIL) ─────────── */
/*
 * v311.sil: struct spec { union addr l; FFLD(unused); VFLD(o);
 *                         union addr a; FFLD(f); VFLD(v); }
 * l = length, o = offset into string block, a = arena offset of block,
 * v = type (S=1 for real strings), f = flags (MBZ for specifiers).
 * Size = 2 * DESCR.
 */

typedef struct __attribute__((packed)) {
    int_t       l;   /* length of substring           */
    uint8_t     unused; /* MBZ                        */
    int_t       o;   /* byte offset into string block */
    int_t       a;   /* arena offset of STRING block  */
    uint8_t     f;   /* flags (MBZ)                   */
    int_t       v;   /* type (S_TYPE for strings)     */
} SPEC_t;

#define SPEC   ((int)sizeof(SPEC_t))        /* = 2 * DESCR */

/* Convenience: pointer to string bytes from a SPEC */
#define SP_PTR(sp)   ((char *)A2P((sp)->a) + (sp)->o)
#define SP_LEN(sp)   ((sp)->l)

/* ── Flags (F field bits) — verbatim from v311.sil ───────────────────── */

#define FNC   01    /* function / code pointer — INVK1 tests this         */
#define TTL   02    /* block title (header descriptor)                     */
#define STTL  04    /* string block title                                  */
#define MARK  010   /* GC mark bit                                         */
#define PTR   020   /* A field contains an arena pointer (needs relocation) */
#define FRZN  040   /* frozen table [PLB34]                                */

/* ── Data type codes — verbatim from v311.sil lines 900–915 ─────────── */
/*
 * These are stored in the V field of a DESCR to identify the type
 * of the value in the A field (or the size of a block header).
 */

#define S      1    /* STRING                                              */
#define B      2    /* BLOCK (internal)                                    */
#define P      3    /* PATTERN                                             */
#define A      4    /* ARRAY                                               */
#define T      5    /* TABLE                                               */
#define I      6    /* INTEGER                                             */
#define R      7    /* REAL                                                */
#define C      8    /* CODE                                                */
#define N      9    /* NAME                                                */
#define K      10   /* KEYWORD (NAME)                                      */
#define E      11   /* EXPRESSION                                          */
#define L      12   /* LINKED STRING (internal)                            */
#define M      14   /* Malloc'd Linked String [PLB130]                     */
#define DATSTA 100  /* First user datatype [PLB31]                         */

/* BL (BLOCKS) omitted — §20 BLOCKS is skipped entirely */

/* ── Token type codes (Equivalences) — verbatim from v311.sil ───────── */

#define ARYTYP  7   /* Array reference                                     */
#define CLNTYP  5   /* Goto field                                          */
#define CMATYP  2   /* Comma                                               */
#define CMTTYP  2   /* Comment card                                        */
#define CNTTYP  4   /* Continue card                                       */
#define CTLTYP  3   /* Control card                                        */
#define DIMTYP  1   /* Dimension separator                                 */
#define EOSTYP  6   /* End of statement                                    */
#define EQTYP   4   /* Equal sign                                          */
#define FGOTYP  3   /* Failure goto                                        */
#define FTOTYP  6   /* Failure direct goto                                 */
#define FLITYP  6   /* Literal real                                        */
#define FNCTYP  5   /* Function call                                       */
#define ILITYP  2   /* Literal integer                                     */
#define LPTYP   1   /* Left parenthesis                                    */
#define NBTYP   1   /* Nonbreak character                                  */
#define NEWTYP  1   /* New statement                                       */
#define NSTTYP  4   /* Parenthesized expression                            */
#define QLITYP  1   /* Quoted literal                                      */
#define RBTYP   7   /* Right bracket                                       */
#define RPTYP   3   /* Right parenthesis                                   */
#define SGOTYP  2   /* Success goto                                        */
#define STOTYP  5   /* Success direct goto                                 */
#define UGOTYP  1   /* Unconditional goto                                  */
#define UTOTYP  4   /* Unconditional direct goto                           */
#define VARTYP  3   /* Variable                                            */

/* ── Constants (EQUs) — verbatim from v311.sil §1 ───────────────────── */
/*
 * DESCR-relative offsets: expressed as multiples of DESCR bytes.
 * This matches exactly the SIL "EQU N*DESCR" form.
 * Example: ATTRIB EQU 2*DESCR means the attrib field is at
 *          byte offset 2*DESCR from the block's title descriptor.
 */

/* String block field offsets */
#define ATTRIB  (2 * DESCR)   /* Offset of label in string structure       */
#define LNKFLD  (3 * DESCR)   /* Offset of link in string structure        */
#define BCDFLD  (4 * DESCR)   /* Offset of string bytes in string struct   */

/* Code node field offsets */
#define FATHER  (1 * DESCR)   /* Offset of father in code node             */
#define LSON    (2 * DESCR)   /* Offset of left son in code node           */
#define RSIB    (3 * DESCR)   /* Offset of right sibling in code node      */
#define CODE    (4 * DESCR)   /* Offset of code in code node               */

/* Size limits */
#define ESASIZ   50           /* Limit on number of syntactic errors        */
#define FBKLSZ  (10 * DESCR)  /* Size of function descriptor block          */
#define ARRLEN   20           /* Limit on array print image length          */
#define CARDSZ   1024         /* Width of compiler input [PLB21][PLB57]     */
#define STNOSZ   8            /* Length of statement number field           */
#define DSTSZ   (2 * STNOSZ)  /* Space for left and right numbering         */
#define CNODSZS (4 * DESCR)   /* Size of code node                          */
#define DATSIZ   1000         /* Limit on number of defined data types      */
#define EXTSIZ   10           /* Default allocation for tables              */
#define NAMLSZ   20           /* Growth quantum for name list               */
#define NODESZ  (3 * DESCR)   /* Size of pattern node                       */
#define SIZLIM   0x7fffffff   /* Maximum object size (31 bits)              */

/* Hash table */
#define OBSFT    13           /* Power of two for bin headers [PLB75]       */
#define OBSIZ   (1 << OBSFT)  /* Number of bin headers = 8192 [PLB75]       */
#define OBARY   (OBSIZ + 3)   /* Total number of bins                       */
#define OBOFF   (OBSIZ - 2)   /* Offset length in bins                      */

/* Stack and buffer sizes */
#define OCASIZ   1500         /* Descriptors of initial object code         */
#define SPDLSZ   8000         /* Descriptors of pattern stack [PLB104]      */
#define STSIZE   4000         /* Descriptors of interpreter stack [PLB104]  */
#define SPDR    (SPEC + DESCR)/* Descriptor plus specifier                  */
#define VLRECL   0            /* Variable length record length [PLB129]     */

/* ── Pattern function dispatch indexes — verbatim from v311.sil ──────── */
/*
 * These are the XATP, XARBN etc. constants used by PATBRA (§11)
 * to dispatch to the correct pattern matching sub-procedure.
 */

#define XANYC    1    /* ANY(cset)                                          */
#define XARBF    2    /* ARB first (0-char try)                             */
#define XARBN    3    /* ARBNO                                              */
#define XATP     4    /* @ cursor capture                                   */
#define XCHR     5    /* single character match                             */
#define XBAL     6    /* BAL                                                */
#define XBALF    7    /* BAL failure                                        */
#define XBRKC    8    /* BREAK(cset)                                        */
#define XBRKX    9    /* BREAKX                                             */
#define XBRKXF   10   /* BREAKX failure                                     */
#define XDNME    11   /* $ immediate value assignment                       */
#define XDNME1   12   /* $ immediate (alternate form)                       */
#define XEARB    13   /* ARB extended (non-zero advance)                    */
#define XDSAR    14   /* deferred expression in pattern                     */
#define XENME    15   /* . conditional assignment (expression target)       */
#define XENMI    16   /* . immediate conditional assignment                 */
#define XFARB    17   /* ARB first (0-char try)                             */
#define XFNME    18   /* function call in pattern                           */
#define XLNTH    19   /* LEN(n)                                             */
#define XNME     20   /* . conditional value assignment                     */
#define XNNYC    21   /* NOTANY(cset)                                       */
#define XONAR    22   /* ARBNO predecessor node                             */
#define XONRF    23   /* ARBNO predecessor (reset form)                     */
#define XPOSI    24   /* POS(n)                                             */
#define XRPSI    25   /* RPOS(n)                                            */
#define XRTB     26   /* RTAB(n)                                            */
#define XFAIL    27   /* FAIL                                               */
#define XSALF    28   /* success/alternate link                             */
#define XSCOK    29   /* scan success continuation                          */
#define XSCON    30   /* scan continue                                      */
#define XSPNC    31   /* SPAN(cset)                                         */
#define XSTAR    32   /* * unevaluated expression                           */
#define XTB      33   /* TAB(n)                                             */
#define XRTNL3   34   /* return null (3-way)                                */
#define XFNCE    35   /* FENCE                                              */
#define XSUCF    36   /* SUCCEED                                            */

/* ── SilResult — return code for SIL procedure translation ───────────── */
/*
 * SIL procedures return via RRTURN with an exit number (1, 2, 3...).
 * In C each translates to a typed return. Most procedures have two
 * exits: success (OK) and failure (FAIL). Where SIL has more exits
 * we use explicit integer values matching the SIL exit numbers.
 */

typedef enum {
    FAIL = 0,   /* SIL failure exit — :F branch taken, no result          */
    OK   = 1    /* SIL success exit — :S branch taken, result valid       */
} SilResult;

/* ── Convenience macros for DESCR_t access ───────────────────────────── */

/* Read/write the A field as integer (arena offset or integer value) */
#define D_A(d)    ((d).a.i)
/* Read/write the A field as real */
#define D_R(d)    ((d).a.f)
/* Read/write the F field (flags) */
#define D_F(d)    ((d).f)
/* Read/write the V field (type code or size) */
#define D_V(d)    ((d).v)

/* Type tests on DESCR_t */
#define IS_FNC(d)   (D_F(d) & FNC)
#define IS_TTL(d)   (D_F(d) & TTL)
#define IS_STR(d)   (D_V(d) == S)
#define IS_INT(d)   (D_V(d) == I)
#define IS_REAL(d)  (D_V(d) == R)
#define IS_PAT(d)   (D_V(d) == P)
#define IS_NAME(d)  (D_V(d) == N)
#define IS_KW(d)    (D_V(d) == K)
#define IS_EXPR(d)  (D_V(d) == E)
#define IS_CODE(d)  (D_V(d) == C)
#define IS_ARR(d)   (D_V(d) == A)
#define IS_TBL(d)   (D_V(d) == T)
#define IS_USER(d)  (D_V(d) >= DATSTA)

/* DESCR copy — mirrors SIL MOVD */
#define MOVD(dst, src)   ((dst) = (src))

/* Null / zero DESCR */
#define ZEROD  ((DESCR_t){.a={.i=0}, .f=0, .v=0})

#endif /* SIL_TYPES_H */
