// PatternBuilder.cs — Walks IrNode pattern subtree → IByrdBox graph
//
// Dispatches on IrKind (not bespoke Node records).
// Mirrors ByrdBoxFactory.cs but takes IrNode trees from Snobol4Parser.
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01b

using Snobol4.Runtime.Boxes;

namespace ScripInterp;

public sealed class PatternBuilder
{
    private readonly Action<string, string>   _setVar;
    private readonly Func<string, string>     _getStringVar;
    private readonly Func<string, IByrdBox?>  _getPatternVar;
    private readonly Func<IrNode, SnobolVal>  _evalNode;

    private readonly List<BbCapture> _captures = new();
    public IReadOnlyList<BbCapture> Captures => _captures;

    public PatternBuilder(
        Action<string, string>   setVar,
        Func<string, string>     getStringVar,
        Func<string, IByrdBox?>  getPatternVar,
        Func<IrNode, SnobolVal>  evalNode)
    {
        _setVar        = setVar;
        _getStringVar  = getStringVar;
        _getPatternVar = getPatternVar;
        _evalNode      = evalNode;
    }

    // ── Entry point ──────────────────────────────────────────────────────────

    public IByrdBox Build(IrNode n)
    {
        _captures.Clear();
        return BuildNode(n);
    }

    // ── Recursive builder ────────────────────────────────────────────────────

    private IByrdBox BuildNode(IrNode n)
    {
        return n.Kind switch
        {
            // Structural
            IrKind.E_ALT  => BuildAlt(n),
            IrKind.E_SEQ  => BuildSeq(n),
            IrKind.E_CAT  => BuildSeq(n),   // CAT in pattern context = sequence

            // Literals
            IrKind.E_QLIT => new BbLit(n.SVal ?? ""),
            IrKind.E_ILIT => new BbLit(n.IVal.ToString()),
            IrKind.E_FLIT => new BbLit(n.DVal.ToString()),

            // Captures
            IrKind.E_CAPT_COND_ASGN  => BuildCaptureCond(n),
            IrKind.E_CAPT_IMMED_ASGN => BuildCaptureImm(n),
            IrKind.E_CAPT_CURSOR      => BuildCaptCursor(n),

            // Deferred pattern
            IrKind.E_DEFER => BuildDeferred(n),

            // Variable — resolve at build time
            IrKind.E_VAR  => BuildVar(n.SVal!),

            // Indirect $expr
            IrKind.E_INDIRECT => BuildIndirect(n),

            // Nullary pattern primitives
            IrKind.E_ARB     => new BbArb(),
            IrKind.E_REM     => new BbRem(),
            IrKind.E_FAIL    => new BbFail(),
            IrKind.E_SUCCEED => new BbSucceed(),
            IrKind.E_FENCE   => new BbFence(),
            IrKind.E_ABORT   => new BbAbort(),
            IrKind.E_BAL     => new BbBal(),

            // Unary pattern primitives — arg in Children[0]
            IrKind.E_ANY     => new BbAny(StrArg(n, 0)),
            IrKind.E_NOTANY  => new BbNotany(StrArg(n, 0)),
            IrKind.E_SPAN    => new BbSpan(StrArg(n, 0)),
            IrKind.E_BREAK   => new BbBrk(StrArg(n, 0)),
            IrKind.E_BREAKX  => new BbBreakx(StrArg(n, 0)),
            IrKind.E_LEN     => new BbLen(IntArg(n, 0)),
            IrKind.E_TAB     => new BbTab(IntArg(n, 0)),
            IrKind.E_RTAB    => new BbRtab(IntArg(n, 0)),
            IrKind.E_POS     => new BbPos(IntArg(n, 0)),
            IrKind.E_RPOS    => new BbRpos(IntArg(n, 0)),
            IrKind.E_ARBNO   => n.Children.Length >= 1
                                    ? new BbArbno(BuildNode(n.Children[0]))
                                    : new BbEps(),

            // Function call — may be a pattern builtin with dynamic args
            IrKind.E_FNC  => BuildFncPattern(n),

            _ => new BbLit("")   // safe fallback
        };
    }

    // ── Alt ──────────────────────────────────────────────────────────────────

    private IByrdBox BuildAlt(IrNode n)
    {
        var parts = new List<IrNode>();
        void Collect(IrNode x)
        {
            if (x.Kind == IrKind.E_ALT)
                foreach (var c in x.Children) Collect(c);
            else
                parts.Add(x);
        }
        Collect(n);
        return new BbAlt(parts.Select(BuildNode).ToArray());
    }

    // ── Seq ──────────────────────────────────────────────────────────────────

    private IByrdBox BuildSeq(IrNode n)
    {
        var parts = new List<IrNode>();
        void Collect(IrNode x)
        {
            if (x.Kind == IrKind.E_SEQ || x.Kind == IrKind.E_CAT)
                foreach (var c in x.Children) Collect(c);
            else
                parts.Add(x);
        }
        Collect(n);
        if (parts.Count == 0) return new BbEps();
        if (parts.Count == 1) return BuildNode(parts[0]);
        // Right-fold
        IByrdBox right = BuildNode(parts[^1]);
        for (int i = parts.Count - 2; i >= 0; i--)
            right = new BbSeq(BuildNode(parts[i]), right);
        return right;
    }

    // ── Captures ─────────────────────────────────────────────────────────────

    private IByrdBox BuildCaptureCond(IrNode n)
    {
        // Children[0] = inner pattern node, SVal or Children[0] of inner = varname
        // For E_CAPT_COND_ASGN the child is E_VAR(varname) — the inner pattern
        // is the surrounding context; here we wrap the previous box.
        // When parser emits E_CAPT_COND_ASGN with one child E_VAR, the pattern
        // is the containing SEQ node's left sibling. For standalone .var, wrap Eps.
        var varName = n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR
                    ? n.Children[0].SVal!
                    : (n.SVal ?? "");
        var inner   = new BbEps();
        var box     = new BbCapture(inner, varName, immediate: false) { SetVar = _setVar };
        _captures.Add(box);
        return box;
    }

    private IByrdBox BuildCaptureImm(IrNode n)
    {
        var varName = n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR
                    ? n.Children[0].SVal!
                    : (n.SVal ?? "");
        var inner   = new BbEps();
        return new BbCapture(inner, varName, immediate: true) { SetVar = _setVar };
    }

    private IByrdBox BuildCaptCursor(IrNode n)
    {
        var varName = n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR
                    ? n.Children[0].SVal!
                    : (n.SVal ?? "");
        return new BbAtp(varName) { SetVar = _setVar };
    }

    // ── Deferred ─────────────────────────────────────────────────────────────

    private IByrdBox BuildDeferred(IrNode n)
    {
        if (n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR)
        {
            var name = n.Children[0].SVal!;
            return new BbDvar(name)
                   { GetStringVar = _getStringVar, GetPatternVar = _getPatternVar };
        }
        var val = _evalNode(n.Children.Length > 0 ? n.Children[0] : n).ToString();
        return new BbLit(val);
    }

    // ── Variable ─────────────────────────────────────────────────────────────

    private IByrdBox BuildVar(string name)
    {
        var patBox = _getPatternVar(name);
        if (patBox != null) return patBox;
        var str = _getStringVar(name);
        return new BbLit(str);
    }

    // ── Indirect ─────────────────────────────────────────────────────────────

    private IByrdBox BuildIndirect(IrNode n)
    {
        var val = n.Children.Length > 0 ? _evalNode(n.Children[0]).ToString() : "";
        return BuildVar(val);
    }

    // ── Function call — pattern builtins with dynamic args ───────────────────

    private IByrdBox BuildFncPattern(IrNode n)
    {
        var name = n.SVal?.ToUpperInvariant() ?? "";
        var args = n.Children;

        int    IntArg2(int i) => args.Length > i ? (int)_evalNode(args[i]).ToInt() : 0;
        string StrArg2(int i) => args.Length > i ? _evalNode(args[i]).ToString() : "";

        return name switch
        {
            "LEN"     => new BbLen(IntArg2(0)),
            "POS"     => new BbPos(IntArg2(0)),
            "RPOS"    => new BbRpos(IntArg2(0)),
            "TAB"     => new BbTab(IntArg2(0)),
            "RTAB"    => new BbRtab(IntArg2(0)),
            "REM"     => new BbRem(),
            "ANY"     => new BbAny(StrArg2(0)),
            "NOTANY"  => new BbNotany(StrArg2(0)),
            "SPAN"    => new BbSpan(StrArg2(0)),
            "BREAK"   => new BbBrk(StrArg2(0)),
            "BREAKX"  => new BbBreakx(StrArg2(0)),
            "BAL"     => new BbBal(),
            "FENCE"   => new BbFence(),
            "ABORT"   => new BbAbort(),
            "FAIL"    => new BbFail(),
            "SUCCEED" => new BbSucceed(),
            "ARB"     => new BbArb(),
            "ARBNO"   => args.Length >= 1 ? new BbArbno(BuildNode(args[0])) : new BbEps(),
            _         => new BbLit(StrArg2(0))
        };
    }

    // ── Arg helpers ──────────────────────────────────────────────────────────

    private int    IntArg(IrNode n, int i) =>
        n.Children.Length > i ? (int)_evalNode(n.Children[i]).ToInt() : 0;

    private string StrArg(IrNode n, int i) =>
        n.Children.Length > i ? _evalNode(n.Children[i]).ToString() : "";
}
