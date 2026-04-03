// bb_rpos.cs — RPOS: assert cursor == Length-n (zero-width)
// Mirrors src/runtime/boxes/rpos/bb_rpos.c
//
// α: if cursor != Length-n → ω;  γ zero-width
// β: ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_rpos : IByrdBox
{
    private readonly int _n;
    public bb_rpos(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor != ms.Length - _n) return Spec.Fail;
        return Spec.ZeroWidth(ms.Cursor);
    }

    public Spec Beta(MatchState ms) => Spec.Fail;
}
