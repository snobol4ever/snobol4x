; assign_lit.s — Sprint A7 artifact
; Pattern: POS(0) LIT("hello") $ CAP RPOS(0)   Subject: "hello"
; Expected: "hello\n" on stdout, exit 0
;
; $ (immediate assignment) Byrd Box semantics (v311.sil ENMI):
;
;   The $ node wraps a sub-pattern.  Its four ports are:
;
;   dol_α:    save cursor (entry_cursor) → child_α
;   dol_β:    → child_β                 (backtrack into child)
;   child_γ → dol_γ: pop entry_cursor;
;                    span = subject[entry_cursor .. cursor]
;                    store span into capture buffer + set capture_len
;                    → outer_γ          (succeed, assignment done)
;   child_ω → dol_ω: assignment NOT performed → outer_ω
;
;   Key: $ does NOT undo on backtrack — it is "immediate" (fires once
;   when child succeeds; no rollback on later failure).
;
; Wiring for POS(0) . (LIT("hello") $ CAP) . RPOS(0):
;   _start        → pos_alpha
;   pos_γ         → dol_alpha
;   dol_α:          save cursor → entry_cursor; jmp lit_alpha
;   lit_γ  → dol_γ: pop entry_cursor;
;                    copy subject[entry_cursor..cursor] → cap_buf, cap_len
;                    jmp rpos_alpha
;   lit_ω  → dol_ω → match_fail
;   rpos_γ        → match_success (print cap_buf, exit 0)
;   rpos_ω        → lit_beta → dol_ω → match_fail
;   pos_ω         → match_fail
;
; .bss layout:
;   cursor          — current match position
;   lit_saved       — cursor saved before lit advance (for lit β)
;   entry_cursor    — cursor when dol_α entered (span start)
;   cap_buf         — capture buffer (max 256 bytes)
;   cap_len         — length of captured span
;
; assemble:  nasm -f elf64 assign_lit.s -o assign_lit.o
; link:      ld assign_lit.o -o assign_lit
; run:       ./assign_lit && echo PASS || echo FAIL
;
; M-ASM-ASSIGN fires when assign_lit + assign_digits both PASS.

    global _start

section .data

subject:    db "hello"
subj_len:   equ 5
lit_str:    db "hello"
lit_len:    equ 5
newline:    db 10

section .bss

cursor:         resq 1
lit_saved:      resq 1
entry_cursor:   resq 1
cap_len:        resq 1
cap_buf:        resb 256

section .text

_start:
    mov     qword [cursor], 0
    jmp     pos_alpha

; -----------------------------------------------------------------------
; POS(0) — cursor must be 0 to succeed
; -----------------------------------------------------------------------
pos_alpha:
    cmp     qword [cursor], 0
    jne     match_fail
    jmp     dol_alpha           ; pos_γ → dol_α

pos_beta:
    jmp     match_fail          ; pos can't retry

; -----------------------------------------------------------------------
; DOL α — save entry cursor, enter child (LIT)
; -----------------------------------------------------------------------
dol_alpha:
    mov     rax, [cursor]
    mov     [entry_cursor], rax ; push entry cursor
    jmp     lit_alpha           ; → child α

; DOL β — backtrack into child
dol_beta:
    jmp     lit_beta

; -----------------------------------------------------------------------
; LIT("hello") α — bounds check, compare, advance cursor
; -----------------------------------------------------------------------
lit_alpha:
    ; bounds: cursor + 5 <= subj_len
    mov     rax, [cursor]
    add     rax, lit_len
    cmp     rax, subj_len
    jg      lit_omega           ; too short → ω

    ; compare subject[cursor..cursor+5] vs "hello"
    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx            ; rsi = &subject[cursor]
    lea     rdi, [rel lit_str]
    mov     rcx, lit_len
    repe    cmpsb
    jne     lit_omega           ; mismatch → ω

    ; save cursor, advance
    mov     rax, [cursor]
    mov     [lit_saved], rax
    add     rax, lit_len
    mov     [cursor], rax
    jmp     dol_gamma           ; lit_γ → dol_γ

; LIT ω — failed before advancing (bounds/mismatch)
lit_omega:
    jmp     dol_omega

; LIT β — restore cursor, fail
lit_beta:
    mov     rax, [lit_saved]
    mov     [cursor], rax
    jmp     dol_omega

; -----------------------------------------------------------------------
; DOL γ — child succeeded: compute span, store into cap_buf
; -----------------------------------------------------------------------
dol_gamma:
    ; span = subject[entry_cursor .. cursor]
    mov     rax, [cursor]
    mov     rbx, [entry_cursor]
    sub     rax, rbx            ; rax = span length
    mov     [cap_len], rax

    ; memcpy: subject[entry_cursor .. cursor) → cap_buf
    lea     rsi, [rel subject]
    add     rsi, rbx            ; rsi = &subject[entry_cursor]
    lea     rdi, [rel cap_buf]
    mov     rcx, rax
    rep     movsb

    jmp     rpos_alpha          ; dol_γ → outer_γ (next node: RPOS(0))

; DOL ω — child failed, no assignment
dol_omega:
    jmp     match_fail

; -----------------------------------------------------------------------
; RPOS(0) — cursor must be at end of subject
; -----------------------------------------------------------------------
rpos_alpha:
    cmp     qword [cursor], subj_len
    jne     rpos_omega
    jmp     match_success

rpos_omega:
    jmp     lit_beta            ; backtrack into lit_β → dol_ω → fail

; -----------------------------------------------------------------------
; match_success — print cap_buf, exit 0
; -----------------------------------------------------------------------
match_success:
    ; write cap_buf[0..cap_len)
    mov     rax, 1              ; sys_write
    mov     rdi, 1              ; stdout
    lea     rsi, [rel cap_buf]
    mov     rdx, [cap_len]
    syscall

    ; write newline
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel newline]
    mov     rdx, 1
    syscall

    mov     eax, 60
    xor     edi, edi
    syscall

; -----------------------------------------------------------------------
; match_fail — exit 1
; -----------------------------------------------------------------------
match_fail:
    mov     eax, 60
    mov     edi, 1
    syscall
