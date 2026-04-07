; bb_atp.s   _XATP       @var — write cursor Δ as DT_I into varname; no backtrack
; spec_t  bb_atp(void *zeta, int entry)
;   rdi = zeta (atp_t*)    esi = entry
; atp_t: { int done @0; [pad4]; const char *varname @8 }
; DESCR_t: { DTYPE_t v @0 (int); uint32_t slen @4; union{...} @8 }  — DT_I=6
; NV_SET_fn(const char *name, DESCR_t v)  — v passed as struct (rsi:rdx = v[0..7]:v[8..15])

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ
extern NV_SET_fn

section .text
global bb_atp

bb_atp:
        push    rbx
        push    r12
        sub     rsp, 24                 ; space for DESCR_t (16 bytes) + align
        mov     rbx, rdi                ; rbx = ζ (atp_t*)
        cmp     esi, 0
        je      ATP_α
        jmp     ATP_β
ATP_α:  mov     dword [rbx+0], 1        ; ζ->done = 1
        ; if (ζ->varname && ζ->varname[0])
        mov     r12, qword [rbx+8]      ; ζ->varname
        test    r12, r12
        jz      .atp_no_set
        cmp     byte [r12], 0
        je      .atp_no_set
        ; DESCR_t v = { .v=DT_I=6, .slen=0, .i=(int64_t)Δ }
        ; layout: dword v @0, dword slen @4, qword i @8
        mov     dword [rsp+0], 6        ; v = DT_I
        mov     dword [rsp+4], 0        ; slen = 0
        movsxd  rax, dword [rel Δ]
        mov     qword [rsp+8], rax      ; i = Δ
        ; NV_SET_fn(ζ->varname, v)
        ; SysV: rdi=name, rsi=v[0..7], rdx=v[8..15]
        mov     rdi, r12
        mov     rsi, qword [rsp+0]      ; v low qword (v+slen)
        mov     rdx, qword [rsp+8]      ; v high qword (i)
        call    NV_SET_fn
.atp_no_set:
        ; ATP = spec(Σ+Δ, 0)
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx
        xor     edx, edx
        jmp     ATP_γ
ATP_β:                                  jmp     ATP_ω
ATP_γ:  add     rsp, 24
        pop     r12
        pop     rbx
        ret
ATP_ω:  xor     eax, eax
        xor     edx, edx
        add     rsp, 24
        pop     r12
        pop     rbx
        ret
