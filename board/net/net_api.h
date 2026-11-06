#pragma once

// Boundary between panda (unity build, MISRA) and the lwIP side (separately
// compiled objects). Only plain C types cross this line: panda never sees
// lwIP types and lwIP never touches Ethernet hardware.

#include <stdbool.h>
#include <stdint.h>

#define ETH_MAC_LEN 6U
#define ETH_FRAME_MAX_LEN 1522U  // 1518 byte frame + 802.1Q tag, no FCS

// ***** Implemented by panda (board/drivers/eth.h) *****

// True while the PHY reports link and the MAC/DMA are running.
bool eth_link_is_up(void);

// MAC address derived from the MCU unique ID (locally administered, unicast).
void eth_get_mac(uint8_t mac[ETH_MAC_LEN]);

// Received frame without FCS, or NULL when none is pending. The frame stays
// valid until eth_rx_frame_release(), which must be called for every frame.
const uint8_t *eth_rx_frame_get(uint16_t *len);
void eth_rx_frame_release(void);

// DMA transmit buffer of ETH_FRAME_MAX_LEN bytes, or NULL while the TX ring is
// full. Fill it, then hand it to the DMA with eth_tx_frame_send().
uint8_t *eth_tx_buffer_get(void);
bool eth_tx_frame_send(uint16_t len);

// Wrap-safe millisecond clock derived from the microsecond timer.
uint32_t eth_millis(void);

// Entropy for the network stack's PRNG (unique ID mixed with timer).
uint32_t eth_random_seed(void);

// ***** Implemented by the lwIP side (board/net/net.c) *****

// Bring up lwIP, the network interface, DHCP/AutoIP and IPv6 SLAAC.
void net_init(void);

// Process received frames, link changes and lwIP timers. Main loop only.
void net_poll(void);
