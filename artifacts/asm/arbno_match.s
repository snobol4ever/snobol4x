; arbno_match.s — Sprint A5 artifact
; Pattern: POS(0) ARBNO(LIT("a")) RPOS(0)   Subject: "aaa"
; Expected: "aaa\n" on stdout, exit 0
;
; ARBNO Byrd Box semantics (from v311.sil ARBN/EARB/ARBF):
;
;   ARBNO(P) matches zero or more non-overlapping repetitions of P.
;   It succeeds IMMEDIATELY (zero reps), pushing a backtrack entry.
;   On backtrack (β), it tries ONE more repetition of P.
;   If P advances cursor AND succeeds, push another backtrack entry and
;   re-succeed. If P fails or cursor didn't advance, ARBNO fails.
;
; Flat .bss cursor stack (max depth 64):
;   arbno_stack[0..63]  — saved cursor values at each ARBNO entry
;   arbno_depth         — current stack depth (int64)
;   cursor              — match cursor
;   pos_saved           — cursor at POS(0) entry (unused for backtrack here)
;   lit_saved           — cursor before lit advance
;   assign_start        — cursor at ARBNO α entry (span start for . V)
;
; Wiring (SEQ: POS(0) . ARBNO . RPOS(0)):
;   _start            → pos_alpha
;   pos_γ             → assign_alpha       (save span start)
;   assign_γ          → arbno_alpha
;   arbno_α:          push cursor; succeed (zero reps)
;   arbno_γ           → rpos_alpha
;   arbno_β:          pop cursor; try one more rep of LIT("a")
;                       if lit_γ → push new cursor; re-succeed (arbno_γ)
;                       if lit_ω → arbno_ω
;   rpos_γ            → match_success (print span, exit 0)
;   rpos_ω            → arbno_β           (backtrack into ARBNO)
;   pos_ω / arbno_ω   → match_fail
;
; assemble:  nasm -f elf64 arbno_match.s -o arbno_match.o
; link:      ld arbno_match.o -o arbno_match
; run:       ./arbno_match && echo PASS || echo FAIL

    global _start

section .data

subject:    db "aaa"
subj_len:   equ 3
lit_str:    db "a"
lit_len:    equ 1
newline:    db 10

section .bss

cursor:         resq 1
assign_start:   resq 1          ; cursor when ARBNO α entered (span start)
lit_saved:      resq 1          ; cursor before lit advance
arbno_depth:    resq 1          ; cursor stack depth
arbno_stack:    resq 64         ; cursor stack (one entry per ARBNO rep attempt)

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
    jmp     assign_alpha        ; CAT: pos_γ → assign_α (save span start)

; -----------------------------------------------------------------------
; ASSIGN start — record cursor as span start for . V
; -----------------------------------------------------------------------
assign_alpha:
    mov     rax, [cursor]
    mov     [assign_start], rax
    jmp     arbno_alpha         ; CAT: assign_γ → arbno_α

; -----------------------------------------------------------------------
; ARBNO(LIT("a"))
;
; α: push current cursor; succeed immediately (zero reps).
; β: pop saved cursor; try one more LIT rep.
;    if LIT succeeds AND cursor advanced: push new cursor, re-succeed.
;    if LIT fails or cursor stalled: omega.
; -----------------------------------------------------------------------
arbno_alpha:
    ; push cursor onto arbno_stack
    mov     rax, [arbno_depth]
    lea     rbx, [rel arbno_stack]
    mov     rcx, [cursor]
    mov     [rbx + rax*8], rcx
    inc     rax
    mov     [arbno_depth], rax
    jmp     arbno_gamma         ; succeed immediately (zero reps)

arbno_beta:
    ; pop saved cursor
    mov     rax, [arbno_depth]
    test    rax, rax
    jz      arbno_omega         ; stack empty → fail
    dec     rax
    mov     [arbno_depth], rax
    lea     rbx, [rel arbno_stack]
    mov     rcx, [rbx + rax*8]
    mov     [cursor], rcx       ; restore cursor to saved position

    ; try one more LIT("a") repetition
    ; save cursor-before-lit so lit_beta can restore
    mov     rax, [cursor]
    mov     [lit_saved], rax

    ; bounds check
    mov     rax, [cursor]
    add     rax, lit_len
    cmp     rax, subj_len
    jg      arbno_omega         ; no room → fail

    ; compare
    lea     rsi, [rel subject]
    mov     rcx, [cursor]
    add     rsi, rcx
    lea     rdi, [rel lit_str]
    mov     rcx, lit_len
    repe    cmpsb
    jne     arbno_omega         ; mismatch → fail

    ; lit succeeded: advance cursor
    mov     rax, [cursor]
    add     rax, lit_len
    mov     [cursor], rax

    ; guard: cursor must have advanced (no zero-length infinite loop)
    mov     rbx, [lit_saved]
    cmp     rax, rbx
    je      arbno_omega         ; cursor stalled → fail

    ; push new cursor, re-succeed
    mov     rax, [arbno_depth]
    lea     rbx, [rel arbno_stack]
    mov     rcx, [cursor]
    mov     [rbx + rax*8], rcx
    inc     rax
    mov     [arbno_depth], rax
    jmp     arbno_gamma         ; re-succeed with one more rep

arbno_omega:
    jmp     match_fail          ; ARBNO exhausted, propagate failure upward

arbno_gamma:
    jmp     rpos_alpha          ; CAT: arbno_γ → rpos_α

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
    jmp     arbno_beta          ; CAT: rpos_ω → arbno_β

rpos_gamma:
    jmp     match_success

; -----------------------------------------------------------------------
match_success:
    ; print subject[assign_start .. cursor)
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
