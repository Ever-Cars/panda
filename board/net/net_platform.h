#pragma once

// Platform hooks the network stack needs from panda. Part of the panda build
// (main.c), called from the separately compiled lwIP glue through net_api.h.

#include "board/net/net_api.h"

uint32_t sys_now(void) {
  static uint32_t net_clock_ms = 0U;

  // Unsigned arithmetic handles timer wrap; sub-ms time carries forward.
  // Requires calls <71 min apart (normal lwIP polling) and no timer reset.
  net_clock_ms += (microsecond_timer_get() - (net_clock_ms * 1000U)) / 1000U;
  return net_clock_ms;
}

void net_get_serial(char serial[ETH_SERIAL_LEN + 1U]) {
  static const char hex[] = "0123456789abcdef";
  const uint8_t *uid = (const uint8_t *)UID_BASE;
  for (uint32_t i = 0U; i < (ETH_SERIAL_LEN / 2U); i++) {
    serial[2U * i] = hex[uid[i] >> 4];
    serial[(2U * i) + 1U] = hex[uid[i] & 0xFU];
  }

  serial[ETH_SERIAL_LEN] = '\0';
}
