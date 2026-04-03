.class public bb/bb_rem
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.method public <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_rem/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_rem/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/omega I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    aload_0
    getfield bb/bb_rem/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iload_1
    isub
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method
