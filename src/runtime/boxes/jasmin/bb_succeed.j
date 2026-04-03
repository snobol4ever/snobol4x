.class public bb/bb_succeed
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
    .limit stack 5
    .limit locals 1
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_succeed/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_succeed/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
