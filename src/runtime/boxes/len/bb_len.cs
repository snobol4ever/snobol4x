// bb_len.cs — LEN: match exactly n characters
// Mirrors src/runtime/boxes/bb_len.c
//
// α: if cursor+n > Ω → ω;  cursor += n; γ
// β: cursor -= n; ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_len : IByrdBox
{
    private readonly int _n;
    public bb_len(int n) { _n = n; }

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
