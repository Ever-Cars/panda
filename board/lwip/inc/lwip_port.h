#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx.h"

extern void print(const char *a);

extern void set_gpio_mode(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void set_gpio_output(GPIO_TypeDef *GPIO, unsigned int pin, bool enabled);
extern void set_gpio_output_type(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int output_type);
extern void set_gpio_alternate(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void set_gpio_pullup(GPIO_TypeDef *GPIO, unsigned int pin, unsigned int mode);
extern void register_set(volatile uint32_t *addr, uint32_t val, uint32_t mask);
extern void register_set_bits(volatile uint32_t *addr, uint32_t val);