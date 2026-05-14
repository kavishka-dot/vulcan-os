/*
 * vulcan_spi.c — SPI master driver for STM32H743
 * Vulcan OS
 *
 * SPI1 pinout (Nucleo-H743ZI2 Arduino header):
 *   PA5  = SPI1_SCK  (AF5)
 *   PA6  = SPI1_MISO (AF5)
 *   PA7  = SPI1_MOSI (AF5)
 *   Software CS via GPIO (caller configures cs_pin)
 */

#include "vulcan_spi.h"
#include "vulcan_clock.h"

/* RCC APB2ENR: SPI1 bit 12 */
#define RCC_APB2ENR  (*(volatile uint32_t *)(0x58024400UL + 0xF0))
/* RCC APB1LENR: SPI2 bit 14, SPI3 bit 15 */
#define RCC_APB1LENR (*(volatile uint32_t *)(0x58024400UL + 0xE8))

/* ─── init ───────────────────────────────────────────────────────── */

vk_status_t vk_spi_init(vk_spi_inst_t *inst,
                         vk_spi_t      *spi,
                         vk_spi_mode_t  mode,
                         vk_spi_div_t   div,
                         uint8_t        data_bits,
                         vk_pin_t       cs_pin,
                         vk_dma_ch_t   *dma_tx,
                         vk_dma_ch_t   *dma_rx)
{
    inst->spi      = spi;
    inst->cs_pin   = cs_pin;
    inst->dma_tx   = dma_tx;
    inst->dma_rx   = dma_rx;
    inst->dma_done = 0;
    vk_mutex_init(&inst->mutex);

    /* Enable peripheral clock */
    if (spi == SPI1)
        RCC_APB2ENR  |= (1u << 12);
    else if (spi == SPI2)
        RCC_APB1LENR |= (1u << 14);
    else if (spi == SPI3)
        RCC_APB1LENR |= (1u << 15);

    /* GPIO for SPI1 */
    if (spi == SPI1) {
        vk_gpio_clk_enable(GPIOA);
        vk_pin_t sck  = VK_PIN(GPIOA, 5);
        vk_pin_t miso = VK_PIN(GPIOA, 6);
        vk_pin_t mosi = VK_PIN(GPIOA, 7);
        vk_gpio_config(sck,  VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_VHIGH, VK_GPIO_NO_PULL);
        vk_gpio_config(miso, VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_VHIGH, VK_GPIO_PULL_UP);
        vk_gpio_config(mosi, VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_VHIGH, VK_GPIO_NO_PULL);
        vk_gpio_set_af(sck,  5);
        vk_gpio_set_af(miso, 5);
        vk_gpio_set_af(mosi, 5);
    }

    /* CS pin: output high (deasserted) */
    vk_gpio_output(cs_pin, VK_GPIO_SPEED_HIGH);
    vk_gpio_set(cs_pin);

    /* Disable SPI before config */
    spi->CR1 &= ~SPI_CR1_SPE;

    /* CFG1: data size, FIFO threshold, baud rate */
    spi->CFG1 = SPI_CFG1_DSIZE(data_bits) |
                SPI_CFG1_FTHLV(1)          |   /* threshold = 1 frame */
                SPI_CFG1_MBR(div);

    /* CFG2: master, mode, software NSS, full-duplex */
    uint32_t cfg2 = SPI_CFG2_MASTER |
                    SPI_CFG2_SSM    |
                    SPI_CFG2_COMM_FD|
                    SPI_CFG2_AFCNTR;
    if (mode & 1) cfg2 |= SPI_CFG2_CPHA;
    if (mode & 2) cfg2 |= SPI_CFG2_CPOL;
    spi->CFG2 = cfg2;

    /* SSI must be high in master/SSM mode */
    spi->CR1 = SPI_CR1_SSI;

    /* Enable SPI */
    spi->CR1 |= SPI_CR1_SPE;

    return VK_OK;
}

/* ─── blocking xfer (full-duplex, 1 byte) ────────────────────────── */

uint8_t vk_spi_xfer(vk_spi_inst_t *inst, uint8_t tx) {
    vk_spi_t *spi = inst->spi;

    /* Start transfer */
    spi->CR1 |= SPI_CR1_CSTART;

    /* Wait for TXP (TX FIFO has space) */
    while (!(spi->SR & SPI_SR_TXP));
    *(volatile uint8_t *)&spi->TXDR = tx;

    /* Wait for RXP (RX FIFO has data) */
    while (!(spi->SR & SPI_SR_RXP));
    uint8_t rx = *(volatile uint8_t *)&spi->RXDR;

    /* Wait for end of transfer */
    while (!(spi->SR & SPI_SR_EOT));
    spi->IFCR = SPI_SR_EOT | SPI_SR_TXTF;

    return rx;
}

/* ─── burst write ────────────────────────────────────────────────── */

void vk_spi_write(vk_spi_inst_t *inst, const uint8_t *tx, uint16_t len) {
    vk_spi_t *spi = inst->spi;
    spi->CR2 = len;          /* number of transfers */
    spi->CR1 |= SPI_CR1_CSTART;

    for (uint16_t i = 0; i < len; i++) {
        while (!(spi->SR & SPI_SR_TXP));
        *(volatile uint8_t *)&spi->TXDR = tx[i];
        /* drain RX FIFO to prevent overflow */
        if (spi->SR & SPI_SR_RXP)
            (void)*(volatile uint8_t *)&spi->RXDR;
    }
    while (!(spi->SR & SPI_SR_EOT));
    spi->IFCR = SPI_SR_EOT | SPI_SR_TXTF;
}

/* ─── burst read ─────────────────────────────────────────────────── */

void vk_spi_read(vk_spi_inst_t *inst, uint8_t *rx, uint16_t len) {
    vk_spi_t *spi = inst->spi;
    spi->CR2 = len;
    spi->CR1 |= SPI_CR1_CSTART;

    for (uint16_t i = 0; i < len; i++) {
        while (!(spi->SR & SPI_SR_TXP));
        *(volatile uint8_t *)&spi->TXDR = 0xFF;   /* dummy TX */
        while (!(spi->SR & SPI_SR_RXP));
        rx[i] = *(volatile uint8_t *)&spi->RXDR;
    }
    while (!(spi->SR & SPI_SR_EOT));
    spi->IFCR = SPI_SR_EOT | SPI_SR_TXTF;
}

/* ─── DMA-driven transfer ────────────────────────────────────────── */

static void _spi_dma_done(void *arg) {
    vk_spi_inst_t *inst = (vk_spi_inst_t *)arg;
    inst->dma_done = 1;
}

vk_status_t vk_spi_xfer_dma(vk_spi_inst_t *inst,
                              const uint8_t *tx, uint8_t *rx,
                              uint16_t len,
                              void (*cb)(void *arg), void *arg)
{
    if (!inst->dma_tx || !inst->dma_rx) return VK_ERR_PARAM;

    inst->dma_done = 0;

    /* Enable DMA requests in SPI */
    inst->spi->CFG1 |= SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN;

    /* RX DMA: SPI DR → rx buffer */
    inst->dma_rx->cfg.cb_complete = cb ? cb : _spi_dma_done;
    inst->dma_rx->cfg.cb_arg      = cb ? arg : inst;
    vk_dma_start(inst->dma_rx,
                 (uint32_t)&inst->spi->RXDR,
                 (uint32_t)rx, len);

    /* TX DMA: tx buffer → SPI DR */
    vk_dma_start(inst->dma_tx,
                 (uint32_t)tx,
                 (uint32_t)&inst->spi->TXDR, len);

    inst->spi->CR2 = len;
    inst->spi->CR1 |= SPI_CR1_CSTART;

    return VK_OK;
}

vk_status_t vk_spi_wait(vk_spi_inst_t *inst, uint32_t timeout_ms) {
    vk_tick_t deadline = vk_tick_get() + timeout_ms;
    while (!inst->dma_done) {
        if (vk_tick_get() >= deadline) return VK_ERR_TIMEOUT;
        vk_task_yield();
    }
    while (!(inst->spi->SR & SPI_SR_EOT));
    inst->spi->IFCR = SPI_SR_EOT | SPI_SR_TXTF;
    inst->spi->CFG1 &= ~(SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);
    return VK_OK;
}

/* ─── bus mutex ──────────────────────────────────────────────────── */

void vk_spi_acquire(vk_spi_inst_t *inst) { vk_mutex_lock(&inst->mutex); }
void vk_spi_release(vk_spi_inst_t *inst) { vk_mutex_unlock(&inst->mutex); }
