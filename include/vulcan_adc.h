#ifndef VULCAN_ADC_H
#define VULCAN_ADC_H

/*
 * vulcan_adc.h — ADC1/ADC2 driver for STM32H743
 * Vulcan OS
 *
 * Operates in continuous DMA mode: ADC samples continuously into a
 * circular DMA buffer. The half/full-complete DMA callbacks give the
 * ML pipeline a fresh batch of samples every half-buffer period without
 * any CPU involvement in the sampling loop.
 *
 * Resolution: 16-bit (maximum on H743 ADC3) or 12-bit (ADC1/2)
 * Reference: VREF+ = 3.3V
 * Typical use: IQ samples from an envelope detector, microphone, IMU
 */

#include "vulcan.h"
#include "vulcan_dma.h"

/* ─── ADC base addresses ──────────────────────────────────────────── */
#define ADC1_BASE    0x40022000UL
#define ADC2_BASE    0x40022100UL
#define ADC12_CCR    (*(volatile uint32_t *)(0x40022300UL + 0x08))

/* ADC register offsets */
typedef struct {
    volatile uint32_t ISR;      /* interrupt and status */
    volatile uint32_t IER;      /* interrupt enable */
    volatile uint32_t CR;       /* control */
    volatile uint32_t CFGR;     /* configuration */
    volatile uint32_t CFGR2;    /* configuration 2 */
    volatile uint32_t SMPR1;    /* sample time 1 */
    volatile uint32_t SMPR2;    /* sample time 2 */
    volatile uint32_t PCSEL;    /* channel preselection */
    volatile uint32_t LTR1;     /* watchdog threshold low */
    volatile uint32_t HTR1;     /* watchdog threshold high */
    uint32_t          _res0[2];
    volatile uint32_t SQR1;     /* regular sequence 1 */
    volatile uint32_t SQR2;
    volatile uint32_t SQR3;
    volatile uint32_t SQR4;
    volatile uint32_t DR;       /* regular data */
} vk_adc_t;

#define ADC1  ((vk_adc_t *)ADC1_BASE)
#define ADC2  ((vk_adc_t *)ADC2_BASE)

/* ADC CR bits */
#define ADC_CR_ADEN     (1u <<  0)
#define ADC_CR_ADDIS    (1u <<  1)
#define ADC_CR_ADSTART  (1u <<  2)
#define ADC_CR_ADSTP    (1u <<  4)
#define ADC_CR_BOOST    (3u <<  8)   /* boost mode for fADC > 20 MHz */
#define ADC_CR_ADCAL    (1u << 31)
#define ADC_CR_ADCALDIF (1u << 30)
#define ADC_CR_DEEPPWD  (1u << 29)
#define ADC_CR_ADVREGEN (1u << 28)

/* ADC CFGR bits */
#define ADC_CFGR_DMNGT_DMA_CIRC  (3u <<  0)   /* DMA circular mode */
#define ADC_CFGR_RES_12BIT       (0u <<  2)
#define ADC_CFGR_RES_14BIT       (1u <<  2)
#define ADC_CFGR_RES_16BIT       (2u <<  2)
#define ADC_CFGR_RES_8BIT        (3u <<  2)
#define ADC_CFGR_EXTSEL(n)       ((n) << 5)
#define ADC_CFGR_EXTEN_NONE      (0u << 10)
#define ADC_CFGR_EXTEN_RISING    (1u << 10)
#define ADC_CFGR_CONT            (1u << 13)   /* continuous mode */
#define ADC_CFGR_DISCEN          (1u << 16)
#define ADC_CFGR_OVSR_NONE       (0u << 0)

/* ─── ADC channel config ──────────────────────────────────────────── */

typedef enum {
    VK_ADC_RES_8   = 3,
    VK_ADC_RES_12  = 0,
    VK_ADC_RES_14  = 1,
    VK_ADC_RES_16  = 2,
} vk_adc_res_t;

typedef enum {
    VK_ADC_SMPR_1_5   = 0,
    VK_ADC_SMPR_2_5   = 1,
    VK_ADC_SMPR_8_5   = 2,
    VK_ADC_SMPR_16_5  = 3,
    VK_ADC_SMPR_32_5  = 4,
    VK_ADC_SMPR_64_5  = 5,
    VK_ADC_SMPR_387_5 = 6,
    VK_ADC_SMPR_810_5 = 7,
} vk_adc_smpr_t;

/* ─── ADC instance ────────────────────────────────────────────────── */

#define VK_ADC_MAX_CH   16
#define VK_ADC_BUF_LEN  256   /* samples per DMA buffer (half used per callback) */

typedef struct {
    vk_adc_t      *adc;
    vk_dma_ch_t   *dma_ch;

    /* DMA sample buffer (uint16_t for 12/16-bit; placed in SRAM accessible by DMA) */
    uint16_t        buf[VK_ADC_BUF_LEN];
    uint8_t         n_channels;     /* channels in scan sequence */
    vk_adc_res_t    resolution;

    /*
     * Batch callback — called from DMA half/full ISR with a pointer to
     * VK_ADC_BUF_LEN/2 fresh samples. n = samples per channel × n_channels.
     * Use this to feed the ML pipeline.
     */
    void (*batch_cb)(const uint16_t *samples, uint16_t n, void *arg);
    void  *cb_arg;
} vk_adc_inst_t;

/* ─── API ─────────────────────────────────────────────────────────── */

/*
 * vk_adc_init — configure ADC in continuous DMA circular mode.
 *
 *   inst       : ADC instance to populate
 *   adc        : ADC1 or ADC2
 *   channels   : array of channel numbers (0..19) to scan
 *   n_channels : length of channels array
 *   res        : sample resolution
 *   smpr       : sample time (same for all channels)
 *   dma_ch     : pre-init'd DMA channel for P2M transfers
 */
vk_status_t vk_adc_init(vk_adc_inst_t  *inst,
                         vk_adc_t       *adc,
                         const uint8_t  *channels,
                         uint8_t         n_channels,
                         vk_adc_res_t    res,
                         vk_adc_smpr_t   smpr,
                         vk_dma_ch_t    *dma_ch);

/* Start continuous sampling */
void vk_adc_start(vk_adc_inst_t *inst);

/* Stop sampling */
void vk_adc_stop(vk_adc_inst_t *inst);

/* Single blocking conversion on one channel (for calibration/startup) */
uint16_t vk_adc_read_blocking(vk_adc_t *adc, uint8_t channel,
                               vk_adc_res_t res, vk_adc_smpr_t smpr);

/* DMA half/full callback — call from DMA ISR */
void vk_adc_dma_cb(const uint16_t *half_buf, uint16_t n, void *arg);

#endif /* VULCAN_ADC_H */
