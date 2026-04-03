.class public bb/bb_rtab
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.field private advance I
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_rtab/n I
    aload_0
    aconst_null
    putfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method public <init>(Lbb/bb_box$MatchState;Ljava/util/function/IntSupplier;)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iconst_0
    putfield bb/bb_rtab/n I
    aload_0
    aload_2
    putfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    ifnull rtab_val_static
    aload_0
    getfield bb/bb_rtab/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
rtab_val_static:
    aload_0
    invokevirtual bb/bb_rtab/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 4
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    aload_0
    invokevirtual bb/bb_rtab/val()I
    isub
    istore_1
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iload_1
    if_icmpgt rtab_omega
    iload_1
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    isub
    istore_2
    aload_0
    iload_2
    putfield bb/bb_rtab/advance I
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_3
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    iload_1
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_3
    iload_2
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
rtab_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_rtab/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    getfield bb/bb_rtab/advance I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method
