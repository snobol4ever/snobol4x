; arbno_alt.s — Sprint A5 artifact
; Pattern: POS(0) ARBNO(LIT("a") | LIT("b")) . V RPOS(0)   Subject: "abba"
; Expected: "abba\n" on stdout, exit 0  (matches all 4 chars)
;
; ARBNO(ALT(LIT("a"), LIT("b"))):
;   Each repetition tries "a" first, then "b" on failure.
;   ARBNO backtracks by trying one more ALT rep.
;   Backtrack within a rep: ALT tries its right arm.
;   Since each LIT consumes exactly one char, no zero-advance guard fires.
;
; Wiring:
;   _start → pos_α → assign_α → arbno_α
;   arbno_α: push cursor; arbno_γ → rpos_α
;   rpos_ω → arbno_β
;   arbno_β: pop cursor; run alt_α for one rep
;            alt_α → lit1_α; lit1_ω → lit2_α; lit2_ω → arbno_ω
;            lit1_γ or lit2_γ → rep_success
;   rep_success: push new cursor; arbno_γ (re-succeed)
;   rpos_γ → match_success (print span)
;
; assemble:  nasm -f elf64 arbno_alt.s -o arbno_alt.o
; link:      ld arbno_alt.o -o arbno_alt
; run:       ./arbno_alt && echo PASS || echo FAIL

    global _start

section .data

subject:    db "abba"
subj_len:   equ 4
lit1_str:   db "a"
lit1_len:   equ 1
lit2_str:   db "b"
lit2_len:   equ 1
newline:    db 10

section .bss

cursor:         resq 1
assign_start:   resq 1
cursor_at_alt:  resq 1          ; saved by ALT α within each rep
lit1_saved:     resq 1
lit2_saved:     resq 1
arbno_depth:    resq 1
arbno_stack:    resq 64

section .text

_start:
    mov     qword [cursor], 0
    mov     qword [arbno_depth], 0
    jmp     pos_alpha

; -----------------------------------------------------------------------
; POS(0)
; -----------------------------------------------------------------------
pos_alpha:
    cmp     qword [cursor], 0
    jne     pos_omega
    jmp     pos_gamma
pos_beta:
pos_omega:
    jmp     match_fail
pos_gamma:
    jmp     assign_alpha

; -----------------------------------------------------------------------
; Save span start
; -----------------------------------------------------------------------
assign_alpha:
    mov     rax, [cursor]
    mov     [assign_start], rax
    jmp     arbno_alpha

; -----------------------------------------------------------------------
; ARBNO(ALT(LIT("a"), LIT("b")))
; -----------------------------------------------------------------------
arbno_alpha:
    ; push cursor; succeed immediately (zero reps)
    mov     rax, [arbno_depth]
    lea     rbx, [rel arbno_stack]
    mov     rcx, [cursor]
    mov     [rbx + rax*8], rcx
    inc     rax
    mov     [arbno_depth], rax
    jmp     arbno_gamma

arbno_beta:
    ; pop saved cursor; try one more ALT rep
    mov     rax, [arbno_depth]
    test    rax, rax
    jz      arbno_omega
    dec     rax
    mov     [arbno_depth], rax
    lea     rbx, [rel arbno_stack]
    mov     rcx, [rbx + rax*8]
    mov     [cursor], rcx
    ; save cursor_before_rep for progress guard
    mov     [cursor_at_alt], rcx
    jmp     alt_alpha           ; run one ALT rep

arbno_omega:
    jmp     match_fail

arbno_gamma:
    jmp     rpos_alpha

; -----------------------------------------------------------------------
; ALT(LIT("a"), LIT("b")) — one repetition inside ARBNO
; This is entered from arbno_beta to try one more rep.
; On success → rep_success. On failure → arbno_omega.
; -----------------------------------------------------------------------
alt_alpha:
    ; save cursor at alt entry (for left_ω restore)
    mov     rax, [cursor]
    mov     [cursor_at_alt], rax
    jmp     lit1_alpha

; LIT("a")
lit1_alpha:
    mov     rax, [cursor]
    add     rax, lit1_len
    cmp     rax, subj_len
    jg      lit1_omega

    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx
    lea     rdi, [rel lit1_str]
    mov     rcx, lit1_len
    repe    cmpsb
    jne     lit1_omega

    mov     rax, [cursor]
    mov     [lit1_saved], rax
    add     rax, lit1_len
    mov     [cursor], rax
    jmp     lit1_gamma

lit1_beta:
    mov     rax, [lit1_saved]
    mov     [cursor], rax
lit1_omega:
    ; left arm failed → restore cursor, try right arm
    mov     rax, [cursor_at_alt]
    mov     [cursor], rax
    jmp     lit2_alpha

lit1_gamma:
    jmp     rep_success

; LIT("b")
lit2_alpha:
    mov     rax, [cursor]
    add     rax, lit2_len
    cmp     rax, subj_len
    jg      lit2_omega

    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx
    lea     rdi, [rel lit2_str]
    mov     rcx, lit2_len
    repe    cmpsb
    jne     lit2_omega

    mov     rax, [cursor]
    mov     [lit2_saved], rax
    add     rax, lit2_len
    mov     [cursor], rax
    jmp     lit2_gamma

lit2_beta:
    mov     rax, [lit2_saved]
    mov     [cursor], rax
lit2_omega:
    jmp     arbno_omega         ; both arms failed → ARBNO fails

lit2_gamma:
    jmp     rep_success

; -----------------------------------------------------------------------
; rep_success: one ALT rep succeeded, cursor advanced
; Guard: cursor must have advanced
; Push new cursor, re-succeed (loop back to arbno_γ)
; -----------------------------------------------------------------------
rep_success:
    ; zero-advance guard
    mov     rax, [cursor]
    mov     rbx, [cursor_at_alt]
    cmp     rax, rbx
    je      arbno_omega         ; no progress → fail

    ; push new cursor
    mov     rax, [arbno_depth]
    lea     rbx, [rel arbno_stack]
    mov     rcx, [cursor]
    mov     [rbx + rax*8], rcx
    inc     rax
    mov     [arbno_depth], rax
    jmp     arbno_gamma         ; re-succeed

; -----------------------------------------------------------------------
; RPOS(0)
; -----------------------------------------------------------------------
rpos_alpha:
    mov     rax, [cursor]
    cmp     rax, subj_len
    jne     rpos_omega
    jmp     rpos_gamma
rpos_beta:
rpos_omega:
    jmp     arbno_beta          ; backtrack into ARBNO
rpos_gamma:
    jmp     match_success

; -----------------------------------------------------------------------
match_success:
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel subject]
    mov     rcx, [assign_start]
    add     rsi, rcx
    mov     rdx, [cursor]
    mov     rcx, [assign_start]
    sub     rdx, rcx
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
