/*
 * icon_ast.h — Tiny-ICON AST node types
 *
 * Maps to Proebsting 1996 paper operators plus Icon language constructs.
 * icont node names (from tcode.c) annotated in comments for cross-reference.
 *
 * Port model: α (start) / β (resume) / γ (succeed) / ω (fail)
 * as in Byrd Box IR used throughout snobol4x.
 */

#ifndef ICON_AST_H
#define ICON_AST_H

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Node kind enum
 *
 * Tier 0 = Rung 1 (paper examples): ICN_INT through ICN_EVERY
 * Tier 1 = Rung 2-3: procedures, suspend, user generators
 * Tier 2 = Rung 4: strings, scanning, csets
 * -------------------------------------------------------------------------*/
typedef enum {
    /* --- Literals (icont: N_Int, N_Real, N_Str, N_Cset) --- */
    ICN_INT,       /* integer literal: val.ival   -- paper §4.1 */
    ICN_REAL,      /* real literal:    val.fval */
    ICN_STR,       /* string literal:  val.sval */
    ICN_CSET,      /* cset literal:    val.sval -- Tier 2 */

    /* --- Variable / keyword (icont: N_Id, N_Key) --- */
    ICN_VAR,       /* variable or &keyword: val.sval = name */

    /* --- Arithmetic (icont: N_Binop) --- */
    ICN_ADD,       /* E1 + E2 */
    ICN_SUB,       /* E1 - E2 */
    ICN_MUL,       /* E1 * E2 */
    ICN_DIV,       /* E1 / E2 */
    ICN_MOD,       /* E1 % E2 */
    ICN_POW,       /* E1 ^ E2 */
    ICN_NEG,       /* -E (unary) */

    /* --- Numeric relational (icont: N_Binop) --- */
    ICN_LT,        /* E1 < E2  -- paper §4.3 goal-directed retry */
    ICN_LE,        /* E1 <= E2 */
    ICN_GT,        /* E1 > E2 */
    ICN_GE,        /* E1 >= E2 */
    ICN_EQ,        /* E1 = E2  (numeric equality) */
    ICN_NE,        /* E1 ~= E2 */

    /* --- String relational --- */
    ICN_SLT,       /* E1 << E2 */
    ICN_SLE,       /* E1 <<= E2 */
    ICN_SGT,       /* E1 >> E2 */
    ICN_SGE,       /* E1 >>= E2 */
    ICN_SEQ,       /* E1 == E2 (string equality) */
    ICN_SNE,       /* E1 ~== E2 */

    /* --- String concat --- */
    ICN_CONCAT,    /* E1 || E2   -- Tier 2 */
    ICN_LCONCAT,   /* E1 ||| E2  -- Tier 2 */

    /* --- Generator: to (icont: N_To, N_ToBy) --- */
    ICN_TO,        /* E1 to E2      -- paper §4.4 inline counter */
    ICN_TO_BY,     /* E1 to E2 by E3 */

    /* --- Value alternation (NOT pattern alt) --- */
    ICN_ALT,       /* E1 | E2  -- n-ary via left-spine */

    /* --- Conjunction --- */
    ICN_AND,       /* E1 & E2  -- irgen.icn ir_conjunction wiring */

    /* --- Unary generators / operators --- */
    ICN_BANG,      /* !E  (generate list/string elements) -- Tier 1 */
    ICN_SIZE,      /* *E  (size of string) -- Tier 2 */
    ICN_LIMIT,     /* E \ N  (limitation) */
    ICN_NOT,       /* not E  (succeed if E fails) */

    /* --- Control flow --- */
    ICN_SEQ_EXPR,  /* E1 ; E2  (expression sequence) */
    ICN_EVERY,     /* every E [do body] -- paper §4 */
    ICN_WHILE,     /* while E [do body] -- icont: N_Loop ltype=WHILE */
    ICN_UNTIL,     /* until E [do body] -- icont: N_Loop ltype=UNTIL */
    ICN_REPEAT,    /* repeat body       -- icont: N_Loop ltype=REPEAT */
    ICN_IF,        /* if E then E2 [else E3] -- paper §4.5 indirect goto */
    ICN_CASE,      /* case E of { ... } -- Tier 2 */

    /* --- Assignment --- */
    ICN_ASSIGN,    /* E1 := E2 */
    ICN_AUGOP,     /* E1 op:= E2  (augmented -- subtype in val.ival) */
    ICN_SWAP,      /* E1 :=: E2 */

    /* --- String scanning (icont: N_Scan) --- */
    ICN_SCAN,      /* E ? body  -- Tier 2 */
    ICN_SCAN_AUGOP,/* E ?:= body -- Tier 2 */

    /* --- Procedure / call (icont: N_Proc, N_Invok) --- */
    ICN_CALL,      /* fn(args...)  -- Tier 1 */
    ICN_RETURN,    /* return [E]   -- Tier 1 */
    ICN_SUSPEND,   /* suspend E [do body] -- Tier 1 */
    ICN_FAIL,      /* fail         -- Tier 1 */
    ICN_BREAK,     /* break [E] */
    ICN_NEXT,      /* next */
    ICN_PROC,      /* procedure name(params) body end -- Tier 1 */

    /* --- Structure (icont: N_Field, subscript, record) --- */
    ICN_FIELD,     /* E.name  -- Tier 2 */
    ICN_SUBSCRIPT, /* E[i]    -- Tier 2 */
    ICN_RECORD,    /* record decl -- Tier 2 */
    ICN_GLOBAL,    /* global decl */

    /* --- Co-expression (out of scope) --- */
    /* ICN_CREATE intentionally omitted */

    ICN_KIND_COUNT /* sentinel */
} IcnKind;

/* -------------------------------------------------------------------------
 * AST node
 * -------------------------------------------------------------------------*/

/* Forward declaration */
typedef struct IcnNode IcnNode;

/* Variable-length children list (small inline array for leaf/unary/binary;
 * heap-allocated for n-ary nodes like ICN_CALL argument lists). */
#define ICN_INLINE_CHILDREN 3

struct IcnNode {
    IcnKind    kind;
    int        line;        /* source line (for error messages) */

    /* Literal / name payload */
    union {
        long   ival;        /* ICN_INT, augop subtype */
        double fval;        /* ICN_REAL */
        char  *sval;        /* ICN_STR, ICN_CSET, ICN_VAR (owned) */
    } val;

    /* Children */
    int       nchildren;
    IcnNode **children;     /* heap-allocated array of child pointers */
};

/* -------------------------------------------------------------------------
 * Node constructors (allocate on heap, children passed as varargs)
 * -------------------------------------------------------------------------*/

IcnNode *icn_node_new(IcnKind kind, int line, int nchildren, ...);
void icn_node_append(IcnNode *parent, IcnNode *child);
IcnNode *icn_leaf_int(int line, long ival);
IcnNode *icn_leaf_real(int line, double fval);
IcnNode *icn_leaf_str(IcnKind kind, int line, const char *s, size_t len);
IcnNode *icn_leaf_var(int line, const char *name);
void     icn_node_free(IcnNode *n);   /* recursive free */

/* -------------------------------------------------------------------------
 * Diagnostic
 * -------------------------------------------------------------------------*/
const char *icn_kind_name(IcnKind kind);
void        icn_node_dump(IcnNode *n, int indent); /* pretty-print to stderr */

#endif /* ICON_AST_H */
