.class public bb/bb_breakx
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final chars Ljava/lang/String;
.field private matched_len I
.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_breakx/chars Ljava/lang/String;
    return
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 6
    .limit locals 3
    aload_0
    iconst_0
    putfield bb/bb_breakx/matched_len I
bx_loop:
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_breakx/matched_len I
    iadd
    istore_1
    iload_1
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge bx_omega
    aload_0
    getfield bb/bb_breakx/chars Ljava/lang/String;
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    iload_1
    invokevirtual java/lang/String/charAt(I)C
    invokevirtual java/lang/String/indexOf(I)I
    ifge bx_check
    aload_0
    dup
    getfield bb/bb_breakx/matched_len I
    iconst_1
    iadd
    putfield bb/bb_breakx/matched_len I
    goto bx_loop
bx_check:
    ; BREAKX: fail if zero advance
    aload_0
    getfield bb/bb_breakx/matched_len I
    ifeq bx_omega
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_2
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_breakx/matched_len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_2
    aload_0
    getfield bb/bb_breakx/matched_len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
bx_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_breakx/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_breakx/matched_len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method
