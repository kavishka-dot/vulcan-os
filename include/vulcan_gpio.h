#ifndef VULCAN_GPIO_H
#define VULCAN_GPIO_H

/*
 * vulcan_gpio.h — GPIO driver for STM32H743
 * Vulcan OS
 *
 * Covers: pin mode, output type, speed, pull, AF selection, atomic set/clear.
 * All operations are register-level; no STM32 HAL dependency.
 */

#include "vulcan.h"

/* ─── GPIO base addresses ─────────────────────────────────────────── */
#define GPIOA_BASE  0x58020000UL
#define GPIOB_BASE  0x58020400UL
#define GPIOC_BASE  0x58020800UL
#define GPIOD_BASE  0x58020C00UL
#define GPIOE_BASE  0x58021000UL
#define GPIOF_BASE  0x58021400UL
#define GPIOG_BASE  0x58021800UL
#define GPIOH_BASE  0x58021C00UL
#define GPIOI_BASE  0x58022000UL
#define GPIOJ_BASE  0x58022400UL
#define GPIOK_BASE  0x58022800UL

/* GPIO register layout (each port) */
typedef struct {
    volatile uint32_t MODER;    /* mode register */
    volatile uint32_t OTYPER;   /* output type */
    volatile uint32_t OSPEEDR;  /* output speed */
    volatile uint32_t PUPDR;    /* pull-up/pull-down */
    volatile uint32_t IDR;      /* input data */
    volatile uint32_t ODR;      /* output data */
    volatile uint32_t BSRR;     /* bit set/reset (atomic) */
    volatile uint32_t LCKR;     /* lock */
    volatile uint32_t AFR[2];   /* alternate function low/high */
} vk_gpio_port_t;

#define GPIOA  ((vk_gpio_port_t *)GPIOA_BASE)
#define GPIOB  ((vk_gpio_port_t *)GPIOB_BASE)
#define GPIOC  ((vk_gpio_port_t *)GPIOC_BASE)
#define GPIOD  ((vk_gpio_port_t *)GPIOD_BASE)
#define GPIOE  ((vk_gpio_port_t *)GPIOE_BASE)
#define GPIOF  ((vk_gpio_port_t *)GPIOF_BASE)
#define GPIOG  ((vk_gpio_port_t *)GPIOG_BASE)
#define GPIOH  ((vk_gpio_port_t *)GPIOH_BASE)
#define GPIOI  ((vk_gpio_port_t *)GPIOI_BASE)

/* ─── pin configuration enums ─────────────────────────────────────── */

typedef enum {
    VK_GPIO_INPUT     = 0,
    VK_GPIO_OUTPUT    = 1,
    VK_GPIO_AF        = 2,
    VK_GPIO_ANALOG    = 3,
} vk_gpio_mode_t;

typedef enum {
    VK_GPIO_PUSH_PULL  = 0,
    VK_GPIO_OPEN_DRAIN = 1,
} vk_gpio_otype_t;

typedef enum {
    VK_GPIO_SPEED_LOW    = 0,
    VK_GPIO_SPEED_MEDIUM = 1,
    VK_GPIO_SPEED_HIGH   = 2,
    VK_GPIO_SPEED_VHIGH  = 3,
} vk_gpio_speed_t;

typedef enum {
    VK_GPIO_NO_PULL   = 0,
    VK_GPIO_PULL_UP   = 1,
    VK_GPIO_PULL_DOWN = 2,
} vk_gpio_pull_t;

/* ─── pin descriptor ──────────────────────────────────────────────── */

typedef struct {
    vk_gpio_port_t *port;
    uint8_t         pin;    /* 0..15 */
} vk_pin_t;

/* Convenience constructors */
#define VK_PIN(port, pin)  ((vk_pin_t){ (port), (pin) })

/* Nucleo-H743ZI2 on-board LEDs */
#define VK_LED_GREEN   VK_PIN(GPIOB, 0)   /* LD1 */
#define VK_LED_YELLOW  VK_PIN(GPIOE, 1)   /* LD2 */
#define VK_LED_RED     VK_PIN(GPIOB, 14)  /* LD3 */

/* ─── RCC AHB4 enable bits for GPIO ports ─────────────────────────── */
#define RCC_AHB4ENR         (*(volatile uint32_t *)(0x58024400UL + 0xE0))
#define RCC_AHB4ENR_GPIOAEN (1u << 0)
#define RCC_AHB4ENR_GPIOBEN (1u << 1)
#define RCC_AHB4ENR_GPIOCEN (1u << 2)
#define RCC_AHB4ENR_GPIODEN (1u << 3)
#define RCC_AHB4ENR_GPIOEEN (1u << 4)
#define RCC_AHB4ENR_GPIOFEN (1u << 5)
#define RCC_AHB4ENR_GPIOGEN (1u << 6)
#define RCC_AHB4ENR_GPIOHEN (1u << 7)
#define RCC_AHB4ENR_GPIOIEN (1u << 8)

/* ─── API ─────────────────────────────────────────────────────────── */

/* Enable clock for a GPIO port (must call before any pin config) */
void vk_gpio_clk_enable(vk_gpio_port_t *port);

/* Configure a pin */
void vk_gpio_config(vk_pin_t pin,
                    vk_gpio_mode_t  mode,
                    vk_gpio_otype_t otype,
                    vk_gpio_speed_t speed,
                    vk_gpio_pull_t  pull);

/* Set alternate function (0..15) */
void vk_gpio_set_af(vk_pin_t pin, uint8_t af);

/* Digital output — atomic BSRR writes (IRQ-safe) */
VULCAN_INLINE void vk_gpio_set(vk_pin_t p) {
    p.port->BSRR = (1u << p.pin);
}
VULCAN_INLINE void vk_gpio_clear(vk_pin_t p) {
    p.port->BSRR = (1u << (p.pin + 16));
}
VULCAN_INLINE void vk_gpio_toggle(vk_pin_t p) {
    p.port->ODR ^= (1u << p.pin);
}

/* Digital input */
VULCAN_INLINE uint32_t vk_gpio_read(vk_pin_t p) {
    return (p.port->IDR >> p.pin) & 1u;
}

/* Shorthand: configure as push-pull output */
void vk_gpio_output(vk_pin_t pin, vk_gpio_speed_t speed);

/* Shorthand: configure as input with optional pull */
void vk_gpio_input(vk_pin_t pin, vk_gpio_pull_t pull);

#endif /* VULCAN_GPIO_H */
