// BbPos.cs — POS: assert cursor == n (zero-width)
// Mirrors src/runtime/boxes/bb_pos.c
//
// α: if cursor != n → ω;  γ zero-width
// β: ω

namespace Snobol4.Runtime.Boxes;

public sealed class BbPos : IByrdBox
{
    private readonly int _n;
    public BbPos(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor != _n) return Spec.Fail;
        return Spec.ZeroWidth(ms.Cursor);
    }

    public Spec Beta(MatchState ms) => Spec.Fail;
}

// BbRpos.cs — RPOS: assert cursor == Length-n (zero-width)
// Mirrors src/runtime/boxes/bb_rpos.c

public sealed class BbRpos : IByrdBox
{
    private readonly int _n;
    public BbRpos(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor != ms.Length - _n) return Spec.Fail;
        return Spec.ZeroWidth(ms.Cursor);
    }

    public Spec Beta(MatchState ms) => Spec.Fail;
}
