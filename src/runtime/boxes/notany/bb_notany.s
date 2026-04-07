; bb_notany.s  _XNNYC    match one char if NOT in set
; spec_t  bb_notany(void *zeta, int entry)
;   rdi = zeta (notany_t*) esi = entry
; notany_t: { const char *chars @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω
extern strchr

section .text
global bb_notany

bb_notany:
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (notany_t*)
        cmp     esi, 0
        je      NOTANY_α
        jmp     NOTANY_β
NOTANY_α:
        ; if (Δ>=Ω || strchr(ζ->chars, Σ[Δ])) goto ω
        mov     eax, dword [rel Δ]
        cmp     eax, dword [rel Ω]
        jge     NOTANY_ω
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        movzx   esi, byte [r12+rcx]     ; Σ[Δ]
        mov     rdi, qword [rbx+0]      ; ζ->chars
        call    strchr
        test    rax, rax
        jnz     NOTANY_ω                ; found → NOT in set fails
        ; NOTANY = spec(Σ+Δ, 1);  Δ++
        mov     rax, r12
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, 1                  ; δ = 1
        add     dword [rel Δ], 1        ; Δ++
        jmp     NOTANY_γ
NOTANY_β:
        sub     dword [rel Δ], 1        ; Δ--
        jmp     NOTANY_ω
NOTANY_γ:
        pop     r12
        pop     rbx
        ret
NOTANY_ω:
        xor     eax, eax
        xor     edx, edx
        pop     r12
        pop     rbx
        ret
