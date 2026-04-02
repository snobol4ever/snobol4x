// BbArbno.cs — ARBNO: zero-or-more greedy; zero-advance guard; β unwinds stack
// Mirrors src/runtime/boxes/bb_arbno.c
//
// Stack of (matched_spec, start_cursor) frames — each greedy iteration
// pushes one frame.  β pops the top frame (shortest match first on retry).
//
// α:  depth=0; push frame(zero-width, cursor);
//     loop: try body.α from current position
//       body_γ and cursor advanced → push new frame, loop
//       body_γ but cursor unchanged → zero-advance guard → stop, γ(top frame)
//       body_ω → γ(top frame)
// β:  if depth <= 0 → ω
//     depth--; restore cursor from frame; γ(frame.matched)

namespace Snobol4.Runtime.Boxes;

public sealed class BbArbno : IByrdBox
{
    private const int StackMax = 64;

    private readonly IByrdBox _body;

    private readonly Spec[] _matchedStack = new Spec[StackMax];
    private readonly int[]  _startStack   = new int[StackMax];
    private int              _depth;

    public BbArbno(IByrdBox body) { _body = body; }

    public Spec Alpha(MatchState ms)
    {
        _depth = 0;
        _matchedStack[0] = Spec.ZeroWidth(ms.Cursor);
        _startStack[0]   = ms.Cursor;

        while (true)
        {
            int startHere = ms.Cursor;
            var br = _body.Alpha(ms);

            if (br.IsFail)
            {
                // body_ω → stop, succeed with accumulated match
                return _matchedStack[_depth];        // ARBNO_γ
            }

            // body_γ — check zero-advance guard
            if (ms.Cursor == startHere)
            {
                // Zero advance: stop here (infinite-loop guard)
                return _matchedStack[_depth];        // ARBNO_γ_now
            }

            // Advance: push new frame
            if (_depth + 1 < StackMax)
            {
                _depth++;
                _matchedStack[_depth] = _matchedStack[_depth - 1].Cat(br);
                _startStack[_depth]   = ms.Cursor;
            }
            // else stack full: stop
            else
            {
                return _matchedStack[_depth];
            }
        }
    }

    public Spec Beta(MatchState ms)
    {
        if (_depth <= 0) return Spec.Fail;           // ARBNO_ω
        _depth--;
        ms.Cursor = _startStack[_depth];
        return _matchedStack[_depth];                // ARBNO_γ (shorter match)
    }
}
