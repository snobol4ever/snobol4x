// snobol4lib.il — SNOBOL4 standard library helpers for sno2c -net backend
//
// All sno_* helper functions used by emitted .il programs.
// Will grow to include all INCLUDES from beauty.sno and full SNOBOL4 repertoire.
//
// Compile once:
//   ilasm snobol4lib.il /dll /output:snobol4lib.dll
// Reference from emitted .il:
//   .assembly extern snobol4lib {}
//   call string [snobol4lib]Snobol4Lib::sno_add(string, string)

.assembly extern mscorlib {}
.assembly snobol4lib {}
.module snobol4lib.dll

.class public auto ansi beforefieldinit Snobol4Lib
       extends [mscorlib]System.Object
{

  // -----------------------------------------------------------------------
  // sno_parse_dbl — parse string to float64; empty/unparseable -> 0.0
  // -----------------------------------------------------------------------
  .method public static float64 sno_parse_dbl(string s) cil managed
  {
    .maxstack 2
    .locals init (float64 V_0, bool V_1)
    ldarg.0
    ldloca.s V_0
    call       bool [mscorlib]System.Double::TryParse(string, float64&)
    pop
    ldloc.0
    ret
  }

  // -----------------------------------------------------------------------
  // sno_fmt_dbl — format float64 as SNOBOL4 numeric string
  //   integer-valued -> no decimal; real -> default ToString
  // -----------------------------------------------------------------------
  .method public static string sno_fmt_dbl(float64 v) cil managed
  {
    .maxstack 3
    .locals init (float64 V_0, int64 V_1)
    ldarg.0
    stloc.0
    ldloc.0
    conv.i8
    stloc.1
    ldloc.0
    ldloc.1
    conv.r8
    beq        FMT_IS_INT
    ldloc.0
    box        [mscorlib]System.Double
    callvirt   instance string object::ToString()
    ret
  FMT_IS_INT:
    ldloc.1
    box        [mscorlib]System.Int64
    callvirt   instance string object::ToString()
    ret
  }

  // -----------------------------------------------------------------------
  // sno_add — numeric if both parseable, else string concat
  // -----------------------------------------------------------------------
  .method public static string sno_add(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1, bool V_2, bool V_3, string V_4, string V_5)
    ldarg.0
    callvirt   instance string [mscorlib]System.String::Trim()
    stloc.s V_4
    ldarg.1
    callvirt   instance string [mscorlib]System.String::Trim()
    stloc.s V_5
    ldloc.s V_4
    ldstr      ""
    call       bool [mscorlib]System.String::op_Equality(string, string)
    brfalse    ADD_PARSE_A
    ldstr      "0"
    stloc.s V_4
  ADD_PARSE_A:
    ldloc.s V_4
    ldloca.s V_0
    call       bool [mscorlib]System.Double::TryParse(string, float64&)
    stloc.2
    ldloc.s V_5
    ldstr      ""
    call       bool [mscorlib]System.String::op_Equality(string, string)
    brfalse    ADD_PARSE_B
    ldstr      "0"
    stloc.s V_5
  ADD_PARSE_B:
    ldloc.s V_5
    ldloca.s V_1
    call       bool [mscorlib]System.Double::TryParse(string, float64&)
    stloc.3
    ldloc.2
    ldloc.3
    and
    brfalse    ADD_CONCAT
    ldloc.0
    ldloc.1
    add
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  ADD_CONCAT:
    ldarg.0
    ldarg.1
    call       string [mscorlib]System.String::Concat(string, string)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_sub / sno_mpy / sno_div — numeric binary ops
  // -----------------------------------------------------------------------
  .method public static string sno_sub(string a, string b) cil managed
  {
    .maxstack 3
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    sub
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  }

  .method public static string sno_mpy(string a, string b) cil managed
  {
    .maxstack 3
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    mul
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  }

  .method public static string sno_div(string a, string b) cil managed
  {
    .maxstack 3
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    div
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_neg — unary numeric negation
  // -----------------------------------------------------------------------
  .method public static string sno_neg(string a) cil managed
  {
    .maxstack 2
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    neg
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_gt/lt/ge/le/eq/ne — numeric comparisons, return int32 1=success 0=fail
  // -----------------------------------------------------------------------
  .method public static int32 sno_gt(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    bgt        GT_TRUE
    ldc.i4.0
    ret
  GT_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_lt(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    blt        LT_TRUE
    ldc.i4.0
    ret
  LT_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_ge(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    bge        GE_TRUE
    ldc.i4.0
    ret
  GE_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_le(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    ble        LE_TRUE
    ldc.i4.0
    ret
  LE_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_eq(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    beq        EQ_TRUE
    ldc.i4.0
    ret
  EQ_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_ne(string a, string b) cil managed
  {
    .maxstack 4
    .locals init (float64 V_0, float64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    bne.un     NE_TRUE
    ldc.i4.0
    ret
  NE_TRUE:
    ldc.i4.1
    ret
  }

  // -----------------------------------------------------------------------
  // sno_ident / sno_differ — string identity comparison
  // -----------------------------------------------------------------------
  .method public static int32 sno_ident(string a, string b) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    call       bool [mscorlib]System.String::op_Equality(string, string)
    ret
  }

  .method public static int32 sno_differ(string a, string b) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    call       bool [mscorlib]System.String::op_Inequality(string, string)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_size — returns string length as integer string
  // -----------------------------------------------------------------------
  .method public static string sno_size(string a) cil managed
  {
    .maxstack 2
    ldarg.0
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    box        [mscorlib]System.Int32
    callvirt   instance string object::ToString()
    ret
  }

  // -----------------------------------------------------------------------
  // sno_litmatch — 1 if subj contains pat, 0 otherwise
  // -----------------------------------------------------------------------
  .method public static int32 sno_litmatch(string subj, string pat) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    callvirt   instance bool [mscorlib]System.String::Contains(string)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_alphabet — 256-char string of all bytes 0..255 (&ALPHABET)
  // -----------------------------------------------------------------------
  .method public static string sno_alphabet() cil managed
  {
    .maxstack 3
    .locals init (char[] V_0, int32 V_1)
    ldc.i4     256
    newarr     [mscorlib]System.Char
    stloc.0
    ldc.i4.0
    stloc.1
  ALPHA_LOOP:
    ldloc.1
    ldc.i4     256
    bge        ALPHA_DONE
    ldloc.0
    ldloc.1
    ldloc.1
    conv.u2
    stelem.i2
    ldloc.1
    ldc.i4.1
    add
    stloc.1
    br         ALPHA_LOOP
  ALPHA_DONE:
    ldloc.0
    newobj     instance void [mscorlib]System.String::.ctor(char[])
    ret
  }

  // -----------------------------------------------------------------------
  // sno_datatype — returns "integer", "real", or "string"
  // -----------------------------------------------------------------------
  .method public static string sno_datatype(string s) cil managed
  {
    .maxstack 3
    .locals init (float64 V_0, int64 V_1, bool V_2)
    ldarg.0
    ldloca.s V_0
    call       bool [mscorlib]System.Double::TryParse(string, float64&)
    stloc.2
    ldloc.2
    brfalse    DT_STRING
    ldloc.0
    conv.i8
    stloc.1
    ldloc.0
    ldloc.1
    conv.r8
    beq        DT_INTEGER
    ldstr      "real"
    ret
  DT_INTEGER:
    ldstr      "integer"
    ret
  DT_STRING:
    ldstr      "string"
    ret
  }

  // -----------------------------------------------------------------------
  // sno_lgt/llt/lge/lle/leq/lne — lexical string comparisons
  // -----------------------------------------------------------------------
  .method public static int32 sno_lgt(string a, string b) cil managed
  {
    .maxstack 3
    ldarg.0
    ldarg.1
    ldc.i4.4
    call       int32 [mscorlib]System.String::Compare(string, string, valuetype [mscorlib]System.StringComparison)
    ldc.i4.0
    bgt        LGT_TRUE
    ldc.i4.0
    ret
  LGT_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_llt(string a, string b) cil managed
  {
    .maxstack 3
    ldarg.0
    ldarg.1
    ldc.i4.4
    call       int32 [mscorlib]System.String::Compare(string, string, valuetype [mscorlib]System.StringComparison)
    ldc.i4.0
    blt        LLT_TRUE
    ldc.i4.0
    ret
  LLT_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_lge(string a, string b) cil managed
  {
    .maxstack 3
    ldarg.0
    ldarg.1
    ldc.i4.4
    call       int32 [mscorlib]System.String::Compare(string, string, valuetype [mscorlib]System.StringComparison)
    ldc.i4.0
    bge        LGE_TRUE
    ldc.i4.0
    ret
  LGE_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_lle(string a, string b) cil managed
  {
    .maxstack 3
    ldarg.0
    ldarg.1
    ldc.i4.4
    call       int32 [mscorlib]System.String::Compare(string, string, valuetype [mscorlib]System.StringComparison)
    ldc.i4.0
    ble        LLE_TRUE
    ldc.i4.0
    ret
  LLE_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_leq(string a, string b) cil managed
  {
    .maxstack 3
    ldarg.0
    ldarg.1
    ldc.i4.4
    call       int32 [mscorlib]System.String::Compare(string, string, valuetype [mscorlib]System.StringComparison)
    ldc.i4.0
    beq        LEQ_TRUE
    ldc.i4.0
    ret
  LEQ_TRUE:
    ldc.i4.1
    ret
  }

  .method public static int32 sno_lne(string a, string b) cil managed
  {
    .maxstack 3
    ldarg.0
    ldarg.1
    ldc.i4.4
    call       int32 [mscorlib]System.String::Compare(string, string, valuetype [mscorlib]System.StringComparison)
    ldc.i4.0
    bne.un     LNE_TRUE
    ldc.i4.0
    ret
  LNE_TRUE:
    ldc.i4.1
    ret
  }

  // -----------------------------------------------------------------------
  // TODO: beauty.sno INCLUDE functions to add here:
  //   sno_substr, sno_dupl, sno_replace, sno_ucase, sno_lcase,
  //   sno_lpad, sno_rpad, sno_trim, sno_integer, sno_real,
  //   sno_reverse, sno_char, sno_ord, sno_remdr
  // -----------------------------------------------------------------------

} // end class Snobol4Lib
