ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS {
  . = KERNEL_VMA;
  __kernel_start = .;

  .text ALIGN(4K) : {
    *(.text .text.*)
  }

  .rodata ALIGN(4K) : {
    *(.rodata .rodata.*)
    . = ALIGN(8);
    __start_kernel_syms = .;
    KEEP(*(kernel_syms))
    __stop_kernel_syms = .;
  }

  .data ALIGN(4K) : {
    *(.data .data.*)
  }

  .bss ALIGN(4K) : {
    *(COMMON)
    *(.bss .bss.*)
  }

  __kernel_end = .;

  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
  }
}
