.class public bb/bb_seq
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final left Lbb/bb_box;
.field private final right Lbb/bb_box;
.field private matched_start I
.field private matched_len I

.method public <init>(Lbb/bb_box$MatchState;Lbb/bb_box;Lbb/bb_box;)V
    .limit stack 3
    .limit locals 4
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_seq/left Lbb/bb_box;
    aload_0
    aload_3
    putfield bb/bb_seq/right Lbb/bb_box;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    ; matched_start = delta; matched_len = 0
    aload_0
    aload_0
    getfield bb/bb_seq/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_seq/matched_start I
    aload_0
    iconst_0
    putfield bb/bb_seq/matched_len I
    ; lr = left.α()
    aload_0
    getfield bb/bb_seq/left Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull seq_omega
    ; matched_len += lr.len
    aload_0
    dup
    getfield bb/bb_seq/matched_len I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    putfield bb/bb_seq/matched_len I
    ; tail call rightAlpha
    aload_0
    invokevirtual bb/bb_seq/rightAlpha()Lbb/bb_box$Spec;
    areturn
seq_omega:
    aconst_null
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    ; rr = right.β()
    aload_0
    getfield bb/bb_seq/right Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull seq_beta_right_omega
    ; return Spec(matched_start, matched_len + rr.len)
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_seq/matched_start I
    aload_0
    getfield bb/bb_seq/matched_len I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
seq_beta_right_omega:
    aload_0
    invokevirtual bb/bb_seq/leftBeta()Lbb/bb_box$Spec;
    areturn
.end method

; private Spec rightAlpha()
.method private rightAlpha()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_seq/right Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull rightAlpha_omega
    ; return Spec(matched_start, matched_len + rr.len)
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_seq/matched_start I
    aload_0
    getfield bb/bb_seq/matched_len I
    aload_1
    getfield bb/bb_box$Spec/len I
    iadd
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
rightAlpha_omega:
    aload_0
    invokevirtual bb/bb_seq/leftBeta()Lbb/bb_box$Spec;
    areturn
.end method

; private Spec leftBeta()
.method private leftBeta()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 2
    aload_0
    getfield bb/bb_seq/left Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    astore_1
    aload_1
    ifnull leftBeta_omega
    ; matched_len = lr.len
    aload_0
    aload_1
    getfield bb/bb_box$Spec/len I
    putfield bb/bb_seq/matched_len I
    aload_0
    invokevirtual bb/bb_seq/rightAlpha()Lbb/bb_box$Spec;
    areturn
leftBeta_omega:
    aconst_null
    areturn
.end method
