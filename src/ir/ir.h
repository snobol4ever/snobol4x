/*
 * ir.h — Unified Intermediate Representation
 *
 * THE single source of truth for all IR node kinds across all frontends
 * and all backends in scrip-cc. Every frontend lowers to EXPR_t nodes
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
 * See archive/doc/ARCH-sil-heritage.md for full lineage.
 * See archive/doc/IR_AUDIT.md for node-by-node Byrd box wiring notes.
 * See archive/doc/SIL_NAMES_AUDIT.md for broader naming law.
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
    E_KEYWORD,           /* &IDENT keyword reference         (K=10 data type)     */
    E_INDIRECT,         /* $expr  indirect / imm-assign target                  */
    E_DEFER,        /* *expr  deferred / indirect pattern ref (XSTAR=32; was E_STAR) */

    /* --- Arithmetic ------------------------------------------------------ */

    E_INTERROGATE,  /* ?X  interrogation: null if X succeeds, fail if X fails  (o$int)  */
    E_NAME,         /* .X  name reference: return name descriptor of X         (o$nam)  */
    E_MNS,          /* Unary minus          (MNS proc in SIL; o$com in MINIMAL) */
    E_PLS,          /* Unary plus / numeric coerce  (PLS proc in SIL; o$aff in MINIMAL) */
    E_ADD,          /* Addition                                              */
    E_SUB,          /* Subtraction                                           */
    E_MUL,          /* Multiplication                                        */
    E_DIV,          /* Division                                              */
    E_MOD,          /* Modulo / remainder                                    */
    E_POW,          /* Exponentiation       (EXR in SIL; was E_EXPOP)        */

    /* --- Sequence and Alternation ---------------------------------------- */

    E_SEQ,          /* Goal-directed sequence, n-ary — Byrd-box wiring       */
                    /* α→lα, lγ→rα, rω→lβ, rγ→γ. SNOBOL4 pattern CAT;      */
                    /* Icon ||/;/&/loop bodies. (CONCAT/CONCL in SIL)          */
    E_CAT,       /* Pure value-context string concat, n-ary, cannot fail  */
                    /* SNOBOL4 value ctx; JVM StringBuilder; .NET Concat.     */
                    /* M-G4-SPLIT-SEQ-CONCAT (2026-03-28).                   */
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

    E_CAPT_COND_ASGN,    /* .var  conditional capture (on success) (was E_NAM)   */
    E_CAPT_IMMED_ASGN,     /* $var  immediate capture               (was E_DOL)    */
    E_CAPT_CURSOR,     /* @var  cursor position capture (XATP=4; was E_ATP)    */

    /* --- Call, Access, Assignment, Scan, Swap ---------------------------- */

    E_FNC,          /* Function call / goal / builtin, n-ary (FNCTYP=5)     */
    E_IDX,          /* Array / table / record subscript (ARYTYP=7; absorbs E_ARY) */
    E_ASSIGN,       /* Assignment  (ASGN proc in SIL; was E_ASGN)           */
    E_SCAN,        /* E ? E  scanning  (XSCON=30/SCONCL; was E_SCAN)       */
    E_SWAP,         /* :=:  swap bindings  (SWAP proc in SIL)               */

    /* --- Icon Generators and Constructors -------------------------------- */

    E_SUSPEND,      /* Generator suspend / yield                            */
    E_TO,           /* i to j  generator                                    */
    E_TO_BY,        /* i to j by k  generator                               */
    E_LIMIT,        /* E \ N  limitation                                    */
    E_ALTERNATE,       /* Icon / Rebus alt generator, left-then-right (was E_ALT_GEN) */
    E_ITERATE,         /* !E  iterate list or string elements (was E_BANG)     */
    E_MAKELIST,     /* [e1,e2,...]  list constructor                        */

    /* --- Prolog ---------------------------------------------------------- */

    E_UNIFY,        /* =/2  unification with trail                          */
    E_CLAUSE,       /* Horn clause: head + body + EnvLayout                 */
    E_CHOICE,       /* Predicate choice point: α/β chain over clauses       */
    E_CUT,          /* !  cut / FENCE — seals β of enclosing choice         */
    E_TRAIL_MARK,   /* Save trail.top into env slot                         */
    E_TRAIL_UNWIND, /* Restore trail to saved mark                          */

    /* --- Icon: Numeric Relational ----------------------------------------
     * Goal-directed: succeed and yield rhs if condition holds, else fail.
     * Six distinct Byrd-box wiring patterns (each distinct comparison jump).
     * No SNOBOL4/Prolog equivalent — those use E_FNC("lt",...) dispatch.
     * M-G9-ICON-IR-WIRE (2026-03-30). */

    E_LT,           /* E1 < E2   (numeric less-than)                        */
    E_LE,           /* E1 <= E2  (numeric less-or-equal)                    */
    E_GT,           /* E1 > E2   (numeric greater-than)                     */
    E_GE,           /* E1 >= E2  (numeric greater-or-equal)                 */
    E_EQ,           /* E1 = E2   (numeric equality)                         */
    E_NE,           /* E1 ~= E2  (numeric not-equal)                        */

    /* --- Icon: Lexicographic (String) Relational -------------------------
     * L prefix = Lexicographic. Goal-directed semantics on string values.
     * E_L{LT,LE,GT,GE,EQ,NE} — parallel to E_{LT,LE,GT,GE} numeric relops. */

    E_LLT,          /* E1 << E2  (string less-than)                         */
    E_LLE,          /* E1 <<= E2 (string less-or-equal)                     */
    E_LGT,          /* E1 >> E2  (string greater-than)                      */
    E_LGE,          /* E1 >>= E2 (string greater-or-equal)                  */
    E_LEQ,         /* E1 == E2  (string equality;  ICN_SEQ)                */
    E_LNE,          /* E1 ~== E2 (string not-equal)                         */

    /* --- Icon: Cset Operators -------------------------------------------- */

    E_CSET_COMPL,   /* ~E       cset complement                             */
    E_CSET_UNION,   /* E1 ++ E2 cset union                                  */
    E_CSET_DIFF,    /* E1 -- E2 cset difference                             */
    E_CSET_INTER,   /* E1 ** E2 cset intersection                           */
    E_LCONCAT,      /* E1 ||| E2  list concatenation (distinct from || str) */

    /* --- Icon: Unary Operators ------------------------------------------- */

    E_NONNULL,      /* \E   succeed if E non-null, yield E's value          */
    E_NULL,         /* /E   succeed if E is null, yield &null               */
    E_NOT,          /* not E  succeed iff E fails                           */
    E_SIZE,         /* *E   size of string/list/table                       */
    E_RANDOM,       /* ?E   random element or integer in [1,E]              */
    E_IDENTICAL,    /* E1 === E2  object identity (same pointer)            */
    E_AUGOP,        /* E1 op:= E2  augmented assignment; op subtype in ival */

    /* --- Icon: Expression Sequence / Control Flow ------------------------ */

    E_SEQ_EXPR,     /* (E1; E2; ...; En) — evaluate all, result = last     */
    E_EVERY,        /* every E [do body]  — drive generator to exhaustion  */
    E_WHILE,        /* while E [do body]                                    */
    E_UNTIL,        /* until E [do body]                                    */
    E_REPEAT,       /* repeat body        — unconditional loop              */
    E_IF,           /* if E then E2 [else E3]                               */
    E_CASE,         /* case E of { clauses }                                */
    E_RETURN,       /* return [E]         — return from procedure           */
    E_LOOP_BREAK,   /* break [E]          — exit innermost loop
                     * NOTE: distinct from E_BREAK = SNOBOL4 BREAK(S)      */
    E_LOOP_NEXT,    /* next               — restart innermost loop          */
    E_BANG_BINARY,  /* E1 ! E2            — invoke E1 with list E2         */

    /* --- Icon: Structure / Declarations ---------------------------------- */

    E_SECTION,      /* E[i:j]   string section                              */
    E_SECTION_PLUS, /* E[i+:n]  section by length (forward)                */
    E_SECTION_MINUS,/* E[i-:n]  section by length (backward)               */
    E_RECORD,       /* record declaration                                   */
    E_FIELD,        /* E.name   field access                                */
    E_GLOBAL,       /* global varname  declaration                          */
    E_INITIAL,      /* initial { body }  once-on-first-call block          */

    /* --- Sentinel -------------------------------------------------------- */

    E_KIND_COUNT    /* Total number of kinds — used for array sizing / asserts.
                     * NOT a valid node kind. Must remain last. */

} EKind;

/* =========================================================================
 * EXPR_t — unified n-ary expression node
 *
 * All structural children live in the `children` array (realloc-grown).
 * Leaf nodes (E_QLIT / E_ILIT / E_FLIT / E_CSET / E_NUL / E_VAR / E_KEYWORD)
 * have nchildren == 0.
 *
 * The `id` field is assigned during the emit pass (unique per program).
 * It drives all generated label strings: P_<id>_α, L<id>_α, etc.
 *
 * sval / ival / fval union is not a C union here — all three exist as
 * separate fields to avoid aliasing hazards across frontends. The active
 * field is determined by kind:
 *   sval — E_QLIT (text), E_VAR/E_KEYWORD/E_FNC/E_IDX (name), E_CSET (chars)
 *   ival — E_ILIT
 *   fval — E_FLIT
 * ========================================================================= */

/* EXPR_t — the unified IR node struct.
 *
 * FI-0A: ir.h is the sole owner of this definition. scrip_cc.h no longer
 * carries a duplicate body. The EXPR_T_DEFINED guard has been removed —
 * this struct is defined exactly once, here.
 */
typedef struct EXPR_t EXPR_t;

struct EXPR_t {
    EKind    kind;          /* node kind from EKind enum above              */
    char    *sval;          /* string payload (see comment above)           */
    long long ival;         /* integer payload                              */
    double   dval;          /* float payload (named dval throughout codebase) */
    EXPR_t **children;      /* child nodes — realloc-grown array            */
    int      nchildren;     /* number of valid entries in children[]        */
    int      nalloc;        /* allocated capacity of children[]             */
    int      id;            /* unique node id — assigned at emit time       */
};

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
    [E_KEYWORD]           = "E_KEYWORD",
    [E_INDIRECT]         = "E_INDIRECT",
    [E_DEFER]        = "E_DEFER",
    [E_INTERROGATE]  = "E_INTERROGATE",
    [E_NAME]         = "E_NAME",
    [E_MNS]          = "E_MNS",
    [E_PLS]          = "E_PLS",
    [E_ADD]          = "E_ADD",
    [E_SUB]          = "E_SUB",
    [E_MUL]          = "E_MUL",
    [E_DIV]          = "E_DIV",
    [E_MOD]          = "E_MOD",
    [E_POW]          = "E_POW",
    [E_SEQ]          = "E_SEQ",
    [E_CAT]       = "E_CAT",
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
    [E_CAPT_COND_ASGN]    = "E_CAPT_COND_ASGN",
    [E_CAPT_IMMED_ASGN]     = "E_CAPT_IMMED_ASGN",
    [E_CAPT_CURSOR]     = "E_CAPT_CURSOR",
    [E_FNC]          = "E_FNC",
    [E_IDX]          = "E_IDX",
    [E_ASSIGN]       = "E_ASSIGN",
    [E_SCAN]        = "E_SCAN",
    [E_SWAP]         = "E_SWAP",
    [E_SUSPEND]      = "E_SUSPEND",
    [E_TO]           = "E_TO",
    [E_TO_BY]        = "E_TO_BY",
    [E_LIMIT]        = "E_LIMIT",
    [E_ALTERNATE]       = "E_ALTERNATE",
    [E_ITERATE]         = "E_ITERATE",
    [E_MAKELIST]     = "E_MAKELIST",
    [E_UNIFY]        = "E_UNIFY",
    [E_CLAUSE]       = "E_CLAUSE",
    [E_CHOICE]       = "E_CHOICE",
    [E_CUT]          = "E_CUT",
    [E_TRAIL_MARK]   = "E_TRAIL_MARK",
    [E_TRAIL_UNWIND] = "E_TRAIL_UNWIND",
    /* Icon numeric relational */
    [E_LT]           = "E_LT",
    [E_LE]           = "E_LE",
    [E_GT]           = "E_GT",
    [E_GE]           = "E_GE",
    [E_EQ]           = "E_EQ",
    [E_NE]           = "E_NE",
    /* Icon string relational */
    [E_LLT]          = "E_LLT",
    [E_LLE]          = "E_LLE",
    [E_LGT]          = "E_LGT",
    [E_LGE]          = "E_LGE",
    [E_LEQ]         = "E_LEQ",
    [E_LNE]          = "E_LNE",
    /* Icon cset ops */
    [E_CSET_COMPL]   = "E_CSET_COMPL",
    [E_CSET_UNION]   = "E_CSET_UNION",
    [E_CSET_DIFF]    = "E_CSET_DIFF",
    [E_CSET_INTER]   = "E_CSET_INTER",
    [E_LCONCAT]      = "E_LCONCAT",
    /* Icon unary */
    [E_NONNULL]      = "E_NONNULL",
    [E_NULL]         = "E_NULL",
    [E_NOT]          = "E_NOT",
    [E_SIZE]         = "E_SIZE",
    [E_RANDOM]       = "E_RANDOM",
    [E_IDENTICAL]    = "E_IDENTICAL",
    [E_AUGOP]        = "E_AUGOP",
    /* Icon control flow */
    [E_SEQ_EXPR]     = "E_SEQ_EXPR",
    [E_EVERY]        = "E_EVERY",
    [E_WHILE]        = "E_WHILE",
    [E_UNTIL]        = "E_UNTIL",
    [E_REPEAT]       = "E_REPEAT",
    [E_IF]           = "E_IF",
    [E_CASE]         = "E_CASE",
    [E_RETURN]       = "E_RETURN",
    [E_LOOP_BREAK]   = "E_LOOP_BREAK",
    [E_LOOP_NEXT]    = "E_LOOP_NEXT",
    [E_BANG_BINARY]  = "E_BANG_BINARY",
    /* Icon structure */
    [E_SECTION]      = "E_SECTION",
    [E_SECTION_PLUS] = "E_SECTION_PLUS",
    [E_SECTION_MINUS]= "E_SECTION_MINUS",
    [E_RECORD]       = "E_RECORD",
    [E_FIELD]        = "E_FIELD",
    [E_GLOBAL]       = "E_GLOBAL",
    [E_INITIAL]      = "E_INITIAL",
};

#endif /* IR_DEFINE_NAMES */



#ifdef __cplusplus
}
#endif

#endif /* IR_H */
