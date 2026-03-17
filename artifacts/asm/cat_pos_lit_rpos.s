; cat_pos_lit_rpos.s — Sprint A3 artifact
; Pattern: POS(0) "hello" RPOS(0)   Subject: "hello"
; Expected: "hello\n" on stdout, exit 0
;
; CAT wiring (mirrors cat_pos_lit_rpos.c oracle exactly):
;   pos1_γ  → lit1_α
;   lit1_γ  → rpos1_α
;   rpos1_γ → assign1_α    ($ OUTPUT — print matched span)
;   rpos1_ω → lit1_β
;   lit1_ω  → pos1_β
;
; .bss layout:
;   cursor          — current cursor (int64)
;   lit1_saved      — cursor saved before lit advance (int64)
;   assign1_start   — cursor value when assign entered (int64)
;
; assemble:  nasm -f elf64 cat_pos_lit_rpos.s -o cat_pos_lit_rpos.o
; link:      ld cat_pos_lit_rpos.o -o cat_pos_lit_rpos
; run:       ./cat_pos_lit_rpos && echo PASS || echo FAIL
;
; M-ASM-SEQ fires when this produces "hello\n" and exit 0.

    global _start

section .data

subject:    db "hello"
subj_len:   equ 5
lit_str:    db "hello"
lit_len:    equ 5
newline:    db 10

section .bss

cursor:         resq 1
lit1_saved:     resq 1
assign1_start:  resq 1

section .text

_start:
    mov     qword [cursor], 0
    jmp     pos1_alpha

; -----------------------------------------------------------------------
; POS(0)
; -----------------------------------------------------------------------
pos1_alpha:
    cmp     qword [cursor], 0
    jne     pos1_omega
    jmp     pos1_gamma

pos1_beta:
pos1_omega:
    jmp     match_fail         ; nothing can backtrack into POS at start

pos1_gamma:
    jmp     lit1_alpha         ; CAT: pos_γ → lit_α

; -----------------------------------------------------------------------
; LIT("hello")
; -----------------------------------------------------------------------
lit1_alpha:
    mov     rax, [cursor]
    add     rax, lit_len
    cmp     rax, subj_len
    jg      lit1_omega

    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx
    lea     rdi, [rel lit_str]
    mov     rcx, lit_len
    repe    cmpsb
    jne     lit1_omega

    mov     rax, [cursor]
    mov     [lit1_saved], rax
    add     rax, lit_len
    mov     [cursor], rax
    jmp     lit1_gamma

lit1_beta:
    mov     rax, [lit1_saved]
    mov     [cursor], rax
    jmp     lit1_omega

lit1_gamma:
    jmp     rpos1_alpha        ; CAT: lit_γ → rpos_α

lit1_omega:
    jmp     pos1_beta          ; CAT: lit_ω → pos_β

; -----------------------------------------------------------------------
; RPOS(0)
; -----------------------------------------------------------------------
rpos1_alpha:
    mov     rax, [cursor]
    cmp     rax, subj_len      ; subj_len - 0
    jne     rpos1_omega
    jmp     rpos1_gamma

rpos1_beta:
rpos1_omega:
    jmp     lit1_beta          ; CAT: rpos_ω → lit_β

rpos1_gamma:
    jmp     assign1_alpha      ; CAT: rpos_γ → assign_α

; -----------------------------------------------------------------------
; ASSIGN ($ OUTPUT) — print subject[assign1_start .. cursor)
; -----------------------------------------------------------------------
assign1_alpha:
    mov     qword [assign1_start], 0   ; span starts at cursor before pos (=0)
    ; fall through — child already matched, cursor is at end

assign1_do_assign:
    ; sys_write(1, subject + assign1_start, cursor - assign1_start)
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel subject]
    mov     rcx, [assign1_start]
    add     rsi, rcx
    mov     rdx, [cursor]
    mov     rcx, [assign1_start]
    sub     rdx, rcx           ; length = cursor - start
    syscall

    ; newline
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel newline]
    mov     rdx, 1
    syscall

    jmp     match_success

; -----------------------------------------------------------------------
match_success:
    mov     eax, 60
    xor     edi, edi
    syscall

match_fail:
    mov     eax, 60
    mov     edi, 1
    syscall
