; arbno_empty.s — Sprint A5 artifact
; Pattern: POS(0) ARBNO(LIT("x")) RPOS(0)   Subject: "aaa"
; Expected: zero reps match at pos 0, RPOS(0) fails (cursor=0 ≠ subj_len=3),
;           backtrack into ARBNO, LIT("x") fails on "aaa", ARBNO fails,
;           overall match fails → exit 1
;
; Wait — re-reading SNOBOL4 semantics: ARBNO matches zero or more.
; With subject "aaa" and pattern POS(0) ARBNO('x') RPOS(0):
;   ARBNO succeeds with 0 reps (cursor=0). RPOS(0) checks cursor==3 → fails.
;   Backtrack to ARBNO β: try 'x' at cursor 0 → 'a'≠'x' → fail.
;   Match fails → exit 1.
;
; This tests the "zero reps then fail" path.
;
; assemble:  nasm -f elf64 arbno_empty.s -o arbno_empty.o
; link:      ld arbno_empty.o -o arbno_empty
; run:       ./arbno_empty; echo $?   # must be 1 (fail, no output)

    global _start

section .data

subject:    db "aaa"
subj_len:   equ 3
lit_str:    db "x"
lit_len:    equ 1

section .bss

cursor:         resq 1
assign_start:   resq 1
lit_saved:      resq 1
arbno_depth:    resq 1
arbno_stack:    resq 64

section .text

_start:
    mov     qword [cursor], 0
    mov     qword [arbno_depth], 0
    jmp     pos_alpha

; POS(0)
pos_alpha:
    cmp     qword [cursor], 0
    jne     pos_omega
    jmp     pos_gamma
pos_beta:
pos_omega:
    jmp     match_fail
pos_gamma:
    jmp     assign_alpha

; record span start
assign_alpha:
    mov     rax, [cursor]
    mov     [assign_start], rax
    jmp     arbno_alpha

; ARBNO(LIT("x"))
arbno_alpha:
    mov     rax, [arbno_depth]
    lea     rbx, [rel arbno_stack]
    mov     rcx, [cursor]
    mov     [rbx + rax*8], rcx
    inc     rax
    mov     [arbno_depth], rax
    jmp     arbno_gamma

arbno_beta:
    mov     rax, [arbno_depth]
    test    rax, rax
    jz      arbno_omega
    dec     rax
    mov     [arbno_depth], rax
    lea     rbx, [rel arbno_stack]
    mov     rcx, [rbx + rax*8]
    mov     [cursor], rcx

    mov     rax, [cursor]
    mov     [lit_saved], rax

    mov     rax, [cursor]
    add     rax, lit_len
    cmp     rax, subj_len
    jg      arbno_omega

    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx
    lea     rdi, [rel lit_str]
    mov     rcx, lit_len
    repe    cmpsb
    jne     arbno_omega

    mov     rax, [cursor]
    add     rax, lit_len
    mov     [cursor], rax

    mov     rbx, [lit_saved]
    cmp     rax, rbx
    je      arbno_omega

    mov     rax, [arbno_depth]
    lea     rbx, [rel arbno_stack]
    mov     rcx, [cursor]
    mov     [rbx + rax*8], rcx
    inc     rax
    mov     [arbno_depth], rax
    jmp     arbno_gamma

arbno_omega:
    jmp     match_fail

arbno_gamma:
    jmp     rpos_alpha

; RPOS(0)
rpos_alpha:
    mov     rax, [cursor]
    cmp     rax, subj_len
    jne     rpos_omega
    jmp     rpos_gamma
rpos_beta:
rpos_omega:
    jmp     arbno_beta
rpos_gamma:
    jmp     match_success

match_success:
    mov     eax, 60
    xor     edi, edi
    syscall

match_fail:
    mov     eax, 60
    mov     edi, 1
    syscall
