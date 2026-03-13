ENTRY(_start)
SECTIONS
{
    . = 0x2000000;
    _kernel_start = .;
    .text : ALIGN(4K) {
        *(.text*)
    }
    .rodata : ALIGN(4K) {
        *(.rodata*)
        *(.lrodata*)
    }
    .data : ALIGN(4K) {
        *(.data*)
        *(.sdata*)
        *(.ldata*)
    }
    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss*)
        *(.sbss*)
        *(.lbss*)
    }
    _kernel_end = .;
}
