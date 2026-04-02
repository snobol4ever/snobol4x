// BbSpan.cs — SPAN: longest prefix of chars in set (must be ≥1)
// Mirrors src/runtime/boxes/bb_span.c
//
// α: scan forward while char in charset; if none matched → ω;
//    cursor += count; γ(count chars)
// β: cursor -= count; ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbSpan : IByrdBox
{
    private readonly string _chars;
    private int _count;

    public BbSpan(string chars) { _chars = chars ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        _count = 0;
        while (ms.CharInSet(ms.Cursor + _count, _chars)) _count++;
        if (_count <= 0) return Spec.Fail;
        var result = Spec.Of(ms.Cursor, _count);
        ms.Cursor += _count;
        return result;
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor -= _count;
        return Spec.Fail;
    }
}
