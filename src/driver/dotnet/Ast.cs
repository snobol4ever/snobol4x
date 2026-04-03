// Ast.cs — Typed AST nodes for scrip-interp.cs
//
// Mirrors STMT_t / EXPR_t from src/frontend/snobol4/scrip_cc.h
// Produced by Snobol4Parser, consumed by Executor.
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01

namespace ScripInterp;

// ── Statement ────────────────────────────────────────────────────────────────

public sealed class Stmt
{
    public string?  Label       { get; init; }   // source label (null if none)
    public Node?    Subject     { get; init; }   // null → pure goto
    public Node?    Pattern     { get; init; }   // null → assignment-only
    public bool     HasEq       { get; init; }   // = replacement present
    public Node?    Replacement { get; init; }
    public string?  GotoS       { get; init; }   // :S(label)
    public string?  GotoF       { get; init; }   // :F(label)
    public string?  GotoU       { get; init; }   // :(label) unconditional
    public bool     IsEnd       { get; init; }   // END statement
}

// ── Expression nodes ─────────────────────────────────────────────────────────

public abstract record Node;

// Literals
public record NLit(string Value)          : Node;  // unquoted integer/real literal
public record SLit(string Value)          : Node;  // quoted string literal

// Variable / name
public record Var(string Name)            : Node;  // simple variable reference
public record IndirectRef(Node Inner)     : Node;  // $expr — indirect reference

// Operators
public record Cat(Node Left, Node Right)  : Node;  // string concatenation
public record Alt(Node Left, Node Right)  : Node;  // pattern alternation  |
public record Seq(Node Left, Node Right)  : Node;  // pattern sequence     (implicit)

// Function call
public record FncCall(string Name, Node[] Args) : Node;

// Pattern captures
public record CaptCond(Node Inner, string VarName)  : Node;  // pat .var
public record CaptImm(Node Inner, string VarName)   : Node;  // pat $var
public record CaptCursor(string VarName)             : Node;  // @var
public record DeferredPat(Node Inner)                : Node;  // *pat

// Builtins that are structurally special (others go through FncCall)
public record ArrayRef(Node Base, Node[] Indices) : Node;   // base<i,j>
