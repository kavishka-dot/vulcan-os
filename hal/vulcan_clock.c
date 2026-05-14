/*
 * vulcan_clock.c — PLL1 configuration for STM32H743 @ 480 MHz
 * Vulcan OS
 */

#include "vulcan_clock.h"

uint32_t SystemCoreClock = 480000000UL;

/* DWT (Data Watchpoint and Trace) for cycle-accurate delay */
#define DWT_BASE    0xE0001000UL
#define DWT_CTRL    (*(volatile uint32_t *)(DWT_BASE + 0x00))
#define DWT_CYCCNT  (*(volatile uint32_t *)(DWT_BASE + 0x04))
#define CoreDebug_DEMCR (*(volatile uint32_t *)0xE000EDFC)

void vk_clock_init(void) {
    /* ── 1. Enable power control clock ─────────────────────────── */
    /* RCC_APB4ENR bit 1 = SYSCFGEN, bit 0 = not needed here
     * PWR is on APB4 */
    *(volatile uint32_t *)(0x58024400UL + 0xF4) |= (1u << 1); /* SYSCFGEN */
    /* PWR clock via RCC_APB4ENR bit 1 already set; enable PWR */
    *(volatile uint32_t *)(0x58024400UL + 0xF4) |= (1u << 0); /* PWREN */

    /* ── 2. Set VOS0 (scale 0) for 480 MHz operation ───────────── */
    /* PWR_D3CR: VOS = 0b11 (scale 1 = highest performance) */
    PWR_D3CR |= (3u << 14);
    while (!(PWR_D3CR & (1u << 13)));  /* wait VOSRDY */

    /* SYSCFG ODEN — overdrive required for VOS0 on H743 */
    *(volatile uint32_t *)(0x58000400UL + 0x04) |= (1u << 0);  /* ODEN */
    /* Wait ~1 µs (busy loop) */
    for (volatile int i = 0; i < 1000; i++);

    /* ── 3. Enable HSE (25 MHz external crystal) ────────────────── */
    RCC_CR |= (1u << 16);              /* HSEON */
    while (!(RCC_CR & (1u << 17)));    /* wait HSERDY */

    /* ── 4. Configure flash latency for 480 MHz (7 wait states) ── */
    FLASH_ACR = (7u << 0)   |          /* LATENCY = 7 */
                (1u << 8)   |          /* WRHIGHFREQ = 0b10 */
                (2u << 4);

    /* ── 5. Configure PLL1 ──────────────────────────────────────── */
    /* Source = HSE, DIVM1 = 5 → PLL input = 5 MHz */
    RCC_PLLCKSELR = (2u << 0) |        /* PLLSRC = HSE */
                    (5u << 4);         /* DIVM1 = 5 */

    /* PLL1 input range: 4–8 MHz → PLL1RGE = 0b10 */
    /* VCO wide range (960 MHz) → PLL1VCOSEL = 0 */
    RCC_PLLCFGR = (2u << 2) |          /* PLL1RGE = 10 (4–8 MHz) */
                  (0u << 1) |          /* PLL1VCOSEL = wide (192–960 MHz) */
                  (1u << 16) |         /* DIVP1EN */
                  (1u << 17) |         /* DIVQ1EN */
                  (1u << 18);          /* DIVR1EN */

    /* DIVN1=192, DIVP1=2, DIVQ1=4, DIVR1=2
     * VCO = 5 × 192 = 960 MHz
     * SYSCLK = 960/2 = 480 MHz */
    RCC_PLL1DIVR = ((192u - 1u) << 0) |  /* DIVN1 */
                   ((2u   - 1u) << 9) |  /* DIVP1 */
                   ((4u   - 1u) << 16)|  /* DIVQ1 */
                   ((2u   - 1u) << 24);  /* DIVR1 */

    RCC_PLL1FRACR = 0;  /* no fractional division */

    /* Enable PLL1 */
    RCC_CR |= (1u << 24);              /* PLL1ON */
    while (!(RCC_CR & (1u << 25)));    /* wait PLL1RDY */

    /* ── 6. Configure bus prescalers ───────────────────────────── */
    /* D1: HPRE=/2 (AHB=240 MHz), D1PPRE=/2 (APB3=120 MHz) */
    RCC_D1CFGR = (8u << 0) |           /* HPRE = /2 */
                 (4u << 4);            /* D1PPRE = /2 */

    /* D2: APB1=/2 (120 MHz), APB2=/2 (120 MHz) */
    RCC_D2CFGR = (4u << 4) |           /* D2PPRE1 = /2 */
                 (4u << 8);            /* D2PPRE2 = /2 */

    /* D3: APB4=/2 (120 MHz) */
    RCC_D3CFGR = (4u << 4);            /* D3PPRE = /2 */

    /* ── 7. Switch system clock to PLL1P ───────────────────────── */
    RCC_CFGR = (3u << 0);              /* SW = PLL1 */
    while (((RCC_CFGR >> 3) & 7u) != 3u); /* wait SWS = PLL1 */

    SystemCoreClock = 480000000UL;

    /* ── 8. Enable DWT cycle counter for vk_delay_us ───────────── */
    CoreDebug_DEMCR |= (1u << 24);     /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL   |= (1u << 0);           /* CYCCNTENA */
}

void vk_delay_us(uint32_t us) {
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000UL);
    while ((DWT_CYCCNT - start) < ticks);
}

void vk_delay_ms(uint32_t ms) {
    while (ms--) vk_delay_us(1000);
}
