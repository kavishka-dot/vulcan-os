/*
 * vulcan_adc.c — ADC1/ADC2 in continuous DMA circular mode
 * Vulcan OS
 *
 * ADC clock: sourced from PLL2P or adc_ker_ck.
 * For simplicity we use the AHB clock /1 as the ADC kernel clock
 * (set in RCC_D3CCIPR); max ADC clock on H743 = 36 MHz.
 */

#include "vulcan_adc.h"
#include "vulcan_clock.h"

/* RCC_D3CCIPR: ADC kernel clock source */
#define RCC_D3CCIPR  (*(volatile uint32_t *)(0x58024400UL + 0x4C))

/* RCC_AHB1ENR: ADC12 clock enable bit 5 */
#define RCC_AHB1ENR  (*(volatile uint32_t *)(0x58024400UL + 0xD8))
#define RCC_ADC12EN  (1u << 5)

/* ─── internal DMA callback ──────────────────────────────────────── */

/* arg = vk_adc_inst_t* */
static void _adc_dma_half(void *arg) {
    vk_adc_inst_t *inst = (vk_adc_inst_t *)arg;
    if (inst->batch_cb)
        inst->batch_cb(inst->buf,
                       VK_ADC_BUF_LEN / 2,
                       inst->cb_arg);
}

static void _adc_dma_full(void *arg) {
    vk_adc_inst_t *inst = (vk_adc_inst_t *)arg;
    if (inst->batch_cb)
        inst->batch_cb(inst->buf + VK_ADC_BUF_LEN / 2,
                       VK_ADC_BUF_LEN / 2,
                       inst->cb_arg);
}

/* ─── init ───────────────────────────────────────────────────────── */

vk_status_t vk_adc_init(vk_adc_inst_t  *inst,
                         vk_adc_t       *adc,
                         const uint8_t  *channels,
                         uint8_t         n_ch,
                         vk_adc_res_t    res,
                         vk_adc_smpr_t   smpr,
                         vk_dma_ch_t    *dma_ch)
{
    if (!inst || !adc || !channels || n_ch == 0 || n_ch > 16 || !dma_ch)
        return VK_ERR_PARAM;

    inst->adc        = adc;
    inst->dma_ch     = dma_ch;
    inst->n_channels = n_ch;
    inst->resolution = res;

    /* Enable ADC12 clock */
    RCC_AHB1ENR |= RCC_ADC12EN;
    /* Use PER clock (HSE/1) as ADC kernel clock */
    RCC_D3CCIPR &= ~(3u << 16);
    RCC_D3CCIPR |=  (2u << 16);  /* ADCSEL = per_ck (= HSE = 25 MHz) */

    /* Exit deep power-down, enable voltage regulator */
    adc->CR &= ~ADC_CR_DEEPPWD;
    adc->CR |=  ADC_CR_ADVREGEN;
    vk_delay_us(20);  /* TADCVREG_STUP ≥ 10 µs */

    /* Calibrate (single-ended) */
    adc->CR &= ~ADC_CR_ADCALDIF;
    adc->CR |=  ADC_CR_ADCAL;
    while (adc->CR & ADC_CR_ADCAL);

    /* Set BOOST for ADC clock > 20 MHz */
    adc->CR |= ADC_CR_BOOST;

    /* CFGR: DMA circular, continuous, resolution */
    adc->CFGR = ADC_CFGR_DMNGT_DMA_CIRC |
                ((uint32_t)res << 2)     |
                ADC_CFGR_CONT;

    /* Sample time: apply smpr to all channels */
    uint32_t smpr_val = 0;
    for (int i = 0; i < 10; i++)
        smpr_val |= ((uint32_t)smpr << (i * 3));
    adc->SMPR1 = smpr_val;
    adc->SMPR2 = smpr_val;

    /* Preselect channels */
    uint32_t pcsel = 0;
    for (uint8_t i = 0; i < n_ch; i++)
        pcsel |= (1u << channels[i]);
    adc->PCSEL = pcsel;

    /* Sequence: SQR1[L] = n_ch-1, then channel list */
    adc->SQR1 = (uint32_t)(n_ch - 1u);
    for (uint8_t i = 0; i < n_ch && i < 4; i++)
        adc->SQR1 |= ((uint32_t)channels[i] << (6u + i * 6u));
    for (uint8_t i = 4; i < n_ch && i < 9; i++)
        adc->SQR2 |= ((uint32_t)channels[i] << ((i - 4u) * 6u));
    for (uint8_t i = 9; i < n_ch && i < 14; i++)
        adc->SQR3 |= ((uint32_t)channels[i] << ((i - 9u) * 6u));

    /* Configure DMA channel for P2M circular */
    vk_dma_cfg_t dma_cfg = {
        .controller  = DMA1,
        .stream      = 0,
        .mux_request = DMAMUX_REQ_ADC1,
        .dir         = VK_DMA_P2M,
        .pwidth      = VK_DMA_WIDTH_16,
        .mwidth      = VK_DMA_WIDTH_16,
        .pinc        = 0,
        .minc        = 1,
        .circular    = 1,
        .priority    = 2,   /* high */
        .irqn        = 11,  /* DMA1_Stream0_IRQn */
        .cb_half     = _adc_dma_half,
        .cb_complete = _adc_dma_full,
        .cb_arg      = inst,
    };
    vk_dma_init(dma_ch, &dma_cfg);

    /* Enable ADC */
    adc->ISR |= (1u << 0);  /* clear ADRDY */
    adc->CR  |= ADC_CR_ADEN;
    while (!(adc->ISR & (1u << 0)));  /* wait ADRDY */

    return VK_OK;
}

/* ─── start/stop ─────────────────────────────────────────────────── */

void vk_adc_start(vk_adc_inst_t *inst) {
    /* Start DMA circular transfer from ADC DR */
    vk_dma_start(inst->dma_ch,
                 (uint32_t)&inst->adc->DR,
                 (uint32_t)inst->buf,
                 VK_ADC_BUF_LEN);
    /* Kick ADC */
    inst->adc->CR |= ADC_CR_ADSTART;
}

void vk_adc_stop(vk_adc_inst_t *inst) {
    inst->adc->CR |= ADC_CR_ADSTP;
    while (inst->adc->CR & ADC_CR_ADSTP);
    vk_dma_stop(inst->dma_ch);
}

/* ─── single blocking read ───────────────────────────────────────── */

uint16_t vk_adc_read_blocking(vk_adc_t *adc, uint8_t ch,
                               vk_adc_res_t res, vk_adc_smpr_t smpr)
{
    /* Exit deep power-down, enable regulator */
    adc->CR &= ~ADC_CR_DEEPPWD;
    adc->CR |=  ADC_CR_ADVREGEN;
    vk_delay_us(20);

    /* Calibrate */
    adc->CR &= ~ADC_CR_ADCALDIF;
    adc->CR |=  ADC_CR_ADCAL;
    while (adc->CR & ADC_CR_ADCAL);

    adc->CR |= ADC_CR_BOOST;

    /* Single conversion, no DMA */
    adc->CFGR  = (uint32_t)res << 2;
    adc->PCSEL = (1u << ch);
    adc->SMPR1 = (uint32_t)smpr;
    adc->SQR1  = (uint32_t)ch << 6;  /* L=0, SQ1=ch */

    /* Enable */
    adc->ISR |= 1u;
    adc->CR  |= ADC_CR_ADEN;
    while (!(adc->ISR & 1u));

    /* Start */
    adc->CR |= ADC_CR_ADSTART;
    while (!(adc->ISR & (1u << 2)));  /* EOC */

    uint16_t result = (uint16_t)adc->DR;

    /* Shutdown */
    adc->CR |= ADC_CR_ADDIS;
    while (adc->CR & ADC_CR_ADEN);

    return result;
}
