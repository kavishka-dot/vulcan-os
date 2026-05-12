/*
 * vulcan_sched.c — cooperative/preemptive scheduler for Vulcan OS
 * Vulcan OS for STM32H7 (Cortex-M7)
 *
 * Context switch mechanism:
 *   - SysTick ISR fires at VULCAN_TICK_HZ, increments vk_ticks, wakes
 *     sleeping tasks, then sets PENDSVSET if a higher-priority task
 *     became ready.
 *   - PendSV ISR (lowest hardware priority) performs the actual context
 *     switch via naked assembly: saves R4-R11 (+ S16-S31 if FPU active),
 *     swaps SP, restores new task's registers.
 *   - Voluntary yield (vk_task_yield) also triggers PendSV.
 *
 * Run queue:
 *   Simple sorted singly-linked list ordered by priority then FIFO.
 *   O(n) insert — acceptable for VULCAN_MAX_TASKS ≤ 16.
 *   The running task is always at the head of ready_queue.
 */

#include "vulcan.h"
#include "vulcan_task.h"

/* ─── scheduler globals ──────────────────────────────────────────── */

vk_task_t        *vk_current   = NULL;
volatile vk_tick_t vk_ticks    = 0;

static vk_task_t *ready_queue  = NULL;   /* sorted by priority, head = next to run */
static vk_task_t *sleep_queue  = NULL;   /* sorted by wake_tick */
static uint8_t    task_count   = 0;

/* idle task — runs when nothing else is ready */
static vk_task_t  idle_tcb;
static uint32_t   idle_stack[64];        /* 256 bytes — enough for WFI */

/* ─── internal helpers ───────────────────────────────────────────── */

/* Disable/enable interrupts via PRIMASK — returns old state */
VULCAN_INLINE uint32_t _irq_save(void) {
    uint32_t primask;
    __asm volatile ("MRS %0, PRIMASK" : "=r"(primask));
    __asm volatile ("CPSID I" ::: "memory");
    return primask;
}

VULCAN_INLINE void _irq_restore(uint32_t primask) {
    __asm volatile ("MSR PRIMASK, %0" :: "r"(primask) : "memory");
}

/* Insert into ready queue (sorted by priority; FIFO within same priority) */
static void _ready_insert(vk_task_t *t) {
    t->state = VK_TASK_READY;
    vk_task_t **pp = &ready_queue;
    while (*pp && (*pp)->priority <= t->priority)
        pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
}

/* Remove specific task from ready queue */
static void _ready_remove(vk_task_t *t) {
    vk_task_t **pp = &ready_queue;
    while (*pp && *pp != t)
        pp = &(*pp)->next;
    if (*pp) *pp = t->next;
    t->next = NULL;
}

/* Insert into sleep queue (sorted ascending by wake_tick) */
static void _sleep_insert(vk_task_t *t) {
    t->state = VK_TASK_BLOCKED;
    vk_task_t **pp = &sleep_queue;
    while (*pp && (*pp)->wake_tick <= t->wake_tick)
        pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
}

/* Trigger PendSV for context switch */
VULCAN_INLINE void _trigger_pendsv(void) {
    __asm volatile ("DSB" ::: "memory");
    SCB_ICSR = ICSR_PENDSVSET;
    __asm volatile ("ISB" ::: "memory");
}

/* ─── idle task ──────────────────────────────────────────────────── */

static void _idle_task(void *arg) {
    (void)arg;
    for (;;) {
        /* Wait For Interrupt — halts CPU clock until next IRQ.
         * Saves power between ticks; PendSV or SysTick will wake us. */
        __asm volatile ("WFI" ::: "memory");
    }
}

/* ─── scheduler init ─────────────────────────────────────────────── */

void vk_sched_init(void) {
    ready_queue = NULL;
    sleep_queue = NULL;
    vk_current  = NULL;
    vk_ticks    = 0;
    task_count  = 0;

    /* Create idle task at lowest possible priority */
    vk_task_create(&idle_tcb, "idle", _idle_task, NULL,
                   idle_stack, sizeof(idle_stack) / sizeof(uint32_t),
                   (vk_prio_t)255);
}

/* ─── SysTick ISR ────────────────────────────────────────────────── */

void SysTick_Handler(void) {
    vk_ticks++;

    /* Wake tasks whose sleep timer has expired */
    while (sleep_queue && sleep_queue->wake_tick <= vk_ticks) {
        vk_task_t *t = sleep_queue;
        sleep_queue   = t->next;
        t->next       = NULL;
        _ready_insert(t);
    }

    /* If head of ready queue has higher priority than running task, preempt */
    if (ready_queue && vk_current &&
        ready_queue->priority < vk_current->priority) {
        _trigger_pendsv();
    }
}

/* ─── PendSV ISR — context switch ───────────────────────────────── */
/*
 * PendSV is set to the lowest exception priority (0xFF) so it always
 * runs after SysTick and all other ISRs have completed.
 *
 * On entry, hardware has already pushed {R0-R3, R12, LR, PC, xPSR}
 * onto the current task's stack (exception frame).
 *
 * We push the remaining callee-saved registers {R4-R11} manually.
 * On Cortex-M7 with FPU, if the task used floating-point, the hardware
 * also pushes {S0-S15, FPSCR}; we push {S16-S31} manually.
 *
 * EXC_RETURN (LR on exception entry) encodes whether FPU was active:
 *   bit 4 = 0 → extended frame (FPU), = 1 → basic frame
 */

VULCAN_NAKED void PendSV_Handler(void) {
    __asm volatile (
        /* ── save current task context ── */
        "CPSID   I                  \n"  /* disable IRQs during switch */

        /* Check if FPU context needs saving (EXC_RETURN bit 4) */
        "TST     LR, #0x10          \n"
        "IT      EQ                 \n"
        "VPUSHEQ {S16-S31}          \n"  /* push high FPU regs if FPU active */

        "PUSH    {R4-R11, LR}       \n"  /* push callee-saved + EXC_RETURN */

        /* vk_current->sp = SP */
        "LDR     R0, =vk_current    \n"
        "LDR     R1, [R0]           \n"  /* R1 = vk_current */
        "STR     SP, [R1, #0]       \n"  /* vk_current->sp = SP (offset 0) */

        /* ── select next task ── */
        "BL      _vk_schedule       \n"  /* C function: updates vk_current */

        /* ── restore next task context ── */
        "LDR     R0, =vk_current    \n"
        "LDR     R1, [R0]           \n"  /* R1 = new vk_current */
        "LDR     SP, [R1, #0]       \n"  /* SP = new task's sp */

        "POP     {R4-R11, LR}       \n"  /* restore callee-saved + EXC_RETURN */

        "TST     LR, #0x10          \n"
        "IT      EQ                 \n"
        "VPOPEQ  {S16-S31}          \n"  /* restore high FPU regs if needed */

        "CPSIE   I                  \n"  /* re-enable IRQs */
        "BX      LR                 \n"  /* return from exception → new task */
        ::: "memory"
    );
}

/*
 * _vk_schedule — pick the next task to run.
 * Called from PendSV with interrupts disabled.
 */
void _vk_schedule(void) {
    /* Move current running task back to ready queue (if still alive) */
    if (vk_current && vk_current->state == VK_TASK_RUNNING) {
        vk_current->state = VK_TASK_READY;
        _ready_insert(vk_current);
    }

    /* Pop highest-priority ready task */
    vk_task_t *next = ready_queue;
    if (next) {
        ready_queue = next->next;
        next->next  = NULL;
        next->state = VK_TASK_RUNNING;
    }

    vk_current = next;
    /* If next == NULL we have a bug (idle task should always be ready) */
}

/* ─── public task API ────────────────────────────────────────────── */

vk_status_t vk_task_create(vk_task_t    *tcb,
                            const char   *name,
                            vk_task_fn_t  fn,
                            void         *arg,
                            uint32_t     *stack,
                            uint32_t      stack_words,
                            vk_prio_t     priority)
{
    if (!tcb || !fn || !stack || stack_words < 32)
        return VK_ERR_PARAM;
    if (task_count >= VULCAN_MAX_TASKS)
        return VK_ERR_FULL;

    /* Fill stack with canary pattern for watermark measurement */
    for (uint32_t i = 0; i < stack_words; i++)
        stack[i] = VK_STACK_CANARY;

    /*
     * Build initial stack frame (Cortex-M exception return format).
     * Stack grows downward; start from top of array.
     */
    uint32_t *sp = stack + stack_words;

    /* Exception frame pushed by hardware on first "exception return" */
    *(--sp) = 0x01000000u;          /* xPSR — Thumb bit set */
    *(--sp) = (uint32_t)fn;         /* PC — task entry */
    *(--sp) = (uint32_t)vk_task_exit; /* LR — called if fn returns */
    *(--sp) = 0x00000000u;          /* R12 */
    *(--sp) = 0x00000000u;          /* R3 */
    *(--sp) = 0x00000000u;          /* R2 */
    *(--sp) = 0x00000000u;          /* R1 */
    *(--sp) = (uint32_t)arg;        /* R0 — first argument */

    /* Callee-saved registers {R4-R11} + EXC_RETURN
     * EXC_RETURN = 0xFFFFFFFD: return to Thread mode, use PSP, no FPU */
    *(--sp) = 0xFFFFFFFDu;          /* LR = EXC_RETURN */
    *(--sp) = 0x00000000u;          /* R11 */
    *(--sp) = 0x00000000u;          /* R10 */
    *(--sp) = 0x00000000u;          /* R9 */
    *(--sp) = 0x00000000u;          /* R8 */
    *(--sp) = 0x00000000u;          /* R7 */
    *(--sp) = 0x00000000u;          /* R6 */
    *(--sp) = 0x00000000u;          /* R5 */
    *(--sp) = 0x00000000u;          /* R4 */

    /* Populate TCB */
    tcb->sp         = sp;
    tcb->name       = name;
    tcb->priority   = priority;
    tcb->state      = VK_TASK_DEAD;
    tcb->id         = task_count++;
    tcb->wake_tick  = 0;
    tcb->stack_base = stack;
    tcb->stack_size = stack_words * sizeof(uint32_t);
    tcb->next       = NULL;

    uint32_t key = _irq_save();
    _ready_insert(tcb);
    _irq_restore(key);

    return VK_OK;
}

void vk_task_yield(void) {
    _trigger_pendsv();
}

void vk_task_sleep(vk_tick_t ticks) {
    uint32_t key = _irq_save();
    _ready_remove(vk_current);
    vk_current->wake_tick = vk_ticks + ticks;
    _sleep_insert(vk_current);
    _irq_restore(key);
    _trigger_pendsv();
}

VULCAN_NORETURN void vk_task_exit(void) {
    uint32_t key = _irq_save();
    vk_current->state = VK_TASK_DEAD;
    _irq_restore(key);
    _trigger_pendsv();
    for (;;); /* unreachable — suppress noreturn warning */
}

uint32_t vk_stack_unused(const vk_task_t *t) {
    uint32_t count = 0;
    uint32_t *p = t->stack_base;
    while (*p == VK_STACK_CANARY) { p++; count++; }
    return count;
}

/* ─── vk_start — launch scheduler ───────────────────────────────── */

VULCAN_NORETURN void vk_start(void) {
    /*
     * Configure SysTick for VULCAN_TICK_HZ.
     * Assumes SystemCoreClock is set (typically 480 MHz on H7).
     */
    extern uint32_t SystemCoreClock;
    SYSTICK_LOAD = (SystemCoreClock / VULCAN_TICK_HZ) - 1u;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = SYSTICK_CLKSRC | SYSTICK_TICKINT | SYSTICK_ENABLE;

    /* Set PendSV to lowest priority so it runs after all other ISRs */
    SCB_SHPR3 |= (0xFFu << 16);   /* PendSV priority = 0xFF */

    /* Pick first task — scheduler hasn't run yet so grab head of queue */
    vk_current = ready_queue;
    ready_queue = ready_queue->next;
    vk_current->next = NULL;
    vk_current->state = VK_TASK_RUNNING;

    /*
     * Bootstrap: set PSP to the first task's stack, switch to PSP,
     * then fake an exception return to enter Thread mode.
     */
    __asm volatile (
        "MSR PSP, %0            \n"  /* PSP = first task's SP */
        "MOV R0, #2             \n"
        "MSR CONTROL, R0        \n"  /* switch to PSP, unprivileged Thread */
        "ISB                    \n"
        "POP {R4-R11, LR}       \n"  /* restore callee-saved from task frame */
        "POP {R0-R3, R12, LR}   \n"  /* restore R0-R3, R12, LR (from hw frame) */
        "POP {PC}               \n"  /* jump to task entry — drops xPSR implicitly */
        :: "r"(vk_current->sp) : "memory"
    );

    for (;;); /* unreachable */
}
