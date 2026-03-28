/*
 * ir.h — Unified Intermediate Representation
 *
 * THE single source of truth for all IR node kinds across all frontends
 * and all backends in snobol4x. Every frontend lowers to EXPR_t nodes
 * using this EKind enum. Every backend consumes it.
 *
 * 59 canonical node kinds:
 *   5  Literals
 *   4  References
 *   7  Arithmetic
 *   3  Sequence / Alternation
 *   14 Pattern Primitives  (each has distinct Byrd box wiring)
 *   3  Captures
 *   5  Call / Access / Scan / Swap
 *   7  Icon Generators + Constructors
 *   6  Prolog
 *
 * Name heritage: E_ prefix = Expression node. Names derived from SIL
 * v311.sil xxxTYP token type codes (CSNOBOL4 2.3.3) where applicable.
 * See doc/ARCH-sil-heritage.md for full lineage.
 * See doc/IR_AUDIT.md for node-by-node Byrd box wiring notes.
 * See doc/SIL_NAMES_AUDIT.md for broader naming law.
 *
 * DO NOT add new node kinds here without Lon's explicit approval.
 * Protocol: evidence from emitter code that a distinct node is needed,
 * then add to this enum, then update all four backends.
 *
 * Produced by: Claude Sonnet 4.6 (G-7 session, 2026-03-28)
 * Milestone: M-G1-IR-HEADER-DEF
 */

#ifndef IR_H
#define IR_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * EKind — unified expression node kind enum
 * ========================================================================= */

typedef enum EKind {

    /* --- Literals -------------------------------------------------------- */

    E_QLIT,         /* Quoted string / pattern literal  (QLITYP=1 in SIL)   */
    E_ILIT,         /* Integer literal                  (ILITYP=2 in SIL)   */
    E_FLIT,         /* Float / real literal             (FLITYP=6 in SIL)   */
    E_CSET,         /* Cset literal (Icon / Rebus)                          */
    E_NUL,          /* Null / empty value               (was E_NULV)         */

    /* --- References ------------------------------------------------------ */

    E_VAR,          /* Variable reference               (VARTYP=3; was E_VART) */
    E_KW,           /* &IDENT keyword reference         (K=10 data type)     */
    E_INDR,         /* $expr  indirect / imm-assign target                  */
    E_DEFER,        /* *expr  deferred / indirect pattern ref (XSTAR=32; was E_STAR) */

    /* --- Arithmetic ------------------------------------------------------ */

    E_NEG,          /* Unary minus          (MNSR proc in SIL; was E_MNS)   */
    E_PLS,          /* Unary plus / numeric coerce  (PLS proc; '' → 0)      */
    E_ADD,          /* Addition                                              */
    E_SUB,          /* Subtraction                                           */
    E_MPY,          /* Multiplication                                        */
    E_DIV,          /* Division                                              */
    E_MOD,          /* Modulo / remainder                                    */
    E_POW,          /* Exponentiation       (EXR in SIL; was E_EXPOP)        */

    /* --- Sequence and Alternation ---------------------------------------- */

    E_SEQ,          /* Sequence / concat, n-ary (CONCAT/CONCL; was E_CONC)  */
    E_ALT,          /* Pattern alternation, n-ary (ORPP in SIL; was E_OR)   */
    E_OPSYN,        /* & operator: reduce(left, right)                      */

    /* --- Pattern Primitives ---------------------------------------------- */
    /*
     * Each primitive has distinct Byrd box wiring in emit_byrd_asm.c.
     * SIL X___ codes from equ.h confirm each is a separate dispatch case.
     * All 14 are required; none can be merged without emitter evidence.
     */

    E_ARB,          /* Arbitrary match           (XFARB=17; p$arb)          */
    E_ARBNO,        /* Zero-or-more              (XARBN=3;  p$arb)          */
    E_POS,          /* Cursor assert POS(n)      (XPOSI=24)                 */
    E_RPOS,         /* Right cursor RPOS(n)      (XRPSI=25)                 */
    E_ANY,          /* ANY(S) — match one from S (XANYC=1;  p$any)          */
    E_NOTANY,       /* NOTANY(S) — match one not in S  (XNNYC=21)           */
    E_SPAN,         /* SPAN(S) — longest run from S    (XSPNC=31; p$spn)    */
    E_BREAK,        /* BREAK(S) — up to char in S      (XBRKC=8;  p$brk)    */
    E_BREAKX,       /* BREAKX(S) — BREAK + backtrack   (XBRKX=9;  p$bkx)    */
    E_LEN,          /* LEN(N) — exactly N chars        (XLNTH=19; p$len)    */
    E_TAB,          /* TAB(N) — to cursor pos N        (XTB=33;   p$tab)    */
    E_RTAB,         /* RTAB(N) — to N from right       (XRTB=26;  p$rtb)    */
    E_REM,          /* REM — remainder of subject      (p$rem)              */
    E_FAIL,         /* FAIL — always fail              (XFAIL=27; p$fal)    */
    E_SUCCEED,      /* SUCCEED — always succeed        (XSUCF=36; p$suc)    */
    E_FENCE,        /* FENCE — succeed, seal β         (XFNCE=35)           */
    E_ABORT,        /* ABORT — abort entire match                           */
    E_BAL,          /* BAL — balanced parentheses      (XBAL=6;   p$bal)    */

    /* --- Captures -------------------------------------------------------- */

    E_CAPT_COND,    /* .var  conditional capture (on success) (was E_NAM)   */
    E_CAPT_IMM,     /* $var  immediate capture               (was E_DOL)    */
    E_CAPT_CUR,     /* @var  cursor position capture (XATP=4; was E_ATP)    */

    /* --- Call, Access, Assignment, Scan, Swap ---------------------------- */

    E_FNC,          /* Function call / goal / builtin, n-ary (FNCTYP=5)     */
    E_IDX,          /* Array / table / record subscript (ARYTYP=7; absorbs E_ARY) */
    E_ASSIGN,       /* Assignment  (ASGN proc in SIL; was E_ASGN)           */
    E_MATCH,        /* E ? E  scanning  (XSCON=30/SCONCL; was E_SCAN)       */
    E_SWAP,         /* :=:  swap bindings  (SWAP proc in SIL)               */

    /* --- Icon Generators and Constructors -------------------------------- */

    E_SUSPEND,      /* Generator suspend / yield                            */
    E_TO,           /* i to j  generator                                    */
    E_TO_BY,        /* i to j by k  generator                               */
    E_LIMIT,        /* E \ N  limitation                                    */
    E_GENALT,       /* Icon / Rebus alt generator, left-then-right (was E_ALT_GEN) */
    E_ITER,         /* !E  iterate list or string elements (was E_BANG)     */
    E_MAKELIST,     /* [e1,e2,...]  list constructor                        */

    /* --- Prolog ---------------------------------------------------------- */

    E_UNIFY,        /* =/2  unification with trail                          */
    E_CLAUSE,       /* Horn clause: head + body + EnvLayout                 */
    E_CHOICE,       /* Predicate choice point: α/β chain over clauses       */
    E_CUT,          /* !  cut / FENCE — seals β of enclosing choice         */
    E_TRAIL_MARK,   /* Save trail.top into env slot                         */
    E_TRAIL_UNWIND, /* Restore trail to saved mark                          */

    /* --- Sentinel -------------------------------------------------------- */

    E_KIND_COUNT    /* Total number of kinds — used for array sizing / asserts.
                     * NOT a valid node kind. Must remain last. */

} EKind;

/* =========================================================================
 * EXPR_t — unified n-ary expression node
 *
 * All structural children live in the `children` array (realloc-grown).
 * Leaf nodes (E_QLIT / E_ILIT / E_FLIT / E_CSET / E_NUL / E_VAR / E_KW)
 * have nchildren == 0.
 *
 * The `id` field is assigned during the emit pass (unique per program).
 * It drives all generated label strings: P_<id>_α, L<id>_α, etc.
 *
 * sval / ival / fval union is not a C union here — all three exist as
 * separate fields to avoid aliasing hazards across frontends. The active
 * field is determined by kind:
 *   sval — E_QLIT (text), E_VAR/E_KW/E_FNC/E_IDX (name), E_CSET (chars)
 *   ival — E_ILIT
 *   fval — E_FLIT
 * ========================================================================= */

/* EXPR_t — the unified IR node struct.
 *
 * M-G1-IR-HEADER-WIRE: sno2c.h provides its own EXPR_t for now (legacy
 * field names: dval, no nalloc/id).  When sno2c.h includes ir.h first it
 * defines EXPR_T_DEFINED to suppress this copy and avoid a redefinition
 * error.  Struct field unification is a later reorg milestone.
 */
#ifndef EXPR_T_DEFINED
typedef struct EXPR_t EXPR_t;

struct EXPR_t {
    EKind    kind;          /* node kind from EKind enum above              */
    char    *sval;          /* string payload (see comment above)           */
    long long ival;         /* integer payload                              */
    double   fval;          /* float payload                                */
    EXPR_t **children;      /* child nodes — realloc-grown array            */
    int      nchildren;     /* number of valid entries in children[]        */
    int      nalloc;        /* allocated capacity of children[]             */
    int      id;            /* unique node id — assigned at emit time       */
};
#endif /* EXPR_T_DEFINED */

/* =========================================================================
 * EKind name table — for ir_print.c and debugging
 * ========================================================================= */

#ifdef IR_DEFINE_NAMES

static const char * const ekind_name[E_KIND_COUNT] = {
    [E_QLIT]         = "E_QLIT",
    [E_ILIT]         = "E_ILIT",
    [E_FLIT]         = "E_FLIT",
    [E_CSET]         = "E_CSET",
    [E_NUL]          = "E_NUL",
    [E_VAR]          = "E_VAR",
    [E_KW]           = "E_KW",
    [E_INDR]         = "E_INDR",
    [E_DEFER]        = "E_DEFER",
    [E_NEG]          = "E_NEG",
    [E_PLS]          = "E_PLS",
    [E_ADD]          = "E_ADD",
    [E_SUB]          = "E_SUB",
    [E_MPY]          = "E_MPY",
    [E_DIV]          = "E_DIV",
    [E_MOD]          = "E_MOD",
    [E_POW]          = "E_POW",
    [E_SEQ]          = "E_SEQ",
    [E_ALT]          = "E_ALT",
    [E_OPSYN]        = "E_OPSYN",
    [E_ARB]          = "E_ARB",
    [E_ARBNO]        = "E_ARBNO",
    [E_POS]          = "E_POS",
    [E_RPOS]         = "E_RPOS",
    [E_ANY]          = "E_ANY",
    [E_NOTANY]       = "E_NOTANY",
    [E_SPAN]         = "E_SPAN",
    [E_BREAK]        = "E_BREAK",
    [E_BREAKX]       = "E_BREAKX",
    [E_LEN]          = "E_LEN",
    [E_TAB]          = "E_TAB",
    [E_RTAB]         = "E_RTAB",
    [E_REM]          = "E_REM",
    [E_FAIL]         = "E_FAIL",
    [E_SUCCEED]      = "E_SUCCEED",
    [E_FENCE]        = "E_FENCE",
    [E_ABORT]        = "E_ABORT",
    [E_BAL]          = "E_BAL",
    [E_CAPT_COND]    = "E_CAPT_COND",
    [E_CAPT_IMM]     = "E_CAPT_IMM",
    [E_CAPT_CUR]     = "E_CAPT_CUR",
    [E_FNC]          = "E_FNC",
    [E_IDX]          = "E_IDX",
    [E_ASSIGN]       = "E_ASSIGN",
    [E_MATCH]        = "E_MATCH",
    [E_SWAP]         = "E_SWAP",
    [E_SUSPEND]      = "E_SUSPEND",
    [E_TO]           = "E_TO",
    [E_TO_BY]        = "E_TO_BY",
    [E_LIMIT]        = "E_LIMIT",
    [E_GENALT]       = "E_GENALT",
    [E_ITER]         = "E_ITER",
    [E_MAKELIST]     = "E_MAKELIST",
    [E_UNIFY]        = "E_UNIFY",
    [E_CLAUSE]       = "E_CLAUSE",
    [E_CHOICE]       = "E_CHOICE",
    [E_CUT]          = "E_CUT",
    [E_TRAIL_MARK]   = "E_TRAIL_MARK",
    [E_TRAIL_UNWIND] = "E_TRAIL_UNWIND",
};

#endif /* IR_DEFINE_NAMES */

/* =========================================================================
 * Alias bridges — compatibility with pre-reorg sno2c.h names
 *
 * Added in M-G1-IR-HEADER-WIRE. Removed in Phase 5 as each frontend's
 * lower.c is updated to use canonical names directly.
 * DO NOT use these aliases in new code.
 * ========================================================================= */

#ifdef IR_COMPAT_ALIASES

#define E_NULV      E_NUL
#define E_VART      E_VAR
#define E_STAR      E_DEFER
#define E_MNS       E_NEG
#define E_EXPOP     E_POW
#define E_CONC      E_SEQ
#define E_OR        E_ALT
#define E_NAM       E_CAPT_COND
#define E_DOL       E_CAPT_IMM
#define E_ATP       E_CAPT_CUR
#define E_ARY       E_IDX
#define E_ASGN      E_ASSIGN
#define E_SCAN      E_MATCH
#define E_BANG      E_ITER
#define E_ALT_GEN   E_GENALT

#endif /* IR_COMPAT_ALIASES */

#ifdef __cplusplus
}
#endif

#endif /* IR_H */
