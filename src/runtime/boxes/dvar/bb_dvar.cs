// bb_dvar.cs — DVAR: *VAR — re-resolve variable's live value on every α
// Mirrors src/runtime/boxes/bb_dvar.c
//
// On α: read varname from IdentifierTable.
//   If value is a pattern (IByrdBox) → use it as child
//   If value is a string → wrap as bb_lit child
//   Re-build child only when value changes.
// α/β: delegate entirely to child.

namespace Snobol4.Runtime.Boxes;

public sealed class bb_dvar : IByrdBox
{
    private readonly string _varname;

    // Injected by executor
    public Func<string, string>?   GetStringVar   { get; set; }
    public Func<string, IByrdBox?>? GetPatternVar  { get; set; }

    private IByrdBox? _child;
    private string?   _lastStringValue;

    public bb_dvar(string varname) { _varname = varname ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        RebuildChild();
        if (_child == null) return Spec.Fail;
        return _child.Alpha(ms);
    }

    public Spec Beta(MatchState ms)
    {
        if (_child == null) return Spec.Fail;
        return _child.Beta(ms);
    }

    private void RebuildChild()
    {
        // Try pattern value first
        var pat = GetPatternVar?.Invoke(_varname);
        if (pat != null)
        {
            if (!ReferenceEquals(pat, _child))
            {
                _child = pat;
                _lastStringValue = null;
            }
            return;
        }

        // Fall back to string value → wrap as literal
        var s = GetStringVar?.Invoke(_varname) ?? "";
        if (_child is bb_lit && s == _lastStringValue) return;  // unchanged
        _lastStringValue = s;
        _child = new bb_lit(s);
    }
}
