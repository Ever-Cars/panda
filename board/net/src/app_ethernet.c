/**
  ******************************************************************************
  * @file    LwIP/richie-ethernet/Src/app_ethernet.c
  * @author  MCD Application Team
  * @brief   Ethernet specific module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "lwip/opt.h"
#include "lwip_app.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif
#if LWIP_AUTOIP
#include "lwip/autoip.h"
#endif
#include "app_ethernet.h"
#include "ethernetif.h"
#include "lwip_port.h"

#if LWIP_IPV6
NETIF_DECLARE_EXT_CALLBACK(ipv6_status_callback)
#endif

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

uint32_t EthernetLinkTimer;

#if LWIP_DHCP
uint32_t DHCPfineTimer = 0;
uint8_t DHCP_state = DHCP_OFF;
#endif

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
#if LWIP_IPV6
static const char *ip6_addr_state_name(u8_t state)
{
  if (ip6_addr_istentative(state))
    return "tentative";
  if (ip6_addr_ispreferred(state))
    return "preferred";
  if (ip6_addr_isdeprecated(state))
    return "deprecated";
  if (ip6_addr_isduplicated(state))
    return "duplicated";
  return "invalid";
}

/**
  * @brief  Report IPv6 address state transitions (DAD, SLAAC, lifetime expiry)
  */
static void ethernet_ipv6_status_changed(struct netif *netif, netif_nsc_reason_t reason,
                                         const netif_ext_callback_args_t *args)
{
  char addrtxt[IP6ADDR_STRLEN_MAX];
  s8_t index;

  if (reason & LWIP_NSC_IPV6_ADDR_STATE_CHANGED)
  {
    index = args->ipv6_addr_state_changed.addr_index;
    ip6addr_ntoa_r(netif_ip6_addr(netif, index), addrtxt, sizeof(addrtxt));
    print("IPv6 address "); print(addrtxt); print(": ");
    print(ip6_addr_state_name(netif_ip6_addr_state(netif, index))); print("\n");
  }
}

/**
  * @brief  Enable IPv6 on the network interface: EUI-64 link-local address
  *         plus stateless autoconfiguration from Router Advertisements
  * @param  netif: the network interface
  * @retval None
  */
void ethernet_ipv6_init(struct netif *netif)
{
  netif_add_ext_callback(&ipv6_status_callback, ethernet_ipv6_status_changed);
  netif_create_ip6_linklocal_address(netif, 1);
  netif_set_ip6_autoconfig_enabled(netif, 1);
}
#endif /* LWIP_IPV6 */


/**
  * @brief  Notify the User about the network interface config status
  * @param  netif: the network interface
  * @retval None
  */
void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_link_up(netif))
  {
#if LWIP_DHCP
    /* Update DHCP state machine */
    DHCP_state = DHCP_START;
#else
    char *iptxt = ip4addr_ntoa(netif_ip4_addr(netif));
    print("Static IP address: "); print(iptxt); print("\n");
#endif /* LWIP_DHCP */
  }
  else
  {
#if LWIP_DHCP
    /* Update DHCP state machine */
    DHCP_state = DHCP_LINK_DOWN;
#else
    print("The network cable is not connected\n");
#endif /* LWIP_DHCP */
  }
}

#if LWIP_NETIF_LINK_CALLBACK
/**
  * @brief  Ethernet Link periodic check
  * @param  netif
  * @retval None
  */
void Ethernet_Link_Periodic_Handle(struct netif *netif)
{
  /* Ethernet Link every 100ms */
  if (HAL_GetTick() - EthernetLinkTimer >= 100)
  {
    EthernetLinkTimer = HAL_GetTick();
    ethernet_link_check_state(netif);
  }
}
#endif

#if LWIP_DHCP
/**
  * @brief  DHCP_Process_Handle
  * @param  None
  * @retval None
  */
void DHCP_Process(struct netif *netif)
{
  switch (DHCP_state)
  {
    case DHCP_START:
    {
      print("  State: Looking for DHCP server ...\n");
      ip_addr_set_zero_ip4(&netif->ip_addr);
      ip_addr_set_zero_ip4(&netif->netmask);
      ip_addr_set_zero_ip4(&netif->gw);
      dhcp_start(netif);
      DHCP_state = DHCP_WAIT_ADDRESS;
    }
    break;

  case DHCP_WAIT_ADDRESS:
    {
      if (dhcp_supplied_address(netif))
      {
        DHCP_state = DHCP_ADDRESS_ASSIGNED;
        char *iptxt = ip4addr_ntoa(netif_ip4_addr(netif));
        print("IP address assigned by a DHCP server: "); print(iptxt); print("\n");
      }
#if LWIP_AUTOIP
      else if (autoip_supplied_address(netif))
      {
        DHCP_state = DHCP_AUTOIP_ASSIGNED;
        char *iptxt = ip4addr_ntoa(netif_ip4_addr(netif));
        print("Auto IP address assigned: "); print(iptxt); print("\n");
      }
#endif /* LWIP_AUTOIP */
    }
    break;
#if LWIP_AUTOIP
  case DHCP_AUTOIP_ASSIGNED:
    {
      /* A late DHCP lease replaces the link-local address, and an AutoIP
         conflict drops it; either way go back to waiting for an address */
      if (dhcp_supplied_address(netif) || !autoip_supplied_address(netif))
      {
        DHCP_state = DHCP_WAIT_ADDRESS;
      }
    }
    break;
#endif /* LWIP_AUTOIP */
  case DHCP_LINK_DOWN:
    {
      DHCP_state = DHCP_OFF;
      print("The network cable is not connected\n");
    }
    break;
  default: break;
  }
}

/**
  * @brief  DHCP periodic check
  * @param  netif
  * @retval None
  */
void DHCP_Periodic_Handle(struct netif *netif)
{
  /* Fine DHCP periodic process every 500ms */
  if (HAL_GetTick() - DHCPfineTimer >= DHCP_FINE_TIMER_MSECS)
  {
    DHCPfineTimer =  HAL_GetTick();
    /* process DHCP state machine */
    DHCP_Process(netif);
  }
}
#endif


