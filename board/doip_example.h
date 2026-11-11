#pragma once

#include "board/net/doip.h"

// Included by main.c only. Run one read-DID test over IPv4, then IPv6.
// This blocking example keeps the network running while it waits; interrupts
// still service USB and CAN, but the main loop's LED animation pauses.
// DoIP framing and session handling stay in doip.c for later USB/SPI consumers.
#define DOIP_TEST_SOURCE_ADDRESS 0x0E80U
#define DOIP_TEST_DID 0xF17CU
#define DOIP_TEST_ADDRESS_TIMEOUT_MS 15000U
#define DOIP_TEST_UDS_RESPONSE_TIMEOUT_MS 2000U
// The ECU can announce itself before its TCP diagnostic listener is ready.
#define DOIP_TEST_ECU_STARTUP_TIMEOUT_MS 30000U
#define UDS_READ_DATA_BY_IDENTIFIER 0x22U
#define UDS_READ_DATA_BY_IDENTIFIER_RESPONSE 0x62U
#define UDS_READ_DID_PREFIX_SIZE 3U

static bool doip_test_ipv6;
static bool doip_test_discovering;
static bool doip_test_found;
static bool doip_test_connected;
static bool doip_test_ack;
static bool doip_test_response;
static const char *doip_test_error;
static doip_announcement_t doip_test_announcement;

// Pump the network without recursively entering doip_example_poll().
static void doip_test_service(void) {
  net_poll();
}

// Wait for a callback flag, or the whole interval when complete is NULL.
// Discovery uses the full response window to collect announcements.
static bool doip_test_wait(const bool *complete, uint32_t timeout_ms) {
  uint32_t started = sys_now();
  while (eth_is_link_up() && (doip_test_error == NULL) &&
         ((complete == NULL) || !*complete) && ((sys_now() - started) < timeout_ms)) {
    doip_test_service();
  }
  return eth_is_link_up() && (doip_test_error == NULL) && ((complete == NULL) || *complete);
}

// Print a VIN or serial number without assuming the bytes are null-terminated.
static void doip_test_print_bytes(const uint8_t *data, uint16_t len) {
  for (uint16_t i = 0U; i < len; i++) {
    putch(((data[i] >= 0x20U) && (data[i] <= 0x7EU)) ? (char)data[i] : '.');
  }
}

// Keep the first announcement for this family, as in scripts/doip/read_did.py.
void doip_transport_announcement_received(const doip_announcement_t *announcement) {
  if (doip_test_discovering && !doip_test_found && (announcement->address.ipv6 == doip_test_ipv6)) {
    doip_test_announcement = *announcement;
    doip_test_found = true;
    print("DoIP announcement: VIN "); doip_test_print_bytes(announcement->vin, DOIP_VIN_SIZE);
    print(" logical 0x"); puthx(announcement->logical_address, 4U); print("\n");
  }
}

// Routing activation is complete; the connection can now carry UDS requests.
void doip_transport_connected(void) {
  doip_test_connected = true;
}

// The gateway accepted the request. The UDS reply is a separate message.
void doip_transport_acknowledged(void) {
  doip_test_ack = true;
}

// Interpret only the positive read-DID reply needed by this example.
void doip_transport_diagnostic_received(const uint8_t *data, uint16_t len) {
  if ((len >= UDS_READ_DID_PREFIX_SIZE) && (data[0] == UDS_READ_DATA_BY_IDENTIFIER_RESPONSE) &&
      (data[1] == (uint8_t)(DOIP_TEST_DID >> 8)) && (data[2] == (uint8_t)DOIP_TEST_DID)) {
    print("DoIP DID 0xF17C: ");
    doip_test_print_bytes(&data[UDS_READ_DID_PREFIX_SIZE], len - UDS_READ_DID_PREFIX_SIZE);
    print("\n");
    doip_test_response = true;
  } else {
    doip_test_error = "unexpected UDS response";
  }
}

// A peer closing after the complete reply does not undo a successful read.
void doip_transport_failed(doip_error_t error, int16_t detail) {
  (void)detail;
  if ((error != DOIP_ERROR_TCP_CLOSED) || !doip_test_ack || !doip_test_response) {
    doip_test_error = doip_transport_error_name(error);
  }
}

// Report a transport error when available, otherwise the failed example step.
static bool doip_test_fail(const char *reason) {
  print("DoIP "); print(doip_test_ipv6 ? "IPv6" : "IPv4"); print(" test FAIL: ");
  print(doip_test_error != NULL ? doip_test_error : reason); print("\n");
  return false;
}

// Execute the same discovery -> connect -> read sequence for either IP family.
static bool doip_test_run(bool ipv6) {
  doip_test_ipv6 = ipv6;
  doip_test_error = NULL;
  doip_test_connected = false;
  doip_test_ack = false;
  doip_test_response = false;

  print("DoIP "); print(ipv6 ? "IPv6" : "IPv4"); print(" waiting for address\n");
  uint32_t started = sys_now();
  while (!doip_transport_ready(ipv6)) {
    if (!eth_is_link_up() || ((sys_now() - started) >= DOIP_TEST_ADDRESS_TIMEOUT_MS)) {
      return doip_test_fail("address configuration timed out");
    }
    doip_test_service();
  }
  if (!doip_transport_init()) {
    return doip_test_fail("UDP init failed");
  }

  // Retry discovery/connection while the ECU boots. Each discovery still gets
  // the spec's full response window; transport protocol timers are independent.
  started = sys_now();
  while ((sys_now() - started) < DOIP_TEST_ECU_STARTUP_TIMEOUT_MS) {
    doip_test_error = NULL;
    doip_test_found = false;
    doip_test_discovering = true;
    print("DoIP "); print(ipv6 ? "IPv6" : "IPv4"); print(" active discovery\n");
    bool discovered = doip_transport_discover(ipv6) && doip_test_wait(NULL, DOIP_CONTROL_TIMEOUT_MS);
    doip_test_discovering = false;
    if (!discovered) {
      return doip_test_fail("discovery failed");
    }
    if (!doip_test_found) {
      continue;
    }

    const doip_config_t config = {
      .source_address = DOIP_TEST_SOURCE_ADDRESS,
      .target_address = doip_test_announcement.logical_address,
      .protocol_version = doip_test_announcement.protocol_version,
    };
    print("DoIP connecting and activating routing\n");
    if (doip_transport_connect(&doip_test_announcement.address, &config) &&
        doip_test_wait(&doip_test_connected, DOIP_TCP_CONNECT_TIMEOUT_MS + DOIP_ROUTING_ACTIVATION_TIMEOUT_MS)) {
      break;
    }
    doip_transport_close();
    doip_transport_poll();
    doip_test_connected = false;
  }
  if (!doip_test_connected) {
    return doip_test_fail("ECU startup timed out");
  }

  const uint8_t request[] = {UDS_READ_DATA_BY_IDENTIFIER, (uint8_t)(DOIP_TEST_DID >> 8), (uint8_t)DOIP_TEST_DID};
  print("DoIP reading DID 0xF17C\n");
  if (!doip_transport_send(request, sizeof(request))) {
    return doip_test_fail("diagnostic send failed");
  }
  if (!doip_test_wait(&doip_test_response, DOIP_TEST_UDS_RESPONSE_TIMEOUT_MS) ||
      !doip_test_wait(&doip_test_ack, DOIP_DIAGNOSTIC_ACK_TIMEOUT_MS)) {
    return doip_test_fail("diagnostic reply timed out");
  }
  print("DoIP "); print(ipv6 ? "IPv6" : "IPv4"); print(" test PASS\n");
  return true;
}

// Run once on each link-up. IPv6 runs even when the IPv4 test fails.
static void doip_example_poll(void) {
  static bool link_was_up = false;
  if (!eth_is_link_up()) {
    link_was_up = false;
    return;
  }
  if (link_was_up) {
    return;
  }
  link_was_up = true;

  bool ipv4_passed = doip_test_run(false);
  doip_transport_close();
  doip_transport_poll();
  bool ipv6_passed = doip_test_run(true);
  doip_transport_close();
  doip_transport_poll();
  print("DoIP tests complete: IPv4 "); print(ipv4_passed ? "PASS" : "FAIL");
  print(", IPv6 "); print(ipv6_passed ? "PASS" : "FAIL"); print("\n");
}

// Assert the ECU activation line before waiting for Ethernet and ECU startup.
static void doip_example_init(void) {
  richie_set_doip_enabled(true);
  print("DoIP IPv4/IPv6 tests enabled; waiting for link/address\n");
}
