// bb_pos.cs — POS: assert cursor == n (zero-width)
// Mirrors src/runtime/boxes/pos/bb_pos.c
//
// α: if cursor != n → ω;  γ zero-width
// β: ω

namespace Snobol4.Runtime.Boxes;

public sealed class bb_pos : IByrdBox
{
    private readonly int _n;
    public bb_pos(int n) { _n = n; }

    public Spec Alpha(MatchState ms)
    {
        if (ms.Cursor != _n) return Spec.Fail;
        return Spec.ZeroWidth(ms.Cursor);
    }

    public Spec Beta(MatchState ms) => Spec.Fail;
}
