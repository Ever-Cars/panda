/*
 * Copyright (c) 2025 Ever Cars
 */

#include "lwip_app.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"

#include "app_ethernet.h"
#include "ethernetif.h"
#include "tcp_echoserver.h"

#define SRAM12_START 0x30000000UL
#define SRAM12_SIZE  (32UL * 1024UL)
#define AXISRAM_START 0x24000000UL
#define AXISRAM_SIZE  (320UL * 1024UL)

#define LWIP_IP_ADDR0 192U
#define LWIP_IP_ADDR1 168U
#define LWIP_IP_ADDR2 0U
#define LWIP_IP_ADDR3 10U
#define LWIP_NETMASK_ADDR0 255U
#define LWIP_NETMASK_ADDR1 255U
#define LWIP_NETMASK_ADDR2 255U
#define LWIP_NETMASK_ADDR3 0U
#define LWIP_GATEWAY_ADDR0 192U
#define LWIP_GATEWAY_ADDR1 168U
#define LWIP_GATEWAY_ADDR2 0U
#define LWIP_GATEWAY_ADDR3 1U

// mem.c needs MEM_SIZE plus allocator metadata and alignment headroom.
uint8_t lwip_ram_heap[MEM_SIZE + 512U]
  __attribute__((aligned(32), section(".axisram.lwip_heap")));

extern ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
extern ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
extern uint8_t memp_memory_RX_POOL_base[];

static struct netif lwip_netif;

static bool memory_range_is_within(const void *address, size_t size,
                                   uintptr_t region_start, size_t region_size) {
  const uintptr_t start = (uintptr_t)address;
  const uintptr_t region_end = region_start + region_size;

  return (start >= region_start) &&
         (start <= region_end) &&
         (size <= (region_end - start));
}

void lwip_clock_init(void) {
  // SYSCFG owns the MII/RMII selection.
  MODIFY_REG(SYSCFG->PMCR, SYSCFG_PMCR_EPIS_SEL, SYSCFG_ETH_RMII);

  // Enable only the Ethernet peripheral gates; do not alter PLLs or bus dividers.
  RCC->AHB1ENR |= RCC_AHB1ENR_ETH1MACEN |
                  RCC_AHB1ENR_ETH1TXEN |
                  RCC_AHB1ENR_ETH1RXEN;

  // Richie can provide the LAN8742 XI clock from the 25 MHz HSE on PA8.
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_1);
}

bool lwip_dma_memory_init(void) {
  const bool descriptors_valid =
    memory_range_is_within(DMARxDscrTab, sizeof(DMARxDscrTab),
                           SRAM12_START, SRAM12_SIZE) &&
    memory_range_is_within(DMATxDscrTab, sizeof(DMATxDscrTab),
                           SRAM12_START, SRAM12_SIZE);
  const bool rx_pool_valid =
    memory_range_is_within(memp_memory_RX_POOL_base, 1U,
                           SRAM12_START, SRAM12_SIZE);
  const bool heap_valid =
    memory_range_is_within(lwip_ram_heap, sizeof(lwip_ram_heap),
                           AXISRAM_START, AXISRAM_SIZE);

  if (descriptors_valid && rx_pool_valid && heap_valid) {
    memset(DMARxDscrTab, 0, sizeof(DMARxDscrTab));
    memset(DMATxDscrTab, 0, sizeof(DMATxDscrTab));
    memset(lwip_ram_heap, 0, sizeof(lwip_ram_heap));
    return true;
  }

  return false;
}

bool lwip_stack_init(void) {
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gateway;

  lwip_clock_init();

#if LWIP_DHCP
  ip_addr_set_zero_ip4(&ipaddr);
  ip_addr_set_zero_ip4(&netmask);
  ip_addr_set_zero_ip4(&gateway);
#else
  IP4_ADDR(&ipaddr, LWIP_IP_ADDR0, LWIP_IP_ADDR1, LWIP_IP_ADDR2, LWIP_IP_ADDR3);
  IP4_ADDR(&netmask, LWIP_NETMASK_ADDR0, LWIP_NETMASK_ADDR1,
           LWIP_NETMASK_ADDR2, LWIP_NETMASK_ADDR3);
  IP4_ADDR(&gateway, LWIP_GATEWAY_ADDR0, LWIP_GATEWAY_ADDR1,
           LWIP_GATEWAY_ADDR2, LWIP_GATEWAY_ADDR3);
#endif

  lwip_init();

  if (netif_add(&lwip_netif, &ipaddr, &netmask, &gateway, NULL,
                &ethernetif_init, &ethernet_input) == NULL) {
    return false;
  }

  netif_set_default(&lwip_netif);
  ethernet_link_status_updated(&lwip_netif);

#if LWIP_NETIF_LINK_CALLBACK
  netif_set_link_callback(&lwip_netif, ethernet_link_status_updated);
#endif

  tcp_echoserver_init();

  return true;
}

void lwip_poll(void) {
  ethernetif_input(&lwip_netif);
  sys_check_timeouts();

#if LWIP_NETIF_LINK_CALLBACK
  Ethernet_Link_Periodic_Handle(&lwip_netif);
#endif

#if LWIP_DHCP
  DHCP_Periodic_Handle(&lwip_netif);
#endif
}