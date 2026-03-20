// snobol4run.il — SNOBOL4 .NET runtime internals
//
// Keyword state, I/O, and execution support for sno2c -net emitted programs.
//
// Compile once:
//   ilasm snobol4run.il /dll /output:snobol4run.dll
// Reference from emitted .il:
//   .assembly extern snobol4run {}

.assembly extern mscorlib {}
.assembly snobol4run {}
.module snobol4run.dll

.class public auto ansi beforefieldinit Snobol4Run
       extends [mscorlib]System.Object
{
  // -----------------------------------------------------------------------
  // Global keyword state — shared across all programs via static fields
  // -----------------------------------------------------------------------
  .field public static string kw_stno
  .field public static string kw_anchor
  .field public static string kw_trim
  .field public static string kw_fullscan

  .method static void .cctor() cil managed
  {
    .maxstack 1
    ldstr      "0"
    stsfld     string Snobol4Run::kw_stno
    ldstr      "0"
    stsfld     string Snobol4Run::kw_anchor
    ldstr      "0"
    stsfld     string Snobol4Run::kw_trim
    ldstr      "0"
    stsfld     string Snobol4Run::kw_fullscan
    ret
  }

  // -----------------------------------------------------------------------
  // sno_output — write a line to stdout (OUTPUT = x)
  // -----------------------------------------------------------------------
  .method public static void sno_output(string s) cil managed
  {
    .maxstack 1
    ldarg.0
    call       void [mscorlib]System.Console::WriteLine(string)
    ret
  }

  // -----------------------------------------------------------------------
  // sno_input — read a line from stdin (INPUT keyword)
  //   returns null on EOF  (caller must check: null → statement fails)
  //   returns "" for blank line (success, empty string)
  // -----------------------------------------------------------------------
  .method public static string sno_input() cil managed
  {
    .maxstack 1
    call       string [mscorlib]System.Console::ReadLine()
    ret
  }

  // -----------------------------------------------------------------------
  // sno_stno_inc — increment &STNO, return new value as string
  // -----------------------------------------------------------------------
  .method public static string sno_stno_inc() cil managed
  {
    .maxstack 3
    .locals init (int32 V_0)
    ldsfld     string Snobol4Run::kw_stno
    call       int32 [mscorlib]System.Int32::Parse(string)
    ldc.i4.1
    add
    stloc.0
    ldloc.0
    box        [mscorlib]System.Int32
    callvirt   instance string object::ToString()
    dup
    stsfld     string Snobol4Run::kw_stno
    ret
  }

} // end class Snobol4Run
