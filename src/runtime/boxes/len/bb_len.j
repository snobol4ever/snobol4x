.class public bb/bb_len
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
    putfield bb/bb_len/n I
    aload_0
    aconst_null
    putfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
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
    putfield bb/bb_len/n I
    aload_0
    aload_2
    putfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    return
.end method
.method private val()I
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    ifnull len_val_static
    aload_0
    getfield bb/bb_len/dyn Ljava/util/function/IntSupplier;
    invokeinterface java/util/function/IntSupplier/getAsInt()I 1
    ireturn
len_val_static:
    aload_0
    invokevirtual bb/bb_len/val()I
    ireturn
.end method
.method public α()Lbb/bb_box$Spec;
    .limit stack 5
    .limit locals 2
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_len/val()I
    iadd
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/omega I
    if_icmpgt len_omega
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    getfield bb/bb_box$MatchState/delta I
    istore_1
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_len/val()I
    iadd
    putfield bb/bb_box$MatchState/delta I
    new bb/bb_box$Spec
    dup
    iload_1
    aload_0
    invokevirtual bb/bb_len/val()I
    invokespecial bb/bb_box$Spec/<init>(II)V
    areturn
len_omega:
    aconst_null
    areturn
.end method
.method public β()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    aload_0
    getfield bb/bb_len/ms Lbb/bb_box$MatchState;
    dup
    getfield bb/bb_box$MatchState/delta I
    aload_0
    invokevirtual bb/bb_len/val()I
    isub
    putfield bb/bb_box$MatchState/delta I
    aconst_null
    areturn
.end method
