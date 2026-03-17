; assign_digits.s — Sprint A7 artifact
; Pattern: SPAN("0123456789") $ NUM   Subject: "abc123def"
; Expected: "123\n" on stdout, exit 0
;
; Unanchored match: try the pattern starting at each subject position
; until it succeeds or we exhaust the subject.
;
; SPAN(S) semantics (v311.sil SPNC):
;   Match one or more chars, each in set S.
;   Advances cursor by count of leading S-chars at cursor.
;   If zero chars match: fail.
;   SPAN is greedy and non-backtracking for the span itself:
;   on β it simply fails (no partial retry).
;
; $ (immediate assignment) Byrd Box — same as assign_lit.s:
;   dol_α: save entry_cursor → child_α (SPAN)
;   child_γ → dol_γ: compute span[entry_cursor..cursor] → cap_buf/cap_len; → γ
;   child_ω → dol_ω → advance outer cursor, retry (unanchored loop)
;
; Outer unanchored loop:
;   outer_start: outer_cursor = 0
;   loop:        set cursor = outer_cursor; try pattern
;                if dol_γ → match_success
;                if dol_ω → outer_cursor++; if outer_cursor >= subj_len → match_fail
;
; .bss layout:
;   cursor          — inner match cursor
;   outer_cursor    — position at which current attempt started
;   entry_cursor    — cursor when dol_α entered (span start for $)
;   span_start      — cursor at SPAN α entry (= entry_cursor in this test)
;   cap_buf         — capture buffer
;   cap_len         — capture length
;
; assemble:  nasm -f elf64 assign_digits.s -o assign_digits.o
; link:      ld assign_digits.o -o assign_digits
; run:       ./assign_digits && echo PASS || echo FAIL

    global _start

section .data

subject:    db "abc123def"
subj_len:   equ 9
digits:     db "0123456789"
digits_len: equ 10
newline:    db 10

section .bss

cursor:         resq 1
outer_cursor:   resq 1
entry_cursor:   resq 1
span_count:     resq 1      ; number of chars matched by SPAN
cap_len:        resq 1
cap_buf:        resb 256

section .text

_start:
    mov     qword [outer_cursor], 0

; -----------------------------------------------------------------------
; Outer unanchored loop
; -----------------------------------------------------------------------
outer_loop:
    mov     rax, [outer_cursor]
    cmp     rax, subj_len
    jge     match_fail          ; exhausted all positions

    mov     [cursor], rax       ; set inner cursor = outer position
    jmp     dol_alpha           ; try pattern at this position

outer_advance:
    mov     rax, [outer_cursor]
    inc     rax
    mov     [outer_cursor], rax
    jmp     outer_loop

; -----------------------------------------------------------------------
; DOL α — save entry cursor, enter SPAN
; -----------------------------------------------------------------------
dol_alpha:
    mov     rax, [cursor]
    mov     [entry_cursor], rax
    jmp     span_alpha

dol_beta:
    ; backtrack into span — SPAN has no β (it's greedy-only), so fail
    jmp     dol_omega

; -----------------------------------------------------------------------
; SPAN("0123456789") α — count leading digit chars at cursor
; -----------------------------------------------------------------------
span_alpha:
    ; count how many consecutive chars at cursor are in digits set
    mov     rax, [cursor]       ; rax = current cursor
    mov     r10, 0              ; r10 = count of matched chars

.span_loop:
    ; check bounds
    mov     rbx, rax
    add     rbx, r10
    cmp     rbx, subj_len
    jge     .span_done          ; hit end of subject

    ; load subject[cursor + count]
    lea     rcx, [rel subject]
    movzx   edx, byte [rcx + rbx]   ; edx = subject char

    ; scan digits set for this char
    lea     rsi, [rel digits]
    mov     r9, digits_len
    xor     r8, r8
.digit_scan:
    cmp     r8, r9
    jge     .not_digit
    movzx   edi, byte [rsi + r8]
    cmp     edx, edi
    je      .is_digit
    inc     r8
    jmp     .digit_scan
.is_digit:
    inc     r10
    jmp     .span_loop
.not_digit:
    ; this char not in set → stop
.span_done:
    ; if count == 0: SPAN fails (needs at least one)
    cmp     r10, 0
    je      span_omega

    ; advance cursor by count
    mov     rax, [cursor]
    add     rax, r10
    mov     [cursor], rax
    mov     [span_count], r10
    jmp     dol_gamma           ; span_γ → dol_γ

span_omega:
    jmp     dol_omega           ; span failed

span_beta:
    ; SPAN has no meaningful β — greedy, can't retreat partially
    jmp     dol_omega

; -----------------------------------------------------------------------
; DOL γ — span succeeded: capture subject[entry_cursor..cursor]
; -----------------------------------------------------------------------
dol_gamma:
    mov     rax, [cursor]
    mov     rbx, [entry_cursor]
    sub     rax, rbx            ; rax = span length
    mov     [cap_len], rax

    lea     rsi, [rel subject]
    add     rsi, rbx            ; rsi = &subject[entry_cursor]
    lea     rdi, [rel cap_buf]
    mov     rcx, rax
    rep     movsb               ; copy span → cap_buf

    jmp     match_success       ; dol_γ = outer_γ here (no RPOS)

; DOL ω — child failed: try next position
dol_omega:
    jmp     outer_advance

; -----------------------------------------------------------------------
; match_success — print cap_buf, exit 0
; -----------------------------------------------------------------------
match_success:
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel cap_buf]
    mov     rdx, [cap_len]
    syscall

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
