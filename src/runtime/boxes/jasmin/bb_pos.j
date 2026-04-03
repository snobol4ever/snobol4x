.class public bb/bb_pos
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.field private final n I
.field private final dyn Ljava/util/function/IntSupplier;
.method public <init>(Lbb/bb_box$MatchState;I)V
    .limit stack 3
    .limit locals 3
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    iload_2
    putfield bb/bb_pos/n I
    aload_0
    aconst_null
    putfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
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
    putfield bb/bb_pos/n I
    aload_0
    aload_2
    putfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    ifnull pos_val_static
    aload_0
    getfield bb/bb_pos/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
pos_val_static:
    aload_0
    invokevirtual bb/bb_pos/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 1
    aload_0
    getfield bb/bb_pos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_pos/val()I
    if_icmpne pos_omega
    new bb/bb_box$Spec
    dup
    aload_0
    getfield bb/bb_pos/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    iconst_0
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
pos_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 1
    .limit locals 1
    aconst_null
    areturn
.end method
