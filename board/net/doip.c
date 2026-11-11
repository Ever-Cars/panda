// DoIP carries UDS diagnostic bytes over Ethernet:
//   1. UDP discovery returns a vehicle's IP address and diagnostic address.
//   2. TCP connects to that IP; routing activation registers our tester address.
//   3. Diagnostic messages carry UDS requests/replies between logical addresses.
// A DoIP ACK confirms that a request was accepted for routing. The ECU's UDS
// reply is separate. Alive checks let the gateway check that we are still here.
// This module handles those steps; the consumer decides which UDS bytes to send.

#include <string.h>
#include "lwip/netif.h"
#include "lwip/autoip.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "net_api.h"
#include "doip.h"

static struct udp_pcb *doip_discovery_udp;
static struct udp_pcb *doip_announcement_udp;
static struct tcp_pcb *doip_tcp;
static doip_session_t doip_session;

// Read a logical address or payload type, stored in network byte order.
static uint16_t doip_read_u16(const uint8_t *data) {
  return ((uint16_t)data[0] << 8) | data[1];
}

// Read the four-byte payload length from a DoIP header.
static uint32_t doip_read_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

// Encode a logical address or payload type in network byte order.
static void doip_write_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
}

// Construct the common eight-byte envelope. The length excludes the header;
// the inverse version byte lets the receiver detect an invalid header format.
static void doip_write_header(uint8_t *data, uint8_t version, doip_payload_type_t type, uint16_t len) {
  data[DOIP_VERSION_OFFSET] = version;
  data[DOIP_INVERSE_VERSION_OFFSET] = version ^ DOIP_PROTOCOL_VERSION_INVERSE_MASK;
  doip_write_u16(&data[DOIP_PAYLOAD_TYPE_OFFSET], (uint16_t)type);
  data[DOIP_PAYLOAD_LENGTH_OFFSET] = 0U;
  data[DOIP_PAYLOAD_LENGTH_OFFSET + 1U] = 0U;
  doip_write_u16(&data[DOIP_PAYLOAD_LENGTH_OFFSET + 2U], len);
}

// Recognize supported versions and their inverse byte (for example, 02 FD).
static bool doip_header_version_valid(const uint8_t *header) {
  uint8_t version = header[DOIP_VERSION_OFFSET];
  uint8_t inverse = header[DOIP_INVERSE_VERSION_OFFSET];
  return (version >= DOIP_PROTOCOL_VERSION_2012) && (version <= DOIP_PROTOCOL_VERSION_2025) &&
         ((uint8_t)(version ^ inverse) == DOIP_PROTOCOL_VERSION_INVERSE_MASK);
}

// Report whether this family has a usable local address and an Ethernet link.
// IPv6 duplicate-address detection and IPv4 AutoIP probing finish independently.
bool doip_transport_ready(bool ipv6) {
  if ((netif_default == NULL) || !netif_is_up(netif_default) || !netif_is_link_up(netif_default)) {
    return false;
  }
  // On reconnection AutoIP re-probes the old address. A nonzero address alone
  // therefore does not mean it is safe to use it yet.
  return ipv6 ? ip6_addr_ispreferred(netif_ip6_addr_state(netif_default, 0)) : autoip_supplied_address(netif_default);
}

// Decode one vehicle-identification response. Active discovery replies and
// unsolicited announcements share this format; the final sync byte is optional.
static void doip_handle_announcement(const uint8_t *data, uint16_t len, const ip_addr_t *addr) {
  if (!doip_header_version_valid(data) ||
      (doip_read_u16(&data[DOIP_PAYLOAD_TYPE_OFFSET]) != DOIP_PAYLOAD_VEHICLE_IDENTIFICATION_RESPONSE) ||
      (doip_read_u32(&data[DOIP_PAYLOAD_LENGTH_OFFSET]) != (uint32_t)(len - DOIP_HEADER_SIZE))) {
    return;
  }

  const uint8_t *payload = &data[DOIP_HEADER_SIZE];
  doip_announcement_t announcement = {
    .address.ipv6 = IP_IS_V6(addr),
    .protocol_version = data[DOIP_VERSION_OFFSET],
    .logical_address = doip_read_u16(&payload[DOIP_VEHICLE_LOGICAL_ADDRESS_OFFSET]),
    .further_action = payload[DOIP_VEHICLE_FURTHER_ACTION_OFFSET],
    .has_sync_status = len == (DOIP_HEADER_SIZE + DOIP_VEHICLE_IDENTIFICATION_SYNC_SIZE),
  };
  if ((announcement.logical_address == DOIP_LOGICAL_ADDRESS_UNASSIGNED) ||
      (announcement.logical_address == DOIP_LOGICAL_ADDRESS_INVALID)) {
    return;
  }
  if (announcement.address.ipv6) {
    MEMCPY(announcement.address.bytes, ip_2_ip6(addr)->addr, DOIP_IPV6_ADDRESS_SIZE);
    announcement.address.zone = ip6_addr_zone(ip_2_ip6(addr));
  } else {
    MEMCPY(announcement.address.bytes, &ip_2_ip4(addr)->addr, DOIP_IPV4_ADDRESS_SIZE);
  }
  MEMCPY(announcement.vin, payload, DOIP_VIN_SIZE);
  MEMCPY(announcement.eid, &payload[DOIP_VEHICLE_EID_OFFSET], DOIP_EID_SIZE);
  MEMCPY(announcement.gid, &payload[DOIP_VEHICLE_GID_OFFSET], DOIP_GID_SIZE);
  if (announcement.has_sync_status) {
    announcement.sync_status = payload[DOIP_VEHICLE_SYNC_STATUS_OFFSET];
  }
  doip_transport_announcement_received(&announcement);
}

// Copy a complete UDP datagram out of lwIP's packet buffers for decoding.
// Ignore wrong-sized messages and senders that cannot be a unicast TCP peer.
static void doip_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
  (void)arg;
  (void)pcb;
  (void)port;  // Table 48 permits dynamically assigned reply ports.
  uint16_t len = p->tot_len;
  if (ip_addr_isany(addr) || ip_addr_ismulticast(addr) ||
      (IP_IS_V4(addr) && ip4_addr_isbroadcast(ip_2_ip4(addr), netif_default)) ||
      ((len != (DOIP_HEADER_SIZE + DOIP_VEHICLE_IDENTIFICATION_SIZE)) &&
       (len != (DOIP_HEADER_SIZE + DOIP_VEHICLE_IDENTIFICATION_SYNC_SIZE)))) {
    (void)pbuf_free(p);
    return;
  }
  uint8_t data[DOIP_HEADER_SIZE + DOIP_VEHICLE_IDENTIFICATION_SYNC_SIZE];
  (void)pbuf_copy_partial(p, data, len, 0U);
  doip_handle_announcement(data, len, addr);
  (void)pbuf_free(p);
}

// Open a UDP listener on our interface. ANY accepts IPv4 and IPv6; enabling
// broadcast reception allows announcements sent to the whole IPv4 link.
static struct udp_pcb *doip_udp_listen(uint16_t port) {
  struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb == NULL) {
    return NULL;
  }
  if (udp_bind(pcb, IP_ANY_TYPE, port) != ERR_OK) {
    udp_remove(pcb);
    return NULL;
  }
  udp_bind_netif(pcb, netif_default);
  udp_set_multicast_ttl(pcb, 1U);
  ip_set_option(pcb, SOF_BROADCAST);
  udp_recv(pcb, doip_udp_recv, NULL);
  return pcb;
}

// Keep two UDP listeners: our dynamic port receives discovery replies, while
// port 13400 receives announcements that vehicles send without being asked.
bool doip_transport_init(void) {
  if (doip_discovery_udp != NULL) {
    return true;
  }
  uint16_t port = (uint16_t)(DOIP_DYNAMIC_PORT_MIN + (net_rand() % DOIP_DYNAMIC_PORT_COUNT));
  doip_discovery_udp = doip_udp_listen(port);
  if (doip_discovery_udp == NULL) {
    return false;
  }
  doip_announcement_udp = doip_udp_listen(DOIP_UDP_DISCOVERY_PORT);
  if (doip_announcement_udp == NULL) {
    udp_remove(doip_discovery_udp);
    doip_discovery_udp = NULL;
    return false;
  }
  return true;
}

// Broadcast/multicast an unfiltered vehicle-identification request. Discovery
// uses version 0xff so the reply can tell us which protocol version to use.
bool doip_transport_discover(bool ipv6) {
  if ((doip_discovery_udp == NULL) || !doip_transport_ready(ipv6)) {
    return false;
  }
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, DOIP_HEADER_SIZE, PBUF_RAM);
  if (p == NULL) {
    return false;
  }
  ip_addr_t destination;
  ip_addr_t local;
  if (ipv6) {
    // FF02::1 reaches all nodes on this link. The zone selects our interface.
    ip_addr_set_zero_ip6(&destination);
    ip6_addr_set_allnodes_linklocal(ip_2_ip6(&destination));
    ip6_addr_assign_zone(ip_2_ip6(&destination), IP6_MULTICAST, netif_default);
    ip_addr_copy_from_ip6(local, *netif_ip6_addr(netif_default, 0));
  } else {
    ip_addr_copy_from_ip4(local, *netif_ip4_addr(netif_default));
    IP_ADDR4(&destination, 255U, 255U, 255U, 255U);  // Table 20: limited broadcast.
  }
  uint8_t request[DOIP_HEADER_SIZE];
  doip_write_header(request, DOIP_PROTOCOL_VERSION_DISCOVERY, DOIP_PAYLOAD_VEHICLE_IDENTIFICATION_REQUEST, 0U);
  err_t err = pbuf_take(p, request, sizeof(request));
  if (err == ERR_OK) {
    err = udp_sendto_if_src(doip_discovery_udp, p, &destination, DOIP_UDP_DISCOVERY_PORT, netif_default, &local);
  }
  (void)pbuf_free(p);
  return err == ERR_OK;
}

// Close the raw socket after detaching callbacks. Ordinary application closes
// are handled in poll(), so closing from a consumer callback is safe too.
static void doip_tcp_close_now(void) {
  if (doip_tcp != NULL) {
    struct tcp_pcb *pcb = doip_tcp;
    doip_tcp = NULL;
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
    }
  }
  doip_session.state = DOIP_CLOSED;
  doip_session.ack_pending = false;
  doip_session.alive_pending = false;
  doip_session.rx_len = 0U;
}

// Schedule an intentional close without notifying the consumer of a failure.
// Repeated calls and closing an already closed connection are harmless.
void doip_transport_close(void) {
  if (doip_session.state != DOIP_CLOSED) {
    doip_session.state = DOIP_CLOSING;
  }
}

// Preserve the first failure and defer socket teardown/notification to poll().
// This avoids freeing a PCB while lwIP is still using it in a receive callback.
static void doip_fail(doip_error_t error, int16_t detail) {
  if ((doip_session.state != DOIP_CLOSED) && (doip_session.state != DOIP_CLOSING) && (doip_session.state != DOIP_FAILED)) {
    doip_session.error = error;
    doip_session.detail = detail;
    doip_session.state = DOIP_FAILED;
  }
}

// Frame and queue a DoIP message. TCP owns a copy because it may retransmit
// after the caller's stack buffers have gone out of scope.
static bool doip_send_frame(doip_payload_type_t type, const uint8_t *payload, uint16_t len) {
  uint8_t data[DOIP_HEADER_SIZE + DOIP_MAX_PAYLOAD_SIZE];
  if ((doip_tcp == NULL) || (len > DOIP_MAX_PAYLOAD_SIZE)) {
    return false;
  }
  doip_write_header(data, doip_session.config.protocol_version, type, len);
  MEMCPY(&data[DOIP_HEADER_SIZE], payload, len);
  if (tcp_write(doip_tcp, data, DOIP_HEADER_SIZE + len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
    return false;
  }
  (void)tcp_output(doip_tcp);
  return true;
}

// Reject malformed supported messages as soon as their header is complete.
// Unknown types are consumed and ignored within the configured memory limit.
static bool doip_payload_length_valid(uint16_t type, uint32_t len) {
  switch (type) {
    case DOIP_PAYLOAD_HEADER_NACK:
      return len == DOIP_HEADER_NACK_SIZE;
    case DOIP_PAYLOAD_ROUTING_ACTIVATION_RESPONSE:
      return (len == DOIP_ROUTING_ACTIVATION_RESPONSE_SIZE) || (len == DOIP_ROUTING_ACTIVATION_RESPONSE_OEM_SIZE);
    case DOIP_PAYLOAD_ALIVE_CHECK_REQUEST:
      return len == 0U;
    case DOIP_PAYLOAD_DIAGNOSTIC_MESSAGE:
      return len >= DOIP_DIAGNOSTIC_MESSAGE_MIN_SIZE;
    case DOIP_PAYLOAD_DIAGNOSTIC_ACK:
    case DOIP_PAYLOAD_DIAGNOSTIC_NACK:
      return len >= DOIP_DIAGNOSTIC_ACK_SIZE;
    default:
      return true;
  }
}

// Accept routing activation only when the reply echoes our tester address and
// reports success. The gateway address in the reply may differ from our target.
static void doip_handle_activation(const uint8_t *payload) {
  uint16_t tester_address = doip_read_u16(&payload[DOIP_ROUTING_ACTIVATION_TESTER_ADDRESS_OFFSET]);
  uint8_t code = payload[DOIP_ROUTING_ACTIVATION_CODE_OFFSET];
  if (tester_address != doip_session.config.source_address) {
    doip_fail(DOIP_ERROR_INVALID_FRAME, DOIP_PAYLOAD_ROUTING_ACTIVATION_RESPONSE);
    return;
  }
  if (code != DOIP_ROUTING_ACTIVATION_SUCCESS) {
    doip_fail(DOIP_ERROR_ROUTING_ACTIVATION_REJECTED, code);
    return;
  }
  doip_session.state = DOIP_ACTIVE;
  doip_transport_connected();
}

// Deliver UDS replies and check ACK/NACKs for our request. Replies reverse the
// request's logical addresses: source is the ECU, target is our tester.
static void doip_handle_diagnostic(uint16_t type, const uint8_t *payload, uint16_t len) {
  uint16_t source = doip_read_u16(&payload[DOIP_SOURCE_ADDRESS_OFFSET]);
  uint16_t target = doip_read_u16(&payload[DOIP_TARGET_ADDRESS_OFFSET]);
  if ((source != doip_session.config.target_address) || (target != doip_session.config.source_address)) {
    return;
  }
  if (type == DOIP_PAYLOAD_DIAGNOSTIC_MESSAGE) {
    doip_transport_diagnostic_received(&payload[DOIP_DIAGNOSTIC_DATA_OFFSET], len - DOIP_DIAGNOSTIC_DATA_OFFSET);
    return;
  }
  if (!doip_session.ack_pending) {
    return;
  }

  // An ACK/NACK may echo some or all of the submitted UDS bytes after its code.
  uint16_t echo_len = len - DOIP_DIAGNOSTIC_ACK_SIZE;
  if ((echo_len > doip_session.request_len) || (memcmp(&payload[DOIP_DIAGNOSTIC_ACK_SIZE], doip_session.request, echo_len) != 0)) {
    doip_fail(DOIP_ERROR_DIAGNOSTIC_ACK_INVALID, type);
    return;
  }
  uint8_t code = payload[DOIP_DIAGNOSTIC_ACK_CODE_OFFSET];
  if (type == DOIP_PAYLOAD_DIAGNOSTIC_NACK) {
    doip_fail(DOIP_ERROR_DIAGNOSTIC_NACK, code);
    return;
  }
  if (code != DOIP_DIAGNOSTIC_ACK_OK) {
    doip_fail(DOIP_ERROR_DIAGNOSTIC_ACK_INVALID, code);
    return;
  }
  doip_session.ack_pending = false;
  doip_transport_acknowledged();
}

// Dispatch one complete, validated frame. Connection state determines which
// replies are useful; an alive-check request just queues a reply for poll().
static void doip_handle_frame(uint16_t type, const uint8_t *payload, uint16_t len) {
  switch (type) {
    case DOIP_PAYLOAD_ROUTING_ACTIVATION_RESPONSE:
      if (doip_session.state == DOIP_WAIT_ACTIVATION) {
        doip_handle_activation(payload);
      }
      break;
    case DOIP_PAYLOAD_DIAGNOSTIC_MESSAGE:
    case DOIP_PAYLOAD_DIAGNOSTIC_ACK:
    case DOIP_PAYLOAD_DIAGNOSTIC_NACK:
      if (doip_session.state == DOIP_ACTIVE) {
        doip_handle_diagnostic(type, payload, len);
      }
      break;
    case DOIP_PAYLOAD_ALIVE_CHECK_REQUEST:
      doip_session.alive_pending = true;
      doip_session.alive_started_ms = sys_now();
      break;
    default:
      // 7.DoIP-039/040: ignore unrelated messages, including header NACKs.
      // Clients do not send header NACKs or ACK the ECU's UDS responses.
      break;
  }
}

// Reassemble a TCP byte stream into DoIP frames. Headers and payloads may be
// fragmented across callbacks, or multiple complete frames may arrive together.
static void doip_receive_bytes(const uint8_t *data, uint16_t len) {
  for (uint16_t i = 0U; (i < len) && (doip_session.state != DOIP_FAILED) && (doip_session.state != DOIP_CLOSING); i++) {
    doip_session.rx[doip_session.rx_len++] = data[i];
    if (doip_session.rx_len < DOIP_HEADER_SIZE) {
      continue;
    }
    uint16_t type = doip_read_u16(&doip_session.rx[DOIP_PAYLOAD_TYPE_OFFSET]);
    uint32_t payload_len = doip_read_u32(&doip_session.rx[DOIP_PAYLOAD_LENGTH_OFFSET]);
    if (doip_session.rx_len == DOIP_HEADER_SIZE) {
      // Check the header before buffering any payload. Its length must fit our
      // buffer and the message format; TCP must use the discovered version.
      if (!doip_header_version_valid(doip_session.rx) ||
          (doip_session.rx[DOIP_VERSION_OFFSET] != doip_session.config.protocol_version) ||
          (payload_len > DOIP_MAX_PAYLOAD_SIZE) || !doip_payload_length_valid(type, payload_len)) {
        doip_fail(DOIP_ERROR_INVALID_FRAME, type);
        return;
      }
    }
    if (doip_session.rx_len == (DOIP_HEADER_SIZE + payload_len)) {
      doip_handle_frame(type, &doip_session.rx[DOIP_HEADER_SIZE], (uint16_t)payload_len);
      doip_session.rx_len = 0U;
    }
  }
}

// Record fatal TCP errors after lwIP has freed its socket state. Distinguish
// connection establishment from loss of an already established DoIP session.
static void doip_tcp_error(void *arg, err_t err) {
  (void)arg;
  doip_tcp = NULL;
  doip_fail(doip_session.state == DOIP_CONNECTING ? DOIP_ERROR_TCP_CONNECT : DOIP_ERROR_TCP_CLOSED, err);
}

// Consume every pbuf in a received chain and reopen TCP's receive window. A
// null pbuf is the peer's FIN; an abort here must return ERR_ABRT to lwIP.
static err_t doip_tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  (void)arg;
  if (p == NULL) {
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    doip_tcp = NULL;
    err_t ret = ERR_OK;
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
      ret = ERR_ABRT;
    }
    doip_fail(DOIP_ERROR_TCP_CLOSED, ERR_CLSD);
    return ret;
  }
  if (err == ERR_OK) {
    for (const struct pbuf *q = p; q != NULL; q = q->next) {
      doip_receive_bytes(q->payload, q->len);
    }
    tcp_recved(pcb, p->tot_len);
  }
  (void)pbuf_free(p);
  return ERR_OK;
}

// Advance from the TCP handshake to routing activation. TCP connectivity alone
// does not authorize diagnostic messages on the vehicle's DoIP connection.
static err_t doip_tcp_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
  (void)arg;
  (void)pcb;
  if (doip_session.state != DOIP_CONNECTING) {
    return ERR_OK;
  }
  if (err != ERR_OK) {
    doip_fail(DOIP_ERROR_TCP_CONNECT, err);
    return ERR_OK;
  }
  doip_session.state = DOIP_SEND_ACTIVATION;
  doip_session.started_ms = sys_now();
  return ERR_OK;
}

// Start a nonblocking connection using the advertised version and caller's
// logical addresses. IPv6 scope is preserved for link-local peers.
bool doip_transport_connect(const doip_address_t *address, const doip_config_t *config) {
  if ((address == NULL) || (config == NULL)) {
    return false;
  }
  if (!doip_transport_ready(address->ipv6) || (doip_tcp != NULL) || (doip_session.state != DOIP_CLOSED)) {
    return false;
  }
  if ((config->protocol_version < DOIP_PROTOCOL_VERSION_2012) || (config->protocol_version > DOIP_PROTOCOL_VERSION_2025) ||
      (config->source_address == DOIP_LOGICAL_ADDRESS_UNASSIGNED) || (config->source_address == DOIP_LOGICAL_ADDRESS_INVALID) ||
      (config->target_address == DOIP_LOGICAL_ADDRESS_UNASSIGNED) || (config->target_address == DOIP_LOGICAL_ADDRESS_INVALID)) {
    return false;
  }
  doip_tcp = tcp_new_ip_type(address->ipv6 ? IPADDR_TYPE_V6 : IPADDR_TYPE_V4);
  if (doip_tcp == NULL) {
    return false;
  }
  (void)memset(&doip_session, 0, sizeof(doip_session));
  doip_session.config = *config;
  doip_session.ipv6 = address->ipv6;
  doip_session.state = DOIP_CONNECTING;
  doip_session.started_ms = sys_now();
  tcp_recv(doip_tcp, doip_tcp_recv);
  tcp_err(doip_tcp, doip_tcp_error);
  ip_addr_t remote;
  ip_addr_t local;
  if (address->ipv6) {
    ip_addr_set_zero_ip6(&remote);
    MEMCPY(ip_2_ip6(&remote)->addr, address->bytes, DOIP_IPV6_ADDRESS_SIZE);
    ip6_addr_set_zone(ip_2_ip6(&remote), address->zone);
    if (ip6_addr_lacks_zone(ip_2_ip6(&remote), IP6_UNICAST)) {
      ip6_addr_assign_zone(ip_2_ip6(&remote), IP6_UNICAST, netif_default);
    }
    ip_addr_copy_from_ip6(local, *netif_ip6_addr(netif_default, 0));
  } else {
    IP_ADDR4(&remote, address->bytes[0], address->bytes[1], address->bytes[2], address->bytes[3]);
    ip_addr_copy_from_ip4(local, *netif_ip4_addr(netif_default));
  }
  // Bind to the chosen family's local address, as we did for UDP discovery.
  err_t err = tcp_bind(doip_tcp, &local, 0U);
  if (err == ERR_OK) {
    err = tcp_connect(doip_tcp, &remote, DOIP_TCP_DATA_PORT, doip_tcp_connected);
  }
  if (err != ERR_OK) {
    doip_tcp_close_now();
  }
  return err == ERR_OK;
}

// Send a diagnostic payload without exposing DoIP headers or logical addresses
// to USB/SPI consumers. Keep one unacknowledged request for ACK-echo validation.
bool doip_transport_send(const uint8_t *data, uint16_t len) {
  if ((doip_session.state != DOIP_ACTIVE) || doip_session.ack_pending || (data == NULL) ||
      (len == 0U) || (len > DOIP_MAX_DIAGNOSTIC_SIZE)) {
    return false;
  }
  uint8_t payload[DOIP_DIAGNOSTIC_DATA_OFFSET + DOIP_MAX_DIAGNOSTIC_SIZE];
  doip_write_u16(&payload[DOIP_SOURCE_ADDRESS_OFFSET], doip_session.config.source_address);
  doip_write_u16(&payload[DOIP_TARGET_ADDRESS_OFFSET], doip_session.config.target_address);
  MEMCPY(&payload[DOIP_DIAGNOSTIC_DATA_OFFSET], data, len);
  if (!doip_send_frame(DOIP_PAYLOAD_DIAGNOSTIC_MESSAGE, payload, DOIP_DIAGNOSTIC_DATA_OFFSET + len)) {
    return false;
  }
  MEMCPY(doip_session.request, data, len);
  doip_session.request_len = len;
  doip_session.ack_pending = true;
  doip_session.ack_started_ms = sys_now();
  return true;
}

// Drive routing activation, ACK/alive deadlines and deferred closes from the
// main loop. UDS services and their response timers belong to the consumer.
void doip_transport_poll(void) {
  if (doip_session.state == DOIP_CLOSING) {
    doip_tcp_close_now();
    return;
  }
  if (doip_session.state == DOIP_FAILED) {
    doip_error_t error = doip_session.error;
    int16_t detail = doip_session.detail;
    doip_tcp_close_now();
    doip_transport_failed(error, detail);
    return;
  }
  if (doip_session.state == DOIP_CLOSED) {
    return;  // UDP discovery remains available without a TCP connection.
  }
  if (!doip_transport_ready(doip_session.ipv6)) {
    doip_fail(DOIP_ERROR_LOCAL_ADDRESS_LOST, 0);
    return;
  }

  uint32_t now = sys_now();
  switch (doip_session.state) {
    case DOIP_CONNECTING:
      if ((now - doip_session.started_ms) >= DOIP_TCP_CONNECT_TIMEOUT_MS) {
        doip_fail(DOIP_ERROR_TCP_CONNECT_TIMEOUT, 0);
      }
      break;
    case DOIP_SEND_ACTIVATION: {
      // Register our tester before sending diagnostics. Default activation has
      // the tester address, activation type and four reserved zero bytes.
      uint8_t activation[DOIP_ROUTING_ACTIVATION_REQUEST_SIZE] = {0U};
      doip_write_u16(&activation[DOIP_ROUTING_ACTIVATION_TESTER_ADDRESS_OFFSET], doip_session.config.source_address);
      activation[DOIP_ROUTING_ACTIVATION_TYPE_OFFSET] = DOIP_ACTIVATION_TYPE_DEFAULT;
      if (doip_send_frame(DOIP_PAYLOAD_ROUTING_ACTIVATION_REQUEST, activation, sizeof(activation))) {
        doip_session.state = DOIP_WAIT_ACTIVATION;
        doip_session.started_ms = now;
      } else if ((now - doip_session.started_ms) >= DOIP_ROUTING_ACTIVATION_TIMEOUT_MS) {
        doip_fail(DOIP_ERROR_ROUTING_ACTIVATION_TIMEOUT, 0);
      }
      break;
    }
    case DOIP_WAIT_ACTIVATION:
      if ((now - doip_session.started_ms) >= DOIP_ROUTING_ACTIVATION_TIMEOUT_MS) {
        doip_fail(DOIP_ERROR_ROUTING_ACTIVATION_TIMEOUT, 0);
      }
      break;
    default:
      break;  // Once active, receive callbacks deliver diagnostic replies.
  }
  if (doip_session.state == DOIP_FAILED) {
    return;
  }

  // An alive-check reply contains only our tester address. Retry queueing it
  // until the short deadline if TCP's transmit buffers are temporarily full.
  if (doip_session.alive_pending) {
    uint8_t source[DOIP_ALIVE_CHECK_RESPONSE_SIZE];
    doip_write_u16(source, doip_session.config.source_address);
    if (doip_send_frame(DOIP_PAYLOAD_ALIVE_CHECK_RESPONSE, source, sizeof(source))) {
      doip_session.alive_pending = false;
    } else if ((now - doip_session.alive_started_ms) >= DOIP_ALIVE_CHECK_RESPONSE_TIMEOUT_MS) {
      doip_fail(DOIP_ERROR_ALIVE_CHECK_TIMEOUT, 0);
    }
  }
  // The DoIP ACK deadline is independent of any UDS response/pending messages.
  if (doip_session.ack_pending && ((now - doip_session.ack_started_ms) >= DOIP_DIAGNOSTIC_ACK_TIMEOUT_MS)) {
    doip_fail(DOIP_ERROR_DIAGNOSTIC_ACK_TIMEOUT, 0);
  }
}

// Provide a readable error for console users without making consumers decode
// DoIP wire values or lwIP errors. The callback also carries the original detail.
const char *doip_transport_error_name(doip_error_t error) {
  static const char *const names[] = {
    [DOIP_ERROR_LOCAL_ADDRESS_LOST] = "local address lost",
    [DOIP_ERROR_TCP_CONNECT] = "TCP connection not ready",
    [DOIP_ERROR_TCP_CONNECT_TIMEOUT] = "TCP connect timed out",
    [DOIP_ERROR_TCP_CLOSED] = "TCP disconnected",
    [DOIP_ERROR_ROUTING_ACTIVATION_REJECTED] = "routing activation rejected",
    [DOIP_ERROR_ROUTING_ACTIVATION_TIMEOUT] = "routing activation timed out",
    [DOIP_ERROR_DIAGNOSTIC_NACK] = "diagnostic NACK",
    [DOIP_ERROR_DIAGNOSTIC_ACK_INVALID] = "invalid diagnostic ACK",
    [DOIP_ERROR_DIAGNOSTIC_ACK_TIMEOUT] = "diagnostic ACK timed out",
    [DOIP_ERROR_ALIVE_CHECK_TIMEOUT] = "alive check response timed out",
    [DOIP_ERROR_INVALID_FRAME] = "invalid or oversized DoIP frame",
  };
  uint32_t index = (uint32_t)error;
  return index < (sizeof(names) / sizeof(names[0])) ? names[index] : "unknown DoIP error";
}
