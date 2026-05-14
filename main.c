/*
 * main.c — Vulcan OS Phase 2 demo
 * STM32H743 @ 480 MHz
 *
 * Demonstrates full HAL stack:
 *   - SysClock at 480 MHz via PLL1
 *   - UART3 debug output (ST-Link virtual COM, 115200 baud)
 *   - GPIO LED heartbeat (Nucleo LD1/LD2/LD3)
 *   - ADC1 continuous DMA -> ML pipeline task
 *   - SPI1 master (ready for external flash or SDR front-end)
 *   - Kernel: 3 tasks + idle, mutex, pool allocator, tensor arena
 */

#include "vulcan.h"
#include "vulcan_task.h"
#include "vulcan_mutex.h"
#include "vulcan_mem.h"
#include "vulcan_clock.h"
#include "vulcan_gpio.h"
#include "vulcan_uart.h"
#include "vulcan_dma.h"
#include "vulcan_adc.h"
#include "vulcan_spi.h"

/* memory */
VK_POOL_DECLARE (msg_pool,     64,         8);
VK_ARENA_DECLARE(tensor_arena, 64 * 1024);

/* task stacks */
#define STACK_WORDS 512
static vk_task_t  sensor_tcb,  infer_tcb,  report_tcb;
static uint32_t   sensor_stack[STACK_WORDS];
static uint32_t   infer_stack [STACK_WORDS];
static uint32_t   report_stack[STACK_WORDS];

/* ADC + DMA */
static vk_dma_ch_t   adc_dma;
static vk_adc_inst_t adc1_inst;

static volatile const uint16_t *adc_batch_ptr = NULL;
static volatile uint16_t        adc_batch_len  = 0;

static void on_adc_batch(const uint16_t *samples, uint16_t n, void *arg) {
    (void)arg;
    adc_batch_ptr = samples;
    adc_batch_len = n;
}

/* SPI1 */
static vk_spi_inst_t spi1_inst;

/* task: sensor */
static void task_sensor(void *arg) {
    (void)arg;
    VK_LOG("sensor task started");
    static const uint8_t adc_ch[] = { 4, 5 };
    vk_adc_init(&adc1_inst, ADC1, adc_ch, 2,
                VK_ADC_RES_12, VK_ADC_SMPR_64_5, &adc_dma);
    adc1_inst.batch_cb = on_adc_batch;
    vk_adc_start(&adc1_inst);
    VK_LOG("ADC1 continuous DMA started (CH4, CH5)");
    for (;;) {
        vk_gpio_toggle(VK_LED_GREEN);
        vk_task_sleep(500);
    }
}

/* task: inference */
static void task_infer(void *arg) {
    (void)arg;
    VK_LOG("inference task started");
    uint32_t infer_count = 0;
    for (;;) {
        while (!adc_batch_len) vk_task_yield();
        const uint16_t *batch = (const uint16_t *)adc_batch_ptr;
        uint16_t        n     = adc_batch_len;
        adc_batch_len = 0;
        int8_t *tensor = (int8_t *)vk_tensor_alloc(&tensor_arena, n);
        if (tensor) {
            int32_t acc = 0;
            for (uint16_t i = 0; i < n; i++) {
                tensor[i] = (int8_t)(((int32_t)batch[i] - 2048) >> 4);
                acc += tensor[i];
            }
            infer_count++;
            if ((infer_count % 1000) == 0) {
                VK_LOG("infer #%u  acc=%d  arena_peak=%u B",
                       infer_count, (int)acc, tensor_arena.peak);
                vk_gpio_toggle(VK_LED_YELLOW);
            }
        }
        vk_arena_reset(&tensor_arena);
    }
}

/* task: report */
static void task_report(void *arg) {
    (void)arg;
    VK_LOG("report task started");
    vk_spi_cs_assert(&spi1_inst);
    uint8_t echo = vk_spi_xfer(&spi1_inst, 0xA5);
    vk_spi_cs_release(&spi1_inst);
    VK_LOG("SPI1 loopback: sent=0xA5 recv=0x%02X", echo);
    for (;;) {
        vk_task_sleep(5000);
        VK_LOG("tick=%u  sensor_free=%u  infer_free=%u  arena_peak=%u",
               vk_tick_get(),
               vk_stack_unused(&sensor_tcb),
               vk_stack_unused(&infer_tcb),
               tensor_arena.peak);
        vk_gpio_toggle(VK_LED_RED);
    }
}

/* entry point */
int main(void) {
    vk_clock_init();
    vk_uart_init(&vk_uart3, USART3_BASE, 115200, 120000000UL);
    VK_LOG("Vulcan OS -- Phase 2  STM32H743 @ %u Hz", SystemCoreClock);

    vk_gpio_output(VK_LED_GREEN,  VK_GPIO_SPEED_LOW);
    vk_gpio_output(VK_LED_YELLOW, VK_GPIO_SPEED_LOW);
    vk_gpio_output(VK_LED_RED,    VK_GPIO_SPEED_LOW);

    vk_spi_init(&spi1_inst, SPI1,
                VK_SPI_MODE0, VK_SPI_DIV4, 8,
                VK_PIN(GPIOA, 4), NULL, NULL);

    vk_sched_init();
    VK_POOL_INIT (msg_pool,     64, 8);
    VK_ARENA_INIT(tensor_arena, 64 * 1024);

    vk_task_create(&sensor_tcb, "sensor", task_sensor, NULL,
                   sensor_stack, STACK_WORDS, 1);
    vk_task_create(&infer_tcb,  "infer",  task_infer,  NULL,
                   infer_stack,  STACK_WORDS, 2);
    vk_task_create(&report_tcb, "report", task_report, NULL,
                   report_stack, STACK_WORDS, 3);

    vk_start();
}
