ENTRY(hook_run)
SECTIONS {
  . = 0;
  .text   : { *(.text*) }
  .rodata : { *(.rodata*) *(.rodata.*) }
  .data   : { *(.data*) *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.llvm*) }
}
