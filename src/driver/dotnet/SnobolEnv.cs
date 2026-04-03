// SnobolEnv.cs — Variable store and builtin function dispatch
//
// Mirrors the NV store (snobol4.c) and the SNOBOL4 builtin table.
// Keeps types as DESCR values (string / int / real).
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01

namespace ScripInterp;

// ── DESCR — universal value (mirrors DESCR.cs in runtime/net/) ────────────

public enum DType { Null, String, Int, Real, Fail }

public sealed class SnobolVal
{
    public static readonly SnobolVal Null = new(DType.Null, "", 0, 0.0);
    public static readonly SnobolVal Fail = new(DType.Fail, "", 0, 0.0);

    public DType  Type   { get; }
    public string Str    { get; }
    public long   Int    { get; }
    public double Real   { get; }

    public bool IsFail => Type == DType.Fail;
    public bool IsNull => Type == DType.Null;

    private SnobolVal(DType t, string s, long i, double r)
    { Type = t; Str = s; Int = i; Real = r; }

    public static SnobolVal Of(string s) => new(DType.String, s, 0, 0.0);
    public static SnobolVal Of(long   i) => new(DType.Int,    i.ToString(), i, 0.0);
    public static SnobolVal Of(double r)
    {
        // SNOBOL4 real format: strip trailing zeros after decimal, keep point
        // 1.0 → "1."   0.5 → ".5"   1.5 → "1.5"   0.0 → "0."
        string s = r.ToString("G", System.Globalization.CultureInfo.InvariantCulture);
        // If no decimal point, it's been formatted as integer-like — add point
        if (!s.Contains('.') && !s.Contains('E') && !s.Contains('e'))
            s += ".";
        // Strip trailing zeros after decimal (but keep the point itself)
        if (s.Contains('.'))
        {
            s = s.TrimEnd('0');
            if (s.EndsWith('.')) { /* keep trailing dot */ }
        }
        // "0." edge case
        return new(DType.Real, s, 0, r);
    }

    public override string ToString() => Type switch
    {
        DType.String => Str,
        DType.Int    => Int.ToString(),
        DType.Real   => Real.ToString(),
        _            => ""
    };

    // Coerce to integer (for LEN, POS etc.)
    public long ToInt() => Type switch
    {
        DType.Int    => Int,
        DType.Real   => (long)Real,
        DType.String => long.TryParse(Str, out var v) ? v : 0,
        _            => 0
    };
}

// ── SnobolEnv ─────────────────────────────────────────────────────────────

public sealed class SnobolEnv
{
    private readonly Dictionary<string, SnobolVal> _vars =
        new(StringComparer.OrdinalIgnoreCase);

    // Predefined system variables
    public SnobolEnv()
    {
        Set("&ANCHOR",  SnobolVal.Of(0L));
        Set("&TRIM",    SnobolVal.Of(0L));
        Set("&FULLSCAN",SnobolVal.Of(0L));
        Set("&STCOUNT", SnobolVal.Of(0L));
        Set("&STLIMIT", SnobolVal.Of(50000L));
        Set("OUTPUT",   SnobolVal.Null);
        Set("INPUT",    SnobolVal.Null);
    }

    public SnobolVal Get(string name) =>
        _vars.TryGetValue(name, out var v) ? v : SnobolVal.Null;

    public void Set(string name, SnobolVal val) =>
        _vars[name.ToUpperInvariant()] = val;

    // User-defined functions: name → (paramNames, localNames, bodyLabel)
    public sealed record FuncDef(string[] Params, string[] Locals, string BodyLabel);
    private readonly Dictionary<string, FuncDef> _funcs =
        new(StringComparer.OrdinalIgnoreCase);

    public void DefineFunc(string spec, string? entryLabel = null)
    {
        // spec: "FUNCNAME(p1,p2,p3:l1,l2)"
        var paren = spec.IndexOf('(');
        if (paren < 0) { _funcs[spec.ToUpperInvariant()] = new FuncDef(Array.Empty<string>(), Array.Empty<string>(), (entryLabel ?? spec).ToUpperInvariant()); return; }
        var name  = spec[..paren].Trim().ToUpperInvariant();
        var inner = spec[(paren+1)..spec.LastIndexOf(')')].Trim();
        var colonIdx = inner.IndexOf(':');
        string paramStr = colonIdx >= 0 ? inner[..colonIdx] : inner;
        string localStr = colonIdx >= 0 ? inner[(colonIdx+1)..] : "";
        var pars  = paramStr.Split(',').Select(p => p.Trim().ToUpperInvariant()).Where(p => p.Length > 0).ToArray();
        var locs  = localStr.Split(',').Select(l => l.Trim().ToUpperInvariant()).Where(l => l.Length > 0).ToArray();
        _funcs[name] = new FuncDef(pars, locs, (entryLabel ?? name).ToUpperInvariant());
    }

    public FuncDef? GetFunc(string name) =>
        _funcs.TryGetValue(name, out var f) ? f : null;

    // ── Builtin dispatch ─────────────────────────────────────────────────────

    public SnobolVal CallBuiltin(string name, SnobolVal[] args)
    {
        return name.ToUpperInvariant() switch
        {
            "SIZE"    => args.Length >= 1 ? SnobolVal.Of((long)args[0].ToString().Length) : SnobolVal.Of(0L),
            "DUPL"    => Dupl(args),
            "SUBSTR"  => Substr(args),
            "REPLACE" => Replace(args),
            "TRIM"    => args.Length >= 1 ? SnobolVal.Of(args[0].ToString().TrimEnd()) : SnobolVal.Null,
            "LTRIM"   => args.Length >= 1 ? SnobolVal.Of(args[0].ToString().TrimStart()) : SnobolVal.Null,
            "LPAD"    => Lpad(args, false),
            "RPAD"    => Lpad(args, true),
            "CONVERT" => Convert_(args),
            "INTEGER" => args.Length >= 1 && long.TryParse(args[0].ToString(), out var iv) ? SnobolVal.Of(iv) : SnobolVal.Fail,
            "REAL"    => args.Length >= 1 && double.TryParse(args[0].ToString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var rv) ? SnobolVal.Of(rv) : SnobolVal.Fail,
            "STRING"  => args.Length >= 1 ? SnobolVal.Of(args[0].ToString()) : SnobolVal.Null,
            "IDENT"   => (args.Length >= 2 && args[0].ToString() == args[1].ToString()) ? SnobolVal.Null : SnobolVal.Fail,
            "DIFFER"  => (args.Length >= 2 && args[0].ToString() != args[1].ToString()) ? SnobolVal.Null : SnobolVal.Fail,
            "LT"      => Cmp(args, (a,b) => a < b),
            "LE"      => Cmp(args, (a,b) => a <= b),
            "GT"      => Cmp(args, (a,b) => a > b),
            "GE"      => Cmp(args, (a,b) => a >= b),
            "EQ"      => Cmp(args, (a,b) => a == b),
            "NE"      => Cmp(args, (a,b) => a != b),
            "REMDR"   => args.Length >= 2 ? SnobolVal.Of(args[0].ToInt() % Math.Max(1, args[1].ToInt())) : SnobolVal.Fail,
            "ABS"     => args.Length >= 1 ? SnobolVal.Of(Math.Abs(args[0].ToInt())) : SnobolVal.Fail,
            "MAX"     => args.Length >= 2 ? SnobolVal.Of(Math.Max(args[0].ToInt(), args[1].ToInt())) : SnobolVal.Fail,
            "MIN"     => args.Length >= 2 ? SnobolVal.Of(Math.Min(args[0].ToInt(), args[1].ToInt())) : SnobolVal.Fail,
            "CHAR"    => args.Length >= 1 ? SnobolVal.Of(((char)(int)args[0].ToInt()).ToString()) : SnobolVal.Fail,
            "ORD"     => args.Length >= 1 && args[0].ToString().Length > 0 ? SnobolVal.Of((long)args[0].ToString()[0]) : SnobolVal.Fail,
            "REVERSE" => args.Length >= 1 ? SnobolVal.Of(new string(args[0].ToString().Reverse().ToArray())) : SnobolVal.Null,
            "UPPER"   => args.Length >= 1 ? SnobolVal.Of(args[0].ToString().ToUpperInvariant()) : SnobolVal.Null,
            "UCASE"   => args.Length >= 1 ? SnobolVal.Of(args[0].ToString().ToUpperInvariant()) : SnobolVal.Null,
            "LOWER"   => args.Length >= 1 ? SnobolVal.Of(args[0].ToString().ToLowerInvariant()) : SnobolVal.Null,
            "LCASE"   => args.Length >= 1 ? SnobolVal.Of(args[0].ToString().ToLowerInvariant()) : SnobolVal.Null,
            "ARRAY"   => SnobolVal.Null,  // stub — arrays need SnobolArray type (future)
            "TABLE"   => SnobolVal.Null,  // stub
            "PROTOTYPE" => args.Length >= 1 ? SnobolVal.Of(args[0].Type.ToString().ToUpperInvariant()) : SnobolVal.Fail,
            "DATATYPE" => args.Length >= 1 ? SnobolVal.Of(args[0].Type.ToString().ToUpperInvariant()) : SnobolVal.Fail,
            "TYPE"    => args.Length >= 1 ? SnobolVal.Of(args[0].Type.ToString().ToUpperInvariant()) : SnobolVal.Fail,
            "APPLY"   => SnobolVal.Fail,  // stub
            "EVAL"    => SnobolVal.Fail,  // stub — M-NET-INTERP-B03
            _         => SnobolVal.Fail
        };
    }

    private static SnobolVal Dupl(SnobolVal[] args)
    {
        if (args.Length < 2) return SnobolVal.Null;
        var s = args[0].ToString();
        var n = (int)args[1].ToInt();
        if (n <= 0) return SnobolVal.Of("");
        return SnobolVal.Of(string.Concat(Enumerable.Repeat(s, n)));
    }

    private static SnobolVal Substr(SnobolVal[] args)
    {
        if (args.Length < 2) return SnobolVal.Fail;
        var s   = args[0].ToString();
        var pos = (int)args[1].ToInt() - 1;  // SNOBOL4 is 1-based
        if (pos < 0 || pos > s.Length) return SnobolVal.Fail;
        if (args.Length >= 3)
        {
            var len = (int)args[2].ToInt();
            if (pos + len > s.Length) return SnobolVal.Fail;
            return SnobolVal.Of(s.Substring(pos, len));
        }
        return SnobolVal.Of(s[pos..]);
    }

    private static SnobolVal Replace(SnobolVal[] args)
    {
        if (args.Length < 3) return SnobolVal.Fail;
        var s   = args[0].ToString();
        var from = args[1].ToString();
        var to   = args[2].ToString();
        if (from.Length != to.Length) return SnobolVal.Fail;
        var result = new System.Text.StringBuilder(s.Length);
        foreach (char c in s)
        {
            int idx = from.IndexOf(c);
            result.Append(idx >= 0 ? to[idx] : c);
        }
        return SnobolVal.Of(result.ToString());
    }

    private static SnobolVal Lpad(SnobolVal[] args, bool right)
    {
        if (args.Length < 2) return SnobolVal.Fail;
        var s = args[0].ToString();
        var n = (int)args[1].ToInt();
        var ch = args.Length >= 3 ? args[2].ToString() : " ";
        var pad = ch.Length > 0 ? ch[0] : ' ';
        return SnobolVal.Of(right ? s.PadRight(n, pad) : s.PadLeft(n, pad));
    }

    private static SnobolVal Convert_(SnobolVal[] args)
    {
        if (args.Length < 2) return SnobolVal.Fail;
        var val  = args[0];
        var type = args[1].ToString().ToUpperInvariant();
        return type switch
        {
            "INTEGER" => long.TryParse(val.ToString(), out var iv) ? SnobolVal.Of(iv) : SnobolVal.Fail,
            "REAL"    => double.TryParse(val.ToString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var rv) ? SnobolVal.Of(rv) : SnobolVal.Fail,
            "STRING"  => SnobolVal.Of(val.ToString()),
            _         => SnobolVal.Fail
        };
    }

    private static SnobolVal Cmp(SnobolVal[] args, Func<double,double,bool> pred)
    {
        if (args.Length < 2) return SnobolVal.Fail;
        double a, b;
        if (args[0].Type == DType.Int && args[1].Type == DType.Int)
        { a = args[0].Int; b = args[1].Int; }
        else
        { a = args[0].Type == DType.String ? 0 : (double)args[0].ToInt();
          b = args[1].Type == DType.String ? 0 : (double)args[1].ToInt(); }
        return pred(a, b) ? args[0] : SnobolVal.Fail;
    }
}
