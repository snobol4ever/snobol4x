// IrNode.cs — C# mirror of the unified IR
//
// Mirrors:
//   EKind     enum  → ir.h  (same names, same semantics)
//   IrNode    class → EXPR_t struct in ir.h
//   SnoGoto   class → SnoGoto struct in scrip_cc.h
//   IrStmt    class → STMT_t struct in scrip_cc.h
//
// This is the "one IR, three consumers" invariant:
//   scrip-cc (C)        consumes EXPR_t / EKind from ir.h
//   scrip-interp.c (C)  consumes same
//   scrip-interp.cs     consumes IrNode / IrKind (this file)
//
// Node kind names are IDENTICAL to EKind names.
// SNOBOL4-frontend-relevant subset only: Icon/Prolog/Rebus kinds included
// as enum members for completeness but not dispatched by this interpreter.
//
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
// SPRINT:  D-169 / M-NET-INTERP-A01b

namespace ScripInterp;

// ── IrKind — mirrors EKind from ir.h exactly ───────────────────────────────

public enum IrKind
{
    // Literals
    E_QLIT,             // quoted string / pattern literal
    E_ILIT,             // integer literal
    E_FLIT,             // float literal
    E_CSET,             // cset literal (Icon/Rebus)
    E_NUL,              // null / empty value

    // References
    E_VAR,              // variable reference
    E_KEYWORD,          // &IDENT keyword
    E_INDIRECT,         // $expr indirect reference
    E_DEFER,            // *expr deferred pattern ref

    // Arithmetic / operators
    E_INTERROGATE,      // ?X interrogation
    E_NAME,             // .X name reference
    E_MNS,              // unary minus
    E_PLS,              // unary plus / numeric coerce
    E_ADD,
    E_SUB,
    E_MUL,
    E_DIV,
    E_MOD,
    E_POW,

    // Sequence / alternation
    E_SEQ,              // goal-directed concat (Byrd-box wiring)
    E_CAT,              // pure value-context string concat
    E_ALT,              // pattern alternation
    E_OPSYN,            // & operator

    // Pattern primitives
    E_ARB,
    E_ARBNO,
    E_POS,
    E_RPOS,
    E_ANY,
    E_NOTANY,
    E_SPAN,
    E_BREAK,
    E_BREAKX,
    E_LEN,
    E_TAB,
    E_RTAB,
    E_REM,
    E_FAIL,
    E_SUCCEED,
    E_FENCE,
    E_ABORT,
    E_BAL,

    // Captures
    E_CAPT_COND_ASGN,   // .var conditional capture
    E_CAPT_IMMED_ASGN,  // $var immediate capture
    E_CAPT_CURSOR,      // @var cursor capture

    // Call / access / assignment / scan
    E_FNC,              // function call, n-ary
    E_IDX,              // array/table subscript
    E_ASSIGN,           // assignment
    E_SCAN,             // E ? E scanning
    E_SWAP,             // :=: swap

    // Icon generators (present for enum completeness; not dispatched by SNOBOL4 interpreter)
    E_SUSPEND, E_TO, E_TO_BY, E_LIMIT, E_ALTERNATE, E_ITERATE, E_MAKELIST,

    // Prolog (present for completeness)
    E_UNIFY, E_CLAUSE, E_CHOICE, E_CUT, E_TRAIL_MARK, E_TRAIL_UNWIND,

    // Icon numeric relational
    E_LT, E_LE, E_GT, E_GE, E_EQ, E_NE,

    // Icon lexicographic relational
    E_LLT, E_LLE, E_LGT, E_LGE, E_LEQ, E_LNE,

    // Icon cset operators
    E_CSET_COMPL, E_CSET_UNION, E_CSET_DIFF, E_CSET_INTER, E_LCONCAT,

    // Icon unary
    E_NONNULL, E_NULL, E_NOT, E_SIZE, E_RANDOM, E_IDENTICAL, E_AUGOP,

    // Icon control flow
    E_SEQ_EXPR, E_EVERY, E_WHILE, E_UNTIL, E_REPEAT,
    E_IF, E_CASE, E_RETURN, E_LOOP_BREAK, E_LOOP_NEXT,
    E_BANG_BINARY,
}

// ── IrNode — mirrors EXPR_t from ir.h ──────────────────────────────────────
//
// Fields:
//   Kind      ← kind
//   SVal      ← sval  (E_QLIT text; E_VAR/E_KEYWORD/E_FNC/E_IDX name)
//   IVal      ← ival  (E_ILIT value)
//   DVal      ← dval  (E_FLIT value; note: ir.h uses dval, not fval)
//   Children  ← children[] / nchildren
//   Id        ← id    (assigned at emit time; not needed by interpreter)

public sealed class IrNode
{
    public IrKind   Kind;
    public string?  SVal;
    public long     IVal;
    public double   DVal;
    public IrNode[] Children;
    public int      Id;

    public IrNode(IrKind kind)
    {
        Kind     = kind;
        Children = [];
    }

    // ── Leaf constructors ─────────────────────────────────────────────────

    public static IrNode QStr(string s)
        => new(IrKind.E_QLIT)  { SVal = s };

    public static IrNode Int(long v)
        => new(IrKind.E_ILIT)  { IVal = v };

    public static IrNode Float(double v)
        => new(IrKind.E_FLIT)  { DVal = v };

    public static IrNode Nul()
        => new(IrKind.E_NUL);

    public static IrNode Var(string name)
        => new(IrKind.E_VAR)   { SVal = name };

    public static IrNode Keyword(string name)
        => new(IrKind.E_KEYWORD) { SVal = name };

    // ── N-ary constructor ─────────────────────────────────────────────────

    public static IrNode Nary(IrKind kind, params IrNode[] children)
    {
        var n = new IrNode(kind) { Children = children };
        return n;
    }

    // ── Convenience predicates ────────────────────────────────────────────

    public bool IsLeaf     => Children.Length == 0;
    public bool IsPattern  => Kind is
        IrKind.E_SEQ or IrKind.E_ALT or IrKind.E_ARB or IrKind.E_ARBNO or
        IrKind.E_ANY or IrKind.E_NOTANY or IrKind.E_SPAN or IrKind.E_BREAK or
        IrKind.E_BREAKX or IrKind.E_LEN or IrKind.E_TAB or IrKind.E_RTAB or
        IrKind.E_REM or IrKind.E_FAIL or IrKind.E_SUCCEED or IrKind.E_FENCE or
        IrKind.E_ABORT or IrKind.E_BAL or IrKind.E_POS or IrKind.E_RPOS or
        IrKind.E_CAPT_COND_ASGN or IrKind.E_CAPT_IMMED_ASGN or
        IrKind.E_CAPT_CURSOR or IrKind.E_DEFER or IrKind.E_QLIT;

    public override string ToString() =>
        SVal != null ? $"{Kind}({SVal})" :
        Kind == IrKind.E_ILIT ? $"E_ILIT({IVal})" :
        Kind == IrKind.E_FLIT ? $"E_FLIT({DVal})" :
        $"{Kind}[{Children.Length}]";
}

// ── SnoGoto — mirrors SnoGoto from scrip_cc.h ──────────────────────────────

public sealed class SnoGoto
{
    public string? OnSuccess;           // ← onsuccess
    public string? OnFailure;           // ← onfailure
    public string? Uncond;              // ← uncond
    // computed_* fields omitted for interpreter (resolved at parse time)
}

// ── IrStmt — mirrors STMT_t from scrip_cc.h ───────────────────────────────
//
// Fields:
//   Label       ← label
//   Subject     ← subject   (EXPR_t*)
//   Pattern     ← pattern   (EXPR_t*; null if no pattern)
//   Replacement ← replacement (EXPR_t*; null if no replacement)
//   Go          ← go        (SnoGoto*)
//   IsEnd       ← is_end
//   HasEq       ← has_eq    (explicit = replacement present)
//   LineNo      ← lineno

public sealed class IrStmt
{
    public string?  Label;
    public IrNode?  Subject;
    public IrNode?  Pattern;
    public IrNode?  Replacement;
    public SnoGoto? Go;
    public bool     IsEnd;
    public bool     HasEq;
    public int      LineNo;

    // Convenience: effective goto targets
    public string? GotoOnSuccess => Go?.OnSuccess ?? Go?.Uncond;
    public string? GotoOnFailure => Go?.OnFailure ?? Go?.Uncond;
    public string? GotoUnconditional => Go?.Uncond;

    public bool HasPattern     => Pattern != null;
    public bool HasReplacement => Replacement != null || HasEq;
    public bool HasGoto        => Go != null &&
                                  (Go.OnSuccess != null || Go.OnFailure != null || Go.Uncond != null);

    public override string ToString()
    {
        var sb = new System.Text.StringBuilder();
        if (Label != null) sb.Append(Label).Append(": ");
        if (Subject != null) sb.Append(Subject);
        if (Pattern != null) sb.Append(" ? ").Append(Pattern);
        if (Replacement != null) sb.Append(" = ").Append(Replacement);
        if (HasGoto) sb.Append(" :").Append(GotoOnSuccess ?? "").Append("/").Append(GotoOnFailure ?? "");
        if (IsEnd) sb.Append(" [END]");
        return sb.ToString();
    }
}
