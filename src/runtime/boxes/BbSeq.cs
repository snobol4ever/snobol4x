// BbSeq.cs — SEQ: concatenation — left then right; β retries right then left
// Mirrors src/runtime/boxes/bb_seq.c
//
// State machine (mirrors the C goto model exactly):
//   α:        try left.α
//             left_γ → try right.α
//             right_γ → SEQ_γ
//             right_ω → left.β → left_γ / left_ω
//             left_ω  → SEQ_ω
//   β:        try right.β
//             right_γ → SEQ_γ
//             right_ω → left.β → ...

namespace Snobol4.Runtime.Boxes;

public sealed class BbSeq : IByrdBox
{
    private readonly IByrdBox _left;
    private readonly IByrdBox _right;
    private Spec _matched;

    public BbSeq(IByrdBox left, IByrdBox right)
    {
        _left  = left;
        _right = right;
    }

    public Spec Alpha(MatchState ms)
    {
        _matched = Spec.ZeroWidth(ms.Cursor);
        return TryLeft(ms, fromAlpha: true);
    }

    public Spec Beta(MatchState ms)
    {
        return TryRight(ms, fromAlpha: false);
    }

    // ── internal state machine ──────────────────────────────────────────

    private Spec TryLeft(MatchState ms, bool fromAlpha)
    {
        var lr = fromAlpha ? _left.Alpha(ms) : _left.Beta(ms);
        if (lr.IsFail) return Spec.Fail;                    // left_ω → SEQ_ω
        // left_γ
        _matched = _matched.Cat(lr);
        return TryRight(ms, fromAlpha: true);
    }

    private Spec TryRight(MatchState ms, bool fromAlpha)
    {
        while (true)
        {
            var rr = fromAlpha ? _right.Alpha(ms) : _right.Beta(ms);
            if (!rr.IsFail)
                return _matched.Cat(rr);                    // right_γ → SEQ_γ

            // right_ω → retry left with β
            var lr = _left.Beta(ms);
            if (lr.IsFail) return Spec.Fail;                // left_ω → SEQ_ω
            _matched = _matched.Cat(lr);
            fromAlpha = true;                               // left_γ → retry right.α
        }
    }
}
