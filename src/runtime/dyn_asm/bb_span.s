; bb_span.s  _XSPNC      longest prefix of chars in set (≥1)
; spec_t  bb_span(void *zeta, int entry)
;   rdi = zeta (span_t*)   esi = entry
; span_t: { const char *chars @0; int δ @8 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω
extern strchr

section .text
global bb_span

bb_span:
        push    rbx
        push    r12
        push    r13
        mov     rbx, rdi                ; rbx = ζ (span_t*)
        cmp     esi, 0
        je      SPAN_α
        jmp     SPAN_β
SPAN_α: ; ζ->δ = 0
        mov     dword [rbx+8], 0
        ; while (Δ+ζ->δ < Ω && strchr(ζ->chars, Σ[Δ+ζ->δ])) ζ->δ++
        mov     r12, qword [rel Σ]      ; r12 = Σ
        mov     r13, qword [rbx+0]      ; r13 = ζ->chars
.span_loop:
        mov     eax, dword [rel Δ]
        add     eax, dword [rbx+8]      ; Δ+ζ->δ
        cmp     eax, dword [rel Ω]
        jge     .span_done
        movsxd  rcx, eax
        movzx   esi, byte [r12+rcx]     ; Σ[Δ+ζ->δ]
        mov     rdi, r13
        call    strchr
        test    rax, rax
        jz      .span_done
        add     dword [rbx+8], 1        ; ζ->δ++
        jmp     .span_loop
.span_done:
        ; if (ζ->δ <= 0) goto ω
        cmp     dword [rbx+8], 0
        jle     SPAN_ω
        ; SPAN = spec(Σ+Δ, ζ->δ);  Δ += ζ->δ
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        mov     edx, dword [rbx+8]      ; δ = ζ->δ
        add     dword [rel Δ], edx      ; Δ += ζ->δ
        jmp     SPAN_γ
SPAN_β: mov     ecx, dword [rbx+8]      ; ζ->δ
        sub     dword [rel Δ], ecx      ; Δ -= ζ->δ
        jmp     SPAN_ω
SPAN_γ: pop     r13
        pop     r12
        pop     rbx
        ret
SPAN_ω: xor     eax, eax
        xor     edx, edx
        pop     r13
        pop     r12
        pop     rbx
        ret
