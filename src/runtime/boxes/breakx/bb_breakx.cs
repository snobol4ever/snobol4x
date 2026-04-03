// bb_breakx.cs — BREAKX: like BREAK but fails if zero chars consumed
// Mirrors src/runtime/boxes/breakx/bb_breakx.c
//
// α: scan forward while char NOT in charset;
//    zero advance OR hit EOS → ω;
//    cursor += count; γ(count chars)
// β: cursor -= count; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_breakx : IByrdBox
{
    private readonly string _chars;
    private int _count;

    public bb_breakx(string chars) { _chars = chars ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        _count = 0;
        while (ms.Cursor + _count < ms.Length &&
               !ms.CharInSet(ms.Cursor + _count, _chars))
            _count++;
        // BREAKX: zero advance OR hit EOS → ω
        if (_count == 0 || ms.Cursor + _count >= ms.Length) return Spec.Fail;
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
