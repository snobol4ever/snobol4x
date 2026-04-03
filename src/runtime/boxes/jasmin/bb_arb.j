.class public bb/bb_arb
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private arb_count I
.field private arb_start I
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
    ; count=0; start=delta; return Spec(delta,0)
    aload_0
    iconst_0
    putfield bb/bb_arb/arb_count I
    aload_0
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    putfield bb/bb_arb/arb_start I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    ; count++
    aload_0
    dup
    getfield bb/bb_arb/arb_count I
    iconst_1
    iadd
    putfield bb/bb_arb/arb_count I
    ; if start+count > omega → ω
    aload_0
    getfield bb/bb_arb/arb_start I
    aload_0
    getfield bb/bb_arb/arb_count I
    iadd
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpgt arb_omega
    ; delta = start; return Spec(delta, count); delta += count
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    aload_0
    getfield bb/bb_arb/arb_start I
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_arb/arb_count I
    invokespecial bb/bb_box$Spec/<init>(II)V
    ; delta += count
    aload_0
    getfield bb/bb_arb/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_arb/arb_count I
    iadd
    putfield bb/bb_box$MatchState/delta I
    areturn
arb_omega:
    aconst_null
    areturn
.end method
