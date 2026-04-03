.class public bb/bb_notany
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_notany/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge notany_omega
    aload_0
    getfield bb/bb_notany/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    ifge notany_omega
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    iconst_1
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    iconst_1
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
notany_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_notany/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    iconst_1
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method
