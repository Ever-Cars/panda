#pragma once

#include <stdint.h>
#include <stdbool.h>

#define FIFO_SIZE_DEBUG 0x400U

typedef struct {
  volatile uint16_t w_ptr_rx;
  volatile uint16_t r_ptr_rx;
  uint8_t *elems_rx;
  uint32_t rx_fifo_size;
} debug_ring_t;

extern debug_ring_t debug_ring;

bool get_char_debug(char *elem);
bool injectc_debug(char elem);

void putch(const char a);
void print(const char *a);
void puth(unsigned int i);
void puthx(uint32_t i, uint8_t len);
