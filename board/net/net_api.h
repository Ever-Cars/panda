#pragma once

// Boundary between panda (unity build) and the lwIP side (separately
// compiled objects). Only plain C types cross this line: panda never sees
// lwIP types and lwIP never touches Ethernet hardware.

#include <stdbool.h>
#include <stdint.h>

#define ETH_MAC_LEN 6U
#define ETH_FRAME_MAX_LEN 1522U  // 1518 byte frame + 802.1Q tag, no FCS
#define ETH_SERIAL_LEN 24U  // 96-bit MCU unique ID in hex, as panda's USB serial

// ***** Implemented by panda (board/drivers/eth.h) *****

// Reset and configure the PHY and MAC. Busy-waits ~40 ms for PHY power-up.
void eth_init(void);

// Link supervision, init retries and DMA error recovery. Main loop only.
void eth_service(void);

// True while the PHY reports link and the MAC/DMA are running.
bool eth_is_link_up(void);

// MAC address derived from the MCU unique ID (locally administered, unicast).
void eth_copy_mac(uint8_t mac[ETH_MAC_LEN]);

// Received frame without FCS, or NULL when none is pending. The frame stays
// valid until eth_rx_frame_release(), which must be called for every frame.
const uint8_t *eth_rx_frame_get(uint16_t *len);
void eth_rx_frame_release(void);

// DMA transmit buffer of ETH_FRAME_MAX_LEN bytes, or NULL while the TX ring is
// full. Fill it, then hand it to the DMA with eth_tx_frame_send().
uint8_t *eth_tx_buffer_get(void);
bool eth_tx_frame_send(uint16_t len);

// ***** Implemented by panda (board/net/net_platform.h) *****

// lwIP's millisecond clock, derived from the microsecond timer.
uint32_t sys_now(void);

// Panda serial (MCU unique ID as ETH_SERIAL_LEN hex characters) plus a NUL.
void net_get_serial(char serial[ETH_SERIAL_LEN + 1U]);

// ***** Implemented by the lwIP side (board/net/net.c) *****

// Bring up Ethernet and lwIP with concurrent IPv4 AutoIP/APIPA and IPv6
// link-local/SLAAC.
void net_init(void);

// Service Ethernet, then process received frames, link changes and lwIP
// timers. Main loop only.
void net_poll(void);
