#ifndef VULCAN_MEM_H
#define VULCAN_MEM_H

/*
 * vulcan_mem.h — deterministic pool allocator
 * Vulcan OS for STM32H7 (Cortex-M7)
 *
 * Design goals:
 *   • O(1) alloc and free — no searching, no coalescing
 *   • Zero fragmentation — fixed-size blocks per pool
 *   • No heap — all storage is statically declared by the caller
 *   • ISR-safe — uses PRIMASK critical sections around free-list ops
 *
 * Usage:
 *   #define MY_POOL_BLOCK  64
 *   #define MY_POOL_COUNT  32
 *   VK_POOL_DECLARE(sensor_pool, MY_POOL_BLOCK, MY_POOL_COUNT);
 *   vk_mem_init(&sensor_pool);
 *   void *p = vk_mem_alloc(&sensor_pool);
 *   vk_mem_free(&sensor_pool, p);
 *
 * The macro lays out memory as:
 *   [ vk_pool_t header ][ block 0 ][ block 1 ] ... [ block N-1 ]
 * Each free block's first 4 bytes hold a pointer to the next free block
 * (intrusive free list — no separate bookkeeping array needed).
 */

#include "vulcan.h"

/* ─── pool control block ─────────────────────────────────────────── */

typedef struct vk_pool {
    void        *free_list;     /* head of intrusive free list */
    uint32_t     block_size;    /* bytes per block (>= 4) */
    uint32_t     total;         /* total blocks */
    uint32_t     used;          /* currently allocated blocks */
} vk_pool_t;

/* ─── static pool declaration macro ──────────────────────────────── */

/*
 * VK_POOL_DECLARE(name, block_bytes, n_blocks)
 *
 * Declares:
 *   vk_pool_t       name;
 *   uint8_t         name##_storage[block_bytes * n_blocks];  (aligned 8)
 *
 * Call vk_pool_init(&name, name##_storage, block_bytes, n_blocks) once.
 *
 * The convenience macro VK_POOL_INIT does this in one step.
 */
#define VK_POOL_DECLARE(name, block_bytes, n_blocks)                    \
    static uint8_t VULCAN_ALIGNED(8)                                    \
        name##_storage[(block_bytes) * (n_blocks)];                     \
    static vk_pool_t name

#define VK_POOL_INIT(name, block_bytes, n_blocks)                       \
    vk_pool_init(&(name), (name##_storage),                             \
                 (block_bytes), (n_blocks))

/* ─── tensor arena ───────────────────────────────────────────────── */

/*
 * vk_arena_t — bump-pointer allocator for ML tensor storage.
 * Alloc is O(1); free is all-or-nothing (reset to base).
 * Lives in DTCM RAM for zero-wait-cycle access on H7.
 */
typedef struct {
    uint8_t  *base;
    uint8_t  *ptr;       /* next free byte */
    uint32_t  capacity;  /* bytes */
    uint32_t  peak;      /* high-water mark */
} vk_arena_t;

/* Declare a static arena backed by a plain array */
#define VK_ARENA_DECLARE(name, bytes)                                   \
    static uint8_t VULCAN_ALIGNED(8)                                    \
        VULCAN_SECTION(".dtcmram") name##_buf[bytes];                   \
    static vk_arena_t name

#define VK_ARENA_INIT(name, bytes)                                      \
    vk_arena_init(&(name), (name##_buf), (bytes))

/* ─── API ─────────────────────────────────────────────────────────── */

/* Pool allocator */
void        vk_pool_init (vk_pool_t *p, void *storage,
                           uint32_t block_size, uint32_t n_blocks);
void       *vk_mem_alloc (vk_pool_t *p);           /* returns NULL if full */
void        vk_mem_free  (vk_pool_t *p, void *ptr);
uint32_t    vk_mem_avail (const vk_pool_t *p);     /* free block count */

/* Arena allocator */
void        vk_arena_init  (vk_arena_t *a, void *buf, uint32_t bytes);
void       *vk_arena_alloc (vk_arena_t *a, uint32_t bytes, uint32_t align);
void        vk_arena_reset (vk_arena_t *a);        /* free everything */
uint32_t    vk_arena_used  (const vk_arena_t *a);

/* Convenience: 8-byte aligned arena alloc (common case for tensors) */
#define vk_tensor_alloc(arena, bytes) \
    vk_arena_alloc((arena), (bytes), 8u)

#endif /* VULCAN_MEM_H */
