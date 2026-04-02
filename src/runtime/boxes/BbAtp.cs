// BbAtp.cs — ATP: @var — write cursor position as integer into varname
// Mirrors src/runtime/boxes/bb_atp.c
//
// α: write Δ (cursor, 0-based) to varname; γ zero-width (always succeeds)
// β: ω (no retry — cursor capture is a side-effect, not a match)

namespace Snobol4.Runtime.Boxes;

public sealed class BbAtp : IByrdBox
{
    private readonly string _varname;

    // Injected by executor — writes integer-as-string to IdentifierTable
    public Action<string, string>? SetVar { get; set; }

    public BbAtp(string varname) { _varname = varname ?? ""; }

    public Spec Alpha(MatchState ms)
    {
        if (!string.IsNullOrEmpty(_varname))
            SetVar?.Invoke(_varname, ms.Cursor.ToString());
        return Spec.ZeroWidth(ms.Cursor);             // ATP_γ
    }

    public Spec Beta(MatchState ms) => Spec.Fail;    // ATP_ω
}
