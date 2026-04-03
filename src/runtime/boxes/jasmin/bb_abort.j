.class public bb/bb_abort
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.inner class public static final abort_exception inner bb/bb_abort$AbortException outer bb/bb_abort

.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 1
    new bb/bb_abort$AbortException
    dup
    invokespecial bb/bb_abort$AbortException/<init>()V
    athrow
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 1
    new bb/bb_abort$AbortException
    dup
    invokespecial bb/bb_abort$AbortException/<init>()V
    athrow
.end method
