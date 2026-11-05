/*
 * H420X clean-room application linker script.
 *
 * The stock bootloader occupies 0x08000000..0x08003FFF and jumps to
 * 0x08004000. The DFU image is exactly 32,000 bytes, so this script must
 * never place a loadable byte at or above 0x0800BD00.
 *
 * SRAM: the stock vector table starts the app stack at 0x20000EF0. Using the
 * same top keeps the image valid on both 4 KiB and 8 KiB parts and leaves any
 * bootloader scratch area above 0x20000EF0 untouched.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
ENTRY(Reset_Handler)

MEMORY
{
    APP_FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 0x7D00
    SRAM (rwx)     : ORIGIN = 0x20000000, LENGTH = 0x0EF0
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);
_stack_reserve = 0x400;

SECTIONS
{
    .isr_vector ORIGIN(APP_FLASH) : ALIGN(4)
    {
        KEEP(*(.isr_vector))
    } > APP_FLASH

    .text : ALIGN(4)
    {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > APP_FLASH

    .ARM.exidx : ALIGN(4)
    {
        *(.ARM.exidx*)
    } > APP_FLASH

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> APP_FLASH
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    .stack_reservation (NOLOAD) : ALIGN(8)
    {
        _stack_bottom = .;
        . += _stack_reserve;
        _stack_top = .;
    } > SRAM

    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.attributes*) }
}

ASSERT(ADDR(.isr_vector) == 0x08004000,
       "app vectors must start at the bootloader jump address")
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= 0x0800BD00,
       "app image crosses the 32,000-byte DFU boundary")
ASSERT(_stack_top <= _estack,
       "static data and stack reservation exceed the usable SRAM window")
ASSERT(_estack == 0x20000EF0,
       "unexpected H420X SRAM top")
