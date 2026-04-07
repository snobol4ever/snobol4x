// bb_boxes.il — All Byrd box MSIL implementations, consolidated
// Single-assembly compilation: shared foundation + all boxes + executor
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

.assembly extern mscorlib {}
.assembly boxes {}
.module boxes.dll

// ───── shared/bb_box.il ─────
// bb_box.il — Dynamic Byrd Box foundation for .NET MSIL
//
// Defines:
//   Spec        — valuetype (mirrors spec_t: start/length, Fail sentinel)
//   MatchState  — class (mirrors C globals Σ/Δ/Ω: subject string + cursor)
//   IByrdBox    — interface (α/β entry ports)
//
// Assemble as part of boxes.dll:
//   ilasm /dll /output:boxes.dll bb_box.il bb_executor.il bb_lit.il ...
//
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
// SPRINT:  D-169


// ── Spec valuetype ──────────────────────────────────────────────────────────
// mirrors spec_t { int start; int length; }
// Spec.Fail has Start == -1

.class public sequential ansi sealed beforefieldinit
       Snobol4.Runtime.Boxes.Spec
       extends [mscorlib]System.ValueType
{
  .field public int32 Start
  .field public int32 Length

  // static readonly Spec Fail = new Spec(-1, 0)
  .field public static initonly valuetype Snobol4.Runtime.Boxes.Spec Fail

  .method static void .cctor() cil managed
  {
    .maxstack 3
    ldsflda    valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ldc.i4.m1
    stfld      int32 Snobol4.Runtime.Boxes.Spec::Start
    ldsflda    valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    ret
  }

  // bool get_IsFail() => Start < 0
  .method public instance bool get_IsFail() cil managed
  {
    .maxstack 2
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Start
    ldc.i4.0
    clt
    ret
  }

  .property instance bool IsFail()
  {
    .get instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
  }

  // static Spec Of(int start, int length)
  .method public static valuetype Snobol4.Runtime.Boxes.Spec Of(int32 start, int32 length) cil managed
  {
    .maxstack 2
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_0)
    ldloca.s   V_0
    ldarg.0
    stfld      int32 Snobol4.Runtime.Boxes.Spec::Start
    ldloca.s   V_0
    ldarg.1
    stfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    ldloc.0
    ret
  }

  // static Spec ZeroWidth(int pos)
  .method public static valuetype Snobol4.Runtime.Boxes.Spec ZeroWidth(int32 pos) cil managed
  {
    .maxstack 2
    ldarg.0
    ldc.i4.0
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret
  }

  // Spec Cat(Spec right) => new Spec(Start, Length + right.Length)
  .method public instance valuetype Snobol4.Runtime.Boxes.Spec Cat(valuetype Snobol4.Runtime.Boxes.Spec right) cil managed
  {
    .maxstack 4
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Start
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    ldarga.s   right
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret
  }
}

// ── MatchState class ────────────────────────────────────────────────────────
// mirrors C globals: const char *Σ (subject), int Δ (cursor), int Ω (length)

.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.MatchState
       extends [mscorlib]System.Object
{
  .field private string _subject
  .field public  int32  Cursor

  .method public specialname rtspecialname instance void .ctor(string subject, int32 startCursor) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     MS_NOTNULL
    pop
    ldstr      ""
  MS_NOTNULL:
    stfld      string Snobol4.Runtime.Boxes.MatchState::_subject
    ldarg.0
    ldarg.2
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ret
  }

  .method public specialname rtspecialname instance void .ctor(string subject) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    ldc.i4.0
    call       instance void Snobol4.Runtime.Boxes.MatchState::.ctor(string, int32)
    ret
  }

  // string get_Subject()
  .method public instance string get_Subject() cil managed
  {
    .maxstack 1
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.MatchState::_subject
    ret
  }

  .property instance string Subject()
  {
    .get instance string Snobol4.Runtime.Boxes.MatchState::get_Subject()
  }

  // int get_Length()
  .method public instance int32 get_Length() cil managed
  {
    .maxstack 1
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.MatchState::_subject
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    ret
  }

  .property instance int32 Length()
  {
    .get instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
  }

  // bool MatchesAt(int pos, string lit)
  .method public instance bool MatchesAt(int32 pos, string lit) cil managed
  {
    .maxstack 4
    .locals init (int32 V_litlen)
    // if pos + lit.Length > Subject.Length → false
    ldarg.2
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    stloc.0
    ldarg.1
    ldloc.0
    add
    ldarg.0
    call       instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bgt        MA_FALSE
    // Subject.AsSpan(pos, litlen).SequenceEqual(lit.AsSpan())
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.MatchState::_subject
    ldarg.1
    ldloc.0
    call       valuetype [mscorlib]System.ReadOnlySpan`1<char> [mscorlib]System.MemoryExtensions::AsSpan(string, int32, int32)
    ldarg.2
    call       valuetype [mscorlib]System.ReadOnlySpan`1<char> [mscorlib]System.MemoryExtensions::AsSpan(string)
    call       bool [mscorlib]System.MemoryExtensions::SequenceEqual<char>(valuetype [mscorlib]System.ReadOnlySpan`1<!!0>, valuetype [mscorlib]System.ReadOnlySpan`1<!!0>)
    ret
  MA_FALSE:
    ldc.i4.0
    ret
  }

  // bool CharInSet(int pos, string charset)
  .method public instance bool CharInSet(int32 pos, string charset) cil managed
  {
    .maxstack 3
    // if pos >= Length → false
    ldarg.1
    ldarg.0
    call       instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bge        CS_FALSE
    // charset.IndexOf(Subject[pos]) >= 0
    ldarg.2
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.MatchState::_subject
    ldarg.1
    callvirt   instance char [mscorlib]System.String::get_Chars(int32)
    callvirt   instance int32 [mscorlib]System.String::IndexOf(char)
    ldc.i4.0
    blt        CS_FALSE
    ldc.i4.1
    ret
  CS_FALSE:
    ldc.i4.0
    ret
  }
}

// ── IByrdBox interface ──────────────────────────────────────────────────────

.class public interface abstract auto ansi
       Snobol4.Runtime.Boxes.IByrdBox
{
  .method public virtual abstract instance
          valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed {}

  .method public virtual abstract instance
          valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed {}
}

// ───── lit ─────
// bb_lit.il — LIT: literal string match
// α: cursor+len>Ω→ω; subject[cursor..+len]!=lit→ω; cursor+=len; γ
// β: cursor-=len; ω
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_lit
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _lit
  .field private int32  _len

  .method public specialname rtspecialname instance void .ctor(string lit) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     LIT_NOTNULL
    pop
    ldstr      ""
  LIT_NOTNULL:
    stfld      string Snobol4.Runtime.Boxes.bb_lit::_lit
    ldarg.0
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_lit::_lit
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    stfld      int32 Snobol4.Runtime.Boxes.bb_lit::_len
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_result)
    // if cursor + len > Length → fail
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_lit::_len
    add
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bgt        LIT_A_FAIL
    // if !MatchesAt(cursor, lit) → fail
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_lit::_lit
    callvirt   instance bool Snobol4.Runtime.Boxes.MatchState::MatchesAt(int32, string)
    brfalse    LIT_A_FAIL
    // result = Spec.Of(cursor, len)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_lit::_len
    call       valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    // cursor += len
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_lit::_len
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  LIT_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    // cursor -= len
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_lit::_len
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── seq ─────
// bb_seq.il — SEQ: left then right; β retries right then left
// State: _mStart (fixed at α entry), _mLen (left-match length, RESET on each new left result)
// Fix: mLen is stored as the left span only; gamma emits mStart+(mLen+rr.Length)
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_seq
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox _left
  .field private class Snobol4.Runtime.Boxes.IByrdBox _right
  .field private int32 _mStart
  .field private int32 _mLen

  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox left,
                class Snobol4.Runtime.Boxes.IByrdBox right) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.0
    ldarg.2
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_lr,
                  valuetype Snobol4.Runtime.Boxes.Spec V_rr)

    ldarg.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mStart

    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_lr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_A_OMEGA

    ldarg.0
    ldloca.s   V_lr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen

  SEQ_A_TRY_RIGHT:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    ldloca.s   V_rr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brfalse    SEQ_A_GAMMA

  SEQ_A_LEFT_BETA:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Beta(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_lr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_A_OMEGA
    ldarg.0
    ldloca.s   V_lr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    br         SEQ_A_TRY_RIGHT

  SEQ_A_GAMMA:
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mStart
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    ldloca.s   V_rr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
    call       valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret

  SEQ_A_OMEGA:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_rr,
                  valuetype Snobol4.Runtime.Boxes.Spec V_lr)

  SEQ_B_TRY_RIGHT_BETA:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Beta(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_rr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brfalse    SEQ_B_GAMMA

  SEQ_B_LEFT_BETA:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Beta(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    ldloca.s   V_lr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_B_OMEGA
    ldarg.0
    ldloca.s   V_lr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_rr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_B_LEFT_BETA

  SEQ_B_GAMMA:
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mStart
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    ldloca.s   V_rr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
    call       valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret

  SEQ_B_OMEGA:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── alt ─────
// bb_alt.il — ALT: alternation
// α: try children[0..n-1] in order; β retries same child
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_alt
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox[] _children
  .field private int32 _current
  .field private int32 _savedPos

  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox[] children) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld      class Snobol4.Runtime.Boxes.IByrdBox[] Snobol4.Runtime.Boxes.bb_alt::_children
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (int32 V_i, valuetype Snobol4.Runtime.Boxes.Spec V_cr)

    // _savedPos = ms.Cursor
    ldarg.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stfld      int32 Snobol4.Runtime.Boxes.bb_alt::_savedPos

    ldc.i4.0
    stloc.0   // i = 0

  ALT_A_LOOP:
    ldloc.0
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox[] Snobol4.Runtime.Boxes.bb_alt::_children
    ldlen
    conv.i4
    bge        ALT_A_FAIL

    // ms.Cursor = _savedPos
    ldarg.1
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_alt::_savedPos
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor

    // cr = children[i].α(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox[] Snobol4.Runtime.Boxes.bb_alt::_children
    ldloc.0
    ldelem.ref
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1

    ldloca.s   V_cr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     ALT_A_NEXT

    // success: _current = i; return cr
    ldarg.0
    ldloc.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_alt::_current
    ldloc.1
    ret

  ALT_A_NEXT:
    ldloc.0
    ldc.i4.1
    add
    stloc.0
    br         ALT_A_LOOP

  ALT_A_FAIL:
    ldarg.1
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_alt::_savedPos
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_cr)
    // cr = children[_current].β(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox[] Snobol4.Runtime.Boxes.bb_alt::_children
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_alt::_current
    ldelem.ref
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Beta(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_cr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     ALT_B_FAIL
    ldloc.0
    ret
  ALT_B_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── arb ─────
// bb_arb.il — ARB: match 0..N chars lazily; β tries one more
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_arb
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _count
  .field private int32 _start

  .method public specialname rtspecialname instance void .ctor() cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 2
    ldarg.0
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_arb::_count
    ldarg.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stfld      int32 Snobol4.Runtime.Boxes.bb_arb::_start
    // zero-width success first (lazy)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    // _count++
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_count
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_arb::_count
    // if _start + _count > Length → fail
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_start
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_count
    add
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bgt        ARB_B_FAIL
    // cursor = start + count
    ldarg.1
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_start
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_count
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_start
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arb::_count
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret
  ARB_B_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── arbno ─────
// bb_arbno.il — ARBNO: zero-or-more greedy; β unwinds stack
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_arbno
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox _body
  // stacks as int arrays: [0]=start, [1]=len interleaved; depth index
  .field private int32[] _matchStart
  .field private int32[] _matchLen
  .field private int32[] _startStack
  .field private int32   _depth

  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox body) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_arbno::_body
    ldarg.0
    ldc.i4     64
    newarr     [mscorlib]System.Int32
    stfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchStart
    ldarg.0
    ldc.i4     64
    newarr     [mscorlib]System.Int32
    stfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchLen
    ldarg.0
    ldc.i4     64
    newarr     [mscorlib]System.Int32
    stfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_startStack
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (int32 V_startHere, valuetype Snobol4.Runtime.Boxes.Spec V_br)

    // _depth = 0; push frame(ZeroWidth, cursor)
    ldarg.0
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchStart
    ldc.i4.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stelem.i4
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchLen
    ldc.i4.0
    ldc.i4.0
    stelem.i4
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_startStack
    ldc.i4.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stelem.i4

  ARBNO_A_LOOP:
    // startHere = ms.Cursor
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stloc.0
    // br = _body.α(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_arbno::_body
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    // if br.IsFail → stop, succeed with top frame
    ldloca.s   V_br
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     ARBNO_A_STOP
    // zero-advance guard: if cursor == startHere → stop
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    beq        ARBNO_A_STOP
    // advance: depth++ push new frame (if < 64)
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldc.i4     63
    bge        ARBNO_A_STOP
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    // _matchStart[depth] = _matchStart[depth-1] (cumulative start = always frame0 start)
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchStart
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchStart
    ldc.i4.0
    ldelem.i4
    stelem.i4
    // _matchLen[depth] = _matchLen[depth-1] + br.Length
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchLen
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchLen
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldc.i4.1
    sub
    ldelem.i4
    ldloca.s   V_br
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
    stelem.i4
    // _startStack[depth] = cursor
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_startStack
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stelem.i4
    br         ARBNO_A_LOOP

  ARBNO_A_STOP:
    // return Spec.Of(_matchStart[_depth], _matchLen[_depth])
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchStart
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldelem.i4
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchLen
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldelem.i4
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    // if _depth <= 0 → fail
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldc.i4.0
    ble        ARBNO_B_FAIL
    // _depth--
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldc.i4.1
    sub
    stfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    // ms.Cursor = _startStack[_depth]
    ldarg.1
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_startStack
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldelem.i4
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchStart
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldelem.i4
    ldarg.0
    ldfld      int32[] Snobol4.Runtime.Boxes.bb_arbno::_matchLen
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_arbno::_depth
    ldelem.i4
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret
  ARBNO_B_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── any ─────
// bb_any.il — ANY: match one char if in charset; NOTANY: if NOT in charset
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_any
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _chars

  .method public specialname rtspecialname instance void .ctor(string chars) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     ANY_NN
    pop
    ldstr      ""
  ANY_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_any::_chars
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_any::_chars
    callvirt   instance bool Snobol4.Runtime.Boxes.MatchState::CharInSet(int32, string)
    brfalse    ANY_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldc.i4.1
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  ANY_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldc.i4.1
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── notany ─────
// bb_notany.il — NOTANY: match one char if NOT in charset
// α: CharInSet→ω; cursor++; γ(1 char)
// β: cursor--; ω
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6


.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.bb_notany
       extends [mscorlib]System.Object
       implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _chars

  .method public specialname rtspecialname instance void .ctor(string chars) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     NOTANY_NN
    pop
    ldstr      ""
  NOTANY_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_notany::_chars
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    // if CharInSet(cursor, chars) → fail  (brtrue = fail if in set)
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_notany::_chars
    callvirt   instance bool Snobol4.Runtime.Boxes.MatchState::CharInSet(int32, string)
    brtrue     NOTANY_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldc.i4.1
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  NOTANY_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldc.i4.1
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── span ─────
// bb_span.il — SPAN: longest prefix of chars in set (≥1 required)
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_span
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _chars
  .field private int32  _count
  .method public specialname rtspecialname instance void .ctor(string chars) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     SP_NN
    pop
    ldstr      ""
  SP_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_span::_chars
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.0
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
  SP_A_LOOP:
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    add
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_span::_chars
    callvirt   instance bool Snobol4.Runtime.Boxes.MatchState::CharInSet(int32, string)
    brfalse    SP_A_DONE
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    br         SP_A_LOOP
  SP_A_DONE:
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    ldc.i4.0
    ble        SP_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  SP_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_span::_count
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── brk ─────
// bb_brk.il — BREAK: scan to first char in set
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_brk
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _chars
  .field private int32  _count
  .method public specialname rtspecialname instance void .ctor(string chars) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     BRK_NN
    pop
    ldstr      ""
  BRK_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_brk::_chars
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.0
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
  BRK_A_LOOP:
    // while cursor+count < Length && !CharInSet(cursor+count)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    add
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bge        BRK_A_EOS
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    add
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_brk::_chars
    callvirt   instance bool Snobol4.Runtime.Boxes.MatchState::CharInSet(int32, string)
    brtrue     BRK_A_FOUND
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    br         BRK_A_LOOP
  BRK_A_EOS:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  BRK_A_FOUND:
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_brk::_count
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── breakx ─────
// bb_breakx.il — BREAKX: like BREAK but zero advance OR EOS → fail
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_breakx
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _chars
  .field private int32  _count
  .method public specialname rtspecialname instance void .ctor(string chars) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     BRKX_NN
    pop
    ldstr      ""
  BRKX_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_breakx::_chars
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.0
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
  BRKX_A_LOOP:
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    add
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bge        BRKX_A_FAIL
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    add
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_breakx::_chars
    callvirt   instance bool Snobol4.Runtime.Boxes.MatchState::CharInSet(int32, string)
    brtrue     BRKX_A_FOUND
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    br         BRKX_A_LOOP
  BRKX_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  BRKX_A_FOUND:
    // zero advance → fail
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    ldc.i4.0
    ble        BRKX_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_breakx::_count
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── len ─────
// bb_len.il — LEN: match exactly n characters
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_len
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _n
  .method public specialname rtspecialname instance void .ctor(int32 n) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld      int32 Snobol4.Runtime.Boxes.bb_len::_n
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_len::_n
    add
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    bgt        LEN_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_len::_n
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_len::_n
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  LEN_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_len::_n
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── pos ─────
// bb_pos.il — POS: assert cursor==n (zero-width); RPOS: cursor==Length-n
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_pos
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _n
  .method public specialname rtspecialname instance void .ctor(int32 n) cil managed
  {
    .maxstack 2
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld int32 Snobol4.Runtime.Boxes.bb_pos::_n
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 2
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_pos::_n
    bne.un     POS_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  POS_A_FAIL:
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_rpos
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _n
  .method public specialname rtspecialname instance void .ctor(int32 n) cil managed
  {
    .maxstack 2
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld int32 Snobol4.Runtime.Boxes.bb_rpos::_n
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_rpos::_n
    sub
    bne.un     RPOS_A_FAIL
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  RPOS_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── tab ─────
// bb_tab.il — TAB: advance TO absolute pos n; RTAB: advance TO Length-n
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_tab
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _n
  .field private int32 _advance
  .method public specialname rtspecialname instance void .ctor(int32 n) cil managed
  {
    .maxstack 2
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld int32 Snobol4.Runtime.Boxes.bb_tab::_n
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_tab::_n
    bgt        TAB_A_FAIL
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_tab::_n
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    sub
    stfld      int32 Snobol4.Runtime.Boxes.bb_tab::_advance
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_tab::_advance
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_tab::_n
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  TAB_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_tab::_advance
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_rtab
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _n
  .field private int32 _advance
  .method public specialname rtspecialname instance void .ctor(int32 n) cil managed
  {
    .maxstack 2
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld int32 Snobol4.Runtime.Boxes.bb_rtab::_n
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (int32 V_target, valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_rtab::_n
    sub
    stloc.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    bgt        RTAB_A_FAIL
    ldarg.0
    ldloc.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    sub
    stfld      int32 Snobol4.Runtime.Boxes.bb_rtab::_advance
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_rtab::_advance
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.1
    ldarg.1
    ldloc.0
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.1
    ret
  RTAB_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_rtab::_advance
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── rem ─────
// bb_rem.il — REM: match entire remainder; no backtrack
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_rem
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .method public specialname rtspecialname instance void .ctor() cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r, int32 V_len)
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    sub
    stloc.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.1
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    stloc.0
    ldarg.1
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc.0
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── eps ─────
// bb_eps.il — EPS: zero-width success once; done flag prevents double-γ
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_eps
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private bool _done
  .method public specialname rtspecialname instance void .ctor() cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 2
    ldarg.0
    ldfld      bool Snobol4.Runtime.Boxes.bb_eps::_done
    brtrue     EPS_A_FAIL
    ldarg.0
    ldc.i4.1
    stfld      bool Snobol4.Runtime.Boxes.bb_eps::_done
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  EPS_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── bal ─────
// bb_bal.il — BAL: match balanced-parentheses expression
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_bal
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private int32 _matchedLen
  .method public specialname rtspecialname instance void .ctor() cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (int32 V_pos, int32 V_depth, int32 V_len, char V_c, int32 V_start)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stloc.0   // pos
    ldc.i4.0
    stloc.1   // depth
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
    stloc.2   // len
    ldloc.0
    stloc      4  // start = pos
    ldloc.0
    ldloc.2
    bge        BAL_A_FAIL
  BAL_A_LOOP:
    ldarg.1
    callvirt   instance string Snobol4.Runtime.Boxes.MatchState::get_Subject()
    ldloc.0
    callvirt   instance char [mscorlib]System.String::get_Chars(int32)
    stloc.3   // c = subject[pos]
    ldloc.3
    ldc.i4     40   // '('
    bne.un     BAL_CHECK_CLOSE
    ldloc.1
    ldc.i4.1
    add
    stloc.1   // depth++
    br         BAL_NEXT
  BAL_CHECK_CLOSE:
    ldloc.3
    ldc.i4     41   // ')'
    bne.un     BAL_NEXT
    ldloc.1
    ldc.i4.0
    beq        BAL_A_FAIL   // unmatched close
    ldloc.1
    ldc.i4.1
    sub
    stloc.1   // depth--
  BAL_NEXT:
    ldloc.0
    ldc.i4.1
    add
    stloc.0   // pos++
    // while pos < len && depth > 0
    ldloc.0
    ldloc.2
    bge        BAL_DONE
    ldloc.1
    ldc.i4.0
    bgt        BAL_A_LOOP
  BAL_DONE:
    ldloc.1
    ldc.i4.0
    bne.un     BAL_A_FAIL   // unclosed open
    ldarg.0
    ldloc.0
    ldloc      4
    sub
    stfld      int32 Snobol4.Runtime.Boxes.bb_bal::_matchedLen
    ldarg.1
    ldloc.0
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc      4
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_bal::_matchedLen
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Of(int32, int32)
    ret
  BAL_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    ldarg.1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_bal::_matchedLen
    sub
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
}

// ───── abort ─────
// bb_terminals.il — FENCE, ABORT, FAIL, SUCCEED terminal boxes
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

// bb_fence: succeed once; β cuts
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_fence
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .method public specialname rtspecialname instance void .ctor() cil managed
  { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Object::.ctor() ret }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
}

// bb_abort: always ω
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_abort
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .method public specialname rtspecialname instance void .ctor() cil managed
  { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Object::.ctor() ret }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
}

// bb_fail: always ω — force backtrack
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_fail
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .method public specialname rtspecialname instance void .ctor() cil managed
  { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Object::.ctor() ret }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
}

// bb_succeed: always γ zero-width; outer scan loop retries on backtrack
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_succeed
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .method public specialname rtspecialname instance void .ctor() cil managed
  { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Object::.ctor() ret }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 1
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  }
}

// ───── not ─────
// bb_not.il — NOT: \X — succeed iff child fails; β always ω
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_not
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox _child
  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox child) cil managed
  {
    .maxstack 2
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_not::_child
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (int32 V_saved, valuetype Snobol4.Runtime.Boxes.Spec V_cr)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stloc.0
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_not::_child
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    // always restore cursor
    ldarg.1
    ldloc.0
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloca.s   V_cr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brfalse    NOT_A_FAIL   // child_γ → NOT_ω
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  NOT_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
}

// ───── interr ─────
// bb_interr.il — INTERR: ?X — zero-width if child succeeds, ω if child fails
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_interr
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox _child
  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox child) cil managed
  {
    .maxstack 2
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_interr::_child
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (int32 V_saved, valuetype Snobol4.Runtime.Boxes.Spec V_cr)
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stloc.0
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_interr::_child
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    // always restore cursor
    ldarg.1
    ldloc.0
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloca.s   V_cr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     INT_A_FAIL   // child_ω → INT_ω
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  INT_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
}

// ───── capture ─────
// bb_capture.il — CAPTURE: $ writes immediately; . buffers for Phase-5 commit
// Uses a static delegate field pattern compatible with ilasm.
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_capture
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox _child
  .field private string   _varname
  .field private bool     _immediate
  // SetVar delegate: Action<string,string>
  .field public  class [mscorlib]System.Action`2<string,string> SetVar
  // Pending capture
  .field public  string   Pending
  .field public  bool     HasPending

  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox child,
                string varname,
                bool immediate) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_capture::_child
    ldarg.0
    ldarg.2
    dup
    brtrue     CAP_NN
    pop
    ldstr      ""
  CAP_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_capture::_varname
    ldarg.0
    ldarg.3
    stfld      bool Snobol4.Runtime.Boxes.bb_capture::_immediate
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_cr)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_capture::_child
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::α(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_cr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     CAP_A_FAIL
    ldarg.0
    ldarg.1
    ldloca.s   V_cr
    call       instance void Snobol4.Runtime.Boxes.bb_capture::OnGamma(
                 class Snobol4.Runtime.Boxes.MatchState,
                 valuetype Snobol4.Runtime.Boxes.Spec&)
    ldloc.0
    ret
  CAP_A_FAIL:
    ldarg.0
    ldc.i4.0
    stfld      bool Snobol4.Runtime.Boxes.bb_capture::HasPending
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 3
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_cr)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_capture::_child
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::β(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_cr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     CAP_B_FAIL
    ldarg.0
    ldarg.1
    ldloca.s   V_cr
    call       instance void Snobol4.Runtime.Boxes.bb_capture::OnGamma(
                 class Snobol4.Runtime.Boxes.MatchState,
                 valuetype Snobol4.Runtime.Boxes.Spec&)
    ldloc.0
    ret
  CAP_B_FAIL:
    ldarg.0
    ldc.i4.0
    stfld      bool Snobol4.Runtime.Boxes.bb_capture::HasPending
    ldsfld     valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  // OnGamma — extract matched substring, write or buffer
  .method private instance void OnGamma(
            class Snobol4.Runtime.Boxes.MatchState ms,
            valuetype Snobol4.Runtime.Boxes.Spec& cr) cil managed
  {
    .maxstack 4
    .locals init (string V_matched)
    // matched = ms.Subject.Substring(cr.Start, cr.Length)
    ldarg.1
    callvirt   instance string Snobol4.Runtime.Boxes.MatchState::get_Subject()
    ldarg.2
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Start
    ldarg.2
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)
    stloc.0
    // if _immediate && SetVar != null && varname != ""
    ldarg.0
    ldfld      bool Snobol4.Runtime.Boxes.bb_capture::_immediate
    brfalse    OG_DEFERRED
    ldarg.0
    ldfld      class [mscorlib]System.Action`2<string,string> Snobol4.Runtime.Boxes.bb_capture::SetVar
    brfalse    OG_DONE
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_capture::_varname
    ldstr      ""
    callvirt   instance bool [mscorlib]System.String::Equals(string)
    brtrue     OG_DONE
    ldarg.0
    ldfld      class [mscorlib]System.Action`2<string,string> Snobol4.Runtime.Boxes.bb_capture::SetVar
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_capture::_varname
    ldloc.0
    callvirt   instance void class [mscorlib]System.Action`2<string,string>::Invoke(!0, !1)
    br         OG_DONE
  OG_DEFERRED:
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_capture::_varname
    ldstr      ""
    callvirt   instance bool [mscorlib]System.String::Equals(string)
    brtrue     OG_DONE
    ldarg.0
    ldloc.0
    stfld      string Snobol4.Runtime.Boxes.bb_capture::Pending
    ldarg.0
    ldc.i4.1
    stfld      bool Snobol4.Runtime.Boxes.bb_capture::HasPending
  OG_DONE:
    ret
  }

  // CommitPending — called by Phase 5 on :S
  .method public instance void CommitPending() cil managed
  {
    .maxstack 3
    ldarg.0
    ldfld      bool Snobol4.Runtime.Boxes.bb_capture::HasPending
    brfalse    CP_DONE
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_capture::_varname
    ldstr      ""
    callvirt   instance bool [mscorlib]System.String::Equals(string)
    brtrue     CP_DONE
    ldarg.0
    ldfld      class [mscorlib]System.Action`2<string,string> Snobol4.Runtime.Boxes.bb_capture::SetVar
    brfalse    CP_DONE
    ldarg.0
    ldfld      class [mscorlib]System.Action`2<string,string> Snobol4.Runtime.Boxes.bb_capture::SetVar
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_capture::_varname
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_capture::Pending
    dup
    brtrue     CP_NOTNULL
    pop
    ldstr      ""
  CP_NOTNULL:
    callvirt   instance void class [mscorlib]System.Action`2<string,string>::Invoke(!0, !1)
  CP_DONE:
    ldarg.0
    ldc.i4.0
    stfld      bool Snobol4.Runtime.Boxes.bb_capture::HasPending
    ret
  }
}

// ───── atp ─────
// bb_atp.il — ATP: @var — write cursor position as integer string into varname
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_atp
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _varname
  .field public  class [mscorlib]System.Action`2<string,string> SetVar

  .method public specialname rtspecialname instance void .ctor(string varname) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     ATP_NN
    pop
    ldstr      ""
  ATP_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_atp::_varname
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Alpha(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    // if SetVar != null && varname != "" → SetVar(varname, cursor.ToString())
    ldarg.0
    ldfld      class [mscorlib]System.Action`2<string,string> Snobol4.Runtime.Boxes.bb_atp::SetVar
    brfalse    ATP_A_SKIP
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_atp::_varname
    ldstr      ""
    callvirt   instance bool [mscorlib]System.String::Equals(string)
    brtrue     ATP_A_SKIP
    ldarg.0
    ldfld      class [mscorlib]System.Action`2<string,string> Snobol4.Runtime.Boxes.bb_atp::SetVar
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_atp::_varname
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    box        [mscorlib]System.Int32
    callvirt   instance string [mscorlib]System.Object::ToString()
    callvirt   instance void class [mscorlib]System.Action`2<string,string>::Invoke(!0, !1)
  ATP_A_SKIP:
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::ZeroWidth(int32)
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          Beta(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  { .maxstack 1 ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail ret }
}

// ───── dvar ─────
// bb_dvar.il — DVAR: *VAR — re-resolve variable's live value on every α
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
.class public auto ansi beforefieldinit Snobol4.Runtime.Boxes.bb_dvar
       extends [mscorlib]System.Object implements Snobol4.Runtime.Boxes.IByrdBox
{
  .field private string _varname
  .field public  class [mscorlib]System.Func`2<string,string>  GetStringVar
  .field public  class [mscorlib]System.Func`2<string,class Snobol4.Runtime.Boxes.IByrdBox> GetPatternVar
  .field private class Snobol4.Runtime.Boxes.IByrdBox _child
  .field private string _lastStringValue

  .method public specialname rtspecialname instance void .ctor(string varname) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    dup
    brtrue     DV_NN
    pop
    ldstr      ""
  DV_NN:
    stfld      string Snobol4.Runtime.Boxes.bb_dvar::_varname
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 2
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_r)
    ldarg.0
    call       instance void Snobol4.Runtime.Boxes.bb_dvar::RebuildChild()
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
    brfalse    DV_A_FAIL
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::α(class Snobol4.Runtime.Boxes.MatchState)
    ret
  DV_A_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 2
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
    brfalse    DV_B_FAIL
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::β(class Snobol4.Runtime.Boxes.MatchState)
    ret
  DV_B_FAIL:
    ldsfld valuetype Snobol4.Runtime.Boxes.Spec Snobol4.Runtime.Boxes.Spec::Fail
    ret
  }

  .method private instance void RebuildChild() cil managed
  {
    .maxstack 3
    .locals init (class Snobol4.Runtime.Boxes.IByrdBox V_pat, string V_s)

    // Try pattern first
    ldarg.0
    ldfld      class [mscorlib]System.Func`2<string,class Snobol4.Runtime.Boxes.IByrdBox>
               Snobol4.Runtime.Boxes.bb_dvar::GetPatternVar
    brfalse    DV_RC_STRING

    ldarg.0
    ldfld      class [mscorlib]System.Func`2<string,class Snobol4.Runtime.Boxes.IByrdBox>
               Snobol4.Runtime.Boxes.bb_dvar::GetPatternVar
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_dvar::_varname
    callvirt   instance !1 class [mscorlib]System.Func`2<string,class Snobol4.Runtime.Boxes.IByrdBox>::Invoke(!0)
    stloc.0

    ldloc.0
    brfalse    DV_RC_STRING
    // pat != null — use it if changed
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
    ldloc.0
    beq        DV_RC_DONE   // same reference → no rebuild
    ldarg.0
    ldloc.0
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
    ldarg.0
    ldnull
    stfld      string Snobol4.Runtime.Boxes.bb_dvar::_lastStringValue
    br         DV_RC_DONE

  DV_RC_STRING:
    // fall back to string value → wrap as bb_lit
    ldarg.0
    ldfld      class [mscorlib]System.Func`2<string,string>
               Snobol4.Runtime.Boxes.bb_dvar::GetStringVar
    brfalse    DV_RC_EMPTY
    ldarg.0
    ldfld      class [mscorlib]System.Func`2<string,string>
               Snobol4.Runtime.Boxes.bb_dvar::GetStringVar
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_dvar::_varname
    callvirt   instance !1 class [mscorlib]System.Func`2<string,string>::Invoke(!0)
    stloc.1
    br         DV_RC_HAVELITVAL
  DV_RC_EMPTY:
    ldstr      ""
    stloc.1
  DV_RC_HAVELITVAL:
    // if _lastStringValue == s → unchanged
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_dvar::_lastStringValue
    brfalse    DV_RC_REBUILD
    ldarg.0
    ldfld      string Snobol4.Runtime.Boxes.bb_dvar::_lastStringValue
    ldloc.1
    callvirt   instance bool [mscorlib]System.String::Equals(string)
    brtrue     DV_RC_DONE
  DV_RC_REBUILD:
    ldarg.0
    ldloc.1
    stfld      string Snobol4.Runtime.Boxes.bb_dvar::_lastStringValue
    ldarg.0
    ldloc.1
    newobj     instance void Snobol4.Runtime.Boxes.bb_lit::.ctor(string)
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_dvar::_child
  DV_RC_DONE:
    ret
  }
}

// ───── shared/bb_executor.il ─────
// bb_executor.il — Phase 3 trampoline: drives the Byrd box graph
//
// MatchResult valuetype + ByrdBoxExecutor class.
// Mirrors bb_executor.cs / Scanner.Match() in snobol4dotnet.
//
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
// SPRINT:  D-169


// ── MatchResult valuetype ───────────────────────────────────────────────────

.class public sequential ansi sealed beforefieldinit
       Snobol4.Runtime.Boxes.MatchResult
       extends [mscorlib]System.ValueType
{
  .field public bool   Success
  .field public int32  MatchStart
  .field public int32  MatchLength
  .field public int32  CursorAfter

  // static MatchResult Succeeded(int start, int len, int after)
  .method public static valuetype Snobol4.Runtime.Boxes.MatchResult
          Succeeded(int32 start, int32 len, int32 after) cil managed
  {
    .maxstack 2
    .locals init (valuetype Snobol4.Runtime.Boxes.MatchResult V_0)
    ldloca.s   V_0
    ldc.i4.1
    stfld      bool Snobol4.Runtime.Boxes.MatchResult::Success
    ldloca.s   V_0
    ldarg.0
    stfld      int32 Snobol4.Runtime.Boxes.MatchResult::MatchStart
    ldloca.s   V_0
    ldarg.1
    stfld      int32 Snobol4.Runtime.Boxes.MatchResult::MatchLength
    ldloca.s   V_0
    ldarg.2
    stfld      int32 Snobol4.Runtime.Boxes.MatchResult::CursorAfter
    ldloc.0
    ret
  }

  // static readonly MatchResult Failed
  .field public static initonly valuetype Snobol4.Runtime.Boxes.MatchResult Failed

  .method static void .cctor() cil managed
  {
    .maxstack 1
    // Failed is all-zero (Success=false, all ints=0) — default init suffices
    ret
  }
}

// ── ByrdBoxExecutor class ───────────────────────────────────────────────────

.class public auto ansi beforefieldinit
       Snobol4.Runtime.Boxes.ByrdBoxExecutor
       extends [mscorlib]System.Object
{
  .field private class Snobol4.Runtime.Boxes.IByrdBox _root
  // _captures: List<bb_capture> — stored as object, cast on use
  .field private class [mscorlib]System.Collections.Generic.List`1<object> _captures

  .method public specialname rtspecialname instance void
          .ctor(class Snobol4.Runtime.Boxes.IByrdBox root,
                class [mscorlib]System.Collections.Generic.IReadOnlyList`1<object> captures) cil managed
  {
    .maxstack 2
    ldarg.0
    call       instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.ByrdBoxExecutor::_root
    ldarg.0
    newobj     instance void class [mscorlib]System.Collections.Generic.List`1<object>::.ctor()
    stfld      class [mscorlib]System.Collections.Generic.List`1<object> Snobol4.Runtime.Boxes.ByrdBoxExecutor::_captures
    ret
  }

  // ── Run — Phase 3 scan loop ─────────────────────────────────────────────
  // anchor=true: try only at startPos
  // anchor=false: try each position startPos..Length

  .method public instance valuetype Snobol4.Runtime.Boxes.MatchResult
          Run(class Snobol4.Runtime.Boxes.MatchState ms, bool anchor) cil managed
  {
    .maxstack 4
    .locals init (
      int32  V_startPos,    // 0
      int32  V_limit,       // 1
      int32  V_pos,         // 2
      valuetype Snobol4.Runtime.Boxes.Spec V_r,  // 3
      int32  V_matchStart   // 4
    )

    // startPos = ms.Cursor
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stloc.0

    // limit = anchor ? startPos : ms.Length
    ldarg.2
    brfalse    RUN_NOTANCHOR
    ldloc.0
    br         RUN_HAVELIMIT
  RUN_NOTANCHOR:
    ldarg.1
    callvirt   instance int32 Snobol4.Runtime.Boxes.MatchState::get_Length()
  RUN_HAVELIMIT:
    stloc.1

    // for pos = startPos; pos <= limit; pos++
    ldloc.0
    stloc.2

  RUN_LOOP:
    ldloc.2
    ldloc.1
    bgt        RUN_FAIL

    // ms.Cursor = pos
    ldarg.1
    ldloc.2
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor

    // matchStart = pos
    ldloc.2
    stloc      4

    // r = _root.α(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.ByrdBoxExecutor::_root
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Alpha(class Snobol4.Runtime.Boxes.MatchState)
    stloc.3

    // if !r.IsFail → success
    ldloca.s   V_r
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     RUN_TRY_BETA

    // CommitCaptures(); return Succeeded(matchStart, cursor-matchStart, cursor)
    ldarg.0
    call       instance void Snobol4.Runtime.Boxes.ByrdBoxExecutor::CommitCaptures()
    ldloc      4
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc      4
    sub
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.MatchResult
               Snobol4.Runtime.Boxes.MatchResult::Succeeded(int32, int32, int32)
    ret

  RUN_TRY_BETA:
    // r = _root.β(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.ByrdBoxExecutor::_root
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::Beta(class Snobol4.Runtime.Boxes.MatchState)
    stloc.3

    ldloca.s   V_r
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     RUN_NEXT_POS

    ldarg.0
    call       instance void Snobol4.Runtime.Boxes.ByrdBoxExecutor::CommitCaptures()
    ldloc      4
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldloc      4
    sub
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    call       valuetype Snobol4.Runtime.Boxes.MatchResult
               Snobol4.Runtime.Boxes.MatchResult::Succeeded(int32, int32, int32)
    ret

  RUN_NEXT_POS:
    // ms.Cursor = pos + 1 (reset for next start position)
    ldarg.1
    ldloc.2
    ldc.i4.1
    add
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    // pos++
    ldloc.2
    ldc.i4.1
    add
    stloc.2
    br         RUN_LOOP

  RUN_FAIL:
    // restore cursor; return Failed
    ldarg.1
    ldloc.0
    stfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    ldsfld     valuetype Snobol4.Runtime.Boxes.MatchResult
               Snobol4.Runtime.Boxes.MatchResult::Failed
    ret
  }

  // CommitCaptures — no-op at MSIL level; capture boxes commit inline
  .method private instance void CommitCaptures() cil managed
  {
    .maxstack 0
    ret
  }
}
