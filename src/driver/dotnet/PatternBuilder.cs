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

    private readonly List<bb_capture> _captures = new();
    public IReadOnlyList<bb_capture> Captures => _captures;

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
            IrKind.E_QLIT => new bb_lit(n.SVal ?? ""),
            IrKind.E_ILIT => new bb_lit(n.IVal.ToString()),
            IrKind.E_FLIT => new bb_lit(n.DVal.ToString()),

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
            IrKind.E_ARB     => new bb_arb(),
            IrKind.E_REM     => new bb_rem(),
            IrKind.E_FAIL    => new bb_fail(),
            IrKind.E_SUCCEED => new bb_succeed(),
            IrKind.E_FENCE   => new bb_fence(),
            IrKind.E_ABORT   => new bb_abort(),
            IrKind.E_BAL     => new bb_bal(),

            // Unary pattern primitives — arg in Children[0]
            IrKind.E_ANY     => new bb_any(StrArg(n, 0)),
            IrKind.E_NOTANY  => new bb_notany(StrArg(n, 0)),
            IrKind.E_SPAN    => new bb_span(StrArg(n, 0)),
            IrKind.E_BREAK   => new bb_brk(StrArg(n, 0)),
            IrKind.E_BREAKX  => new bb_breakx(StrArg(n, 0)),
            IrKind.E_LEN     => new bb_len(IntArg(n, 0)),
            IrKind.E_TAB     => new bb_tab(IntArg(n, 0)),
            IrKind.E_RTAB    => new bb_rtab(IntArg(n, 0)),
            IrKind.E_POS     => new bb_pos(IntArg(n, 0)),
            IrKind.E_RPOS    => new bb_rpos(IntArg(n, 0)),
            IrKind.E_ARBNO   => n.Children.Length >= 1
                                    ? new bb_arbno(BuildNode(n.Children[0]))
                                    : new bb_eps(),

            // Function call — may be a pattern builtin with dynamic args
            IrKind.E_FNC  => BuildFncPattern(n),

            _ => new bb_lit("")   // safe fallback
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
        return new bb_alt(parts.Select(BuildNode).ToArray());
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
        if (parts.Count == 0) return new bb_eps();
        if (parts.Count == 1) return BuildNode(parts[0]);

        // Build left-to-right as a list of IByrdBox, handling capture wrapping:
        // When a part is E_CAPT_COND_ASGN or E_CAPT_IMMED_ASGN, it wraps the
        // immediately preceding box (its left sibling in the pattern sequence).
        var boxes = new List<IByrdBox>();
        foreach (var part in parts)
        {
            bool isCond  = part.Kind == IrKind.E_CAPT_COND_ASGN;
            bool isImmed = part.Kind == IrKind.E_CAPT_IMMED_ASGN;
            if ((isCond || isImmed) && boxes.Count > 0)
            {
                // Pop the last box — this is what the capture matches over
                var prev    = boxes[^1];
                boxes.RemoveAt(boxes.Count - 1);
                var varName = part.Children.Length > 0 && part.Children[0].Kind == IrKind.E_VAR
                            ? part.Children[0].SVal!
                            : (part.SVal ?? "");
                var cap = new bb_capture(prev, varName, immediate: isImmed) { SetVar = _setVar };
                _captures.Add(cap);
                boxes.Add(cap);
            }
            else if (part.Kind == IrKind.E_CAPT_CURSOR)
            {
                // @var — cursor capture wraps Eps (records position, not span)
                var varName = part.Children.Length > 0 && part.Children[0].Kind == IrKind.E_VAR
                            ? part.Children[0].SVal!
                            : (part.SVal ?? "");
                boxes.Add(new bb_atp(varName) { SetVar = _setVar });
            }
            else
            {
                boxes.Add(BuildNode(part));
            }
        }

        if (boxes.Count == 1) return boxes[0];
        // Right-fold into bb_seq chain
        IByrdBox right = boxes[^1];
        for (int i = boxes.Count - 2; i >= 0; i--)
            right = new bb_seq(boxes[i], right);
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
        var inner   = new bb_eps();
        var box     = new bb_capture(inner, varName, immediate: false) { SetVar = _setVar };
        _captures.Add(box);
        return box;
    }

    private IByrdBox BuildCaptureImm(IrNode n)
    {
        var varName = n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR
                    ? n.Children[0].SVal!
                    : (n.SVal ?? "");
        var inner   = new bb_eps();
        return new bb_capture(inner, varName, immediate: true) { SetVar = _setVar };
    }

    private IByrdBox BuildCaptCursor(IrNode n)
    {
        var varName = n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR
                    ? n.Children[0].SVal!
                    : (n.SVal ?? "");
        return new bb_atp(varName) { SetVar = _setVar };
    }

    // ── Deferred ─────────────────────────────────────────────────────────────

    private IByrdBox BuildDeferred(IrNode n)
    {
        if (n.Children.Length > 0 && n.Children[0].Kind == IrKind.E_VAR)
        {
            var name = n.Children[0].SVal!;
            return new bb_dvar(name)
                   { GetStringVar = _getStringVar, GetPatternVar = _getPatternVar };
        }
        var val = _evalNode(n.Children.Length > 0 ? n.Children[0] : n).ToString();
        return new bb_lit(val);
    }

    // ── Variable ─────────────────────────────────────────────────────────────

    private IByrdBox BuildVar(string name)
    {
        var patBox = _getPatternVar(name);
        if (patBox != null) return patBox;
        var str = _getStringVar(name);
        return new bb_lit(str);
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
            "LEN"     => new bb_len(IntArg2(0)),
            "POS"     => new bb_pos(IntArg2(0)),
            "RPOS"    => new bb_rpos(IntArg2(0)),
            "TAB"     => new bb_tab(IntArg2(0)),
            "RTAB"    => new bb_rtab(IntArg2(0)),
            "REM"     => new bb_rem(),
            "ANY"     => new bb_any(StrArg2(0)),
            "NOTANY"  => new bb_notany(StrArg2(0)),
            "SPAN"    => new bb_span(StrArg2(0)),
            "BREAK"   => new bb_brk(StrArg2(0)),
            "BREAKX"  => new bb_breakx(StrArg2(0)),
            "BAL"     => new bb_bal(),
            "FENCE"   => new bb_fence(),
            "ABORT"   => new bb_abort(),
            "FAIL"    => new bb_fail(),
            "SUCCEED" => new bb_succeed(),
            "ARB"     => new bb_arb(),
            "ARBNO"   => args.Length >= 1 ? new bb_arbno(BuildNode(args[0])) : new bb_eps(),
            _         => new bb_lit(StrArg2(0))
        };
    }

    // ── Arg helpers ──────────────────────────────────────────────────────────

    private int    IntArg(IrNode n, int i) =>
        n.Children.Length > i ? (int)_evalNode(n.Children[i]).ToInt() : 0;

    private string StrArg(IrNode n, int i) =>
        n.Children.Length > i ? _evalNode(n.Children[i]).ToString() : "";
}
