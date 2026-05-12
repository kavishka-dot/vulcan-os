/*
 * vulcan_mem.c — pool allocator + arena allocator
 * Vulcan OS for STM32H7 (Cortex-M7)
 *
 * Pool allocator:
 *   Free blocks are linked via an intrusive singly-linked list stored
 *   in the first 4 bytes of each block.  alloc = pop head, free = push head.
 *   Both are O(1) and IRQ-safe via PRIMASK critical section.
 *
 * Arena allocator:
 *   Bump pointer.  Alloc = align ptr, return old ptr, advance.
 *   Reset = ptr = base.  No individual free.  Designed for ML tensor
 *   lifetimes where the entire inference graph is freed atomically.
 */

#include "vulcan_mem.h"

/* ─── irq helpers ────────────────────────────────────────────────── */

static inline uint32_t _irq_save(void) {
    uint32_t primask;
    __asm volatile ("MRS %0, PRIMASK" : "=r"(primask));
    __asm volatile ("CPSID I" ::: "memory");
    return primask;
}

static inline void _irq_restore(uint32_t primask) {
    __asm volatile ("MSR PRIMASK, %0" :: "r"(primask) : "memory");
}

/* ─── pool allocator ─────────────────────────────────────────────── */

void vk_pool_init(vk_pool_t *p, void *storage,
                  uint32_t block_size, uint32_t n_blocks)
{
    /* block_size must be >= sizeof(void*) and aligned to 4 */
    if (block_size < 4) block_size = 4;
    block_size = (block_size + 3u) & ~3u;  /* round up to 4-byte alignment */

    p->block_size = block_size;
    p->total      = n_blocks;
    p->used       = 0;
    p->free_list  = NULL;

    /* Build intrusive free list: each block's first word = next free block */
    uint8_t *blk = (uint8_t *)storage;
    for (uint32_t i = 0; i < n_blocks; i++) {
        void **node = (void **)blk;
        *node = p->free_list;       /* link to previous head */
        p->free_list = blk;         /* new head */
        blk += block_size;
    }
    /* List is now in reverse order — fine for LIFO alloc */
}

void *vk_mem_alloc(vk_pool_t *p) {
    uint32_t key = _irq_save();

    if (!p->free_list) {
        _irq_restore(key);
        return NULL;               /* pool exhausted */
    }

    /* Pop head of free list */
    void **node  = (void **)p->free_list;
    p->free_list = *node;          /* advance head to next free block */
    p->used++;

    _irq_restore(key);

    /* Zero the block before handing out — prevents data leakage between tasks */
    uint8_t *blk = (uint8_t *)node;
    for (uint32_t i = 0; i < p->block_size; i++)
        blk[i] = 0;

    return (void *)node;
}

void vk_mem_free(vk_pool_t *p, void *ptr) {
    if (!ptr) return;

    uint32_t key = _irq_save();

    /* Push ptr back onto free list */
    void **node  = (void **)ptr;
    *node        = p->free_list;
    p->free_list = ptr;
    p->used--;

    _irq_restore(key);
}

uint32_t vk_mem_avail(const vk_pool_t *p) {
    return p->total - p->used;
}

/* ─── arena allocator ────────────────────────────────────────────── */

void vk_arena_init(vk_arena_t *a, void *buf, uint32_t bytes) {
    a->base     = (uint8_t *)buf;
    a->ptr      = (uint8_t *)buf;
    a->capacity = bytes;
    a->peak     = 0;
}

void *vk_arena_alloc(vk_arena_t *a, uint32_t bytes, uint32_t align) {
    /* align must be power of 2 */
    uintptr_t cur = (uintptr_t)a->ptr;
    uintptr_t aligned = (cur + align - 1u) & ~(uintptr_t)(align - 1u);
    uint32_t  padding = (uint32_t)(aligned - cur);

    if (padding + bytes > (uint32_t)(a->capacity - (uint32_t)(a->ptr - a->base)))
        return NULL;   /* out of arena space */

    a->ptr = (uint8_t *)(aligned + bytes);

    uint32_t used = (uint32_t)(a->ptr - a->base);
    if (used > a->peak)
        a->peak = used;

    /* Zero-init tensor storage */
    uint8_t *blk = (uint8_t *)aligned;
    for (uint32_t i = 0; i < bytes; i++)
        blk[i] = 0;

    return (void *)aligned;
}

void vk_arena_reset(vk_arena_t *a) {
    a->ptr = a->base;
    /* peak is preserved for diagnostics */
}

uint32_t vk_arena_used(const vk_arena_t *a) {
    return (uint32_t)(a->ptr - a->base);
}
