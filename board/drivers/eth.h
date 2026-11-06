#pragma once

// Ethernet for Richie: LAN8742A PHY over RMII on the STM32H7 MAC.
// Polled from the main loop. The lwIP side reaches it only through
// board/net/net_api.h.

#include "board/net/net_api.h"
#include "board/stm32h7/lleth.h"

// Richie wiring, see richie_init_ethernet()
#define ETH_PHY_RESET_GPIO GPIOE
#define ETH_PHY_RESET_PIN 0U
#define ETH_PHY_ADDR 0U

// LAN8742A registers
#define ETH_PHY_BMSR 1U
#define ETH_PHY_ID1 2U
#define ETH_PHY_ID2 3U
#define ETH_PHY_SCSR 31U
#define ETH_PHY_BMSR_LINK (1UL << 2)
#define ETH_PHY_SCSR_AUTODONE (1UL << 12)
#define ETH_PHY_SCSR_100M (1UL << 3)
#define ETH_PHY_SCSR_FULL_DUPLEX (1UL << 4)

#define ETH_LINK_POLL_US 100000U
#define ETH_INIT_RETRY_US 1000000U  // each attempt busy-waits ~51 ms (PHY reset) + up to 10 ms
#define ETH_TX_WAIT_US 500U  // ~4 full-size frames at 100 Mbit/s

// Counters, readable over SWD without halting the core
typedef struct {
  bool initialized;
  bool link_up;
  bool speed_100m;
  bool full_duplex;
  uint16_t phy_id1;
  uint16_t phy_id2;
  uint32_t link_changes;
  uint32_t rx_frames;
  uint32_t rx_dropped;
  uint32_t tx_frames;
  uint32_t tx_ring_full;
  uint32_t phy_errors;
  uint32_t dma_errors;
  uint32_t dma_error_status;
  uint32_t init_failures;
} eth_state_t;

extern eth_state_t eth_state;
eth_state_t eth_state;

static uint8_t eth_mac[ETH_MAC_LEN];

static void eth_delay_us(uint32_t us) {
  uint32_t start = microsecond_timer_get();
  while (get_ts_elapsed(microsecond_timer_get(), start) < us) {}
}

// "initialized" means the MAC is reset and configured, ready for lleth_start().
// It is cleared whenever the DMA fails to stop cleanly, so the rings are never
// reprogrammed under a running DMA; eth_service() then retries the full init.
static void eth_stop(void) {
  if (!lleth_stop()) {
    eth_state.initialized = false;
  }
  eth_state.link_up = false;
}

// Reset the PHY and the MAC together. The PHY must be reset too: after losing
// its clock the LAN8742A can come back with a garbled RMII receive path (all
// frames fail CRC) until it is reset. It then drives the 50 MHz RMII reference
// clock that the MAC reset needs.
static void eth_try_init(void) {
  static uint32_t last_attempt_us = 0U;
  static bool attempted = false;
  uint32_t now = microsecond_timer_get();
  if (!attempted || (get_ts_elapsed(now, last_attempt_us) >= ETH_INIT_RETRY_US)) {
    attempted = true;
    eth_state.link_up = false;  // a reset always restarts from link down
    set_gpio_output(ETH_PHY_RESET_GPIO, ETH_PHY_RESET_PIN, false);
    eth_delay_us(1000U);
    set_gpio_output(ETH_PHY_RESET_GPIO, ETH_PHY_RESET_PIN, true);
    eth_delay_us(50000U);
    eth_state.initialized = lleth_init(eth_mac);
    last_attempt_us = microsecond_timer_get();
    if (!eth_state.initialized) {
      eth_state.init_failures++;
    }
  }
}

// Hash the 96-bit unique ID (FNV-1a 64) so chips from the same wafer, whose IDs
// differ in a few bits, still get well spread addresses.
static void eth_derive_mac(void) {
  const uint8_t *uid = (const uint8_t *)UID_BASE;
  uint64_t hash = 0xCBF29CE484222325ULL;
  for (uint32_t i = 0U; i < 12U; i++) {
    hash ^= uid[i];
    hash *= 0x100000001B3ULL;
  }
  eth_mac[0] = 0x02U;  // locally administered, unicast
  for (uint32_t i = 1U; i < ETH_MAC_LEN; i++) {
    eth_mac[i] = (uint8_t)(hash & 0xFFU);
    hash >>= 8U;
  }
}

// MDIO read that also fails when no PHY drives the bus (reads as all ones,
// which would otherwise look like "link up, autonegotiation done").
static bool eth_phy_read(uint32_t phy_reg, uint16_t *value) {
  bool ok = lleth_mdio_read(ETH_PHY_ADDR, phy_reg, value);
  return ok && (*value != 0xFFFFU);
}

static void eth_link_poll(void) {
  uint16_t bmsr = 0U;
  // Link status is latched low: the first read reports drops since the last poll
  bool ok = eth_phy_read(ETH_PHY_BMSR, &bmsr);
  ok = ok && eth_phy_read(ETH_PHY_BMSR, &bmsr);
  if (!ok) {
    eth_state.phy_errors++;
  }
  bool link = ok && ((bmsr & ETH_PHY_BMSR_LINK) != 0U);

  if (link && !eth_state.link_up) {
    uint16_t scsr = 0U;
    if (eth_phy_read(ETH_PHY_SCSR, &scsr) && ((scsr & ETH_PHY_SCSR_AUTODONE) != 0U)) {
      eth_state.speed_100m = (scsr & ETH_PHY_SCSR_100M) != 0U;
      eth_state.full_duplex = (scsr & ETH_PHY_SCSR_FULL_DUPLEX) != 0U;
      lleth_start(eth_state.speed_100m, eth_state.full_duplex);
      eth_state.link_up = true;
      eth_state.link_changes++;
      print("ETH link up, "); print(eth_state.speed_100m ? "100" : "10");
      print(eth_state.full_duplex ? " Mbit full duplex\n" : " Mbit half duplex\n");
    }
  } else if (!link && eth_state.link_up) {
    eth_stop();
    eth_state.link_changes++;
    print("ETH link down\n");
  } else {
    // no change
  }
}

void eth_init(void) {
  eth_derive_mac();
  eth_try_init();  // the board powers the PHY and holds it in reset until here
  if (eth_state.initialized) {
    bool ok = eth_phy_read(ETH_PHY_ID1, &eth_state.phy_id1);
    ok = ok && eth_phy_read(ETH_PHY_ID2, &eth_state.phy_id2);
    if (!ok) {
      eth_state.phy_errors++;
    }
    print("ETH init, PHY ID 0x"); puth(((uint32_t)eth_state.phy_id1 << 16) | eth_state.phy_id2); print("\n");
  } else {
    print("ETH init failed: MAC reset timed out (no RMII reference clock?), retrying\n");
  }
}

// Link supervision, MAC init retries and DMA error recovery. Main loop only.
void eth_service(void) {
  if (!eth_state.initialized) {
    eth_try_init();  // rate limited; link-up is only possible once this succeeds
  } else {
    static uint32_t eth_link_poll_last_us = 0U;
    uint32_t now = microsecond_timer_get();
    if (get_ts_elapsed(now, eth_link_poll_last_us) >= ETH_LINK_POLL_US) {
      eth_link_poll_last_us = now;
      eth_link_poll();
    }

    uint32_t status = lleth_dma_status();
    if ((status & ETH_DMACSR_FBE) != 0U) {
      // Fatal bus error stops the DMA: restart it on the next link poll
      eth_state.dma_errors++;
      eth_state.dma_error_status = status;
      eth_stop();
    }
  }
}

// Let the next received frame wake the main loop out of WFI (power save).
// Call after the network has been serviced: a frame that completed meanwhile
// has RI latched and wakes the loop immediately.
void eth_rx_wake_arm(void) {
  if (eth_state.link_up) {
    lleth_rx_irq_arm();
  }
}

// ***** net_api.h *****

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
bool eth_link_is_up(void) {
  return eth_state.link_up;
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
void eth_get_mac(uint8_t mac[ETH_MAC_LEN]) {
  (void)memcpy(mac, eth_mac, ETH_MAC_LEN);
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
const uint8_t *eth_rx_frame_get(uint16_t *len) {
  const uint8_t *frame = NULL;
  if (eth_state.link_up) {
    frame = lleth_rx_frame_get(len, &eth_state.rx_dropped);
    if (frame != NULL) {
      eth_state.rx_frames++;
    }
  }
  return frame;
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
void eth_rx_frame_release(void) {
  lleth_rx_frame_release();
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
uint8_t *eth_tx_buffer_get(void) {
  uint8_t *buf = NULL;
  if (eth_state.link_up) {
    uint32_t start = microsecond_timer_get();
    buf = lleth_tx_buffer_get();
    while ((buf == NULL) && (get_ts_elapsed(microsecond_timer_get(), start) < ETH_TX_WAIT_US)) {
      buf = lleth_tx_buffer_get();
    }
    if (buf == NULL) {
      eth_state.tx_ring_full++;
    }
  }
  return buf;
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
bool eth_tx_frame_send(uint16_t len) {
  bool ret = false;
  if (eth_state.link_up && (len <= ETH_FRAME_MAX_LEN) && (lleth_tx_buffer_get() != NULL)) {
    lleth_tx_frame_send(len);
    eth_state.tx_frames++;
    ret = true;
  }
  return ret;
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
uint32_t eth_millis(void) {
  static uint32_t eth_clock_last_us = 0U;
  static uint32_t eth_clock_rem_us = 0U;
  static uint32_t eth_clock_ms = 0U;
  uint32_t now = microsecond_timer_get();
  eth_clock_rem_us += get_ts_elapsed(now, eth_clock_last_us);
  eth_clock_last_us = now;
  eth_clock_ms += eth_clock_rem_us / 1000U;
  eth_clock_rem_us %= 1000U;
  return eth_clock_ms;
}

// cppcheck-suppress misra-c2012-8.7 ; called from the separately compiled lwIP glue (board/net)
uint32_t eth_random_seed(void) {
  const uint32_t *uid = (const uint32_t *)UID_BASE;
  return uid[0] ^ uid[1] ^ uid[2] ^ microsecond_timer_get();
}
