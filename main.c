/*
 * main.c — Vulcan OS Phase 1 demo
 * STM32H743 target (480 MHz Cortex-M7)
 *
 * Demonstrates:
 *   - Three concurrent tasks at different priorities
 *   - Sleep / wake via SysTick
 *   - Mutex-protected shared resource
 *   - Pool allocator for message passing
 *   - Arena allocator for a mock tensor buffer
 *   - Stack watermark reporting over UART
 */

#include "vulcan.h"
#include "vulcan_task.h"
#include "vulcan_mutex.h"
#include "vulcan_mem.h"

/* ─── hardware init stub (replace with STM32CubeMX output) ──────── */
extern void SystemInit(void);         /* sets SystemCoreClock = 480 MHz */
static void uart_init(void)  {}       /* configure UART3 for debug */
static void uart_putc(char c) { (void)c; }  /* transmit one byte */

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_puthex(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(v >> 28) & 0xF]);
    uart_putc(hex[(v >> 24) & 0xF]);
    uart_putc(hex[(v >> 20) & 0xF]);
    uart_putc(hex[(v >> 16) & 0xF]);
    uart_putc(hex[(v >> 12) & 0xF]);
    uart_putc(hex[(v >>  8) & 0xF]);
    uart_putc(hex[(v >>  4) & 0xF]);
    uart_putc(hex[(v >>  0) & 0xF]);
}

/* ─── shared resources ───────────────────────────────────────────── */

static vk_mutex_t  console_mutex = VK_MUTEX_INIT;

/* Pool for 64-byte messages — 8 blocks */
VK_POOL_DECLARE(msg_pool, 64, 8);

/* 64 KB tensor arena in DTCM RAM */
VK_ARENA_DECLARE(tensor_arena, 64 * 1024);

/* ─── task stacks (static allocation — no heap) ──────────────────── */

#define STACK_WORDS  256   /* 1 KB per task */

static vk_task_t  sensor_tcb,  infer_tcb,  report_tcb;
static uint32_t   sensor_stack[STACK_WORDS];
static uint32_t   infer_stack [STACK_WORDS];
static uint32_t   report_stack[STACK_WORDS];

/* ─── task: sensor ───────────────────────────────────────────────── */
/*
 * Priority 1 (high) — simulates reading ADC/IQ samples every 10 ms,
 * allocating a 64-byte message from the pool, filling it with mock
 * data, and passing the pointer to the inference task via a shared ptr.
 */

static volatile void *sensor_msg_ptr = NULL;  /* IPC: sensor → infer */

static void task_sensor(void *arg) {
    (void)arg;
    uint32_t sample_n = 0;

    for (;;) {
        /* Allocate a message block */
        uint8_t *msg = (uint8_t *)vk_mem_alloc(&msg_pool);
        if (msg) {
            /* Fill with mock ADC samples */
            for (int i = 0; i < 64; i++)
                msg[i] = (uint8_t)((sample_n + i) & 0xFF);
            sensor_msg_ptr = msg;
            sample_n++;
        }

        vk_task_sleep(10);   /* 10 ms period */
    }
}

/* ─── task: inference ────────────────────────────────────────────── */
/*
 * Priority 2 — wakes when sensor data is ready, allocates tensor
 * memory from arena, runs a mock "inference" (just sums the bytes),
 * frees the message back to pool, resets arena.
 */

static void task_infer(void *arg) {
    (void)arg;
    uint32_t infer_count = 0;

    for (;;) {
        /* Spin-yield until sensor produces data */
        while (!sensor_msg_ptr)
            vk_task_yield();

        uint8_t *msg = (uint8_t *)sensor_msg_ptr;
        sensor_msg_ptr = NULL;

        /* Allocate mock input tensor (64 bytes = 64 INT8 features) */
        int8_t *tensor = (int8_t *)vk_tensor_alloc(&tensor_arena, 64);
        if (tensor) {
            uint32_t acc = 0;
            for (int i = 0; i < 64; i++) {
                tensor[i] = (int8_t)msg[i];
                acc += msg[i];
            }
            infer_count++;
            (void)acc;   /* result would feed next layer */
        }

        /* Free message block back to pool */
        vk_mem_free(&msg_pool, msg);

        /* Reset arena (all tensors freed atomically) */
        vk_arena_reset(&tensor_arena);

        vk_task_sleep(1);  /* yield for 1 tick before next inference */
    }
}

/* ─── task: report ───────────────────────────────────────────────── */
/*
 * Priority 3 (low) — prints kernel diagnostics every 5 s.
 */

static void task_report(void *arg) {
    (void)arg;

    for (;;) {
        vk_task_sleep(5000);   /* 5 seconds */

        vk_mutex_lock(&console_mutex);

        uart_puts("\r\n[VULCAN] tick=");
        uart_puthex(vk_tick_get());
        uart_puts("\r\n");

        /* Stack watermarks */
        uart_puts("  sensor stack free=");
        uart_puthex(vk_stack_unused(&sensor_tcb));
        uart_puts(" words\r\n");

        uart_puts("  infer  stack free=");
        uart_puthex(vk_stack_unused(&infer_tcb));
        uart_puts(" words\r\n");

        uart_puts("  report stack free=");
        uart_puthex(vk_stack_unused(&report_tcb));
        uart_puts(" words\r\n");

        /* Memory stats */
        uart_puts("  pool avail=");
        uart_puthex(vk_mem_avail(&msg_pool));
        uart_puts("/8 blocks\r\n");

        uart_puts("  arena peak=");
        uart_puthex(tensor_arena.peak);
        uart_puts(" bytes\r\n");

        vk_mutex_unlock(&console_mutex);
    }
}

/* ─── entry point ────────────────────────────────────────────────── */

int main(void) {
    SystemInit();
    uart_init();

    uart_puts("\r\nVulcan OS — Phase 1\r\n");
    uart_puts("STM32H743 Cortex-M7 @ 480 MHz\r\n\r\n");

    /* Initialise kernel */
    vk_sched_init();

    /* Initialise memory subsystems */
    VK_POOL_INIT(msg_pool,    64, 8);
    VK_ARENA_INIT(tensor_arena, 64 * 1024);
    vk_mutex_init(&console_mutex);

    /* Create tasks */
    vk_task_create(&sensor_tcb, "sensor", task_sensor, NULL,
                   sensor_stack, STACK_WORDS, 1);

    vk_task_create(&infer_tcb,  "infer",  task_infer,  NULL,
                   infer_stack,  STACK_WORDS, 2);

    vk_task_create(&report_tcb, "report", task_report, NULL,
                   report_stack, STACK_WORDS, 3);

    /* Hand off to scheduler — never returns */
    vk_start();
}
