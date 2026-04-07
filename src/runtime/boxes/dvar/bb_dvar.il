// bb_dvar.il — DVAR: *VAR — re-resolve variable's live value on every α
// AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
.assembly extern mscorlib {}
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
