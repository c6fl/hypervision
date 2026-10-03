;
; Thanks to vmcall & vmc for their assistance with this sublime asm
;

EXTERN  KeBugCheckEx:PROC
EXTERN  hv_launch_finalize:PROC            ; NTSTATUS (vmx_context*)
EXTERN  hv_handle_exit:PROC                ; unsigned long long (vmx_context*)

PUBLIC  asm_vm_launch
PUBLIC  asm_vm_guest_resume
PUBLIC  asm_vm_exit_handler
PUBLIC  asm_xsetbv
PUBLIC  asm_invept
PUBLIC  asm_capture_segments
PUBLIC  asm_get_tr
PUBLIC  asm_get_gdtr
PUBLIC  asm_get_idtr

CTX_RAX             equ 000h
CTX_RCX             equ 008h
CTX_RDX             equ 010h
CTX_RBX             equ 018h
CTX_RSP             equ 020h
CTX_RBP             equ 028h
CTX_RSI             equ 030h
CTX_RDI             equ 038h
CTX_R8              equ 040h
CTX_R9              equ 048h
CTX_R10             equ 050h
CTX_R11             equ 058h
CTX_R12             equ 060h
CTX_R13             equ 068h
CTX_R14             equ 070h
CTX_R15             equ 078h
CTX_GUEST_RIP       equ 080h
CTX_GUEST_RSP       equ 088h
CTX_GUEST_RFLAGS    equ 090h
CTX_GUEST_GS_BASE   equ 098h
CTX_GUEST_CS        equ 0A0h
CTX_GUEST_SS        equ 0A8h
CTX_FATAL_REASON    equ 0B0h
CTX_FATAL_QUAL      equ 0B8h

MSR_IA32_GS_BASE    equ 0C0000101h
_TEXT   SEGMENT

; void asm_vm_launch(vmx_context* ctx)

ALIGN   16
asm_vm_launch PROC
    ; we save every guest gpr, in this case rcx is saved into its own
    mov     [rcx + CTX_RAX], rax
    mov     [rcx + CTX_RCX], rcx
    mov     [rcx + CTX_RDX], rdx
    mov     [rcx + CTX_RBX], rbx
    mov     [rcx + CTX_RBP], rbp
    mov     [rcx + CTX_RSI], rsi
    mov     [rcx + CTX_RDI], rdi
    mov     [rcx + CTX_R8],  r8
    mov     [rcx + CTX_R9],  r9
    mov     [rcx + CTX_R10], r10
    mov     [rcx + CTX_R11], r11
    mov     [rcx + CTX_R12], r12
    mov     [rcx + CTX_R13], r13
    mov     [rcx + CTX_R14], r14
    mov     [rcx + CTX_R15], r15

    ; guest rsp is the current rsp so guest resumes with a plain ret after regs are restored
    mov     [rcx + CTX_GUEST_RSP], rsp

    pushfq
    pop     rax
    mov     [rcx + CTX_GUEST_RFLAGS], rax

    ; vmcs setup
    sub     rsp, 28h
    call    hv_launch_finalize               ; rcx still holds ctx rn
    add     rsp, 28h
    test    eax, eax
    jnz     lv_fail

    vmlaunch
    jc      lv_fail
    jz      lv_fail
    ud2

lv_fail:
    mov     eax, 0C0000001h                  ; STATUS_UNSUCCESSFUL
    ret
asm_vm_launch ENDP

ALIGN   16
asm_vm_guest_resume:
    mov     rcx, gs:[0]
    mov     r15, [rcx + CTX_R15]
    mov     r14, [rcx + CTX_R14]
    mov     r13, [rcx + CTX_R13]
    mov     r12, [rcx + CTX_R12]
    mov     r11, [rcx + CTX_R11]
    mov     r10, [rcx + CTX_R10]
    mov     r9,  [rcx + CTX_R9]
    mov     r8,  [rcx + CTX_R8]
    mov     rdi, [rcx + CTX_RDI]
    mov     rsi, [rcx + CTX_RSI]
    mov     rbp, [rcx + CTX_RBP]
    mov     rbx, [rcx + CTX_RBX]
    mov     rdx, [rcx + CTX_RDX]
    mov     rax, [rcx + CTX_RAX]
    mov     rcx, [rcx + CTX_RCX]
    ret

asm_vm_exit_handler PROC
    mov     rcx, gs:[0]                      ; ctx
    mov     [rcx + CTX_RAX], rax
    mov     [rcx + CTX_RCX], rcx
    mov     [rcx + CTX_RDX], rdx
    mov     [rcx + CTX_RBX], rbx
    mov     [rcx + CTX_RBP], rbp
    mov     [rcx + CTX_RSI], rsi
    mov     [rcx + CTX_RDI], rdi
    mov     [rcx + CTX_R8],  r8
    mov     [rcx + CTX_R9],  r9
    mov     [rcx + CTX_R10], r10
    mov     [rcx + CTX_R11], r11
    mov     [rcx + CTX_R12], r12
    mov     [rcx + CTX_R13], r13
    mov     [rcx + CTX_R14], r14
    mov     [rcx + CTX_R15], r15

    sub     rsp, 28h
    call    hv_handle_exit                   ; rax: 0=resume 1=vmxoff done 2=fatal
    add     rsp, 28h

    cmp     rax, 1
    je      ev_vmxoff_path
    cmp     rax, 2
    je      ev_fatal_path                 ; lol

    ; --- resume the guest ---
    mov     rcx, gs:[0]
    mov     r15, [rcx + CTX_R15]
    mov     r14, [rcx + CTX_R14]
    mov     r13, [rcx + CTX_R13]
    mov     r12, [rcx + CTX_R12]
    mov     r11, [rcx + CTX_R11]
    mov     r10, [rcx + CTX_R10]
    mov     r9,  [rcx + CTX_R9]
    mov     r8,  [rcx + CTX_R8]
    mov     rdi, [rcx + CTX_RDI]
    mov     rsi, [rcx + CTX_RSI]
    mov     rbp, [rcx + CTX_RBP]
    mov     rbx, [rcx + CTX_RBX]
    mov     rdx, [rcx + CTX_RDX]
    mov     rax, [rcx + CTX_RAX]
    mov     rcx, [rcx + CTX_RCX]
    vmresume
    jc      ev_fail
    jz      ev_fail
    ud2

ev_fail:
    jmp     ev_fatal_path 


ev_vmxoff_path:
    mov     rbp, gs:[0]                      ; ctx cuz host gs base still active

    mov     rax, [rbp + CTX_GUEST_GS_BASE]
    mov     rdx, rax
    shr     rdx, 32
    mov     ecx, MSR_IA32_GS_BASE
    wrmsr

    mov     rdx, [rbp + CTX_GUEST_RSP]
    sub     rdx, 8*5
    mov     rsp, rdx
    mov     rax, [rbp + CTX_GUEST_RIP]
    mov     [rsp + 0*8], rax
    mov     rax, [rbp + CTX_GUEST_CS]
    mov     [rsp + 1*8], rax
    mov     rax, [rbp + CTX_GUEST_RFLAGS]
    mov     [rsp + 2*8], rax
    mov     rax, [rbp + CTX_GUEST_RSP]
    mov     [rsp + 3*8], rax
    mov     rax, [rbp + CTX_GUEST_SS]
    mov     [rsp + 4*8], rax

    mov     r15, [rbp + CTX_R15]
    mov     r14, [rbp + CTX_R14]
    mov     r13, [rbp + CTX_R13]
    mov     r12, [rbp + CTX_R12]
    mov     r11, [rbp + CTX_R11]
    mov     r10, [rbp + CTX_R10]
    mov     r9,  [rbp + CTX_R9]
    mov     r8,  [rbp + CTX_R8]
    mov     rdi, [rbp + CTX_RDI]
    mov     rsi, [rbp + CTX_RSI]
    mov     rbx, [rbp + CTX_RBX]
    mov     rdx, [rbp + CTX_RDX]
    mov     rax, [rbp + CTX_RAX]
    mov     rcx, [rbp + CTX_RCX]
    mov     rbp, [rbp + CTX_RBP]
    iretq

ev_fatal_path:
    mov     rbp, gs:[0]
    vmxoff
    mov     rax, [rbp + CTX_GUEST_GS_BASE]
    mov     rdx, rax
    shr     rdx, 32
    mov     ecx, MSR_IA32_GS_BASE
    wrmsr

    mov     ecx, 53465648h                   ; 'HVFS'
    mov     edx, dword ptr [rbp + CTX_FATAL_REASON]
    mov     r8d,  dword ptr [rbp + CTX_FATAL_QUAL]
    xor     r9d, r9d
    sub     rsp, 28h
    call    KeBugCheckEx
    ud2

asm_vm_exit_handler ENDP
asm_xsetbv PROC
    mov     eax, edx
    shr     rdx, 32
    xsetbv
    ret
asm_xsetbv ENDP

asm_invept PROC
    invept  rcx, xmmword ptr [rdx]
    xor     eax, eax
    jnc     @f
    jmp     inv_fail
@@:
    jnz     @f
    jmp     inv_fail
@@:
    ret
inv_fail:
    mov     eax, 1
    ret
asm_invept ENDP

asm_capture_segments PROC
    mov     ax, cs
    mov     dword ptr [rcx + 0*4], eax
    mov     ax, ds
    mov     dword ptr [rcx + 1*4], eax
    mov     ax, es
    mov     dword ptr [rcx + 2*4], eax
    mov     ax, ss
    mov     dword ptr [rcx + 3*4], eax
    mov     ax, fs
    mov     dword ptr [rcx + 4*4], eax
    mov     ax, gs
    mov     dword ptr [rcx + 5*4], eax
    ret
asm_capture_segments ENDP

; unsigned short asm_get_tr()
asm_get_tr PROC
    str     ax
    movzx   eax, ax
    ret
asm_get_tr ENDP

; void asm_get_gdtr(void* out /*rcx*/) — 10-byte sgdt image
asm_get_gdtr PROC
    sgdt    fword ptr [rcx]
    ret
asm_get_gdtr ENDP

; void asm_get_idtr(void* out /*rcx*/) — 10-byte sidt image
asm_get_idtr PROC
    sidt    fword ptr [rcx]
    ret
asm_get_idtr ENDP

_TEXT   ENDS
END
