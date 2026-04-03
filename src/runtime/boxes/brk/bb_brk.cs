// bb_brk.cs — BREAK: scan to first char in set (zero-advance is OK)
// Mirrors src/runtime/boxes/brk/bb_brk.c
//
// α: scan forward while char NOT in charset;
//    if reached EOS without finding a set-char → ω;
//    cursor += count; γ(count chars)
// β: cursor -= count; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_brk : IByrdBox
{
    private readonly string _chars;
    private int _count;

    public bb_brk(string chars) { _chars = chars ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        _count = 0;
        while (ms.Cursor + _count < ms.Length &&
               !ms.CharInSet(ms.Cursor + _count, _chars))
            _count++;
        // must have found a set-char (not hit EOS)
        if (ms.Cursor + _count >= ms.Length) return Spec.Fail;
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
