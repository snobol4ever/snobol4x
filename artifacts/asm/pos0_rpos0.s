; pos0_rpos0.s — Sprint A2 artifact
; Pattern: POS(0) RPOS(0)   Subject: ""  (empty string)
; Expected: match succeeds (cursor==0 == subject_len==0) → exit 0
;           (no output — pattern only checks positions)
;
; Byrd Box layout:
;   pos1  α/β/γ/ω
;   rpos1 α/β/γ/ω
;   CAT wiring: pos1_γ → rpos1_α
;               rpos1_ω → pos1_β
;
; POS(n):  α: cursor==n? γ : ω    β=ω (no save needed — zero-width)
; RPOS(n): α: cursor==subj_len-n? γ : ω    β=ω
;
; All variables: flat .bss — cursor only (no saves needed for zero-width nodes)
;
; assemble:  nasm -f elf64 pos0_rpos0.s -o pos0_rpos0.o
; link:      ld pos0_rpos0.o -o pos0_rpos0
; run:       ./pos0_rpos0 && echo PASS || echo FAIL
;
; M-ASM-SEQ (partial): POS/RPOS nodes confirmed correct.

    global _start

section .data

subj_len:   equ 0              ; empty subject

section .bss

cursor:     resq 1

section .text

_start:
    mov     qword [cursor], 0
    jmp     pos1_alpha

; -----------------------------------------------------------------------
; POS(0) — α: succeed iff cursor == 0
; -----------------------------------------------------------------------
pos1_alpha:
    cmp     qword [cursor], 0
    jne     pos1_omega
    jmp     pos1_gamma

pos1_beta:                     ; = pos1_omega for zero-width node
pos1_omega:
    jmp     match_fail

pos1_gamma:
    jmp     rpos1_alpha        ; CAT: pos_γ → rpos_α

; -----------------------------------------------------------------------
; RPOS(0) — α: succeed iff cursor == subject_len - 0
; -----------------------------------------------------------------------
rpos1_alpha:
    mov     rax, [cursor]
    cmp     rax, subj_len      ; subj_len - 0 = 0
    jne     rpos1_omega
    jmp     rpos1_gamma

rpos1_beta:
rpos1_omega:
    jmp     pos1_beta          ; CAT: rpos_ω → pos_β

rpos1_gamma:
    jmp     match_success      ; whole pattern matched

; -----------------------------------------------------------------------
match_success:
    mov     eax, 60
    xor     edi, edi
    syscall

match_fail:
    mov     eax, 60
    mov     edi, 1
    syscall
