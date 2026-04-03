// ByrdBoxExecutor.cs — Phase 3 trampoline: drives the box graph
// Mirrors Scanner.Match() in snobol4dotnet and stmt_exec.c Phase 3.
//
// Given a root IByrdBox and a MatchState, runs the scan loop:
//   for cursor = startPos .. Length:
//     try root.Alpha()
//     on success: commit captures, return MatchResult.Success
//     on failure: try root.Beta() for backtrack within same start pos
//   return MatchResult.Failure
//
// Phase 5 (capture commit) is triggered here on :S.
// The caller (ThreadedExecuteLoop or scrip-cc emitted code) handles the
// subject splice and variable assignment.

namespace Snobol4.Runtime.Boxes;

public readonly struct MatchResult
{
    public readonly bool   Success;
    public readonly int    MatchStart;
    public readonly int    MatchLength;
    public readonly int    CursorAfter;

    private MatchResult(bool ok, int start, int len, int after)
    { Success = ok; MatchStart = start; MatchLength = len; CursorAfter = after; }

    public static MatchResult Succeeded(int start, int len, int after) =>
        new(true,  start, len, after);
    public static readonly MatchResult Failed = new(false, 0, 0, 0);
}

public sealed class ByrdBoxExecutor
{
    private readonly IByrdBox           _root;
    private readonly IReadOnlyList<bb_capture> _captures;

    public ByrdBoxExecutor(IByrdBox root, IReadOnlyList<bb_capture> captures)
    {
        _root     = root;
        _captures = captures;
    }

    // ── Run — Phase 3 scan loop ─────────────────────────────────────────────
    // anchor=true: try only at startPos (mirrors &ANCHOR non-zero)
    // anchor=false: try at each position from startPos .. Length

    public MatchResult Run(MatchState ms, bool anchor = false)
    {
        int startPos = ms.Cursor;
        int limit    = anchor ? startPos : ms.Length;

        for (int pos = startPos; pos <= limit; pos++)
        {
            ms.Cursor = pos;
            int matchStart = pos;

            // Try α entry
            var r = _root.Alpha(ms);

            if (!r.IsFail)
            {
                // Phase 5: commit pending captures
                CommitCaptures();
                return MatchResult.Succeeded(matchStart, ms.Cursor - matchStart, ms.Cursor);
            }

            // α failed — try β backtrack chain within this start position
            while (true)
            {
                r = _root.Beta(ms);
                if (!r.IsFail)
                {
                    CommitCaptures();
                    return MatchResult.Succeeded(matchStart, ms.Cursor - matchStart, ms.Cursor);
                }
                break;   // β exhausted for this position
            }
            // Reset cursor for next start position
            ms.Cursor = pos + 1;
        }

        ms.Cursor = startPos;   // restore on failure
        return MatchResult.Failed;
    }

    private void CommitCaptures()
    {
        foreach (var cap in _captures)
            cap.CommitPending();
    }
}
