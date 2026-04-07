; ByrdBoxLinkage.j — shared JVM runtime for SCRIP cross-language ABI.
; Provides the RESULT thread-local slot used by all EXPORT wrappers.
; See ARCH-scrip-abi.md §3.2.
;
; Compiled once and included on the jasmin classpath for every multi-language
; SCRIP program.

.class public ByrdBoxLinkage
.super java/lang/Object

; RESULT: a static AtomicReference<Object> used as a thread-local result slot.
; EXPORT wrappers set this before invoking gamma; callers read it after return.
.field public static RESULT Ljava/util/concurrent/atomic/AtomicReference;

.method static <clinit>()V
    .limit stack 2
    .limit locals 0
    new java/util/concurrent/atomic/AtomicReference
    dup
    invokespecial java/util/concurrent/atomic/AtomicReference/<init>()V
    putstatic ByrdBoxLinkage/RESULT Ljava/util/concurrent/atomic/AtomicReference;
    return
.end method

.method public <init>()V
    .limit stack 1
    .limit locals 1
    aload_0
    invokespecial java/lang/Object/<init>()V
    return
.end method
