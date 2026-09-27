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
#include "lwip_port.h"
#include "tcp_echoserver.h"


// mem.c needs MEM_SIZE plus allocator metadata and alignment headroom.
uint8_t lwip_ram_heap[MEM_SIZE + 512U] __attribute__((aligned(32), section(".axisram")));

static struct netif lwip_netif;

static void lwip_systic_init(void)
{
  // Use systick as time base source and configure 1ms tick
  // Set reload register
  SysTick->LOAD = (uint32_t)((CORE_CLOCK_HZ/1000U) - 1UL);
  // Set Priority for Systick Interrupt
  NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
  // Load the SysTick Counter Value
  SysTick->VAL = 0UL;
  // Enable SysTick IRQ and SysTick Timer
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

void lwip_clock_init(void) {
  // SYSCFG owns the MII/RMII selection.
  MODIFY_REG(SYSCFG->PMCR, SYSCFG_PMCR_EPIS_SEL, SYSCFG_ETH_RMII);

  // Enable only the Ethernet peripheral gates; do not alter PLLs or bus dividers.
  register_set_bits(&(RCC->AHB1ENR), RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN);

  // Richie can provide the LAN8742 XI clock from the 25 MHz HSE on PA8 (MCO1 = HSE / 1).
  set_gpio_pullup(GPIOA, 8, GPIO_NOPULL);
  set_gpio_alternate(GPIOA, 8, GPIO_AF0_MCO);
  register_set_bits(&(GPIOA->OSPEEDR), GPIO_OSPEEDR_OSPEED8);
  register_set_bits(&(RCC->CFGR), RCC_CFGR_MCO1_1 | RCC_CFGR_MCO1PRE_0);
}

bool lwip_stack_init(void) {
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gateway;

  lwip_clock_init();
  lwip_systic_init();

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