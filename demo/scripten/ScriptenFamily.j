.class public ScriptenFamily
.super java/lang/Object

.method public static main([Ljava/lang/String;)V
    .limit stack 4
    .limit locals 1
    ; SNOBOL4 — reads CSV from stdin, populates Prolog DB
    aconst_null
    invokestatic Family_snobol4/main([Ljava/lang/String;)V
    ; Icon — queries Prolog, prints report
    invokestatic Family_icon/icn_main()V
    return
.end method
