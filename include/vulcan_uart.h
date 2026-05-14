#ifndef VULCAN_UART_H
#define VULCAN_UART_H

/*
 * vulcan_uart.h — UART driver for STM32H743
 * Vulcan OS
 *
 * Supports USART1..USART3, UART4..UART8.
 * Two modes per instance:
 *   • TX: interrupt-driven with circular buffer (non-blocking for tasks)
 *   • RX: DMA circular buffer with line-available callback
 *
 * Default debug port: USART3 (D8/D9 on Nucleo-H743ZI2 ST-Link virtual COM)
 */

#include "vulcan.h"
#include "vulcan_mutex.h"

/* ─── UART peripheral base addresses ─────────────────────────────── */
#define USART1_BASE  0x40011000UL
#define USART2_BASE  0x40004400UL
#define USART3_BASE  0x40004800UL
#define UART4_BASE   0x40004C00UL
#define UART5_BASE   0x40005000UL
#define USART6_BASE  0x40011400UL
#define UART7_BASE   0x40007800UL
#define UART8_BASE   0x40007C00UL

/* UART register offsets */
#define UART_CR1    0x00
#define UART_CR2    0x04
#define UART_CR3    0x08
#define UART_BRR    0x0C
#define UART_ISR    0x1C
#define UART_ICR    0x20
#define UART_RDR    0x24
#define UART_TDR    0x28

/* ISR flags */
#define UART_ISR_TXE   (1u <<  7)   /* TX data register empty */
#define UART_ISR_TC    (1u <<  6)   /* transmission complete */
#define UART_ISR_RXNE  (1u <<  5)   /* RX not empty */
#define UART_ISR_ORE   (1u <<  3)   /* overrun error */
#define UART_ISR_IDLE  (1u <<  4)   /* idle line detected */

/* CR1 bits */
#define UART_CR1_UE    (1u <<  0)   /* UART enable */
#define UART_CR1_RE    (1u <<  2)   /* receiver enable */
#define UART_CR1_TE    (1u <<  3)   /* transmitter enable */
#define UART_CR1_RXNEIE (1u << 5)   /* RX interrupt enable */
#define UART_CR1_TCIE  (1u <<  6)   /* TC interrupt enable */
#define UART_CR1_TXEIE (1u <<  7)   /* TXE interrupt enable */
#define UART_CR1_IDLEIE (1u << 4)   /* IDLE interrupt enable */
#define UART_CR1_OVER8 (1u << 15)   /* oversampling by 8 */
#define UART_CR1_FIFOEN (1u << 29)  /* FIFO mode enable (H7 only) */

/* ─── TX ring buffer size ─────────────────────────────────────────── */
#define VK_UART_TX_BUF  256u
#define VK_UART_RX_BUF  256u

/* ─── UART instance descriptor ────────────────────────────────────── */
typedef struct {
    /* hardware */
    volatile uint32_t  *base;        /* peripheral base address */
    uint32_t            irqn;        /* NVIC IRQ number */
    uint32_t            pclk;        /* peripheral clock Hz */

    /* TX ring buffer (interrupt-driven) */
    uint8_t             tx_buf[VK_UART_TX_BUF];
    volatile uint16_t   tx_head;     /* write index */
    volatile uint16_t   tx_tail;     /* read index (ISR advances) */
    volatile uint8_t    tx_busy;     /* 1 while ISR draining buffer */

    /* RX ring buffer (filled by ISR or DMA) */
    uint8_t             rx_buf[VK_UART_RX_BUF];
    volatile uint16_t   rx_head;
    volatile uint16_t   rx_tail;

    /* optional RX line callback */
    void (*rx_cb)(const uint8_t *line, uint16_t len);

    vk_mutex_t          tx_mutex;    /* multi-task TX serialisation */
} vk_uart_t;

/* ─── pre-declared instances (defined in vulcan_uart.c) ──────────── */
extern vk_uart_t vk_uart1;
extern vk_uart_t vk_uart3;   /* default debug port */

/* ─── API ─────────────────────────────────────────────────────────── */

/*
 * vk_uart_init — configure UART peripheral and GPIO pins.
 *
 *   uart     : pointer to a vk_uart_t instance (use &vk_uart3 for debug)
 *   base     : peripheral base (e.g. USART3_BASE)
 *   baud     : baud rate (e.g. 115200)
 *   pclk_hz  : APB clock feeding this UART (typically 120 MHz on H743)
 */
void vk_uart_init(vk_uart_t *uart, uint32_t base,
                  uint32_t baud, uint32_t pclk_hz);

/*
 * vk_uart_putc — enqueue one byte; blocks if TX buffer full.
 */
void vk_uart_putc(vk_uart_t *uart, uint8_t c);

/*
 * vk_uart_puts — send null-terminated string.
 */
void vk_uart_puts(vk_uart_t *uart, const char *s);

/*
 * vk_uart_write — send raw bytes.
 */
void vk_uart_write(vk_uart_t *uart, const uint8_t *data, uint16_t len);

/*
 * vk_uart_printf — formatted print (no malloc; stack buffer 128 B).
 */
void vk_uart_printf(vk_uart_t *uart, const char *fmt, ...);

/*
 * vk_uart_getc — read one byte; returns -1 if RX buffer empty.
 */
int  vk_uart_getc(vk_uart_t *uart);

/*
 * vk_uart_readline — block until '\n' received; fills buf (max len-1 chars).
 * Returns number of bytes written (excluding null terminator).
 */
uint16_t vk_uart_readline(vk_uart_t *uart, char *buf, uint16_t len);

/*
 * vk_uart_isr — call from USARTx_IRQHandler in your IRQ table.
 */
void vk_uart_isr(vk_uart_t *uart);

/* ─── debug convenience macros ────────────────────────────────────── */
#define VK_LOG(fmt, ...)  vk_uart_printf(&vk_uart3, "[VK] " fmt "\r\n", ##__VA_ARGS__)
#define VK_LOGRAW(fmt, ...) vk_uart_printf(&vk_uart3, fmt, ##__VA_ARGS__)

#endif /* VULCAN_UART_H */
