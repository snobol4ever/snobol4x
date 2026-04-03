.class public bb/bb_bal
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private bal_len I
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
    .limit locals 4
    ; depth=0; len=0
    iconst_0
    istore_1               ; depth
    aload_0
    iconst_0
    putfield bb/bb_bal/bal_len I
bal_loop:
    ; while delta+len < omega
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    iadd
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpge bal_omega
    ; ch = sigma.charAt(delta+len)
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/sigma Ljava/lang/String;
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    iadd
    invokevirtual java/lang/String/charAt(I)C
    istore_2               ; ch
    ; if ch == '(' → depth++; len++
    iload_2
    bipush 40              ; '('
    if_icmpne bal_not_open
    iinc 1 1
    aload_0
    dup
    getfield bb/bb_bal/bal_len I
    iconst_1
    iadd
    putfield bb/bb_bal/bal_len I
    goto bal_check
bal_not_open:
    ; if ch == ')' && depth > 0 → depth--; len++
    iload_2
    bipush 41              ; ')'
    if_icmpne bal_other
    iload_1
    ifle bal_omega         ; depth == 0 and ')' → unmatched, stop
    iinc 1 -1
    aload_0
    dup
    getfield bb/bb_bal/bal_len I
    iconst_1
    iadd
    putfield bb/bb_bal/bal_len I
    goto bal_check
bal_other:
    ; other char: len++
    aload_0
    dup
    getfield bb/bb_bal/bal_len I
    iconst_1
    iadd
    putfield bb/bb_bal/bal_len I
bal_check:
    ; if depth == 0 && len > 0 → success
    iload_1
    ifne bal_loop
    aload_0
    getfield bb/bb_bal/bal_len I
    ifle bal_loop
    ; success
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_3
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_3
    aload_0
    getfield bb/bb_bal/bal_len I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
bal_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_bal/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_bal/bal_len I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method
