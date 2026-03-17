BITS 64

GLOBAL isr_stub_table
EXTERN interrupt_dispatch

SECTION .text

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1
    jmp isr_common_stub
%endmacro

%macro GEN_ISR 1
%if %1 = 8 || %1 = 10 || %1 = 11 || %1 = 12 || %1 = 13 || %1 = 14 || %1 = 17 || %1 = 21 || %1 = 29 || %1 = 30
    ISR_ERR %1
%else
    ISR_NOERR %1
%endif
%endmacro

%assign i 0
%rep 256
    GEN_ISR i
%assign i i + 1
%endrep

isr_common_stub:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, [rsp + 120] 
    mov rsi, [rsp + 128]  
    mov rdx, rsp         
    cld
    sub rsp, 8
    call interrupt_dispatch
    add rsp, 8
    mov rsp, rax

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16
    iretq

SECTION .data
align 8
isr_stub_table:
%assign j 0
%rep 256
    dq isr_stub_%+j
%assign j j + 1
%endrep
