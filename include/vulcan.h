#ifndef VULCAN_H
#define VULCAN_H

/*
 * vulcan.h — core types and configuration
 * Vulcan OS for STM32H7 (Cortex-M7)
 */

#include <stdint.h>
#include <stddef.h>

/* ─── build configuration ─────────────────────────────────────────── */

#define VULCAN_MAX_TASKS        16
#define VULCAN_TICK_HZ          1000          /* 1 ms tick */
#define VULCAN_DEFAULT_STACK    1024          /* bytes */
#define VULCAN_POOL_BLOCK_SIZES {32, 64, 128, 256, 512}
#define VULCAN_POOL_BLOCK_COUNT 16            /* per size class */

/* ─── compiler / arch helpers ─────────────────────────────────────── */

#define VULCAN_NAKED        __attribute__((naked))
#define VULCAN_NORETURN     __attribute__((noreturn))
#define VULCAN_ALIGNED(n)   __attribute__((aligned(n)))
#define VULCAN_SECTION(s)   __attribute__((section(s)))
#define VULCAN_UNUSED       __attribute__((unused))
#define VULCAN_WEAK         __attribute__((weak))
#define VULCAN_INLINE       static inline __attribute__((always_inline))

/* Cortex-M7 memory-mapped registers */
#define SCB_ICSR        (*(volatile uint32_t *)0xE000ED04)  /* interrupt control & state */
#define SCB_SHPR3       (*(volatile uint32_t *)0xE000ED20)  /* system handler priority 3 */
#define SYSTICK_CTRL    (*(volatile uint32_t *)0xE000E010)
#define SYSTICK_LOAD    (*(volatile uint32_t *)0xE000E014)
#define SYSTICK_VAL     (*(volatile uint32_t *)0xE000E018)
#define SYSTICK_CALIB   (*(volatile uint32_t *)0xE000E01C)

#define ICSR_PENDSVSET  (1u << 28)   /* set PendSV pending */
#define SYSTICK_CLKSRC  (1u << 2)    /* use processor clock */
#define SYSTICK_TICKINT (1u << 1)    /* enable tick interrupt */
#define SYSTICK_ENABLE  (1u << 0)

/* ─── primitive types ─────────────────────────────────────────────── */

typedef uint32_t    vk_tick_t;
typedef uint8_t     vk_prio_t;     /* 0 = highest */
typedef int32_t     vk_status_t;

#define VK_OK           ( 0)
#define VK_ERR_FULL     (-1)
#define VK_ERR_TIMEOUT  (-2)
#define VK_ERR_NOMEM    (-3)
#define VK_ERR_PARAM    (-4)

/* ─── task states ─────────────────────────────────────────────────── */

typedef enum {
    VK_TASK_DEAD    = 0,
    VK_TASK_READY   = 1,
    VK_TASK_RUNNING = 2,
    VK_TASK_BLOCKED = 3,   /* sleeping or waiting on mutex */
} vk_task_state_t;

/* ─── forward declarations ────────────────────────────────────────── */

struct vk_task;
struct vk_mutex;
struct vk_pool;

typedef void (*vk_task_fn_t)(void *arg);

#endif /* VULCAN_H */
