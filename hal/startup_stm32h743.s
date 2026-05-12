/*
 * startup_stm32h743.s — vector table and reset handler
 * Vulcan OS for STM32H743 (Cortex-M7)
 *
 * Responsibilities:
 *   1. Define vector table (first 16 entries + IRQs 0..7 for now)
 *   2. Copy .data from Flash to SRAM
 *   3. Zero .bss
 *   4. Call SystemInit() (sets PLL, SystemCoreClock)
 *   5. Call main()
 *   6. Infinite loop if main returns (should never happen)
 */

    .syntax unified
    .cpu cortex-m7
    .thumb

/* ── external symbols ────────────────────────────────────────────── */

    .extern _sstack          /* top of MSP stack (from linker script) */
    .extern _sdata, _edata   /* .data section boundaries in SRAM */
    .extern _etext           /* end of .text in Flash (LMA of .data) */
    .extern _sbss,  _ebss    /* .bss section boundaries */
    .extern SystemInit
    .extern main

/* ── weak default handlers ───────────────────────────────────────── */

    .macro WEAK_DEFAULT name
    .weak \name
    .thumb_set \name, Default_Handler
    .endm

/* ── vector table ────────────────────────────────────────────────── */

    .section .isr_vector, "a", %progbits
    .type   g_pfnVectors, %object

g_pfnVectors:
    /* ARM Cortex-M core exceptions */
    .word   _sstack                     /* 0: initial MSP */
    .word   Reset_Handler               /* 1: reset */
    .word   NMI_Handler                 /* 2: NMI */
    .word   HardFault_Handler           /* 3: hard fault */
    .word   MemManage_Handler           /* 4: MPU fault */
    .word   BusFault_Handler            /* 5: bus fault */
    .word   UsageFault_Handler          /* 6: usage fault */
    .word   0                           /* 7: reserved */
    .word   0                           /* 8: reserved */
    .word   0                           /* 9: reserved */
    .word   0                           /* 10: reserved */
    .word   SVC_Handler                 /* 11: SVCall */
    .word   DebugMon_Handler            /* 12: debug monitor */
    .word   0                           /* 13: reserved */
    .word   PendSV_Handler              /* 14: PendSV  ← context switch */
    .word   SysTick_Handler             /* 15: SysTick ← tick counter */

    /* STM32H743 peripheral IRQs (0..7 shown; extend as needed) */
    .word   WWDG_IRQHandler             /* 0 */
    .word   PVD_AVD_IRQHandler          /* 1 */
    .word   TAMP_STAMP_IRQHandler       /* 2 */
    .word   RTC_WKUP_IRQHandler         /* 3 */
    .word   FLASH_IRQHandler            /* 4 */
    .word   RCC_IRQHandler              /* 5 */
    .word   EXTI0_IRQHandler            /* 6 */
    .word   EXTI1_IRQHandler            /* 7 */

/* ── reset handler ───────────────────────────────────────────────── */

    .section .text.Reset_Handler
    .type   Reset_Handler, %function
    .thumb_func
Reset_Handler:

    /* Copy .data from Flash (LMA) to SRAM (VMA) */
    ldr     r0, =_sdata
    ldr     r1, =_edata
    ldr     r2, =_etext
    b       2f
1:
    ldr     r3, [r2], #4
    str     r3, [r0], #4
2:
    cmp     r0, r1
    blo     1b

    /* Zero .bss */
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    mov     r2, #0
    b       4f
3:
    str     r2, [r0], #4
4:
    cmp     r0, r1
    blo     3b

    /* Enable FPU (CPACR bits CP10/CP11 = 0b11) */
    ldr     r0, =0xE000ED88
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb

    /* Lazy FPU context save/restore — set LSPEN in FPCCR */
    ldr     r0, =0xE000EF34
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 30)    /* LSPEN = 1 */
    str     r1, [r0]

    bl      SystemInit             /* configure clocks (480 MHz) */
    bl      main

    /* If main returns, loop forever */
5:  b       5b

    .size Reset_Handler, . - Reset_Handler

/* ── default / fault handlers ────────────────────────────────────── */

    .section .text.Default_Handler
    .type   Default_Handler, %function
    .thumb_func
Default_Handler:
HardFault_Handler:
    /*
     * Capture fault info from the stack frame.
     * R0..R3, R12, LR, PC, xPSR were pushed by hardware.
     * Spin here so a debugger can inspect registers.
     */
    tst     lr, #4             /* check EXC_RETURN bit 2: 0=MSP, 1=PSP */
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
1:  b       1b                 /* hang — attach debugger here */
    .size Default_Handler, . - Default_Handler

/* ── weak handler aliases ────────────────────────────────────────── */

    WEAK_DEFAULT NMI_Handler
    WEAK_DEFAULT MemManage_Handler
    WEAK_DEFAULT BusFault_Handler
    WEAK_DEFAULT UsageFault_Handler
    WEAK_DEFAULT SVC_Handler
    WEAK_DEFAULT DebugMon_Handler
    WEAK_DEFAULT WWDG_IRQHandler
    WEAK_DEFAULT PVD_AVD_IRQHandler
    WEAK_DEFAULT TAMP_STAMP_IRQHandler
    WEAK_DEFAULT RTC_WKUP_IRQHandler
    WEAK_DEFAULT FLASH_IRQHandler
    WEAK_DEFAULT RCC_IRQHandler
    WEAK_DEFAULT EXTI0_IRQHandler
    WEAK_DEFAULT EXTI1_IRQHandler

    /* PendSV and SysTick are NOT weak — defined in vulcan_sched.c */
