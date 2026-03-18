; lit_hello.s — Sprint A1 artifact
; LIT node: inline byte compare against "hello"
; Subject: "hello"  Pattern: LIT("hello")
; Expected: match succeeds → write "hello\n" to stdout → exit 0
;           match fails    → exit 1
;
; Byrd Box α/β/γ/ω are real labels.
; All variables live flat in .bss — no structs, no malloc.
;
; assemble:  nasm -f elf64 lit_hello.s -o lit_hello.o
; link:      ld lit_hello.o -o lit_hello
; run:       ./lit_hello && echo PASS || echo FAIL
;
; M-ASM-LIT fires when this matches and produces "hello\n" on stdout.

    global _start

section .data

subject:    db "hello"
subj_len:   equ 5
lit_str:    db "hello"
lit_len:    equ 5
newline:    db 10              ; '\n'

section .bss

cursor:         resq 1         ; int64  current cursor position
saved_cursor:   resq 1         ; int64  saved on α before advance (restored on β)

section .text

_start:
    ; --- initialise match state ---
    mov     qword [cursor], 0  ; cursor = 0

    ; --- jump to α ---
    jmp     lit1_alpha

; -----------------------------------------------------------------------
; α — bounds check, compare, advance
; -----------------------------------------------------------------------
lit1_alpha:
    mov     rax, [cursor]      ; rax = cursor
    add     rax, lit_len       ; rax = cursor + lit_len
    cmp     rax, subj_len      ; cursor + lit_len > subj_len?
    jg      lit1_omega         ; yes → fail

    ; compare subject[cursor .. cursor+5) with lit_str
    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx           ; rsi → subject + cursor
    lea     rdi, [rel lit_str] ; rdi → "hello"
    mov     rcx, lit_len
    repe    cmpsb              ; compare byte-by-byte
    jne     lit1_omega         ; mismatch → fail

    ; save cursor, advance
    mov     rax, [cursor]
    mov     [saved_cursor], rax
    add     rax, lit_len
    mov     [cursor], rax

    jmp     lit1_gamma

; -----------------------------------------------------------------------
; β — restore cursor, fall to ω  (backtrack entry)
; -----------------------------------------------------------------------
lit1_beta:
    mov     rax, [saved_cursor]
    mov     [cursor], rax
    jmp     lit1_omega

; -----------------------------------------------------------------------
; γ — match succeeded: write matched substring to stdout, exit 0
; -----------------------------------------------------------------------
lit1_gamma:
    ; sys_write(1, subject + saved_cursor, lit_len)
    mov     rax, 1             ; sys_write
    mov     rdi, 1             ; fd = stdout
    lea     rsi, [rel subject]
    mov     rcx, [saved_cursor]
    add     rsi, rcx           ; subject + saved_cursor
    mov     rdx, lit_len       ; length
    syscall

    ; write newline
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel newline]
    mov     rdx, 1
    syscall

    ; exit 0
    mov     eax, 60
    xor     edi, edi
    syscall

; -----------------------------------------------------------------------
; ω — match failed: exit 1
; -----------------------------------------------------------------------
lit1_omega:
    mov     eax, 60
    mov     edi, 1
    syscall
