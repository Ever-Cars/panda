#pragma once

#include <stdbool.h>
#include <stdint.h>

// Every DoIP packet starts with: version (1 byte), inverse version (1 byte),
// message type (2 bytes), payload length (4 bytes). All integers are big-endian;
// the payload length excludes this header. ISO 13400-2:2025, Tables 16-17.
#define DOIP_HEADER_SIZE 8U
#define DOIP_VERSION_OFFSET 0U
#define DOIP_INVERSE_VERSION_OFFSET 1U
#define DOIP_PAYLOAD_TYPE_OFFSET 2U
#define DOIP_PAYLOAD_LENGTH_OFFSET 4U
#define DOIP_PROTOCOL_VERSION_2012 0x02U
#define DOIP_PROTOCOL_VERSION_2025 0x04U
#define DOIP_PROTOCOL_VERSION_DISCOVERY 0xFFU
#define DOIP_PROTOCOL_VERSION_INVERSE_MASK 0xFFU

typedef enum {
  DOIP_PAYLOAD_HEADER_NACK = 0x0000U,
  DOIP_PAYLOAD_VEHICLE_IDENTIFICATION_REQUEST = 0x0001U,
  DOIP_PAYLOAD_VEHICLE_IDENTIFICATION_RESPONSE = 0x0004U,
  DOIP_PAYLOAD_ROUTING_ACTIVATION_REQUEST = 0x0005U,
  DOIP_PAYLOAD_ROUTING_ACTIVATION_RESPONSE = 0x0006U,
  DOIP_PAYLOAD_ALIVE_CHECK_REQUEST = 0x0007U,
  DOIP_PAYLOAD_ALIVE_CHECK_RESPONSE = 0x0008U,
  DOIP_PAYLOAD_DIAGNOSTIC_MESSAGE = 0x8001U,
  DOIP_PAYLOAD_DIAGNOSTIC_ACK = 0x8002U,
  DOIP_PAYLOAD_DIAGNOSTIC_NACK = 0x8003U,
} doip_payload_type_t;

// Discovery identifies a vehicle with its VIN, logical address and entity/group
// identifiers (EID/GID). Offsets are relative to the payload (Tables 5 and 7).
#define DOIP_VIN_SIZE 17U
#define DOIP_EID_SIZE 6U
#define DOIP_GID_SIZE 6U
#define DOIP_VEHICLE_IDENTIFICATION_SIZE 32U
#define DOIP_VEHICLE_IDENTIFICATION_SYNC_SIZE 33U
#define DOIP_VEHICLE_LOGICAL_ADDRESS_OFFSET 17U
#define DOIP_VEHICLE_EID_OFFSET 19U
#define DOIP_VEHICLE_GID_OFFSET 25U
#define DOIP_VEHICLE_FURTHER_ACTION_OFFSET 31U
#define DOIP_VEHICLE_SYNC_STATUS_OFFSET 32U
#define DOIP_LOGICAL_ADDRESS_UNASSIGNED 0x0000U
#define DOIP_LOGICAL_ADDRESS_INVALID 0xFFFFU
#define DOIP_FURTHER_ACTION_NONE 0x00U
#define DOIP_SYNC_STATUS_INCOMPLETE 0x10U

// Diagnostic payload: source address (2 bytes), target address (2 bytes), UDS.
// An ACK/NACK has a one-byte code in place of UDS, optionally followed by bytes
// echoed from the request. Tables 53-56.
#define DOIP_LOGICAL_ADDRESS_SIZE 2U
#define DOIP_SOURCE_ADDRESS_OFFSET 0U
#define DOIP_TARGET_ADDRESS_OFFSET 2U
#define DOIP_DIAGNOSTIC_DATA_OFFSET 4U
#define DOIP_DIAGNOSTIC_MESSAGE_MIN_SIZE (DOIP_DIAGNOSTIC_DATA_OFFSET + 1U)
#define DOIP_DIAGNOSTIC_ACK_SIZE 5U
#define DOIP_DIAGNOSTIC_ACK_CODE_OFFSET 4U
#define DOIP_DIAGNOSTIC_ACK_OK 0x00U

// Routing activation registers the tester's logical address with the gateway
// before diagnostics can be sent on the TCP connection (Tables 23-24).
#define DOIP_ACTIVATION_TYPE_DEFAULT 0x00U
#define DOIP_ROUTING_ACTIVATION_REQUEST_SIZE 7U
#define DOIP_ROUTING_ACTIVATION_RESPONSE_SIZE 9U
#define DOIP_ROUTING_ACTIVATION_RESPONSE_OEM_SIZE 13U
#define DOIP_ROUTING_ACTIVATION_TESTER_ADDRESS_OFFSET 0U
#define DOIP_ROUTING_ACTIVATION_TYPE_OFFSET 2U
#define DOIP_ROUTING_ACTIVATION_CODE_OFFSET 4U
#define DOIP_ROUTING_ACTIVATION_SUCCESS 0x10U

// Header errors carry one code byte. An alive-check reply carries our address.
// Tables 21 and 28.
#define DOIP_HEADER_NACK_SIZE 1U
#define DOIP_ALIVE_CHECK_RESPONSE_SIZE 2U

// Table 12 (client timings), sections 11.1 and 11.2 (ports).
#define DOIP_CONTROL_TIMEOUT_MS 2000U
#define DOIP_ROUTING_ACTIVATION_TIMEOUT_MS 2000U
#define DOIP_DIAGNOSTIC_ACK_TIMEOUT_MS 2000U
#define DOIP_ALIVE_CHECK_RESPONSE_TIMEOUT_MS 300U
#define DOIP_TCP_DATA_PORT 13400U
#define DOIP_UDP_DISCOVERY_PORT 13400U
#define DOIP_DYNAMIC_PORT_MIN 49152U
#define DOIP_DYNAMIC_PORT_COUNT 16384U
#define DOIP_IPV4_ADDRESS_SIZE 4U
#define DOIP_IPV6_ADDRESS_SIZE 16U

// Local memory/connection limits, separate from the protocol's timing values.
#define DOIP_TCP_CONNECT_TIMEOUT_MS 2000U
#define DOIP_MAX_PAYLOAD_SIZE 128U
// Leave room for a diagnostic ACK that echoes the entire submitted request.
#define DOIP_MAX_DIAGNOSTIC_SIZE (DOIP_MAX_PAYLOAD_SIZE - DOIP_DIAGNOSTIC_ACK_SIZE)

// The IP address locates the Ethernet peer. The separate 16-bit logical
// addresses inside DoIP messages select a diagnostic endpoint behind that peer.
// IPv6 link-local addresses also need a zone (our local interface index).
typedef struct {
  uint8_t bytes[DOIP_IPV6_ADDRESS_SIZE];
  uint8_t zone;
  bool ipv6;
} doip_address_t;

// Discovery returns both the vehicle identity and the address to connect to.
// VIN is a fixed-width byte array, not a null-terminated C string.
typedef struct {
  doip_address_t address;
  uint16_t logical_address;
  uint8_t protocol_version;
  uint8_t vin[DOIP_VIN_SIZE];
  uint8_t eid[DOIP_EID_SIZE];
  uint8_t gid[DOIP_GID_SIZE];
  uint8_t further_action;
  bool has_sync_status;
  uint8_t sync_status;
} doip_announcement_t;

// Our tester address and the ECU's diagnostic address go inside DoIP messages.
// The protocol version comes from discovery, rather than being assumed locally.
typedef struct {
  uint16_t source_address;
  uint16_t target_address;
  uint8_t protocol_version;
} doip_config_t;

typedef enum {
  DOIP_ERROR_LOCAL_ADDRESS_LOST,
  DOIP_ERROR_TCP_CONNECT,
  DOIP_ERROR_TCP_CONNECT_TIMEOUT,
  DOIP_ERROR_TCP_CLOSED,
  DOIP_ERROR_ROUTING_ACTIVATION_REJECTED,
  DOIP_ERROR_ROUTING_ACTIVATION_TIMEOUT,
  DOIP_ERROR_DIAGNOSTIC_NACK,
  DOIP_ERROR_DIAGNOSTIC_ACK_INVALID,
  DOIP_ERROR_DIAGNOSTIC_ACK_TIMEOUT,
  DOIP_ERROR_ALIVE_CHECK_TIMEOUT,
  DOIP_ERROR_INVALID_FRAME,
} doip_error_t;

// Normal flow: CLOSED -> CONNECTING -> SEND_ACTIVATION -> WAIT_ACTIVATION -> ACTIVE.
// CLOSING/FAILED defer cleanup until lwIP's callbacks return to the main loop.
typedef enum {
  DOIP_CLOSED,
  DOIP_CONNECTING,
  DOIP_SEND_ACTIVATION,
  DOIP_WAIT_ACTIVATION,
  DOIP_ACTIVE,
  DOIP_CLOSING,
  DOIP_FAILED,
} doip_state_t;

// One connection's progress, independent protocol timers and bounded storage.
typedef struct {
  doip_state_t state;
  doip_config_t config;
  bool ipv6;
  uint32_t started_ms;
  bool ack_pending;
  uint32_t ack_started_ms;
  bool alive_pending;
  uint32_t alive_started_ms;
  // Retain the last request to validate any bytes echoed by its ACK/NACK.
  uint16_t request_len;
  uint8_t request[DOIP_MAX_DIAGNOSTIC_SIZE];
  // TCP can split a DoIP frame anywhere, so keep bytes until it is complete.
  uint16_t rx_len;
  uint8_t rx[DOIP_HEADER_SIZE + DOIP_MAX_PAYLOAD_SIZE];
  doip_error_t error;
  int16_t detail;
} doip_session_t;

// One diagnostic connection, plus UDP listeners that remain available for both
// IP families. All calls/callbacks run on the main loop; no lwIP types cross this
// interface, so the same API can be called by later USB/SPI handlers.

// Has address configuration finished for this family on a live Ethernet link?
bool doip_transport_ready(bool ipv6);
// Open the UDP listeners once. They serve IPv4 and IPv6 simultaneously.
bool doip_transport_init(void);
// Send one discovery request; announcements arrive through the callback below.
bool doip_transport_discover(bool ipv6);
// Start TCP and routing activation; the connected callback signals completion.
bool doip_transport_connect(const doip_address_t *address, const doip_config_t *config);
// Queue UDS bytes. Wait for their DoIP ACK before submitting another request.
bool doip_transport_send(const uint8_t *data, uint16_t len);
// Schedule a close, including when called from a transport callback.
void doip_transport_close(void);
// Advance connection setup, service protocol deadlines and finish queued closes.
void doip_transport_poll(void);
// Describe a transport failure for logs or the caller's user interface.
const char *doip_transport_error_name(doip_error_t error);

// Implemented by the consumer (doip_example.h for now). Connected means routing
// activation succeeded, not merely that TCP connected. Diagnostic data contains
// only the UDS payload: the transport handles framing, addresses, ACKs and alive
// checks. Borrowed announcement/data buffers are valid only during the callback.
// Failures are reported from poll(), after the connection has been released.
void doip_transport_announcement_received(const doip_announcement_t *announcement);
void doip_transport_connected(void);
void doip_transport_acknowledged(void);
void doip_transport_diagnostic_received(const uint8_t *data, uint16_t len);
void doip_transport_failed(doip_error_t error, int16_t detail);
