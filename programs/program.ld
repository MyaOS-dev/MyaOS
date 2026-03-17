ENTRY(program_main)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0;

  .text : ALIGN(16)
  {
    *(.text*)
    *(.rodata*)
  } :text

  .data : ALIGN(16)
  {
    *(.data*)
  } :data

  .bss (NOLOAD) : ALIGN(16)
  {
    *(.bss*)
    *(COMMON)
  } :data

  /DISCARD/ :
  {
    *(.comment*)
    *(.eh_frame*)
    *(.note*)
    *(.note.*)
    *(.interp)
    *(.gnu*)
  }
}
