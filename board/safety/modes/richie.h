#pragma once

#include "board/safety/declarations.h"
#include "opendbc/safety/modes/defaults.h"

// Diagnostic requests are only allowed on the primary CAN bus.
#define RICHIE_DIAGNOSTIC_MSG(addr) {(addr), 0U, 8, false, false}

static safety_config richie_safety_init(uint16_t param) {
  static const CanMsg RICHIE_TX_MSGS[] = {
    RICHIE_DIAGNOSTIC_MSG(0x5B9),       // Mercedes EQB cluster
    RICHIE_DIAGNOSTIC_MSG(0x60A),       // Tesla BMS
    RICHIE_DIAGNOSTIC_MSG(0x6F1),       // BMW / MINI mixed addressing
    RICHIE_DIAGNOSTIC_MSG(0x714),       // Porsche cluster
    RICHIE_DIAGNOSTIC_MSG(0x726),       // Bifrost VIN probe
    RICHIE_DIAGNOSTIC_MSG(0x776),       // Audi cluster
    RICHIE_DIAGNOSTIC_MSG(0x7A2),       // Tesla vehicle information
    RICHIE_DIAGNOSTIC_MSG(0x7C6),       // Hyundai / Kia vehicle information
    RICHIE_DIAGNOSTIC_MSG(0x7DF),       // Functional VIN request
    RICHIE_DIAGNOSTIC_MSG(0x7E0),       // Ford / VAG VIN and vehicle information
    RICHIE_DIAGNOSTIC_MSG(0x7E2),       // Hyundai / Kia VIN
    RICHIE_DIAGNOSTIC_MSG(0x7E4),       // Ford / Hyundai / Kia battery
    RICHIE_DIAGNOSTIC_MSG(0x7E5),       // Audi battery / Mercedes VIN
    RICHIE_DIAGNOSTIC_MSG(0x7E7),       // Mercedes EQB battery
    RICHIE_DIAGNOSTIC_MSG(0x17FC0076),  // Porsche VIN probe
    RICHIE_DIAGNOSTIC_MSG(0x17FC007B),  // Porsche Taycan battery
    RICHIE_DIAGNOSTIC_MSG(0x18DA07F1),  // Bifrost 29-bit OBD VIN probe
    RICHIE_DIAGNOSTIC_MSG(0x1C400076),  // VW ID.Buzz cluster
    RICHIE_DIAGNOSTIC_MSG(0x1C40007B),  // VW ID.Buzz battery
    RICHIE_DIAGNOSTIC_MSG(0x1DD01635),  // Volvo / Polestar battery
    RICHIE_DIAGNOSTIC_MSG(0x1DD01A01),  // Volvo / Polestar vehicle information
  };

  safety_config cfg = nooutput_init(param);
  cfg.tx_msgs = RICHIE_TX_MSGS;
  cfg.tx_msgs_len = sizeof(RICHIE_TX_MSGS) / sizeof(RICHIE_TX_MSGS[0]);
  return cfg;
}

static bool richie_safety_tx_hook(const CANPacket_t *msg) {
  // The common TX check enforces the exact arbitration ID, bus and length.
  return (msg->fd == 0U) &&
         ((msg->extended != 0U) == (msg->addr > 0x7FFU));
}

const safety_hooks richie_hooks = {
  .init = richie_safety_init,
  .rx = default_rx_hook,
  .tx = richie_safety_tx_hook,
};
