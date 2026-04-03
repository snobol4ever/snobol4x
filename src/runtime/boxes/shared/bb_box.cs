// IByrdBox.cs — Dynamic Byrd Box Runtime for .NET
//
// Mirrors src/runtime/boxes/bb_box.h
//
// Every box implements two entry ports:
//   α()  — fresh entry (α port)
//   β()   — backtrack re-entry (β port)
//
// Return convention (mirrors spec_t):
//   Spec.Fail  — ω fired (failure)
//   any other  — γ fired (success, value = matched substring as Spec)
//
// Global match state is carried in MatchState — equivalent to the C globals Σ/Δ/Ω.
// Boxes receive a reference to MatchState so cursor mutations are shared.
//
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
// SPRINT:  M-NET-BOXES (D-165 pivot)

namespace Snobol4.Runtime.Boxes;

// ── Spec — matched substring descriptor ────────────────────────────────────
// Mirrors spec_t: { const char *σ; int δ; }
// We store (Start, Length) into the subject string rather than a pointer.
// Spec.Fail has Start == -1.

public readonly struct Spec
{
    public readonly int Start;
    public readonly int Length;

    public Spec(int start, int length) { Start = start; Length = length; }
    public bool IsFail => Start < 0;
    public static readonly Spec Fail = new(-1, 0);

    public static Spec Of(int start, int length) => new(start, length);
    public static Spec ZeroWidth(int pos)        => new(pos, 0);
    public Spec Cat(Spec right)                  => new(Start, Length + right.Length);
    public override string ToString()            => IsFail ? "<fail>" : $"({Start},{Length})";
}

// ── MatchState — shared cursor and subject ──────────────────────────────────
// Mirrors the C globals:   extern const char *Σ;  extern int Δ;  extern int Ω;
// Passed by reference into every box so mutations are visible across the graph.

public sealed class MatchState
{
    public string  Subject  { get; }
    public int     Cursor   { get; set; }
    public int     Length   => Subject.Length;

    public MatchState(string subject, int startCursor = 0)
    {
        Subject = subject ?? "";
        Cursor  = startCursor;
    }

    // Convenience: does Subject[Cursor..] start with lit?
    public bool MatchesAt(int pos, string lit)
    {
        if (pos + lit.Length > Subject.Length) return false;
        return Subject.AsSpan(pos, lit.Length).SequenceEqual(lit.AsSpan());
    }

    // Does Subject[Cursor] exist and appear in charset?
    public bool CharInSet(int pos, string charset)
    {
        if (pos >= Subject.Length) return false;
        return charset.IndexOf(Subject[pos]) >= 0;
    }
}

// ── IByrdBox — every box implements this ───────────────────────────────────

public interface IByrdBox
{
    Spec α(MatchState ms);   // α port — fresh entry
    Spec β(MatchState ms);    // β port — backtrack
}
