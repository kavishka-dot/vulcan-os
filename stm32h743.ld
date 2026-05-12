/*
 * stm32h743.ld — linker script for STM32H743ZI
 *
 * Memory map (RM0433 Table 4):
 *   ITCM  :  64 KB @ 0x00000000  (instruction tightly coupled, 0-wait)
 *   DTCM  : 128 KB @ 0x20000000  (data tightly coupled, 0-wait — tensor arena)
 *   SRAM1 : 128 KB @ 0x24000000  (AXI SRAM — DMA accessible)
 *   SRAM2 :  32 KB @ 0x30000000  }
 *   SRAM3 :  32 KB @ 0x30020000  } AHB SRAM (slower, DMA accessible)
 *   SRAM4 :  64 KB @ 0x38000000  }
 *   Flash :   2 MB @ 0x08000000  (Bank 1 + Bank 2)
 *
 * Vulcan placement:
 *   .text   → Flash
 *   .data   → SRAM1 (initialised from Flash)
 *   .bss    → SRAM1
 *   .dtcm   → DTCM  (tensor arena — zero-wait from CPU)
 *   stacks  → DTCM  (task stacks also benefit from 0-wait)
 */

ENTRY(Reset_Handler)

MEMORY {
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    DTCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    SRAM1  (rwx) : ORIGIN = 0x24000000, LENGTH = 128K
    SRAM2  (rwx) : ORIGIN = 0x30000000, LENGTH = 32K
}

SECTIONS {

    /* ── vector table + code ─────────────────────────────────────── */
    .isr_vector : {
        KEEP(*(.isr_vector))
    } >FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* ── initialised data (copied from Flash to SRAM1 at startup) ── */
    .data : AT(_etext) {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >SRAM1

    /* ── zero-initialised data ──────────────────────────────────── */
    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >SRAM1

    /* ── tensor arena in DTCM (zero-wait CPU access) ───────────── */
    .dtcmram (NOLOAD) : {
        . = ALIGN(8);
        *(.dtcmram)
        . = ALIGN(8);
        _edtcm = .;
    } >DTCM

    /* ── kernel stack (grows down from top of DTCM) ─────────────── */
    .stack (NOLOAD) : {
        . = ALIGN(8);
        _sstack = .;
        . = . + 4096;   /* 4 KB initial MSP stack for startup + ISRs */
        . = ALIGN(8);
        _estack = .;
    } >DTCM

    /* ── discard ARM exception table junk ───────────────────────── */
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

/* Provide symbols for startup code */
PROVIDE(_sstack  = ORIGIN(DTCM) + LENGTH(DTCM));
PROVIDE(end      = _ebss);
