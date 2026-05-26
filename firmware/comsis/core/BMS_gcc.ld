/*
 * Linker script for STM32F103 converted from BMS.sct
 * FLASH: 0x08000000, 64KB (0x10000)
 * RAM:   0x20000000, 20KB (0x5000)
 */

/* 修复2：定义栈顶地址，指向 RAM 的最末端，提供给启动文件的 _estack 使用 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 20K
}

ENTRY(Reset_Handler)

SECTIONS
{
  /* 修复1：必须独立成段并放在最前面，且名称必须是 .isr_vector */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))    /* 与 startup_stm32f10x_md.s 中的段名严格对应 */
    . = ALIGN(4);
  } > FLASH

  /* RT-Thread init table (prevent --gc-sections from dropping entries) */
  .rti_fn :
  {
    . = ALIGN(4);
    KEEP(*(SORT(.rti_fn.*)))
    . = ALIGN(4);
  } > FLASH

  /* Finsh symbol table for GCC */
  .fsymtab :
  {
    . = ALIGN(4);
    __fsymtab_start = .;
    KEEP(*(FSymTab))
    KEEP(*(.fsymtab*))
    __fsymtab_end = .;
    . = ALIGN(4);
  } > FLASH

  /* 代码和只读数据 */
  .text :
  {
    . = ALIGN(4);
    *(.text)                     /* .text sections (code) */
    *(.text*)                    /* .text* sections (code) */
    *(.rodata)                   /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)                  /* .rodata* sections (constants, strings, etc.) */
    *(.glue_7)                   /* glue arm to thumb code */
    *(.glue_7t)                  /* glue thumb to arm code */
    *(.eh_frame)
    . = ALIGN(4);
  } > FLASH

  /* 修复3：增加 .init_array 段，这是 GCC 下 __libc_init_array 必需的运行环境支持 */
  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH
  
  /* 注意：_sidata 必须紧跟在存放在 FLASH 中的所有代码段之后 */
  _sidata = LOADADDR(.data);

  /* 初始化数据 (在 FLASH 中，需要复制到 RAM) */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;                  /* create a global symbol at data start */
    *(.data)                     /* .data sections */
    *(.data*)                    /* .data* sections */
    . = ALIGN(4);
    _edata = .;                  /* define a global symbol at data end */
  } > RAM AT > FLASH

  /* 未初始化数据 */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;                   /* define a global symbol at bss start */
    *(.bss)                      /* .bss sections */
    *(.bss*)                     /* .bss* sections */
    *(COMMON)                    /* COMMON sections */
    . = ALIGN(4);
    _ebss = .;                   /* define a global symbol at bss end */
  } > RAM

  /* 栈和堆定义 */
  . = ALIGN(8);
  end = .;
  _end = .;
  __end = .;
}