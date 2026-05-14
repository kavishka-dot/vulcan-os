/*
 * vulcan_dma.c — DMA1/DMA2 + DMAMUX1 driver for STM32H743
 * Vulcan OS
 */

#include "vulcan_dma.h"
#include "vulcan_task.h"   /* for vk_tick_get */

/* RCC AHB1ENR: DMA1=bit0, DMA2=bit1 */
#define RCC_AHB1ENR  (*(volatile uint32_t *)(0x58024400UL + 0xD8))
#define RCC_AHB1ENR_DMA1EN (1u << 0)
#define RCC_AHB1ENR_DMA2EN (1u << 1)

/* NVIC registers */
#define NVIC_ISER(n)  (*(volatile uint32_t *)(0xE000E100UL + (n)*4))
#define NVIC_IPR(n)   (*(volatile uint8_t  *)(0xE000E400UL + (n)))

static void _nvic_enable(uint32_t irqn, uint8_t prio) {
    NVIC_IPR(irqn)        = (uint8_t)(prio << 4);
    NVIC_ISER(irqn >> 5) |= (1u << (irqn & 31u));
}

/* ISR flag bit positions for each stream in LISR/HISR */
static const uint8_t _isr_shift[8] = { 0, 6, 16, 22, 0, 6, 16, 22 };

static uint32_t _get_isr(vk_dma_t *dma, uint8_t stream) {
    uint32_t reg = (stream < 4) ? dma->LISR : dma->HISR;
    return (reg >> _isr_shift[stream]) & 0x3Fu;
}

static void _clear_isr(vk_dma_t *dma, uint8_t stream) {
    uint32_t mask = (0x3Fu << _isr_shift[stream]);
    if (stream < 4) dma->LIFCR = mask;
    else            dma->HIFCR = mask;
}

/* ─── init ───────────────────────────────────────────────────────── */

vk_status_t vk_dma_init(vk_dma_ch_t *ch, const vk_dma_cfg_t *cfg) {
    if (!ch || !cfg) return VK_ERR_PARAM;
    if (cfg->stream > 7) return VK_ERR_PARAM;

    ch->dma        = cfg->controller;
    ch->stream     = &cfg->controller->S[cfg->stream];
    ch->stream_idx = cfg->stream;
    ch->cfg        = *cfg;
    ch->busy       = 0;

    /* Enable DMA controller clock */
    if (cfg->controller == DMA1)
        RCC_AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    else
        RCC_AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    /* Disable stream before configuring */
    ch->stream->CR &= ~DMA_CR_EN;
    while (ch->stream->CR & DMA_CR_EN);

    /* DMAMUX: assign request to stream
     * DMA1 streams 0-7 → DMAMUX channels 0-7
     * DMA2 streams 0-7 → DMAMUX channels 8-15 */
    uint8_t mux_ch = (cfg->controller == DMA2) ? (cfg->stream + 8) : cfg->stream;
    DMAMUX1[mux_ch].CCR = cfg->mux_request & 0x7Fu;

    /* Enable IRQ */
    if (cfg->irqn > 0)
        _nvic_enable(cfg->irqn, 5);

    return VK_OK;
}

/* ─── start ──────────────────────────────────────────────────────── */

vk_status_t vk_dma_start(vk_dma_ch_t *ch,
                          uint32_t src, uint32_t dst, uint16_t count)
{
    vk_dma_stream_t *s   = ch->stream;
    const vk_dma_cfg_t *c = &ch->cfg;

    /* Disable, clear flags */
    s->CR &= ~DMA_CR_EN;
    while (s->CR & DMA_CR_EN);
    _clear_isr(ch->dma, ch->stream_idx);

    /* Addresses */
    if (c->dir == VK_DMA_P2M) {
        s->PAR  = src;    /* peripheral source */
        s->M0AR = dst;    /* memory destination */
    } else if (c->dir == VK_DMA_M2P) {
        s->M0AR = src;    /* memory source */
        s->PAR  = dst;    /* peripheral destination */
    } else {
        /* M2M */
        s->PAR  = src;
        s->M0AR = dst;
    }

    s->NDTR = count;
    s->FCR  = 0;   /* direct mode (no FIFO buffering for now) */

    /* Build CR */
    uint32_t cr = 0;
    cr |= ((uint32_t)c->dir     << 6);
    cr |= ((uint32_t)c->pwidth  << 11);
    cr |= ((uint32_t)c->mwidth  << 13);
    cr |= ((uint32_t)c->priority<< 16);
    if (c->pinc)    cr |= DMA_CR_PINC;
    if (c->minc)    cr |= DMA_CR_MINC;
    if (c->circular) cr |= DMA_CR_CIRC;
    if (c->cb_complete) cr |= DMA_CR_TCIE;
    if (c->cb_half)     cr |= DMA_CR_HTIE;
    if (c->cb_error)    cr |= DMA_CR_TEIE;

    s->CR = cr;
    ch->busy = 1;
    s->CR |= DMA_CR_EN;

    return VK_OK;
}

/* ─── stop ───────────────────────────────────────────────────────── */

void vk_dma_stop(vk_dma_ch_t *ch) {
    ch->stream->CR &= ~DMA_CR_EN;
    while (ch->stream->CR & DMA_CR_EN);
    _clear_isr(ch->dma, ch->stream_idx);
    ch->busy = 0;
}

/* ─── wait ───────────────────────────────────────────────────────── */

vk_status_t vk_dma_wait(vk_dma_ch_t *ch, uint32_t timeout_ms) {
    vk_tick_t deadline = vk_tick_get() + timeout_ms;
    while (ch->busy) {
        if (vk_tick_get() >= deadline)
            return VK_ERR_TIMEOUT;
        /* yield if called from a task context */
        extern vk_task_t *vk_current;
        if (vk_current) vk_task_yield();
    }
    return VK_OK;
}

/* ─── ISR ────────────────────────────────────────────────────────── */

void vk_dma_isr(vk_dma_ch_t *ch) {
    uint32_t flags = _get_isr(ch->dma, ch->stream_idx);
    _clear_isr(ch->dma, ch->stream_idx);

    /* bit 5 = TCIF, bit 4 = HTIF, bit 3-0 = errors */
    if (flags & (1u << 1)) {  /* HTIF */
        if (ch->cfg.cb_half)
            ch->cfg.cb_half(ch->cfg.cb_arg);
    }
    if (flags & (1u << 5)) {  /* TCIF */
        if (!ch->cfg.circular)
            ch->busy = 0;
        if (ch->cfg.cb_complete)
            ch->cfg.cb_complete(ch->cfg.cb_arg);
    }
    if (flags & (1u << 3)) {  /* TEIF */
        ch->busy = 0;
        if (ch->cfg.cb_error)
            ch->cfg.cb_error(ch->cfg.cb_arg);
    }
}

/* ─── memcpy via DMA ─────────────────────────────────────────────── */

/* Shared M2M channel — caller must ensure exclusive access */
static vk_dma_ch_t _m2m_ch;
static uint8_t     _m2m_init = 0;

vk_status_t vk_dma_memcpy(vk_dma_ch_t *ch,
                           void *dst, const void *src, uint32_t bytes)
{
    if (!ch) {
        /* Use internal M2M channel (DMA2 stream 0) */
        if (!_m2m_init) {
            vk_dma_cfg_t cfg = {
                .controller  = DMA2,
                .stream      = 0,
                .mux_request = DMAMUX_REQ_MEM2MEM,
                .dir         = VK_DMA_M2M,
                .pwidth      = VK_DMA_WIDTH_32,
                .mwidth      = VK_DMA_WIDTH_32,
                .pinc        = 1,
                .minc        = 1,
                .circular    = 0,
                .priority    = 1,
                .irqn        = 56,  /* DMA2_Stream0_IRQn */
            };
            vk_dma_init(&_m2m_ch, &cfg);
            _m2m_init = 1;
        }
        ch = &_m2m_ch;
    }

    return vk_dma_start(ch, (uint32_t)src, (uint32_t)dst,
                        (uint16_t)((bytes + 3u) / 4u));
}
