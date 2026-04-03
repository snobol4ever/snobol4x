.class public bb/bb_not
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final child Lbb/bb_box;
.field private start I

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_not/child Lbb/bb_box;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    aload_0
    getfield bb/bb_not/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_not/start I
    aload_0
    getfield bb/bb_not/child Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    ; if child succeeded → NOT_ω
    aload_1
    ifnonnull not_omega
    ; child failed → restore delta, return zero-width
    aload_0
    getfield bb/bb_not/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_not/start I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_not/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
not_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method
