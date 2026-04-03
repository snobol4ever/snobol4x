.class public abstract bb/bb_box
.super java/lang/Object
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box

.field public static final Α I = 0
.field public static final Β I = 1
.field protected ms Lbb/bb_box$MatchState;

.method protected <init>(Lbb/bb_box$MatchState;)V
    .limit stack 2
    .limit locals 2
    aload_0
    invokespecial java/lang/Object/<init>()V
    aload_0
    aload_1
    putfield bb/bb_box/ms Lbb/bb_box$MatchState;
    return
.end method

.method public abstract α()Lbb/bb_box$Spec;
.end method

.method public abstract β()Lbb/bb_box$Spec;
.end method

.method public final call(I)Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 2
    iload_1
    ifne call_β
    aload_0
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    areturn
call_β:
    aload_0
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    areturn
.end method
