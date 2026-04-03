.class public bb/bb_dvar
.super bb/bb_box
.inner class public static final spec inner bb/bb_box$Spec outer bb/bb_box
.inner class public static final matchstate inner bb/bb_box$MatchState outer bb/bb_box
.inner interface public static abstract box_resolver inner bb/bb_dvar$BoxResolver outer bb/bb_dvar
.field private final varname Ljava/lang/String;
.field private final resolver Lbb/bb_dvar$BoxResolver;
.field private child Lbb/bb_box;

.method public <init>(Lbb/bb_box$MatchState;Ljava/lang/String;Lbb/bb_dvar$BoxResolver;)V
    .limit stack 3
    .limit locals 4
    aload_0
    aload_1
    invokespecial bb/bb_box/<init>(Lbb/bb_box$MatchState;)V
    aload_0
    aload_2
    putfield bb/bb_dvar/varname Ljava/lang/String;
    aload_0
    aload_3
    putfield bb/bb_dvar/resolver Lbb/bb_dvar$BoxResolver;
    return
.end method

.method public α()Lbb/bb_box$Spec;
    .limit stack 4
    .limit locals 1
    ; child = resolver.resolve(varname, ms)
    aload_0
    aload_0
    getfield bb/bb_dvar/resolver Lbb/bb_dvar$BoxResolver;
    aload_0
    getfield bb/bb_dvar/varname Ljava/lang/String;
    aload_0
    getfield bb/bb_dvar/ms Lbb/bb_box$MatchState;
    invokeinterface bb/bb_dvar$BoxResolver/resolve(Ljava/lang/String;Lbb/bb_box$MatchState;)Lbb/bb_box; 3
    putfield bb/bb_dvar/child Lbb/bb_box;
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    ifnonnull dvar_alpha_call
    aconst_null
    areturn
dvar_alpha_call:
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    invokevirtual bb/bb_box/α()Lbb/bb_box$Spec;
    areturn
.end method

.method public β()Lbb/bb_box$Spec;
    .limit stack 2
    .limit locals 1
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    ifnonnull dvar_beta_call
    aconst_null
    areturn
dvar_beta_call:
    aload_0
    getfield bb/bb_dvar/child Lbb/bb_box;
    invokevirtual bb/bb_box/β()Lbb/bb_box$Spec;
    areturn
.end method
