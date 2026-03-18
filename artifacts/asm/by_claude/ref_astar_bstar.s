; ref_astar_bstar.s — Sprint A8 artifact
; Pattern: ASTAR BSTAR   where ASTAR = ARBNO(LIT("a")), BSTAR = ARBNO(LIT("b"))
; Subject: "aaabb"
; Expected: "aaabb\n" on stdout, exit 0
;
; Named pattern calling convention (Proebsting §4.5 indirect goto / gate):
;
;   Each named pattern has TWO entry labels:
;       pat_NAME_alpha:   initial match attempt
;       pat_NAME_beta:    backtrack / resume
;
;   The CALL SITE, before jumping to a named pattern, stores the
;   continuation labels (γ and ω) into two flat .bss qwords:
;       pat_NAME_ret_gamma  dq  address_of_caller_gamma
;       pat_NAME_ret_omega  dq  address_of_caller_omega
;   Then jumps to pat_NAME_alpha (or pat_NAME_beta for backtrack).
;
;   The named pattern body ends with:
;       jmp [pat_NAME_ret_gamma]   ; on success
;       jmp [pat_NAME_ret_omega]   ; on failure
;
;   This mirrors Proebsting §4.5 "gate" indirect goto and v311.sil RCALL/BRANCH SCOK.
;   No call stack needed — pure flat labels + indirect jumps.
;
; Layout for ASTAR BSTAR (SEQ of two named pattern refs):
;
;   _start → ASTAR call site (ref0):
;     ref0 stores ret_gamma=ref0_gamma, ret_omega=ref0_omega
;     jmp pat_ASTAR_alpha
;   ref0_gamma → BSTAR call site (ref1):
;     ref1 stores ret_gamma=match_success, ret_omega=ref1_omega
;     jmp pat_BSTAR_alpha
;   ref0_omega → match_fail
;   ref1_omega → pat_ASTAR_beta (backtrack into ASTAR)
;     re-stores ret_gamma=ref0_gamma, ret_omega=ref0_omega [already set]
;     jmp pat_ASTAR_beta
;
; .bss layout:
;   cursor                  — match cursor
;   pat_ASTAR_ret_gamma     — ASTAR success continuation
;   pat_ASTAR_ret_omega     — ASTAR failure continuation
;   pat_BSTAR_ret_gamma     — BSTAR success continuation
;   pat_BSTAR_ret_omega     — BSTAR failure continuation
;   astar_arb_stack[64]     — ARBNO cursor stack for ASTAR
;   astar_arb_depth         — stack depth for ASTAR
;   astar_lit_saved         — LIT("a") cursor save
;   astar_arb_cur_before    — zero-advance guard cursor
;   bstar_arb_stack[64]     — ARBNO cursor stack for BSTAR
;   bstar_arb_depth         — stack depth for BSTAR
;   bstar_lit_saved         — LIT("b") cursor save
;   bstar_arb_cur_before    — zero-advance guard cursor
;
; assemble:  nasm -f elf64 ref_astar_bstar.s -o ref_astar_bstar.o
; link:      ld ref_astar_bstar.o -o ref_astar_bstar
; run:       ./ref_astar_bstar && echo PASS || echo FAIL
;
; M-ASM-NAMED fires when ref_astar_bstar + anbn both PASS.

    global _start

section .data

subject:    db "aaabb"
subj_len:   equ 5
lit_a:      db "a"
lit_b:      db "b"
newline:    db 10

section .bss

cursor:                 resq 1

; ASTAR calling convention slots
pat_ASTAR_ret_gamma:    resq 1
pat_ASTAR_ret_omega:    resq 1

; BSTAR calling convention slots
pat_BSTAR_ret_gamma:    resq 1
pat_BSTAR_ret_omega:    resq 1

; ASTAR internals (ARBNO(LIT("a")))
astar_arb_stack:        resq 64
astar_arb_depth:        resq 1
astar_lit_saved:        resq 1
astar_arb_cur_before:   resq 1

; BSTAR internals (ARBNO(LIT("b")))
bstar_arb_stack:        resq 64
bstar_arb_depth:        resq 1
bstar_lit_saved:        resq 1
bstar_arb_cur_before:   resq 1

; span capture for output
match_start:            resq 1

section .text

; =======================================================================
; _start — top-level match: ASTAR . BSTAR anchored (POS(0) implicit, RPOS(0) explicit)
; =======================================================================
_start:
    mov     qword [cursor], 0
    mov     qword [match_start], 0

    ; --- Call ASTAR (ref0) ---
    lea     rax, [rel ref0_gamma]
    mov     [pat_ASTAR_ret_gamma], rax
    lea     rax, [rel ref0_omega]
    mov     [pat_ASTAR_ret_omega], rax
    jmp     pat_ASTAR_alpha

ref0_gamma:
    ; ASTAR succeeded — now call BSTAR (ref1)
    lea     rax, [rel ref1_gamma]
    mov     [pat_BSTAR_ret_gamma], rax
    lea     rax, [rel ref1_omega]
    mov     [pat_BSTAR_ret_omega], rax
    jmp     pat_BSTAR_alpha

ref1_gamma:
    ; BSTAR succeeded — check RPOS(0): cursor must be at end
    cmp     qword [cursor], subj_len
    je      match_success
    ; cursor not at end — backtrack into BSTAR
    lea     rax, [rel ref1_gamma]
    mov     [pat_BSTAR_ret_gamma], rax
    lea     rax, [rel ref1_omega]
    mov     [pat_BSTAR_ret_omega], rax
    jmp     pat_BSTAR_beta

ref1_omega:
    ; BSTAR exhausted — backtrack into ASTAR
    lea     rax, [rel ref0_gamma]
    mov     [pat_ASTAR_ret_gamma], rax
    lea     rax, [rel ref0_omega]
    mov     [pat_ASTAR_ret_omega], rax
    jmp     pat_ASTAR_beta

ref0_omega:
    jmp     match_fail

; =======================================================================
; Named pattern: ASTAR = ARBNO(LIT("a"))
;   pat_ASTAR_alpha — initial entry
;   pat_ASTAR_beta  — backtrack entry
; =======================================================================

pat_ASTAR_alpha:
    ; ARBNO α: init depth=0, push cursor, succeed immediately
    mov     qword [astar_arb_depth], 0
    mov     rax, [cursor]
    lea     rbx, [rel astar_arb_stack]
    mov     [rbx], rax
    mov     qword [astar_arb_depth], 1
    jmp     [pat_ASTAR_ret_gamma]       ; succeed (zero reps)

pat_ASTAR_beta:
    ; ARBNO β: pop cursor, save cur_before, try one more LIT("a")
    mov     rax, [astar_arb_depth]
    test    rax, rax
    je      astar_arbno_omega           ; stack empty → fail
    dec     rax
    mov     [astar_arb_depth], rax
    lea     rbx, [rel astar_arb_stack]
    mov     rcx, [rbx + rax*8]
    mov     [cursor], rcx
    mov     [astar_arb_cur_before], rcx
    jmp     astar_lit_alpha

astar_lit_alpha:
    ; LIT("a") α
    mov     rax, [cursor]
    add     rax, 1
    cmp     rax, subj_len
    jg      astar_lit_omega
    lea     rbx, [rel subject]
    mov     rcx, [cursor]
    movzx   eax, byte [rbx + rcx]
    cmp     al, 'a'
    jne     astar_lit_omega
    mov     rax, [cursor]
    mov     [astar_lit_saved], rax
    inc     rax
    mov     [cursor], rax
    jmp     astar_child_ok

astar_lit_omega:
    jmp     astar_arbno_omega

astar_lit_beta:
    mov     rax, [astar_lit_saved]
    mov     [cursor], rax
    jmp     astar_arbno_omega

astar_child_ok:
    ; zero-advance guard
    mov     rax, [cursor]
    mov     rbx, [astar_arb_cur_before]
    cmp     rax, rbx
    je      astar_arbno_omega
    ; push new cursor, re-succeed
    mov     rdx, [astar_arb_depth]
    lea     rbx, [rel astar_arb_stack]
    mov     [rbx + rdx*8], rax
    inc     qword [astar_arb_depth]
    jmp     [pat_ASTAR_ret_gamma]

astar_arbno_omega:
    jmp     [pat_ASTAR_ret_omega]

; =======================================================================
; Named pattern: BSTAR = ARBNO(LIT("b"))
;   pat_BSTAR_alpha — initial entry
;   pat_BSTAR_beta  — backtrack entry
; =======================================================================

pat_BSTAR_alpha:
    ; ARBNO α: init depth=0, push cursor, succeed immediately
    mov     qword [bstar_arb_depth], 0
    mov     rax, [cursor]
    lea     rbx, [rel bstar_arb_stack]
    mov     [rbx], rax
    mov     qword [bstar_arb_depth], 1
    jmp     [pat_BSTAR_ret_gamma]

pat_BSTAR_beta:
    ; ARBNO β
    mov     rax, [bstar_arb_depth]
    test    rax, rax
    je      bstar_arbno_omega
    dec     rax
    mov     [bstar_arb_depth], rax
    lea     rbx, [rel bstar_arb_stack]
    mov     rcx, [rbx + rax*8]
    mov     [cursor], rcx
    mov     [bstar_arb_cur_before], rcx
    jmp     bstar_lit_alpha

bstar_lit_alpha:
    ; LIT("b") α
    mov     rax, [cursor]
    add     rax, 1
    cmp     rax, subj_len
    jg      bstar_lit_omega
    lea     rbx, [rel subject]
    mov     rcx, [cursor]
    movzx   eax, byte [rbx + rcx]
    cmp     al, 'b'
    jne     bstar_lit_omega
    mov     rax, [cursor]
    mov     [bstar_lit_saved], rax
    inc     rax
    mov     [cursor], rax
    jmp     bstar_child_ok

bstar_lit_omega:
    jmp     bstar_arbno_omega

bstar_lit_beta:
    mov     rax, [bstar_lit_saved]
    mov     [cursor], rax
    jmp     bstar_arbno_omega

bstar_child_ok:
    ; zero-advance guard
    mov     rax, [cursor]
    mov     rbx, [bstar_arb_cur_before]
    cmp     rax, rbx
    je      bstar_arbno_omega
    mov     rdx, [bstar_arb_depth]
    lea     rbx, [rel bstar_arb_stack]
    mov     [rbx + rdx*8], rax
    inc     qword [bstar_arb_depth]
    jmp     [pat_BSTAR_ret_gamma]

bstar_arbno_omega:
    jmp     [pat_BSTAR_ret_omega]

; =======================================================================
; match_success — print subject[0..cursor), exit 0
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
