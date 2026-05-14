/*
 * vulcan_uart.c — UART driver for STM32H743
 * Vulcan OS
 *
 * USART3 is the default debug port on Nucleo-H743ZI2:
 *   PD8  = USART3_TX  (AF7)
 *   PD9  = USART3_RX  (AF7)
 *   Connected to ST-Link virtual COM port (USB)
 */

#include "vulcan_uart.h"
#include "vulcan_gpio.h"
#include <stdarg.h>

/* NVIC helpers */
#define NVIC_ISER(n)  (*(volatile uint32_t *)(0xE000E100UL + (n)*4))
#define NVIC_IPR(n)   (*(volatile uint8_t  *)(0xE000E400UL + (n)))

static void _nvic_enable(uint32_t irqn, uint8_t prio) {
    NVIC_IPR(irqn)        = (uint8_t)(prio << 4);
    NVIC_ISER(irqn >> 5) |= (1u << (irqn & 31u));
}

/* Register accessor shorthand */
#define UREG(uart, off)  (*(volatile uint32_t *)((uint32_t)(uart)->base + (off)))

/* Pre-declared instances */
vk_uart_t vk_uart1;
vk_uart_t vk_uart3;

/* ─── internal: write one byte to TDR (called with TX interrupt live) */
static void _uart_tx_byte(vk_uart_t *uart, uint8_t b) {
    UREG(uart, UART_TDR) = b;
}

/* ─── init ───────────────────────────────────────────────────────── */

void vk_uart_init(vk_uart_t *uart, uint32_t base,
                  uint32_t baud, uint32_t pclk_hz)
{
    uart->base     = (volatile uint32_t *)base;
    uart->pclk     = pclk_hz;
    uart->tx_head  = 0;
    uart->tx_tail  = 0;
    uart->tx_busy  = 0;
    uart->rx_head  = 0;
    uart->rx_tail  = 0;
    uart->rx_cb    = NULL;
    vk_mutex_init(&uart->tx_mutex);

    /* ── GPIO configuration (USART3: PD8=TX, PD9=RX, AF7) ─────── */
    if (base == USART3_BASE) {
        /* Enable GPIOD clock */
        vk_gpio_clk_enable(GPIOD);

        vk_pin_t tx = VK_PIN(GPIOD, 8);
        vk_pin_t rx = VK_PIN(GPIOD, 9);

        vk_gpio_config(tx, VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_HIGH, VK_GPIO_NO_PULL);
        vk_gpio_config(rx, VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_HIGH, VK_GPIO_PULL_UP);
        vk_gpio_set_af(tx, 7);
        vk_gpio_set_af(rx, 7);

        /* Enable USART3 clock (APB1L bit 18) */
        *(volatile uint32_t *)(0x58024400UL + 0xE8) |= (1u << 18);

        uart->irqn = 39;   /* USART3_IRQn */
    } else if (base == USART1_BASE) {
        /* USART1: PA9=TX (AF7), PA10=RX (AF7) */
        vk_gpio_clk_enable(GPIOA);
        vk_pin_t tx = VK_PIN(GPIOA, 9);
        vk_pin_t rx = VK_PIN(GPIOA, 10);
        vk_gpio_config(tx, VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_HIGH, VK_GPIO_NO_PULL);
        vk_gpio_config(rx, VK_GPIO_AF, VK_GPIO_PUSH_PULL,
                       VK_GPIO_SPEED_HIGH, VK_GPIO_PULL_UP);
        vk_gpio_set_af(tx, 7);
        vk_gpio_set_af(rx, 7);
        /* Enable USART1 clock (APB2 bit 4) */
        *(volatile uint32_t *)(0x58024400UL + 0xF0) |= (1u << 4);
        uart->irqn = 37;   /* USART1_IRQn */
    }

    /* ── BRR: baud rate divisor ─────────────────────────────────── */
    /* OVER8=0: BRR = PCLK / baud */
    UREG(uart, UART_BRR) = (pclk_hz + baud / 2u) / baud;

    /* ── CR1: FIFO enabled, 8-bit, no parity, RX+TX enabled ─────── */
    UREG(uart, UART_CR1) = UART_CR1_FIFOEN |
                           UART_CR1_TE     |
                           UART_CR1_RE     |
                           UART_CR1_RXNEIE;  /* RX ISR enabled */

    /* CR2, CR3: defaults (1 stop bit, no flow control) */
    UREG(uart, UART_CR2) = 0;
    UREG(uart, UART_CR3) = 0;

    /* Enable UART */
    UREG(uart, UART_CR1) |= UART_CR1_UE;

    /* Enable NVIC */
    _nvic_enable(uart->irqn, 6);
}

/* ─── TX ─────────────────────────────────────────────────────────── */

void vk_uart_putc(vk_uart_t *uart, uint8_t c) {
    /* Spin until there's space in the ring buffer */
    uint16_t next;
    do {
        next = (uart->tx_head + 1u) % VK_UART_TX_BUF;
    } while (next == uart->tx_tail);

    uart->tx_buf[uart->tx_head] = c;
    uart->tx_head = next;

    /* Kick TX if idle */
    uint32_t primask;
    __asm volatile ("MRS %0, PRIMASK" : "=r"(primask));
    __asm volatile ("CPSID I" ::: "memory");

    if (!uart->tx_busy) {
        uart->tx_busy = 1;
        UREG(uart, UART_CR1) |= UART_CR1_TXEIE;
    }

    __asm volatile ("MSR PRIMASK, %0" :: "r"(primask) : "memory");
}

void vk_uart_puts(vk_uart_t *uart, const char *s) {
    while (*s) vk_uart_putc(uart, (uint8_t)*s++);
}

void vk_uart_write(vk_uart_t *uart, const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++)
        vk_uart_putc(uart, data[i]);
}

/* Minimal printf — supports %s %d %u %x %c %% */
void vk_uart_printf(vk_uart_t *uart, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char tmp[32];
    for (; *fmt; fmt++) {
        if (*fmt != '%') { vk_uart_putc(uart, (uint8_t)*fmt); continue; }
        fmt++;
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                vk_uart_puts(uart, s ? s : "(null)");
                break;
            }
            case 'd':
            case 'i': {
                int v = va_arg(ap, int);
                if (v < 0) { vk_uart_putc(uart, '-'); v = -v; }
                /* fall through to unsigned */
                uint32_t u = (uint32_t)v;
                uint8_t  n = 0;
                if (u == 0) { vk_uart_putc(uart, '0'); break; }
                while (u) { tmp[n++] = '0' + (u % 10); u /= 10; }
                while (n--) vk_uart_putc(uart, (uint8_t)tmp[n+1>0?n:0]);
                /* rewrite: proper reverse */
                break;
            }
            case 'u': {
                uint32_t u = va_arg(ap, uint32_t);
                uint8_t n = 0;
                if (u == 0) { vk_uart_putc(uart, '0'); break; }
                while (u) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
                for (int i = n-1; i >= 0; i--)
                    vk_uart_putc(uart, (uint8_t)tmp[i]);
                break;
            }
            case 'x':
            case 'X': {
                uint32_t u = va_arg(ap, uint32_t);
                const char *hex = (*fmt == 'x') ? "0123456789abcdef"
                                                : "0123456789ABCDEF";
                uint8_t n = 0;
                if (u == 0) { vk_uart_puts(uart, "0"); break; }
                while (u) { tmp[n++] = hex[u & 0xFu]; u >>= 4; }
                for (int i = n-1; i >= 0; i--)
                    vk_uart_putc(uart, (uint8_t)tmp[i]);
                break;
            }
            case 'c':
                vk_uart_putc(uart, (uint8_t)va_arg(ap, int));
                break;
            case '%':
                vk_uart_putc(uart, '%');
                break;
            default:
                vk_uart_putc(uart, '%');
                vk_uart_putc(uart, (uint8_t)*fmt);
                break;
        }
    }
    va_end(ap);
}

/* ─── RX ─────────────────────────────────────────────────────────── */

int vk_uart_getc(vk_uart_t *uart) {
    if (uart->rx_head == uart->rx_tail)
        return -1;
    uint8_t c = uart->rx_buf[uart->rx_tail];
    uart->rx_tail = (uart->rx_tail + 1u) % VK_UART_RX_BUF;
    return c;
}

uint16_t vk_uart_readline(vk_uart_t *uart, char *buf, uint16_t len) {
    uint16_t n = 0;
    while (n < len - 1u) {
        int c;
        while ((c = vk_uart_getc(uart)) < 0)
            vk_task_yield();
        if (c == '\n' || c == '\r') break;
        buf[n++] = (char)c;
    }
    buf[n] = '\0';
    return n;
}

/* ─── ISR ────────────────────────────────────────────────────────── */

void vk_uart_isr(vk_uart_t *uart) {
    uint32_t isr = UREG(uart, UART_ISR);

    /* RX: byte received */
    if (isr & UART_ISR_RXNE) {
        uint8_t c = (uint8_t)UREG(uart, UART_RDR);
        uint16_t next = (uart->rx_head + 1u) % VK_UART_RX_BUF;
        if (next != uart->rx_tail) {  /* not full */
            uart->rx_buf[uart->rx_head] = c;
            uart->rx_head = next;
        }
        /* overrun silently dropped */
    }

    /* TX: data register empty */
    if (isr & UART_ISR_TXE) {
        if (uart->tx_tail != uart->tx_head) {
            _uart_tx_byte(uart, uart->tx_buf[uart->tx_tail]);
            uart->tx_tail = (uart->tx_tail + 1u) % VK_UART_TX_BUF;
        } else {
            /* Buffer drained — disable TXE interrupt */
            UREG(uart, UART_CR1) &= ~UART_CR1_TXEIE;
            uart->tx_busy = 0;
        }
    }

    /* Clear overrun flag if set */
    if (isr & UART_ISR_ORE)
        UREG(uart, UART_ICR) = (1u << 3);
}

/* ─── ISR dispatch — wire these to your vector table ─────────────── */
void USART3_IRQHandler(void) { vk_uart_isr(&vk_uart3); }
void USART1_IRQHandler(void) { vk_uart_isr(&vk_uart1); }
