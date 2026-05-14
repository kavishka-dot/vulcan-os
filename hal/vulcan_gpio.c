/*
 * vulcan_gpio.c — GPIO driver for STM32H743
 * Vulcan OS
 */

#include "vulcan_gpio.h"

/* Map port pointer → AHB4ENR bit */
static uint32_t _port_clk_bit(vk_gpio_port_t *port) {
    switch ((uint32_t)port) {
        case GPIOA_BASE: return RCC_AHB4ENR_GPIOAEN;
        case GPIOB_BASE: return RCC_AHB4ENR_GPIOBEN;
        case GPIOC_BASE: return RCC_AHB4ENR_GPIOCEN;
        case GPIOD_BASE: return RCC_AHB4ENR_GPIODEN;
        case GPIOE_BASE: return RCC_AHB4ENR_GPIOEEN;
        case GPIOF_BASE: return RCC_AHB4ENR_GPIOFEN;
        case GPIOG_BASE: return RCC_AHB4ENR_GPIOGEN;
        case GPIOH_BASE: return RCC_AHB4ENR_GPIOHEN;
        case GPIOI_BASE: return RCC_AHB4ENR_GPIOIEN;
        default: return 0;
    }
}

void vk_gpio_clk_enable(vk_gpio_port_t *port) {
    RCC_AHB4ENR |= _port_clk_bit(port);
    /* Short delay for clock to propagate */
    (void)RCC_AHB4ENR;
}

void vk_gpio_config(vk_pin_t        pin,
                    vk_gpio_mode_t  mode,
                    vk_gpio_otype_t otype,
                    vk_gpio_speed_t speed,
                    vk_gpio_pull_t  pull)
{
    uint8_t p = pin.pin;
    vk_gpio_port_t *port = pin.port;

    /* MODER: 2 bits per pin */
    port->MODER  &= ~(3u << (p * 2));
    port->MODER  |=  ((uint32_t)mode  << (p * 2));

    /* OTYPER: 1 bit per pin */
    port->OTYPER &= ~(1u << p);
    port->OTYPER |=  ((uint32_t)otype << p);

    /* OSPEEDR: 2 bits per pin */
    port->OSPEEDR &= ~(3u << (p * 2));
    port->OSPEEDR |=  ((uint32_t)speed << (p * 2));

    /* PUPDR: 2 bits per pin */
    port->PUPDR  &= ~(3u << (p * 2));
    port->PUPDR  |=  ((uint32_t)pull  << (p * 2));
}

void vk_gpio_set_af(vk_pin_t pin, uint8_t af) {
    uint8_t p   = pin.pin;
    uint8_t reg = p >> 3;        /* AFR[0] for pins 0-7, AFR[1] for pins 8-15 */
    uint8_t pos = (p & 7u) * 4; /* 4 bits per pin in AFR */

    pin.port->AFR[reg] &= ~(0xFu << pos);
    pin.port->AFR[reg] |=  ((uint32_t)(af & 0xFu) << pos);
}

void vk_gpio_output(vk_pin_t pin, vk_gpio_speed_t speed) {
    vk_gpio_clk_enable(pin.port);
    vk_gpio_config(pin,
                   VK_GPIO_OUTPUT,
                   VK_GPIO_PUSH_PULL,
                   speed,
                   VK_GPIO_NO_PULL);
}

void vk_gpio_input(vk_pin_t pin, vk_gpio_pull_t pull) {
    vk_gpio_clk_enable(pin.port);
    vk_gpio_config(pin,
                   VK_GPIO_INPUT,
                   VK_GPIO_PUSH_PULL,
                   VK_GPIO_SPEED_LOW,
                   pull);
}
