// bb_seq.il — SEQ: left then right; β retries right then left
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6

.assembly extern mscorlib {}

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

  // ── α ───────────────────────────────────────────────────────────────
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          α(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_lr,
                  valuetype Snobol4.Runtime.Boxes.Spec V_rr)

    // _mStart = cursor; _mLen = 0
    ldarg.0
    ldarg.1
    ldfld      int32 Snobol4.Runtime.Boxes.MatchState::Cursor
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mStart
    ldarg.0
    ldc.i4.0
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen

    // lr = _left.α(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::α(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_lr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_A_OMEGA

    // left_γ: accumulate
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    ldloca.s   V_lr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen

  SEQ_A_TRY_RIGHT:
    // rr = _right.α(ms)
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::α(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    ldloca.s   V_rr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brfalse    SEQ_A_GAMMA

    // right_ω → left.β (inline — no cross-method branch)
  SEQ_A_LEFT_BETA:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::β(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_lr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_A_OMEGA
    // left_γ from β: re-accumulate and retry right.α
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    ldloca.s   V_lr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
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

  // ── β ────────────────────────────────────────────────────────────────
  .method public virtual instance valuetype Snobol4.Runtime.Boxes.Spec
          β(class Snobol4.Runtime.Boxes.MatchState ms) cil managed
  {
    .maxstack 4
    .locals init (valuetype Snobol4.Runtime.Boxes.Spec V_rr,
                  valuetype Snobol4.Runtime.Boxes.Spec V_lr)

  SEQ_B_TRY_RIGHT:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::β(class Snobol4.Runtime.Boxes.MatchState)
    stloc.0
    ldloca.s   V_rr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brfalse    SEQ_B_GAMMA

  SEQ_B_LEFT_BETA:
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_left
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::β(class Snobol4.Runtime.Boxes.MatchState)
    stloc.1
    ldloca.s   V_lr
    call       instance bool Snobol4.Runtime.Boxes.Spec::get_IsFail()
    brtrue     SEQ_B_OMEGA
    ldarg.0
    ldarg.0
    ldfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    ldloca.s   V_lr
    ldfld      int32 Snobol4.Runtime.Boxes.Spec::Length
    add
    stfld      int32 Snobol4.Runtime.Boxes.bb_seq::_mLen
    // retry right.α
    ldarg.0
    ldfld      class Snobol4.Runtime.Boxes.IByrdBox Snobol4.Runtime.Boxes.bb_seq::_right
    ldarg.1
    callvirt   instance valuetype Snobol4.Runtime.Boxes.Spec
               Snobol4.Runtime.Boxes.IByrdBox::α(class Snobol4.Runtime.Boxes.MatchState)
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
