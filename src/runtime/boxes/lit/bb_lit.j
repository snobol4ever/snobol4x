.class public bb/bb_lit
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box

.field private final lit Ljava/lang/String;
.field private final len I

.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_lit/lit Ljava/lang/String;
    aload_0
    aload_2
    invokevirtual java/lang/String/length()I
    putfield bb/bb_lit/len I
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/len I
    iadd
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpgt lit_omega
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/lit Ljava/lang/String;
    iconst_0
    aload_0
    getfield bb/bb_lit/len I
    invokevirtual java/lang/String/regionMatches(ILjava/lang/String;II)Z
    ifeq lit_omega
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    aload_0
    getfield bb/bb_lit/len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
lit_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_lit/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_lit/len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method
