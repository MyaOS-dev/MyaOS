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
        . = ALIGN(8);
        __myaos_kmods_start = .;
        KEEP(*(.myaos_kmods))
        __myaos_kmods_end = .;
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
