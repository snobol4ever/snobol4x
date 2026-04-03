using System.Linq;
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
        DType.Real   => Str,
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

    // Pattern variable store: name → IR node (set when RHS of assignment is a pattern expression)
    private readonly Dictionary<string, IrNode> _patVars =
        new(StringComparer.OrdinalIgnoreCase);

    public void SetPattern(string name, IrNode patNode) =>
        _patVars[name.ToUpperInvariant()] = patNode;

    public IrNode? GetPattern(string name) =>
        _patVars.TryGetValue(name.ToUpperInvariant(), out var n) ? n : null;

    // ── Array store ───────────────────────────────────────────────────────────
    // Handle encoding: arrays use tag 0x0000_0000, tables 0x1000_0000, data objs 0x2000_0000
    private const long TAG_ARRAY = 0x0000_0000L;
    private const long TAG_TABLE = 0x1000_0000L;
    private const long TAG_DATA  = 0x2000_0000L;
    private const long TAG_MASK  = 0x7000_0000L;
    private const long IDX_MASK  = 0x0FFF_FFFFL;

    private readonly List<SnobolVal?[]>                          _arrays = new();
    private readonly List<Dictionary<string, SnobolVal>>         _tables = new();

    // ARRAY(n) — 1-indexed numeric array; ARRAY(n,v) with default
    public SnobolVal ArrayCreate(SnobolVal[] args)
    {
        int size = args.Length >= 1 ? (int)args[0].ToInt() : 0;
        if (size < 1) return SnobolVal.Fail;
        var arr = new SnobolVal?[size];
        if (args.Length >= 2)
        {
            var def = args[1];
            for (int i = 0; i < arr.Length; i++) arr[i] = def;
        }
        _arrays.Add(arr);
        return SnobolVal.Of(TAG_ARRAY | (long)(_arrays.Count - 1));
    }

    public SnobolVal TableCreate()
    {
        _tables.Add(new Dictionary<string, SnobolVal>(StringComparer.Ordinal));
        return SnobolVal.Of(TAG_TABLE | (long)(_tables.Count - 1));
    }

    public bool IsArray(SnobolVal v)  => v.Type == DType.Int && (v.Int & TAG_MASK) == TAG_ARRAY && (v.Int & IDX_MASK) < _arrays.Count && (v.Int & IDX_MASK) >= 0;
    public bool IsTable(SnobolVal v)  => v.Type == DType.Int && (v.Int & TAG_MASK) == TAG_TABLE && (v.Int & IDX_MASK) < _tables.Count;

    public SnobolVal ArrayGet(SnobolVal handle, long idx)
    {
        if (!IsArray(handle)) return SnobolVal.Null;
        var arr = _arrays[(int)(handle.Int & IDX_MASK)];
        int i = (int)idx - 1;   // SNOBOL4 is 1-based
        if (i < 0 || i >= arr.Length) return SnobolVal.Null;
        return arr[i] ?? SnobolVal.Null;
    }

    public void ArraySet(SnobolVal handle, long idx, SnobolVal val)
    {
        if (!IsArray(handle)) return;
        var arr = _arrays[(int)(handle.Int & IDX_MASK)];
        int i = (int)idx - 1;
        if (i >= 0 && i < arr.Length) arr[i] = val;
    }

    public SnobolVal TableGet(SnobolVal handle, string key)
    {
        if (!IsTable(handle)) return SnobolVal.Null;
        var tbl = _tables[(int)(handle.Int & IDX_MASK)];
        return tbl.TryGetValue(key, out var v) ? v : SnobolVal.Null;
    }

    public void TableSet(SnobolVal handle, string key, SnobolVal val)
    {
        if (!IsTable(handle)) return;
        _tables[(int)(handle.Int & IDX_MASK)][key] = val;
    }

    // ── Data type store ───────────────────────────────────────────────────────
    // Each DATA type is a named record with ordered field names.
    public sealed record DataTypeDef(string TypeName, string[] Fields);
    private readonly Dictionary<string, DataTypeDef> _dataTypes =
        new(StringComparer.OrdinalIgnoreCase);

    // Instances: handle → field values
    private readonly List<(string TypeName, SnobolVal[] Fields)> _dataObjs = new();

    public void DataDefine(string spec)
    {
        // spec like "complex(real,imag)" or "point(x,y)"
        int p = spec.IndexOf('(');
        if (p < 0) return;
        var typeName = spec[..p].Trim();
        var fieldPart = spec[(p + 1)..].TrimEnd(')');
        var fields = fieldPart.Split(',').Select(f => f.Trim()).ToArray();
        _dataTypes[typeName] = new DataTypeDef(typeName, fields);
    }

    public bool IsDataType(string name) => _dataTypes.ContainsKey(name);

    private SnobolVal DataBuiltin(string spec)
    {
        DataDefine(spec);
        return SnobolVal.Null;
    }

    public SnobolVal DataCreate(string typeName, SnobolVal[] args)
    {
        if (!_dataTypes.TryGetValue(typeName, out var def)) return SnobolVal.Fail;
        var fields = new SnobolVal[def.Fields.Length];
        for (int i = 0; i < fields.Length; i++)
            fields[i] = i < args.Length ? args[i] : SnobolVal.Null;
        _dataObjs.Add((typeName, fields));
        return SnobolVal.Of(TAG_DATA | (long)(_dataObjs.Count - 1));
    }

    public bool IsDataObj(SnobolVal v) => v.Type == DType.Int && (v.Int & TAG_MASK) == TAG_DATA && (v.Int & IDX_MASK) < _dataObjs.Count;

    public SnobolVal DataGetField(SnobolVal handle, string fieldName)
    {
        if (!IsDataObj(handle)) return SnobolVal.Null;
        var (typeName, fields) = _dataObjs[(int)(handle.Int & IDX_MASK)];
        if (!_dataTypes.TryGetValue(typeName, out var def)) return SnobolVal.Null;
        int fi = Array.IndexOf(def.Fields, fieldName);
        if (fi < 0) fi = Array.FindIndex(def.Fields, f => string.Equals(f, fieldName, StringComparison.OrdinalIgnoreCase));
        return fi >= 0 ? fields[fi] : SnobolVal.Null;
    }

    public void DataSetField(SnobolVal handle, string fieldName, SnobolVal val)
    {
        if (!IsDataObj(handle)) return;
        var (typeName, fields) = _dataObjs[(int)(handle.Int & IDX_MASK)];
        if (!_dataTypes.TryGetValue(typeName, out var def)) return;
        int fi = Array.FindIndex(def.Fields, f => string.Equals(f, fieldName, StringComparison.OrdinalIgnoreCase));
        if (fi >= 0) fields[fi] = val;
    }

    // Predefined system variables
    public SnobolEnv()
    {
        Set("&ANCHOR",   SnobolVal.Of(0L));
        Set("&TRIM",     SnobolVal.Of(0L));
        Set("&FULLSCAN", SnobolVal.Of(0L));
        Set("&STCOUNT",  SnobolVal.Of(0L));
        Set("&STLIMIT",  SnobolVal.Of(50000L));
        Set("&ALPHABET", SnobolVal.Of(new string(Enumerable.Range(0, 256).Select(i => (char)i).ToArray())));
        Set("&UCASE",    SnobolVal.Of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
        Set("&LCASE",    SnobolVal.Of("abcdefghijklmnopqrstuvwxyz"));
        Set("OUTPUT",    SnobolVal.Null);
        Set("INPUT",     SnobolVal.Null);
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
            "DATA"    => args.Length >= 1 ? DataBuiltin(args[0].ToString()) : SnobolVal.Fail,
            "IDENT"   => (args.Length >= 2 && args[0].ToString() == args[1].ToString()) ? SnobolVal.Null : SnobolVal.Fail,
            "DIFFER"  => (args.Length >= 2 && args[0].ToString() != args[1].ToString()) ? SnobolVal.Null : SnobolVal.Fail,
            // Numeric predicates — strict: both must be numeric, else Fail
            "LT"      => NumCmp(args, (a,b) => a < b),
            "LE"      => NumCmp(args, (a,b) => a <= b),
            "GT"      => NumCmp(args, (a,b) => a > b),
            "GE"      => NumCmp(args, (a,b) => a >= b),
            "EQ"      => NumCmp(args, (a,b) => a == b),
            "NE"      => NumCmp(args, (a,b) => a != b),
            // Lexical string predicates — return first arg on success
            "LLT"     => LexCmp(args, c => c <  0),
            "LLE"     => LexCmp(args, c => c <= 0),
            "LGT"     => LexCmp(args, c => c >  0),
            "LGE"     => LexCmp(args, c => c >= 0),
            "LEQ"     => LexCmp(args, c => c == 0),
            "LNE"     => LexCmp(args, c => c != 0),
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
            "ARRAY"   => args.Length >= 1 ? this.ArrayCreate(args) : SnobolVal.Fail,
            "TABLE"   => this.TableCreate(),
            "PROTOTYPE" => args.Length >= 1 ? DataType(args[0]) : SnobolVal.Fail,
            "DATATYPE"  => args.Length >= 1 ? DataType(args[0]) : SnobolVal.Fail,
            "TYPE"      => args.Length >= 1 ? DataType(args[0]) : SnobolVal.Fail,
            "APPLY"   => SnobolVal.Fail,  // stub
            "EVAL"    => SnobolVal.Fail,  // stub — M-NET-INTERP-B03
            _         => SnobolVal.Fail
        };
    }

    // DATATYPE: returns the SNOBOL4 type name — DATA objects use their type name, not "INT"
    private SnobolVal DataType(SnobolVal v)
    {
        if (v.IsNull) return SnobolVal.Of("STRING");
        if (v.IsFail) return SnobolVal.Fail;
        if (IsTable(v)) return SnobolVal.Of("TABLE");
        if (IsArray(v)) return SnobolVal.Of("ARRAY");
        if (IsDataObj(v)) return SnobolVal.Of(_dataObjs[(int)v.Int].TypeName.ToUpperInvariant());
        return v.Type switch
        {
            DType.Int    => SnobolVal.Of("INTEGER"),
            DType.Real   => SnobolVal.Of("REAL"),
            DType.String => SnobolVal.Of("STRING"),
            _            => SnobolVal.Of("STRING")
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
            "INTEGER" => val.Type == DType.Real
                ? SnobolVal.Of((long)val.Real)
                : long.TryParse(val.ToString(), out var iv) ? SnobolVal.Of(iv) : SnobolVal.Fail,
            "REAL"    => val.Type == DType.Real
                ? val
                : double.TryParse(val.ToString(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out var rv) ? SnobolVal.Of(rv) : SnobolVal.Fail,
            "STRING"  => SnobolVal.Of(val.ToString()),
            _         => SnobolVal.Fail
        };
    }

    // Numeric comparison — both args must be numeric (Int or Real), else Fail
    private static SnobolVal NumCmp(SnobolVal[] args, Func<double,double,bool> pred)
    {
        if (args.Length < 2) return SnobolVal.Fail;
        var a = args[0]; var b = args[1];
        bool aNum = a.Type == DType.Int || a.Type == DType.Real;
        bool bNum = b.Type == DType.Int || b.Type == DType.Real;
        // Coerce string if it looks numeric
        if (!aNum && double.TryParse(a.ToString(), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out _)) aNum = true;
        if (!bNum && double.TryParse(b.ToString(), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out _)) bNum = true;
        if (!aNum || !bNum) return SnobolVal.Fail;
        double av = a.Type == DType.Real ? a.Real : (double)a.ToInt();
        double bv = b.Type == DType.Real ? b.Real : (double)b.ToInt();
        return pred(av, bv) ? args[0] : SnobolVal.Fail;
    }

    // Lexical comparison — compare as strings, return first arg on success
    private static SnobolVal LexCmp(SnobolVal[] args, Func<int,bool> pred)
    {
        if (args.Length < 2) return SnobolVal.Fail;
        int cmp = string.Compare(args[0].ToString(), args[1].ToString(), StringComparison.Ordinal);
        return pred(cmp) ? args[0] : SnobolVal.Fail;
    }
}
