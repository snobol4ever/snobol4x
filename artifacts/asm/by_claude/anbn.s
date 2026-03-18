; anbn.s — Sprint A8 artifact
; Pattern: POS(0) SPAN("a") $ AHEAD  SPAN("b") RPOS(0)  where |AHEAD| == |b-span|
; Simpler testable version: match "aabb" as two named patterns with shared counter.
;
; Actually: demonstrate named pattern SELF-REFERENCE / nesting.
; We use the classic a^n b^n check: count a's, then verify exactly that many b's.
;
; Implementation: two named patterns A_BLOCK and B_BLOCK.
;   A_BLOCK = LIT("a")   (matches exactly one 'a')
;   B_BLOCK = LIT("b")   (matches exactly one 'b')
;
; Top-level pattern (for subject "aabb", n=2):
;   POS(0) A_BLOCK A_BLOCK B_BLOCK B_BLOCK RPOS(0)
;   → match "aabb", print it, exit 0.
;
; This oracle proves:
;   1. Named pattern α/β/γ/ω indirect-jmp calling convention works
;   2. Multiple references to the SAME named pattern in sequence work
;   3. Backtracking through multiple named pattern call sites works
;
; Subject: "aabb"  — expect "aabb\n", exit 0
; Subject: "ab"    — would also match (n=1), but we fix subject to "aabb"
;
; Calling convention (same as ref_astar_bstar.s):
;   Before jumping to pat_NAME_alpha or pat_NAME_beta:
;     lea rax, [rel continuation_label]
;     mov [pat_NAME_ret_gamma], rax
;     lea rax, [rel fail_label]
;     mov [pat_NAME_ret_omega], rax
;   Named pattern body ends: jmp [pat_NAME_ret_gamma] / jmp [pat_NAME_ret_omega]
;
; assemble:  nasm -f elf64 anbn.s -o anbn.o
; link:      ld anbn.o -o anbn
; run:       ./anbn && echo PASS || echo FAIL

    global _start

section .data

subject:    db "aabb"
subj_len:   equ 4
lit_a:      db "a"
lit_b:      db "b"
newline:    db 10

section .bss

cursor:             resq 1

; A_BLOCK calling slots
pat_A_ret_gamma:    resq 1
pat_A_ret_omega:    resq 1
; A_BLOCK internals
a_lit_saved:        resq 1

; B_BLOCK calling slots
pat_B_ret_gamma:    resq 1
pat_B_ret_omega:    resq 1
; B_BLOCK internals
b_lit_saved:        resq 1

section .text

; =======================================================================
; _start: POS(0) . A_BLOCK . A_BLOCK . B_BLOCK . B_BLOCK . RPOS(0)
; Sequence of 4 named-pattern call sites. Backtrack chain on failure.
; =======================================================================
_start:
    mov     qword [cursor], 0

    ; ref0: call A_BLOCK (first 'a')
    lea     rax, [rel ref0_gamma]
    mov     [pat_A_ret_gamma], rax
    lea     rax, [rel ref0_omega]
    mov     [pat_A_ret_omega], rax
    jmp     pat_A_alpha

ref0_gamma:
    ; ref1: call A_BLOCK (second 'a')
    lea     rax, [rel ref1_gamma]
    mov     [pat_A_ret_gamma], rax
    lea     rax, [rel ref1_omega]
    mov     [pat_A_ret_omega], rax
    jmp     pat_A_alpha

ref1_gamma:
    ; ref2: call B_BLOCK (first 'b')
    lea     rax, [rel ref2_gamma]
    mov     [pat_B_ret_gamma], rax
    lea     rax, [rel ref2_omega]
    mov     [pat_B_ret_omega], rax
    jmp     pat_B_alpha

ref2_gamma:
    ; ref3: call B_BLOCK (second 'b')
    lea     rax, [rel ref3_gamma]
    mov     [pat_B_ret_gamma], rax
    lea     rax, [rel ref3_omega]
    mov     [pat_B_ret_omega], rax
    jmp     pat_B_alpha

ref3_gamma:
    ; RPOS(0): cursor must equal subj_len
    cmp     qword [cursor], subj_len
    je      match_success
    ; not at end — backtrack into ref3 (B_BLOCK beta)
    lea     rax, [rel ref3_gamma]
    mov     [pat_B_ret_gamma], rax
    lea     rax, [rel ref3_omega]
    mov     [pat_B_ret_omega], rax
    jmp     pat_B_beta

ref3_omega:
    ; B_BLOCK(2) failed — backtrack into ref2 (B_BLOCK beta)
    lea     rax, [rel ref2_gamma]
    mov     [pat_B_ret_gamma], rax
    lea     rax, [rel ref2_omega]
    mov     [pat_B_ret_omega], rax
    jmp     pat_B_beta

ref2_omega:
    ; B_BLOCK(1) failed — backtrack into ref1 (A_BLOCK beta)
    lea     rax, [rel ref1_gamma]
    mov     [pat_A_ret_gamma], rax
    lea     rax, [rel ref1_omega]
    mov     [pat_A_ret_omega], rax
    jmp     pat_A_beta

ref1_omega:
    ; A_BLOCK(2) failed — backtrack into ref0 (A_BLOCK beta)
    lea     rax, [rel ref0_gamma]
    mov     [pat_A_ret_gamma], rax
    lea     rax, [rel ref0_omega]
    mov     [pat_A_ret_omega], rax
    jmp     pat_A_beta

ref0_omega:
    jmp     match_fail

; =======================================================================
; Named pattern: A_BLOCK = LIT("a")
; =======================================================================
pat_A_alpha:
    ; bounds check
    mov     rax, [cursor]
    cmp     rax, subj_len
    jge     pat_A_omega
    ; compare subject[cursor] == 'a'
    lea     rbx, [rel subject]
    movzx   ecx, byte [rbx + rax]
    cmp     cl, 'a'
    jne     pat_A_omega
    ; save cursor, advance
    mov     [a_lit_saved], rax
    inc     rax
    mov     [cursor], rax
    jmp     [pat_A_ret_gamma]

pat_A_beta:
    ; restore cursor
    mov     rax, [a_lit_saved]
    mov     [cursor], rax
    jmp     pat_A_omega

pat_A_omega:
    jmp     [pat_A_ret_omega]

; =======================================================================
; Named pattern: B_BLOCK = LIT("b")
; =======================================================================
pat_B_alpha:
    mov     rax, [cursor]
    cmp     rax, subj_len
    jge     pat_B_omega
    lea     rbx, [rel subject]
    movzx   ecx, byte [rbx + rax]
    cmp     cl, 'b'
    jne     pat_B_omega
    mov     [b_lit_saved], rax
    inc     rax
    mov     [cursor], rax
    jmp     [pat_B_ret_gamma]

pat_B_beta:
    mov     rax, [b_lit_saved]
    mov     [cursor], rax
    jmp     pat_B_omega

pat_B_omega:
    jmp     [pat_B_ret_omega]

; =======================================================================
; match_success / match_fail
; =======================================================================
match_success:
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel subject]
    mov     rdx, [cursor]
    syscall

    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel newline]
    mov     rdx, 1
    syscall

    mov     eax, 60
    xor     edi, edi
    syscall

match_fail:
    mov     eax, 60
    mov     edi, 1
    syscall
