#pragma once

#include <stdbool.h>
#include <stdint.h>

// drivers/gpio.h — used instead of HAL_GPIO_Init so panda's register_map stays in sync.
// Constants mirror board/drivers/gpio.h; keep in step if that file ever changes.
#define PANDA_MODE_OUTPUT 1U
#define PANDA_MODE_ALTERNATE 2U
#define PANDA_PULL_NONE 0U
#define PANDA_OUTPUT_TYPE_PUSH_PULL 0U

extern void set_gpio_mode(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void set_gpio_output(GPIO_TypeDef *GPIO, unsigned int pin, bool enabled);
extern void set_gpio_output_type(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int output_type);
extern void set_gpio_alternate(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void set_gpio_pullup(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void register_set(volatile uint32_t *addr, uint32_t val, uint32_t mask);
extern void register_set_bits(volatile uint32_t *addr, uint32_t val);