#ifndef VULCAN_MUTEX_H
#define VULCAN_MUTEX_H

/*
 * vulcan_mutex.h — non-recursive binary mutex
 * Vulcan OS for STM32H7 (Cortex-M7)
 *
 * Uses LDREX/STREX (load-link / store-conditional) for atomic
 * test-and-set without disabling interrupts.  Waiters are put
 * in VK_TASK_BLOCKED state and re-queued when the mutex is released.
 */

#include "vulcan.h"
#include "vulcan_task.h"

/* ─── mutex control block ─────────────────────────────────────────── */

typedef struct vk_mutex {
    volatile uint32_t  locked;       /* 0 = free, 1 = held */
    vk_task_t         *owner;        /* task that holds the lock */
    vk_task_t         *wait_head;    /* singly-linked wait queue (FIFO) */
    vk_task_t         *wait_tail;
} vk_mutex_t;

/* ─── static initialiser ─────────────────────────────────────────── */

#define VK_MUTEX_INIT  { .locked = 0, .owner = NULL, \
                         .wait_head = NULL, .wait_tail = NULL }

/* ─── API ─────────────────────────────────────────────────────────── */

void vk_mutex_init(vk_mutex_t *m);

/*
 * vk_mutex_lock — acquire mutex; blocks if held by another task.
 *                 Must NOT be called from an ISR.
 */
void vk_mutex_lock(vk_mutex_t *m);

/*
 * vk_mutex_unlock — release mutex; wakes highest-priority waiter.
 *                   Must NOT be called from an ISR.
 */
void vk_mutex_unlock(vk_mutex_t *m);

/*
 * vk_mutex_trylock — non-blocking attempt; returns VK_OK or VK_ERR_FULL
 */
vk_status_t vk_mutex_trylock(vk_mutex_t *m);

#endif /* VULCAN_MUTEX_H */
