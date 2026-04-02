// BbBrk.cs — BREAK: scan to first char in set (zero-advance is OK)
// Mirrors src/runtime/boxes/bb_brk.c
//
// α: scan forward while char NOT in charset;
//    if reached EOS without finding a set-char → ω;
//    cursor += count; γ(count chars)
// β: cursor -= count; ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbBrk : IByrdBox
{
    private readonly string _chars;
    private int _count;

    public BbBrk(string chars) { _chars = chars ?? ""; }

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

// BbBreakx.cs — BREAKX: like BREAK but fails if zero chars consumed
// Mirrors src/runtime/boxes/bb_breakx.c

public sealed class BbBreakx : IByrdBox
{
    private readonly string _chars;
    private int _count;

    public BbBreakx(string chars) { _chars = chars ?? ""; }

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
