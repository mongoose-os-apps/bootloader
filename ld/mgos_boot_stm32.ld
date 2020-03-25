/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 */

ENTRY(stm32_entry)

_stack = SRAM_BASE_ADDR + SRAM_SIZE;
_stack_size = 0x4000;

MEMORY {
  FLASH_BL (rx)  : ORIGIN = FLASH_BASE_ADDR, LENGTH = FLASH_BL_SIZE - 8 /* for init flag */
  SRAM     (rwx) : ORIGIN = SRAM_BASE_ADDR,  LENGTH = SRAM_SIZE
}

/* Define output sections */
SECTIONS {
  .int_vectors : {
    KEEP(*(.flash_int_vectors_boot))
  } > FLASH_BL

  /* Small area at the top of RAM for communicating between loader and app. */
  .boot_state : {
    *(.boot_state)
  } > SRAM

  /* Note: Needs to precede .text section to ensure correct placement of IRAM sections. */
  .data : {
    _data_start = .;
    . = ALIGN(4);
    *(.RamFunc)
    *(SORT(.text.IRAM.*))
    *(SORT(.rodata.IRAM.*))
    *(.data SORT(.data.*))
    _data_end = .;
  } > SRAM AT > FLASH_BL
  _data_flash_start = LOADADDR(.data);

  .text : {
    . = ALIGN(4);
    *(.text SORT(.text.*))
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
  } > FLASH_BL

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH_BL
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH_BL

  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } > FLASH_BL
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } > FLASH_BL
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } > FLASH_BL

  .rodata : {
    *(.rodata SORT(.rodata.*))
  } > FLASH_BL

  .bss : {
    _bss_start = .;
    . = ALIGN(256);  /* Vectors must be aligned to 0x100 */
    KEEP(*(.bss.ram_int_vectors))
    *(.bss SORT(.bss.*))
    *(COMMON)
    _bss_end = .;
  } > SRAM

  .heap : {
    . = ALIGN(8);
    _heap_start = .;
    . = . + (LENGTH(SRAM) - SIZEOF(.boot_state) - SIZEOF(.data) - SIZEOF(.bss) - _stack_size);
    . = ALIGN(8);
    _heap_end = .;
  } > SRAM

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
