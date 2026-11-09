// lwIP integration: network interface on top of net_api.h, DHCP/AutoIP and
// IPv6 SLAAC, polled from the panda main loop.

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "netif/ethernet.h"

#include "net_api.h"
#include "net_echo.h"

// panda, board/drivers/uart.h and board/libc.h
extern void print(const char *a);
extern void puth(unsigned int i);
extern void assert_fatal(bool condition, const char *msg);

#define NET_RX_FRAMES_PER_POLL 8U

// Frames dropped because the lwIP heap was full, readable over SWD
uint32_t net_rx_no_mem;

static struct netif net_netif;
static bool net_link_up;
static uint32_t net_rand_state;

uint32_t net_rand(void) {
  // xorshift32, seeded from the unique ID and timer in net_init()
  uint32_t x = net_rand_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;

  net_rand_state = x;
  return x;
}

// A failed lwIP assertion means its state is corrupt and the failing operation
// must not resume. Take panda's fatal path: it never returns, so lwIP (which
// only runs in the main loop) is stopped for good, while interrupts (CAN,
// safety, USB/SPI, heartbeat) keep running. The LED fade stops as a visible sign.
void net_platform_assert(const char *msg, int line, const char *file) {
  print("lwIP assert at "); print(file); print(":0x"); puth((unsigned int)line); print("\n");
  assert_fatal(false, msg);
  while (true) {}  // assert_fatal(false, ...) hangs; this makes noreturn provable
}

u32_t sys_now(void) {
  return eth_millis();
}

static err_t net_linkoutput(struct netif *netif, struct pbuf *p) {
  (void)netif;
  err_t err = ERR_BUF;
  if (p->tot_len <= ETH_FRAME_MAX_LEN) {
    uint8_t *buf = eth_tx_buffer_get();  // counts ring-full in eth_state
    if (buf == NULL) {
      err = ERR_MEM;
    } else {
      (void)pbuf_copy_partial(p, buf, p->tot_len, 0);
      err = eth_tx_frame_send(p->tot_len) ? ERR_OK : ERR_IF;
    }
  }

  return err;
}

static err_t net_netif_init(struct netif *netif) {
  netif->name[0] = 'e';
  netif->name[1] = 'n';
  netif->hostname = "richie";

  netif->mtu = 1500;
  netif->mtu6 = 1500;
  netif->hwaddr_len = ETH_MAC_LEN;
  eth_get_mac(netif->hwaddr);

  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_MLD6;
  netif->output = etharp_output;
  netif->output_ip6 = ethip6_output;
  netif->linkoutput = net_linkoutput;
  return ERR_OK;
}

static void net_status_callback(struct netif *netif, netif_nsc_reason_t reason, const netif_ext_callback_args_t *args) {
  if ((reason & LWIP_NSC_IPV4_ADDRESS_CHANGED) != 0U) {
    char buf[IP4ADDR_STRLEN_MAX];
    print(dhcp_supplied_address(netif) ? "NET IPv4 (DHCP): " : "NET IPv4: ");
    print(ip4addr_ntoa_r(netif_ip4_addr(netif), buf, (int)sizeof(buf))); print("\n");
  }

  if ((reason & LWIP_NSC_IPV6_ADDR_STATE_CHANGED) != 0U) {
    s8_t i = args->ipv6_addr_state_changed.addr_index;
    if (ip6_addr_ispreferred(netif_ip6_addr_state(netif, i))) {
      char buf[IP6ADDR_STRLEN_MAX];
      print("NET IPv6: "); print(ip6addr_ntoa_r(netif_ip6_addr(netif, i), buf, (int)sizeof(buf))); print("\n");
    }
  }
}

void net_init(void) {
  net_rand_state = eth_random_seed() | 1U;  // xorshift state must be nonzero
  lwip_init();

  (void)netif_add(&net_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, net_netif_init, ethernet_input);
  netif_set_default(&net_netif);

  NETIF_DECLARE_EXT_CALLBACK(net_ext_callback);
  netif_add_ext_callback(&net_ext_callback, net_status_callback);

  netif_create_ip6_linklocal_address(&net_netif, 1);
  net_netif.ip6_autoconfig_enabled = 1;  // the setter macro null-checks a static address (-Waddress)

  // Administratively up; DHCP, AutoIP and SLAAC follow the link state
  netif_set_up(&net_netif);
  (void)dhcp_start(&net_netif);

  net_echo_init();
}

void net_poll(void) {
  bool link_up = eth_link_is_up();
  if (link_up != net_link_up) {
    net_link_up = link_up;
    if (link_up) {
      netif_set_link_up(&net_netif);
    } else {
      netif_set_link_down(&net_netif);
    }
  }

  for (uint32_t i = 0U; i < NET_RX_FRAMES_PER_POLL; i++) {
    uint16_t len = 0U;
    const uint8_t *frame = eth_rx_frame_get(&len);
    if (frame == NULL) {
      break;
    }

    // Received frames share the lwIP heap with transmit data (no pbuf pool)
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
    if (p != NULL) {
      (void)pbuf_take(p, frame, len);
    } else {
      net_rx_no_mem++;
    }
    eth_rx_frame_release();

    if ((p != NULL) && (net_netif.input(p, &net_netif) != ERR_OK)) {
      pbuf_free(p);
    }
  }

  sys_check_timeouts();
}
