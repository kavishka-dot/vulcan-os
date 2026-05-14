#ifndef VULCAN_DMA_H
#define VULCAN_DMA_H

/*
 * vulcan_dma.h — DMA1/DMA2 driver for STM32H743
 * Vulcan OS
 *
 * STM32H743 has two general-purpose DMA controllers (DMA1, DMA2),
 * each with 8 streams, plus a dedicated BDMA for low-power domain.
 * Each stream has a 4-deep request MUX (DMAMUX1).
 *
 * Vulcan uses DMA for:
 *   • ADC → memory (continuous sensor sampling)
 *   • Memory → SPI TX (model weight streaming)
 *   • UART RX → memory (host command reception)
 *   • Memory → memory (tensor buffer copy)
 *
 * Transfer complete / half-complete / error callbacks are fired from
 * the stream ISR and execute in interrupt context — keep them short.
 */

#include "vulcan.h"

/* ─── DMA base addresses ──────────────────────────────────────────── */
#define DMA1_BASE    0x40020000UL
#define DMA2_BASE    0x40020400UL
#define DMAMUX1_BASE 0x40020800UL

/* Stream register layout (each stream is 0x18 bytes apart) */
typedef struct {
    volatile uint32_t CR;    /* configuration */
    volatile uint32_t NDTR;  /* number of data items */
    volatile uint32_t PAR;   /* peripheral address */
    volatile uint32_t M0AR;  /* memory 0 address */
    volatile uint32_t M1AR;  /* memory 1 address (double-buffer) */
    volatile uint32_t FCR;   /* FIFO control */
} vk_dma_stream_t;

/* DMA controller register layout */
typedef struct {
    volatile uint32_t LISR;   /* low  interrupt status */
    volatile uint32_t HISR;   /* high interrupt status */
    volatile uint32_t LIFCR;  /* low  interrupt flag clear */
    volatile uint32_t HIFCR;  /* high interrupt flag clear */
    vk_dma_stream_t   S[8];   /* streams 0..7 */
} vk_dma_t;

#define DMA1  ((vk_dma_t *)DMA1_BASE)
#define DMA2  ((vk_dma_t *)DMA2_BASE)

/* DMAMUX channel register */
typedef struct {
    volatile uint32_t CCR;    /* channel configuration */
} vk_dmamux_ch_t;
#define DMAMUX1  ((vk_dmamux_ch_t *)DMAMUX1_BASE)

/* ─── DMA CR bits ─────────────────────────────────────────────────── */
#define DMA_CR_EN       (1u <<  0)
#define DMA_CR_DMEIE    (1u <<  1)
#define DMA_CR_TEIE     (1u <<  2)   /* transfer error IE */
#define DMA_CR_HTIE     (1u <<  3)   /* half-transfer IE */
#define DMA_CR_TCIE     (1u <<  4)   /* transfer complete IE */
#define DMA_CR_PFCTRL   (1u <<  5)   /* peripheral flow control */
#define DMA_CR_DIR_P2M  (0u <<  6)   /* periph → memory */
#define DMA_CR_DIR_M2P  (1u <<  6)   /* memory → periph */
#define DMA_CR_DIR_M2M  (2u <<  6)   /* memory → memory */
#define DMA_CR_CIRC     (1u <<  8)   /* circular mode */
#define DMA_CR_PINC     (1u <<  9)   /* peripheral increment */
#define DMA_CR_MINC     (1u << 10)   /* memory increment */
#define DMA_CR_PSIZE8   (0u << 11)
#define DMA_CR_PSIZE16  (1u << 11)
#define DMA_CR_PSIZE32  (2u << 11)
#define DMA_CR_MSIZE8   (0u << 13)
#define DMA_CR_MSIZE16  (1u << 13)
#define DMA_CR_MSIZE32  (2u << 13)
#define DMA_CR_PL_LOW   (0u << 16)
#define DMA_CR_PL_MED   (1u << 16)
#define DMA_CR_PL_HIGH  (2u << 16)
#define DMA_CR_PL_VHIGH (3u << 16)
#define DMA_CR_DBM      (1u << 18)   /* double-buffer mode */
#define DMA_CR_CT       (1u << 19)   /* current target (DBM) */
#define DMA_CR_MBURST1  (0u << 23)
#define DMA_CR_MBURST4  (1u << 23)
#define DMA_CR_MBURST8  (2u << 23)

/* ─── transfer descriptor ─────────────────────────────────────────── */

typedef enum {
    VK_DMA_P2M = 0,   /* peripheral → memory (ADC, UART RX) */
    VK_DMA_M2P = 1,   /* memory → peripheral (SPI TX, UART TX) */
    VK_DMA_M2M = 2,   /* memory → memory */
} vk_dma_dir_t;

typedef enum {
    VK_DMA_WIDTH_8  = 0,
    VK_DMA_WIDTH_16 = 1,
    VK_DMA_WIDTH_32 = 2,
} vk_dma_width_t;

typedef struct {
    vk_dma_t       *controller;    /* DMA1 or DMA2 */
    uint8_t         stream;        /* 0..7 */
    uint8_t         mux_request;   /* DMAMUX1 request number (RM0433 Table 110) */
    vk_dma_dir_t    dir;
    vk_dma_width_t  pwidth;        /* peripheral data width */
    vk_dma_width_t  mwidth;        /* memory data width */
    uint8_t         pinc;          /* peripheral address increment */
    uint8_t         minc;          /* memory address increment */
    uint8_t         circular;      /* 1 = circular mode */
    uint8_t         priority;      /* 0=low .. 3=very high */
    uint32_t        irqn;          /* NVIC IRQ number for this stream */

    /* callbacks (called from ISR — keep short) */
    void (*cb_complete)(void *arg);
    void (*cb_half)(void *arg);
    void (*cb_error)(void *arg);
    void  *cb_arg;
} vk_dma_cfg_t;

/* Runtime handle */
typedef struct {
    vk_dma_t        *dma;
    vk_dma_stream_t *stream;
    uint8_t          stream_idx;
    vk_dma_cfg_t     cfg;
    volatile uint8_t busy;
} vk_dma_ch_t;

/* ─── DMAMUX request IDs (RM0433 Table 110, selected) ────────────── */
#define DMAMUX_REQ_ADC1    9
#define DMAMUX_REQ_ADC2    10
#define DMAMUX_REQ_SPI1_RX 37
#define DMAMUX_REQ_SPI1_TX 38
#define DMAMUX_REQ_SPI2_RX 39
#define DMAMUX_REQ_SPI2_TX 40
#define DMAMUX_REQ_USART1_RX 41
#define DMAMUX_REQ_USART1_TX 42
#define DMAMUX_REQ_USART3_RX 45
#define DMAMUX_REQ_USART3_TX 46
#define DMAMUX_REQ_MEM2MEM  0   /* no request needed for M2M */

/* ─── API ─────────────────────────────────────────────────────────── */

/* Initialise a DMA channel from a config struct */
vk_status_t vk_dma_init(vk_dma_ch_t *ch, const vk_dma_cfg_t *cfg);

/*
 * vk_dma_start — begin a transfer.
 *   src  : source address (memory or peripheral)
 *   dst  : destination address
 *   count: number of items (in units of mwidth)
 */
vk_status_t vk_dma_start(vk_dma_ch_t *ch,
                          uint32_t src, uint32_t dst, uint16_t count);

/* Stop and disable a DMA stream */
void vk_dma_stop(vk_dma_ch_t *ch);

/* Poll for completion (blocking — for simple use cases) */
vk_status_t vk_dma_wait(vk_dma_ch_t *ch, uint32_t timeout_ms);

/* Call from DMAx_Streamy_IRQHandler */
void vk_dma_isr(vk_dma_ch_t *ch);

/* Memory-to-memory copy using DMA (async; fires cb_complete when done) */
vk_status_t vk_dma_memcpy(vk_dma_ch_t *ch,
                           void *dst, const void *src, uint32_t bytes);

#endif /* VULCAN_DMA_H */
