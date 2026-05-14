#ifndef VULCAN_CLOCK_H
#define VULCAN_CLOCK_H

/*
 * vulcan_clock.h — system clock configuration for STM32H743
 * Vulcan OS
 *
 * Target: 480 MHz CPU from 25 MHz HSE crystal
 *
 * PLL1 configuration (HSE=25 MHz):
 *   DIVM1 = 5   → PLL input = 25/5  = 5 MHz
 *   DIVN1 = 192 → VCO      = 5×192  = 960 MHz
 *   DIVP1 = 2   → SYSCLK   = 960/2  = 480 MHz
 *   DIVQ1 = 4   → PLL1Q    = 960/4  = 240 MHz (USB, SPI)
 *   DIVR1 = 2   → PLL1R    = 960/2  = 480 MHz (unused)
 *
 * Bus prescalers:
 *   AHB  /2 → 240 MHz (HCLK)
 *   APB1 /2 → 120 MHz
 *   APB2 /2 → 120 MHz
 *   APB3 /2 → 120 MHz
 *   APB4 /2 → 120 MHz
 */

#include "vulcan.h"

/* ─── RCC base ───────────────────────────────────────────────────── */
#define RCC_BASE        0x58024400UL
#define RCC             ((volatile uint32_t *)RCC_BASE)

/* RCC register offsets (word index) */
#define RCC_CR          (RCC[0x00/4])
#define RCC_CFGR        (RCC[0x10/4])
#define RCC_D1CFGR      (RCC[0x18/4])
#define RCC_D2CFGR      (RCC[0x1C/4])
#define RCC_D3CFGR      (RCC[0x20/4])
#define RCC_PLLCKSELR   (RCC[0x28/4])
#define RCC_PLLCFGR     (RCC[0x2C/4])
#define RCC_PLL1DIVR    (RCC[0x30/4])
#define RCC_PLL1FRACR   (RCC[0x34/4])
#define RCC_AHB1ENR     (RCC[0xD8/4])
#define RCC_AHB2ENR     (RCC[0xDC/4])
#define RCC_APB1LENR    (RCC[0xE8/4])
#define RCC_APB1HENR    (RCC[0xEC/4])
#define RCC_APB2ENR     (RCC[0xF0/4])
#define RCC_APB4ENR     (RCC[0xF4/4])

/* Flash interface */
#define FLASH_BASE      0x52002000UL
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_BASE + 0x00))

/* PWR */
#define PWR_BASE        0x58024800UL
#define PWR_CR3         (*(volatile uint32_t *)(PWR_BASE + 0x0C))
#define PWR_D3CR        (*(volatile uint32_t *)(PWR_BASE + 0x18))
#define PWR_CSR1        (*(volatile uint32_t *)(PWR_BASE + 0x04))

/* ─── API ─────────────────────────────────────────────────────────── */

/*
 * vk_clock_init — configure PLL1 and all bus clocks.
 * Must be the very first call in main() before any peripheral use.
 * Sets SystemCoreClock = 480000000.
 */
void vk_clock_init(void);

/* Populated by vk_clock_init; read by SysTick setup in vk_start() */
extern uint32_t SystemCoreClock;

/* Microsecond busy-wait (uses DWT cycle counter, enabled by vk_clock_init) */
void vk_delay_us(uint32_t us);
void vk_delay_ms(uint32_t ms);

#endif /* VULCAN_CLOCK_H */
