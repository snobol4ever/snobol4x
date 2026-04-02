// BbArb.cs — ARB: match 0..N chars lazily; β tries one more
// Mirrors src/runtime/boxes/bb_arb.c
//
// α: count=0; start=cursor; γ(zero-width) — try empty first
// β: count++;  if start+count > Length → ω;
//    cursor = start+count; γ(count chars)

namespace Snobol4.Runtime.Boxes;

public sealed class BbArb : IByrdBox
{
    private int _count;
    private int _start;

    public Spec Alpha(MatchState ms)
    {
        _count = 0;
        _start = ms.Cursor;
        // zero-width success first (lazy — shortest match)
        return Spec.ZeroWidth(ms.Cursor);
    }

    public Spec Beta(MatchState ms)
    {
        _count++;
        if (_start + _count > ms.Length) return Spec.Fail;
        ms.Cursor = _start + _count;
        return Spec.Of(_start, _count);
    }
}
