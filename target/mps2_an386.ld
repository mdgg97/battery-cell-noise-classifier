/* Linker script for QEMU mps2-an386 (Cortex-M4): 4 MB code SRAM at 0x0, 4 MB data SRAM at 0x20000000 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

_stack_size = 0x4000; /* 16 KB */

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* heap for newlib (_sbrk) between end of .bss and the stack */
    end = .;
    __end__ = .;

    _estack = ORIGIN(RAM) + LENGTH(RAM);
    __HeapLimit = _estack - _stack_size;
    __StackLimit = __HeapLimit;
}
