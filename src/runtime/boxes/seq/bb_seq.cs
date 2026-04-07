// bb_seq.cs — SEQ: concatenation — left then right; β retries right then left
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

public sealed class bb_seq : IByrdBox
{
    private readonly IByrdBox _left;
    private readonly IByrdBox _right;
    private Spec _matched;

    public bb_seq(IByrdBox left, IByrdBox right)
    {
        _left  = left;
        _right = right;
    }

    public Spec α(MatchState ms)
    {
        _matched = Spec.ZeroWidth(ms.Cursor);
        return TryLeft(ms, fromα: true);
    }

    public Spec β(MatchState ms)
    {
        return TryRight(ms, fromα: false);
    }

    // ── internal state machine ──────────────────────────────────────────

    private Spec TryLeft(MatchState ms, bool fromα)
    {
        var lr = fromα ? _left.α(ms) : _left.β(ms);
        if (lr.IsFail) return Spec.Fail;                    // left_ω → SEQ_ω
        // left_γ
        _matched = _matched.Cat(lr);
        return TryRight(ms, fromα: true);
    }

    private Spec TryRight(MatchState ms, bool fromα)
    {
        while (true)
        {
            var rr = fromα ? _right.α(ms) : _right.β(ms);
            if (!rr.IsFail)
                return _matched.Cat(rr);                    // right_γ → SEQ_γ

            // right_ω → retry left with β
            var lr = _left.β(ms);
            if (lr.IsFail) return Spec.Fail;                // left_ω → SEQ_ω
            _matched = _matched.Cat(lr);
            fromAlpha = true;                               // left_γ → retry right.α
        }
    }
}
