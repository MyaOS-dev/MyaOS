BITS 64
GLOBAL _start
EXTERN kernel_main
SECTION .text
_start:
    cli
    cld
    mov rsp, stack_top
    and rsp, -16
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
SECTION .bss
align 16
stack_bottom:
    resb 65536
stack_top: