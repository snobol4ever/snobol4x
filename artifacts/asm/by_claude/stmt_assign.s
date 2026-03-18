; stmt_assign.s — Sprint A10 Step 2: X = 'foo' ; OUTPUT = X
;
; Build + run same as stmt_output_lit.s
;   ./stmt_assign   → prints "foo\n"

    global  main
    extern  stmt_init
    extern  stmt_strval
    extern  stmt_get
    extern  stmt_set
    extern  stmt_output
    extern  stmt_is_fail
    extern  stmt_finish

section .data
    str_foo     db  'foo', 0
    str_X       db  'X', 0
    str_OUTPUT  db  'OUTPUT', 0

section .note.GNU-stack noalloc noexec nowrite progbits

section .text

main:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 32

    call    stmt_init

    ; --- X = 'foo' ---
    ; _v = stmt_strval("foo")
    lea     rdi, [rel str_foo]
    call    stmt_strval
    mov     [rbp-16], rax
    mov     [rbp-8],  rdx

    ; if (!stmt_is_fail(_v)) stmt_set("X", _v)
    mov     rdi, [rbp-16]
    mov     rsi, [rbp-8]
    call    stmt_is_fail
    test    eax, eax
    jnz     .skip1

    lea     rdi, [rel str_X]
    mov     rsi, [rbp-16]
    mov     rdx, [rbp-8]
    call    stmt_set

.skip1:
    ; --- OUTPUT = X ---
    ; _v2 = stmt_get("X")
    lea     rdi, [rel str_X]
    call    stmt_get
    mov     [rbp-16], rax
    mov     [rbp-8],  rdx

    ; if (!stmt_is_fail(_v2)) stmt_output(_v2)
    mov     rdi, [rbp-16]
    mov     rsi, [rbp-8]
    call    stmt_is_fail
    test    eax, eax
    jnz     .skip2

    mov     rdi, [rbp-16]
    mov     rsi, [rbp-8]
    call    stmt_output

.skip2:
    call    stmt_finish
    xor     eax, eax
    leave
    ret
