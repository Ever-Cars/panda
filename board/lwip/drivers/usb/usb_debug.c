#include "usb_debug.h"
#include "stm32h7xx.h"

static uint8_t debug_elems_rx[FIFO_SIZE_DEBUG];

debug_ring_t debug_ring = {
  .w_ptr_rx = 0,
  .r_ptr_rx = 0,
  .elems_rx = debug_elems_rx,
  .rx_fifo_size = FIFO_SIZE_DEBUG,
};

bool get_char_debug(char *elem) {
  bool ret = false;
  __disable_irq();
  if (debug_ring.w_ptr_rx != debug_ring.r_ptr_rx) {
    if (elem != NULL) *elem = (char)debug_ring.elems_rx[debug_ring.r_ptr_rx];
    debug_ring.r_ptr_rx = (debug_ring.r_ptr_rx + 1U) % debug_ring.rx_fifo_size;
    ret = true;
  }
  __enable_irq();
  return ret;
}

bool injectc_debug(char elem) {
  bool ret = false;
  __disable_irq();
  uint16_t next_w_ptr = (debug_ring.w_ptr_rx + 1U) % debug_ring.rx_fifo_size;
  if (next_w_ptr == debug_ring.r_ptr_rx) {
    // overwrite oldest byte
    debug_ring.r_ptr_rx = (debug_ring.r_ptr_rx + 1U) % debug_ring.rx_fifo_size;
  }
  debug_ring.elems_rx[debug_ring.w_ptr_rx] = (uint8_t)elem;
  debug_ring.w_ptr_rx = next_w_ptr;
  ret = true;
  __enable_irq();
  return ret;
}

void putch(const char a) {
  (void)injectc_debug(a);
}

void print(const char *a) {
  for (const char *in = a; *in; in++) {
    if (*in == '\n') putch('\r');
    putch(*in);
  }
}

void puthx(uint32_t i, uint8_t len) {
  const char c[] = "0123456789abcdef";
  for (int pos = ((int)len * 4) - 4; pos > -4; pos -= 4) {
    putch(c[(i >> (unsigned int)(pos)) & 0xFU]);
  }
}

void puth(unsigned int i) {
  puthx(i, 8U);
}
