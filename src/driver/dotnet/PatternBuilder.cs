// PatternBuilder.cs — Walks a scrip-interp Node AST → IByrdBox graph
//
// Mirrors ByrdBoxFactory.cs (src/runtime/boxes/shared/bb_factory.cs) but
// takes our Pidgin-produced Node tree instead of the snobol4dotnet Pattern hierarchy.
//
// Variable access is injected via delegates so boxes can write/read
// SnobolEnv without a hard dependency.
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01

using Snobol4.Runtime.Boxes;

namespace ScripInterp;

public sealed class PatternBuilder
{
    private readonly Action<string, string>   _setVar;
    private readonly Func<string, string>     _getStringVar;
    private readonly Func<string, IByrdBox?>  _getPatternVar;
    private readonly Func<Node, SnobolVal>    _evalNode;

    // Pending .var captures — committed by Executor on :S (Phase 5)
    private readonly List<BbCapture> _captures = new();
    public IReadOnlyList<BbCapture> Captures => _captures;

    public PatternBuilder(
        Action<string, string>   setVar,
        Func<string, string>     getStringVar,
        Func<string, IByrdBox?>  getPatternVar,
        Func<Node, SnobolVal>    evalNode)
    {
        _setVar        = setVar;
        _getStringVar  = getStringVar;
        _getPatternVar = getPatternVar;
        _evalNode      = evalNode;
    }

    // ── Entry point ──────────────────────────────────────────────────────────

    public IByrdBox Build(Node n)
    {
        _captures.Clear();
        return BuildNode(n);
    }

    // ── Recursive builder ────────────────────────────────────────────────────

    private IByrdBox BuildNode(Node n) => n switch
    {
        // Structural
        Alt(var l, var r)   => BuildAlt(l, r),
        Cat(var l, var r)   => BuildSeq(l, r),
        Seq(var l, var r)   => BuildSeq(l, r),

        // Literals — build a BbLit from the string value
        SLit(var v)         => new BbLit(v),
        NLit(var v)         => new BbLit(v),   // numeric literal used as pattern → match text

        // Captures
        CaptCond(var inner, var vname) => BuildCaptureCond(inner, vname),
        CaptImm(var inner, var vname)  => BuildCaptureImm(inner, vname),
        CaptCursor(var vname)          => new BbAtp(vname) { SetVar = _setVar },
        DeferredPat(var inner)         => BuildDeferred(inner),

        // Variable — look up at build time as string (literal match) or
        // deferred pattern variable; if it names a pattern var → use it
        Var(var name)       => BuildVar(name),

        // Function calls that map to named pattern builtins
        FncCall(var name, var args) => BuildFncPattern(name, args),

        // Indirect $expr — resolve to string at build time
        IndirectRef(var inner) => BuildIndirect(inner),

        _ => new BbLit("")   // safe fallback
    };

    // ── Alt ──────────────────────────────────────────────────────────────────

    private IByrdBox BuildAlt(Node l, Node r)
    {
        // Flatten nested Alt into n-ary array (mirrors ByrdBoxFactory.FlattenAlt)
        var parts = new List<Node>();
        void Collect(Node x) { if (x is Alt(var a, var b)) { Collect(a); Collect(b); } else parts.Add(x); }
        Collect(new Alt(l, r));
        return new BbAlt(parts.Select(BuildNode).ToArray());
    }

    // ── Seq ──────────────────────────────────────────────────────────────────

    private IByrdBox BuildSeq(Node l, Node r)
    {
        // Flatten nested Cat/Seq into right-fold (mirrors ByrdBoxFactory.BuildSeq)
        var parts = new List<Node>();
        void Collect(Node x)
        {
            if (x is Cat(var a, var b)) { Collect(a); Collect(b); }
            else if (x is Seq(var sa, var sb)) { Collect(sa); Collect(sb); }
            else parts.Add(x);
        }
        Collect(new Cat(l, r));
        if (parts.Count == 1) return BuildNode(parts[0]);
        IByrdBox right = BuildNode(parts[^1]);
        for (int i = parts.Count - 2; i >= 0; i--)
            right = new BbSeq(BuildNode(parts[i]), right);
        return right;
    }

    // ── Captures ─────────────────────────────────────────────────────────────

    private IByrdBox BuildCaptureCond(Node inner, string vname)
    {
        var child = BuildNode(inner);
        var box   = new BbCapture(child, vname, immediate: false) { SetVar = _setVar };
        _captures.Add(box);
        return box;
    }

    private IByrdBox BuildCaptureImm(Node inner, string vname)
    {
        var child = BuildNode(inner);
        return new BbCapture(child, vname, immediate: true) { SetVar = _setVar };
    }

    private IByrdBox BuildDeferred(Node inner)
    {
        // *var — re-resolve pattern variable at match time
        if (inner is Var(var name))
            return new BbDvar(name) { GetStringVar = _getStringVar, GetPatternVar = _getPatternVar };
        // *expr — evaluate to string and match as literal (simplified)
        var val = _evalNode(inner).ToString();
        return new BbLit(val);
    }

    private IByrdBox BuildVar(string name)
    {
        // Check if the variable currently holds a pattern → use as pattern box
        var patBox = _getPatternVar(name);
        if (patBox != null) return patBox;
        // Otherwise: variable holds a string — match its current value as literal
        var str = _getStringVar(name);
        return new BbLit(str);
    }

    private IByrdBox BuildIndirect(Node inner)
    {
        var val = _evalNode(inner).ToString();
        // val is a variable name — resolve it
        return BuildVar(val);
    }

    // ── Named pattern builtins → boxes ───────────────────────────────────────

    private IByrdBox BuildFncPattern(string name, Node[] args)
    {
        // Evaluate integer/string args eagerly at build time (static args).
        // Dynamic args (variables) are re-evaluated at match time via BbDvar.
        int    IntArg(int i) => args.Length > i ? (int)_evalNode(args[i]).ToInt() : 0;
        string StrArg(int i) => args.Length > i ? _evalNode(args[i]).ToString() : "";

        return name.ToUpperInvariant() switch
        {
            "LEN"     => new BbLen(IntArg(0)),
            "POS"     => new BbPos(IntArg(0)),
            "RPOS"    => new BbRpos(IntArg(0)),
            "TAB"     => new BbTab(IntArg(0)),
            "RTAB"    => new BbRtab(IntArg(0)),
            "REM"     => new BbRem(),
            "ANY"     => new BbAny(StrArg(0)),
            "NOTANY"  => new BbNotany(StrArg(0)),
            "SPAN"    => new BbSpan(StrArg(0)),
            "BREAK"   => new BbBrk(StrArg(0)),
            "BREAKX"  => new BbBreakx(StrArg(0)),
            "BAL"     => new BbBal(),
            "FENCE"   => new BbFence(),
            "ABORT"   => new BbAbort(),
            "FAIL"    => new BbFail(),
            "SUCCEED" => new BbSucceed(),
            "ARB"     => new BbArb(),
            "ARBNO"   => args.Length >= 1
                            ? new BbArbno(BuildNode(args[0]))
                            : new BbEps(),
            // Fallback: evaluate as string and match literally
            _ => new BbLit(StrArg(0))
        };
    }
}
