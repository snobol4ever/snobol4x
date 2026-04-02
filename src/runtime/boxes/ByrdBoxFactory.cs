// ByrdBoxFactory.cs — Build a Byrd box graph from a snobol4dotnet Pattern tree
// Mirrors bb_build() in src/runtime/dyn/stmt_exec.c
//
// Takes a snobol4dotnet Pattern object (Jeff's existing tree) and produces
// a rooted IByrdBox graph ready to run through a ByrdBoxExecutor.
//
// The factory is also the bridge between the two worlds:
//   snobol4dotnet Pattern hierarchy → IByrdBox graph
//
// Variable access is injected via SetVar / GetStringVar / GetPatternVar
// delegates so the boxes can write to IdentifierTable without a hard dependency
// on Executive.

using Snobol4.Common;   // for Pattern, LiteralPattern, etc.

namespace Snobol4.Runtime.Boxes;

public sealed class ByrdBoxFactory
{
    private readonly Action<string, string>    _setVar;
    private readonly Func<string, string>      _getStringVar;
    private readonly Func<string, IByrdBox?>   _getPatternVar;

    // Pending captures registered during Build — Phase 5 commits them on :S
    private readonly List<BbCapture> _captures = new();

    public IReadOnlyList<BbCapture> Captures => _captures;

    public ByrdBoxFactory(
        Action<string, string>   setVar,
        Func<string, string>     getStringVar,
        Func<string, IByrdBox?>  getPatternVar)
    {
        _setVar        = setVar;
        _getStringVar  = getStringVar;
        _getPatternVar = getPatternVar;
    }

    // ── Entry point ─────────────────────────────────────────────────────────

    public IByrdBox Build(Pattern pattern)
    {
        _captures.Clear();
        return BuildNode(pattern);
    }

    // ── Recursive builder ────────────────────────────────────────────────────

    private IByrdBox BuildNode(Pattern p)
    {
        return p switch
        {
            LiteralPattern   lit  => new BbLit(lit.Literal),
            ConcatenatePattern cat => BuildSeq(cat),
            AlternatePattern   alt => BuildAlt(alt),
            ArbNoPattern       arb => BuildArbno(arb),
            ArbPattern             => BuildArb(),

            LenPattern         len  => new BbLen(GetIntArg(len)),
            PosPattern         pos  => new BbPos(GetIntArg(pos)),
            RPosPattern        rpos => new BbRpos(GetIntArg(rpos)),
            TabPattern         tab  => new BbTab(GetIntArg(tab)),
            RTabPattern        rtab => new BbRtab(GetIntArg(rtab)),
            RemPattern              => new BbRem(),
            AnyPattern         any  => new BbAny(GetCharsetArg(any)),
            NotAnyPattern      not  => new BbNotany(GetCharsetArg(not)),
            SpanPattern        spn  => new BbSpan(GetCharsetArg(spn)),
            BreakPattern       brk  => new BbBrk(GetCharsetArg(brk)),
            BreakXPattern      brkx => new BbBreakx(GetCharsetArg(brkx)),
            BalPattern              => new BbBal(),
            FencePattern            => new BbFence(),
            AbortPattern            => new BbAbort(),
            FailPattern             => new BbFail(),
            SucceedPattern          => new BbSucceed(),
            NullPattern             => new BbEps(),

            ConditionalVariableAssociationPattern cond => BuildCaptureCond(cond),
            // Immediate capture ($var) — wrapped around child
            // CursorAssignmentPattern (@var)
            CursorAssignmentPattern atp => BuildAtp(atp),

            UnevaluatedPattern uep => BuildDvar(uep),

            _ => new BbLit("")   // unknown pattern → empty literal (safe fallback)
        };
    }

    // ── Structural builders ──────────────────────────────────────────────────

    private IByrdBox BuildSeq(ConcatenatePattern cat)
    {
        // Flatten nested SEQ into n-ary right-fold (mirrors ir_nary_right_fold)
        var children = FlattenSeq(cat);
        if (children.Count == 1) return BuildNode(children[0]);
        // Right-fold: SEQ(a, SEQ(b, SEQ(c, d)))
        IByrdBox right = BuildNode(children[^1]);
        for (int i = children.Count - 2; i >= 0; i--)
            right = new BbSeq(BuildNode(children[i]), right);
        return right;
    }

    private List<Pattern> FlattenSeq(ConcatenatePattern cat)
    {
        var list = new List<Pattern>();
        void Collect(Pattern p)
        {
            if (p is ConcatenatePattern c) { Collect(c.LeftPattern!); Collect(c.RightPattern!); }
            else list.Add(p);
        }
        Collect(cat);
        return list;
    }

    private IByrdBox BuildAlt(AlternatePattern alt)
    {
        var children = FlattenAlt(alt);
        return new BbAlt(children.Select(BuildNode).ToArray());
    }

    private List<Pattern> FlattenAlt(AlternatePattern alt)
    {
        var list = new List<Pattern>();
        void Collect(Pattern p)
        {
            if (p is AlternatePattern a) { Collect(a.LeftPattern!); Collect(a.RightPattern!); }
            else list.Add(p);
        }
        Collect(alt);
        return list;
    }

    private IByrdBox BuildArbno(ArbNoPattern arb)
    {
        // ArbNoPattern wraps its child pattern
        var child = arb.ArbPattern != null ? BuildNode(arb.ArbPattern) : new BbEps();
        return new BbArbno(child);
    }

    private static IByrdBox BuildArb() => new BbArb();

    // ── Capture builders ─────────────────────────────────────────────────────

    private IByrdBox BuildCaptureCond(ConditionalVariableAssociationPattern cond)
    {
        var child   = cond.LeftPattern  != null ? BuildNode(cond.LeftPattern)  : new BbEps();
        var varname = cond.VariableName ?? "";
        var box     = new BbCapture(child, varname, immediate: false)
                      { SetVar = _setVar };
        _captures.Add(box);
        return box;
    }

    private IByrdBox BuildAtp(CursorAssignmentPattern atp)
    {
        return new BbAtp(atp.VariableName ?? "") { SetVar = _setVar };
    }

    private IByrdBox BuildDvar(UnevaluatedPattern uep)
    {
        // UnevaluatedPattern wraps a DeferredCode delegate — at match time
        // it evaluates to a pattern.  Map to BbDvar which re-resolves on α.
        var varname = uep.VariableName ?? "";
        return new BbDvar(varname)
        {
            GetStringVar  = _getStringVar,
            GetPatternVar = _getPatternVar,
        };
    }

    // ── Argument extractors ──────────────────────────────────────────────────
    // These read the integer or charset argument from pattern nodes.
    // The pattern nodes store arguments in their Ast / parameter fields.

    private static int GetIntArg(Pattern p)
    {
        // Most argument patterns store their value in Ast[0] or a field.
        // Try reflection-free path first via known subclasses.
        return p switch
        {
            LenPattern  l  => l.N,
            PosPattern  ps => ps.N,
            RPosPattern r  => r.N,
            TabPattern  t  => t.N,
            RTabPattern rt => rt.N,
            _              => 0
        };
    }

    private static string GetCharsetArg(Pattern p) => p switch
    {
        AnyPattern    a  => a.Characters ?? "",
        NotAnyPattern n  => n.Characters ?? "",
        SpanPattern   s  => s.Characters ?? "",
        BreakPattern  b  => b.Characters ?? "",
        BreakXPattern bx => bx.Characters ?? "",
        _                => ""
    };
}
