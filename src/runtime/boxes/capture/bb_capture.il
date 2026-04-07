// bb_capture.il — CAPTURE: $ writes immediately; . buffers for Phase-5 commit
// Uses a static delegate field pattern compatible with ilasm.
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
.assembly extern mscorlib {}

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
