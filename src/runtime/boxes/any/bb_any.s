; bb_any.s   _XANYC      match one char if in set
; spec_t  bb_any(void *zeta, int entry)
;   rdi = zeta (any_t*)    esi = entry
; any_t: { const char *chars @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω
extern strchr

section .text
global bb_any

bb_any:
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (any_t*)
        cmp     esi, 0
        je      ANY_α
        jmp     ANY_β
ANY_α:  ; if (Δ>=Ω || !strchr(ζ->chars, Σ[Δ])) goto ω
        mov     eax, dword [rel Δ]
        cmp     eax, dword [rel Ω]
        jge     ANY_ω
        ; load Σ[Δ]
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        movzx   esi, byte [r12+rcx]     ; Σ[Δ]
        mov     rdi, qword [rbx+0]      ; ζ->chars
        call    strchr
        test    rax, rax
        jz      ANY_ω
        ; ANY = spec(Σ+Δ, 1);  Δ++
        mov     rax, r12
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, 1                  ; δ = 1
        add     dword [rel Δ], 1        ; Δ++
        jmp     ANY_γ
ANY_β:  sub     dword [rel Δ], 1        ; Δ--
        jmp     ANY_ω
ANY_γ:  pop     r12
        pop     rbx
        ret
ANY_ω:  xor     eax, eax
        xor     edx, edx
        pop     r12
        pop     rbx
        ret
