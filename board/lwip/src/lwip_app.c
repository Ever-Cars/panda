/*
 * Copyright (c) 2025 Ever Cars
 */

#include "lwip_app.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "lwip_app.h"
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

// mem.c needs MEM_SIZE plus allocator metadata and alignment headroom.
uint8_t lwip_ram_heap[MEM_SIZE + 512U] __attribute__((aligned(32), section(".sram4.lwip_heap")));

static struct netif lwip_netif;


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
  IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
  IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
  IP4_ADDR(&gateway, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
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