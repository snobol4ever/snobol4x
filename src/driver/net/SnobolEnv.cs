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

public sealed class DESCR
{
    public static readonly DESCR Null = new(DType.Null, "", 0, 0.0);
    public static readonly DESCR Fail = new(DType.Fail, "", 0, 0.0);

    public DType  Type   { get; }
    public string Str    { get; }
    public long   Int    { get; }
    public double Real   { get; }

    public bool IsFail => Type == DType.Fail;
    public bool IsNull => Type == DType.Null;

    private DESCR(DType t, string s, long i, double r)
    { Type = t; Str = s; Int = i; Real = r; }

    public static DESCR Of(string s) => new(DType.String, s, 0, 0.0);
    public static DESCR Of(long   i) => new(DType.Int,    i.ToString(), i, 0.0);
    public static DESCR Of(double r)
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
    private readonly Dictionary<string, DESCR> _vars =
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

    // Multi-dim array descriptor
    private sealed class SnobolArray
    {
        public string   Proto;           // dimension string e.g. "3" or "2,2" or "-1:1,2"
        public int[]    Lo;              // lower bound per dimension (default 1)
        public int[]    Size;            // size per dimension
        public DESCR?[] Data;            // flat storage, row-major
        public SnobolArray(string proto, int[] lo, int[] size, DESCR? def)
        {
            Proto = proto; Lo = lo; Size = size;
            int total = 1; foreach (var s in size) total *= s;
            Data = new DESCR?[total];
            if (def != null) for (int i = 0; i < Data.Length; i++) Data[i] = def;
        }
        // Convert multi-index to flat index; return -1 if out of bounds
        public int FlatIdx(long[] indices)
        {
            if (indices.Length != Size.Length) return -1;
            int flat = 0;
            for (int d = 0; d < Size.Length; d++)
            {
                int i = (int)indices[d] - Lo[d];
                if (i < 0 || i >= Size[d]) return -1;
                flat = flat * Size[d] + i;
            }
            return flat;
        }
    }

    private readonly List<SnobolArray>                       _arrays = new();
    private readonly List<Dictionary<string, DESCR>>         _tables = new();

    // Parse dimension spec: "3" or "2,2" or "-1:1,2"
    private static (int lo, int size) ParseDimSpec(string spec)
    {
        var colon = spec.IndexOf(':');
        if (colon >= 0)
        {
            int lo = int.Parse(spec[..colon].Trim());
            int hi = int.Parse(spec[(colon+1)..].Trim());
            return (lo, hi - lo + 1);
        }
        int n = int.Parse(spec.Trim());
        return (1, n);  // default lower bound = 1
    }

    // ARRAY(n) or ARRAY("m,n,...") or ARRAY("lo:hi,...") with optional default
    public DESCR ArrayCreate(DESCR[] args)
    {
        if (args.Length < 1) return DESCR.Fail;
        var spec  = args[0].ToString();
        var def   = args.Length >= 2 ? args[1] : (DESCR?)null;
        var parts = spec.Split(',');
        var lo    = new int[parts.Length];
        var size  = new int[parts.Length];
        for (int d = 0; d < parts.Length; d++)
        {
            try { (lo[d], size[d]) = ParseDimSpec(parts[d]); }
            catch { return DESCR.Fail; }
            if (size[d] < 1) return DESCR.Fail;
        }
        // Normalise prototype: single dim → just the size number
        string proto = string.Join(",", size.Select((s,d) =>
            lo[d] == 1 ? s.ToString() : $"{lo[d]}:{lo[d]+s-1}"));
        _arrays.Add(new SnobolArray(proto, lo, size, def));
        return DESCR.Of(TAG_ARRAY | (long)(_arrays.Count - 1));
    }

    public DESCR TableCreate()
    {
        _tables.Add(new Dictionary<string, DESCR>(StringComparer.Ordinal));
        return DESCR.Of(TAG_TABLE | (long)(_tables.Count - 1));
    }

    public bool IsArray(DESCR v)  => v.Type == DType.Int && (v.Int & TAG_MASK) == TAG_ARRAY && (v.Int & IDX_MASK) < _arrays.Count && (v.Int & IDX_MASK) >= 0;
    public bool IsTable(DESCR v)  => v.Type == DType.Int && (v.Int & TAG_MASK) == TAG_TABLE && (v.Int & IDX_MASK) < _tables.Count;

    public string ArrayProto(DESCR handle) => IsArray(handle) ? _arrays[(int)(handle.Int & IDX_MASK)].Proto : "";

    public DESCR ArrayGet(DESCR handle, long idx) => ArrayGetMulti(handle, new[] { idx });

    public DESCR ArrayGetMulti(DESCR handle, long[] indices)
    {
        if (!IsArray(handle)) return DESCR.Null;
        var arr = _arrays[(int)(handle.Int & IDX_MASK)];
        int fi = arr.FlatIdx(indices);
        if (fi < 0) return DESCR.Fail;
        return arr.Data[fi] ?? DESCR.Null;
    }

    public void ArraySet(DESCR handle, long idx, DESCR val) => ArraySetMulti(handle, new[] { idx }, val);

    public void ArraySetMulti(DESCR handle, long[] indices, DESCR val)
    {
        if (!IsArray(handle)) return;
        var arr = _arrays[(int)(handle.Int & IDX_MASK)];
        int fi = arr.FlatIdx(indices);
        if (fi >= 0) arr.Data[fi] = val;
    }

    public DESCR TableGet(DESCR handle, string key)
    {
        if (!IsTable(handle)) return DESCR.Null;
        var tbl = _tables[(int)(handle.Int & IDX_MASK)];
        return tbl.TryGetValue(key, out var v) ? v : DESCR.Null;
    }

    public void TableSet(DESCR handle, string key, DESCR val)
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
    private readonly List<(string TypeName, DESCR[] Fields)> _dataObjs = new();

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

    private DESCR DataBuiltin(string spec)
    {
        DataDefine(spec);
        return DESCR.Null;
    }

    public DESCR DataCreate(string typeName, DESCR[] args)
    {
        if (!_dataTypes.TryGetValue(typeName, out var def)) return DESCR.Fail;
        var fields = new DESCR[def.Fields.Length];
        for (int i = 0; i < fields.Length; i++)
            fields[i] = i < args.Length ? args[i] : DESCR.Null;
        _dataObjs.Add((typeName, fields));
        return DESCR.Of(TAG_DATA | (long)(_dataObjs.Count - 1));
    }

    public bool IsDataObj(DESCR v) => v.Type == DType.Int && (v.Int & TAG_MASK) == TAG_DATA && (v.Int & IDX_MASK) < _dataObjs.Count;

    public DESCR DataGetField(DESCR handle, string fieldName)
    {
        if (!IsDataObj(handle)) return DESCR.Null;
        var (typeName, fields) = _dataObjs[(int)(handle.Int & IDX_MASK)];
        if (!_dataTypes.TryGetValue(typeName, out var def)) return DESCR.Null;
        int fi = Array.IndexOf(def.Fields, fieldName);
        if (fi < 0) fi = Array.FindIndex(def.Fields, f => string.Equals(f, fieldName, StringComparison.OrdinalIgnoreCase));
        return fi >= 0 ? fields[fi] : DESCR.Null;
    }

    public void DataSetField(DESCR handle, string fieldName, DESCR val)
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
        Set("&ANCHOR",   DESCR.Of(0L));
        Set("&TRIM",     DESCR.Of(1L));  // SPITBOL default: trim trailing whitespace from input
        Set("&FULLSCAN", DESCR.Of(0L));
        Set("&STCOUNT",  DESCR.Of(0L));
        Set("&STNO",     DESCR.Of(0L));
        Set("&STLIMIT",  DESCR.Of(50000L));
        Set("&ALPHABET", DESCR.Of(new string(Enumerable.Range(0, 256).Select(i => (char)i).ToArray())));
        Set("&UCASE",    DESCR.Of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
        Set("&LCASE",    DESCR.Of("abcdefghijklmnopqrstuvwxyz"));
        Set("OUTPUT",    DESCR.Null);
        Set("INPUT",     DESCR.Null);
    }

    public DESCR Get(string name) =>
        _vars.TryGetValue(name, out var v) ? v : DESCR.Null;

    public void Set(string name, DESCR val) =>
        _vars[name.ToUpperInvariant()] = val;

    public void IncrStcount()
    {
        var v = Get("&STCOUNT");
        _vars["&STCOUNT"] = DESCR.Of(v.Type == DType.Int ? v.Int + 1 : 1L);
        var v2 = Get("&STNO");
        _vars["&STNO"] = DESCR.Of(v2.Type == DType.Int ? v2.Int + 1 : 1L);
    }

    // User-defined functions: name → (paramNames, localNames, bodyLabel)
    public sealed record FuncDef(string[] Params, string[] Locals, string BodyLabel);
    private readonly Dictionary<string, FuncDef> _funcs =
        new(StringComparer.OrdinalIgnoreCase);

    public void DefineFunc(string spec, string? entryLabel = null)
    {
        // spec: "FUNCNAME(p1,p2,p3)l1,l2,l3"  (locals after closing paren)
        var paren = spec.IndexOf('(');
        if (paren < 0) { _funcs[spec.ToUpperInvariant()] = new FuncDef(Array.Empty<string>(), Array.Empty<string>(), (entryLabel ?? spec).ToUpperInvariant()); return; }
        var name     = spec[..paren].Trim().ToUpperInvariant();
        var closeParen = spec.LastIndexOf(')');
        var inner    = spec[(paren+1)..closeParen].Trim();
        var localStr = closeParen < spec.Length - 1 ? spec[(closeParen+1)..].TrimStart(',').Trim() : "";
        var pars = inner.Split(',').Select(p => p.Trim().ToUpperInvariant()).Where(p => p.Length > 0).ToArray();
        var locs = localStr.Split(',').Select(l => l.Trim().ToUpperInvariant()).Where(l => l.Length > 0).ToArray();
        _funcs[name] = new FuncDef(pars, locs, (entryLabel ?? name).ToUpperInvariant());
    }

    public FuncDef? GetFunc(string name) =>
        _funcs.TryGetValue(name, out var f) ? f : null;

    // ── Builtin dispatch ─────────────────────────────────────────────────────

    public DESCR CallBuiltin(string name, DESCR[] args)
    {
        return name.ToUpperInvariant() switch
        {
            "SIZE"    => args.Length >= 1 ? DESCR.Of((long)args[0].ToString().Length) : DESCR.Of(0L),
            "ARG"     => ArgOrLocal(args, local: false),
            "LOCAL"   => ArgOrLocal(args, local: true),
            "DUPL"    => Dupl(args),
            "SUBSTR"  => Substr(args),
            "REPLACE" => Replace(args),
            "TRIM"    => args.Length >= 1 ? DESCR.Of(args[0].ToString().TrimEnd()) : DESCR.Null,
            "LTRIM"   => args.Length >= 1 ? DESCR.Of(args[0].ToString().TrimStart()) : DESCR.Null,
            "LPAD"    => Lpad(args, false),
            "RPAD"    => Lpad(args, true),
            "CONVERT" => Convert_(args),
            "INTEGER" => args.Length >= 1 && long.TryParse(args[0].ToString(), out var iv) ? DESCR.Of(iv) : DESCR.Fail,
            "REAL"    => args.Length >= 1 && double.TryParse(args[0].ToString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var rv) ? DESCR.Of(rv) : DESCR.Fail,
            "STRING"  => args.Length >= 1 ? DESCR.Of(args[0].ToString()) : DESCR.Null,
            "DATA"    => args.Length >= 1 ? DataBuiltin(args[0].ToString()) : DESCR.Fail,
            "IDENT"   => (args.Length >= 2 && args[0].ToString() == args[1].ToString()) ? DESCR.Null : DESCR.Fail,
            "DIFFER"  => (args.Length >= 2 && args[0].ToString() != args[1].ToString()) ? DESCR.Null : DESCR.Fail,
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
            "REMDR"   => args.Length >= 2 ? DESCR.Of(args[0].ToInt() % Math.Max(1, args[1].ToInt())) : DESCR.Fail,
            "ABS"     => args.Length >= 1 ? DESCR.Of(Math.Abs(args[0].ToInt())) : DESCR.Fail,
            "MAX"     => args.Length >= 2 ? DESCR.Of(Math.Max(args[0].ToInt(), args[1].ToInt())) : DESCR.Fail,
            "MIN"     => args.Length >= 2 ? DESCR.Of(Math.Min(args[0].ToInt(), args[1].ToInt())) : DESCR.Fail,
            "CHAR"    => args.Length >= 1 ? DESCR.Of(((char)(int)args[0].ToInt()).ToString()) : DESCR.Fail,
            "ORD"     => args.Length >= 1 && args[0].ToString().Length > 0 ? DESCR.Of((long)args[0].ToString()[0]) : DESCR.Fail,
            "REVERSE" => args.Length >= 1 ? DESCR.Of(new string(args[0].ToString().Reverse().ToArray())) : DESCR.Null,
            "UPPER"   => args.Length >= 1 ? DESCR.Of(args[0].ToString().ToUpperInvariant()) : DESCR.Null,
            "UCASE"   => args.Length >= 1 ? DESCR.Of(args[0].ToString().ToUpperInvariant()) : DESCR.Null,
            "LOWER"   => args.Length >= 1 ? DESCR.Of(args[0].ToString().ToLowerInvariant()) : DESCR.Null,
            "LCASE"   => args.Length >= 1 ? DESCR.Of(args[0].ToString().ToLowerInvariant()) : DESCR.Null,
            "ARRAY"   => args.Length >= 1 ? this.ArrayCreate(args) : DESCR.Fail,
            "TABLE"   => this.TableCreate(),
            "PROTOTYPE" => args.Length >= 1 ? Prototype(args[0]) : DESCR.Fail,
            "DATATYPE"  => args.Length >= 1 ? DataType(args[0]) : DESCR.Fail,
            "TYPE"      => args.Length >= 1 ? DataType(args[0]) : DESCR.Fail,
            "ITEM"      => Item(args),
            "VALUE"     => ValueBuiltin(args),
            "APPLY"   => DESCR.Fail,  // stub
            "EVAL"    => DESCR.Fail,  // stub — M-NET-INTERP-B03
            _         => DESCR.Fail
        };
    }

    // DATATYPE: returns the SNOBOL4 type name — DATA objects use their type name, not "INT"
    private DESCR DataType(DESCR v)
    {
        if (v.IsNull) return DESCR.Of("STRING");
        if (v.IsFail) return DESCR.Fail;
        if (IsTable(v)) return DESCR.Of("TABLE");
        if (IsArray(v)) return DESCR.Of("ARRAY");
        if (IsDataObj(v)) return DESCR.Of(_dataObjs[(int)(v.Int & IDX_MASK)].TypeName.ToUpperInvariant());  // FIX: was (int)v.Int, missing mask
        return v.Type switch
        {
            DType.Int    => DESCR.Of("INTEGER"),
            DType.Real   => DESCR.Of("REAL"),
            DType.String => DESCR.Of("STRING"),
            _            => DESCR.Of("STRING")
        };
    }

    // PROTOTYPE: for arrays returns dimension string e.g. "3" or "2,2"; for tables "TABLE"; strings their length
    private DESCR Prototype(DESCR v)
    {
        if (IsArray(v)) return DESCR.Of(ArrayProto(v));
        if (IsTable(v)) return DESCR.Of("TABLE");
        if (IsDataObj(v))
        {
            var (typeName, _) = _dataObjs[(int)(v.Int & IDX_MASK)];
            if (_dataTypes.TryGetValue(typeName, out var def))
                return DESCR.Of(string.Join(",", def.Fields));
        }
        // For strings: PROTOTYPE returns the string length as string
        return DESCR.Of(v.ToString().Length.ToString());
    }

    // ITEM(arr, i1, i2, ...) — programmatic multi-dim subscript
    private DESCR Item(DESCR[] args)
    {
        if (args.Length < 2) return DESCR.Fail;
        var container = args[0];
        if (IsArray(container))
        {
            var indices = args.Skip(1).Select(a => a.ToInt()).ToArray();
            return ArrayGetMulti(container, indices);
        }
        if (IsTable(container)) return TableGet(container, args[1].ToString());
        return DESCR.Fail;
    }

    // ITEM lvalue support: ItemSet(arr, indices, val)
    public DESCR ItemSet(DESCR container, DESCR[] indexArgs, DESCR val)
    {
        if (IsArray(container))
        {
            var indices = indexArgs.Select(a => a.ToInt()).ToArray();
            ArraySetMulti(container, indices, val);
            return val;
        }
        if (IsTable(container) && indexArgs.Length >= 1)
        { TableSet(container, indexArgs[0].ToString(), val); return val; }
        return DESCR.Fail;
    }

    private DESCR ArgOrLocal(DESCR[] args, bool local)
    {
        if (args.Length < 2) return DESCR.Fail;
        var funcName = args[0].ToString().ToUpperInvariant().TrimStart('.');
        if (!_funcs.TryGetValue(funcName, out var def)) return DESCR.Fail;
        var n = (int)args[1].ToInt();
        var names = local ? def.Locals : def.Params;
        if (n < 1 || n > names.Length) return DESCR.Fail;
        return DESCR.Of(names[n - 1].ToUpperInvariant());
    }

    private static DESCR Dupl(DESCR[] args)
    {
        if (args.Length < 2) return DESCR.Null;
        var s = args[0].ToString();
        var n = (int)args[1].ToInt();
        if (n <= 0) return DESCR.Of("");
        return DESCR.Of(string.Concat(Enumerable.Repeat(s, n)));
    }

    private static DESCR Substr(DESCR[] args)
    {
        if (args.Length < 2) return DESCR.Fail;
        var s   = args[0].ToString();
        var pos = (int)args[1].ToInt() - 1;  // SNOBOL4 is 1-based
        if (pos < 0 || pos > s.Length) return DESCR.Fail;
        if (args.Length >= 3)
        {
            var len = (int)args[2].ToInt();
            if (pos + len > s.Length) return DESCR.Fail;
            return DESCR.Of(s.Substring(pos, len));
        }
        return DESCR.Of(s[pos..]);
    }

    private static DESCR Replace(DESCR[] args)
    {
        if (args.Length < 3) return DESCR.Fail;
        var s   = args[0].ToString();
        var from = args[1].ToString();
        var to   = args[2].ToString();
        if (from.Length != to.Length) return DESCR.Fail;
        var result = new System.Text.StringBuilder(s.Length);
        foreach (char c in s)
        {
            int idx = from.IndexOf(c);
            result.Append(idx >= 0 ? to[idx] : c);
        }
        return DESCR.Of(result.ToString());
    }

    private static DESCR Lpad(DESCR[] args, bool right)
    {
        if (args.Length < 2) return DESCR.Fail;
        var s = args[0].ToString();
        var n = (int)args[1].ToInt();
        var ch = args.Length >= 3 ? args[2].ToString() : " ";
        var pad = ch.Length > 0 ? ch[0] : ' ';
        return DESCR.Of(right ? s.PadRight(n, pad) : s.PadLeft(n, pad));
    }

    private DESCR Convert_(DESCR[] args)
    {
        if (args.Length < 2) return DESCR.Fail;
        var val  = args[0];
        var type = args[1].ToString().ToUpperInvariant();

        // TABLE → ARRAY: Nx2 array, row i = [key, value]
        if (type == "ARRAY" && IsTable(val))
        {
            var tbl  = _tables[(int)(val.Int & IDX_MASK)];
            int n    = tbl.Count;
            // prototype "n,2"
            var arr  = new SnobolArray($"{n},2", new[]{1,1}, new[]{n,2}, null);
            int row  = 0;
            foreach (var kv in tbl)
            {
                arr.Data[row * 2]     = DESCR.Of(kv.Key);
                arr.Data[row * 2 + 1] = kv.Value;
                row++;
            }
            _arrays.Add(arr);
            return DESCR.Of(TAG_ARRAY | (long)(_arrays.Count - 1));
        }

        // ARRAY → TABLE: expects Nx2 array; col1=key, col2=value
        if (type == "TABLE" && IsArray(val))
        {
            var src = _arrays[(int)(val.Int & IDX_MASK)];
            var d   = new Dictionary<string, DESCR>(StringComparer.Ordinal);
            if (src.Size.Length == 2 && src.Size[1] == 2)
            {
                for (int r = 0; r < src.Size[0]; r++)
                {
                    var key = src.Data[r * 2]?.ToString() ?? "";
                    var v2  = src.Data[r * 2 + 1] ?? DESCR.Null;
                    d[key]  = v2;
                }
            }
            _tables.Add(d);
            return DESCR.Of(TAG_TABLE | (long)(_tables.Count - 1));
        }

        return type switch
        {
            "INTEGER" => val.Type == DType.Real
                ? DESCR.Of((long)val.Real)
                : long.TryParse(val.ToString(), out var iv) ? DESCR.Of(iv) : DESCR.Fail,
            "REAL"    => val.Type == DType.Real
                ? val
                : double.TryParse(val.ToString(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out var rv) ? DESCR.Of(rv) : DESCR.Fail,
            "STRING"  => DESCR.Of(val.ToString()),
            _         => DESCR.Fail
        };
    }

    // VALUE(name) — dereference a variable by name, fail if unset
    private DESCR ValueBuiltin(DESCR[] args)
    {
        if (args.Length < 1) return DESCR.Fail;
        var name = args[0].ToString().ToUpperInvariant();
        var v    = Get(name);
        return v.IsNull ? DESCR.Fail : v;
    }

    // Numeric comparison — both args must be numeric (Int or Real), else Fail
    private static DESCR NumCmp(DESCR[] args, Func<double,double,bool> pred)
    {
        if (args.Length < 2) return DESCR.Fail;
        var a = args[0]; var b = args[1];
        bool aNum = a.Type == DType.Int || a.Type == DType.Real;
        bool bNum = b.Type == DType.Int || b.Type == DType.Real;
        // Coerce string if it looks numeric
        if (!aNum && double.TryParse(a.ToString(), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out _)) aNum = true;
        if (!bNum && double.TryParse(b.ToString(), System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out _)) bNum = true;
        if (!aNum || !bNum) return DESCR.Fail;
        double av = a.Type == DType.Real ? a.Real : (double)a.ToInt();
        double bv = b.Type == DType.Real ? b.Real : (double)b.ToInt();
        return pred(av, bv) ? DESCR.Of("") : DESCR.Fail;
    }

    // Lexical comparison — compare as strings, return null string on success (SNOBOL4 spec)
    private static DESCR LexCmp(DESCR[] args, Func<int,bool> pred)
    {
        if (args.Length < 2) return DESCR.Fail;
        int cmp = string.Compare(args[0].ToString(), args[1].ToString(), StringComparison.Ordinal);
        return pred(cmp) ? DESCR.Of("") : DESCR.Fail;
    }
}
