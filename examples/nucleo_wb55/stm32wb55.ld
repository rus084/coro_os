ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_msp_size = 2048;
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .init_array : { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } > FLASH
  .data : { . = ALIGN(8); _sdata = .; *(.data*) . = ALIGN(8); _edata = .; } > RAM AT> FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > RAM
  .msp_stack _estack - _msp_size (NOLOAD) : { _msp_bottom = .; . += _msp_size; } > RAM
  ASSERT(_ebss <= _msp_bottom, "RAM objects overlap MSP reserve")
  /DISCARD/ : { *(.comment) *(.note*) }
}
