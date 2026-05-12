/*
 * vulcan_mutex.c — binary mutex with LDREX/STREX atomic lock
 * Vulcan OS for STM32H7 (Cortex-M7)
 *
 * LDREX/STREX provide exclusive-access semantics via the CPU's local
 * monitor — no need to disable interrupts for the test-and-set itself.
 * We only take a brief critical section when manipulating the wait queue.
 */

#include "vulcan_mutex.h"

/* ─── internal: irq helpers (replicated here to avoid circular deps) ─ */

static inline uint32_t _irq_save(void) {
    uint32_t primask;
    __asm volatile ("MRS %0, PRIMASK" : "=r"(primask));
    __asm volatile ("CPSID I" ::: "memory");
    return primask;
}

static inline void _irq_restore(uint32_t primask) {
    __asm volatile ("MSR PRIMASK, %0" :: "r"(primask) : "memory");
}

/* Defined in vulcan_sched.c */
extern vk_task_t *vk_current;
extern void _trigger_pendsv(void);

/* ─── implementation ─────────────────────────────────────────────── */

void vk_mutex_init(vk_mutex_t *m) {
    m->locked    = 0;
    m->owner     = NULL;
    m->wait_head = NULL;
    m->wait_tail = NULL;
}

vk_status_t vk_mutex_trylock(vk_mutex_t *m) {
    uint32_t old, result;
    do {
        /* LDREX — load with exclusive monitor set */
        __asm volatile ("LDREX %0, [%2]" : "=r"(old) : "m"(m->locked), "r"(&m->locked));
        if (old != 0) {
            /* Already locked — clear exclusive monitor and return */
            __asm volatile ("CLREX" ::: "memory");
            return VK_ERR_FULL;
        }
        /* STREX — store only if exclusive monitor is still set */
        __asm volatile ("STREX %0, %2, [%3]"
                        : "=&r"(result), "=m"(m->locked)
                        : "r"(1u), "r"(&m->locked));
    } while (result != 0);   /* retry if another CPU/ISR preempted between LDREX/STREX */

    __asm volatile ("DMB" ::: "memory");  /* memory barrier before accessing protected data */
    m->owner = vk_current;
    return VK_OK;
}

void vk_mutex_lock(vk_mutex_t *m) {
    for (;;) {
        if (vk_mutex_trylock(m) == VK_OK)
            return;

        /* Lock is held — enqueue ourselves on the wait list */
        uint32_t key = _irq_save();

        /* Re-check under critical section — might have been released */
        if (m->locked == 0) {
            _irq_restore(key);
            continue;
        }

        /* Add to wait tail (FIFO queue) */
        vk_current->state = VK_TASK_BLOCKED;
        vk_current->next  = NULL;
        if (m->wait_tail) {
            m->wait_tail->next = vk_current;
            m->wait_tail       = vk_current;
        } else {
            m->wait_head = m->wait_tail = vk_current;
        }

        _irq_restore(key);

        /* Yield — PendSV will pick another task; we won't resume until unlock */
        _trigger_pendsv();
    }
}

void vk_mutex_unlock(vk_mutex_t *m) {
    __asm volatile ("DMB" ::: "memory");  /* ensure writes visible before release */

    uint32_t key = _irq_save();

    m->locked = 0;
    m->owner  = NULL;

    /* Wake the highest-priority waiter (head of FIFO wait queue) */
    if (m->wait_head) {
        vk_task_t *waiter = m->wait_head;
        m->wait_head = waiter->next;
        if (!m->wait_head)
            m->wait_tail = NULL;
        waiter->next = NULL;

        /* Put waiter back in ready queue — defined in vulcan_sched.c */
        extern void _ready_insert(vk_task_t *);
        _ready_insert(waiter);
    }

    _irq_restore(key);

    /* If a waiter has higher priority than us, yield now */
    if (m->wait_head == NULL && vk_current) {
        /* no-op: we just woke the only waiter */
    }
    _trigger_pendsv();
}
