// bb_lit.cs — LIT: literal string match
// Mirrors src/runtime/boxes/bb_lit.c
//
// α: if cursor+len > Ω → ω
//    if subject[cursor..cursor+len] != lit → ω
//    cursor += len; γ(spec(cursor-len, len))
// β: cursor -= len; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_lit : IByrdBox
{
    private readonly string _lit;
    private readonly int    _len;

    public bb_lit(string lit)
    {
        _lit = lit ?? "";
        _len = _lit.Length;
    }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor + _len > ms.Length)             return Spec.Fail;
        if (!ms.MatchesAt(ms.Cursor, _lit))            return Spec.Fail;
        var result = Spec.Of(ms.Cursor, _len);
        ms.Cursor += _len;
        return result;                                 // γ
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor -= _len;
        return Spec.Fail;                              // ω
    }
}
