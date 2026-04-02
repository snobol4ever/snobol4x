// BbLen.cs — LEN: match exactly n characters
// Mirrors src/runtime/boxes/bb_len.c
//
// α: if cursor+n > Ω → ω;  cursor += n; γ
// β: cursor -= n; ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbLen : IByrdBox
{
    private readonly int _n;
    public BbLen(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor + _n > ms.Length)  return Spec.Fail;
        var result = Spec.Of(ms.Cursor, _n);
        ms.Cursor += _n;
        return result;
    }

    public Spec Beta(MatchState ms)
    {
        ms.Cursor -= _n;
        return Spec.Fail;
    }
}
