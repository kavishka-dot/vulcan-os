#ifndef VULCAN_TASK_H
#define VULCAN_TASK_H

/*
 * vulcan_task.h — task control block, scheduler API
 * Vulcan OS for STM32H7 (Cortex-M7)
 *
 * Context layout on Cortex-M7 stack (grows downward):
 *
 *   High addr  ┌─────────────┐  ← initial SP (top of stack buffer)
 *              │  xPSR       │  bit 24 must be 1 (Thumb mode)
 *              │  PC         │  task entry point
 *              │  LR         │  = vk_task_exit (sentinel)
 *              │  R12        │
 *              │  R3         │
 *              │  R2         │
 *              │  R1         │
 *              │  R0         │  ← arg passed to task fn
 *   (hardware auto-saves these 8 regs on exception entry)
 *              ├─────────────┤
 *              │  R11        │
 *              │  R10        │
 *              │  R9         │
 *              │  R8         │
 *              │  R7         │
 *              │  R6         │
 *              │  R5         │
 *              │  R4         │  ← saved SP points here after init
 *   Low addr   └─────────────┘
 *
 *  FPU context (S16-S31) pushed below R4 if task uses FPU.
 *  We use FPCA bit in CONTROL to lazy-save; no manual push needed.
 */

#include "vulcan.h"

/* ─── task control block ──────────────────────────────────────────── */

typedef struct vk_task {
    /* saved stack pointer — MUST be first field; asm indexes at offset 0 */
    uint32_t           *sp;

    /* identity */
    const char         *name;
    vk_prio_t           priority;       /* 0 = highest */
    vk_task_state_t     state;
    uint8_t             id;

    /* scheduling */
    vk_tick_t           wake_tick;      /* when to unblock from sleep */

    /* stack bookkeeping */
    uint32_t           *stack_base;     /* lowest address (watermark end) */
    uint32_t            stack_size;     /* bytes */

    /* linked list for run-queue / sleep-queue */
    struct vk_task     *next;
} vk_task_t;

/* ─── scheduler state (extern, defined in vulcan_sched.c) ─────────── */

extern vk_task_t       *vk_current;    /* currently running task */
extern volatile vk_tick_t vk_ticks;   /* global tick counter */

/* ─── public API ──────────────────────────────────────────────────── */

/*
 * vk_task_create — register a new task
 *
 *   stack     : pointer to a statically allocated uint32_t array
 *   stack_words : size of that array in 32-bit words
 *   priority  : lower number = higher priority (0 is highest)
 */
vk_status_t vk_task_create(vk_task_t    *tcb,
                            const char   *name,
                            vk_task_fn_t  fn,
                            void         *arg,
                            uint32_t     *stack,
                            uint32_t      stack_words,
                            vk_prio_t     priority);

/*
 * vk_task_yield — voluntarily give up CPU; triggers PendSV
 */
void vk_task_yield(void);

/*
 * vk_task_sleep — block for `ticks` SysTick periods
 */
void vk_task_sleep(vk_tick_t ticks);

/*
 * vk_task_exit — called if a task function returns (should not happen)
 */
VULCAN_NORETURN void vk_task_exit(void);

/*
 * vk_sched_init — prepare scheduler data structures (call before vk_start)
 */
void vk_sched_init(void);

/*
 * vk_start — transfer control to scheduler; never returns
 */
VULCAN_NORETURN void vk_start(void);

/*
 * vk_tick_get — read current tick count (ISR-safe)
 */
VULCAN_INLINE vk_tick_t vk_tick_get(void) {
    return vk_ticks;
}

/* ─── stack canary / watermark ────────────────────────────────────── */

#define VK_STACK_CANARY  0xDEADBEEFu

uint32_t vk_stack_unused(const vk_task_t *t);   /* words never written */

#endif /* VULCAN_TASK_H */
