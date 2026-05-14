#ifndef VULCAN_SPI_H
#define VULCAN_SPI_H

/*
 * vulcan_spi.h — SPI master driver for STM32H743
 * Vulcan OS
 *
 * STM32H743 SPI is a full-duplex synchronous serial interface.
 * The H7 SPI peripheral has a built-in FIFO (16 bytes TX, 16 bytes RX).
 *
 * Two transfer modes:
 *   • Blocking (polling) — simple, for low-frequency config transactions
 *   • DMA-driven         — for bulk model weight streaming (MB-range)
 *
 * Typical Vulcan uses:
 *   SPI1 → external NOR flash (W25Q128, 128 Mbit) for model storage
 *   SPI2 → ADALM-Pluto or custom SDR front-end (IQ data)
 *   SPI3 → auxiliary (IMU, pressure sensor, etc.)
 */

#include "vulcan.h"
#include "vulcan_dma.h"
#include "vulcan_gpio.h"
#include "vulcan_mutex.h"

/* ─── SPI base addresses ──────────────────────────────────────────── */
#define SPI1_BASE  0x40013000UL
#define SPI2_BASE  0x40003800UL
#define SPI3_BASE  0x40003C00UL
#define SPI4_BASE  0x40013400UL
#define SPI5_BASE  0x40015000UL
#define SPI6_BASE  0x58001400UL

/* SPI register layout */
typedef struct {
    volatile uint32_t CR1;      /* control 1 */
    volatile uint32_t CR2;      /* control 2 */
    volatile uint32_t CFG1;     /* configuration 1 */
    volatile uint32_t CFG2;     /* configuration 2 */
    volatile uint32_t IER;      /* interrupt enable */
    volatile uint32_t SR;       /* status */
    volatile uint32_t IFCR;     /* interrupt flag clear */
    uint32_t          _res0;
    volatile uint32_t TXDR;     /* TX data (byte/halfword/word access) */
    uint32_t          _res1[3];
    volatile uint32_t RXDR;     /* RX data */
    uint32_t          _res2[3];
    volatile uint32_t CRCPOLY;
    volatile uint32_t TXCRC;
    volatile uint32_t RXCRC;
    volatile uint32_t UDRDR;
} vk_spi_t;

#define SPI1  ((vk_spi_t *)SPI1_BASE)
#define SPI2  ((vk_spi_t *)SPI2_BASE)
#define SPI3  ((vk_spi_t *)SPI3_BASE)

/* SPI CR1 bits */
#define SPI_CR1_SPE     (1u <<  0)   /* SPI enable */
#define SPI_CR1_MASRX   (1u <<  8)
#define SPI_CR1_CSTART  (1u <<  9)   /* start transfer */
#define SPI_CR1_CSUSP   (1u << 10)   /* suspend */
#define SPI_CR1_HDDIR   (1u << 11)   /* half-duplex direction */
#define SPI_CR1_SSI     (1u << 12)

/* SPI CFG1 bits */
#define SPI_CFG1_DSIZE(n)  ((uint32_t)((n)-1) << 0)   /* data size = n bits */
#define SPI_CFG1_FTHLV(n)  ((uint32_t)((n)-1) << 5)   /* FIFO threshold */
#define SPI_CFG1_TXDMAEN   (1u << 15)
#define SPI_CFG1_RXDMAEN   (1u << 14)
#define SPI_CFG1_MBR(n)    ((uint32_t)(n) << 28)       /* baud rate divider: 0=/2 .. 7=/256 */

/* SPI CFG2 bits */
#define SPI_CFG2_CPOL      (1u << 25)   /* clock polarity */
#define SPI_CFG2_CPHA      (1u << 24)   /* clock phase */
#define SPI_CFG2_LSBFRST   (1u << 23)   /* LSB first */
#define SPI_CFG2_MASTER    (1u << 22)   /* master mode */
#define SPI_CFG2_SP(n)     ((uint32_t)(n) << 19)  /* serial protocol */
#define SPI_CFG2_COMM_FD   (0u << 17)   /* full-duplex */
#define SPI_CFG2_COMM_TX   (3u << 17)   /* TX-only simplex */
#define SPI_CFG2_SSM       (1u << 26)   /* software NSS management */
#define SPI_CFG2_SSOM      (1u << 30)
#define SPI_CFG2_AFCNTR    (1u << 31)   /* AF controlled by SPI (not GPIO) */

/* SPI SR bits */
#define SPI_SR_TXP    (1u <<  1)   /* TX packet space available */
#define SPI_SR_RXP    (1u <<  0)   /* RX packet available */
#define SPI_SR_EOT    (1u <<  3)   /* end of transfer */
#define SPI_SR_TXTF   (1u <<  4)   /* TX transfer filled */
#define SPI_SR_OVR    (1u << 11)   /* overrun */

/* ─── SPI mode ────────────────────────────────────────────────────── */
typedef enum {
    VK_SPI_MODE0 = 0,   /* CPOL=0 CPHA=0 */
    VK_SPI_MODE1 = 1,   /* CPOL=0 CPHA=1 */
    VK_SPI_MODE2 = 2,   /* CPOL=1 CPHA=0 */
    VK_SPI_MODE3 = 3,   /* CPOL=1 CPHA=1 */
} vk_spi_mode_t;

/* Baud rate divider (SPI kernel clock / divisor) */
typedef enum {
    VK_SPI_DIV2   = 0,
    VK_SPI_DIV4   = 1,
    VK_SPI_DIV8   = 2,
    VK_SPI_DIV16  = 3,
    VK_SPI_DIV32  = 4,
    VK_SPI_DIV64  = 5,
    VK_SPI_DIV128 = 6,
    VK_SPI_DIV256 = 7,
} vk_spi_div_t;

/* ─── SPI instance ────────────────────────────────────────────────── */
typedef struct {
    vk_spi_t     *spi;
    vk_pin_t      cs_pin;        /* software CS (GPIO) */
    vk_dma_ch_t  *dma_tx;       /* optional TX DMA channel */
    vk_dma_ch_t  *dma_rx;       /* optional RX DMA channel */
    vk_mutex_t    mutex;         /* bus ownership */
    volatile uint8_t dma_done;  /* flag set by DMA complete callback */
} vk_spi_inst_t;

/* ─── API ─────────────────────────────────────────────────────────── */

/*
 * vk_spi_init — configure SPI peripheral as master.
 *
 *   inst      : SPI instance to populate
 *   spi       : SPI1, SPI2, or SPI3
 *   mode      : clock polarity/phase
 *   div       : baud rate divisor
 *   data_bits : 8 or 16
 *   cs_pin    : GPIO pin for software chip select
 *   dma_tx    : DMA channel for TX (NULL = polling only)
 *   dma_rx    : DMA channel for RX (NULL = polling only)
 */
vk_status_t vk_spi_init(vk_spi_inst_t *inst,
                         vk_spi_t      *spi,
                         vk_spi_mode_t  mode,
                         vk_spi_div_t   div,
                         uint8_t        data_bits,
                         vk_pin_t       cs_pin,
                         vk_dma_ch_t   *dma_tx,
                         vk_dma_ch_t   *dma_rx);

/* Assert (low) / deassert (high) CS */
VULCAN_INLINE void vk_spi_cs_assert  (vk_spi_inst_t *s) { vk_gpio_clear(s->cs_pin); }
VULCAN_INLINE void vk_spi_cs_release (vk_spi_inst_t *s) { vk_gpio_set(s->cs_pin); }

/* Blocking full-duplex byte transfer (simultaneous TX+RX) */
uint8_t vk_spi_xfer(vk_spi_inst_t *inst, uint8_t tx);

/* Blocking write-only burst */
void vk_spi_write(vk_spi_inst_t *inst, const uint8_t *tx, uint16_t len);

/* Blocking read burst (sends 0xFF as dummy TX) */
void vk_spi_read(vk_spi_inst_t *inst, uint8_t *rx, uint16_t len);

/* DMA-driven full-duplex transfer (non-blocking; fires callback on completion) */
vk_status_t vk_spi_xfer_dma(vk_spi_inst_t *inst,
                              const uint8_t *tx, uint8_t *rx,
                              uint16_t len,
                              void (*cb)(void *arg), void *arg);

/* Blocking wait for DMA transfer to finish */
vk_status_t vk_spi_wait(vk_spi_inst_t *inst, uint32_t timeout_ms);

/* Acquire / release bus mutex (for multi-device SPI bus) */
void vk_spi_acquire(vk_spi_inst_t *inst);
void vk_spi_release(vk_spi_inst_t *inst);

#endif /* VULCAN_SPI_H */
