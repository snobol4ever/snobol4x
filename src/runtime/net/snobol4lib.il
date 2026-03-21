// snobol4lib.il - SNOBOL4 standard library helpers for sno2c -net backend
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
  // sno_parse_dbl - parse string to float64; empty/unparseable -> 0.0
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
  // sno_fmt_dbl - format float64 as SNOBOL4 numeric string
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
  // sno_add - numeric if both parseable, else string concat
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
  // sno_sub / sno_mpy / sno_div - numeric binary ops
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
    // SNOBOL4: integer / integer = integer (truncating toward zero)
    // real / anything = real
    .maxstack 4
    .locals init (int64 V_0, int64 V_1, float64 V_2, float64 V_3, bool V_4, bool V_5)
    // Try parsing a as int64
    ldarg.0
    ldloca.s V_0
    call       bool [mscorlib]System.Int64::TryParse(string, int64&)
    stloc.s V_4
    ldarg.1
    ldloca.s V_1
    call       bool [mscorlib]System.Int64::TryParse(string, int64&)
    stloc.s V_5
    // Both integers?
    ldloc.s V_4
    ldloc.s V_5
    and
    brfalse    DIV_FLOAT
    // Integer division — truncate toward zero
    ldloc.0
    ldloc.1
    div
    box        [mscorlib]System.Int64
    callvirt   instance string object::ToString()
    ret
  DIV_FLOAT:
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.2
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.3
    ldloc.2
    ldloc.3
    div
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_pow - exponentiation (a ** b)
  // SNOBOL4: integer ** integer = integer when result is whole, else real
  // -----------------------------------------------------------------------
  .method public static string sno_pow(string a, string b) cil managed
  {
    .maxstack 3
    .locals init (float64 V_0, float64 V_1, float64 V_2)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    stloc.1
    ldloc.0
    ldloc.1
    call       float64 [mscorlib]System.Math::Pow(float64, float64)
    stloc.2
    ldloc.2
    call       string [snobol4lib]Snobol4Lib::sno_fmt_dbl(float64)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_neg - unary numeric negation
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
  // sno_gt/lt/ge/le/eq/ne - numeric comparisons, return int32 1=success 0=fail
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
  // sno_ident / sno_differ - string identity comparison
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
  // sno_size - returns string length as integer string
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
  // sno_litmatch - 1 if subj contains pat, 0 otherwise
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
  // sno_alphabet - 256-char string of all bytes 0..255 (&ALPHABET)
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
  // sno_datatype - returns "integer", "real", or "string"
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
  // sno_lgt/llt/lge/lle/leq/lne - lexical string comparisons
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
  // TODO: sno_char, sno_ord - add when needed
  // -----------------------------------------------------------------------


  // -----------------------------------------------------------------------
  // sno_substr(str, start_str, len_str) - 1-based substring
  //   len_str = "-1" means to end of string
  // -----------------------------------------------------------------------
  .method public static string sno_substr(string s, string start_s, string len_s) cil managed
  {
    .maxstack 4
    .locals init (int32 V_0, int32 V_1, int32 V_2)
    // start0 = int(start_s) - 1
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i4
    ldc.i4.1
    sub
    stloc.0
    // slen
    ldarg.0
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    stloc.2
    // clamp start0
    ldloc.0
    ldc.i4.0
    bge        SUB_START_OK
    ldc.i4.0
    stloc.0
  SUB_START_OK:
    // len
    ldarg.2
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i4
    stloc.1
    ldloc.1
    ldc.i4.0
    bge        SUB_LEN_OK
    // len < 0 means to end
    ldloc.2
    ldloc.0
    sub
    stloc.1
  SUB_LEN_OK:
    // clamp end
    ldloc.0
    ldloc.1
    add
    ldloc.2
    blt        SUB_END_OK
    ldloc.2
    ldloc.0
    sub
    stloc.1
  SUB_END_OK:
    ldarg.0
    ldloc.0
    ldloc.1
    callvirt   instance string [mscorlib]System.String::Substring(int32, int32)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_replace(str, from, to) - char-by-char translation table
  // -----------------------------------------------------------------------
  .method public static string sno_replace(string s, string from_s, string to_s) cil managed
  {
    .maxstack 4
    .locals init (char[] V_0, int32 V_1, int32 V_2, int32 V_3, int32 V_4, int32 V_5)
    // convert s to char array
    ldarg.0
    callvirt   instance char[] [mscorlib]System.String::ToCharArray()
    stloc.0
    ldc.i4.0
    stloc.1
  REPL_OUTER:
    ldloc.1
    ldloc.0
    ldlen
    bge        REPL_OUTER_DONE
    ldloc.0
    ldloc.1
    ldelem.u2
    stloc.3
    // search from_s for this char
    ldc.i4.0
    stloc.2
  REPL_INNER:
    ldloc.2
    ldarg.1
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    bge        REPL_INNER_DONE
    ldarg.1
    ldloc.2
    callvirt   instance char [mscorlib]System.String::get_Chars(int32)
    stloc.s V_5
    ldloc.3
    ldloc.s V_5
    bne.un     REPL_NO_MATCH
    // found - replace with to_s[i] if in range, then stop inner loop
    ldloc.2
    ldarg.2
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    bge        REPL_INNER_DONE
    ldarg.2
    ldloc.2
    callvirt   instance char [mscorlib]System.String::get_Chars(int32)
    stloc.s V_5
    ldloc.0
    ldloc.1
    ldloc.s V_5
    stelem.i2
    br         REPL_INNER_DONE
  REPL_NO_MATCH:
    ldloc.2
    ldc.i4.1
    add
    stloc.2
    br         REPL_INNER
  REPL_INNER_DONE:
    // exit inner loop
    ldloc.1
    ldc.i4.1
    add
    stloc.1
    br         REPL_OUTER
  REPL_OUTER_DONE:
    ldloc.0
    newobj     instance void [mscorlib]System.String::.ctor(char[])
    ret
  }

  // -----------------------------------------------------------------------
  // sno_dupl(str, n_str) - repeat str n times
  // -----------------------------------------------------------------------
  .method public static string sno_dupl(string s, string n_s) cil managed
  {
    .maxstack 3
    .locals init (int32 V_0, string V_1)
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i4
    stloc.0
    ldstr      ""
    stloc.1
  DUPL_LOOP:
    ldloc.0
    ldc.i4.0
    ble        DUPL_DONE
    ldloc.1
    ldarg.0
    call       string [mscorlib]System.String::Concat(string, string)
    stloc.1
    ldloc.0
    ldc.i4.1
    sub
    stloc.0
    br         DUPL_LOOP
  DUPL_DONE:
    ldloc.1
    ret
  }

  // -----------------------------------------------------------------------
  // sno_trim(str) - remove trailing whitespace
  // -----------------------------------------------------------------------
  .method public static string sno_trim(string s) cil managed
  {
    .maxstack 2
    ldarg.0
    callvirt   instance string [mscorlib]System.String::TrimEnd()
    ret
  }

  // -----------------------------------------------------------------------
  // sno_reverse(str) - reverse string characters
  // -----------------------------------------------------------------------
  .method public static string sno_reverse(string s) cil managed
  {
    .maxstack 3
    .locals init (char[] V_0)
    ldarg.0
    callvirt   instance char[] [mscorlib]System.String::ToCharArray()
    stloc.0
    ldloc.0
    call       void [mscorlib]System.Array::Reverse(class [mscorlib]System.Array)
    ldloc.0
    newobj     instance void [mscorlib]System.String::.ctor(char[])
    ret
  }

  // -----------------------------------------------------------------------
  // sno_lpad(str, n_str, pad) - left-pad string to length n
  // -----------------------------------------------------------------------
  .method public static string sno_lpad(string s, string n_s, string pad) cil managed
  {
    .maxstack 4
    .locals init (int32 V_0, int32 V_1, int32 V_2)
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i4
    stloc.0
    ldarg.0
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    stloc.1
    ldloc.1
    ldloc.0
    bge        LPAD_DONE
    // pad char: first char of pad arg, default space
    ldarg.2
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    ldc.i4.0
    ble        LPAD_USE_SPACE
    ldarg.2
    ldc.i4.0
    callvirt   instance char [mscorlib]System.String::get_Chars(int32)
    stloc.2
    br         LPAD_DO
  LPAD_USE_SPACE:
    ldc.i4.s   32
    stloc.2
  LPAD_DO:
    ldarg.0
    ldloc.0
    ldloc.2
    conv.u2
    callvirt   instance string [mscorlib]System.String::PadLeft(int32, char)
    ret
  LPAD_DONE:
    ldarg.0
    ret
  }

  // -----------------------------------------------------------------------
  // sno_rpad(str, n_str, pad) - right-pad string to length n
  // -----------------------------------------------------------------------
  .method public static string sno_rpad(string s, string n_s, string pad) cil managed
  {
    .maxstack 4
    .locals init (int32 V_0, int32 V_1, int32 V_2)
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i4
    stloc.0
    ldarg.0
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    stloc.1
    ldloc.1
    ldloc.0
    bge        RPAD_DONE
    ldarg.2
    callvirt   instance int32 [mscorlib]System.String::get_Length()
    ldc.i4.0
    ble        RPAD_USE_SPACE
    ldarg.2
    ldc.i4.0
    callvirt   instance char [mscorlib]System.String::get_Chars(int32)
    stloc.2
    br         RPAD_DO
  RPAD_USE_SPACE:
    ldc.i4.s   32
    stloc.2
  RPAD_DO:
    ldarg.0
    ldloc.0
    ldloc.2
    conv.u2
    callvirt   instance string [mscorlib]System.String::PadRight(int32, char)
    ret
  RPAD_DONE:
    ldarg.0
    ret
  }

  // -----------------------------------------------------------------------
  // sno_is_integer(str) - returns int32 1 if str is an integer, 0 otherwise
  // -----------------------------------------------------------------------
  .method public static int32 sno_is_integer(string s) cil managed
  {
    .maxstack 3
    .locals init (int64 V_0)
    ldarg.0
    ldloca.s V_0
    call       bool [mscorlib]System.Int64::TryParse(string, int64&)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_remdr(a, b) - integer remainder as decimal string
  // -----------------------------------------------------------------------
  .method public static string sno_remdr(string a, string b) cil managed
  {
    .maxstack 3
    .locals init (int64 V_0, int64 V_1)
    ldarg.0
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i8
    stloc.0
    ldarg.1
    call       float64 [snobol4lib]Snobol4Lib::sno_parse_dbl(string)
    conv.i8
    stloc.1
    ldloc.0
    ldloc.1
    rem
    box        [mscorlib]System.Int64
    callvirt   instance string object::ToString()
    ret
  }

} // end class Snobol4Lib
